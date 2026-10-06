/*
  ==============================================================================

    FxSlot.h
    UN hueco de efecto: el tipo que hay puesto, sus parametros, su ganancia y
    su mezcla. Namespace abd::dsp, modulo ABDShared::DspEffects.

    QUE HACE Y QUE NO. Hace de contenedor con estado de audio y de frontera
    wet/dry. NO hace de sistema de ruteo (eso es `FxEngine`), ni de politica de
    producto (eso se queda en el consumidor), ni de interfaz.

    LA FRONTERA WET/DRY, y por que esta AQUI y no en el efecto. Cada motor de
    este modulo mezcla como le parece: la reverb tiene `wetLevel` y `dryLevel`
    dentro, el coro mezcla por muestra, el delay devuelve solo la parte mojada.
    Un sistema de slots necesita UNA mezcla igual para todos, porque el panel
    tiene una perilla de mezcla por slot y el usuario espera que haga lo mismo
    con cualquier efecto. Si cada motor mezclase por su cuenta, la perilla solo
    tendria sentido en algunos. Asi que el slot mezcla, y para eso el efecto
    tiene que poder devolver la senal YA MOJADA: de ahi el `FxProcessFn`, que
    escribe en `outL/outR` y deja la decision al slot.

    Y POR QUE LA MEZCLA NO NECESITA COPIAR LA SECA. El mojado va a
    `wetBuffer_`, que es del slot, y la mezcla se hace en el mismo buffer de
    entrada leyéndolo antes de escribirlo. Copiar la seca seria gastar la mitad
    del ancho de banda de memoria del lazo de audio para nada.

    LO QUE NO SE PASA POR EL SLOT. La realimentacion, el limitador, la latencia
    y la reserva de buffers son del motor (`FxEngine`). Un slot no tiene
    realimentacion propia porque la del ruteo 9 es del motor entero.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/FxRegistry.h"

namespace abd::dsp
{

//==============================================================================
class FxSlot
{
public:
    FxSlot() = default;

    ~FxSlot() { destroy(); }

    FxSlot(const FxSlot&)            = delete;
    FxSlot& operator=(const FxSlot&) = delete;

    FxSlot(FxSlot&& other) noexcept { moveFrom(other); }
    FxSlot& operator=(FxSlot&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            moveFrom(other);
        }
        return *this;
    }

    /// Un hueco se puede mover (lo devuelve la fabrica) pero no copiar: copiarlo
    /// exigiria clonar el efecto, que es estado opaco con un destructor
    /// arbitrario. Por eso el constructor de copia esta eliminado.
    void moveFrom(FxSlot& other) noexcept
    {
        catalogue_     = other.catalogue_;
        catalogueSize_ = other.catalogueSize_;
        info_          = other.info_;
        effect_        = other.effect_;
        type_          = other.type_;
        gain_          = other.gain_;
        mix_           = other.mix_;
        sampleRate_    = other.sampleRate_;
        maxBlockSize_  = other.maxBlockSize_;

        for (int i = 0; i < kFxMaxParams; ++i)
        {
            params_[i]  = other.params_[i];
            touched_[i] = other.touched_[i];
        }

        wetBuffer_ = std::move(other.wetBuffer_);

        other.info_          = nullptr;
        other.effect_        = nullptr;
        other.catalogue_     = nullptr;
        other.catalogueSize_ = 0;
    }

    //==============================================================================
    /** Prepara el hueco y sus buffers. El catalogo puede cambiar entre llamadas,
        asi que se pasa aqui y no se deduce del tipo. */
    void prepare(double sampleRate, int numChannels, int maxBlockSize,
                 const FxEffectInfo* catalogue, int catalogueSize) noexcept
    {
        sampleRate_   = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlockSize_ = maxBlockSize > 0 ? maxBlockSize : 512;
        (void)numChannels; // los dos primeros canales; el resto no se toca

        catalogue_     = catalogue;
        catalogueSize_ = catalogueSize;

        // Antes de nada, lo que hubiera. `prepare` se puede llamar mas de una
        // vez (cambio de sample rate, cambio de catalogo) y crear encima de una
        // instancia viva la pierde: el puntero viejo se sobrescribe y su
        // memoria se queda sin dueño. No se oye, pero el detector de fugas del
        // modulo lo dice al final del programa, y con razon.
        destroy();

        wetBuffer_.setSize(2, maxBlockSize_);
        wetBuffer_.clear();

        // Los parametros por defecto son los de la fila del catalogo, que estan
        // en unidades fisicas y hay que pasar a normalizado. Se hace ANTES de
        // crear la instancia para que el efecto nazca ya en su sitio: un efecto
        // que nace con los parametros a cero y los recibe despues puede dar un
        // tictac en el primer bloque.
        syncDefaultsFromCatalogue();
        createInstance();
    }

    //==============================================================================
    /** Cambia el tipo del hueco. Destruye la instancia anterior y crea la
        nueva si la hay. Si el indice no vale, el hueco se queda en bypass.

        Y LOS MANDOS NO SE TOCAN. Es lo que hace un panel de verdad: el mando se
        queda donde lo puso el usuario y lo que cambia es que ese mando ahora
        controla otra cosa. La primera version si los reiniciaba a los valores
        por defecto de la fila nueva, que hacia que cambiar de efecto se
        llevara por delante el trabajo de ajustarlo — y el motor que se quedaba
        con los valores viejos, porque el `pushAllParams` de la creacion va
        DESPUES: los mandos de la pantalla y los del efecto no decian lo mismo.

        Los valores por defecto son los de `prepare`, que es donde nace el
        hueco. Despues, el usuario manda. */
    void setType(int type) noexcept
    {
        if (type == type_)
            return;

        type_ = type;
        destroy();
        syncDefaultsFromCatalogue();
        createInstance(); // `createInstance` reaplica los mandos que ya hay
    }

    int getType() const noexcept { return type_; }

    bool isActive() const noexcept { return effect_ != nullptr; }

    //==============================================================================
    /** Pone un parametro NORMALIZADO 0..1. Se guarda aunque no haya efecto:
        un parametro puesto antes de meter el efecto tiene que seguir ahi. */
    void setParameter(int index, float value) noexcept
    {
        if (index < 0 || index >= kFxMaxParams)
            return;

        // Y UN NaN NO ES UN VALOR DE MANDO. `jlimit` recorta comparando, y un
        // NaN no es mayor ni menor que nada, asi que pasaria de largo tal cual
        // y entraria en el estado del efecto. En un coro es un zumbido; en el
        // ruteo 9 es peor, porque `FxEngine` guarda la salida del bloque en su
        // cola de realimentacion y ese NaN vuelve en cada bloque, para siempre.
        //
        // `std::isfinite` no es libm: es una prueba de bits que el compilador
        // resuelve sin llamada, asi que el modulo sigue siendo libm-free.
        if (!std::isfinite(value))
            return;

        touched_[index] = true;

        const float v = jlimit(0.0f, 1.0f, value);
        if (v == params_[index])
            return;

        params_[index] = v;

        if (effect_ != nullptr && info_ != nullptr)
        {
            if (index < info_->numParams)
                info_->setParam(effect_, index, v);
        }
    }

    float getParameter(int index) const noexcept
    {
        return (index >= 0 && index < kFxMaxParams) ? params_[index] : 0.0f;
    }

    /** Si el usuario ha movido ese mando alguna vez. Un mando sin tocar sigue
        tomando el valor por defecto de la fila que haya en el hueco. */
    bool isParameterTouched(int index) const noexcept
    {
        return index >= 0 && index < kFxMaxParams && touched_[index];
    }

    // Los dos unten las manos: un valor que no es un numero se IGNORA y el
    // mando se queda como estaba, en vez de recortarse a un numero valido. Es
    // la unica diferencia con el recorte de verdad, que sigue igual.
    void setGain(float gain) noexcept
    {
        if (std::isfinite(gain)) gain_ = gain < 0.0f ? 0.0f : gain;
    }
    float getGain() const noexcept { return gain_; }

    void setMix(float mix) noexcept
    {
        if (std::isfinite(mix)) mix_ = jlimit(0.0f, 1.0f, mix);
    }
    float getMix() const noexcept { return mix_; }

    //==============================================================================
    /** Vacia el estado de audio del efecto sin tocar los mandos.

        NO es `setAllParams`. Reponer los parametros no limpia una cola que ya
        tiene contenido: un delay con la realimentacion a 0.6 sigue sonando las
        repeticiones que tenia guardadas. Por eso el contrato tiene `reset`, y lo
        que se repone despues son los mandos, no el estado. */
    void reset() noexcept
    {
        if (effect_ == nullptr || info_ == nullptr)
            return;

        if (info_->reset != nullptr)
            info_->reset(effect_);

        // Los mandos se reaplican porque un `reset()` de motor de efectos
        // significa "vuelve al estado de partida", y en un producto el estado
        // de partida son los mandos que el usuario tiene puestos. Perderlos
        // seria cambiar el sonido del plugin sin que nadie lo pidiera.
        pushAllParams();
    }

    //==============================================================================
    /**
        Procesa un bloque. `buffer` lleva la SECA; al salir lleva el resultado.

        Un hueco en bypass NO toca el buffer, ni de gains ni de mezcla: se lo
        deja al motor, que es quien decide si el bypass total es un bypass o un
       paso por la ganancia. Mezclar "casi en bypass" aqui haria que subir la
        ganancia de un slot en bypass cambiase el sonido, que es la clase de
        bug mas dificil de ver que tiene un sistema de efectos.
    */
    void process(AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        if (effect_ == nullptr || info_ == nullptr)
            return; // bypass: la senal pasa intacta

        if (buffer.getNumChannels() < 1)
            return;

        const int n = jmin(numSamples, jmin(maxBlockSize_, buffer.getNumSamples()));
        if (n <= 0)
            return;

        // Un buffer de un solo canal se procesa como MONO: se alimenta y se
        // recoge el canal 0 dos veces. Leer el canal 1 de un buffer de un canal
        // es un nullptr disfrazado, y el motor de ABDEep hacia justo eso.
        const int channels = buffer.getNumChannels();
        const float* inL   = buffer.getReadPointer(0);
        const float* inR   = channels > 1 ? buffer.getReadPointer(1) : inL;
        float* outL        = wetBuffer_.getWritePointer(0);
        float* outR        = wetBuffer_.getWritePointer(1);

        info_->process(effect_, inL, inR, outL, outR, n);

        float* bufL      = buffer.getWritePointer(0);
        const float seca = 1.0f - mix_;

        for (int i = 0; i < n; ++i)
            bufL[i] = inL[i] * seca + outL[i] * mix_ * gain_;

        if (channels > 1)
        {
            float* bufR = buffer.getWritePointer(1);
            for (int i = 0; i < n; ++i)
                bufR[i] = inR[i] * seca + outR[i] * mix_ * gain_;
        }
    }

    /** El efecto de este slot, para inspeccion. nullptr si esta en bypass. */
    const FxEffectInfo* getEffectInfo() const noexcept { return info_; }

