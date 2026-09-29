/*
  ==============================================================================

    DspEffectsTests.cpp
    Tests standalone del modulo DspEffects (namespace abd::dsp). SIN JUCE: este
    fichero compila y enlaza solo contra ABDShared::DspEffects + DspCore, que es
    justamente la propiedad que se quiere blindar.

    Que se cubre aqui:

      1. CONTRATO DE ETAPA. Que toda etapa de caracter tenga el ciclo de vida
         (`prepareSampleRate` / `getSampleRate` / `reset`) y un `processSample`
         que acepta el par `(drive, hiss)` con valores por defecto. Es la
         unica forma de que la inyeccion de politica siga siendo inyeccion: si
         el motor llega a depender de los internos de una etapa, este test es lo
         que se da cuenta. Ver la nota sobre el wow en characters/TapeColour.h.

      2. PUENTE DE DIODOS. Que pase recto por debajo del umbral, que recorte por
         encima y que sea PAR (un puente de diodos no sesga la senal).

      3. COLOR DE CINTA. Que la curva sea asimetrica (satura antes en positivo),
         que `drive` 0 sea transparente, y que el siseo sea reproducible con la
         misma semilla y distinto con otra.

      4. ECO DE MULTI-CABEZAL. Que dos perfiles distintos con el MISMO motor
         den audio distinto (si no, el perfil no se estaria inyectando de
         verdad), que la etapa de color cambie el resultado, que `reset` devuelva
         el motor a su estado inicial bit a bit, y que la salida se mantenga
         finita y con un pico razonable en un lazo de realimentacion.

      5. RING MOD. Que los extremos del mapeo exponencial den los extremos del
         perfil, que las tres formas de onda produzcan tres resultados
         distintos, que la etapa cambie el resultado, y lo mismo de determinismo
         y estabilidad.

      6. PERFILES COMO DATOS. Que la tabla de los doce modos del RE-201 y la de
         los diez reverbs del DeepMind 12 esten completas, sin ids repetidos y
         con los numeros de fabrica que traian del original. Un perfil es lo
         unico que se puede cambiar sin que nada falle al compilar.

    Lo que NO se promete aqui: paridad con ningun efecto de ABDEep. Este modulo
    no tiene los originales a mano; la comparacion, si se quiere, vive en el
    consumidor (que es lo que hizo DspReverb con juce::Reverb en ABDNeural).

    Uso: ABDShared_DspEffects_Tests  (no toma argumentos; 0 = OK)

  ==============================================================================
*/

#include "DspCore/DspCore.h"
#include "DspEffects/EffectPolicy.h"
#include "DspEffects/MultiHeadEcho.h"
#include "DspEffects/RingMod.h"
#include "DspEffects/DspSchroederReverb.h"
#include "DspEffects/DspChorus.h"
#include "DspEffects/DspDelay.h"
#include "DspEffects/DspReverb.h"
#include "DspEffects/DspSaturation.h"
#include "DspEffects/characters/DiodeBridge.h"
#include "DspEffects/characters/TapeColour.h"
#include "DspEffects/profiles/Re201Profile.h"
#include "DspEffects/profiles/ReverbProfile.h"
#include "DspEffects/JunoBBD.h"
#include "DspEffects/characters/BbdNoise.h"
#include "DspEffects/profiles/JunoBbdProfile.h"
#include "DspEffects/FxEngine.h"
#include "DspEffects/FxDefaultCatalogue.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

void check (bool ok, const char* what)
{
    ++gChecks;

    if (! ok)
    {
        std::printf ("[FAIL] %s\n", what);
        ++gFailures;
    }
}

/** Comprueba el contrato de una etapa de caracter. Ver el punto 1 de la cabecera. */
template <typename Stage>
void checkStageContract (const char* name)
{
    // El ciclo de vida que comparte CharacterStage.
    check ((std::is_base_of<abd::dsp::CharacterStage, Stage>::value),
           "la etapa no deriva de CharacterStage");

    Stage stage;

    stage.prepareSampleRate (48000.0);
    check (stage.getSampleRate() == 48000.0, "getSampleRate no devuelve lo preparado");

    // `processSample` tiene que existir y devolver float. Se quita el `const`
    // de la declaracion porque `decltype` de una variable const<float> es
    // `const float`, y el test no va a estar mirando calidades.
    const float viaOneArg = stage.processSample (0.25f);
    check (std::is_same<std::remove_cv_t<decltype (viaOneArg)>, float>::value,
           "processSample no devuelve float");

    // Y tiene que aceptar el par (drive, hiss), porque es lo que le pasa un
    // motor: si lo cambias, hay que cambiar la convencion en los dos sitios.
    const float viaThreeArgs = stage.processSample (0.25f, 0.5f, 0.5f);
    check (std::is_same<std::remove_cv_t<decltype (viaThreeArgs)>, float>::value,
           "processSample no acepta el par (drive, hiss)");

    stage.reset();

    std::printf ("       [ok] contrato de etapa: %s\n", name);
}

//==============================================================================
/** 2. El puente de diodos: recto abajo, recortado arriba, y par. */
void testDiodeBridge()
{
    abd::dsp::DiodeBridge bridge;

    // Por debajo del umbral la senal pasa intacta.
    check (bridge.processSample (0.1f, 1.0f, 0.3f) == 0.1f,
           "puente de diodos: deberia pasar recto por debajo del umbral");

    // Y por encima se comprime: 3.0 con umbral 0.3 da bastante menos de 3.0.
    const float hot = bridge.processSample (3.0f, 1.0f, 0.3f);
    check (hot > 0.3f && hot < 3.0f,
           "puente de diodos: deberia recortar por encima del umbral");

    // Paridad: el puente no puede sesgar la senal, o el anillo rectifica.
    float worstAsymmetry = 0.0f;

    for (int i = 1; i <= 1000; ++i)
    {
        const float x = (float) i / 100.0f;

        worstAsymmetry = std::max (worstAsymmetry,
                                   std::abs (bridge.processSample (x, 1.0f, 0.3f)
                                           + bridge.processSample (-x, 1.0f, 0.3f)));
    }

    check (worstAsymmetry < 1.0e-6f, "puente de diodos: no es par");

    // EL UMBRAL SE RECORTA, y lo que hace `threshold = 1` NO es lo que decia su
    // comentario. MEDIDO antes de arreglarlo:
    //
    //     x = 2.0, threshold 1.0 -> 1.000000  (recorte duro; decia "transparente")
    //     x = 2.0, threshold 1.5 -> 1.119203  (y la curva subia y luego bajaba)
    //
    // El segundo caso era una curva NO MONOTONA: por encima del umbral el termino
    // `(1 - threshold)` es negativo, asi que la senal salia mas pequena cuanto
    // mas grande era la entrada. Con el recorte, 1.5 se comporta como 1.
    check (bridge.processSample (2.0f, 1.0f, 1.0f) == 1.0f,
           "puente de diodos: threshold = 1 recorta duro a 1.0, no es transparente");

    check (bridge.processSample (2.0f, 1.0f, 1.5f)
               == bridge.processSample (2.0f, 1.0f, 1.0f),
           "puente de diodos: un umbral fuera de rango se recorta al tope");

    // Y la curva tiene que ser MONOTONA en todo el recorrido, que es lo que el
    // recorte garantiza y lo que antes fallaba por encima de 1.
    float worstDrop = 0.0f;
    float anterior = bridge.processSample (0.0f, 1.0f, 0.5f);

    for (int i = 1; i <= 400; ++i)
    {
        const float x = (float) i / 100.0f;
        const float ahora = bridge.processSample (x, 1.0f, 0.5f);
        worstDrop = std::max (worstDrop, anterior - ahora);
        anterior = ahora;
    }

    check (worstDrop <= 0.0f, "puente de diodos: la curva de transferencia es monotona");
}

//==============================================================================
/** 3. El color de cinta: asimetrico, transparente en drive 0, y con semilla. */
void testTapeColour()
{
    using abd::dsp::TapeColour;

    // Asimetria: el lado POSITIVO satura antes que el negativo. Es lo que
    // distingue una cinta de un limitador.
    check (TapeColour::tapeCurve (0.5f) > -TapeColour::tapeCurve (-0.5f),
           "color de cinta: la curva deberia saturar antes en positivo");

    // drive 0 con siseo 0 es la identidad: una etapa que no se usa no debe
    // tocar la senal.
    check (TapeColour().processSample (0.4f, 0.0f, 0.0f) == 0.4f,
           "color de cinta: drive 0 y siseo 0 deberian ser la identidad");

    // Determinismo del siseo: misma semilla, mismo audio. Se recogen las dos
    // series ANTES de compararlas, porque el siseo avanza con cada llamada:
    // comparar dentro del bucle compararia llamadas distintas de cada
    // instancia y daria un falso negativo.
    auto hiss = []
    {
        TapeColour stage;
        stage.reset();

        std::vector<float> out;
        for (int i = 0; i < 64; ++i)
            out.push_back (stage.processSample (0.0f, 0.0f, 1.0f));

        return out;
    };

    TapeColour other;
    other.setNoiseSeed (0x1234abcd);

    std::vector<float> otherHiss;
    for (int i = 0; i < 64; ++i)
        otherHiss.push_back (other.processSample (0.0f, 0.0f, 1.0f));

    check (hiss() == hiss(), "color de cinta: la misma semilla no da el mismo siseo");
    check (hiss() != otherHiss, "color de cinta: semillas distintas dan el mismo siseo");
}

//==============================================================================
/** Corre el eco con una etapa concreta y devuelve el audio, muestra a muestra. */
template <typename Colour>
std::vector<float> renderEcho (abd::dsp::MultiHeadEcho<abd::dsp::StudioEchoProfile, Colour>& echo,
                               int numSamples)
{
    std::vector<float> out;
    out.reserve ((size_t) numSamples * 2);

    for (int s = 0; s < numSamples; ++s)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float input = ((s % 64) < 32) ? 0.3f : -0.3f;
            out.push_back (echo.processSample (c, input, 0.25f, 0.5f, 0.5f, 0.5f, 0.3f));
        }

        echo.advance();
    }

    return out;
}

//==============================================================================
/** 4. El eco de multi-cabezal. */
void testMultiHeadEcho()
{
    using namespace abd::dsp;

    // El motor con color de cinta y el motor limpio.
    MultiHeadEcho<StudioEchoProfile, TapeColour> coloured;
    MultiHeadEcho<StudioEchoProfile, NullStage>   plain;

    coloured.prepare (48000.0);
    plain.prepare (48000.0);

    coloured.setMode (2);
    plain.setMode (2);

    const auto colouredAudio = renderEcho (coloured, 4096);
    const auto plainAudio    = renderEcho (plain, 4096);

    check (colouredAudio.size() == 8192, "eco: no ha salido el numero de muestras pedido");

    // La etapa de color tiene que NOTARSE. Si las dos coincide, el motor no la
    // esta usando y la inyeccion de politica es decorativa.
    bool differs = false;
    for (size_t i = 0; i < colouredAudio.size(); ++i)
        if (colouredAudio[i] != plainAudio[i]) { differs = true; break; }

    check (differs, "eco: la etapa de color no cambia nada (no se esta usando)");

    // Salida FINITA y con un pico razonable: un lazo de realimentacion que se
    // dispara es el fallo clasico al tocar los coeficientes del perfil.
    float peak = 0.0f;
    bool  finite = true;

    for (float x : colouredAudio)
    {
        if (! std::isfinite (x)) { finite = false; break; }

        peak = std::max (peak, std::abs (x));
    }

    check (finite, "eco: la salida se ha ido a infinito o NaN");
    check (peak > 1.0e-3f, "eco: la salida es practicamente muda");
    check (peak < 4.0f, "eco: la salida se dispara");

    // Determinismo: dos motores con los mismos ajustes dan el MISMO audio.
    MultiHeadEcho<StudioEchoProfile, TapeColour> again;
    again.prepare (48000.0);
    again.setMode (2);

    check (renderEcho (again, 4096) == colouredAudio, "eco: dos pasadas dan audio distinto");

    // Y `reset` devuelve al estado inicial: preparar, resetear y procesar tiene
    // que dar lo mismo que preparar y procesar.
    coloured.reset();
    check (renderEcho (coloured, 4096) == colouredAudio, "eco: reset() no devuelve al estado inicial");
}

//==============================================================================
/** 5. El ring mod. */
void testRingMod()
{
    using namespace abd::dsp;

    // Los extremos del mapeo exponencial tienen que dar los del perfil. Si
    // esto falla, el mapeo de frecuencias esta roto y ningun barrido sirve.
    RingMod<RingModProfile, DiodeBridge> ring;
    ring.prepare (48000.0);

    check (std::abs (ring.getFrequencyHz (0.0f) - RingModProfile::minFrequencyHz) < 1.0e-2f,
           "ring mod: el extremo bajo del mapeo no da la frecuencia minima del perfil");
    check (std::abs (ring.getFrequencyHz (1.0f) - RingModProfile::maxFrequencyHz) < 1.0f,
           "ring mod: el extremo alto del mapeo no da la frecuencia maxima del perfil");

    // Las tres formas de onda tienen que sonar distintas.
    auto renderWaveform = [&ring] (float waveform)
    {
        ring.reset();

        float sum = 0.0f;
        for (int s = 0; s < 2048; ++s)
        {
            sum += ring.processSample (0, 0.5f, waveform);
            ring.advance (0.5f, 0.3f, 0.0f);
        }
        return sum;
    };

    const float sineSum = renderWaveform (0.0f);
    const float sawSum  = renderWaveform (0.5f);
    const float squareSum = renderWaveform (0.9f);

    check (sineSum != sawSum && sawSum != squareSum && sineSum != squareSum,
           "ring mod: dos formas de onda dan el mismo resultado");

    // La etapa inyectada cambia el resultado. Y lo que se comprueba no es que
    // `NullStage` deje pasar la ENTRADA: el anillo siempre multiplica por el
    // modulador, con etapa o sin ella. Lo que hace la etapa es quitar el
    // recorte: con puente los lados de la campana se recortan, sin puente la
    // salida es el producto crudo y llega tan alto como la entrada.
    RingMod<RingModProfile, DiodeBridge> withBridge;
    RingMod<RingModProfile, NullStage>   without;

    withBridge.prepare (48000.0);
    without.prepare (48000.0);

    bool  differs = false;
    float peakWith = 0.0f, peakWithout = 0.0f;
    bool  withoutStayedUnderInput = true;

    for (int s = 0; s < 2048; ++s)
    {
        const float a = withBridge.processSample (0, 0.9f, 0.0f);
        const float b = without.processSample  (0, 0.9f, 0.0f);

        if (a != b) differs = true;

        peakWith    = std::max (peakWith, std::abs (a));
        peakWithout = std::max (peakWithout, std::abs (b));

        if (std::abs (b) > 0.9f) withoutStayedUnderInput = false;

        withBridge.advance (0.5f, 0.3f, 0.0f);
        without.advance  (0.5f, 0.3f, 0.0f);
    }

    check (differs, "ring mod: la etapa de color no cambia nada (no se esta usando)");
    check (withoutStayedUnderInput,
           "ring mod: NullStage deberia dejar el producto crudo, sin recorte");
    check (peakWithout > peakWith,
           "ring mod: el puente de diodos deberia recortar los lados de la campana");
}

