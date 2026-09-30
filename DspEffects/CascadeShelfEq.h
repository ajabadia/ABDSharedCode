/*
  ==============================================================================

    CascadeShelfEq.h
    DOS repisas en cascada, con los numeros de la maquina inyectados.
    Namespace abd::dsp, modulo ABDShared::DspEffects.

    QUE ES. La maquina que compone dos `ShelfFilter` en serie, y que toma del
    perfil SOLO la tabla de frecuencias. La panaden las reglas; el perfil pone
    los numeros; el motor —el biquad— lo pone `ShelfFilter`, que ya estaba.

        dsp::CascadeShelfEq<dsp::MS2000EqProfile> eq;
        eq.prepare (sampleRate);
        eq.setLowIndex (1);
        eq.processFrame (left, right);

    POR QUE UNA MAQUINA PARA "DOS LLAMADAS". Porque esas dos llamadas estaban en
    un shim de producto sin una sola comprobacion que las mirara, y porque el
    patron ya existe aqui: `MultiHeadEcho<Re201Profile, TapeColour>` hace
    exactamente lo mismo con las posiciones de cabezal de un eco. Compuesta de N
    repisas, tabla de N maquinas, motor una vez.

    Y CONCRETO: un producto con un ecualizador de dos bandas donde las dos
    posiciones del panel esten fijas por hardware puede instanciar esta con su
    perfil, y el motor no cambia.

    QUE NO HACE. No conoce el MS2000. No tiene tablas. No sabe que existen
    cuatro posiciones: si le das 4 a un perfil de cuatro, se recorta a 3, y si
    le das 4 a uno de ocho, se lo come. El recorte es de la maquina porque es
    una regla, y una regla no es de nadie en concreto.

    LA TRAMPA DE LOS DOS MANDOS, Y POR QUE ESTAN SEPARADOS. `setGainDB` NO
    reescribe la frecuencia. En el shim de ABDMS2000 eso estaba escrito en un
    comentario como si fuera una casualidad, y no lo es: si al pedir +0,01 dB
    se reescribiera tambien la frecuencia, moveriamos la que el usuario tiene
    puesta ahi desde hace rato. Son dos mandos y van por dos caminos. La
    frecuencia solo se reescribe cuando cambia el indice, y ahi si se comprueba
    que haya cambiado, porque recalcular el biquad sin motivo es lo que hace que
    una automatizacion de ganancia cargue la CPU.

    LO QUE ES IGUAL Y LO QUE NO. El motor es `ShelfFilter` sin tocar: mismo Q
    de Butterworth, misma banda muerta de 0,05 dB, mismo tope de 0,45 veces el
    sample rate. O sea, la cadena de dos bandas suena igual que antes, y la
    diferencia de libm que trae la migracion esta medida y fijada en el banco
    (ver `namespace frozen` en `DspEffectsTests.cpp`).

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/ShelfFilter.h"

namespace abd::dsp
{

//==============================================================================
/** Dos repisas en cascada: una baja y una alta, con estado separado por canal.

    @tparam Profile  Solo numeros: `numPositions`, `lowFreqs`, `highFreqs`,
                     `defaultLowIndex`, `defaultHighIndex`. El perfil puede no
                     tener tablas (vease `setLowFrequencyHz`), en cuyo caso solo
                     hacen falta los dos indices por defecto. */
template <typename Profile>
class CascadeShelfEq
{
public:
    CascadeShelfEq() = default;
    ~CascadeShelfEq() = default;

    //==============================================================================
    /** Prepara las dos repisas y las deja en los indices de fabrica del perfil.

        Los modos se fijan AQUI y no en cada cambio: son fijos por construccion,
        y un `setMode` por bloque seria trabajo de la nada. */
    void prepare (double sampleRate) noexcept
    {
        bajo_.prepare (sampleRate);
        alto_.prepare (sampleRate);

        bajo_.setMode (ShelfMode::Low);
        alto_.setMode (ShelfMode::High);

        lowIndex_  = Profile::defaultLowIndex;
        highIndex_ = Profile::defaultHighIndex;

        // Y LAS DOS PUERTAS SE ABREN. `prepare` es un arranque, no un cambio de
        // mando: si el producto habia escrito un hercio a mano en el uso
        // anterior, `prepare` significa "vuelve a la tabla del perfil". Sin
        // estas dos lineas, `aplicaFrecuencias` se encuentra con las dos
        // banderas en `false` y no hace nada, y la maquina se queda con las
        // frecuencias del uso anterior mientras `getLowIndex` dice lo contrario.
        lowUsaTabla_  = true;
        highUsaTabla_ = true;

        aplicaFrecuencias();
        bajo_.setGainDB (Profile::defaultLowGainDb);
        alto_.setGainDB (Profile::defaultHighGainDb);
    }

    /** Vacia el estado de audio de las dos. Los coeficientes se quedan: un
        `reset` no es un `prepare` y no debe reasignar nada. */
    void reset() noexcept
    {
        bajo_.reset();
        alto_.reset();
    }

    //==============================================================================
    /** El indice de la repisa baja, 0..numPositions-1. */
    void setLowIndex (int index) noexcept
    {
        const int recortado = recorte (index);

        // EL "HA CAMBIADO?" MIRA TAMBIEN SI MANDA LA TABLA. Con la guarda de
        // solo el indice, escribir un hercio a mano y luego pedir el indice que
        // ya estaba no hacia NADA: la puerta continua se quedaba abierta para
        // siempre y el selector parecia roto. Elegir por indice cierra la
        // puerta, cambie el indice o no.
        if (recortado == lowIndex_ && lowUsaTabla_)
            return;

        lowIndex_ = recortado;
        lowUsaTabla_ = true;
        aplicaFrecuencias();
    }

