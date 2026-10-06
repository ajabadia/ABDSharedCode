/*
  ==============================================================================

    FxRegistry.h
    El CONTRATO de un efecto que se puede poner en un slot, y el catalogo que
    dice cuales hay. Namespace abd::dsp, modulo ABDShared::DspEffects.

    POR QUE ESTE FICHERO EXISTE. Un sistema de slots tiene que poder meter "el
    efecto que elija el usuario" en un hueco, y eso obliga a un despachador en
    tiempo de ejecucion. El sistema de slots de ABDEep lo resuelve con una clase
    base virtual y un `switch` de 50 casos en `FXSlot_Factory.cpp`, con el
    coste de que `FXSlot.h` incluye CINCUENTA cabeceras de efecto: tocar uno
    obliga a recompilar todo, y saber que efectos hay exige leer el `switch`.

    Aqui se resuelve al reves, con las dos reglas que ya tiene el modulo:

      1. NADA VIRTUAL en el lazo de audio. Un efecto se borra por PUNTERO A
         FUNCION, que en un compilador moderno es una llamada indirecta sin
         coste extra y, sobre todo, sin vtable que invalidar el cache. Es la
         misma razon por la que `EffectPolicy.h` inyecta la etapa de caracter
         como parametro de plantilla en vez de usar una interfaz: aqui no se
         puede usar plantilla porque el TIPO lo elige el usuario en caliente.

      2. EL CATALOGO ES UNA TABLA, no un `switch`. Anadir un efecto es anadir
         una fila. Un efecto que no se compila nunca no aparece en la tabla.

    Y hay una pieza que no es el motor ni la politica: el CONTRATO DE
    PARAMETROS. Un slot habla normalizado 0..1 porque es lo que sale de un
    panel, y los motores de este modulo hablan unidades fisicas (Hz, milisegundos,
    dB). Ese reparto es de PRODUCTO y por la regla del modulo no puede vivir en
    el motor. Vive aqui, en el adaptador, que es donde se traduce. Ver
    `adapters/BasicAdapters.h`.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"

#include <cstddef>

namespace abd::dsp
{

//==============================================================================
/** Un parametro de un efecto, en las unidades en que lo entiende el motor. */
struct FxParamSpec
{
    const char* name;   /**< Nombre para la interface. */
    float minValue;     /**< Valor fisico en el extremo izquierdo. */
    float maxValue;     /**< Valor fisico en el extremo derecho. */
    float defaultValue; /**< Valor fisico por defecto. */
    float skew;         /**< 1.0 = lineal. < 1 da MAS RECORRIDO a la parte
                             baja del rango.
        
                             El signo esta al reves de lo que parece, y
                             escribiendolo al derecho se eligieron seis
                             mandos inservibles. Con `skew = 0.3`, la
                             mitad fisica del rango cae en el 0.10 del
                             recorrido del mando, no en el 0.5: el
                             recorrido se reparte `1 / 0.3 = 3.3` veces
                             mas hacia los valores bajos. Eso es lo que
                             quiere un retardo (resolucion fina entre
                             20 y 200 ms) y lo que NO quiere un coro
                             (sus 0.5 a 2 Hz estan en el ultimo 10% del
                             recorrido si el rango llega a 10 Hz). */
    int steps;          /**< 0 = continuo. > 1 = discreto, este numero
                             de valores repartidos entre min y max. El
                             modo del BBD son 4 estados, y en un mando
                             continuo el usuario tendria que adivinar en
                             que fraccion del recorrido esta el segundo. */
};

//==============================================================================
/** Crea una instancia del efecto, o devuelve nullptr si no se puede. */
using FxCreateFn = void* (*)(double sampleRate);

/** Procesa un bloque stereo. Los punteros pueden coincidir (proceso in-place). */
using FxProcessFn = void (*)(void* instance, const float* inL, const float* inR,
                             float* outL, float* outR, int numSamples);

/** Pone un parametro por indice, NORMALIZADO 0..1. El adaptador traduce. */
using FxSetParamFn = void (*)(void* instance, int index, float normalisedValue);

/** Pone TODOS los parametros de golpe. Opcional: si es nullptr, el slot los
    pone uno a uno. Existe para los efectos que tienen un bloque de parametros
    (la reverb, que toma una estructura entera) y asi se ahorra un bucle. */