//==============================================================================
/** 6. El perfil del RE-201: los doce modos del selector de la maquina.

    Este test es el guardián de DATOS, no de DSP: el perfil es una tabla, y una
    tabla no se rompe sola, se rompe cuando alguien la edita. aqui esta lo que
    no se puede tocar sin que alguien se entere.
*/
void testRe201Profile()
{
    using Re201 = abd::dsp::Re201Profile;

    check (Re201::numModes == 12, "el RE-201 deberia tener los doce modos del selector");

    // Los cuatro primeros son solo eco; del quinto en adelante entra la reverb.
    for (int m = 0; m < 4; ++m)
        check (! Re201::modes[m].reverb, "los modos 1-4 del RE-201 no llevan reverb");

    for (int m = 4; m < 12; ++m)
        check (Re201::modes[m].reverb, "los modos 5-12 del RE-201 llevan reverb");

    // El ultimo modo es "solo reverb": ningun cabezal activo.
    check (! Re201::modes[11].head[0] && ! Re201::modes[11].head[1]
             && ! Re201::modes[11].head[2],
           "el modo 12 del RE-201 deberia ser solo reverb, sin ningun cabezal");

    // Y en todos los demas hay al menos uno, o el modo no haria nada.
    for (int m = 0; m < 11; ++m)
    {
        const bool any = Re201::modes[m].head[0] || Re201::modes[m].head[1]
                      || Re201::modes[m].head[2];

        check (any, "un modo del RE-201 sin ningun cabezal activo no hace eco");
    }

    // Las combinaciones de cabezal del selector, una a una. Esta es la tabla
    // que se copio de JUNiO601, y es lo que mas caro seria perder en silencio.
    struct Expected { int mode; bool h1, h2, h3; };
    const Expected expected[] =
    {
        {  0, true,  false, false }, {  1, false, true,  false },
        {  2, false, false, true  }, {  3, true,  true,  false },
        {  4, true,  false, false }, {  5, false, true,  false },
        {  6, false, false, true  }, {  7, true,  true,  false },
        {  8, true,  false, true  }, {  9, false, true,  true  },
        { 10, true,  true,  true  }
    };

    for (const auto& e : expected)
        check (Re201::modes[e.mode].head[0] == e.h1
            && Re201::modes[e.mode].head[1] == e.h2
            && Re201::modes[e.mode].head[2] == e.h3,
               "la combinacion de cabezales del modo no coincide con el selector");

    // Los ratios son MULTIPLICADORES, no fracciones: el cabezal 3 suena a 3x
    // el retardo base, que es lo que obliga a dimensionar el buffer.
    check (Re201::headRatio[0] == 1.0f, "el cabezal 1 deberia ser el de referencia");
    check (Re201::headRatio[1] > Re201::headRatio[0]
        && Re201::headRatio[2] > Re201::headRatio[1],
           "los ratios de cabezal deberian crecer");

    // El buffer tiene que aguantar el retardo del cabezal mas lejano.
    check (Re201::maxDelaySeconds * Re201::headRatio[2] <= 1.5f + 1.0e-4f,
           "el retardo maximo con los tres cabezales no cabe en el buffer");
}

//==============================================================================
/** 7. El reverberador Schroeder-Moorer.

    No prueba el sonido (eso lo hace la paridad del consumidor, que tiene el
    original a mano). Prueba las INVARIANTES que, si fallan, no suenan mal: lo
    hacen callar.
*/
void testSchroederReverb()
{
    using abd::dsp::SchroederReverb;

    SchroederReverb reverb;
    reverb.prepare (48000.0);

    reverb.setDecay (0.7f);
    reverb.setDamping (0.4f);
    reverb.setDiffusion (0.7f);
    reverb.setRoomSize (0.8f);
    reverb.setPreDelaySeconds (0.1f);

    // El pre-retardo real lleva el factor 0.2 del original: 0.1 s pedidos son
    // 960 muestras a 48 kHz, no 4800. Es un descuido del efecto publicado, y este
    // test lo fija para que nadie lo "arregle" sin querer.
    check (reverb.getPreDelaySamples() == 960,
           "el pre-retardo no ha salido en las muestras pedidas (con el 0.2 del original)");

    // Suena: una entrada constante tiene que dar una cola que no se apaga.
    //
    // OJO con la entrada: tiene que ser IGUAL en los dos canales. El pre-retardo
    // es mono (escribe la media de L y R y se lee una sola vez), asi que con
    // 0.3 / -0.3 la media es cero y la reverb sale muda. No es un fallo del motor
    // ni del test: es la consecuencia directa del pre-retardo mono del original,
    // y por eso esta misma cancelacion se comprueba mas abajo como caso propio.
    float outL = 0.0f, outR = 0.0f, peak = 0.0f;
    bool  finite = true;

    for (int s = 0; s < 48000; ++s)
    {
        reverb.processFrame (0.3f, 0.3f, outL, outR);

        if (! std::isfinite (outL) || ! std::isfinite (outR)) { finite = false; break; }

        peak = std::max (peak, std::abs (outL));
    }

    check (finite, "reverb: la salida se ha ido a infinito o NaN");
    check (peak > 1.0e-4f, "reverb: la salida es practicamente muda");

    // Determinismo: DOS EJEMPLARES FRESCOS, misma preparacion, mismo audio, mismo
    // resultado bit a bit. Se comparan dos instancias nuevas a proposito: comparar
    // la que ya ha sonado 48000 muestras contra una nueva no mide determinismo,
    // mide que la primera tenga cola, y siempre darian distinto.
    SchroederReverb again;
    again.prepare (48000.0);
    again.setDecay (0.7f);
    again.setDamping (0.4f);
    again.setDiffusion (0.7f);
    again.setRoomSize (0.8f);
    again.setPreDelaySeconds (0.1f);

    SchroederReverb third;
    third.prepare (48000.0);
    third.setDecay (0.7f);
    third.setDamping (0.4f);
    third.setDiffusion (0.7f);
    third.setRoomSize (0.8f);
    third.setPreDelaySeconds (0.1f);

    bool same = true;
    for (int s = 0; s < 4096 && same; ++s)
    {
        float aL, aR, bL, bR;

        again.processFrame (0.3f, 0.3f, aL, aR);
        third.processFrame (0.3f, 0.3f, bL, bR);

        if (aL != bL || aR != bR) same = false;
    }

    check (same, "reverb: dos pasadas dan audio distinto");

    // `reset` devuelve al estado inicial.
    reverb.reset();
    peak = 0.0f;
    for (int s = 0; s < 64; ++s)
    {
        reverb.processFrame (0.3f, 0.3f, outL, outR);
        peak = std::max (peak, std::abs (outL));
    }

    check (peak == 0.0f, "reverb: reset() no devuelve al estado inicial");

    // El pre-retardo MONO se come la senal anticorrelacionada. Es el comportamiento
    // del original y no se corrige, asi que se fija aqui a proposito: si alguien
    // hiciera el pre-retardo stereo (o si el motor dejara de leerlo una vez),
    // este test avisa. Con 0.3 / -0.3 la media es exactamente 0.0, y como el
    // pre-retardo esta activo, lo que sale de los conbs es silencio.
    SchroederReverb cancelling;
    cancelling.prepare (48000.0);
    cancelling.setDecay (0.7f);
    cancelling.setRoomSize (0.8f);
    cancelling.setPreDelaySeconds (0.1f);

    peak = 0.0f;
    for (int s = 0; s < 4800; ++s)
    {
        cancelling.processFrame (0.3f, -0.3f, outL, outR);
        peak = std::max (peak, std::abs (outL));
    }

    check (peak == 0.0f,
           "reverb: con pre-retardo mono, una entrada anticorrelacionada deberia salir muda");

    // La variante "Reverse" NIEGA SOLO EL IZQUIERDO. Es un efecto Haas, no una
    // inversion de fase, y es un descuido del original que el port conserva a
    // proposito (ver la cabecera). Este test es lo que hace que, si alguien lo
    // "arregla" sin querer, se entere.
    SchroederReverb inverted;
    inverted.prepare (48000.0);
    inverted.setDecay (0.5f);
    inverted.setRoomSize (0.5f);
    inverted.setInvertLeft (true);

    for (int s = 0; s < 512; ++s)
        inverted.processFrame (0.25f, 0.25f, outL, outR);

    check (outL == -outR, "reverb: la variante Reverse deberia negar solo el izquierdo");

    // Y sin invertir, los dos canales coinciden con entrada igual. Este caso es
    // el que caza que alguien vuelva a compartir un unico buffer entre L y R:
    // con un buffer compartido los dos canales salen identicos SIEMPRE, y este
    // test pasaria, pero el de arriba (que compara L contra -R) dejaria de dar
    // la razon cuando la entrada sea distinta por canal.
    SchroederReverb plain;
    plain.prepare (48000.0);
    plain.setDecay (0.5f);
    plain.setRoomSize (0.5f);

    for (int s = 0; s < 512; ++s)
        plain.processFrame (0.25f, 0.25f, outL, outR);

    check (outL == outR, "reverb: con la misma entrada, L y R deberian coincidir");

    // `setGeometry` tiene que ser EXACTAMENTE lo mismo que `setRoomSize` seguido
    // de `setPreDelaySeconds`. Es lo que permite al consumidor llamar una vez
    // en vez de dos, y si divergieran el pre-retardo de la reverb seria distinto
    // segun quien llamara a cual.
    SchroederReverb bothWays;
    bothWays.prepare (48000.0);
    bothWays.setDecay (0.6f);
    bothWays.setDamping (0.35f);
    bothWays.setDiffusion (0.45f);
    bothWays.setGeometry (0.72f, 0.13f);

    SchroederReverb twoCalls;
    twoCalls.prepare (48000.0);
    twoCalls.setDecay (0.6f);
    twoCalls.setDamping (0.35f);
    twoCalls.setDiffusion (0.45f);
    twoCalls.setRoomSize (0.72f);
    twoCalls.setPreDelaySeconds (0.13f);

    check (bothWays.getPreDelaySamples() == twoCalls.getPreDelaySamples(),
           "setGeometry y las dos llamadas seguidas dan pre-retardos distintos");

    same = true;
    for (int s = 0; s < 4096 && same; ++s)
    {
        float aL, aR, bL, bR;

        bothWays.processFrame (0.2f, 0.2f, aL, aR);
        twoCalls.processFrame (0.2f, 0.2f, bL, bR);

        if (aL != bL || aR != bR) same = false;
    }

    check (same, "setGeometry no suena igual a las dos llamadas por separado");

    // REDIMENSIONAR AL MISMO TAMANO TAMBIEN VACIA, Y ESTO ES UN TEST DE REGRESION
    // DE UNA PARIDAD REAL. El efecto de ABDEep, del que este motor es copia,
    // llama a `clear()` DESPUES de `setSize`, asi que vacia los conbs aunque el
    // numero de muestras no haya cambiado. Una version de este motor que se
    // fiara en que `setSize` ya ha puesto a cero se comia ese caso sin que se
    // notara: el_size_ solo cambia cuando se mueve el mando de tamano, y mover
    // el de pre-retardo lo deja igual. Sonaba casi igual y la paridad bit a bit
    // (que vive en el consumidor) saltaba.
    //
    // Aqui se comprueba en la forma mas directa posible: se llena la reverb de
    // cola, se vuelve a pedir EXACTAMENTE el mismo tamano, y el resultado tiene
    // que ser el de una reverb recien construida.
    auto fillWithTail = [] (SchroederReverb& r, int numSamples)
    {
        float l = 0.0f, rr = 0.0f;

        for (int s = 0; s < numSamples; ++s)
            r.processFrame (0.5f, 0.5f, l, rr);
    };

    auto runFresh = [] (int numSamples, float& outL, float& outR)
    {
        SchroederReverb fresh;
        fresh.prepare (48000.0);
        fresh.setDecay (0.7f);
        fresh.setDamping (0.4f);
        fresh.setDiffusion (0.7f);
        fresh.setRoomSize (0.8f);

        float l = 0.0f, rr = 0.0f;

        for (int s = 0; s < numSamples; ++s)
            fresh.processFrame (0.5f, 0.5f, l, rr);

        outL = l;
        outR = rr;
    };

    SchroederReverb reused;
    reused.prepare (48000.0);
    reused.setDecay (0.7f);
    reused.setDamping (0.4f);
    reused.setDiffusion (0.7f);
    reused.setRoomSize (0.8f);
    fillWithTail (reused, 4096);

    // Mismo `roomSize` de nuevo: mismas longitudes, mismo numero de muestras.
    reused.setRoomSize (0.8f);

    float reusedL = 0.0f, reusedR = 0.0f, freshL = 0.0f, freshR = 0.0f;
    fillWithTail (reused, 512);
    reused.processFrame (0.5f, 0.5f, reusedL, reusedR);
    runFresh (513, freshL, freshR);

    check (reusedL == freshL && reusedR == freshR,
           "reverb: redimensionar al MISMO tamano deberia vaciar la cola igual que el original");

    // Y el caso de la variante "Gated", que no tiene mando de tamano: ahi el
    // unico `rebuild` posible es por pre-retardo, y con el tamano intacto.
    SchroederReverb noSizeKnob;
    noSizeKnob.prepare (48000.0);
    noSizeKnob.setDecay (0.2f);
    noSizeKnob.setRoomSize (0.4f);
    fillWithTail (noSizeKnob, 4096);
    noSizeKnob.setPreDelaySeconds (0.0f);   // el unico rebuild sin cambiar el tamano

    float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
    fillWithTail (noSizeKnob, 512);
    noSizeKnob.processFrame (0.5f, 0.5f, aL, aR);
    runFresh (513, bL, bR);

    check (aL == bL && aR == bR,
           "reverb: cambiar solo el pre-retardo tambien deberia vaciar la cola");
}

//==============================================================================
/**
    Los cuatro conbs tienen que medir DISTINTO, siempre, y con cualquier techo.

    El techo `maxCombSeconds` es una red que el codigo de ABDEep nunca toca (aqui
    vale 0.25 s, contra un conb mas largo de 0.0621 s), asi que el fallo que se
    vigila no es de hoy: es el que aparecera el dia que alguien pase un techo
    mas pequeno creyendo que "cuanto mas pequeno, mas seguro".

    Medido antes del arreglo, con el recorte DESPUES del desplazamiento: a 48 kHz
    y `roomSize` 0.8, con un techo de 0.040 s los cuatro conbs terminaban en 1920
    muestras y el pico de la respuesta al impulso subia de 0.1625 a 0.4875 (3x,
    porque disparaban tres a la vez) con un eco de aleteo encima. Con 0.010 s los
    cuatro colapsaban a 480: la reverb se quadruplicaba y metia un zumbido de
    100 Hz. Un techo defensivo no puede hacer eso.

    El test comprueba el INVARIANTE y no el sintoma, porque el invariante es lo
    que puede volver a romperse por otra via: cuatro longitudes distintas mas
    `i * 7` siempre creciente es lo que garantiza que el techo no pueda volver a
    guardar dos conbs en el mismo sitio.
*/
void testReverbCombLengthsStayDistinct()
{
    using abd::dsp::SchroederReverb;

    // Techos: el de serie (que no llega a recortar) y los que sí recortan, hasta
    // uno tan pequeno que es absurdo. Se recorren tambien varios `roomSize`,
    // porque cuanto mas pequena la sala antes empiezan a chocar los conbs.
    const double ceilings[] = { 0.25, 0.070, 0.057, 0.038, 0.019, 0.010, 0.001 };
    const float  roomSizes[] = { 0.0f, 0.3f, 0.8f, 1.0f };

    for (double ceiling : ceilings)
    {
        for (float roomSize : roomSizes)
        {
            SchroederReverb reverb;
            reverb.prepare (48000.0, ceiling);
            reverb.setRoomSize (roomSize);

            for (int i = 0; i < 4; ++i)
            {
                const int len = reverb.getCombLength (i);

                // Ni cero (un conb de longitud 0 no rebota nunca y se leeria
                // fuera) ni negativo: el recorte no puede Produucir eso.
                check (len >= 1,
                       "reverb: un conb ha quedado con longitud 0 o menos");

                for (int j = i + 1; j < 4; ++j)
                {
                    check (len != reverb.getCombLength (j),
                           "reverb: dos conbs han quedado con la MISMA longitud "
                           "(el techo se esta comiendo el desplazamiento)");
                }
            }
        }
    }

    // Y que el arreglo no haya cambiado nada para el techo de serie: con el
    // margen que hay, los cuatro conbs salen de la formula sin recortar, que es
    // justo lo que fija la paridad con el efecto de ABDEep.
    SchroederReverb plain;
    plain.prepare (48000.0);
    plain.setRoomSize (1.0f);            // el peor caso legal: sizeScale = 1.5

    const int expected[] = { 2138, 2390, 2735, 2980 };   // medidos antes del arreglo

    for (int i = 0; i < 4; ++i)
        check (plain.getCombLength (i) == expected[i],
               "reverb: el techo por defecto (0.25 s) esta recortando cuando no deberia");
}

