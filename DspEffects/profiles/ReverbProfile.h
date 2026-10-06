/*
  ==============================================================================

    ReverbProfile.h
    Los diez reverbs del DeepMind 12 como PERFIL: solo numeros, ningun codigo.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp).

    QUE ES. Una tabla de diez filas y un `find` que la busca por id. El motor es
    `DspSchroederReverb`, que es UN SOLO reverberador Schroeder-Moorer para los
    diez: no hay diez clases ni diez motores, hay uno y diez tablas de numeros.

        auto* variant = dsp::ReverbProfile::find (id);
        dsp::SchroederReverb reverb;
        reverb.setDecay (variant->decay);
        reverb.setDamping (variant->damping);
        reverb.setDiffusion (variant->diffusion);
        reverb.setRoomSize (variant->roomSize);
        reverb.setPreDelaySeconds (variant->preDelaySeconds);
        reverb.setInvertLeft (variant->invertLeft);

    DE DONDE SALEN LOS DATOS. Del `FXSimpleReverb` de ABDEep
    (`Source/DSP/FX/FXSimpleReverb.cpp`, tablas `setDefaultsForType` y
    `numParametersForType`). Los ids son los que usa la fabrica
    (`FXSlot_Factory.cpp`), que es la unica que dice que id es que reverb.

    EL ORDEN DE LOS IDS NO ES 1..10, Y ESTA ES LA TRAMPA. Las ids que reparte
    la fabrica son 1, 2, 3, 4, 5, 6, 22, 26, 27 y 28: la 22 se coló en medio y
    las 26 a 28 son un bloque aparte. En la interfaz (`WebUI/js/effects_data.js`,
    `FX_TYPE_NAMES`) las diez se enumeran seguidas como si fueran 1..10, lo que
    significa que la etiqueta de la web y la reverb que suena NO son la misma a
    partir de la septima. Esa desalineacion no se arregla aqui (no es de este
    modulo), pero se deja escrita en la tabla de este fichero, porque es
    justo el dato que hara falta cuando se arregle.

    EL ORDEN DE LOS MANDOS TAMBIEN ES POR VARIANTE, Y NO ES EL MISMO. Cada
    reverb tiene su propio numero de controles y su propio orden, tomado del
    hardware (`docs/deepmind_fx.md`), y `paramIndex` dice que mando del panel
    mueve que control del motor. Un -1 significa "este mando no existe en esta
    variante": los controles que no se mapean se quedan en su valor de fabrica
    y no se pueden tocar. Eso NO es un descuido y no se completa con un mapeo
    inventado: es lo que hace el efecto desde que se publico.

    LO QUE SIGUE SIENDO DEL CONSUMIDOR. El perfil guarda la maquina (que
    variante, que numeros por defecto, que mando mueve que). NO guarda el wet/dry
    (lo mezcla el slot), NI el suavizado de los mandos, NI el recorte de la
    entrada. Eso es politica de producto y se queda en el consumidor, que es la
    regla del modulo.

  ==============================================================================
*/

#pragma once

namespace abd::dsp
{

//==============================================================================
/**
    Los diez reverbs del DeepMind 12, como datos para `SchroederReverb`.

    `find` devuelve nullptr si el id no es un reverb. Un id de reverb que no
    este en la tabla es un error de la fabrica, no un caso normal, y nullptr
    obliga a decidir que hacer en vez de devolver un perfil inventado en
    silencio.
*/
struct ReverbProfile
{
    /** Una variante. Todos los numeros estan en el rango que espera el motor. */
    struct Variant
    {
        int id;            // id que usa la fabrica de slots
        const char* name;  // como se muestra en el panel
        int numParameters; // controles que expone esta variante

        // Valores de fabrica del motor. Son los que se aplican al construir y
        // los que se quedan fijos cuando un mando no esta mapeado.
        float decay; // < 0 = variante de cola corta ("Reverse")
        float damping;
        float diffusion;
        float roomSize;
        float preDelaySeconds;

        // Que control del panel mueve que control del motor. -1 = no existe.
        //
        // OJO: el retardo real es un quinto de `preDelaySeconds` porque el
        // motor reproduce el factor 0.2 del original (ver DspSchroederReverb.h).
        int paramIndexPreDelay;
        int paramIndexDecay;
        int paramIndexRoomSize;
        int paramIndexDamping;
        int paramIndexDiffusion;