private:
    void destroy() noexcept
    {
        if (effect_ != nullptr && info_ != nullptr && info_->destroy != nullptr)
            info_->destroy(effect_);
        effect_ = nullptr;
        info_   = nullptr;
    }

    void createInstance() noexcept
    {
        info_ = fxEffectAt(catalogue_, catalogueSize_, type_);
        if (info_ == nullptr || info_->create == nullptr)
        {
            info_ = nullptr;
            return;
        }

        effect_ = info_->create(sampleRate_);
        if (effect_ != nullptr)
            pushAllParams();
    }

    void pushAllParams() noexcept
    {
        if (info_ == nullptr)
            return;

        if (info_->setAllParams != nullptr)
        {
            info_->setAllParams(effect_, params_, info_->numParams);
            return;
        }

        for (int i = 0; i < info_->numParams; ++i)
            info_->setParam(effect_, i, params_[i]);
    }

    /**
        Los mandos por defecto de la fila que hay en el hueco, en normalizado.

        Y SOLO LOS QUE EL USUARIO NO HA TOCADO. Esa es la distincion que hace
        que las dos cosas que se esperan de un panel sean las dos a la vez:
        cuando metes un efecto, sus mandos vienen en su sitio — porque si no, un
        delay nace en 10 ms y una reverb nace en silencio— y cuando cambias de
        efecto, los mandos que habias ajustado se quedan donde los pusiste. Sin
        la marca de "tocado", una de las dos se pierde: reiniciar todo al
        cambiar de efecto tira el trabajo del usuario, y no reiniciar nada deja
        los mandos en 0 para siempre, que es un efecto que no suena.

        La marca se levanta en `setParameter`, no en el panel: asi funciona
        tambien para el `setAllParams` de un producto que los fija todos de
        golpe, y para un test que los pone uno a uno.
    */
    void syncDefaultsFromCatalogue() noexcept
    {
        const FxEffectInfo* info = fxEffectAt(catalogue_, catalogueSize_, type_);
        if (info == nullptr || info->params == nullptr)
            return;

        for (int i = 0; i < info->numParams && i < kFxMaxParams; ++i)
        {
            if (touched_[i])
                continue;

            params_[i] = fxNormalise(info->params[i], info->params[i].defaultValue);
        }
    }

    //--- Estado ------------------------------------------------------------
    const FxEffectInfo* catalogue_ = nullptr;
    int catalogueSize_             = 0;
    const FxEffectInfo* info_      = nullptr;
    void* effect_                  = nullptr;

    int type_                   = 0;
    float params_[kFxMaxParams] = {};
    bool touched_[kFxMaxParams] = {};
    float gain_                 = 1.0f;
    float mix_                  = 0.5f;

    double sampleRate_ = 44100.0;
    int maxBlockSize_  = 512;

    AudioBuffer<float> wetBuffer_;
};

} // namespace abd::dsp