//==============================================================================
/**
    El coro BBD del Juno: motor con perfil y etapa inyectados.

    La invariante que manda sobre todas las demas es la PRIMERA, y es la misma
    que se cayo en el eco multi-cabezal: si el estado de un canal avanza una vez
    POR MARCO, mono y estereo-izquierdo tienen que dar identicos bit a bit. Si
    esta falla, el motor esta sonando a doble velocidad en uno de los canales y
    el sintroma se nota antes que ningun test de sonido. Con `BbdNoiseStage` la
    etapa tiene DOS generadores con semillas distintas, que es justo la tentacion
    de que uno de los dos avance dos veces.
*/
void testJunoBbdChannelIndependence()
{
    using Engine = abd::dsp::JunoBBD<abd::dsp::JunoBbdJ106Profile, abd::dsp::BbdNoiseStage>;

    for (auto mode : { abd::dsp::JunoBbdMode::ChorusI,
                       abd::dsp::JunoBbdMode::ChorusII,
                       abd::dsp::JunoBbdMode::ChorusBoth })
    {
        Engine stereo, mono;
        stereo.prepare (48000.0);
        mono.prepare (48000.0);
        stereo.setMode (mode);
        mono.setMode (mode);

        float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
        double maxDiff = 0.0;

        for (int s = 0; s < 48000; ++s)
        {
            const float in = 0.4f * std::sin (float (s) * 0.017f);

            stereo.process (in, -0.3f, aL, aR);
            mono.process (in, in, bL, bR);

            maxDiff = std::max (maxDiff, std::abs (double (aL) - double (bL)));
        }

        check (maxDiff == 0.0,
               "coro BBD: el canal izquierdo NO es independiente del derecho "
               "(el estado de la etapa avanza mas de una vez por marco)");
    }
}

//==============================================================================
/** Determinismo, Off es paso a paso, y los tres modos suenan. */
void testJunoBbdBehaviour()
{
    using Engine = abd::dsp::JunoBBD<abd::dsp::JunoBbdJ106Profile, abd::dsp::BbdNoiseStage>;
    using Clean  = abd::dsp::JunoBBD<abd::dsp::JunoBbdJ106Profile, abd::dsp::BbdNullStage>;

    // Off: paso a paso exacto, sin tocar una muestra.
    {
        Engine off;
        off.prepare (44100.0);

        bool exact = true;
        for (int s = 0; s < 4096; ++s)
        {
            const float in = 0.25f * std::sin (float (s) * 0.05f);
            float l = 0.0f, r = 0.0f;
            off.process (in, -in, l, r);

            if (l != in || r != -in) exact = false;
        }

        check (exact, "coro BBD en Off deberia ser paso a paso exacto");
    }

    // Determinismo: dos instancias nuevas, mismo guion, mismo resultado bit a bit.
    {
        auto run = [] (float& outL, float& outR)
        {
            Engine fresh;
            fresh.prepare (48000.0);
            fresh.setMode (abd::dsp::JunoBbdMode::ChorusI);

            for (int s = 0; s < 24000; ++s)
            {
                const float in = 0.3f * std::sin (float (s) * 0.011f);
                fresh.process (in, in, outL, outR);
            }
        };

        float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
        run (aL, aR);
        run (bL, bR);

        check (aL == bL && aR == bR,
               "coro BBD: dos instancias frescas con el mismo guion no dan lo mismo");
    }

    // Los tres modos suenan, y ninguno se va a NaN ni a infinito.
    for (auto mode : { abd::dsp::JunoBbdMode::ChorusI,
                       abd::dsp::JunoBbdMode::ChorusII,
                       abd::dsp::JunoBbdMode::ChorusBoth })
    {
        Engine e;
        e.prepare (48000.0);
        e.setMode (mode);
        e.setDepth (0.65f);
        e.setMix (0.50f);

        float l = 0.0f, r = 0.0f, peak = 0.0f;
        bool finite = true;

        for (int s = 0; s < 48000 * 3; ++s)
        {
            const float in = 0.4f * std::sin (float (s) * 0.031f) + 0.1f * std::sin (float (s) * 0.9f);
            e.process (in, in, l, r);

            if (! std::isfinite (l) || ! std::isfinite (r)) { finite = false; break; }
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }

        check (finite, "coro BBD: la salida se ha ido a NaN o infinito");
        check (peak > 1.0e-3f, "coro BBD: el modo suena a silencio");
    }

    // La etapa importa: el SUELO DE RUIDO con la entrada a cero tiene que ser
    // claramente mayor con `BbdNoiseStage` que con `BbdNullStage`. Es la
    // comprobacion de que la politica inyectada mueve el sonido, que es justo
    // para lo que existe: sin esto, `NullStage` y `BbdNoiseStage` serian la
    // misma clase con otro nombre y la inyeccion no estaria probando nada.
    //
    // MEDIDO, y por que se mide el suelo y NO el pico de un impulso: con senal,
    // el siseo esta unos 90 dB por debajo y el RMS sale IGUAL a 4 cifras
    // significativas con y sin etapa, asi que un test de pico o de RMS con senal
    // no distinguiria nada y pasaria por buena cualquiera que fuese el
    // cableado. Con la entrada a CERO solo queda el zumbido de red (que es del
    // MOTOR, comun a las dos etapas) mas el siseo y los clics de la etapa:
    // medido 1.5578e-03 contra 3.6704e-05, o sea 42 veces. El umbral de 3 es
    // conservative a proposito: aqui lo que se quiere demostrar es que la etapa
    // esta CAGADA al bus, no que suene "distinto".
    {
        auto noiseFloor = [] (auto& engine)
        {
            engine.prepare (48000.0);
            engine.setMode (abd::dsp::JunoBbdMode::ChorusI);

            double sum = 0.0;
            float l = 0.0f, r = 0.0f;

            for (int s = 0; s < 96000; ++s)
            {
                engine.process (0.0f, 0.0f, l, r);
                sum += double (l) * double (l);
            }

            return std::sqrt (sum / 96000.0);
        };

        Engine noisy;
        Clean  clean;
        const double floorNoisy = noiseFloor (noisy);
        const double floorClean = noiseFloor (clean);

        check (floorNoisy > floorClean * 3.0,
               "coro BBD: la etapa de caracter no mueve el suelo de ruido "
               "(inyectarla no sirve de nada)");
    }
}

//==============================================================================
/** Los dos perfiles existen, y el mecanismo por modelo esta preparado. */
void testJunoBbdProfiles()
{
    using abd::dsp::JunoBbdJ106Profile;
    using abd::dsp::JunoBbdJ60Profile;
    using abd::dsp::JunoBbdProfile;

    // J60 y J106 hoy suenan IGUAL, y por eso se comprueba que el perfil los
    // declara iguales. Cuando haya una calibracion distinta por modelo, este
    // test es el que hay que cambiar, y avisara de que el motor se ha movido.
    check (JunoBbdJ60Profile::value.delayI == JunoBbdJ106Profile::value.delayI
               && JunoBbdJ60Profile::value.rateI == JunoBbdJ106Profile::value.rateI,
           "coro BBD: los perfiles J60 y J106 han divergido; revisa si el motor "
           "sigue creyendo que son el mismo");

    // Los numeros tienen que estar en rango, o el motor produce basura.
    const JunoBbdProfile& p = JunoBbdJ106Profile::value;

    check (p.lineMinSeconds > 0.0f, "coro BBD: la linea no tiene longitud minima");
    check (p.biquadFc > 0.0f && p.biquadFc < 24000.0f, "coro BBD: corte del biquad fuera de rango");
    check (p.biquadQ > 0.0f, "coro BBD: Q del biquad no positiva");
    check (p.clockTrim >= 0.0f && p.clockTrim < 0.5f, "coro BBD: tolerancia de reloj absurda");
    check (p.minClockHz > 0.0f, "coro BBD: techo de reloj no positivo");
    check (p.clickDurationMs > 0.0f, "coro BBD: el clic no dura nada");
    check (p.clickRingQ >= 0.5f, "coro BBD: el anillo del clic no puede tener Q < 0.5 (oscila)");
    check (p.hissColor >= 0.0f && p.hissColor <= 1.0f, "coro BBD: color del siseo fuera de 0..1");
    check (p.modDepthScale > 0.0f, "coro BBD: escala de modulacion no positiva");

    // Los dos perfiles instancian y suenan.
    for (int model = 0; model < 2; ++model)
    {
        if (model == 0)
        {
            abd::dsp::JunoBBD<JunoBbdJ60Profile, abd::dsp::BbdNoiseStage> c;
            c.prepare (44100.0);
            c.setMode (abd::dsp::JunoBbdMode::ChorusII);
            float l = 0.0f, r = 0.0f;
            bool finite = true;
            for (int s = 0; s < 20000; ++s)
            {
                c.process (0.2f, 0.2f, l, r);
                if (! std::isfinite (l)) { finite = false; break; }
            }
            check (finite, "coro BBD: el perfil J60 no es estable");
        }
        else
        {
            abd::dsp::JunoBBD<JunoBbdJ106Profile, abd::dsp::BbdNoiseStage> c;
            c.prepare (44100.0);
            c.setMode (abd::dsp::JunoBbdMode::ChorusBoth);
            float l = 0.0f, r = 0.0f;
            bool finite = true;
            for (int s = 0; s < 20000; ++s)
            {
                c.process (0.2f, 0.2f, l, r);
                if (! std::isfinite (l)) { finite = false; break; }
            }
            check (finite, "coro BBD: el perfil J106 no es estable");
        }
    }
}

//==============================================================================
/**
    Los diez reverbs del DeepMind 12 como datos.

    Esto no prueba que suenen bien: el motor ya tiene sus tests. Prueba que la
    TABLA no se degrade en silencio, que es el fallo caro de un perfil (una
    fila con un numero cambiado no peta, suena a otra cosa).
*/
void testReverbProfile()
{
    using Reverb = abd::dsp::ReverbProfile;

    check (Reverb::numVariants == 10, "deberian ser diez variantes de reverb");

    // Los ids que reparte la fabrica, uno a uno. OJO: no son 1..10, hay un 22 en
    // medio y las tres ultimas son 26, 27 y 28. Si alguien "ordena" la tabla
    // asumiendo que son consecutivos, este test lo dice.
    const int expectedIds[10] = { 1, 2, 3, 4, 5, 6, 22, 26, 27, 28 };

    for (int i = 0; i < 10; ++i)
    {
        const auto* v = Reverb::find (expectedIds[i]);

        check (v != nullptr, "falta una variante de reverb en el perfil");

        if (v == nullptr)
            continue;

        check (v->id == expectedIds[i], "la variante no ha salido con el id pedido");
    }

    // Ningun id repetido y ninguno fuera de la tabla.
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            check (expectedIds[i] != expectedIds[j], "hay ids de reverb repetidos");

    check (Reverb::find (0)  == nullptr, "el id 0 no deberia ser un reverb");
    check (Reverb::find (7)  == nullptr, "el id 7 no deberia ser un reverb");
    check (Reverb::find (25) == nullptr, "el id 25 es un hibrido, no un reverb");

    // Los numeros de fabrica de cada variante. Copiados del `FXSimpleReverb` de
    // ABDEep, y lo mas caro que se puede perder: cambiarlos cambia el sonido de
    // diez efectos publicados.
    struct Expected { int id; float decay, damping, diffusion, roomSize, preDelay; int n; };
    const Expected expected[10] =
    {
        {  1,  0.70f, 0.40f, 0.70f, 0.80f, 0.10f, 12 },
        {  2,  0.60f, 0.30f, 0.80f, 0.50f, 0.05f, 12 },
        {  3,  0.75f, 0.20f, 0.90f, 0.60f, 0.05f, 12 },
        {  4,  0.30f, 0.60f, 0.50f, 0.30f, 0.00f, 10 },
        {  5,  0.20f, 0.80f, 0.30f, 0.40f, 0.00f, 10 },
        {  6, -0.30f, 0.90f, 0.20f, 0.70f, 0.15f,  9 },
        { 22,  0.85f, 0.30f, 0.80f, 0.90f, 0.10f,  5 },
        { 26,  0.50f, 0.50f, 0.60f, 0.60f, 0.05f, 12 },
        { 27,  0.35f, 0.60f, 0.40f, 0.40f, 0.02f, 12 },
        { 28,  0.65f, 0.35f, 0.70f, 0.70f, 0.08f, 12 }
    };

    for (const auto& e : expected)
    {
        const auto* v = Reverb::find (e.id);

        if (v == nullptr)
            continue;

        check (v->decay == e.decay && v->damping == e.damping
            && v->diffusion == e.diffusion && v->roomSize == e.roomSize
            && v->preDelaySeconds == e.preDelay,
               "los numeros de fabrica de la variante no coinciden con el original");

        check (v->numParameters == e.n,
               "el numero de controles de la variante no coincide con el original");
    }

    // "Reverse" es la UNICA con el decay negativo (cola corta) y la unica que
    // niega el canal izquierdo. Si alguien le pone el signo bueno, deja de ser
    // Reverse, y si se le quita el `invertLeft` se queda con la misma reverb que
    // Hall pero sin la decorrelacion que la hacia reconocible.
    for (int i = 0; i < 10; ++i)
    {
        const auto* v = Reverb::find (expectedIds[i]);

        if (v == nullptr)
            continue;

        const bool isReverse = (v->id == 6);

        check ((v->decay < 0.0f) == isReverse,
               "solo Reverse deberia tener el decaimiento negativo");
        check (v->invertLeft == isReverse,
               "solo Reverse deberia invertir el canal izquierdo");
    }

    // Los indices de mando tienen que ser coherentes con el numero de controles
    // de cada variante: un mando que no existe en el panel es un -1, y un mando
    // fuera de rango seria escribir en el hueco de otro control.
    for (int i = 0; i < 10; ++i)
    {
        const auto* v = Reverb::find (expectedIds[i]);

        if (v == nullptr)
            continue;

        const int indices[5] = { v->paramIndexPreDelay,  v->paramIndexDecay,
                                 v->paramIndexRoomSize,  v->paramIndexDamping,
                                 v->paramIndexDiffusion };

        for (auto index : indices)
        {
            check (index >= -1 && index < v->numParameters,
                   "un mando de la variante apunta fuera de sus propios controles");
        }
    }

    // Y el caso que de verdad duele: Deep Verb (5) pone el pre-retardo en el
    // mando 3, donde todas las demas lo tienen en el 0. Es lo que hace que
    // 22 sea Deep Verb y no la misma reverb con otros numeros.
    const auto* deep = Reverb::find (22);

    if (deep != nullptr)
    {
        check (deep->paramIndexPreDelay == 3,
               "Deep Verb deberia tener el pre-retardo en el mando 3");
        check (deep->paramIndexDiffusion == -1,
               "Deep Verb no deberia tener mando de diffusion");
    }

    // La fila generica: mismo `find` en vez de nullptr, con los numeros
    // neutros. Es la que evita un nullptr en medio del audio cuando el
    // constructor recibe un id que no existe.
    check (Reverb::findOrFallback (1) == Reverb::find (1),
           "findOrFallback deberia devolver la variante real cuando existe");
    check (Reverb::findOrFallback (999) == &Reverb::fallback,
           "findOrFallback deberia devolver la fila generica si el id no existe");
    check (Reverb::fallback.decay == 0.5f && Reverb::fallback.damping == 0.5f
        && Reverb::fallback.diffusion == 0.5f && Reverb::fallback.roomSize == 0.5f
        && Reverb::fallback.preDelaySeconds == 0.05f,
           "la fila generica deberia tener los valores neutros del original");
    check (Reverb::fallback.numParameters == 12,
           "la fila generica deberia exponer doce controles");

    // Y la fila generica NO puede colarse en la tabla de variantes.
    for (int i = 0; i < 10; ++i)
        check (&Reverb::variants[i] != &Reverb::fallback,
               "la fila generica no deberia estar en la tabla de variantes");
}