using FxSetAllParamsFn = void (*)(void* instance, const float* normalised, int count);

/** Libera la instancia creada por `create`. */
using FxDestroyFn = void (*)(void* instance);

/** Vacia el estado de AUDIO del efecto, sin tocar los mandos. Se llama en un
    `reset()` de host: los buffers a cero, las fases a su sitio, los retardos a
    cero. Es una operacion distinta de `setAllParams`, y por eso tiene su propia
    funcion: reponer los parametros no limpia una cola que ya tiene contenido. */
using FxResetFn = void (*)(void* instance);

//==============================================================================
/** Una fila del catalogo: que es este efecto y como se habla con el. */
struct FxEffectInfo
{
    const char* name;          /**< Nombre tecnico, sin espacios. */
    const char* displayName;   /**< Nombre para el panel. */
    int numParams;             /**< Cuantos parametros tiene. */
    const FxParamSpec* params; /**< Tabla de `numParams` entradas. */
    FxCreateFn create;
    FxProcessFn process;
    FxSetParamFn setParam;
    FxSetAllParamsFn setAllParams; /**< Puede ser nullptr. */
    FxResetFn reset;               /**< Puede ser nullptr. */
    FxDestroyFn destroy;
};

//==============================================================================
/**
    El catalogo: el array de filas, y su numero.

    SE PASA DESDE EL PRODUCTO, y no hay catalogo por defecto todavia. Un producto
    monta su tabla —la suya, que puede ser solo estos seis efectos o la suya mas
    los 48 de ABDEep— y se la pasa a `FxEngine::setCatalogue`. Es deliberado:
    publicar aqui un `fxDefaultCatalogue()` obligaria a que el modulo Supiera
    que efectos existen, y el modulo no tiene ni que saberlo: su trabajo es el
    motor y el contrato, no el catalogo de un producto.

    Y CUANDO HAYA CATALOGO POR DEFECTO tendra que ser una FUNCION, no un
    `static` en un `.cpp`, porque el modulo entero es header-only: un `static`
    en un header seria una variable por unidad de traduccion, y con seis
    productos en el arbol habria seis catalogos distintos.
*/

/** El valor fisico de un parametro a partir de su valor normalizado 0..1. Es la
    operacion inversa de `fxNormalise`, y la que usa todo adaptador. */
float fxDenormalise(const FxParamSpec& p, float normalised) noexcept;

/** Un valor fisico a su normalizado 0..1, con el sesgo de la tabla. El recorrido
    completo del mando queda DESPUES de aplicar el sesgo, no antes: por eso el
    sesgo es propiedad del MANDO y cambiar las unidades de un efecto no cambia
    lo que se siente al moverlo. */
float fxNormalise(const FxParamSpec& p, float physical) noexcept;

/** Busca un efecto por nombre tecnico. Devuelve nullptr si no esta. */
const FxEffectInfo* fxFindEffect(const FxEffectInfo* catalogue, int count,
                                 const char* name) noexcept;

/** Busca un efecto por indice. Devuelve nullptr si el indice no vale. */
const FxEffectInfo* fxEffectAt(const FxEffectInfo* catalogue, int count,
                               int index) noexcept;

//==============================================================================
// Implementaciones. Van aqui, y no en un .cpp, porque el modulo es header-only.

//==============================================================================
inline float fxNormalise(const FxParamSpec& p, float physical) noexcept
{
    if (p.maxValue <= p.minValue)
        return 0.0f;

    const float t = jlimit(0.0f, 1.0f, (physical - p.minValue) / (p.maxValue - p.minValue));
    if (p.skew <= 0.0f || t <= 0.0f)
        return t;

    // t' = t^skew. Y el exponente es `skew` y NO `1 / skew`: `fxDenormalise`
    // eleva a `1 / skew`, y para que las dos sean inversas la de aqui tiene que
    // elevar a lo contrario. Con las dos al reves, el mando de un retardo de
    // 375 ms salia en 0.0035 y ese mismo mando, reinterpretado, daba 1.63 s.
    // El sintoma —casi todos los mandos agrupados en un extremo del recorrido—
    // es exactamente el de dos funciones que no son inversas, y por eso la
    // comprobacion que lo caza es el viaje de ida y vuelta, no un valor suelto.
    //
    // El sesgo se calcula con `pow` de DspMath, no con `std::pow`: este modulo
    // es libm-free, y una llamada a libm aqui abriria un agujero en la paridad
    // nativa <-> WASM que DspMath sostiene entera. `pow` solo vale para base > 0,
    // y por eso el `t <= 0` de arriba (0^k es 0 para toda k, no un NaN) esta
    // antes de la llamada y no dentro.
    return pow(t, p.skew);
}