    void setHighIndex (int index) noexcept
    {
        const int recortado = recorte (index);

        if (recortado == highIndex_ && highUsaTabla_)
            return;

        highIndex_ = recortado;
        highUsaTabla_ = true;
        aplicaFrecuencias();
    }

    int getLowIndex() const noexcept  { return lowIndex_; }
    int getHighIndex() const noexcept { return highIndex_; }

    //==============================================================================
    /** Frecuencia CONTINUA en hercios, saltandose la tabla del perfil.

        Es la puerta para un producto que no tiene selector de hardware: la
        maquina no obliga a usar las tablas, solo las ofrece.

        CADA REPISA CON SU PROPIA PUERTA, y la politica es "manda la ultima
        puerta usada DE ESA REPISA". Un producto con dial continuo no tiene
        selector que tocar; uno con selector vuelve a la tabla en cuanto lo
        toca, y por eso `setLowIndex`/`setHighIndex` reencienden la tabla: si
        no, la primera vez que un producto escribiese un hercio a mano, su
        tabla quedaria muerta para siempre, que es justo lo contrario de lo que
        se ofrece.

        Y POR QUE DOS Y NO UNA. Con una sola bandera para las dos repisas, tocar
        el selector de la ALTA se llevaba por delante la frecuencia continua de
        la BAJA, sin avisar: `aplicaFrecuencias` escribia las dos y el hercio
        que el producto acababa de poner se perdia. Y no siempre: solo cuando el
        indice de la alta casualmente no era el que ya estaba, porque la guarda
        de "no ha cambiado" cortaba antes. Dos banderas quitan las dos cosas. */
    void setLowFrequencyHz (float hz) noexcept
    {
        // El "ha cambiado?" lo decide `ShelfFilter`, que es quien conoce el
        // techo de 0,45 veces el sample rate. Aqui no se puede repetir ese
        // recorte sin inventar un techo: es la unica razon por la que este
        // setter no lleva su propia guarda.
        bajo_.setFrequencyHz (hz);
        lowUsaTabla_ = false;
    }

    void setHighFrequencyHz (float hz) noexcept
    {
        alto_.setFrequencyHz (hz);
        highUsaTabla_ = false;
    }

    float getLowFrequencyHz() const noexcept  { return bajo_.getFrequencyHz(); }
    float getHighFrequencyHz() const noexcept { return alto_.getFrequencyHz(); }

    //==============================================================================
    /** Ganancia en dB. NO toca la frecuencia: son dos mandos y van por dos
        caminos (ver la nota de la cabecera). */
    void setLowGainDB (float db) noexcept  { bajo_.setGainDB (db); }
    void setHighGainDB (float db) noexcept { alto_.setGainDB (db); }

    float getLowGainDB() const noexcept  { return bajo_.getGainDB(); }
    float getHighGainDB() const noexcept { return alto_.getGainDB(); }

    //==============================================================================
    /** Un par, con las dos repisas en cascada. */
    void processFrame (float& left, float& right) noexcept
    {
        bajo_.processFrame (left, right);
        alto_.processFrame (left, right);
    }

    /** Una muestra, in situ. El estado es el del canal izquierdo, que es lo que
        hace `ShelfFilter::processSample`; un producto monofono que alterna no
        nota la diferencia, y uno que no, deberia usar `processFrame`. */
    float processSample (float& x) noexcept
    {
        bajo_.processSample (x);
        return alto_.processSample (x);
    }

    //==============================================================================
    /** Las dos repisas, para poder mirar o medir el motor. */
    const ShelfFilter& getLowShelf() const noexcept  { return bajo_; }
    const ShelfFilter& getHighShelf() const noexcept { return alto_; }

private:
    //==============================================================================
    static int recorte (int index) noexcept
    {
        // Sin esta linea, un perfil con `numPositions == 0` recortaba a -1 y
        // `aplicaFrecuencias` leia `lowFreqs[-1]`. Un perfil sin posiciones es
        // un error de quien lo escribe, y aqui se dice al escribirlo.
        static_assert (Profile::numPositions > 0,
                       "CascadeShelfEq necesita un perfil con al menos una posicion");
        return index < 0 ? 0
             : (index > Profile::numPositions - 1) ? Profile::numPositions - 1
             : index;
    }

    void aplicaFrecuencias() noexcept
    {
        // UNA ESCRITURA POR REPISA, Y SOLO LA QUE LA PIDE. Una bandera comun
        // escribia las dos frecuencias cada vez que se tocaba cualquiera de los
        // dos selectores, y eso hacia que el selector de la alta borrase el
        // hercio continuo de la baja. Ver la nota de `setLowFrequencyHz`.
        if (lowUsaTabla_)
            bajo_.setFrequencyHz (Profile::lowFreqs [lowIndex_]);

        if (highUsaTabla_)
            alto_.setFrequencyHz (Profile::highFreqs[highIndex_]);
    }

    //--- Estado ------------------------------------------------------------
    ShelfFilter bajo_;
    ShelfFilter alto_;

    int lowIndex_  = 0;
    int highIndex_ = 0;

    /** `false` mientras mande la puerta de hercios continuos DE ESA REPISA.
        Mientras sea `false`, su `getXIndex` es un recuerdo, no la frecuencia. */
    bool lowUsaTabla_  = true;
    bool highUsaTabla_ = true;
};

} // namespace abd::dsp