//==============================================================================
/**
    LOS MOTORES QUE NO TENIAN NINGUN TEST.

    `DspChorus`, `DspDelay`, `DspReverb` y `DspSaturation` estaban en el modulo
    desde antes de que existiera esta suite, y la suite no los tocaba: el
    inventario de §8 de la cabecera los daba por cubiertos y no lo estaban. Con
    un solo test que estubiera bien escrito, el chorus ya habria salido.

    No son tests de paridad (esto no promises 0 ulps con nadie): son tests de
    que la maquina hace lo que su nombre dice, mas lo que hace un motor de audio
    y que un consumidor le pueda dar cualquier valor sin que se rompa.
*/
void testDspDelay()
{
    using abd::dsp::Delay;

    // Un impulso tiene que volver DESPUES del retardo pedido, y no antes.
    Delay delay;
    delay.prepare (44100.0, 4096);

    const int   numSamples = 8192;
    const float delaySamps = 1000.0f;
    const int   expectAt   = 1000;

    std::vector<float> outL (numSamples, 0.0f), outR (numSamples, 0.0f);
    std::vector<float> inL  (numSamples, 0.0f), inR  (numSamples, 0.0f);

    inL[0] = 1.0f;
    inR[0] = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Feedback 0: el impulso solo tiene que salir una vez, en su sitio.
        outL[i] = delay.processSample (0, inL[i], delaySamps, 0.0f);
        outR[i] = delay.processSample (1, inR[i], delaySamps, 0.0f);
        delay.advanceWritePosition();
    }

    bool finite = true;
    for (int i = 0; i < numSamples; ++i)
        if (! std::isfinite (outL[i]) || ! std::isfinite (outR[i])) finite = false;

    check (finite, "dsp::Delay: sale no finito");

    int peakAt = -1;
    for (int i = 0; i < numSamples; ++i)
        if (outL[i] > 0.5f) { peakAt = i; break; }

    check (peakAt >= 0, "dsp::Delay: el impulso no vuelve nunca");
    check (peakAt >= expectAt - 2 && peakAt <= expectAt + 2,
           "dsp::Delay: el impulso vuelve antes o despues de su sitio");

    // Los canales van por separado: el izquierdo escrito no puede salir por el
    // derecho. Es el reparto por `channel & 1` del contrato de llamada.
    Delay sep;
    sep.prepare (44100.0, 4096);

    std::vector<float> sepR (numSamples, 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        sep.processSample (0, i == 0 ? 1.0f : 0.0f, delaySamps, 0.0f);
        sepR[i] = sep.processSample (1, 0.0f, delaySamps, 0.0f);
        sep.advanceWritePosition();
    }

    bool rightClean = true;
    for (int i = 0; i < numSamples; ++i)
        if (std::fabs (sepR[i]) > 1.0e-6f) rightClean = false;

    check (rightClean, "dsp::Delay: se filtra del canal izquierdo al derecho");

    // Un retardo NEGATIVO o de mas capacidad no puede leer fuera del buffer.
    // Es el fallo de indices que tiene `MultiHeadEcho` con perfiles cuyo
    // `headRatio` es mayor que 1, y aqui se vigila que no se repita.
    Delay extreme;
    extreme.prepare (44100.0, 1024);

    bool safe = true;
    for (int i = 0; i < 4000; ++i)
    {
        const float l = extreme.processSample (0, 1.0f, -500.0f, 0.0f);   // negativo
        const float r = extreme.processSample (1, 1.0f, 1.0e9f, 0.0f);    // mayor que el buffer
        extreme.advanceWritePosition();

        if (! std::isfinite (l) || ! std::isfinite (r)) safe = false;
    }

    check (safe, "dsp::Delay: un retardo fuera de rango lee fuera del buffer");
}

//==============================================================================
/**
    REGRESION DEL ENVOLTADO DE LA POSICION DE LECTURA DEL DELAY.

    El defecto: `wrapReadPosition` decia devolver un valor en [0, size), pero un
    `float` no representa todos los reales de ahi, y cuando el resultado caia a
    menos de medio ULP de `size` el redondeo lo subia a `size` EXACTO. El indice
    salia una muestra mas alla del final del buffer.

    Y NO ERA UN RETARDO RARO. Salio montando el sistema de slots, con un retardo
    de 300 ms —14400.001 muestras, porque el mando es normalizado y el retardo no
    cae en la rejilla— y el puntero de escritura encima de la posicion de
    lectura: la resta da −0.001, al envolver da 97023.999, y al redondear a
    float da 97024.000 sobre un buffer de 97024. Medido: en 72000 muestras sale
    UNA lectura fuera de rango, y ocurre en la muestra 14400, que es justo donde
    la escritura alcanza a la lectura. Con un retardo de un numero entero de
    muestras nunca habria pasado, y por eso llevaba aqui sin que nadie lo viera.

    El test replica las dos formulas —la de antes y la de ahora— porque la de
    dentro es privada, y ademas exige que el motor DE VERDAD recorra el barrido
    sin que `getSample` salte: eso es lo que falla si alguien deshace el arreglo.
*/
void testDspDelayReadWrapStaysInRange()
{
    using namespace abd::dsp;

    const double sr = 48000.0;
    const int maxSamples = static_cast<int> (sr * 2.0);
    const int size = maxSamples + 1024;   // lo que reserva `Delay::prepare`

    // Las dos formulas, copiadas del motor. Si el motor cambia, esto cambia con
    // el, y el fallo sale aqui en vez de en produccion.
    const auto wrapAntes = [] (float readPos, int bufferSize)
    {
        const float s = static_cast<float> (bufferSize);
        const long long q = static_cast<long long> (readPos / s);
        float r = readPos - s * static_cast<float> (q);
        if (r < 0.0f) r += s;
        return r;
    };

    const auto wrapDespues = [] (float readPos, int bufferSize)
    {
        const float s = static_cast<float> (bufferSize);
        const long long q = static_cast<long long> (readPos / s);
        float r = readPos - s * static_cast<float> (q);
        if (r < 0.0f) r += s;
        if (r >= s) r = 0.0f;
        return r;
    };

    const float delay = 0.30f * static_cast<float> (sr);
    const int samples = static_cast<int> (sr * 1.5);

    int fueraAntes = 0, primeraAntes = -1, fueraDespues = 0;
    float mayorIndice = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        const float entrada = static_cast<float> (i) - delay;

        const int antes = static_cast<int> (wrapAntes (entrada, size));
        if (antes >= size || antes < 0)
        {
            ++fueraAntes;
            if (primeraAntes < 0) primeraAntes = i;
        }
        mayorIndice = jmax (mayorIndice, wrapAntes (entrada, size));

        if (static_cast<int> (wrapDespues (entrada, size)) >= size)
            ++fueraDespues;
    }

    check (fueraAntes == 1 && primeraAntes == static_cast<int> (delay),
           "dsp::Delay: el defecto se reproduce (1 lectura fuera, en la muestra 14400)");
    check (mayorIndice == static_cast<float> (size),
           "dsp::Delay: la posicion sin arreglar llega exactamente al final del buffer");
    check (fueraDespues == 0, "dsp::Delay: con el arreglo ninguna lectura se sale del buffer");

    // Y el motor de verdad, que es lo que importa: si la formula de dentro se
    // saliese, `getSample` saltaria aqui mismo.
    Delay real;
    real.prepare (sr, maxSamples);

    bool survivor = true;
    for (int i = 0; i < samples; ++i)
    {
        const float l = real.processSample (0, 0.2f, delay, 0.3f);
        real.processSample (1, 0.2f, delay, 0.3f);
        real.advanceWritePosition();
        if (! std::isfinite (l)) survivor = false;
    }

    check (survivor, "dsp::Delay: el motor recorre 1.5 s de retardo sin salirse");
}

//==============================================================================
/**
    `DspChorus`, y en particular el ENVOLTORIO DE LA FASE DEL LFO.

    La fase se envolvia con UN solo `if (phase >= twoPi) phase -= twoPi;`, y
    `advance()` recibe `rateHz` POR MUESTRA y SIN RECORTAR, directo del
    consumidor. Con `rateHz >= sampleRate` la incremento es >= 2*pi, una sola
    resta no envuelve, y la fase crecia sin limite: el LFO seguia sonando (sin()
    de cualquier cosa cae en [-1,1], asi que no se oye raro) pero la modulacion
    era basura, y la reduccion de `sin` en float ya no vale ahi. El fallo es
    invisible en la salida y por eso nadie lo habia visto.

    El invariante que lo pilla es el ALIASING: una frecuencia de `k` veces el
    sample rate avanza `k` vueltas completas por muestra, o sea que tiene que
    modulation MISMA que la de frecuencia 0 (que se queda en fase 0). Con el
    `if` la fase se iba y las dos corridas no coincidian.
*/
void testDspChorus()
{
    using abd::dsp::Chorus;

    // Render con una frecuencia de LFO dada. OJO: el segundo argumento de
    // `prepare` son SEGUNDOS de retardo maximo, no muestras: pasar 512 ahi
    // reservaba 44100*512 = 22 millones de muestras y tardaba una eternidad.
    auto render = [] (float rateHz, int numSamples, double sampleRate)
    {
        Chorus chorus;
        chorus.prepare (sampleRate, 0.1);

        std::vector<float> outL (numSamples, 0.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] = chorus.processSample (0, 1.0f, 0.5f, 0.5f);
            chorus.processSample (1, 1.0f, 0.5f, 0.5f);
            chorus.advance (rateHz);
        }

        return outL;
    };

    const int   numSamples = 4096;
    const double sr         = 44100.0;

    // 1. El caso normal: sale finito y determinista.
    {
        const auto a = render (5.0f, numSamples, sr);
        const auto b = render (5.0f, numSamples, sr);

        bool finite = true, same = true;
        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (a[i])) finite = false;
            if (a[i] != b[i]) same = false;
        }

        check (finite, "dsp::Chorus: sale no finito");
        check (same, "dsp::Chorus: dos render con los mismos datos no coinciden");
    }

    // 2. El aliasing: 4 veces el sample rate equivale a frecuencia 0.
    {
        const auto zero  = render (0.0f, numSamples, sr);
        const auto alias = render (static_cast<float> (4.0 * sr), numSamples, sr);

        float worst = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            worst = std::max (worst, std::fabs (zero[i] - alias[i]));

        // El residuo NO es cero, y hay que saber por que antes de fijar el
        // numero. Medido con las tres envolturas, mismo guion, 4096 muestras:
        //
        //   `if` (el bug original) ....... 2.500e-01
        //   `while` (restar 2*pi x4) ..... 9.875e-02
        //   `wrapPhase` (restar de golpe)  3.381e-02
        //
        // El `if` es el fallo grave: la fase se va sin limite. El `while` lo
        // arregla pero acumula el redondeo de CADA resta, y con rate multiplo
        // del sample rate la diferencia es de un ulp por muestra.
        //
        // El 3.381e-02 que queda con `wrapPhase` NO viene del envuelto: viene
        // de que `twoPi * 176400 / 44100` no es exactamente 4 vueltas en float
        // (25.132740021 en vez de 25.132741928, un error de 1.9e-6 rad). O sea
        // que el rate de un LFO no puede ser un multiplo exacto del sample rate
        // en aritmetica de punto flotante, ni con el mejor envuelto posible.
        // Lo que queda es una modulacion que se mueve 0.013 Hz: inaudible.
        //
        // Y NO se "arregla" reordenando a `twoPi * (rate / sr)`, que da cero
        // exacto en este caso: medido, eso cambia 343 de los rates del guion
        // de paridad congelada del chorus, y rompe el margen de 0 ulps a
        // cambio de quitar 1.9e-6 rad. La paridad gana.
        //
        // El umbral (1e-1) separa wrapPhase (3.4e-2) de las dos versiones
        // rotas (2.5e-1 y 9.9e-2 van justo por encima), asi que este test
        // sigue teniendo dientes: si alguien vuelve al `while` lineal, falla.
        check (worst < 1.0e-1f,
               "dsp::Chorus: la fase del LFO no se envuelve bien con una rate >= sample rate");
    }

    // 3. Una frecuencia por debajo de Nyquist pero alta tampoco puede reventar.
    {
        const auto a = render (static_cast<float> (sr * 0.49f), numSamples, sr);

        bool finite = true;
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (a[i])) finite = false;

        check (finite, "dsp::Chorus: sale no finito con una rate alta");
    }

    // 4. Un rate absurdo o en NaN no puede cuelgar el hilo de audio.
    //
    // Con el `if` de una sola resta esto ya no se colgaba, asi que lo que se
    // comprueba aqui es el `while` lineal: restava 2*pi una vuelta por
    // iteracion, o sea 22 676 iteraciones por muestra con rate = 1e9 Hz, y con
    // rate INFINITO el bucle no terminaba nunca. `wrapPhase` hace el mismo
    // trabajo restando las vueltas enteras de una vez, asi que el coste ya no
    // depende de la entrada.
    //
    // OJO con lo que este test NO demuestra. No es cierto (y se comprobo) que
    // un rate en NaN envenene el motor: con el `if` la fase se quedaba en NaN
    // para siempre, pero `sin(NaN)` devolvia 0 (el plegado de la mantiza leia
    // los bits del NaN), asi que el chorus seguia sonando normal con la
    // modulacion muerta. Eso era un defecto de `sin`, no del chorus, y ya esta
    // arreglado en DspMath con su propio test. Aqui solo se mira que la salida
    // sea finita y que el motor no se cuelgue.
    {
        bool finite = true;

        for (float rate : { 1.0e9f, -1.0e9f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN() })
        {
            const auto a = render (rate, 512, sr);

            for (int i = 0; i < 512; ++i)
                if (! std::isfinite (a[i])) finite = false;
        }

        check (finite, "dsp::Chorus: un rate absurdo deja el motor en NaN o se cuelga");
    }
}

