/*
  ==============================================================================

    Re201Profile.h
    El Space Echo RE-201 como PERFIL: solo los numeros de la maquina.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp).

    QUE ES. Un `struct` de constantes y una tabla de doce modos, y nada mas. Se
    le pasa a `MultiHeadEcho` como parametro de plantilla:

        dsp::MultiHeadEcho<dsp::Re201Profile, dsp::TapeColour> spaceEcho;

    y lo que sale es un eco con los doce modos del selector de la maquina, sin
    que el motor sepa que existe un Roland.

    DE DONDE SALEN LOS DATOS, Y POR QUE IMPORTA. Del motor de ABDJUNiO601
    (`Source/Synth/JunoTapeEcho.{h,cpp}`, tabla `getPresetConfig`), que es la
    emulacion del RE-201 que hay en la suite. NO se copio de ABDEep
    (`FXSpaceEchoRE201`), que tambien tiene un RE-201 pero con DOS problemas que
    hacen que su tabla no sirva de referencia:

      1. Su tabla tiene cinco modos con posiciones y ganancias escritas a mano,
         que no coinciden con el selector de la maquina (que tiene doce
         combinaciones de cabezal).
      2. Su linea de retardo estaba rota: usaba `kMaxDelay = 70560` como mascara
         de bits sin que sea potencia de dos, y como sus cinco bits bajos eran
         cero, `writePos = (writePos + 1) & delayMask` se quedaba en 0 para
         siempre. La linea no avanzaba nunca y el eco no existia. Consta en el
         changelog de la extraccion, con su test de regresion. OJO: esto se
         ARREGLO en ABDEep despues (0.2.54, `kDelaySize = 2^17`), asi que hoy ya
         no es cierto. Sigue puesto porque es el motivo por el que el perfil se
         copio de JUNiO601, y el punto 1 sigue vigente: la tabla de modos de
         ABDEep son cinco modos escritos a mano y el selector de la maquina
         tiene doce, y ESO no se ha tocado.

    Asi que este perfil es el de JUNiO601. Y esa es justo la razon de que un
    perfil sea DATOS y no codigo: cambiar de fuente fue sustituir un `struct`.

    LA FORMA DE UN MODO. Dice QUE cabezales estan activos y si entra el tanque,
    no cuantas veces suena cada uno. El reparto es de la MAQUINA, no del perfil:
    con los tres cabezales activos cada uno suena a un tercio, y eso lo calcula
    el motor (`1 / activos`). Ponerlo aqui seria inventarse una tabla de
    ganancias que el RE-201 no tiene.

  ==============================================================================
*/

#pragma once

namespace abd::dsp
{

//==============================================================================
/** Numeros del Roland Space Echo RE-201, para `MultiHeadEcho`. */
struct Re201Profile
{
    /**
        Una fila del perfil: que cabezales suenan y si entra la reverb.

        Deliberadamente NO lleva ganancias. El reparto entre cabezales
        (1/activos) es de la maquina, no de la tabla, y por eso lo calcula el
        motor.
    */
    struct Mode
    {
        bool head[3];    // cabezales 1, 2 y 3 activos
        bool reverb;     // el tanque entra en este modo
    };

    static constexpr int numModes = 12;

    static constexpr Mode modes[12] =
    {
        { { true,  false, false }, false },   //  1  cabezal 1
        { { false, true,  false }, false },   //  2  cabezal 2
        { { false, false, true  }, false },   //  3  cabezal 3
        { { true,  true,  false }, false },   //  4  cabezales 1 + 2
        { { true,  false, false }, true  },   //  5  cabezal 1 + reverb
        { { false, true,  false }, true  },   //  6  cabezal 2 + reverb
        { { false, false, true  }, true  },   //  7  cabezal 3 + reverb
        { { true,  true,  false }, true  },   //  8  cabezales 1 + 2 + reverb
        { { true,  false, true  }, true  },   //  9  cabezales 1 + 3 + reverb
        { { false, true,  true  }, true  },   // 10  cabezales 2 + 3 + reverb
        { { true,  true,  true  }, true  },   // 11  los tres + reverb
        { { false, false, false }, true  }    // 12  solo reverb (sin eco)
    };

    /**
        Donde esta cada cabezal, como MULTIPLICADOR del retardo base.

        El cabezal 1 es el de referencia (1.0) y los otros dos suenan a 2x y 3x.
        Ojo con la intuicion: no son fracciones del retardo del cabezal 1, son
        retardos MAS LARGOS, y por eso el buffer tiene que aguantar 3 veces el
        retardo base. Estos son los valores por defecto; en JUNiO601 salen de
        la CALIBRACION de la unidad (ver `head2Ratio_` / `head3Ratio_`), asi que
        un sintetizador que calibrase su maquina pasa otros, y por eso estan
        aqui como constantes de perfil y no escritas en el motor.
    */
    static constexpr float headRatio[3] = { 1.0f, 2.0f, 3.0f };

    // Un cabezal de cinta real no envia lo mismo a L que a R.
    static constexpr float headRightScale[3] = { 0.95f, 0.90f, 0.92f };

    // El recorrido del selector de velocidad: con los tres cabezales encendidos
    // el retardo mayor es 3 veces el base, y el buffer se dimensiona para eso.
    static constexpr float minDelaySeconds     = 0.050f;
    static constexpr float maxDelaySeconds     = 0.500f;
    static constexpr float dryGain             = 0.75f;
    static constexpr float headOutputGain      = 0.40f;
    static constexpr float toneFrequencyHz     = 800.0f;
    static constexpr float tankTimeL           = 0.080f;   // linea del tanque
    static constexpr float tankTimeR           = 0.110f;
    static constexpr float wowHz               = 0.5f;     // deriva lenta
    static constexpr float flutterHz           = 8.0f;     // aleteo rapido
    static constexpr float wowAmount           = 0.003f;
    static constexpr float flutterAmount       = 0.001f;
    static constexpr float colourDrive         = 0.25f;
    static constexpr float colourHiss          = 0.15f;
};

} // namespace abd::dsp
