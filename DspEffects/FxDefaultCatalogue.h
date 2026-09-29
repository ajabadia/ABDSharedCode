/*
  ==============================================================================

    FxDefaultCatalogue.h
    El catalogo de los efectos que YA estan en ABDShared. Namespace abd::dsp,
    modulo ABDShared::DspEffects.

    QUE ES. Una tabla de seis filas —coro, delay, reverb de FreeVerb,
    saturacion, reverberador de Schroeder y coro BBD— y las dos funciones que
    la consultan. Es el "dejar disponibles todos los efectos migrados" en una
    linea: un producto incluye esta cabecera, pasa el resultado a
    `FxEngine::setCatalogue` y ya tiene los seis en los cuatro slots.

    POR QUE ES UNA FUNCION Y NO UN ARRAY GLOBAL. El modulo entero es header-only.
    Un `static const FxEffectInfo tabla[]` en una cabecera seria una variable
    con enlace interno en cada unidad de traduccion que la include, y el motor
    compararia punteros de fila de una tabla distinta de la que consulto el
    panel: el mismo efecto con dos direcciones. Con `static` dentro de una
    funcion hay una sola instancia por programa (C++ garantiza que un
    `static` local de una funcion inline se fusiona), asi que la fila que
    devuelve `fxDefaultCatalogue()` es la misma que comparan el motor, el panel
    y el test. Es el mismo motivo por el que `EffectPolicy.h` no lleva estado.

    Y EL NUMERO DE FILAS LO DICE LA TABLA, no una constante al lado: se cuenta
    con `sizeof`, y anadir un efecto es anadir una fila, no tocar un numero en
    tres sitios. Un catalogo con una entrada de mas que el recuento es el modo
    bonito de que el ultimo efecto del panel no exista.

    Y ESTOS SEIS, NO SETENTA. El unico criterio para entrar aqui es que el motor
    este EN este modulo. Los 48 efectos privados de ABDEep no entran todavia: son
    codigo JUCE, y meterlos exigiria que este modulo dejara de ser libm-free y
    JUCE-free, que es justo lo que sostiene la paridad nativa <-> WASM de
    ABDNeural. Un producto con esos 48 monta su propia tabla y le anade estas
    seis filas al final.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "DspEffects/FxRegistry.h"
#include "DspEffects/adapters/BasicAdapters.h"

namespace abd::dsp
{

//==============================================================================
/** Las filas del catalogo por defecto, en el orden en que las ve el panel.

    EL ORDEN ES POLITICA, y por eso es de este fichero y no del motor. El
    criterio es el que usa un usuario delante de un menu: los que se usan todos
    los dias primero, y el BBD al final porque es el menos usual. Insertar uno
    nuevo en medio cambia los numeros de todos los de despues, que es lo que
    haria que "el 4" significara una cosa el lunes y otra el martes. Si se
    inserta, se inserta al final. */
inline const FxEffectInfo* fxDefaultCatalogue (int& count) noexcept
{
    static const FxEffectInfo table[] = {
        { "chorus",      "Chorus",         adapters::ChorusFx::kNumParams,
          adapters::ChorusFx::specs(),
          adapters::ChorusFx::create,  adapters::ChorusFx::process,
          adapters::ChorusFx::setParam, adapters::ChorusFx::setAll,
          adapters::ChorusFx::reset,  adapters::ChorusFx::destroy },

        { "delay",       "Delay",          adapters::DelayFx::kNumParams,
          adapters::DelayFx::specs(),
          adapters::DelayFx::create,   adapters::DelayFx::process,
          adapters::DelayFx::setParam, adapters::DelayFx::setAll,
          adapters::DelayFx::reset,    adapters::DelayFx::destroy },

        { "reverb",      "Reverb",         adapters::ReverbFx::kNumParams,
          adapters::ReverbFx::specs(),
          adapters::ReverbFx::create,  adapters::ReverbFx::process,
          adapters::ReverbFx::setParam, adapters::ReverbFx::setAll,
          adapters::ReverbFx::reset,   adapters::ReverbFx::destroy },

        { "saturation",  "Saturation",     adapters::SaturationFx::kNumParams,
          adapters::SaturationFx::specs(),
          adapters::SaturationFx::create,  adapters::SaturationFx::process,
          adapters::SaturationFx::setParam, adapters::SaturationFx::setAll,
          adapters::SaturationFx::reset,   adapters::SaturationFx::destroy },

        { "schroeder",   "Schroeder",      adapters::SchroederFx::kNumParams,
          adapters::SchroederFx::specs(),
          adapters::SchroederFx::create,  adapters::SchroederFx::process,
          adapters::SchroederFx::setParam, adapters::SchroederFx::setAll,
          adapters::SchroederFx::reset,   adapters::SchroederFx::destroy },

        { "bbd",         "Juno BBD Chorus", adapters::BbdChorusFx::kNumParams,
          adapters::BbdChorusFx::specs(),
          adapters::BbdChorusFx::create, adapters::BbdChorusFx::process,
          adapters::BbdChorusFx::setParam, adapters::BbdChorusFx::setAll,
          adapters::BbdChorusFx::reset,  adapters::BbdChorusFx::destroy }
    };

    count = static_cast<int> (sizeof (table) / sizeof (table[0]));
    return table;
}

/** La misma tabla, como puntero y tamano, para quien no pueda usar la salida. */
inline const FxEffectInfo* fxDefaultCatalogue() noexcept
{
    int count = 0;
    return fxDefaultCatalogue (count);
}

/** Cuantas filas tiene el catalogo por defecto. */
inline int fxDefaultCatalogueSize() noexcept
{
    int count = 0;
    (void) fxDefaultCatalogue (count);
    return count;
}

} // namespace abd::dsp