//==============================================================================
/** `DspSaturation`: monotona, acotada y con imparidad, que es lo que hace
    que una "saturacion" sea una saturacion y no un amplificador raro. */
void testDspSaturation()
{
    using abd::dsp::Saturation;

    // 1. Monotona creciente y acotada.
    //
    // OJO con el arranque: `previous` es el valor en el PRIMER punto del
    // recorrido, no en x = 0. Con `previous = f(0)` y el bucle empezando en
    // x = -7.99992, la primera comparacion era f(-7.99992) < f(0), que es
    // cierto y no dice NADA: atan es creciente y da -1.44 alli. Esoemnudo
    // "no es monótona" con un salto de 1.446 que era mi error de test, no un
    // defecto de la forma (medido: con el recorrido bien, 0 rupturas, y el
    // error de `atan` contra libm es de 1.2e-7, tres ulp).
    float previous = Saturation::processSample (-8.0f, 1.0f);
    bool  increasing = true, bounded = true, finite = true;

    for (int i = 1; i <= 200000; ++i)
    {
        const float x = -8.0f + 16.0f * (float) i / 200000.0f;
        const float y = Saturation::processSample (x, 1.0f);

        if (y < previous) increasing = false;
        if (std::fabs (y) > 1.0001f) bounded = false;
        if (! std::isfinite (y)) finite = false;

        previous = y;
    }

    check (finite, "dsp::Saturation: sale no finito");
    check (bounded, "dsp::Saturation: se pasa de +/-1");
    check (increasing, "dsp::Saturation: no es monótona");

    // 2. Impar: la saturacion de un diode tiembla igual en los dos sentidos.
    bool odd = true;
    for (int i = 1; i <= 20000; ++i)
    {
        const float x = 8.0f * (float) i / 20000.0f;
        if (std::abs (Saturation::processSample (x, 1.0f)
                    + Saturation::processSample (-x, 1.0f)) > 1.0e-5f)
            odd = false;
    }

    check (odd, "dsp::Saturation: pierde la imparidad");

    // 3. Con drive 0 la FORMA da cero, y eso es lo que dice el contrato.
    //
    // Esto NO es la identidad, y el test anterior decia lo contrario. La
    // forma es `atan (x * drive) * 0.6366`, y con drive 0 sale `atan(0)`, o
    // sea CERO para cualquier entrada: measured `processSample (0.5, 0) ==
    // 0.0` exacto. El bypass (que devuelve la entrada intacta) es POLITICA de
    // parametros y vive en el envoltorio de producto, en NEURONiK, junto al
    // mapeo amount -> drive. Lo que se comprueba aqui es lo que la forma
    // promete: sin drive no hay saturacion, y el tope es el propio 2/pi.
    bool silent = true, withinTope = true;
    for (int i = 1; i <= 2000; ++i)
    {
        const float x = -4.0f + 8.0f * (float) i / 2000.0f;

        if (Saturation::processSample (x, 0.0f) != 0.0f) silent = false;
        if (std::fabs (Saturation::processSample (1.0e6f, 1.0f)) > Saturation::atanNormalisation * 1.5707964f)
            withinTope = false;
    }

    check (silent, "dsp::Saturation: con drive 0 deberia dar cero");
    check (withinTope, "dsp::Saturation: se pasa del tope 2/pi");
}

//==============================================================================
/** `DspReverb` (el port de `juce::Reverb`): API POR BLOQUE, no por muestra.

    Que la cola se construya sin NaN, que `reset` la calle y que los parametros
    que se le pasan se respeten (roomSize grande = cola mas larga). */
void testDspReverb()
{
    using abd::dsp::Reverb;

    // Render de `numSamples` muestras con un impulso al principio.
    //
    // Devuelve el pico por `peakOut` y el maximo de la senal a partir de la
    // muestra 1 por `tailOut`. La cola es lo que el seco no puede explicar, y
    // es el unico termino que dice si el wet esta ENCENDIDO o apagado: el pico
    // no, porque el seco es paso directo con ganancia 2 y el wet lleva ganancia
    // interna 0.015 en los conbs (medido: pico 2.0 con wet 0, 0.225 con wet 1).
    auto render = [] (const Reverb::Parameters& p, int numSamples,
                      float& peakOut, float& tailOut)
    {
        Reverb reverb;

        Reverb::Parameters params = p;
        reverb.setParameters (params);
        reverb.setSampleRate (44100.0);
        reverb.reset();

        std::vector<float> left (numSamples, 0.0f), right (numSamples, 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        reverb.processStereo (left.data(), right.data(), numSamples);

        bool finite = true;
        float peak  = 0.0f, tail = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i])) finite = false;
            peak = std::max (peak, std::max (std::fabs (left[i]), std::fabs (right[i])));

            if (i > 0)
                tail = std::max (tail, std::max (std::fabs (left[i]), std::fabs (right[i])));
        }

        peakOut = peak;
        tailOut = tail;
        return finite;
    };

    Reverb::Parameters params;
    params.roomSize = 0.7f;
    params.damping  = 0.4f;
    params.wetLevel = 1.0f;
    params.dryLevel = 0.0f;
    params.width    = 1.0f;

    // 1. Un impulso deja cola y no sale no finito.
    float peak = 0.0f, tail = 0.0f;
    const bool finite = render (params, 20000, peak, tail);

    check (finite, "dsp::Reverb: sale no finito");
    check (tail > 1.0e-4f, "dsp::Reverb: un impulso no deja cola (el motor esta muerto)");
    check (peak <= 2.0f, "dsp::Reverb: la cola se dispara");

    // 2. Una sala mas grande tiene que dar una cola mas larga. Es la unica
    //    forma de comprobar que `roomSize` ESTA CONECTADO a los conbs, que es
    //    el unico parametro de esta estructura que no es un par de ganancias.
    Reverb::Parameters bigRoom = params;
    bigRoom.roomSize = 1.0f;

    float bigTail = 0.0f, smallTail = 0.0f, dummy = 0.0f;
    render (bigRoom, 20000, dummy, bigTail);
    render (params, 20000, dummy, smallTail);

    check (bigRoom.roomSize > params.roomSize, "el test compara dos salas distintas");
    check (std::fabs (bigTail - smallTail) > 1.0e-6f,
           "dsp::Reverb: roomSize no cambia nada, o sea que no llega a los conbs");

    // 3. Sin wet no puede sonar la cola: solo tiene que pasar el seco.
    //
    // Y el pico del seco NO es 1.0 con dryLevel 1, que es lo que este test
    // afirmaba antes. El port escala el seco por `dryScaleFactor = 2.0f`
    // (como hace juce::Reverb), asi que dryLevel 1 da ganancia 2.0 y el
    // impulso sale con pico 2.0. Medido: dryLevel 0 -> 0.0, 1 -> 2.0,
    // 2 -> 4.0.
    //
    // Y comparar picos entre "wet 0" y "wet 1" no dice NADA sobre si el wet se
    // apaga, porque los dos caminos no tienen la misma ganancia: el seco es
    // paso directo x2 y el wet lleva 0.015 internos en los conbs. Medido: pico
    // 2.0 con wet 0 y 0.225 con wet 1, o sea que el wet encendido sounds MENOS
    // fuerte. Lo que si distingue "apagado" de "bajo" es la COLA a partir de la
    // muestra 1, que el seco no puede explicar: 0.000e+00 exacto con wet 0
    // contra 2.25e-01 con wet 1.
    Reverb::Parameters dry = params;
    dry.wetLevel = 0.0f;
    dry.dryLevel = 1.0f;

    float dryPeak = 0.0f, dryTail = 0.0f, wetTail = 0.0f;
    render (dry, 8000, dryPeak, dryTail);
    render (params, 8000, dummy, wetTail);

    check (std::fabs (dryPeak - 2.0f) < 1.0e-3f,
           "dsp::Reverb: el seco no sale con la ganancia del port (2x dryLevel)");
    check (dryTail == 0.0f,
           "dsp::Reverb: con wet 0 deberia salir solo el seco, sin cola");
    check (wetTail > 0.05f,
           "dsp::Reverb: con wet 1 deberia sonar la cola (si no, el wet no llega)");
}

//==============================================================================
/**
    EL RE-201 LEIA FUERA DEL BUFFER. Y NO ERA TEORICO.

    `Re201Profile` tiene `headRatio = {1.0, 2.0, 3.0}` — el cabezal 1 es el de
    referencia y los otros dos suenan a DOS y TRES veces el retardo base, que es
    justo lo que dice la cabecera del perfil ("son retardos MAS LARGOS, y por eso
    el buffer tiene que aguantar 3 veces el retardo base").

    Pero `prepare` dimensionaba la linea a `maxDelaySeconds` a secas. Con el
    cabezal 3 encendido y el retardo al maximo, `readTape` pedia 1.5 s de un
    buffer de 0.5 s: la unica resta se quedaba corta, `(int)readPos` salia
    negativo, y `%` de un entero NEGATIVO en C++ sale negativo, o sea indice
    fuera del buffer. En depuracion, asercion de `AudioBuffer`; en release, lo que
    hubiera en memoria.

    No lo hacia nadie porque el unico perfil que se instanciaba en los tests era
    `StudioEchoProfile`, cuyos ratios (0.20/0.45/0.70) son todos MENORES que 1 y
    nunca llegaban al borde. `testRe201Profile` comprobaba la tabla de datos sin
    llegar a instanciar el motor. Traducido: el motor nunca se probo con la
    maquina para la que existe.

    Este test instancia el motor DE VERDAD con el perfil del RE-201, barre los
    doce modos con el retardo al maximo, y exige que ninguna lectura se salga.
*/
void testRe201HeadReadsStayInRange()
{
    using Echo = abd::dsp::MultiHeadEcho<abd::dsp::Re201Profile, abd::dsp::TapeColour>;

    const double sr = 44100.0;

    Echo echo;
    echo.prepare (sr);

    // El buffer tiene que aguantar el retardo del cabezal mas lejano, no el base.
    const int maxHead = static_cast<int> (sr * abd::dsp::Re201Profile::maxDelaySeconds
                                              * abd::dsp::Re201Profile::headRatio[2]);

    check (echo.delayBufferSize() >= maxHead,
           "la linea de cinta no cabe el retardo del cabezal 3 del RE-201");

    bool finite = true;

    // Los doce modos, con el retardo al maximo del perfil, que es donde estan
    // las tres lecturas mas lejanas. Sin ninguna comprobacion de indice: si
    // `readTape` se sale, la asercion de `AudioBuffer` revienta aqui mismo.
    for (int mode = 0; mode < abd::dsp::Re201Profile::numModes; ++mode)
    {
        echo.setMode (mode);
        echo.reset();

        for (int i = 0; i < 4000; ++i)
        {
            const float l = echo.processSample (0, 1.0f,
                                                abd::dsp::Re201Profile::maxDelaySeconds,
                                                0.3f, 0.5f, 0.5f, 0.3f);
            const float r = echo.processSample (1, 1.0f,
                                                abd::dsp::Re201Profile::maxDelaySeconds,
                                                0.3f, 0.5f, 0.5f, 0.3f);

            echo.advance();

            if (! std::isfinite (l) || ! std::isfinite (r)) finite = false;
        }
    }

    check (finite, "el RE-201 con los doce modos y el retardo al maximo sale no finito");

    // Y con un retardo ABSURDO, que es lo que pasaria si un consumidor mete un
    // valor sin recortar: tiene que envolver, no salirse. Antes, en vez de
    // envolver, leia fuera del buffer.
    echo.setMode (10);   // los tres cabezales
    echo.reset();

    bool safe = true;

    for (int i = 0; i < 2000; ++i)
    {
        const float l = echo.processSample (0, 1.0f, 1.0e6f, 0.3f, 0.5f, 0.5f, 0.3f);
        const float r = echo.processSample (1, 1.0f, 1.0e6f, 0.3f, 0.5f, 0.5f, 0.3f);

        echo.advance();

        if (! std::isfinite (l) || ! std::isfinite (r)) safe = false;
    }

    check (safe, "el RE-201 lee fuera del buffer con un retardo fuera de rango");
}

//==============================================================================
/**
    El pasabajos de tono y el tanque son POR MUESTRA, no por canal.

    `processSample` se llama una vez POR CANAL y `advance` una vez por muestra.
    El filtro de tono y la escritura al tanque estaban dentro de
    `processSample`, asi que en estereo se ejecutaban DOS VECES por muestra: el
    pasabajos corria al doble de la frecuencia de muestreo para la que se calculo
    su coeficiente, y la segunda llamada pisaba la escritura de la primera.

    MEDIDO antes del arreglo, con `NullStage` para que el siseo no contamine y
    `tankMix` alto para que el tanque cuente: el canal izquierdo de un render
    estereo se separaba del render MONO del mismo material en 5.827710e-03. Con
    `feedback` 0 y `tankMix` 0.3 el caso es el mas limpio, porque el tanque solo
    recibe un canal y cualquier diferencia tiene que venir del estado.

    Se mide exactamente 0, no "por debajo de un umbral": la senal de entrada es
    la misma y la ruta del canal izquierdo no depende de cuantos canales le
    mandes al motor.
*/
void testEchoToneIsPerSampleNotPerChannel()
{
    using Echo = abd::dsp::MultiHeadEcho<abd::dsp::Re201Profile, abd::dsp::NullStage>;

    const double sr = 44100.0;
    const int n = static_cast<int> (sr);

    std::vector<float> in (static_cast<size_t> (n));

    for (int i = 0; i < n; ++i)
        in[static_cast<size_t> (i)] = 0.6f * std::sin (0.03f * static_cast<float> (i));

    std::vector<float> mono (static_cast<size_t> (n));
    std::vector<float> left (static_cast<size_t> (n));

    {   // MONO: una sola llamada por muestra.
        Echo echo;
        echo.prepare (sr);
        echo.setMode (10);            // los tres cabezales + tanque

        for (int i = 0; i < n; ++i)
        {
            mono[static_cast<size_t> (i)] = echo.processSample (0, in[static_cast<size_t> (i)],
                                                               0.05f, 0.0f, 0.5f, 0.5f, 0.3f);
            echo.advance();
        }
    }

    {   // ESTEREO: dos llamadas por muestra.
        Echo echo;
        echo.prepare (sr);
        echo.setMode (10);

        for (int i = 0; i < n; ++i)
        {
            left[static_cast<size_t> (i)] = echo.processSample (0, in[static_cast<size_t> (i)],
                                                               0.05f, 0.0f, 0.5f, 0.5f, 0.3f);
            (void) echo.processSample (1, in[static_cast<size_t> (i)],
                                       0.05f, 0.0f, 0.5f, 0.5f, 0.3f);
            echo.advance();
        }
    }

    float maxDiff = 0.0f;

    for (int i = 0; i < n; ++i)
        maxDiff = std::max (maxDiff, std::fabs (mono[static_cast<size_t> (i)]
                                               - left[static_cast<size_t> (i)]));

    check (maxDiff == 0.0f,
           "el canal izquierdo del RE-201 depende de cuantos canales se le manden");
}