inline float fxDenormalise(const FxParamSpec& p, float normalised) noexcept
{
    const float v = jlimit(0.0f, 1.0f, normalised);

    if (p.maxValue <= p.minValue)
        return p.minValue;

    if (p.steps > 1)
    {
        // El recorrido se parte en `steps` tramos IGUALES, y el valor sale del
        // tramo en el que cae el mando: [0, 1/steps) es el primero, y asi
        // hasta el ultimo.
        //
        // Y NO con `redondeo(v * (steps-1))`, que es lo que hacia la primera
        // version y repartia mal: con 4 pasos los tramos salian de 0.33 cada
        // uno pero el redondeo hacia que el TERCER tramo midiese el doble que
        // los otros dos — el mando en 0.5 y en 0.75 daban los dos el modo 2, y
        // el 3 se alcanzaba solo en el ultimo 25%. Con cuatro modos, un selector
        // al que le falta un cuarto es un selector roto.
        const int steps = p.steps;
        const int index = jlimit(0, steps - 1, floorToInt(v * static_cast<float>(steps)));
        const float u   = static_cast<float>(index) / static_cast<float>(steps - 1);
        return p.minValue + u * (p.maxValue - p.minValue);
    }

    // Continuo con sesgo: primero se deshace el sesgo y luego se recorre el
    // rango. OJO AL EXPONENTE, que es `1 / skew` y no `skew`: `fxNormalise`
    // eleva a `1 / skew`, asi que la inversa tiene que hacer lo mismo y no lo
    // contrario. Con `skew` a secas las dos funciones NO SON INVERSAS —el mando
    // de un retardo de 375 ms salia en 0.0035, y ese mismo mando reinterpretado
    // daba 1.63 s— y el sintoma es que casi todos los mandos se agrupan en un
    // extremo del recorrido y el otro extremo no se puede alcanzar.
    const float u = p.skew <= 0.0f ? v : pow(v, 1.0f / p.skew);
    return p.minValue + u * (p.maxValue - p.minValue);
}

inline const FxEffectInfo* fxEffectAt(const FxEffectInfo* catalogue, int count,
                                      int index) noexcept
{
    // El indice 0 es SIEMPRE bypass, este o no este en la tabla. Un panel
    // permite elegir "nada" sin que ningun producto tenga que acordarlo.
    //
    // Y EL LIMITE SUPERIOR ES `> count`, no `>= count`. Con `>=`, el ULTIMO
    // efecto de la tabla era inalcanzable: con seis filas, el indice 6 caia
    // fuera y el BBD —el unico con un selector discreto, y el que mas se nota
    // que falta— no se podia elegir jamas. No lo cazaba ningun test porque los
    // que habia elegian efectos del principio de la lista.
    if (index == 0 || catalogue == nullptr || index < 0 || index > count)
        return nullptr;

    return &catalogue[index - 1];
}

inline const FxEffectInfo* fxFindEffect(const FxEffectInfo* catalogue, int count,
                                        const char* name) noexcept
{
    if (catalogue == nullptr || name == nullptr)
        return nullptr;

    for (int i = 0; i < count; ++i)
    {
        const char* other = catalogue[i].name;
        bool igual        = true;
        for (int c = 0; other[c] != 0 && name[c] != 0; ++c)
        {
            if (other[c] != name[c])
            {
                igual = false;
                break;
            }
        }
        if (igual && other[0] != 0)
            return &catalogue[i];
    }

    return nullptr;
}

//==============================================================================
/** Cuantos parametros tiene, como mucho, un slot. Es el ancho del bus. */
inline constexpr int kFxMaxParams = 12;

/** Cuantos slots tiene un motor. */
inline constexpr int kFxNumSlots = 4;

} // namespace abd::dsp