        bool invertLeft; // "Reverse" niega solo el canal izquierdo
    };

    static constexpr int numVariants = 10;

    // Ordenada por id, que es como la reparte la fabrica.
    static constexpr Variant variants[numVariants] =
        {
            // id  name          n   decay   damp   diff   size   pre    preIdx dcyIdx szIdx dmpIdx difIdx  invL
            {1, "Hall", 12, 0.70f, 0.40f, 0.70f, 0.80f, 0.10f, 0, 1, 2, 3, 4, false},
            {2, "Plate", 12, 0.60f, 0.30f, 0.80f, 0.50f, 0.05f, 0, 1, 2, 3, 4, false},
            {3, "Rich Plate", 12, 0.75f, 0.20f, 0.90f, 0.60f, 0.05f, 0, 1, 2, 3, 4, false},
            {4, "Ambience", 10, 0.30f, 0.60f, 0.50f, 0.30f, 0.00f, 0, 1, 2, 3, 4, false},

            // Gated: el diffusion va al mando 9, y NO hay mandos de size ni damping.
            {5, "Gated", 10, 0.20f, 0.80f, 0.30f, 0.40f, 0.00f, 0, 1, -1, -1, 9, false},

            // Reverse: el decay es NEGATIVO (cola corta) y ademas niega el izquierdo.
            {6, "Reverse", 9, -0.30f, 0.90f, 0.20f, 0.70f, 0.15f, 0, 1, -1, -1, 3, true},

            // Deep Verb: el unico con el pre-retardo en el mando 3 y sin diffusion.
            {22, "Deep Verb", 5, 0.85f, 0.30f, 0.80f, 0.90f, 0.10f, 3, 1, -1, -1, -1, false},

            {26, "Chamber", 12, 0.50f, 0.50f, 0.60f, 0.60f, 0.05f, 0, 1, 2, 3, 4, false},
            {27, "Room", 12, 0.35f, 0.60f, 0.40f, 0.40f, 0.02f, 0, 1, 2, 3, 4, false},
            {28, "Vintage", 12, 0.65f, 0.35f, 0.70f, 0.70f, 0.08f, 0, 1, 2, 3, 4, false}};

    /**
        La fila que se usa cuando el id no es ninguno de los diez.

        Copia la fila `default` del original: valores neutros, doce controles y
        el nombre generico. Existe porque el constructor de `FXSimpleReverb`
        acepta cualquier int, y sin esto un id mal formado seria un
        desreferencia a nullptr en el medio del audio.

        OJO, Y ES UN CAMBIO CONSCIENTE: en el original, un id desconocido caia en
        el `default` del `switch` de `setParameter`, que NO hace nada, o sea que
        sus doce mandos eran inertes. Aqui los doce mandos si mueven el motor
        (con el mismo reparto que Hall). Por la fabrica esto no se puede dar: los
        diez ids que construye estan todos en la tabla, y la unica forma de llegar
        aqui es llamar al constructor a mano con un numero que no existe. Se
        prefiere un id desconocido que suene a algo antes que uno que se calle
        en silencio.
    */
    static constexpr Variant fallback{-1, "Reverb", 12,
                                      0.50f, 0.50f, 0.50f, 0.50f, 0.05f,
                                      0, 1, 2, 3, 4, false};

    /**
        Busca una variante por id. Devuelve nullptr si ese id no es un reverb.

        nullptr y no un perfil por defecto a proposito: un id mal mapeado en la
        fabrica tiene que verse, no sonar a otra reverb. Para el caso de "no se
        que hacer con esto" esta `findOrFallback`.
    */
    static constexpr const Variant* find(int id) noexcept
    {
        for (int i = 0; i < numVariants; ++i)
            if (variants[i].id == id)
                return &variants[i];

        return nullptr;
    }

    /** Como `find`, pero devuelve la fila generica en vez de nullptr. */
    static constexpr const Variant* findOrFallback(int id) noexcept
    {
        const Variant* found = find(id);

        return found != nullptr ? found : &fallback;
    }
};

} // namespace abd::dsp