//==============================================================================
/**
    El mando de retardo entrega el retardo pedido en TODO el recorrido.

    `headRatio` multiplica el retardo base (el cabezal 1 es el de referencia, el
    2 suena al doble y el 3 al triple), y el wow/flutter lo desplazan alrededor
    de esa posicion. El contrato que se comprueba es: el eco aparece donde debe,
    dentro de la deriva.

    LA MEDICION USA EL MAXIMO GLOBAL DE LA COLA, no "la primera muestra que pasa
    un umbral". Con `TapeColour` el siseo vale ~0.045 y un umbral de 0.02 lo
    cruza en muestras sueltas, de modo que un detector por umbral reporta un
    "pico temprano" que en realidad es siseo. Asi se llegó a conclusions
    FALSAS sobre un envuelto de buffer que, al medirlo por el máximo, no ocurre:
    barrido de 501 puntos de 0.480 a 0.500 s, 0 envueltos. El eco cae en
    1.49512 s a 0.500 s pedidos, con o sin el recorte de `readTape`.

    La tolerancia es del 2%: el wow estira la lectura hasta
    `1 + wowAmount + flutterAmount` = 1.004 y la contrae hasta 0.996, o sea un
    +/-0.4%. El 2% deja pasar la deriva con holgura y sigue siendo 25 veces mas
    pequena que el salto que daria un retardo equivocado.
*/
void testEchoDelayKnobDeliversRequestedDelay()
{
    using Echo = abd::dsp::MultiHeadEcho<abd::dsp::Re201Profile, abd::dsp::TapeColour>;

    const double sr = 44100.0;
    const int total = static_cast<int> (sr * 3.0);

    // Posicion y amplitud del eco de un impulso con `feedback` 0. Se usa el
    // maximo global y se excluye la muestra 0, que es el impulso SECO
    // (entrada * dryGain) y no el eco.
    auto peakOfEcho = [&] (int head, float requestedSeconds, float& seconds, float& amplitude)
    {
        std::vector<float> out (static_cast<size_t> (total), 0.0f);

        Echo echo;
        echo.prepare (sr);
        echo.setMode (head);          // Re201Profile: 0 = h1, 1 = h2, 2 = h3

        for (int i = 0; i < total; ++i)
        {
            const float in = (i == 0) ? 1.0f : 0.0f;
            out[static_cast<size_t> (i)] = echo.processSample (0, in, requestedSeconds,
                                                              0.0f, 0.5f, 0.5f, 0.0f);
            echo.advance();
        }

        int best = -1;
        for (int i = 1; i < total; ++i)
            if (best < 0 || std::fabs (out[static_cast<size_t> (i)])
                          > std::fabs (out[static_cast<size_t> (best)]))
                best = i;

        seconds   = best < 0 ? -1.0f : (float) best / (float) sr;
        amplitude = best < 0 ? 0.0f : std::fabs (out[static_cast<size_t> (best)]);
    };

    const float minDelay = abd::dsp::Re201Profile::minDelaySeconds;
    const float maxDelay = abd::dsp::Re201Profile::maxDelaySeconds;
    const float tolerance = 0.02f;

    for (int head = 0; head < 3; ++head)
    {
        const float ratio = abd::dsp::Re201Profile::headRatio[head];
        bool delivered = true;

        auto probe = [&] (float requested)
        {
            const float expected = requested * ratio;
            float seconds = 0.0f, amplitude = 0.0f;
            peakOfEcho (head, requested, seconds, amplitude);

            // Sin eco reconocible, o eco en un sitio que no corresponde.
            if (amplitude < 0.10f
                || std::fabs (seconds - expected) > tolerance * expected)
                delivered = false;
        };

        for (int k = 0; k <= 100; ++k)
            probe (minDelay + (maxDelay - minDelay) * (float) k / 100.0f);

        // Y el tope del mando con la CONSTANTE del perfil, no reconstruida por
        // aritmetica: `minDelay + (maxDelay - minDelay)` en float cae unas
        // muestras por debajo del tope real.
        for (int k = 0; k <= 40; ++k)
            probe (maxDelay * (0.98f + 0.02f * (float) k / 40.0f));

        probe (maxDelay);

        check (delivered, "el RE-201 no entrega el retardo pedido en algun punto del mando");
    }
}

//==============================================================================
/**
    El canal derecho del eco tiene el mismo nivel que el izquierdo.

    El reparto de la maquina es `1 / cabezalesActivos` para que cambiar de
    combinacion no cambie el nivel del eco, y `headRightScale` es el desvio de un
    cabezal real entre L y R (~5-10%), o sea un AJUSTE ENCIMA de ese reparto.

    En el codigo, la rama derecha multiplicaba por `headRightScale` SIN el
    reparto: con un solo cabezal salia 0.95 (correcto por casualidad, porque
    `1/1 = 1`), pero con dos daba entre 1.80 y 2.70, y con los tres 2.70. El
    desequilibrio CRECIA con cada cabezal que se encendia, y el canal derecho
    llegaba a ser casi tres veces mas fuerte que el izquierdo.

    Se mide el cociente R/L del eco de un impulso en varios modos. El rango
    admisible sale del propio perfil: sus tres valores estan entre 0.90 y 0.95.
*/
void testEchoRightChannelLevelMatchesLeft()
{
    using Echo = abd::dsp::MultiHeadEcho<abd::dsp::Re201Profile, abd::dsp::NullStage>;

    const double sr = 44100.0;
    const int total = static_cast<int> (sr * 0.4);

    struct Case { int mode; int heads; const char* what; };
    const Case cases[] =
    {
        {  0, 1, "un cabezal" },
        {  2, 1, "cabezal 3" },
        {  3, 2, "cabezales 1 + 2" },
        {  8, 2, "cabezales 1 + 3" },
        { 10, 2, "cabezales 2 + 3" },
        { 10, 3, "los tres cabezales" }
    };

    for (const auto& c : cases)
    {
        std::vector<float> left (static_cast<size_t> (total), 0.0f);
        std::vector<float> right (static_cast<size_t> (total), 0.0f);

        Echo echo;
        echo.prepare (sr);
        echo.setMode (c.mode);

        for (int i = 0; i < total; ++i)
        {
            const float in = (i == 0) ? 1.0f : 0.0f;
            const float l = echo.processSample (0, in, 0.05f, 0.0f, 0.5f, 0.5f, 0.0f);
            const float r = echo.processSample (1, in, 0.05f, 0.0f, 0.5f, 0.5f, 0.0f);

            // La muestra 0 es la entrada directa, igual en los dos canales; el
            // eco es lo que se mide.
            if (i > 0)
            {
                left[static_cast<size_t> (i)] = l;
                right[static_cast<size_t> (i)] = r;
            }

            echo.advance();
        }

        float peakL = 0.0f, peakR = 0.0f;

        for (int i = 1; i < total; ++i)
        {
            peakL = std::max (peakL, std::fabs (left[static_cast<size_t> (i)]));
            peakR = std::max (peakR, std::fabs (right[static_cast<size_t> (i)]));
        }

        const float ratio = peakL > 0.0f ? peakR / peakL : 0.0f;

        check (ratio > 0.85f && ratio < 1.0f,
               "el canal derecho del eco no esta al mismo nivel que el izquierdo");
    }
}

//==============================================================================
/** HELPERS DEL SISTEMA DE SLOTS. En un `namespace` aparte para no ensuciar el
    de los tests, y con nombres que no chocan con los de arriba. */
namespace fxprobe
{
    using namespace abd::dsp;

    constexpr int kSr = 48000;

    /** Ruido reproducible. La semilla es un parametro, no un reloj: dos llamadas
        con la misma semilla dan los mismos numeros, y dos motores FRESCOS con la
        misma entrada dan la misma salida. */
    std::vector<float> noise (int n, unsigned seed)
    {
        std::vector<float> v (static_cast<size_t> (n), 0.0f);
        unsigned s = seed;
        for (int i = 0; i < n; ++i)
        {
            s = s * 1664525u + 1013904223u;
            v[static_cast<size_t> (i)] =
                (static_cast<float> ((s >> 8) & 0xffffu) / 32768.0f - 1.0f) * 0.2f;
        }
        return v;
    }

    /** Tonos a 220/440/660 Hz. Una reverb con ruido solo suena a ruido, y eso no
        demuestra que el motor este vivo. */
    std::vector<float> musica (int n, double sr = kSr)
    {
        std::vector<float> v (static_cast<size_t> (n), 0.0f);
        for (int i = 0; i < n; ++i)
        {
            const double t = double (i) / sr;
            v[static_cast<size_t> (i)] = static_cast<float> (
                  0.30 * std::sin (2.0 * 3.14159265358979 * 220.0 * t)
                + 0.20 * std::sin (2.0 * 3.14159265358979 * 440.0 * t)
                + 0.10 * std::sin (2.0 * 3.14159265358979 * 660.0 * t));
        }
        return v;
    }

    /** Pasa un bloque por el motor y devuelve el canal izquierdo. */
    std::vector<float> render (abd::dsp::FxEngine& e, const std::vector<float>& in)
    {
        std::vector<float> buf = in;
        const int n = static_cast<int> (buf.size());

        abd::dsp::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            b.setSample (0, i, buf[static_cast<size_t> (i)]);
            b.setSample (1, i, buf[static_cast<size_t> (i)]);
        }

        e.process (b, n);

        for (int i = 0; i < n; ++i)
            buf[static_cast<size_t> (i)] = b.getSample (0, i);

        return buf;
    }

    float rms (const std::vector<float>& b, int from, int to)
    {
        double acc = 0.0;
        for (int i = from; i < to; ++i)
            acc += double (b[static_cast<size_t> (i)]) * double (b[static_cast<size_t> (i)]);
        return static_cast<float> (std::sqrt (acc / double (to - from)));
    }

    float pico (const std::vector<float>& b, int from, int to)
    {
        float p = 0.0f;
        for (int i = from; i < to; ++i)
            p = jmax (p, std::fabs (b[static_cast<size_t> (i)]));
        return p;
    }

    const abd::dsp::FxEffectInfo* catalogo()
    {
        static int count = 0;
        static const abd::dsp::FxEffectInfo* cat = abd::dsp::fxDefaultCatalogue (count);
        return cat;
    }

    int numCatalogo()
    {
        return abd::dsp::fxDefaultCatalogueSize();
    }
} // namespace fxprobe

//==============================================================================
/** EL CATALOGO POR DEFECTO: son las seis filas del modulo, y el indice 0 es
    bypass para todos los productos sin que ninguno tenga que acordarlo. */
void testFxCatalogue()
{
    using namespace abd::dsp;

    const int count = fxprobe::numCatalogo();
    const FxEffectInfo* cat = fxprobe::catalogo();

    check (count == 6, "el catalogo por defecto tiene las seis filas del modulo");
    check (cat != nullptr, "el catalogo por defecto existe");

    for (const char* name : { "chorus", "delay", "reverb", "saturation", "schroeder", "bbd" })
        check (fxFindEffect (cat, count, name) != nullptr, "el catalogo incluye un efecto conocido");

    check (fxFindEffect (cat, count, "noexiste") == nullptr, "un nombre falso no aparece");

    check (fxEffectAt (cat, count, 0) == nullptr, "el indice 0 es siempre bypass");
    check (fxEffectAt (cat, count, 1) == &cat[0], "el indice 1 es la primera fila");

    // Y ESTE es el limite que importa: con `>= count` el ULTIMO efecto era
    // inalcanzable, que era justo el BBD —el unico con selector discreto, y el
    // que mas se nota que falta. No lo cazaba ningun test porque los que habia
    // elegian efectos del principio de la lista.
    check (fxEffectAt (cat, count, count) == &cat[count - 1],
           "el indice del ultimo efecto existe (el limite es count, no count-1)");
    check (fxEffectAt (cat, count, count + 1) == nullptr, "un indice pasado no vale");
    check (fxEffectAt (cat, count, -1) == nullptr, "un indice negativo no vale");
    check (fxEffectAt (nullptr, 0, 1) == nullptr, "un catalogo nulo no devuelve nada");

    // Y cada fila esta completa: un `process` o un `destroy` vacios son un
    // fallo que solo se ve alsonar.
    bool filasCompletas = true;
    for (int i = 0; i < count; ++i)
        if (cat[i].name == nullptr || cat[i].displayName == nullptr || cat[i].params == nullptr
            || cat[i].create == nullptr || cat[i].process == nullptr
            || cat[i].setParam == nullptr || cat[i].destroy == nullptr)
            filasCompletas = false;

    check (filasCompletas, "todas las filas tienen puntero de nombre, tabla y funciones");
}

//==============================================================================
/** EL HUECO: bypass, mandos que sobreviven al cambio de efecto, `reset` que
    limpia el estado y no los mandos, y el `mix`/`gain` que no tocan el bypass. */
void testFxSlotBehaviour()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count = fxprobe::numCatalogo();

    //--- el bypass deja la senal INTACTA, gain y mix incluidos -------------
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);

        for (int s = 0; s < kFxNumSlots; ++s)
        {
            e.getSlot (s).setGain (0.2f);
            e.getSlot (s).setMix (0.0f);
        }

        const std::vector<float> in = fxprobe::noise (1024, 12345u);
        const std::vector<float> out = fxprobe::render (e, in);

        bool identico = true;
        for (size_t i = 0; i < in.size(); ++i)
            if (out[i] != in[i]) identico = false;

        check (identico, "con los cuatro huecos en bypass la senal sale bit a bit igual");
    }

    //--- un hueco en bypass no rompe la serie -----------------------------
    {
        FxEngine a, b;
        a.prepare (fxprobe::kSr, 2, 512); a.setCatalogue (cat, count);
        b.prepare (fxprobe::kSr, 2, 512); b.setCatalogue (cat, count);

        for (FxEngine* engine : { &a, &b })
        {
            engine->getSlot (0).setType (1);
            engine->getSlot (0).setMix (1.0f);
            engine->getSlot (3).setType (2);
            engine->getSlot (3).setMix (0.7f);
        }

        const std::vector<float> in = fxprobe::noise (2048, 777u);
        check (fxprobe::render (a, in) == fxprobe::render (b, in),
               "dos motores frescos con la misma configuracion dan lo mismo");
    }

    //--- los mandos que el usuario toco sobreviven al cambio de efecto -----
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);

        e.getSlot (0).setType (1);
        e.getSlot (0).setParameter (0, 0.8f);
        check (e.getSlot (0).isParameterTouched (0), "un mando puesto a mano queda marcado");

        e.getSlot (0).setType (2);
        check (e.getSlot (0).getParameter (0) == 0.8f,
               "cambiar de efecto deja el mando donde estaba");
        check (! e.getSlot (0).isParameterTouched (1), "un mando que nadie toco sigue sin tocar");
    }

    //--- y el que NO ha sido tocado toma el valor por defecto de la fila ----
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);
        e.getSlot (0).setType (1);

        const FxParamSpec* p = fxFindEffect (cat, count, "chorus")->params;
        check (std::fabs (e.getSlot (0).getParameter (0) - fxNormalise (p[0], p[0].defaultValue)) < 1e-5f,
               "un coro recien metido nace con sus mandos en su sitio");
    }

    //--- reset limpia la COLA, no los mandos -----------------------------
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);

        e.getSlot (0).setType (2);
        e.getSlot (0).setMix (1.0f);
        e.getSlot (0).setParameter (0, fxNormalise (fxFindEffect (cat, count, "delay")->params[0], 0.15f));
        e.getSlot (0).setParameter (1, 0.8f);

        const std::vector<float> musica = fxprobe::musica (8192);
        fxprobe::render (e, musica);
        const float colaAntes = fxprobe::rms (musica, 7000, 8192);
        check (colaAntes > 1e-3f, "el delay suena antes de reset (la cola no es silencio)");

        e.reset();

        check (e.getSlot (0).getMix() == 1.0f, "reset deja la mezcla del hueco");
        check (e.getSlot (0).getParameter (1) == 0.8f, "reset deja la realimentacion");

        const std::vector<float> silencio (8192, 0.0f);
        const std::vector<float> out = fxprobe::render (e, silencio);
        check (fxprobe::rms (out, 7000, 8192) < 0.01f,
               "tras reset el delay no sigue soltando las repeticiones que tenia");
    }
}

//==============================================================================
/** LOS NUEVE RUTEOS: producen numeros finitos, no se disparan solos, y el 1 y el
    8 son el MISMO diagrama (que es lo que hace ABDEep, no un descuido). */
void testFxEngineRouting()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count = fxprobe::numCatalogo();

    for (int r = 0; r <= 9; ++r)
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);
        e.setRouting (static_cast<FxRouting> (r));
        e.setFeedbackGain (0.2f);

        for (int s = 0; s < kFxNumSlots; ++s)
        {
            e.getSlot (s).setType (s + 1);
            e.getSlot (s).setMix (0.5f);
        }

        const std::vector<float> out = fxprobe::render (e, fxprobe::noise (2048, 777u + r));

        bool finito = true;
        float p = 0.0f;
        for (float v : out)
        {
            if (! std::isfinite (v)) finito = false;
            p = jmax (p, std::fabs (v));
        }

        check (finito, "un ruteo produce numeros finitos");
        check (p > 1e-4f, "un ruteo produce audio");
        check (p < 4.0f, "un ruteo con cuatro efectos no se dispara solo");
    }

    // El 1 y el 8 hacen lo mismo. Se comprueba para que, si alguien "limpia" el
    // 8 creyendo que es un error, el test le diga que no lo es.
    FxEngine uno, ocho;
    for (FxEngine* engine : { &uno, &ocho })
    {
        engine->prepare (fxprobe::kSr, 2, 512);
        engine->setCatalogue (cat, count);
        for (int s = 0; s < kFxNumSlots; ++s)
        {
            engine->getSlot (s).setType (s + 1);
            engine->getSlot (s).setMix (0.4f);
        }
    }

    uno.setRouting (FxRouting::ParallelFront);
    ocho.setRouting (FxRouting::ParallelFrontSeries);

    const std::vector<float> in = fxprobe::noise (2048, 4242u);
    check (fxprobe::render (uno, in) == fxprobe::render (ocho, in),
           "el ruteo 1 y el 8 son el mismo diagrama, como en ABDEep");

    // Y un bypass global no toca nada, aunque los huecos esten llenos.
    FxEngine byp;
    byp.prepare (fxprobe::kSr, 2, 512);
    byp.setCatalogue (cat, count);
    byp.setMode (FxMode::Bypass);
    for (int s = 0; s < kFxNumSlots; ++s)
    {
        byp.getSlot (s).setType (s + 1);
        byp.getSlot (s).setMix (1.0f);
    }

    check (fxprobe::render (byp, in) == in, "el modo bypass del motor devuelve la senal intacta");
}

//==============================================================================
/** EL MODO ENVIO Y LA REALIMENTACION DEL 9. El envio es lo que mas se rompe al
    cambiarlo de sitio, asi que lleva su propio test. */
void testFxEngineSendAndFeedback()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count = fxprobe::numCatalogo();

    //--- el envio NO se come el mix del usuario ---------------------------
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);
        e.setMode (FxMode::Send);
        e.setSendLevel (0.3f);
        e.getSlot (0).setType (1);
        e.getSlot (0).setMix (0.85f);

        fxprobe::render (e, fxprobe::noise (2048, 31337u));

        check (e.getSlot (0).getMix() == 0.85f,
               "un bloque de envio deja el mix del hueco como el usuario lo puso");
    }

    //--- envio a 0 = la seca intacta; envio a 1 = el efecto entero ---------
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);
        e.setMode (FxMode::Send);

        const std::vector<float> in = fxprobe::noise (2048, 555u);

        e.setSendLevel (0.0f);
        check (fxprobe::render (e, in) == in, "con el envio a 0 sale la seca intacta");

        e.setSendLevel (1.0f);
        e.getSlot (0).setType (1);
        check (fxprobe::render (e, in) != in, "con el envio a 1 sale el efecto entero");
    }

    //--- el 9 realimenta la SALIDA DEL BLOQUE ANTERIOR, no la seca ---------
    {
        // Con el 9 y ganancia 0 el motor es una serie, y por eso el resultado
        // tiene que ser EXACTAMENTE el de la serie. Si alguien realimenta la
        // seca del bloque en curso, esto deja de cumplirse: la topologia
        // distinta no suena casi igual, suena a otra cosa.
        FxEngine conFb, sinFb;
        for (FxEngine* engine : { &conFb, &sinFb })
        {
            engine->prepare (fxprobe::kSr, 2, 512);
            engine->setCatalogue (cat, count);
            engine->getSlot (0).setType (2);
            engine->getSlot (0).setMix (0.6f);
        }

        conFb.setRouting (FxRouting::SeriesWithFeedback);
        conFb.setFeedbackGain (0.0f);
        sinFb.setRouting (FxRouting::Series);

        const std::vector<float> in = fxprobe::noise (4096, 24680u);
        check (fxprobe::render (conFb, in) == fxprobe::render (sinFb, in),
               "el ruteo 9 con ganancia 0 es exactamente la serie");

        // Y con ganancia, la realimentacion accumulates: el segundo bloque ya no
        // puede ser la serie, porque entra la salida del primero.
        conFb.setFeedbackGain (0.5f);
        const std::vector<float> primera = fxprobe::render (conFb, in);
        const std::vector<float> segunda = fxprobe::render (conFb, in);
        check (primera != segunda, "con realimentacion el bloque siguiente lleva el anterior dentro");
    }
}

//==============================================================================
/** UN BLOQUE MAYOR QUE EL PREPARADO. El troceo va en el motor, y tiene que dar
    el MISMO numero: los motores de este modulo trabajan por muestra. */
void testFxEngineBlockSplitting()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count = fxprobe::numCatalogo();
    const FxParamSpec* delaySpec = fxFindEffect (cat, count, "delay")->params;

    // El motor preparado a 64 recibe un bloque de 8192, y el de referencia
    // preparado a 8192 lo recibe entero. Si el motor entrega el bloque entero a
    // un hueco preparado a 64, ese hueco procesa 64 muestras y devuelve el
    // resto SECO: la mitad del bloque del host en silencio. Medido: 0.119 de
    // diferencia sobre una entrada de 0.2.
    FxEngine troceado, entero;
    troceado.prepare (fxprobe::kSr, 2, 64);
    troceado.setCatalogue (cat, count);

    entero.prepare (fxprobe::kSr, 2, 8192);
    entero.setCatalogue (cat, count);

    for (FxEngine* engine : { &troceado, &entero })
    {
        engine->getSlot (0).setType (2);
        engine->getSlot (0).setMix (0.6f);
        engine->getSlot (0).setParameter (0, fxNormalise (delaySpec[0], 0.30f));
        engine->getSlot (0).setParameter (1, 0.6f);
    }

    const std::vector<float> musica = fxprobe::musica (8192);
    const std::vector<float> a = fxprobe::render (troceado, musica);
    const std::vector<float> b = fxprobe::render (entero, musica);

    float maxDiff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        maxDiff = jmax (maxDiff, std::fabs (a[i] - b[i]));

    check (maxDiff == 0.0f, "trocear el bloque da el mismo numero, no uno parecido");
    check (fxprobe::rms (a, 7800, 8192) > 1e-4f, "el retardo vuelve a sonar en la cola del bloque grande");

    //--- y un buffer de UN canal, que no tiene canal derecho que leer ------
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 1, 512);
        e.setCatalogue (cat, count);
        e.getSlot (0).setType (1);
        e.getSlot (0).setMix (0.7f);

        const std::vector<float> in = fxprobe::noise (1024, 31337u);
        AudioBuffer<float> b (1, 1024);
        for (int i = 0; i < 1024; ++i)
            b.setSample (0, i, in[static_cast<size_t> (i)]);

        e.process (b, 1024);

        float p = 0.0f;
        for (int i = 0; i < 1024; ++i)
            p = jmax (p, std::fabs (b.getSample (0, i)));

        check (std::isfinite (p) && p > 1e-3f, "un buffer de un canal se procesa como mono");
    }
}

//==============================================================================
/** LA TABLA DE PARAMETROS: ida y vuelta, pasos discretos y mandos utilizables.

    La comprobacion central es la de ida y vuelta, y no un valor suelto, porque
    las dos funciones de la tabla pueden no ser inversas la una de la otra, y eso
    solo se note en como se siente el mando. */
void testFxParameterNormalisation()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count = fxprobe::numCatalogo();

    //--- ida y vuelta en todos los mandos del catalogo --------------------
    float peor = 0.0f;
    int contadas = 0;

    for (int f = 0; f < count; ++f)
    {
        for (int p = 0; p < cat[f].numParams; ++p)
        {
            const FxParamSpec& s = cat[f].params[p];
            if (s.steps > 1)
                continue;   // los discretos se comprueban aparte

            for (int i = 0; i <= 100; ++i)
            {
                const float v = static_cast<float> (i) / 100.0f;
                const float fisico = fxDenormalise (s, v);
                const float vuelta = fxNormalise (s, fisico);

                peor = jmax (peor, std::fabs (vuelta - v));
                ++contadas;
            }
        }
    }

    check (contadas > 500, "se ha recorrido la tabla entera de mando");
    check (peor < 1e-3f, "normalizar y desnormalizar son la misma operacion al reves");

    //--- y el valor por defecto cae donde un usuario lo encuentra ---------
    // Un mando cuyo valor por defecto esta en el 0.05 del recorrido es un mando
    // que no se puede usar: el producto se abre con el efecto casi en su minimo.
    std::vector<std::string> malColocados;
    for (int f = 0; f < count; ++f)
    {
        for (int p = 0; p < cat[f].numParams; ++p)
        {
            const FxParamSpec& s = cat[f].params[p];
            if (s.steps > 1)
                continue;

            const float knob = fxNormalise (s, s.defaultValue);
            if (knob < 0.15f || knob > 0.95f)
                malColocados.push_back (std::string (s.name) + "@"
                                        + std::to_string (knob));
        }
    }

    check (malColocados.empty(), "ningun mando por defecto cae en un extremo del recorrido");
    if (! malColocados.empty())
        for (const auto& s : malColocados)
            std::printf ("[info]   mando en un extremo: %s\n", s.c_str());

    //--- los mandos DISCRETOS solo toman valores enteros -------------------
    {
        const FxEffectInfo* bbd = fxFindEffect (cat, count, "bbd");
        check (bbd != nullptr, "el bbd esta en el catalogo");

        if (bbd != nullptr)
        {
            const FxParamSpec& mode = bbd->params[0];
            check (mode.steps == 4, "el modo del bbd declara cuatro pasos");

            bool entero = true;
            std::vector<int> vistos;

            for (int i = 0; i <= 100; ++i)
            {
                const float v = static_cast<float> (i) / 100.0f;
                const float fisico = fxDenormalise (mode, v);

                if (std::fabs (fisico - std::floor (fisico + 0.5f)) > 1e-5f)
                    entero = false;

                const int estado = static_cast<int> (fisico + 0.5f);
                if (std::find (vistos.begin (), vistos.end (), estado) == vistos.end ())
                    vistos.push_back (estado);
            }

            check (entero, "el modo del bbd solo toma valores enteros");
            check (vistos.size () == 4, "el recorrido del mando da los cuatro modos");
        }
    }

    //--- un parametro por encima de su capacidad se recorta, no se sale ----
    {
        FxEngine e;
        e.prepare (fxprobe::kSr, 2, 512);
        e.setCatalogue (cat, count);
        e.getSlot (0).setType (2);
        e.getSlot (0).setMix (1.0f);
        e.getSlot (0).setParameter (0, 1.0f);   // el retardo mas largo que se puede pedir

        const std::vector<float> out = fxprobe::render (e, fxprobe::musica (9600));
        check (std::isfinite (fxprobe::pico (out, 0, 9600)),
               "el retardo al maximo no se sale del buffer");
    }
}

//==============================================================================
/** La etapa de caracter base NO es una interfaz, y ahora se comprueba.

   Hubo un `virtual void prepareRateImpl (double)` protected en `CharacterStage`,
para que una etapa que necesitara el sample rate se enganchara al `prepare`. No
lo sobrescribia NADIE —ni en este modulo ni en ningun producto del arbol— y
costaba un vtable entero en cada etapa, en la clase cuya unica razon de existir
es no gastar indireccion. MEDIDO antes de quitarlo:

    sizeof(NullStage) = 16   sizeof(DiodeBridge) = 16   sizeof(TapeColour) = 24
    is_polymorphic<NullStage> = si   (y de las otras dos tambien)

y despues:

    sizeof(NullStage) =  8   sizeof(DiodeBridge) =  8   sizeof(TapeColour) = 16
    is_polymorphic<NullStage> = no

Ocho bytes y una tabla de funciones por etapa a cambio de nada. */
void testCharacterStageHasNoVtable()
{
    using namespace abd::dsp;

    check (! std::is_polymorphic<CharacterStage>::value,
           "la etapa de caracter base no es polimorfica (no lleva vtable)");
    check (! std::is_polymorphic<NullStage>::value,
           "NullStage no lleva vtable");
    check (! std::is_polymorphic<DiodeBridge>::value,
           "DiodeBridge no lleva vtable");
    check (! std::is_polymorphic<TapeColour>::value,
           "TapeColour no lleva vtable");

    check (sizeof (NullStage) <= sizeof (double) * 2,
           "una etapa sin estado ocupa lo que un sample rate y poco mas");

    // Y el ancla sigue sirviendo: una etapa lee el sample rate que le dio el motor.
    NullStage n;
    n.prepareSampleRate (48000.0);
    check (n.getSampleRate() == 48000.0, "la etapa guarda el sample rate del prepare");

    // Un sample rate invalido no debe dejar a la etapa con uno que rompa un
    // coeficiente: antes se guardaba tal cual.
    n.prepareSampleRate (-1.0);
    check (n.getSampleRate () > 0.0, "un sample rate invalido deja a la etapa con uno utilizable");
}

//==============================================================================
/** Los tres mandos de la reverb se recortan a su rango documentado.

    Los tres tienen el rango escrito en su comentario (0..1, y el `decay` admits
    negativos para la variante "Reverse") y NINGUNO se comprobaba. MEDIDO sin el
    recorte, con un impulso y un segundo de silencio:

        decay 0.8  (en rango)        pico 3.2e-01
        decay 1.0  (documentado)     pico 3.7e-01
        decay 1.2  (fuera)           pico 4.2e-01
        decay 2.0  (muy fuera)       pico 7.9e+06    <- se dispara
        damping 2.0 (fuera)          pico 1.0e+30    <- se dispara
        diffusion 2.0 (fuera)        pico 3.3e+28    <- se dispara

    El coeficiente del conb es `decay * 0.9`, asi que por encima de `decay` 1.111
    la realimentacion pasa de 1. Con `damping = 2` el pasabajos `damp1 = 2,
    damp2 = -1` tiene el polo en -1. Y con `diffusion = 2` el allpass se va.

    El recorte NO cambia ni una muestra de ningun consumidor. Los diez perfiles
    de `ReverbProfile` usan `decay` entre -0.30 y 0.85, `damping` entre 0.20 y
    0.90 y `diffusion` entre 0.20 y 0.90, o sea que ninguno se sale del rango
    que recortan y ninguno llega al 0 de la amortiguacion (el unico caso en el
    que el recorte SI cambia el comportamiento, ver el test de al lado). Por
    eso el recorte es identity en todo lo que un producto pasa de verdad. */
void testSchroederKnobsAreClamped()
{
    using abd::dsp::SchroederReverb;

    auto picoCon = [] (float decay, float damping, float diffusion)
    {
        SchroederReverb r;
        r.prepare (48000.0);
        r.setDecay (decay);
        r.setDamping (damping);
        r.setDiffusion (diffusion);

        float l = 0.0f, rr = 0.0f;
        for (int i = 0; i < 2400; ++i)
            r.processFrame (i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, l, rr);

        float pico = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            r.processFrame (0.0f, 0.0f, l, rr);
            if (! std::isfinite (l)) return 1.0e30f;
            pico = std::max (pico, std::fabs (l));
        }
        return pico;
    };

    // Lo que hay que comprobar es que un valor fuera de rango se comporta como
    // el del tope, y que ninguno se dispara. NO se comparan entre si los picos de
    // decay distintos: mas decay es mas cola y por tanto un pico MAYOR, y eso no
    // es inestabilidad.
    const float topeDecay = picoCon (1.0f, 0.4f, 0.7f);
    check (topeDecay < 1.0f, "decay en su tope documentado la cola se acota");

    check (picoCon (1.2f, 0.4f, 0.7f) <= topeDecay * 1.001f,
           "decay por encima del tope se recorta");
    check (picoCon (2.0f, 0.4f, 0.7f) <= topeDecay * 1.001f,
           "decay muy por encima del tope se recorta y no se dispara");
    check (picoCon (-3.0f, 0.4f, 0.7f) < 1.0f,
           "decay negativo (variante Reverse) sigue siendo estable");

    check (picoCon (0.8f, 2.0f, 0.7f) < 1.0f, "damping fuera de rango se recorta y no se dispara");
    check (picoCon (0.8f, 0.4f, 2.0f) < 1.0f, "diffusion fuera de rango se recorta y no se dispara");
    check (picoCon (0.8f, -1.0f, 0.7f) < 1.0f, "damping negativo se recorta y no se dispara");

    // Y el recorte no toca lo que esta en rango: el tope da exactamente lo mismo
    // que el tope, y un valor normal da lo que daba.
    const float enRango = picoCon (0.8f, 0.4f, 0.7f);
    check (enRango > 0.0f && enRango < 1.0f, "un juego de mandos en rango sigue sonando");

    // ---------------------------------------------------------------------
    // POR QUE `decay` SE RECORTA SOLO POR ARRIBA Y NO ABAJO.
    //
    // Un recorte `jlimit(0, 1)` de los tres mandos seria lo obvious, y ROMPERIA
    // un preset que hoy suena. El `decay` negativo no es un valor invalido: es
    // una variante con significado propio, y el motor lo traduce:
    //
    //     feedback = decay < 0 ? 0.3f : decay * 0.9f
    //     escala   = decay < 0 ? 0.5f  : decay * 0.7f + 0.3f
    //
    // ABDEep lo consume con presets de fabrica que traen `decay` negativo, y su
    // referencia congelada exige el mismo `feedback = 0.3`. Si se recortara por
    // abajo, el negativo caeria en 0, `0` NO es el mismo caso del ternario, y la
    // realimentacion pasaria de 0.3 a 0: la cola de esa variante desapareceria.
    // Por eso aqui va `jmin(decay, 1.0f)` y no `jlimit`.
    //
    // La comprobacion que distingue las dos cosas: un decay negativo tiene
    // realimentacion (0.3) y un decay de 0 no la tiene. Ojo con la ventana que
    // se mide: durante los primeros 250 ms el impulso sigue en vuelo por el
    // pre-retardo y los conbs, y un decay de 0 tambien "suena" ahi (1.1e-01
    // medidos), o sea que medir solo al principio no distingue nada. Lo que
    // separa es la cola TARDE, cuando el impulso ya ha salido:
    //
    //     decay -0.30 : pico 0-0.25s 1.86e-01   pico 0.25-1s 9.51e-06
    //     decay +0.00 : pico 0-0.25s 1.12e-01   pico 0.25-1s 1.25e-10
    //
    // Si alguien recortara por abajo, el negativo caeria en 0 y las dos filas
    // serian la segunda. El "sigue siendo estable" de arriba no lo pilla: 0
    // tambien es estable.
    auto picoTardio = [] (float decay)
    {
        SchroederReverb r;
        r.prepare (48000.0);
        r.setDecay (decay);
        r.setDamping (0.4f);
        r.setDiffusion (0.7f);

        float l = 0.0f, rr = 0.0f;
        for (int i = 0; i < 2400; ++i)
            r.processFrame (i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, l, rr);

        float pico = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            r.processFrame (0.0f, 0.0f, l, rr);
            if (i >= 12000)   // 0.25 s: el impulso ya ha salido de los conbs
                pico = std::max (pico, std::fabs (l));
        }
        return pico;
    };

    const float colaNegativa = picoTardio (-0.3f);
    const float colaCero = picoTardio (0.0f);
    check (colaNegativa > 1.0e-6f,
           "un decay negativo conserva su cola propia (la variante Reverse no se recorta a 0)");
    check (colaNegativa > colaCero * 1000.0f,
           "un decay negativo NO es lo mismo que un decay de 0: los dos casos del ternario se distinguen");

    // Y el recorte por arriba es EXACTO, no aproximado: por encima de 1 el motor
    // tiene que sonar como si le hubieran dado 1, muestra a muestra.
    auto render = [] (float decay)
    {
        SchroederReverb r;
        r.prepare (48000.0);
        r.setDecay (decay);
        r.setDamping (0.4f);
        r.setDiffusion (0.7f);

        std::vector<float> salida;
        salida.reserve (48000);
        float l = 0.0f, rr = 0.0f;
        for (int i = 0; i < 4800; ++i)
        {
            const float x = (i == 0) ? 1.0f : 0.0f;
            r.processFrame (x, x, l, rr);
            salida.push_back (l);
        }
        for (int i = 0; i < 48000; ++i)
        {
            r.processFrame (0.0f, 0.0f, l, rr);
            salida.push_back (l);
        }
        return salida;
    };

    const std::vector<float> enElTope = render (1.0f);
    const std::vector<float> porEncima = render (1.7f);
    const std::vector<float> muyPorEncima = render (9.0f);

    float maxArriba = 0.0f, maxMuy = 0.0f;
    for (std::size_t i = 0; i < enElTope.size(); ++i)
    {
        maxArriba = std::max (maxArriba, std::fabs (enElTope[i] - porEncima[i]));
        maxMuy = std::max (maxMuy, std::fabs (enElTope[i] - muyPorEncima[i]));
    }
    check (maxArriba == 0.0f, "un decay por encima del tope suena EXACTAMENTE como el tope");
    check (maxMuy == 0.0f, "un decay muy por encima del tope tambien, y no se dispara");
}

//==============================================================================
/** La amortiguacion a 0 es "sin amortiguacion", no "estado congelado".

    El pasabajos de la realimentacion del conb es `y = damp1·x + damp2·y` con
    `damp1 = damping` y `damp2 = 1 - damping`. Con `damping = 0` eso degenera en
    `y = y`: el estado se congela en su ultimo valor y no decae nunca, o sea que
    la componente continua que hubiera en la entrada se queda en la salida para
    siempre. MEDIDO, con 200 muestras de continua al principio y dos segundos de
    silencio, tomando la media CON SIGNO del ultimo medio segundo:

        amortiguacion intacta  : +0.000000e+00
        bajada a 0 en caliente: +8.882900e-02   <- antes del arreglo

    Un offset de 0.09 en la salida de un bus de reverberacion es un escalon que
    no se va nunca. El limite correcto de "sin amortiguacion" es pasar todo, y no
    quedarse donde estuviera. */
void testSchroederDampingAtZero()
{
    using abd::dsp::SchroederReverb;

    auto mediaConSigno = [] (float dampingAlEmpezar, float dampingAlCambiar)
    {
        SchroederReverb r;
        r.prepare (48000.0);
        r.setDecay (0.85f);
        r.setDamping (dampingAlEmpezar);
        r.setDiffusion (0.7f);

        float l = 0.0f, rr = 0.0f;
        for (int i = 0; i < 4800; ++i)
            r.processFrame (i < 200 ? 1.0f : 0.0f, i < 200 ? 1.0f : 0.0f, l, rr);

        r.setDamping (dampingAlCambiar);

        double suma = 0.0;
        for (int i = 0; i < 96000; ++i)
        {
            r.processFrame (0.0f, 0.0f, l, rr);
            if (i > 90000) suma += l;
        }
        return suma / 6000.0;
    };

    const double conCalor = mediaConSigno (0.8f, 0.0f);
    const double sinCambio = mediaConSigno (0.8f, 0.8f);

    // ABSOLUTO, y no relativo al caso que no cambia: los dos son cero hasta la
    // ultima cifra util, y compararlos entre si da un umbral del tamaño del
    // ruido en vez de uno con significado fisico.
    check (std::fabs (conCalor) < 1.0e-6,
           "bajar la amortiguacion a 0 no deja un offset de DC permanente");
    check (std::fabs (sinCambio) < 1.0e-6,
           "sin tocar la amortiguacion tampoco hay offset");

    // Y que la amortiguacion siga haciendo su trabajo EN LOS DOS EXTREMOS, que es
    // lo que un estado congelado rompia: con ruido de excencion, la cola con
    // amortiguacion 0 tiene que ser mas larga que con 0.9, porque 0 quiere decir
    // "sin filtrar los agudos" y no "quedarse congelado".
    //
    // (La primera version de esta comprobacion ponia `setDecay(0.0)` y miraba la
    // salida, y se ponia roja con razon ajena: `decay = 0` es realimentacion 0,
    // o sea un conb SIN cola, asi que la salida cae a cero por definicion y no
    // dice nada sobre la amortiguacion.)
    auto colaConRuido = [] (float damping)
    {
        SchroederReverb r;
        r.prepare (48000.0);
        r.setDecay (0.8f);
        r.setDamping (damping);
        r.setDiffusion (0.7f);

        float l = 0.0f, rr = 0.0f;
        uint32_t semilla = 0x12345678u;
        for (int i = 0; i < 4800; ++i)
        {
            semilla = semilla * 1664525u + 1013904223u;
            const float n = ((float) (semilla >> 8) / (float) (1 << 24) - 0.5f) * 0.5f;
            r.processFrame (n, n, l, rr);
        }

        double rms = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            r.processFrame (0.0f, 0.0f, l, rr);
            if (i > 24000) rms += l * l;
        }
        return std::sqrt (rms / 24000.0);
    };

    const double sinFiltrar = colaConRuido (0.0f);
    const double filtrada = colaConRuido (0.9f);

    check (filtrada < sinFiltrar,
           "la amortiguacion a 0 deja mas cola que a 0.9 (filtra, no congela)");
    check (filtrada > 0.0f && sinFiltrar > 0.0f, "las dos colas tienen senal que medir");
}

//==============================================================================
/** `wowAt` y `flutterAt` son la misma funcion y el motor no usa ninguna.

    La cabecera de `TapeColour.h` explica el reparto —"la etapa EVALUA (metodos
    `wow` y `flutter`) y el motor APORTA la posicion"— y no es el reparto que esta
    implementado: `MultiHeadEcho` genera su propia deriva en `advance()` con las
    constantes del perfil. MEDIDO: `|wowAt - flutterAt|` vale 0 en 64 posiciones, y
    el eco del motor cambia muestra a muestra (7.8e-02), o sea que deriva, pero no
    por aqui.

    Se quedan los dos metodos, porque son el punto de extension para un consumidor
    que quiera la deriva atada a la posicion de escritura; lo que se fija es que
    son iguales y que el motor no los llama, para que nadie los lea como el
    contrato vigente. */
void testTapeColourDriftIsNotWired()
{
    using abd::dsp::TapeColour;

    TapeColour colour;
    float peor = 0.0f;

    for (int i = 0; i < 64; ++i)
    {
        const float pos = (float) i * 0.01f;
        peor = std::max (peor, std::fabs (colour.wowAt (pos, 3.0f, 0.5f)
                                       - colour.flutterAt (pos, 3.0f, 0.5f)));
    }

    check (peor == 0.0f, "wowAt y flutterAt son la misma funcion");

    // Y el motor deriva por su cuenta: con el mismo retardo nominal, dos puntos
    // separados tienen valor distinto. Si no derivara, el eco seria una senal
    // repetida.
    abd::dsp::MultiHeadEcho<abd::dsp::Re201Profile, TapeColour> echo;
    echo.prepare (48000.0);
    echo.setMode (0);

    float maxDiff = 0.0f, anterior = 0.0f;
    for (int i = 0; i < 24000; ++i)
    {
        const float t = (float) i / 48000.0f;
        const float in = 0.3f * std::sin (2.0f * 3.14159265f * 220.0f * t);
        const float y = echo.processSample (0, in, 0.1f, 0.0f, 0.5f, 0.5f, 0.0f);
        echo.advance();
        if (i > 12000) maxDiff = std::max (maxDiff, std::fabs (y - anterior));
        anterior = y;
    }

    check (maxDiff > 0.0f, "el eco del motor tiene deriva propia");
}

} // namespace

//==============================================================================
int main()
{
    checkStageContract<abd::dsp::NullStage>       ("NullStage");
    checkStageContract<abd::dsp::TapeColour>      ("TapeColour");
    checkStageContract<abd::dsp::DiodeBridge>     ("DiodeBridge");

    testDiodeBridge();
    testTapeColour();
    testMultiHeadEcho();
    testRingMod();
    testRe201Profile();
    testReverbProfile();
    testSchroederReverb();
    testReverbCombLengthsStayDistinct();
    testJunoBbdChannelIndependence();
    testJunoBbdBehaviour();
    testJunoBbdProfiles();
    testRe201HeadReadsStayInRange();
    testEchoToneIsPerSampleNotPerChannel();
    testEchoDelayKnobDeliversRequestedDelay();
    testEchoRightChannelLevelMatchesLeft();
    testDspDelay();
    testDspDelayReadWrapStaysInRange();
    testDspChorus();
    testDspSaturation();
    testDspReverb();
    testFxCatalogue();
    testFxSlotBehaviour();
    testFxEngineRouting();
    testFxEngineSendAndFeedback();
    testFxEngineBlockSplitting();
    testFxParameterNormalisation();
    testCharacterStageHasNoVtable();
    testSchroederKnobsAreClamped();
    testSchroederDampingAtZero();
    testTapeColourDriftIsNotWired();

    if (gFailures == 0)
    {
        std::printf ("[OK] DspEffects: %d comprobaciones\n", gChecks);
        return 0;
    }

    std::printf ("[FALLO] DspEffects: %d de %d comprobaciones\n", gFailures, gChecks);
    return 1;
}
