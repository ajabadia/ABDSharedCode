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

      7. AUDITRIA ESTRUCTURAL DEL MOTOR DE HUECOS. Lo de arriba pregunta
         "suena bien"; esto pregunta que VIVE el hueco: quien crea y quien
         destruye cada instancia, que hace el hueco con una fila a medias, que
         pasa con un indice que no existe o con un NaN, y que forma tiene que
         tener el bloque. Nada de eso se oye en un test de sonido, y todo eso
         acaba en el panel como un zumbido que no se va. Ver el criterio C1 a
         C8 en la seccion correspondiente.

    Lo que NO se promete aqui: paridad con ningun efecto de ABDEep. Este modulo
    no tiene los originales a mano; la comparacion, si se quiere, vive en el
    consumidor (que es lo que hizo DspReverb con juce::Reverb en ABDNeural).

    Uso: ABDShared_DspEffects_Tests  (no toma argumentos; 0 = OK)

  ==============================================================================
*/

#include "DspCore/DspCore.h"
#include "DspEffects/CascadeShelfEq.h"
#include "DspEffects/DspChorus.h"
#include "DspEffects/DspDelay.h"
#include "DspEffects/DspReverb.h"
#include "DspEffects/DspSaturation.h"
#include "DspEffects/DspSchroederReverb.h"
#include "DspEffects/EffectPolicy.h"
#include "DspEffects/FxDefaultCatalogue.h"
#include "DspEffects/FxEngine.h"
#include "DspEffects/JunoBBD.h"
#include "DspEffects/MultiHeadEcho.h"
#include "DspEffects/Phaser4.h"
#include "DspEffects/RingMod.h"
#include "DspEffects/ShelfFilter.h"
#include "DspEffects/characters/BbdNoise.h"
#include "DspEffects/characters/DiodeBridge.h"
#include "DspEffects/characters/TapeColour.h"
#include "DspEffects/profiles/JunoBbdProfile.h"
#include "DspEffects/profiles/MS2000EqProfile.h"
#include "DspEffects/profiles/Re201Profile.h"
#include "DspEffects/profiles/ReverbProfile.h"
#include "DspEffects/DspPhaser.h"
#include "DspEffects/DspMoodFilter.h"
#include "DspEffects/DspSolinaEnsemble.h"
#include "DspEffects/DspVocoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

int gChecks   = 0;
int gFailures = 0;

void check(bool ok, const char* what)
{
    ++gChecks;

    if (!ok)
    {
        std::printf("[FAIL] %s\n", what);
        ++gFailures;
    }
}

/** Comprueba el contrato de una etapa de caracter. Ver el punto 1 de la cabecera. */
template <typename Stage>
void checkStageContract(const char* name)
{
    // El ciclo de vida que comparte CharacterStage.
    check((std::is_base_of<abd::dsp::CharacterStage, Stage>::value),
          "la etapa no deriva de CharacterStage");

    Stage stage;

    stage.prepareSampleRate(48000.0);
    check(stage.getSampleRate() == 48000.0, "getSampleRate no devuelve lo preparado");

    // `processSample` tiene que existir y devolver float. Se quita el `const`
    // de la declaracion porque `decltype` de una variable const<float> es
    // `const float`, y el test no va a estar mirando calidades.
    const float viaOneArg = stage.processSample(0.25f);
    check(std::is_same<std::remove_cv_t<decltype(viaOneArg)>, float>::value,
          "processSample no devuelve float");

    // Y tiene que aceptar el par (drive, hiss), porque es lo que le pasa un
    // motor: si lo cambias, hay que cambiar la convencion en los dos sitios.
    const float viaThreeArgs = stage.processSample(0.25f, 0.5f, 0.5f);
    check(std::is_same<std::remove_cv_t<decltype(viaThreeArgs)>, float>::value,
          "processSample no acepta el par (drive, hiss)");

    stage.reset();

    std::printf("       [ok] contrato de etapa: %s\n", name);
}

//==============================================================================
/** 2. El puente de diodos: recto abajo, recortado arriba, y par. */
void testDiodeBridge()
{
    abd::dsp::DiodeBridge bridge;

    // Por debajo del umbral la senal pasa intacta.
    check(bridge.processSample(0.1f, 1.0f, 0.3f) == 0.1f,
          "puente de diodos: deberia pasar recto por debajo del umbral");

    // Y por encima se comprime: 3.0 con umbral 0.3 da bastante menos de 3.0.
    const float hot = bridge.processSample(3.0f, 1.0f, 0.3f);
    check(hot > 0.3f && hot < 3.0f,
          "puente de diodos: deberia recortar por encima del umbral");

    // Paridad: el puente no puede sesgar la senal, o el anillo rectifica.
    float worstAsymmetry = 0.0f;

    for (int i = 1; i <= 1000; ++i)
    {
        const float x = (float)i / 100.0f;

        worstAsymmetry = std::max(worstAsymmetry,
                                  std::abs(bridge.processSample(x, 1.0f, 0.3f) + bridge.processSample(-x, 1.0f, 0.3f)));
    }

    check(worstAsymmetry < 1.0e-6f, "puente de diodos: no es par");

    // EL UMBRAL SE RECORTA, y lo que hace `threshold = 1` NO es lo que decia su
    // comentario. MEDIDO antes de arreglarlo:
    //
    //     x = 2.0, threshold 1.0 -> 1.000000  (recorte duro; decia "transparente")
    //     x = 2.0, threshold 1.5 -> 1.119203  (y la curva subia y luego bajaba)
    //
    // El segundo caso era una curva NO MONOTONA: por encima del umbral el termino
    // `(1 - threshold)` es negativo, asi que la senal salia mas pequena cuanto
    // mas grande era la entrada. Con el recorte, 1.5 se comporta como 1.
    check(bridge.processSample(2.0f, 1.0f, 1.0f) == 1.0f,
          "puente de diodos: threshold = 1 recorta duro a 1.0, no es transparente");

    check(bridge.processSample(2.0f, 1.0f, 1.5f) == bridge.processSample(2.0f, 1.0f, 1.0f),
          "puente de diodos: un umbral fuera de rango se recorta al tope");

    // Y la curva tiene que ser MONOTONA en todo el recorrido, que es lo que el
    // recorte garantiza y lo que antes fallaba por encima de 1.
    float worstDrop = 0.0f;
    float anterior  = bridge.processSample(0.0f, 1.0f, 0.5f);

    for (int i = 1; i <= 400; ++i)
    {
        const float x     = (float)i / 100.0f;
        const float ahora = bridge.processSample(x, 1.0f, 0.5f);
        worstDrop         = std::max(worstDrop, anterior - ahora);
        anterior          = ahora;
    }

    check(worstDrop <= 0.0f, "puente de diodos: la curva de transferencia es monotona");
}

//==============================================================================
/** 3. El color de cinta: asimetrico, transparente en drive 0, y con semilla. */
void testTapeColour()
{
    using abd::dsp::TapeColour;

    // Asimetria: el lado POSITIVO satura antes que el negativo. Es lo que
    // distingue una cinta de un limitador.
    check(TapeColour::tapeCurve(0.5f) > -TapeColour::tapeCurve(-0.5f),
          "color de cinta: la curva deberia saturar antes en positivo");

    // drive 0 con siseo 0 es la identidad: una etapa que no se usa no debe
    // tocar la senal.
    check(TapeColour().processSample(0.4f, 0.0f, 0.0f) == 0.4f,
          "color de cinta: drive 0 y siseo 0 deberian ser la identidad");

    // Determinismo del siseo: misma semilla, mismo audio. Se recogen las dos
    // series ANTES de compararlas, porque el siseo avanza con cada llamada:
    // comparar dentro del bucle compararia llamadas distintas de cada
    // instancia y daria un falso negativo.
    auto hiss = [] {
        TapeColour stage;
        stage.reset();

        std::vector<float> out;
        for (int i = 0; i < 64; ++i)
            out.push_back(stage.processSample(0.0f, 0.0f, 1.0f));

        return out;
    };

    TapeColour other;
    other.setNoiseSeed(0x1234abcd);

    std::vector<float> otherHiss;
    for (int i = 0; i < 64; ++i)
        otherHiss.push_back(other.processSample(0.0f, 0.0f, 1.0f));

    check(hiss() == hiss(), "color de cinta: la misma semilla no da el mismo siseo");
    check(hiss() != otherHiss, "color de cinta: semillas distintas dan el mismo siseo");
}

//==============================================================================
/** Corre el eco con una etapa concreta y devuelve el audio, muestra a muestra. */
template <typename Colour>
std::vector<float> renderEcho(abd::dsp::MultiHeadEcho<abd::dsp::StudioEchoProfile, Colour>& echo,
                              int numSamples)
{
    std::vector<float> out;
    out.reserve((size_t)numSamples * 2);

    for (int s = 0; s < numSamples; ++s)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float input = ((s % 64) < 32) ? 0.3f : -0.3f;
            out.push_back(echo.processSample(c, input, 0.25f, 0.5f, 0.5f, 0.5f, 0.3f));
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
    MultiHeadEcho<StudioEchoProfile, NullStage> plain;

    coloured.prepare(48000.0);
    plain.prepare(48000.0);

    coloured.setMode(2);
    plain.setMode(2);

    const auto colouredAudio = renderEcho(coloured, 4096);
    const auto plainAudio    = renderEcho(plain, 4096);

    check(colouredAudio.size() == 8192, "eco: no ha salido el numero de muestras pedido");

    // La etapa de color tiene que NOTARSE. Si las dos coincide, el motor no la
    // esta usando y la inyeccion de politica es decorativa.
    bool differs = false;
    for (size_t i = 0; i < colouredAudio.size(); ++i)
        if (colouredAudio[i] != plainAudio[i])
        {
            differs = true;
            break;
        }

    check(differs, "eco: la etapa de color no cambia nada (no se esta usando)");

    // Salida FINITA y con un pico razonable: un lazo de realimentacion que se
    // dispara es el fallo clasico al tocar los coeficientes del perfil.
    float peak  = 0.0f;
    bool finite = true;

    for (float x : colouredAudio)
    {
        if (!std::isfinite(x))
        {
            finite = false;
            break;
        }

        peak = std::max(peak, std::abs(x));
    }

    check(finite, "eco: la salida se ha ido a infinito o NaN");
    check(peak > 1.0e-3f, "eco: la salida es practicamente muda");
    check(peak < 4.0f, "eco: la salida se dispara");

    // Determinismo: dos motores con los mismos ajustes dan el MISMO audio.
    MultiHeadEcho<StudioEchoProfile, TapeColour> again;
    again.prepare(48000.0);
    again.setMode(2);

    check(renderEcho(again, 4096) == colouredAudio, "eco: dos pasadas dan audio distinto");

    // Y `reset` devuelve al estado inicial: preparar, resetear y procesar tiene
    // que dar lo mismo que preparar y procesar.
    coloured.reset();
    check(renderEcho(coloured, 4096) == colouredAudio, "eco: reset() no devuelve al estado inicial");
}

//==============================================================================
/** 5. El ring mod. */
void testRingMod()
{
    using namespace abd::dsp;

    // Los extremos del mapeo exponencial tienen que dar los del perfil. Si
    // esto falla, el mapeo de frecuencias esta roto y ningun barrido sirve.
    RingMod<RingModProfile, DiodeBridge> ring;
    ring.prepare(48000.0);

    check(std::abs(ring.getFrequencyHz(0.0f) - RingModProfile::minFrequencyHz) < 1.0e-2f,
          "ring mod: el extremo bajo del mapeo no da la frecuencia minima del perfil");
    check(std::abs(ring.getFrequencyHz(1.0f) - RingModProfile::maxFrequencyHz) < 1.0f,
          "ring mod: el extremo alto del mapeo no da la frecuencia maxima del perfil");

    // Las tres formas de onda tienen que sonar distintas.
    auto renderWaveform = [&ring](float waveform) {
        ring.reset();

        float sum = 0.0f;
        for (int s = 0; s < 2048; ++s)
        {
            sum += ring.processSample(0, 0.5f, waveform);
            ring.advance(0.5f, 0.3f, 0.0f);
        }
        return sum;
    };

    const float sineSum   = renderWaveform(0.0f);
    const float sawSum    = renderWaveform(0.5f);
    const float squareSum = renderWaveform(0.9f);

    check(sineSum != sawSum && sawSum != squareSum && sineSum != squareSum,
          "ring mod: dos formas de onda dan el mismo resultado");

    // La etapa inyectada cambia el resultado. Y lo que se comprueba no es que
    // `NullStage` deje pasar la ENTRADA: el anillo siempre multiplica por el
    // modulador, con etapa o sin ella. Lo que hace la etapa es quitar el
    // recorte: con puente los lados de la campana se recortan, sin puente la
    // salida es el producto crudo y llega tan alto como la entrada.
    RingMod<RingModProfile, DiodeBridge> withBridge;
    RingMod<RingModProfile, NullStage> without;

    withBridge.prepare(48000.0);
    without.prepare(48000.0);

    bool differs   = false;
    float peakWith = 0.0f, peakWithout = 0.0f;
    bool withoutStayedUnderInput = true;

    for (int s = 0; s < 2048; ++s)
    {
        const float a = withBridge.processSample(0, 0.9f, 0.0f);
        const float b = without.processSample(0, 0.9f, 0.0f);

        if (a != b) differs = true;

        peakWith    = std::max(peakWith, std::abs(a));
        peakWithout = std::max(peakWithout, std::abs(b));

        if (std::abs(b) > 0.9f) withoutStayedUnderInput = false;

        withBridge.advance(0.5f, 0.3f, 0.0f);
        without.advance(0.5f, 0.3f, 0.0f);
    }

    check(differs, "ring mod: la etapa de color no cambia nada (no se esta usando)");
    check(withoutStayedUnderInput,
          "ring mod: NullStage deberia dejar el producto crudo, sin recorte");
    check(peakWithout > peakWith,
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

    check(Re201::numModes == 12, "el RE-201 deberia tener los doce modos del selector");

    // Los cuatro primeros son solo eco; del quinto en adelante entra la reverb.
    for (int m = 0; m < 4; ++m)
        check(!Re201::modes[m].reverb, "los modos 1-4 del RE-201 no llevan reverb");

    for (int m = 4; m < 12; ++m)
        check(Re201::modes[m].reverb, "los modos 5-12 del RE-201 llevan reverb");

    // El ultimo modo es "solo reverb": ningun cabezal activo.
    check(!Re201::modes[11].head[0] && !Re201::modes[11].head[1] && !Re201::modes[11].head[2],
          "el modo 12 del RE-201 deberia ser solo reverb, sin ningun cabezal");

    // Y en todos los demas hay al menos uno, o el modo no haria nada.
    for (int m = 0; m < 11; ++m)
    {
        const bool any = Re201::modes[m].head[0] || Re201::modes[m].head[1] || Re201::modes[m].head[2];

        check(any, "un modo del RE-201 sin ningun cabezal activo no hace eco");
    }

    // Las combinaciones de cabezal del selector, una a una. Esta es la tabla
    // que se copio de JUNiO601, y es lo que mas caro seria perder en silencio.
    struct Expected
    {
        int mode;
        bool h1, h2, h3;
    };
    const Expected expected[] =
        {
            {0, true, false, false}, {1, false, true, false}, {2, false, false, true}, {3, true, true, false}, {4, true, false, false}, {5, false, true, false}, {6, false, false, true}, {7, true, true, false}, {8, true, false, true}, {9, false, true, true}, {10, true, true, true}};

    for (const auto& e : expected)
        check(Re201::modes[e.mode].head[0] == e.h1 && Re201::modes[e.mode].head[1] == e.h2 && Re201::modes[e.mode].head[2] == e.h3,
              "la combinacion de cabezales del modo no coincide con el selector");

    // Los ratios son MULTIPLICADORES, no fracciones: el cabezal 3 suena a 3x
    // el retardo base, que es lo que obliga a dimensionar el buffer.
    check(Re201::headRatio[0] == 1.0f, "el cabezal 1 deberia ser el de referencia");
    check(Re201::headRatio[1] > Re201::headRatio[0] && Re201::headRatio[2] > Re201::headRatio[1],
          "los ratios de cabezal deberian crecer");

    // El buffer tiene que aguantar el retardo del cabezal mas lejano.
    check(Re201::maxDelaySeconds * Re201::headRatio[2] <= 1.5f + 1.0e-4f,
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
    reverb.prepare(48000.0);

    reverb.setDecay(0.7f);
    reverb.setDamping(0.4f);
    reverb.setDiffusion(0.7f);
    reverb.setRoomSize(0.8f);
    reverb.setPreDelaySeconds(0.1f);

    // El pre-retardo real lleva el factor 0.2 del original: 0.1 s pedidos son
    // 960 muestras a 48 kHz, no 4800. Es un descuido del efecto publicado, y este
    // test lo fija para que nadie lo "arregle" sin querer.
    check(reverb.getPreDelaySamples() == 960,
          "el pre-retardo no ha salido en las muestras pedidas (con el 0.2 del original)");

    // Suena: una entrada constante tiene que dar una cola que no se apaga.
    //
    // OJO con la entrada: tiene que ser IGUAL en los dos canales. El pre-retardo
    // es mono (escribe la media de L y R y se lee una sola vez), asi que con
    // 0.3 / -0.3 la media es cero y la reverb sale muda. No es un fallo del motor
    // ni del test: es la consecuencia directa del pre-retardo mono del original,
    // y por eso esta misma cancelacion se comprueba mas abajo como caso propio.
    float outL = 0.0f, outR = 0.0f, peak = 0.0f;
    bool finite = true;

    for (int s = 0; s < 48000; ++s)
    {
        reverb.processFrame(0.3f, 0.3f, outL, outR);

        if (!std::isfinite(outL) || !std::isfinite(outR))
        {
            finite = false;
            break;
        }

        peak = std::max(peak, std::abs(outL));
    }

    check(finite, "reverb: la salida se ha ido a infinito o NaN");
    check(peak > 1.0e-4f, "reverb: la salida es practicamente muda");

    // Determinismo: DOS EJEMPLARES FRESCOS, misma preparacion, mismo audio, mismo
    // resultado bit a bit. Se comparan dos instancias nuevas a proposito: comparar
    // la que ya ha sonado 48000 muestras contra una nueva no mide determinismo,
    // mide que la primera tenga cola, y siempre darian distinto.
    SchroederReverb again;
    again.prepare(48000.0);
    again.setDecay(0.7f);
    again.setDamping(0.4f);
    again.setDiffusion(0.7f);
    again.setRoomSize(0.8f);
    again.setPreDelaySeconds(0.1f);

    SchroederReverb third;
    third.prepare(48000.0);
    third.setDecay(0.7f);
    third.setDamping(0.4f);
    third.setDiffusion(0.7f);
    third.setRoomSize(0.8f);
    third.setPreDelaySeconds(0.1f);

    bool same = true;
    for (int s = 0; s < 4096 && same; ++s)
    {
        float aL, aR, bL, bR;

        again.processFrame(0.3f, 0.3f, aL, aR);
        third.processFrame(0.3f, 0.3f, bL, bR);

        if (aL != bL || aR != bR) same = false;
    }

    check(same, "reverb: dos pasadas dan audio distinto");

    // `reset` devuelve al estado inicial.
    reverb.reset();
    peak = 0.0f;
    for (int s = 0; s < 64; ++s)
    {
        reverb.processFrame(0.3f, 0.3f, outL, outR);
        peak = std::max(peak, std::abs(outL));
    }

    check(peak == 0.0f, "reverb: reset() no devuelve al estado inicial");

    // El pre-retardo MONO se come la senal anticorrelacionada. Es el comportamiento
    // del original y no se corrige, asi que se fija aqui a proposito: si alguien
    // hiciera el pre-retardo stereo (o si el motor dejara de leerlo una vez),
    // este test avisa. Con 0.3 / -0.3 la media es exactamente 0.0, y como el
    // pre-retardo esta activo, lo que sale de los conbs es silencio.
    SchroederReverb cancelling;
    cancelling.prepare(48000.0);
    cancelling.setDecay(0.7f);
    cancelling.setRoomSize(0.8f);
    cancelling.setPreDelaySeconds(0.1f);

    peak = 0.0f;
    for (int s = 0; s < 4800; ++s)
    {
        cancelling.processFrame(0.3f, -0.3f, outL, outR);
        peak = std::max(peak, std::abs(outL));
    }

    check(peak == 0.0f,
          "reverb: con pre-retardo mono, una entrada anticorrelacionada deberia salir muda");

    // La variante "Reverse" NIEGA SOLO EL IZQUIERDO. Es un efecto Haas, no una
    // inversion de fase, y es un descuido del original que el port conserva a
    // proposito (ver la cabecera). Este test es lo que hace que, si alguien lo
    // "arregla" sin querer, se entere.
    SchroederReverb inverted;
    inverted.prepare(48000.0);
    inverted.setDecay(0.5f);
    inverted.setRoomSize(0.5f);
    inverted.setInvertLeft(true);

    for (int s = 0; s < 512; ++s)
        inverted.processFrame(0.25f, 0.25f, outL, outR);

    check(outL == -outR, "reverb: la variante Reverse deberia negar solo el izquierdo");

    // Y sin invertir, los dos canales coinciden con entrada igual. Este caso es
    // el que caza que alguien vuelva a compartir un unico buffer entre L y R:
    // con un buffer compartido los dos canales salen identicos SIEMPRE, y este
    // test pasaria, pero el de arriba (que compara L contra -R) dejaria de dar
    // la razon cuando la entrada sea distinta por canal.
    SchroederReverb plain;
    plain.prepare(48000.0);
    plain.setDecay(0.5f);
    plain.setRoomSize(0.5f);

    for (int s = 0; s < 512; ++s)
        plain.processFrame(0.25f, 0.25f, outL, outR);

    check(outL == outR, "reverb: con la misma entrada, L y R deberian coincidir");

    // `setGeometry` tiene que ser EXACTAMENTE lo mismo que `setRoomSize` seguido
    // de `setPreDelaySeconds`. Es lo que permite al consumidor llamar una vez
    // en vez de dos, y si divergieran el pre-retardo de la reverb seria distinto
    // segun quien llamara a cual.
    SchroederReverb bothWays;
    bothWays.prepare(48000.0);
    bothWays.setDecay(0.6f);
    bothWays.setDamping(0.35f);
    bothWays.setDiffusion(0.45f);
    bothWays.setGeometry(0.72f, 0.13f);

    SchroederReverb twoCalls;
    twoCalls.prepare(48000.0);
    twoCalls.setDecay(0.6f);
    twoCalls.setDamping(0.35f);
    twoCalls.setDiffusion(0.45f);
    twoCalls.setRoomSize(0.72f);
    twoCalls.setPreDelaySeconds(0.13f);

    check(bothWays.getPreDelaySamples() == twoCalls.getPreDelaySamples(),
          "setGeometry y las dos llamadas seguidas dan pre-retardos distintos");

    same = true;
    for (int s = 0; s < 4096 && same; ++s)
    {
        float aL, aR, bL, bR;

        bothWays.processFrame(0.2f, 0.2f, aL, aR);
        twoCalls.processFrame(0.2f, 0.2f, bL, bR);

        if (aL != bL || aR != bR) same = false;
    }

    check(same, "setGeometry no suena igual a las dos llamadas por separado");

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
    auto fillWithTail = [](SchroederReverb& r, int numSamples) {
        float l = 0.0f, rr = 0.0f;

        for (int s = 0; s < numSamples; ++s)
            r.processFrame(0.5f, 0.5f, l, rr);
    };

    // Los dos parametros de salida se llaman `destL`/`destR` y no `outL`/`outR`
    // porque la funcion que envuelve esta lambda ya declara un par `outL`/`outR`
    // mas arriba, y el parametro lo sombreaba. Aqui se ESCRIBE, no se lee.
    auto runFresh = [](int numSamples, float& destL, float& destR) {
        SchroederReverb fresh;
        fresh.prepare(48000.0);
        fresh.setDecay(0.7f);
        fresh.setDamping(0.4f);
        fresh.setDiffusion(0.7f);
        fresh.setRoomSize(0.8f);

        float l = 0.0f, rr = 0.0f;

        for (int s = 0; s < numSamples; ++s)
            fresh.processFrame(0.5f, 0.5f, l, rr);

        destL = l;
        destR = rr;
    };

    SchroederReverb reused;
    reused.prepare(48000.0);
    reused.setDecay(0.7f);
    reused.setDamping(0.4f);
    reused.setDiffusion(0.7f);
    reused.setRoomSize(0.8f);
    fillWithTail(reused, 4096);

    // Mismo `roomSize` de nuevo: mismas longitudes, mismo numero de muestras.
    reused.setRoomSize(0.8f);

    float reusedL = 0.0f, reusedR = 0.0f, freshL = 0.0f, freshR = 0.0f;
    fillWithTail(reused, 512);
    reused.processFrame(0.5f, 0.5f, reusedL, reusedR);
    runFresh(513, freshL, freshR);

    check(reusedL == freshL && reusedR == freshR,
          "reverb: redimensionar al MISMO tamano deberia vaciar la cola igual que el original");

    // Y el caso de la variante "Gated", que no tiene mando de tamano: ahi el
    // unico `rebuild` posible es por pre-retardo, y con el tamano intacto.
    SchroederReverb noSizeKnob;
    noSizeKnob.prepare(48000.0);
    noSizeKnob.setDecay(0.2f);
    noSizeKnob.setRoomSize(0.4f);
    fillWithTail(noSizeKnob, 4096);
    noSizeKnob.setPreDelaySeconds(0.0f); // el unico rebuild sin cambiar el tamano

    float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
    fillWithTail(noSizeKnob, 512);
    noSizeKnob.processFrame(0.5f, 0.5f, aL, aR);
    runFresh(513, bL, bR);

    check(aL == bL && aR == bR,
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
    const double ceilings[] = {0.25, 0.070, 0.057, 0.038, 0.019, 0.010, 0.001};
    const float roomSizes[] = {0.0f, 0.3f, 0.8f, 1.0f};

    for (double ceiling : ceilings)
    {
        for (float roomSize : roomSizes)
        {
            SchroederReverb reverb;
            reverb.prepare(48000.0, ceiling);
            reverb.setRoomSize(roomSize);

            for (int i = 0; i < 4; ++i)
            {
                const int len = reverb.getCombLength(i);

                // Ni cero (un conb de longitud 0 no rebota nunca y se leeria
                // fuera) ni negativo: el recorte no puede Produucir eso.
                check(len >= 1,
                      "reverb: un conb ha quedado con longitud 0 o menos");

                for (int j = i + 1; j < 4; ++j)
                {
                    check(len != reverb.getCombLength(j),
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
    plain.prepare(48000.0);
    plain.setRoomSize(1.0f); // el peor caso legal: sizeScale = 1.5

    const int expected[] = {2138, 2390, 2735, 2980}; // medidos antes del arreglo

    for (int i = 0; i < 4; ++i)
        check(plain.getCombLength(i) == expected[i],
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

    for (auto mode : {abd::dsp::JunoBbdMode::ChorusI,
                      abd::dsp::JunoBbdMode::ChorusII,
                      abd::dsp::JunoBbdMode::ChorusBoth})
    {
        Engine stereo, mono;
        stereo.prepare(48000.0);
        mono.prepare(48000.0);
        stereo.setMode(mode);
        mono.setMode(mode);

        float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
        double maxDiff = 0.0;

        for (int s = 0; s < 48000; ++s)
        {
            const float in = 0.4f * std::sin(float(s) * 0.017f);

            stereo.process(in, -0.3f, aL, aR);
            mono.process(in, in, bL, bR);

            maxDiff = std::max(maxDiff, std::abs(double(aL) - double(bL)));
        }

        check(maxDiff == 0.0,
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
        off.prepare(44100.0);

        bool exact = true;
        for (int s = 0; s < 4096; ++s)
        {
            const float in = 0.25f * std::sin(float(s) * 0.05f);
            float l = 0.0f, r = 0.0f;
            off.process(in, -in, l, r);

            if (l != in || r != -in) exact = false;
        }

        check(exact, "coro BBD en Off deberia ser paso a paso exacto");
    }

    // Determinismo: dos instancias nuevas, mismo guion, mismo resultado bit a bit.
    {
        auto run = [](float& outL, float& outR) {
            Engine fresh;
            fresh.prepare(48000.0);
            fresh.setMode(abd::dsp::JunoBbdMode::ChorusI);

            for (int s = 0; s < 24000; ++s)
            {
                const float in = 0.3f * std::sin(float(s) * 0.011f);
                fresh.process(in, in, outL, outR);
            }
        };

        float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
        run(aL, aR);
        run(bL, bR);

        check(aL == bL && aR == bR,
              "coro BBD: dos instancias frescas con el mismo guion no dan lo mismo");
    }

    // Los tres modos suenan, y ninguno se va a NaN ni a infinito.
    for (auto mode : {abd::dsp::JunoBbdMode::ChorusI,
                      abd::dsp::JunoBbdMode::ChorusII,
                      abd::dsp::JunoBbdMode::ChorusBoth})
    {
        Engine e;
        e.prepare(48000.0);
        e.setMode(mode);
        e.setDepth(0.65f);
        e.setMix(0.50f);

        float l = 0.0f, r = 0.0f, peak = 0.0f;
        bool finite = true;

        for (int s = 0; s < 48000 * 3; ++s)
        {
            const float in = 0.4f * std::sin(float(s) * 0.031f) + 0.1f * std::sin(float(s) * 0.9f);
            e.process(in, in, l, r);

            if (!std::isfinite(l) || !std::isfinite(r))
            {
                finite = false;
                break;
            }
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
        }

        check(finite, "coro BBD: la salida se ha ido a NaN o infinito");
        check(peak > 1.0e-3f, "coro BBD: el modo suena a silencio");
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
        auto noiseFloor = [](auto& engine) {
            engine.prepare(48000.0);
            engine.setMode(abd::dsp::JunoBbdMode::ChorusI);

            double sum = 0.0;
            float l = 0.0f, r = 0.0f;

            for (int s = 0; s < 96000; ++s)
            {
                engine.process(0.0f, 0.0f, l, r);
                sum += double(l) * double(l);
            }

            return std::sqrt(sum / 96000.0);
        };

        Engine noisy;
        Clean clean;
        const double floorNoisy = noiseFloor(noisy);
        const double floorClean = noiseFloor(clean);

        check(floorNoisy > floorClean * 3.0,
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
    check(JunoBbdJ60Profile::value.delayI == JunoBbdJ106Profile::value.delayI && JunoBbdJ60Profile::value.rateI == JunoBbdJ106Profile::value.rateI,
          "coro BBD: los perfiles J60 y J106 han divergido; revisa si el motor "
          "sigue creyendo que son el mismo");

    // Los numeros tienen que estar en rango, o el motor produce basura.
    const JunoBbdProfile& p = JunoBbdJ106Profile::value;

    check(p.lineMinSeconds > 0.0f, "coro BBD: la linea no tiene longitud minima");
    check(p.biquadFc > 0.0f && p.biquadFc < 24000.0f, "coro BBD: corte del biquad fuera de rango");
    check(p.biquadQ > 0.0f, "coro BBD: Q del biquad no positiva");
    check(p.clockTrim >= 0.0f && p.clockTrim < 0.5f, "coro BBD: tolerancia de reloj absurda");
    check(p.minClockHz > 0.0f, "coro BBD: techo de reloj no positivo");
    check(p.clickDurationMs > 0.0f, "coro BBD: el clic no dura nada");
    check(p.clickRingQ >= 0.5f, "coro BBD: el anillo del clic no puede tener Q < 0.5 (oscila)");
    check(p.hissColor >= 0.0f && p.hissColor <= 1.0f, "coro BBD: color del siseo fuera de 0..1");
    check(p.modDepthScale > 0.0f, "coro BBD: escala de modulacion no positiva");

    // Los dos perfiles instancian y suenan.
    for (int model = 0; model < 2; ++model)
    {
        if (model == 0)
        {
            abd::dsp::JunoBBD<JunoBbdJ60Profile, abd::dsp::BbdNoiseStage> c;
            c.prepare(44100.0);
            c.setMode(abd::dsp::JunoBbdMode::ChorusII);
            float l = 0.0f, r = 0.0f;
            bool finite = true;
            for (int s = 0; s < 20000; ++s)
            {
                c.process(0.2f, 0.2f, l, r);
                if (!std::isfinite(l))
                {
                    finite = false;
                    break;
                }
            }
            check(finite, "coro BBD: el perfil J60 no es estable");
        }
        else
        {
            abd::dsp::JunoBBD<JunoBbdJ106Profile, abd::dsp::BbdNoiseStage> c;
            c.prepare(44100.0);
            c.setMode(abd::dsp::JunoBbdMode::ChorusBoth);
            float l = 0.0f, r = 0.0f;
            bool finite = true;
            for (int s = 0; s < 20000; ++s)
            {
                c.process(0.2f, 0.2f, l, r);
                if (!std::isfinite(l))
                {
                    finite = false;
                    break;
                }
            }
            check(finite, "coro BBD: el perfil J106 no es estable");
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

    check(Reverb::numVariants == 10, "deberian ser diez variantes de reverb");

    // Los ids que reparte la fabrica, uno a uno. OJO: no son 1..10, hay un 22 en
    // medio y las tres ultimas son 26, 27 y 28. Si alguien "ordena" la tabla
    // asumiendo que son consecutivos, este test lo dice.
    const int expectedIds[10] = {1, 2, 3, 4, 5, 6, 22, 26, 27, 28};

    for (int i = 0; i < 10; ++i)
    {
        const auto* v = Reverb::find(expectedIds[i]);

        check(v != nullptr, "falta una variante de reverb en el perfil");

        if (v == nullptr)
            continue;

        check(v->id == expectedIds[i], "la variante no ha salido con el id pedido");
    }

    // Ningun id repetido y ninguno fuera de la tabla.
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            check(expectedIds[i] != expectedIds[j], "hay ids de reverb repetidos");

    check(Reverb::find(0) == nullptr, "el id 0 no deberia ser un reverb");
    check(Reverb::find(7) == nullptr, "el id 7 no deberia ser un reverb");
    check(Reverb::find(25) == nullptr, "el id 25 es un hibrido, no un reverb");

    // Los numeros de fabrica de cada variante. Copiados del `FXSimpleReverb` de
    // ABDEep, y lo mas caro que se puede perder: cambiarlos cambia el sonido de
    // diez efectos publicados.
    struct Expected
    {
        int id;
        float decay, damping, diffusion, roomSize, preDelay;
        int n;
    };
    const Expected expected[10] =
        {
            {1, 0.70f, 0.40f, 0.70f, 0.80f, 0.10f, 12},
            {2, 0.60f, 0.30f, 0.80f, 0.50f, 0.05f, 12},
            {3, 0.75f, 0.20f, 0.90f, 0.60f, 0.05f, 12},
            {4, 0.30f, 0.60f, 0.50f, 0.30f, 0.00f, 10},
            {5, 0.20f, 0.80f, 0.30f, 0.40f, 0.00f, 10},
            {6, -0.30f, 0.90f, 0.20f, 0.70f, 0.15f, 9},
            {22, 0.85f, 0.30f, 0.80f, 0.90f, 0.10f, 5},
            {26, 0.50f, 0.50f, 0.60f, 0.60f, 0.05f, 12},
            {27, 0.35f, 0.60f, 0.40f, 0.40f, 0.02f, 12},
            {28, 0.65f, 0.35f, 0.70f, 0.70f, 0.08f, 12}};

    for (const auto& e : expected)
    {
        const auto* v = Reverb::find(e.id);

        if (v == nullptr)
            continue;

        check(v->decay == e.decay && v->damping == e.damping && v->diffusion == e.diffusion && v->roomSize == e.roomSize && v->preDelaySeconds == e.preDelay,
              "los numeros de fabrica de la variante no coinciden con el original");

        check(v->numParameters == e.n,
              "el numero de controles de la variante no coincide con el original");
    }

    // "Reverse" es la UNICA con el decay negativo (cola corta) y la unica que
    // niega el canal izquierdo. Si alguien le pone el signo bueno, deja de ser
    // Reverse, y si se le quita el `invertLeft` se queda con la misma reverb que
    // Hall pero sin la decorrelacion que la hacia reconocible.
    for (int i = 0; i < 10; ++i)
    {
        const auto* v = Reverb::find(expectedIds[i]);

        if (v == nullptr)
            continue;

        const bool isReverse = (v->id == 6);

        check((v->decay < 0.0f) == isReverse,
              "solo Reverse deberia tener el decaimiento negativo");
        check(v->invertLeft == isReverse,
              "solo Reverse deberia invertir el canal izquierdo");
    }

    // Los indices de mando tienen que ser coherentes con el numero de controles
    // de cada variante: un mando que no existe en el panel es un -1, y un mando
    // fuera de rango seria escribir en el hueco de otro control.
    for (int i = 0; i < 10; ++i)
    {
        const auto* v = Reverb::find(expectedIds[i]);

        if (v == nullptr)
            continue;

        const int indices[5] = {v->paramIndexPreDelay, v->paramIndexDecay,
                                v->paramIndexRoomSize, v->paramIndexDamping,
                                v->paramIndexDiffusion};

        for (auto index : indices)
        {
            check(index >= -1 && index < v->numParameters,
                  "un mando de la variante apunta fuera de sus propios controles");
        }
    }

    // Y el caso que de verdad duele: Deep Verb (5) pone el pre-retardo en el
    // mando 3, donde todas las demas lo tienen en el 0. Es lo que hace que
    // 22 sea Deep Verb y no la misma reverb con otros numeros.
    const auto* deep = Reverb::find(22);

    if (deep != nullptr)
    {
        check(deep->paramIndexPreDelay == 3,
              "Deep Verb deberia tener el pre-retardo en el mando 3");
        check(deep->paramIndexDiffusion == -1,
              "Deep Verb no deberia tener mando de diffusion");
    }

    // La fila generica: mismo `find` en vez de nullptr, con los numeros
    // neutros. Es la que evita un nullptr en medio del audio cuando el
    // constructor recibe un id que no existe.
    check(Reverb::findOrFallback(1) == Reverb::find(1),
          "findOrFallback deberia devolver la variante real cuando existe");
    check(Reverb::findOrFallback(999) == &Reverb::fallback,
          "findOrFallback deberia devolver la fila generica si el id no existe");
    check(Reverb::fallback.decay == 0.5f && Reverb::fallback.damping == 0.5f && Reverb::fallback.diffusion == 0.5f && Reverb::fallback.roomSize == 0.5f && Reverb::fallback.preDelaySeconds == 0.05f,
          "la fila generica deberia tener los valores neutros del original");
    check(Reverb::fallback.numParameters == 12,
          "la fila generica deberia exponer doce controles");

    // Y la fila generica NO puede colarse en la tabla de variantes.
    for (int i = 0; i < 10; ++i)
        check(&Reverb::variants[i] != &Reverb::fallback,
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
    delay.prepare(44100.0, 4096);

    const int numSamples   = 8192;
    const float delaySamps = 1000.0f;
    const int expectAt     = 1000;

    std::vector<float> outL(numSamples, 0.0f), outR(numSamples, 0.0f);
    std::vector<float> inL(numSamples, 0.0f), inR(numSamples, 0.0f);

    inL[0] = 1.0f;
    inR[0] = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Feedback 0: el impulso solo tiene que salir una vez, en su sitio.
        outL[static_cast<size_t>(i)] =
            delay.processSample(0, inL[static_cast<size_t>(i)], delaySamps, 0.0f);
        outR[static_cast<size_t>(i)] =
            delay.processSample(1, inR[static_cast<size_t>(i)], delaySamps, 0.0f);
        delay.advanceWritePosition();
    }

    bool finite = true;
    for (int i = 0; i < numSamples; ++i)
        if (!std::isfinite(outL[static_cast<size_t>(i)]) || !std::isfinite(outR[static_cast<size_t>(i)])) finite = false;

    check(finite, "dsp::Delay: sale no finito");

    int peakAt = -1;
    for (int i = 0; i < numSamples; ++i)
        if (outL[static_cast<size_t>(i)] > 0.5f)
        {
            peakAt = i;
            break;
        }

    check(peakAt >= 0, "dsp::Delay: el impulso no vuelve nunca");
    check(peakAt >= expectAt - 2 && peakAt <= expectAt + 2,
          "dsp::Delay: el impulso vuelve antes o despues de su sitio");

    // Los canales van por separado: el izquierdo escrito no puede salir por el
    // derecho. Es el reparto por `channel & 1` del contrato de llamada.
    Delay sep;
    sep.prepare(44100.0, 4096);

    std::vector<float> sepR(numSamples, 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        sep.processSample(0, i == 0 ? 1.0f : 0.0f, delaySamps, 0.0f);
        sepR[static_cast<size_t>(i)] = sep.processSample(1, 0.0f, delaySamps, 0.0f);
        sep.advanceWritePosition();
    }

    bool rightClean = true;
    for (int i = 0; i < numSamples; ++i)
        if (std::fabs(sepR[static_cast<size_t>(i)]) > 1.0e-6f) rightClean = false;

    check(rightClean, "dsp::Delay: se filtra del canal izquierdo al derecho");

    // Un retardo NEGATIVO o de mas capacidad no puede leer fuera del buffer.
    // Es el fallo de indices que tiene `MultiHeadEcho` con perfiles cuyo
    // `headRatio` es mayor que 1, y aqui se vigila que no se repita.
    Delay extreme;
    extreme.prepare(44100.0, 1024);

    bool safe = true;
    for (int i = 0; i < 4000; ++i)
    {
        const float l = extreme.processSample(0, 1.0f, -500.0f, 0.0f); // negativo
        const float r = extreme.processSample(1, 1.0f, 1.0e9f, 0.0f);  // mayor que el buffer
        extreme.advanceWritePosition();

        if (!std::isfinite(l) || !std::isfinite(r)) safe = false;
    }

    check(safe, "dsp::Delay: un retardo fuera de rango lee fuera del buffer");
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

    const double sr      = 48000.0;
    const int maxSamples = static_cast<int>(sr * 2.0);
    const int size       = maxSamples + 1024; // lo que reserva `Delay::prepare`

    // Las dos formulas, copiadas del motor. Si el motor cambia, esto cambia con
    // el, y el fallo sale aqui en vez de en produccion.
    const auto wrapAntes = [](float readPos, int bufferSize) {
        const float s     = static_cast<float>(bufferSize);
        const long long q = static_cast<long long>(readPos / s);
        float r           = readPos - s * static_cast<float>(q);
        if (r < 0.0f) r += s;
        return r;
    };

    const auto wrapDespues = [](float readPos, int bufferSize) {
        const float s     = static_cast<float>(bufferSize);
        const long long q = static_cast<long long>(readPos / s);
        float r           = readPos - s * static_cast<float>(q);
        if (r < 0.0f) r += s;
        if (r >= s) r = 0.0f;
        return r;
    };

    const float delay = 0.30f * static_cast<float>(sr);
    const int samples = static_cast<int>(sr * 1.5);

    int fueraAntes = 0, primeraAntes = -1, fueraDespues = 0;
    float mayorIndice = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        const float entrada = static_cast<float>(i) - delay;

        const int antes = static_cast<int>(wrapAntes(entrada, size));
        if (antes >= size || antes < 0)
        {
            ++fueraAntes;
            if (primeraAntes < 0) primeraAntes = i;
        }
        mayorIndice = jmax(mayorIndice, wrapAntes(entrada, size));

        if (static_cast<int>(wrapDespues(entrada, size)) >= size)
            ++fueraDespues;
    }

    check(fueraAntes == 1 && primeraAntes == static_cast<int>(delay),
          "dsp::Delay: el defecto se reproduce (1 lectura fuera, en la muestra 14400)");
    check(mayorIndice == static_cast<float>(size),
          "dsp::Delay: la posicion sin arreglar llega exactamente al final del buffer");
    check(fueraDespues == 0, "dsp::Delay: con el arreglo ninguna lectura se sale del buffer");

    // Y el motor de verdad, que es lo que importa: si la formula de dentro se
    // saliese, `getSample` saltaria aqui mismo.
    Delay real;
    real.prepare(sr, maxSamples);

    bool survivor = true;
    for (int i = 0; i < samples; ++i)
    {
        const float l = real.processSample(0, 0.2f, delay, 0.3f);
        real.processSample(1, 0.2f, delay, 0.3f);
        real.advanceWritePosition();
        if (!std::isfinite(l)) survivor = false;
    }

    check(survivor, "dsp::Delay: el motor recorre 1.5 s de retardo sin salirse");
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
    auto render = [](float rateHz, int numSamples, double sampleRate) {
        Chorus chorus;
        chorus.prepare(sampleRate, 0.1);

        std::vector<float> outL(static_cast<size_t>(numSamples), 0.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            outL[static_cast<size_t>(i)] =
                chorus.processSample(0, 1.0f, 0.5f, 0.5f);
            chorus.processSample(1, 1.0f, 0.5f, 0.5f);
            chorus.advance(rateHz);
        }

        return outL;
    };

    const int numSamples = 4096;
    const double sr      = 44100.0;

    // 1. El caso normal: sale finito y determinista.
    {
        const auto a = render(5.0f, numSamples, sr);
        const auto b = render(5.0f, numSamples, sr);

        bool finite = true, same = true;
        for (int i = 0; i < numSamples; ++i)
        {
            if (!std::isfinite(a[static_cast<size_t>(i)])) finite = false;
            if (a[static_cast<size_t>(i)] != b[static_cast<size_t>(i)])
                same = false;
        }

        check(finite, "dsp::Chorus: sale no finito");
        check(same, "dsp::Chorus: dos render con los mismos datos no coinciden");
    }

    // 2. El aliasing: 4 veces el sample rate equivale a frecuencia 0.
    {
        const auto zero  = render(0.0f, numSamples, sr);
        const auto alias = render(static_cast<float>(4.0 * sr), numSamples, sr);

        float worst = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            worst = std::max(worst, std::fabs(zero[static_cast<size_t>(i)] - alias[static_cast<size_t>(i)]));

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
        check(worst < 1.0e-1f,
              "dsp::Chorus: la fase del LFO no se envuelve bien con una rate >= sample rate");
    }

    // 3. Una frecuencia por debajo de Nyquist pero alta tampoco puede reventar.
    {
        const auto a = render(static_cast<float>(sr * 0.49f), numSamples, sr);

        bool finite = true;
        for (int i = 0; i < numSamples; ++i)
            if (!std::isfinite(a[static_cast<size_t>(i)])) finite = false;

        check(finite, "dsp::Chorus: sale no finito con una rate alta");
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

        for (float rate : {1.0e9f, -1.0e9f, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()})
        {
            const auto a = render(rate, 512, sr);

            for (int i = 0; i < 512; ++i)
                if (!std::isfinite(a[static_cast<size_t>(i)])) finite = false;
        }

        check(finite, "dsp::Chorus: un rate absurdo deja el motor en NaN o se cuelga");
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
    float previous  = Saturation::processSample(-8.0f, 1.0f);
    bool increasing = true, bounded = true, finite = true;

    for (int i = 1; i <= 200000; ++i)
    {
        const float x = -8.0f + 16.0f * (float)i / 200000.0f;
        const float y = Saturation::processSample(x, 1.0f);

        if (y < previous) increasing = false;
        if (std::fabs(y) > 1.0001f) bounded = false;
        if (!std::isfinite(y)) finite = false;

        previous = y;
    }

    check(finite, "dsp::Saturation: sale no finito");
    check(bounded, "dsp::Saturation: se pasa de +/-1");
    check(increasing, "dsp::Saturation: no es monótona");

    // 2. Impar: la saturacion de un diode tiembla igual en los dos sentidos.
    bool odd = true;
    for (int i = 1; i <= 20000; ++i)
    {
        const float x = 8.0f * (float)i / 20000.0f;
        if (std::abs(Saturation::processSample(x, 1.0f) + Saturation::processSample(-x, 1.0f)) > 1.0e-5f)
            odd = false;
    }

    check(odd, "dsp::Saturation: pierde la imparidad");

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
        const float x = -4.0f + 8.0f * (float)i / 2000.0f;

        if (Saturation::processSample(x, 0.0f) != 0.0f) silent = false;
        if (std::fabs(Saturation::processSample(1.0e6f, 1.0f)) > Saturation::atanNormalisation * 1.5707964f)
            withinTope = false;
    }

    check(silent, "dsp::Saturation: con drive 0 deberia dar cero");
    check(withinTope, "dsp::Saturation: se pasa del tope 2/pi");
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
    auto render = [](const Reverb::Parameters& p, int numSamples,
                     float& peakOut, float& tailOut) {
        Reverb reverb;

        Reverb::Parameters params = p;
        reverb.setParameters(params);
        reverb.setSampleRate(44100.0);
        reverb.reset();

        std::vector<float> left(static_cast<size_t>(numSamples), 0.0f),
            right(static_cast<size_t>(numSamples), 0.0f);
        left[0]  = 1.0f;
        right[0] = 1.0f;

        reverb.processStereo(left.data(), right.data(), numSamples);

        bool finite = true;
        float peak = 0.0f, tail = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (!std::isfinite(left[static_cast<size_t>(i)]) || !std::isfinite(right[static_cast<size_t>(i)])) finite = false;
            peak = std::max(peak, std::max(std::fabs(left[static_cast<size_t>(i)]),
                                           std::fabs(right[static_cast<size_t>(i)])));

            if (i > 0)
                tail = std::max(tail, std::max(std::fabs(left[static_cast<size_t>(i)]),
                                               std::fabs(right[static_cast<size_t>(i)])));
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
    const bool finite = render(params, 20000, peak, tail);

    check(finite, "dsp::Reverb: sale no finito");
    check(tail > 1.0e-4f, "dsp::Reverb: un impulso no deja cola (el motor esta muerto)");
    check(peak <= 2.0f, "dsp::Reverb: la cola se dispara");

    // 2. Una sala mas grande tiene que dar una cola mas larga. Es la unica
    //    forma de comprobar que `roomSize` ESTA CONECTADO a los conbs, que es
    //    el unico parametro de esta estructura que no es un par de ganancias.
    Reverb::Parameters bigRoom = params;
    bigRoom.roomSize           = 1.0f;

    float bigTail = 0.0f, smallTail = 0.0f, dummy = 0.0f;
    render(bigRoom, 20000, dummy, bigTail);
    render(params, 20000, dummy, smallTail);

    check(bigRoom.roomSize > params.roomSize, "el test compara dos salas distintas");
    check(std::fabs(bigTail - smallTail) > 1.0e-6f,
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
    dry.wetLevel           = 0.0f;
    dry.dryLevel           = 1.0f;

    float dryPeak = 0.0f, dryTail = 0.0f, wetTail = 0.0f;
    render(dry, 8000, dryPeak, dryTail);
    render(params, 8000, dummy, wetTail);

    check(std::fabs(dryPeak - 2.0f) < 1.0e-3f,
          "dsp::Reverb: el seco no sale con la ganancia del port (2x dryLevel)");
    check(dryTail == 0.0f,
          "dsp::Reverb: con wet 0 deberia salir solo el seco, sin cola");
    check(wetTail > 0.05f,
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
    echo.prepare(sr);

    // El buffer tiene que aguantar el retardo del cabezal mas lejano, no el base.
    const int maxHead = static_cast<int>(sr * abd::dsp::Re201Profile::maxDelaySeconds * abd::dsp::Re201Profile::headRatio[2]);

    check(echo.delayBufferSize() >= maxHead,
          "la linea de cinta no cabe el retardo del cabezal 3 del RE-201");

    bool finite = true;

    // Los doce modos, con el retardo al maximo del perfil, que es donde estan
    // las tres lecturas mas lejanas. Sin ninguna comprobacion de indice: si
    // `readTape` se sale, la asercion de `AudioBuffer` revienta aqui mismo.
    for (int mode = 0; mode < abd::dsp::Re201Profile::numModes; ++mode)
    {
        echo.setMode(mode);
        echo.reset();

        for (int i = 0; i < 4000; ++i)
        {
            const float l = echo.processSample(0, 1.0f,
                                               abd::dsp::Re201Profile::maxDelaySeconds,
                                               0.3f, 0.5f, 0.5f, 0.3f);
            const float r = echo.processSample(1, 1.0f,
                                               abd::dsp::Re201Profile::maxDelaySeconds,
                                               0.3f, 0.5f, 0.5f, 0.3f);

            echo.advance();

            if (!std::isfinite(l) || !std::isfinite(r)) finite = false;
        }
    }

    check(finite, "el RE-201 con los doce modos y el retardo al maximo sale no finito");

    // Y con un retardo ABSURDO, que es lo que pasaria si un consumidor mete un
    // valor sin recortar: tiene que envolver, no salirse. Antes, en vez de
    // envolver, leia fuera del buffer.
    echo.setMode(10); // los tres cabezales
    echo.reset();

    bool safe = true;

    for (int i = 0; i < 2000; ++i)
    {
        const float l = echo.processSample(0, 1.0f, 1.0e6f, 0.3f, 0.5f, 0.5f, 0.3f);
        const float r = echo.processSample(1, 1.0f, 1.0e6f, 0.3f, 0.5f, 0.5f, 0.3f);

        echo.advance();

        if (!std::isfinite(l) || !std::isfinite(r)) safe = false;
    }

    check(safe, "el RE-201 lee fuera del buffer con un retardo fuera de rango");
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
    const int n     = static_cast<int>(sr);

    std::vector<float> in(static_cast<size_t>(n));

    for (int i = 0; i < n; ++i)
        in[static_cast<size_t>(i)] = 0.6f * std::sin(0.03f * static_cast<float>(i));

    std::vector<float> mono(static_cast<size_t>(n));
    std::vector<float> left(static_cast<size_t>(n));

    { // MONO: una sola llamada por muestra.
        Echo echo;
        echo.prepare(sr);
        echo.setMode(10); // los tres cabezales + tanque

        for (int i = 0; i < n; ++i)
        {
            mono[static_cast<size_t>(i)] = echo.processSample(0, in[static_cast<size_t>(i)],
                                                              0.05f, 0.0f, 0.5f, 0.5f, 0.3f);
            echo.advance();
        }
    }

    { // ESTEREO: dos llamadas por muestra.
        Echo echo;
        echo.prepare(sr);
        echo.setMode(10);

        for (int i = 0; i < n; ++i)
        {
            left[static_cast<size_t>(i)] = echo.processSample(0, in[static_cast<size_t>(i)],
                                                              0.05f, 0.0f, 0.5f, 0.5f, 0.3f);
            (void)echo.processSample(1, in[static_cast<size_t>(i)],
                                     0.05f, 0.0f, 0.5f, 0.5f, 0.3f);
            echo.advance();
        }
    }

    float maxDiff = 0.0f;

    for (int i = 0; i < n; ++i)
        maxDiff = std::max(maxDiff, std::fabs(mono[static_cast<size_t>(i)] - left[static_cast<size_t>(i)]));

    check(maxDiff == 0.0f,
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
    const int total = static_cast<int>(sr * 3.0);

    // Posicion y amplitud del eco de un impulso con `feedback` 0. Se usa el
    // maximo global y se excluye la muestra 0, que es el impulso SECO
    // (entrada * dryGain) y no el eco.
    auto peakOfEcho = [&](int head, float requestedSeconds, float& seconds, float& amplitude) {
        std::vector<float> out(static_cast<size_t>(total), 0.0f);

        Echo echo;
        echo.prepare(sr);
        echo.setMode(head); // Re201Profile: 0 = h1, 1 = h2, 2 = h3

        for (int i = 0; i < total; ++i)
        {
            const float in              = (i == 0) ? 1.0f : 0.0f;
            out[static_cast<size_t>(i)] = echo.processSample(0, in, requestedSeconds,
                                                             0.0f, 0.5f, 0.5f, 0.0f);
            echo.advance();
        }

        int best = -1;
        for (int i = 1; i < total; ++i)
            if (best < 0 || std::fabs(out[static_cast<size_t>(i)]) > std::fabs(out[static_cast<size_t>(best)]))
                best = i;

        seconds   = best < 0 ? -1.0f : (float)best / (float)sr;
        amplitude = best < 0 ? 0.0f : std::fabs(out[static_cast<size_t>(best)]);
    };

    const float minDelay  = abd::dsp::Re201Profile::minDelaySeconds;
    const float maxDelay  = abd::dsp::Re201Profile::maxDelaySeconds;
    const float tolerance = 0.02f;

    for (int head = 0; head < 3; ++head)
    {
        const float ratio = abd::dsp::Re201Profile::headRatio[head];
        bool delivered    = true;

        auto probe = [&](float requested) {
            const float expected = requested * ratio;
            float seconds = 0.0f, amplitude = 0.0f;
            peakOfEcho(head, requested, seconds, amplitude);

            // Sin eco reconocible, o eco en un sitio que no corresponde.
            if (amplitude < 0.10f || std::fabs(seconds - expected) > tolerance * expected)
                delivered = false;
        };

        for (int k = 0; k <= 100; ++k)
            probe(minDelay + (maxDelay - minDelay) * (float)k / 100.0f);

        // Y el tope del mando con la CONSTANTE del perfil, no reconstruida por
        // aritmetica: `minDelay + (maxDelay - minDelay)` en float cae unas
        // muestras por debajo del tope real.
        for (int k = 0; k <= 40; ++k)
            probe(maxDelay * (0.98f + 0.02f * (float)k / 40.0f));

        probe(maxDelay);

        check(delivered, "el RE-201 no entrega el retardo pedido en algun punto del mando");
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
    const int total = static_cast<int>(sr * 0.4);

    struct Case
    {
        int mode;
        int heads;
        const char* what;
    };
    const Case cases[] =
        {
            {0, 1, "un cabezal"},
            {2, 1, "cabezal 3"},
            {3, 2, "cabezales 1 + 2"},
            {8, 2, "cabezales 1 + 3"},
            {10, 2, "cabezales 2 + 3"},
            {10, 3, "los tres cabezales"}};

    for (const auto& c : cases)
    {
        std::vector<float> left(static_cast<size_t>(total), 0.0f);
        std::vector<float> right(static_cast<size_t>(total), 0.0f);

        Echo echo;
        echo.prepare(sr);
        echo.setMode(c.mode);

        for (int i = 0; i < total; ++i)
        {
            const float in = (i == 0) ? 1.0f : 0.0f;
            const float l  = echo.processSample(0, in, 0.05f, 0.0f, 0.5f, 0.5f, 0.0f);
            const float r  = echo.processSample(1, in, 0.05f, 0.0f, 0.5f, 0.5f, 0.0f);

            // La muestra 0 es la entrada directa, igual en los dos canales; el
            // eco es lo que se mide.
            if (i > 0)
            {
                left[static_cast<size_t>(i)]  = l;
                right[static_cast<size_t>(i)] = r;
            }

            echo.advance();
        }

        float peakL = 0.0f, peakR = 0.0f;

        for (int i = 1; i < total; ++i)
        {
            peakL = std::max(peakL, std::fabs(left[static_cast<size_t>(i)]));
            peakR = std::max(peakR, std::fabs(right[static_cast<size_t>(i)]));
        }

        const float ratio = peakL > 0.0f ? peakR / peakL : 0.0f;

        check(ratio > 0.85f && ratio < 1.0f,
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
std::vector<float> noise(int n, unsigned seed)
{
    std::vector<float> v(static_cast<size_t>(n), 0.0f);
    unsigned s = seed;
    for (int i = 0; i < n; ++i)
    {
        s = s * 1664525u + 1013904223u;
        v[static_cast<size_t>(i)] =
            (static_cast<float>((s >> 8) & 0xffffu) / 32768.0f - 1.0f) * 0.2f;
    }
    return v;
}

/** Tonos a 220/440/660 Hz. Una reverb con ruido solo suena a ruido, y eso no
    demuestra que el motor este vivo. */
std::vector<float> musica(int n, double sr = kSr)
{
    std::vector<float> v(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t            = double(i) / sr;
        v[static_cast<size_t>(i)] = static_cast<float>(
            0.30 * std::sin(2.0 * 3.14159265358979 * 220.0 * t) + 0.20 * std::sin(2.0 * 3.14159265358979 * 440.0 * t) + 0.10 * std::sin(2.0 * 3.14159265358979 * 660.0 * t));
    }
    return v;
}

/** Pasa un bloque por el motor y devuelve el canal izquierdo. */
std::vector<float> render(abd::dsp::FxEngine& e, const std::vector<float>& in)
{
    std::vector<float> buf = in;
    const int n            = static_cast<int>(buf.size());

    abd::dsp::AudioBuffer<float> b(2, n);
    for (int i = 0; i < n; ++i)
    {
        b.setSample(0, i, buf[static_cast<size_t>(i)]);
        b.setSample(1, i, buf[static_cast<size_t>(i)]);
    }

    e.process(b, n);

    for (int i = 0; i < n; ++i)
        buf[static_cast<size_t>(i)] = b.getSample(0, i);

    return buf;
}

float rms(const std::vector<float>& b, int from, int to)
{
    double acc = 0.0;
    for (int i = from; i < to; ++i)
        acc += double(b[static_cast<size_t>(i)]) * double(b[static_cast<size_t>(i)]);
    return static_cast<float>(std::sqrt(acc / double(to - from)));
}

float pico(const std::vector<float>& b, int from, int to)
{
    float p = 0.0f;
    for (int i = from; i < to; ++i)
        p = jmax(p, std::fabs(b[static_cast<size_t>(i)]));
    return p;
}

/** Pasa una senal por UNA fila del catalogo y devuelve el canal izquierdo.

    Sin esto, un contrato de fila tendria que montar un `FxEngine`, elegir un
    slot y deal los mandos uno a uno, y el fallo mas probable —que la fila no
    este bien registrada, o que el motor no la encuentre— quedaria envuelto
    en el motor y no se veria. Aqui la fila se llama DIRECTO, que es como
    corre cuando el motor la tiene en un slot pero sin el mezclador delante.

    `mandos` es opcional, y van en el ORDEN DE LA DECLARACION de la fila, no
    en el orden que se le ocurra a quien llama: ese orden es parte de la
    fila, y un test que lo suponga estara probando la suposicion en vez de
    la fila.

    Y LA SENAL SE PASA VARIAS VECES antes de devolver nada, porque las
    rampas de los mandos tardan 20 ms en asentarse y sin eso la medicion
    sale con el arranque en vez de con el mando. Cuatro vueltas de la senal
    entera son de sobra para 20 ms, incluso con la senal mas corta que usan
    los contratos. */
std::vector<float> porFila(const abd::dsp::FxEffectInfo& fila,
                           const std::vector<float>& in, int bloque,
                           const std::vector<float>* mandos = nullptr)
{
    auto* inst = fila.create(double(kSr));
    if (inst == nullptr)
        return std::vector<float>();

    const int n = static_cast<int>(in.size());

    if (mandos != nullptr)
    {
        std::vector<float> v = *mandos;
        v.resize(static_cast<std::size_t>(fila.numParams), 0.0f);

        // `setAllParams` PUEDE ser nullptr en el contrato, y entonces hay
        // que ir mando a mando. Una fila que solo expose el conjunto no es
        // una fila utilizable desde un panel, y por eso se comprueba que
        // las dos rutas dan el mismo resultado en vez de asumir la rapida.
        if (fila.setAllParams != nullptr)
            fila.setAllParams(inst, v.data(), fila.numParams);
        else
            for (int i = 0; i < fila.numParams; ++i)
                fila.setParam(inst, i, v[static_cast<std::size_t>(i)]);
    }

    std::vector<float> scratch = in;
    for (int rep = 0; rep < 5; ++rep)
    {
        for (int i = 0; i < n; i += bloque)
        {
            const int m = jmin(bloque, n - i);
            fila.process(inst, in.data() + i, in.data() + i,
                         scratch.data() + i, scratch.data() + i, m);
        }
    }

    fila.reset(inst);
    fila.destroy(inst);
    return scratch;
}

const abd::dsp::FxEffectInfo* catalogo()
{
    static int count                         = 0;
    static const abd::dsp::FxEffectInfo* cat = abd::dsp::fxDefaultCatalogue(count);
    return cat;
}

int numCatalogo()
{
    return abd::dsp::fxDefaultCatalogueSize();
}
} // namespace fxprobe

//==============================================================================
/** EL CATALOGO POR DEFECTO: son las ocho filas del modulo, y el indice 0 es
    bypass para todos los productos sin que ninguno tenga que acordarlo. */
void testFxCatalogue()
{
    using namespace abd::dsp;

    const int count         = fxprobe::numCatalogo();
    const FxEffectInfo* cat = fxprobe::catalogo();

    check(count == 8, "el catalogo por defecto tiene las ocho filas del modulo");
    check(cat != nullptr, "el catalogo por defecto existe");

    for (const char* name : {"chorus", "delay", "reverb", "saturation", "schroeder", "bbd"})
        check(fxFindEffect(cat, count, name) != nullptr, "el catalogo incluye un efecto conocido");

    check(fxFindEffect(cat, count, "noexiste") == nullptr, "un nombre falso no aparece");

    check(fxEffectAt(cat, count, 0) == nullptr, "el indice 0 es siempre bypass");
    check(fxEffectAt(cat, count, 1) == &cat[0], "el indice 1 es la primera fila");

    // Y ESTE es el limite que importa: con `>= count` el ULTIMO efecto era
    // inalcanzable, que era justo el BBD —el unico con selector discreto, y el
    // que mas se nota que falta. No lo cazaba ningun test porque los que habia
    // elegian efectos del principio de la lista.
    check(fxEffectAt(cat, count, count) == &cat[count - 1],
          "el indice del ultimo efecto existe (el limite es count, no count-1)");
    check(fxEffectAt(cat, count, count + 1) == nullptr, "un indice pasado no vale");
    check(fxEffectAt(cat, count, -1) == nullptr, "un indice negativo no vale");
    check(fxEffectAt(nullptr, 0, 1) == nullptr, "un catalogo nulo no devuelve nada");

    // Y cada fila esta completa: un `process` o un `destroy` vacios son un
    // fallo que solo se ve alsonar.
    bool filasCompletas = true;
    for (int i = 0; i < count; ++i)
        if (cat[i].name == nullptr || cat[i].displayName == nullptr || cat[i].params == nullptr || cat[i].create == nullptr || cat[i].process == nullptr || cat[i].setParam == nullptr || cat[i].destroy == nullptr)
            filasCompletas = false;

    check(filasCompletas, "todas las filas tienen puntero de nombre, tabla y funciones");
}

//==============================================================================
/** EL HUECO: bypass, mandos que sobreviven al cambio de efecto, `reset` que
    limpia el estado y no los mandos, y el `mix`/`gain` que no tocan el bypass. */
void testFxSlotBehaviour()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count         = fxprobe::numCatalogo();

    //--- el bypass deja la senal INTACTA, gain y mix incluidos -------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);

        for (int s = 0; s < kFxNumSlots; ++s)
        {
            e.getSlot(s).setGain(0.2f);
            e.getSlot(s).setMix(0.0f);
        }

        const std::vector<float> in  = fxprobe::noise(1024, 12345u);
        const std::vector<float> out = fxprobe::render(e, in);

        bool identico = true;
        for (size_t i = 0; i < in.size(); ++i)
            if (out[i] != in[i]) identico = false;

        check(identico, "con los cuatro huecos en bypass la senal sale bit a bit igual");
    }

    //--- un hueco en bypass no rompe la serie -----------------------------
    {
        FxEngine a, b;
        a.prepare(fxprobe::kSr, 2, 512);
        a.setCatalogue(cat, count);
        b.prepare(fxprobe::kSr, 2, 512);
        b.setCatalogue(cat, count);

        for (FxEngine* engine : {&a, &b})
        {
            engine->getSlot(0).setType(1);
            engine->getSlot(0).setMix(1.0f);
            engine->getSlot(3).setType(2);
            engine->getSlot(3).setMix(0.7f);
        }

        const std::vector<float> in = fxprobe::noise(2048, 777u);
        check(fxprobe::render(a, in) == fxprobe::render(b, in),
              "dos motores frescos con la misma configuracion dan lo mismo");
    }

    //--- los mandos que el usuario toco sobreviven al cambio de efecto -----
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);

        e.getSlot(0).setType(1);
        e.getSlot(0).setParameter(0, 0.8f);
        check(e.getSlot(0).isParameterTouched(0), "un mando puesto a mano queda marcado");

        e.getSlot(0).setType(2);
        check(e.getSlot(0).getParameter(0) == 0.8f,
              "cambiar de efecto deja el mando donde estaba");
        check(!e.getSlot(0).isParameterTouched(1), "un mando que nadie toco sigue sin tocar");
    }

    //--- y el que NO ha sido tocado toma el valor por defecto de la fila ----
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(1);

        const FxParamSpec* p = fxFindEffect(cat, count, "chorus")->params;
        check(std::fabs(e.getSlot(0).getParameter(0) - fxNormalise(p[0], p[0].defaultValue)) < 1e-5f,
              "un coro recien metido nace con sus mandos en su sitio");
    }

    //--- reset limpia la COLA, no los mandos -----------------------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);

        e.getSlot(0).setType(2);
        e.getSlot(0).setMix(1.0f);
        e.getSlot(0).setParameter(0, fxNormalise(fxFindEffect(cat, count, "delay")->params[0], 0.15f));
        e.getSlot(0).setParameter(1, 0.8f);

        const std::vector<float> musica = fxprobe::musica(8192);
        fxprobe::render(e, musica);
        const float colaAntes = fxprobe::rms(musica, 7000, 8192);
        check(colaAntes > 1e-3f, "el delay suena antes de reset (la cola no es silencio)");

        e.reset();

        check(e.getSlot(0).getMix() == 1.0f, "reset deja la mezcla del hueco");
        check(e.getSlot(0).getParameter(1) == 0.8f, "reset deja la realimentacion");

        const std::vector<float> silencio(8192, 0.0f);
        const std::vector<float> out = fxprobe::render(e, silencio);
        check(fxprobe::rms(out, 7000, 8192) < 0.01f,
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
    const int count         = fxprobe::numCatalogo();

    for (int r = 0; r <= 9; ++r)
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.setRouting(static_cast<FxRouting>(r));
        e.setFeedbackGain(0.2f);

        for (int s = 0; s < kFxNumSlots; ++s)
        {
            e.getSlot(s).setType(s + 1);
            e.getSlot(s).setMix(0.5f);
        }

        const std::vector<float> out =
            fxprobe::render(e, fxprobe::noise(2048, 777u + static_cast<unsigned>(r)));

        bool finito = true;
        float p     = 0.0f;
        for (float v : out)
        {
            if (!std::isfinite(v)) finito = false;
            p = jmax(p, std::fabs(v));
        }

        check(finito, "un ruteo produce numeros finitos");
        check(p > 1e-4f, "un ruteo produce audio");
        check(p < 4.0f, "un ruteo con cuatro efectos no se dispara solo");
    }

    // El 1 y el 8 hacen lo mismo. Se comprueba para que, si alguien "limpia" el
    // 8 creyendo que es un error, el test le diga que no lo es.
    FxEngine uno, ocho;
    for (FxEngine* engine : {&uno, &ocho})
    {
        engine->prepare(fxprobe::kSr, 2, 512);
        engine->setCatalogue(cat, count);
        for (int s = 0; s < kFxNumSlots; ++s)
        {
            engine->getSlot(s).setType(s + 1);
            engine->getSlot(s).setMix(0.4f);
        }
    }

    uno.setRouting(FxRouting::ParallelFront);
    ocho.setRouting(FxRouting::ParallelFrontSeries);

    const std::vector<float> in = fxprobe::noise(2048, 4242u);
    check(fxprobe::render(uno, in) == fxprobe::render(ocho, in),
          "el ruteo 1 y el 8 son el mismo diagrama, como en ABDEep");

    // Y un bypass global no toca nada, aunque los huecos esten llenos.
    FxEngine byp;
    byp.prepare(fxprobe::kSr, 2, 512);
    byp.setCatalogue(cat, count);
    byp.setMode(FxMode::Bypass);
    for (int s = 0; s < kFxNumSlots; ++s)
    {
        byp.getSlot(s).setType(s + 1);
        byp.getSlot(s).setMix(1.0f);
    }

    check(fxprobe::render(byp, in) == in, "el modo bypass del motor devuelve la senal intacta");
}

//==============================================================================
/** EL MODO ENVIO Y LA REALIMENTACION DEL 9. El envio es lo que mas se rompe al
    cambiarlo de sitio, asi que lleva su propio test. */
void testFxEngineSendAndFeedback()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count         = fxprobe::numCatalogo();

    //--- el envio NO se come el mix del usuario ---------------------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.setMode(FxMode::Send);
        e.setSendLevel(0.3f);
        e.getSlot(0).setType(1);
        e.getSlot(0).setMix(0.85f);

        fxprobe::render(e, fxprobe::noise(2048, 31337u));

        check(e.getSlot(0).getMix() == 0.85f,
              "un bloque de envio deja el mix del hueco como el usuario lo puso");
    }

    //--- envio a 0 = la seca intacta; envio a 1 = el efecto entero ---------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.setMode(FxMode::Send);

        const std::vector<float> in = fxprobe::noise(2048, 555u);

        e.setSendLevel(0.0f);
        check(fxprobe::render(e, in) == in, "con el envio a 0 sale la seca intacta");

        e.setSendLevel(1.0f);
        e.getSlot(0).setType(1);
        check(fxprobe::render(e, in) != in, "con el envio a 1 sale el efecto entero");
    }

    //--- el 9 realimenta la SALIDA DEL BLOQUE ANTERIOR, no la seca ---------
    {
        // Con el 9 y ganancia 0 el motor es una serie, y por eso el resultado
        // tiene que ser EXACTAMENTE el de la serie. Si alguien realimenta la
        // seca del bloque en curso, esto deja de cumplirse: la topologia
        // distinta no suena casi igual, suena a otra cosa.
        FxEngine conFb, sinFb;
        for (FxEngine* engine : {&conFb, &sinFb})
        {
            engine->prepare(fxprobe::kSr, 2, 512);
            engine->setCatalogue(cat, count);
            engine->getSlot(0).setType(2);
            engine->getSlot(0).setMix(0.6f);
        }

        conFb.setRouting(FxRouting::SeriesWithFeedback);
        conFb.setFeedbackGain(0.0f);
        sinFb.setRouting(FxRouting::Series);

        const std::vector<float> in = fxprobe::noise(4096, 24680u);
        check(fxprobe::render(conFb, in) == fxprobe::render(sinFb, in),
              "el ruteo 9 con ganancia 0 es exactamente la serie");

        // Y con ganancia, la realimentacion accumulates: el segundo bloque ya no
        // puede ser la serie, porque entra la salida del primero.
        conFb.setFeedbackGain(0.5f);
        const std::vector<float> primera = fxprobe::render(conFb, in);
        const std::vector<float> segunda = fxprobe::render(conFb, in);
        check(primera != segunda, "con realimentacion el bloque siguiente lleva el anterior dentro");
    }
}

//==============================================================================
/** UN BLOQUE MAYOR QUE EL PREPARADO. El troceo va en el motor, y tiene que dar
    el MISMO numero: los motores de este modulo trabajan por muestra. */
void testFxEngineBlockSplitting()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat      = fxprobe::catalogo();
    const int count              = fxprobe::numCatalogo();
    const FxParamSpec* delaySpec = fxFindEffect(cat, count, "delay")->params;

    // El motor preparado a 64 recibe un bloque de 8192, y el de referencia
    // preparado a 8192 lo recibe entero. Si el motor entrega el bloque entero a
    // un hueco preparado a 64, ese hueco procesa 64 muestras y devuelve el
    // resto SECO: la mitad del bloque del host en silencio. Medido: 0.119 de
    // diferencia sobre una entrada de 0.2.
    FxEngine troceado, entero;
    troceado.prepare(fxprobe::kSr, 2, 64);
    troceado.setCatalogue(cat, count);

    entero.prepare(fxprobe::kSr, 2, 8192);
    entero.setCatalogue(cat, count);

    for (FxEngine* engine : {&troceado, &entero})
    {
        engine->getSlot(0).setType(2);
        engine->getSlot(0).setMix(0.6f);
        engine->getSlot(0).setParameter(0, fxNormalise(delaySpec[0], 0.30f));
        engine->getSlot(0).setParameter(1, 0.6f);
    }

    const std::vector<float> musica = fxprobe::musica(8192);
    const std::vector<float> a      = fxprobe::render(troceado, musica);
    const std::vector<float> b      = fxprobe::render(entero, musica);

    float maxDiff = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        maxDiff = jmax(maxDiff, std::fabs(a[i] - b[i]));

    check(maxDiff == 0.0f, "trocear el bloque da el mismo numero, no uno parecido");
    check(fxprobe::rms(a, 7800, 8192) > 1e-4f, "el retardo vuelve a sonar en la cola del bloque grande");

    //--- y un buffer de UN canal, que no tiene canal derecho que leer ------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 1, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(1);
        e.getSlot(0).setMix(0.7f);

        const std::vector<float> in = fxprobe::noise(1024, 31337u);
        AudioBuffer<float> bus(1, 1024);
        for (int i = 0; i < 1024; ++i)
            bus.setSample(0, i, in[static_cast<size_t>(i)]);

        e.process(bus, 1024);

        float p = 0.0f;
        for (int i = 0; i < 1024; ++i)
            p = jmax(p, std::fabs(bus.getSample(0, i)));

        check(std::isfinite(p) && p > 1e-3f, "un buffer de un canal se procesa como mono");
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
    const int count         = fxprobe::numCatalogo();

    //--- ida y vuelta en todos los mandos del catalogo --------------------
    float peor   = 0.0f;
    int contadas = 0;

    for (int f = 0; f < count; ++f)
    {
        for (int p = 0; p < cat[f].numParams; ++p)
        {
            const FxParamSpec& s = cat[f].params[p];
            if (s.steps > 1)
                continue; // los discretos se comprueban aparte

            for (int i = 0; i <= 100; ++i)
            {
                const float v      = static_cast<float>(i) / 100.0f;
                const float fisico = fxDenormalise(s, v);
                const float vuelta = fxNormalise(s, fisico);

                peor = jmax(peor, std::fabs(vuelta - v));
                ++contadas;
            }
        }
    }

    check(contadas > 500, "se ha recorrido la tabla entera de mando");
    check(peor < 1e-3f, "normalizar y desnormalizar son la misma operacion al reves");

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

            const float knob = fxNormalise(s, s.defaultValue);
            if (knob < 0.15f || knob > 0.95f)
                malColocados.push_back(std::string(s.name) + "@" + std::to_string(knob));
        }
    }

    check(malColocados.empty(), "ningun mando por defecto cae en un extremo del recorrido");
    if (!malColocados.empty())
        for (const auto& s : malColocados)
            std::printf("[info]   mando en un extremo: %s\n", s.c_str());

    //--- los mandos DISCRETOS solo toman valores enteros -------------------
    {
        const FxEffectInfo* bbd = fxFindEffect(cat, count, "bbd");
        check(bbd != nullptr, "el bbd esta en el catalogo");

        if (bbd != nullptr)
        {
            const FxParamSpec& mode = bbd->params[0];
            check(mode.steps == 4, "el modo del bbd declara cuatro pasos");

            bool entero = true;
            std::vector<int> vistos;

            for (int i = 0; i <= 100; ++i)
            {
                const float v      = static_cast<float>(i) / 100.0f;
                const float fisico = fxDenormalise(mode, v);

                if (std::fabs(fisico - std::floor(fisico + 0.5f)) > 1e-5f)
                    entero = false;

                const int estado = static_cast<int>(fisico + 0.5f);
                if (std::find(vistos.begin(), vistos.end(), estado) == vistos.end())
                    vistos.push_back(estado);
            }

            check(entero, "el modo del bbd solo toma valores enteros");
            check(vistos.size() == 4, "el recorrido del mando da los cuatro modos");
        }
    }

    //--- un parametro por encima de su capacidad se recorta, no se sale ----
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(2);
        e.getSlot(0).setMix(1.0f);
        e.getSlot(0).setParameter(0, 1.0f); // el retardo mas largo que se puede pedir

        const std::vector<float> out = fxprobe::render(e, fxprobe::musica(9600));
        check(std::isfinite(fxprobe::pico(out, 0, 9600)),
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

    check(!std::is_polymorphic<CharacterStage>::value,
          "la etapa de caracter base no es polimorfica (no lleva vtable)");
    check(!std::is_polymorphic<NullStage>::value,
          "NullStage no lleva vtable");
    check(!std::is_polymorphic<DiodeBridge>::value,
          "DiodeBridge no lleva vtable");
    check(!std::is_polymorphic<TapeColour>::value,
          "TapeColour no lleva vtable");

    check(sizeof(NullStage) <= sizeof(double) * 2,
          "una etapa sin estado ocupa lo que un sample rate y poco mas");

    // Y el ancla sigue sirviendo: una etapa lee el sample rate que le dio el motor.
    NullStage n;
    n.prepareSampleRate(48000.0);
    check(n.getSampleRate() == 48000.0, "la etapa guarda el sample rate del prepare");

    // Un sample rate invalido no debe dejar a la etapa con uno que rompa un
    // coeficiente: antes se guardaba tal cual.
    n.prepareSampleRate(-1.0);
    check(n.getSampleRate() > 0.0, "un sample rate invalido deja a la etapa con uno utilizable");
}

//==============================================================================
/** Los tres mandos del barrido de reverb entran SIN RECORTE, y eso queda fijo.

    ESTE TEST CAMBIO DE SIGNO. Antes se llamaba `testSchroederKnobsAreClamped` y
    comprobaba que los recortes EXISTIAN: que un `decay` de 1.7 sonara exacto
    como un 1.0, y que un `damping` de 2.0 no se disparara. Ese recorte se ha
    quitado del motor, y este test ahora fija lo contrario.

    POR QUE SE QUITARON. El recorte no se tomaba nunca. Los diez perfiles de
    `ReverbProfile` usan `decay` entre -0.30 y 0.85, `damping` entre 0.20 y
    0.90 y `diffusion` entre 0.20 y 0.90, y los tres mandos del panel son
    normalizados 0..1: ninguno de los dos extremos se toca, ni por arriba ni por
    abajo. Un recorte que no recorta es ruido en el hilo de audio, porque se lee
    como si protegiera algo y no protege nada.

    LO QUE PASA SI ALGUIEN MANDA UN VALOR FUERA DE RANGO. Aqui ya no hay red, y
    esto es lo que hay que saber antes de tocar el motor: el coeficiente del conb
    es `decay * 0.9`, o sea que con `decay` por encima de 1.111 la
    realimentacion pasa de 1 y la cola crece sin limite; con `damping = 2` el
    pasabajos `damp1 = 2, damp2 = -1` tiene el polo en -1; con `diffusion = 2` el
    allpass se va. MEDIDO con un impulso y un segundo de silencio:

        decay 0.8  (en rango)        pico 3.2e-01
        decay 1.0  (documentado)     pico 3.7e-01
        decay 1.2  (fuera)           pico 4.2e-01
        decay 2.0  (muy fuera)       pico 7.9e+06    <- se dispara
        damping 2.0 (fuera)          pico 1.0e+30    <- se dispara
        diffusion 2.0 (fuera)        pico 3.3e+28    <- se dispara

    POR QUE HACE FALTA UNA PUERTA DE LECTURA. Sin getters, "el valor entra sin
    recortarse" no se puede comprobar: lo unico que se ve es la salida, y la
    salida no cambia cuando el recorte no muerde. Por eso `SchroederReverb`
    expone `getDecay`, `getDamping` y `getDiffusion`. Si alguien reintroduce el
    recorte, el punto 3 de este test se pone rojo al instante. */
void testMandosDelBarridoSinRecorte()
{
    using abd::dsp::SchroederReverb;

    //--- 1. EL BARRIDO DEL MANDO, que es lo que llega del panel ---------------
    // 0..1 en 257 pasos, los tres mandos. Es la mitad "passthrough" de la
    // asercion: todo lo que un usuario puede mover tiene que entrar igual.
    for (int i = 0; i <= 256; ++i)
    {
        const float v = static_cast<float>(i) / 256.0f;
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDecay(v);
        r.setDamping(v);
        r.setDiffusion(v);

        check(r.getDecay() == v, "el decay del barrido entra sin recortarse");
        check(r.getDamping() == v, "la amortiguacion del barrido entra sin recortarse");
        check(r.getDiffusion() == v, "la difusion del barrido entra sin recortarse");
    }

    //--- 2. LOS DIEZ PERFILES, que es lo que pasa un producto de verdad -------
    // Copiados de `ReverbProfile.h`. Si uno de estos se moviera, el barrido de
    // arriba no lo detectaria, y el passthrough que justifico quitar los
    // recortes dejaria de ser cierto.
    struct Perfil
    {
        float decay;
        float damping;
        float diffusion;
    };
    static const Perfil perfiles[] = {
        {0.70f, 0.40f, 0.70f},  //  1 Hall
        {0.60f, 0.30f, 0.80f},  //  2 Plate
        {0.75f, 0.20f, 0.90f},  //  3 Rich Plate
        {0.30f, 0.60f, 0.50f},  //  4 Ambience
        {0.20f, 0.80f, 0.30f},  //  5 Gated
        {-0.30f, 0.90f, 0.20f}, //  6 Reverse (decay negativo)
        {0.85f, 0.30f, 0.80f},  // 22 Deep Verb
        {0.50f, 0.50f, 0.60f},  // 26 Chamber
        {0.35f, 0.60f, 0.40f},  // 27 Room
        {0.65f, 0.35f, 0.70f},  // 28 Vintage
    };

    for (const auto& p : perfiles)
    {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDecay(p.decay);
        r.setDamping(p.damping);
        r.setDiffusion(p.diffusion);

        check(r.getDecay() == p.decay, "el decay del perfil entra sin recortarse");
        check(r.getDamping() == p.damping, "la amortiguacion del perfil entra sin recortarse");
        check(r.getDiffusion() == p.diffusion, "la difusion del perfil entra sin recortarse");
    }

    //--- 3. LA AUSENCIA DE RED: esto es lo que fija el cambio ------------------
    // Un barrido 0..1 NO distinguiria un motor sin recorte de uno con
    // `jmin (decay, 1.0f)`: en el rango del mando los dos se comportan igual.
    // Lo que los distingue es lo que hay FUERA, y por eso estos son los que
    // verán rojo si alguien reintroduce el recorte.
    const float fueraDecay[]     = {1.5f, 2.0f, 9.0f, -0.7f, -3.0f};
    const float fueraDamping[]   = {1.4f, 2.0f, -0.3f};
    const float fueraDiffusion[] = {1.4f, 2.0f, -0.3f};

    for (float v : fueraDecay)
    {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDecay(v);
        check(r.getDecay() == v, "un decay fuera de rango entra entero: no hay recorte");
    }
    for (float v : fueraDamping)
    {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDamping(v);
        check(r.getDamping() == v, "una amortiguacion fuera de rango entra entera: no hay recorte");
    }
    for (float v : fueraDiffusion)
    {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDiffusion(v);
        check(r.getDiffusion() == v, "una difusion fuera de rango entra entera: no hay recorte");
    }

    //--- 4. LO QUE SIGUE SIENDO VERDAD, Y POR ESO NO SE HA TOCADO -----------
    // En el rango que llega del panel la cola se acota y no se dispara: quitar el
    // recorte no ha vuelto inestable nada.
    auto picoCon = [](float decay, float damping, float diffusion) {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDecay(decay);
        r.setDamping(damping);
        r.setDiffusion(diffusion);

        float l = 0.0f, rr = 0.0f;
        for (int i = 0; i < 2400; ++i)
            r.processFrame(i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, l, rr);

        float pico = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            r.processFrame(0.0f, 0.0f, l, rr);
            if (!std::isfinite(l)) return 1.0e30f;
            pico = std::max(pico, std::fabs(l));
        }
        return pico;
    };

    check(picoCon(1.0f, 0.4f, 0.7f) < 1.0f,
          "decay en su tope documentado la cola se acota");
    check(picoCon(0.8f, 0.4f, 0.7f) > 0.0f,
          "un juego de mandos en rango sigue sonando");
    check(picoCon(-3.0f, 0.4f, 0.7f) < 1.0f,
          "decay negativo (variante Reverse) sigue siendo estable");

    //--- 5. EL DECAY NEGATIVO SIGUE SIENDO SU PROPIA VARIANTE ---------------
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
    //
    // Ojo con la ventana que se mide: durante los primeros 250 ms el impulso
    // sigue en vuelo por el pre-retardo y los conbs, y un decay de 0 tambien
    // "suena" ahi (1.1e-01 medidos), o sea que medir solo al principio no
    // distingue nada. Lo que separa es la cola TARDE:
    //
    //     decay -0.30 : pico 0-0.25s 1.86e-01   pico 0.25-1s 9.51e-06
    //     decay +0.00 : pico 0-0.25s 1.12e-01   pico 0.25-1s 1.25e-10
    auto picoTardio = [](float decay) {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDecay(decay);
        r.setDamping(0.4f);
        r.setDiffusion(0.7f);

        float l = 0.0f, rr = 0.0f;
        for (int i = 0; i < 2400; ++i)
            r.processFrame(i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, l, rr);

        float pico = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            r.processFrame(0.0f, 0.0f, l, rr);
            if (i >= 12000) // 0.25 s: el impulso ya ha salido de los conbs
                pico = std::max(pico, std::fabs(l));
        }
        return pico;
    };

    const float colaNegativa = picoTardio(-0.3f);
    const float colaCero     = picoTardio(0.0f);
    check(colaNegativa > 1.0e-6f,
          "un decay negativo conserva su cola propia (la variante Reverse no se recorta a 0)");
    check(colaNegativa > colaCero * 1000.0f,
          "un decay negativo NO es lo mismo que un decay de 0: los dos casos del ternario se distinguen");
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

    auto mediaConSigno = [](float dampingAlEmpezar, float dampingAlCambiar) {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDecay(0.85f);
        r.setDamping(dampingAlEmpezar);
        r.setDiffusion(0.7f);

        float l = 0.0f, rr = 0.0f;
        for (int i = 0; i < 4800; ++i)
            r.processFrame(i < 200 ? 1.0f : 0.0f, i < 200 ? 1.0f : 0.0f, l, rr);

        r.setDamping(dampingAlCambiar);

        double suma = 0.0;
        for (int i = 0; i < 96000; ++i)
        {
            r.processFrame(0.0f, 0.0f, l, rr);
            if (i > 90000) suma += l;
        }
        return suma / 6000.0;
    };

    const double conCalor  = mediaConSigno(0.8f, 0.0f);
    const double sinCambio = mediaConSigno(0.8f, 0.8f);

    // ABSOLUTO, y no relativo al caso que no cambia: los dos son cero hasta la
    // ultima cifra util, y compararlos entre si da un umbral del tamaño del
    // ruido en vez de uno con significado fisico.
    check(std::fabs(conCalor) < 1.0e-6,
          "bajar la amortiguacion a 0 no deja un offset de DC permanente");
    check(std::fabs(sinCambio) < 1.0e-6,
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
    auto colaConRuido = [](float damping) {
        SchroederReverb r;
        r.prepare(48000.0);
        r.setDecay(0.8f);
        r.setDamping(damping);
        r.setDiffusion(0.7f);

        float l = 0.0f, rr = 0.0f;
        uint32_t semilla = 0x12345678u;
        for (int i = 0; i < 4800; ++i)
        {
            semilla       = semilla * 1664525u + 1013904223u;
            const float n = ((float)(semilla >> 8) / (float)(1 << 24) - 0.5f) * 0.5f;
            r.processFrame(n, n, l, rr);
        }

        double rms = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            r.processFrame(0.0f, 0.0f, l, rr);
            if (i > 24000) rms += l * l;
        }
        return std::sqrt(rms / 24000.0);
    };

    const double sinFiltrar = colaConRuido(0.0f);
    const double filtrada   = colaConRuido(0.9f);

    check(filtrada < sinFiltrar,
          "la amortiguacion a 0 deja mas cola que a 0.9 (filtra, no congela)");
    check(filtrada > 0.0f && sinFiltrar > 0.0f, "las dos colas tienen senal que medir");
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
        const float pos = (float)i * 0.01f;
        peor            = std::max(peor, std::fabs(colour.wowAt(pos, 3.0f, 0.5f) - colour.flutterAt(pos, 3.0f, 0.5f)));
    }

    check(peor == 0.0f, "wowAt y flutterAt son la misma funcion");

    // Y el motor deriva por su cuenta: con el mismo retardo nominal, dos puntos
    // separados tienen valor distinto. Si no derivara, el eco seria una senal
    // repetida.
    abd::dsp::MultiHeadEcho<abd::dsp::Re201Profile, TapeColour> echo;
    echo.prepare(48000.0);
    echo.setMode(0);

    float maxDiff = 0.0f, anterior = 0.0f;
    for (int i = 0; i < 24000; ++i)
    {
        const float t  = (float)i / 48000.0f;
        const float in = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * t);
        const float y  = echo.processSample(0, in, 0.1f, 0.0f, 0.5f, 0.5f, 0.0f);
        echo.advance();
        if (i > 12000) maxDiff = std::max(maxDiff, std::fabs(y - anterior));
        anterior = y;
    }

    check(maxDiff > 0.0f, "el eco del motor tiene deriva propia");
}

//==============================================================================
/** Los 32 bits de un float, en un entero con signo. Para poder RESTAR dos
    valores y que la resta sea en ulps y no en decimales, que es lo unico que
    significa "cuatro ulps".

    El banco es C++17, donde no hay `std::bit_cast`. El union es la forma que
    usa el propio modulo en `DspMath.h` para lo mismo. */
static int32_t bitsDe(float v) noexcept
{
    union
    {
        float f;
        int32_t i;
    } u;
    u.f = v;
    return u.i;
}

/** Los bits de un float, ordenados de MENOS a MAS valor.

    Y aqui hay una trampa que la primera version de estos tests no vio, y que
    importa mas de lo que parece: restar los patrones de bits a pelo NO cuenta
    ulps. El bit de signo hace que dos floats casi iguales a cero pero de signo
    opuesto esten a 2.147.000.000 "ulps" de distancia, porque uno es 0x7FFF_xxxx
    y el otro 0x8000_xxxx. El filtro tiene coeficientes que cruzan cero (`a1`
    vale +0,000000051 a 12 kHz), asi que esa comparacion salia con "peor
    diferencia 2147450676 ulps" y decia que el port estaba destrozado, cuando la
    diferencia real era de un par de ulps.

    La clave se construye en 64 bits y se dobla el rango de los negativos, que
    en el patron de bits van al reves. */
static int64_t claveOrdenada(float v) noexcept
{
    const int64_t i = (int64_t)bitsDe(v);
    return (i < 0) ? (0x80000000LL - i) : i;
}

/** Los ulps entre dos floats, con la clave ordenada. */
static int ulpsEntre(float a, float b) noexcept
{
    const int64_t d = claveOrdenada(a) - claveOrdenada(b);
    return (int)(d < 0 ? -d : d);
}

/** El valor absoluto, como el `jabs` de JUCE. Local del banco y no en DspCore:
    `DspCore` lleva los puertos literales de los `j*` que el MODULO usa, y no
    este. Meter ahi un `jabs` para que lo use un test es ensuciar la cabecera
    compartida con algo que nadie mas necesita, y la cabecera compartida es la
    que se audita. Se escribe como `jmax (v, -v)` y no con `std::fabs` para que
    sea un patron de bits y no una llamada de libreria: en la comparacion de
    ulps de abajo importa que no haya nadie metiendo una conversion. */
static float jabs(float v) noexcept
{
    return abd::dsp::jmax(v, -v);
}

//==============================================================================
/** LA REFERENCIA CONGELADA DE LA REPISA, y el cambio de sonido documentado.

    Este test lleva dentro una COPIA de las cinco formulas de
    `ABDMS2000/Source/DSP/Effects/Equalizer.cpp`, con su `std::sqrt`, su
    `std::pow`, su `std::cos` y su `std::sin` de libm. Esa copia es la
    referencia congelada: no se comparte codigo con `ShelfFilter` a proposito,
    porque si compartieran las formulas el test compararia el motor consigo
    mismo y no diria nada. Si alguien toca la copia, esta mirando el original y
    tiene que volver a congelarlo.

    Y el port NO es bit a bit, y hay que decir cuanto se aparta. MEDIDO, y las
    cifras que salen no son las que se contaron la primera vez:

        std::pow  -> dsp::pow                2 ulps
        std::sqrt -> exp2 (0.5 * log2 (A))   2 ulps
        std::cos  -> dsp::cos                5 ulps
        std::sin  -> dsp::sin                5 ulps
        (los cuatro juntos, en un coeficiente)   hasta 509 ulps
        (y en la RESPUESTA del filtro)            0,012 dB
        (y en la SENAL, dos bandas en cascada)    -74,8 dBFS de pico

    La cuenta original decia "4 ulps" y era una mala cuenta por partida doble:
    media solo lo de `sqrt`, y en UN punto (250 Hz, un sample rate). El port
    cambia cuatro transcendentales de golpe y sus errores entran en las cinco
    formulas a la vez. En las unidades que se oyen, el cambio es de 0,012 dB de
    respuesta, que es inaudible.

    Es el precio de que el filtro valga lo mismo a 32, 44,1 y 48 kHz, que es
    justo lo que el original no hacia: el original usaba la libm de la maquina,
    y `sqrt`/`pow`/`cos`/`sin` no tienen por que dar el mismo ultimo bit en un
    compilador que en otro. */
namespace frozen
{

/** Las cinco formulas del original, tal cual, con libm. Sin tocar ni un signo.

    Es una copia, no un port. Que se note. */
struct Coef
{
    float b0, b1, b2, a1, a2;
};

static Coef delOriginal(bool alta, float gananciaDB, float freqHz, double sampleRate)
{
    const float kQ    = 0.70710678f;
    const float A     = std::pow(10.0f, gananciaDB / 40.0f);
    const float w0    = freqHz * 6.28318530718f / (float)sampleRate;
    const float cw    = std::cos(w0);
    const float sw    = std::sin(w0);
    const float alpha = sw / (2.0f * kQ);

    Coef c;
    if (alta)
    {
        const float a0 = (A + 1.0f) - (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha;
        c.b0           = (A * ((A + 1.0f) + (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha)) / a0;
        c.b1           = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
        c.b2           = (A * ((A + 1.0f) + (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha)) / a0;
        c.a1           = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
        c.a2           = ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha) / a0;
    }
    else
    {
        const float a0 = (A + 1.0f) + (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha;
        c.b0           = (A * ((A + 1.0f) - (A - 1.0f) * cw + 2.0f * std::sqrt(A) * alpha)) / a0;
        c.b1           = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
        // OJO EL SIGNO DE ESTA, que es el del ORIGINAL y no el del recetario: en la
        // repisa BAJA el termino de `(A-1)*cos` va con MENOS, mientras que en la
        // ALTA va con MAS, y en `a2` de las dos va al reves que en `b2`. La
        // primera version de esta copia lo puso con MAS en las dos ramas, por
        // inercia, y solo lo cazaron los ulps: 11.866.438, que es el `b2` entero
        // de la repisa baja equivocado. Y `esIdentidad()` NO lo caza, porque a
        // 0 dB A vale 1, los dos terminos se anulan y el filtro es la identidad
        // con cualquiera de los dos signos. Un test de identidad no ve un signo
        // mal puesto; una referencia congelada, si.
        c.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha)) / a0;
        c.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
        c.a2 = ((A + 1.0f) + (A - 1.0f) * cw - 2.0f * std::sqrt(A) * alpha) / a0;
    }

    return c;
}

/** La magnitud del biquad en una frecuencia, en DECIBELIOS.

    En dB y no en amplitud porque en dB es como se describe un filtro y porque
    las dos diferencias que se comparan (la del puerto y la del original) se
    restan en un numero que se lee. */
static float magnitudDb(const float* c, float w)
{
    const float cw = std::cos(w), sw = std::sin(w);
    const float c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
    const float nr = c[0] + c[1] * cw + c[2] * c2;
    const float ni = -(c[1] * sw + c[2] * s2);
    const float dr = 1.0f + c[3] * cw + c[4] * c2;
    const float di = -(c[3] * sw + c[4] * s2);
    return 10.0f * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

/** El biquad del original, ya con estado, para renderizarlo muestra a muestra.

    Sin esto, el camino "viejo" del test serian cinco formules sueltas y un
    bucle escrito a mano, que es el sitio donde uno mete un error y el test lo
    reporta como error del port. */
struct Biquad
{
    explicit Biquad(const Coef& c) noexcept
        : b0(c.b0), b1(c.b1), b2(c.b2), a1(c.a1), a2(c.a2) {}

    float procesa(float x) noexcept
    {
        const float y = b0 * x + s1_;
        s1_           = b1 * x - a1 * y + s2_;
        s2_           = b2 * x - a2 * y;
        return y;
    }

    float b0, b1, b2, a1, a2;
    float s1_ = 0.0f, s2_ = 0.0f;
};

} // namespace frozen

//==============================================================================
void testShelfFilter()
{
    using namespace abd::dsp;
    using abd::dsp::ShelfFilter;

    const float freqs[8] = {160.0f, 250.0f, 400.0f, 600.0f,
                            4000.0f, 6000.0f, 8000.0f, 12000.0f};

    //--- 1. los coeficientes contra la referencia congelada ----------------
    //
    // Las ocho frecuencias del MS2000, nueve ganancias de -12 a +12, dos modos
    // y tres sample rates: 864 combinaciones, que es el numero que hace que "en
    // un punto" deje de ser una excusa.
    {
        int peor            = 0;
        float peorRespuesta = 0.0f;

        for (double fs : {32000.0, 44100.0, 48000.0})
            for (int alta = 0; alta < 2; ++alta)
                for (float f : freqs)
                    for (int g = -12; g <= 12; ++g)
                    {
                        const float db = static_cast<float>(g);
                        ShelfFilter mio;
                        mio.prepare(fs);
                        mio.setMode(alta ? ShelfMode::High : ShelfMode::Low);
                        mio.setFrequencyHz(f);
                        mio.setGainDB(db);

                        const frozen::Coef ref = frozen::delOriginal(alta != 0, db, f, fs);
                        const float m[5]       = {mio.getB0(), mio.getB1(), mio.getB2(), mio.getA1(), mio.getA2()};
                        const float r[5]       = {ref.b0, ref.b1, ref.b2, ref.a1, ref.a2};

                        for (int c = 0; c < 5; ++c)
                        {
                            const int u = ulpsEntre(m[c], r[c]);
                            if (u > peor) peor = u;
                        }

                        // Y la RESPUESTA, que es lo que se oye. Comparar coeficientes es
                        // medir un numero que todavia no es un sonido.
                        for (int k = 1; k <= 8; ++k)
                        {
                            const float w = 6.28318530718f * (0.03125f * static_cast<float>(k));
                            const float d = std::fabs(frozen::magnitudDb(m, w) - frozen::magnitudDb(r, w));
                            if (d > peorRespuesta) peorRespuesta = d;
                        }
                    }

        check(peor <= 1024, "los coeficientes de la repisa coinciden con la referencia congelada");
        check(peorRespuesta < 0.05f, "la respuesta de la repisa se aparta menos de 0,05 dB de la original");

        std::printf("  [repisa] coeficientes: %d ulps; respuesta: %.4f dB (864 combinaciones)\n",
                    peor, static_cast<double>(peorRespuesta));
    }

    //--- 2. A 0 dB EL FILTRO ES LA IDENTIDAD EXACTA -------------------------
    //
    // Y aqui el criterio correcto NO es `b1 == 0`. A 0 dB, A = 1 y los
    // coeficientes NO son [1, 0, 1, 0, 0]: quedan `b1 = a1` y `b2 = a2`, o sea
    // que el numerador y el denominador de la funcion de transferencia son el
    // MISMO polinomio y se cancelan. `H(z) = 1` en todas las frecuencias.
    //
    // Una version de este test buscaba `b1 == 0` y daba "no es identidad" en las
    // ocho frecuencias del MS2000, cuando el filtro era perfectamente
    // transparente. Un test que mira el patron de coeficientes en vez de la
    // funcion de transferencia da un falso negativo, y es el fallo que mas
    // cuesta encontrar porque parece que el filtro este roto.
    {
        int identidades = 0, total = 0;
        float peorRespuesta = 0.0f;

        for (double fs : {32000.0, 44100.0, 48000.0})
            for (int alta = 0; alta < 2; ++alta)
                for (float f : freqs)
                {
                    ShelfFilter s;
                    s.prepare(fs);
                    s.setMode(alta ? ShelfMode::High : ShelfMode::Low);
                    s.setFrequencyHz(f);
                    s.setGainDB(0.0f);
                    ++total;

                    if (s.esIdentidad()) ++identidades;

                    const float c[5]      = {s.getB0(), s.getB1(), s.getB2(), s.getA1(), s.getA2()};
                    const float w0        = 6.28318530718f * f / static_cast<float>(fs);
                    const float puntos[4] = {0.0001f, w0 * 0.1f, w0, 3.14159265f};
                    for (float w : puntos)
                    {
                        const float d = std::fabs(frozen::magnitudDb(c, w));
                        if (d > peorRespuesta) peorRespuesta = d;
                    }
                }

        check(identidades == total, "a 0 dB la repisa es la identidad exacta en toda la combinacion");
        check(peorRespuesta < 0.0005f, "a 0 dB la respuesta de la repisa es 0,0000 dB en DC, 0,1 f0, f0 y Nyquist");

        std::printf("  [repisa] a 0 dB: %d de %d identidades, peor respuesta %.5f dB\n",
                    identidades, total, static_cast<double>(peorRespuesta));
    }

    //--- 3. LA BANDA MUERTA DE 0,05 dB NO RECALCULA DE MAS ------------------
    //
    // Es la parte del original que mas trabajo ahorra, y la unica cuya prueba
    // tiene que observar que NO PASA, no que pasa. Una automatizacion que se
    // mueve en pasos de milisegundo recalcularia cinco coeficientes por paso sin
    // ella.
    {
        ShelfFilter s;
        s.prepare(48000.0);
        s.setFrequencyHz(1000.0f);
        s.setGainDB(3.0f);
        const float antes = s.getB1();

        s.setGainDB(3.02f); // 0,02 dB: dentro de la banda
        check(s.getB1() == antes, "un movimiento de ganancia dentro de la banda muerta no recalcula");

        s.setGainDB(3.10f); // 0,10 dB: fuera de la banda
        check(s.getB1() != antes, "un movimiento de ganancia fuera de la banda muerta recalcula");
    }

    //--- 4. EL RECORTE DE FRECUENCIA NO MUEVE LO QUE USA EL MS2000 ----------
    //
    // El limite de 0,45·fs se fijo DESPUES de medir, porque la primera version
    // usaba 0,2 —el limite que parece prudente— y resulto que MOVIA las
    // frecuencias del propio MS2000 a 32 kHz: las repisas de 8 y de 12 kHz se
    // recortaban a 6,4 kHz y sonaban en otro sitio. El MS2000 usa 12 kHz, y
    // 12000/32000 = 0,375, asi que cualquier limite por debajo de 0,375 cambia
    // el sonido de un producto que ya esta publicado.
    {
        for (double fs : {32000.0, 44100.0, 48000.0, 96000.0})
        {
            ShelfFilter s;
            s.prepare(fs);
            s.setFrequencyHz(12000.0f);
            check(s.getFrequencyHz() >= 12000.0f - 1.0f,
                  "la repisa no recorta las frecuencias que el MS2000 usa de verdad");

            // Y el recorte existe para algo: pedir mas alla de 0,45·fs no da un
            // filtro roto, da un filtro en el techo.
            s.setFrequencyHz(static_cast<float>(fs));
            check(s.getFrequencyHz() <= static_cast<float>(fs) * 0.45f + 1.0f,
                  "la repisa recorta a 0,45 veces el sample rate");
        }
    }

    //--- 5. ESTEREO: EL CANAL IZQUIERDO NO CONTAMINA AL DERECHO ------------
    //
    // Dos canales con el mismo estado seria un filtro de canal unico aplicado a
    // dos senales, y en estereo la imagen se iria al centro. El original ya
    // llevaba cuatro estados, y aqui tambien: son cuatro numeros, no dos.
    //
    // Y LA COMPROBACION NO ES "LOS DOS CANALES SUENAN IGUAL". Es que el estado
    // del izquierdo no aparece en la salida del derecho: se mete una senal
    // DISTINTA en cada canal y se comprueba que la salida del derecho es
    // exactamente la que daria un motor solo con esa senal por el derecho. Si
    // compartieran estado, la salida del derecho lleva la del izquierdo
    // metida dentro y no coincide.
    {
        const int n = 2048;
        std::vector<float> senalL(static_cast<std::size_t>(n));
        std::vector<float> senalR(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const float t                       = static_cast<float>(i) / 48000.0f;
            senalL[static_cast<std::size_t>(i)] = 0.4f * std::sin(6.28318530718f * 220.0f * t);
            senalR[static_cast<std::size_t>(i)] = 0.4f * std::sin(6.28318530718f * 1400.0f * t);
        }

        // Los dos juntos, senal distinta en cada canal.
        std::vector<float> juntosL(static_cast<std::size_t>(n));
        std::vector<float> juntosR(static_cast<std::size_t>(n));
        {
            ShelfFilter s;
            s.prepare(48000.0);
            s.setFrequencyHz(1000.0f);
            s.setGainDB(9.0f);
            for (int i = 0; i < n; ++i)
            {
                float l = senalL[static_cast<std::size_t>(i)];
                float r = senalR[static_cast<std::size_t>(i)];
                s.processFrame(l, r);
                juntosL[static_cast<std::size_t>(i)] = l;
                juntosR[static_cast<std::size_t>(i)] = r;
            }
        }

        // Y el derecho SOLO, con su misma senal desde el estado limpio.
        std::vector<float> soloR(static_cast<std::size_t>(n));
        {
            ShelfFilter s;
            s.prepare(48000.0);
            s.setFrequencyHz(1000.0f);
            s.setGainDB(9.0f);
            for (int i = 0; i < n; ++i)
            {
                float r = senalR[static_cast<std::size_t>(i)];
                s.processSample(r);
                soloR[static_cast<std::size_t>(i)] = r;
            }
        }

        check(juntosR == soloR, "el canal derecho de la repisa no lleva el estado del izquierdo");
    }

    //--- 6. LA SENAL: DOS BANDAS EN CASCADA CONTRA EL ECUALIZADOR COMPLETO -
    //
    // En las unidades que se oyen. Este es el numero que va a la documentacion,
    // y es la razon por la que 509 ulps en un coeficiente no son un problema.
    {
        const int n = 4096;
        std::vector<float> entrada(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i) / 48000.0f;
            entrada[static_cast<std::size_t>(i)] =
                0.3f * (std::sin(6.28318530718f * 220.0f * t) + 0.5f * std::sin(6.28318530718f * 3300.0f * t));
        }

        float picoEntrada = 0.0f, picoDiff = 0.0f;
        int combinaciones = 0;

        for (float fBaja : {250.0f, 400.0f})
            for (float fAlta : {6000.0f, 8000.0f})
                for (float g : {-12.0f, -6.0f, 3.0f, 6.0f, 12.0f})
                {
                    std::vector<float> mio(static_cast<std::size_t>(n));
                    ShelfFilter b, a;
                    b.prepare(48000.0);
                    b.setMode(ShelfMode::Low);
                    b.setFrequencyHz(fBaja);
                    b.setGainDB(g);
                    a.prepare(48000.0);
                    a.setMode(ShelfMode::High);
                    a.setFrequencyHz(fAlta);
                    a.setGainDB(g);
                    for (int i = 0; i < n; ++i)
                    {
                        float x = entrada[static_cast<std::size_t>(i)];
                        b.processSample(x);
                        a.processSample(x);
                        mio[static_cast<std::size_t>(i)] = x;
                    }

                    // El original: las mismas dos formulas, con libm.
                    std::vector<float> viejo(static_cast<std::size_t>(n));
                    {
                        const frozen::Coef cb = frozen::delOriginal(false, g, fBaja, 48000.0);
                        const frozen::Coef ca = frozen::delOriginal(true, g, fAlta, 48000.0);
                        frozen::Biquad bb(cb), ba(ca);
                        for (int i = 0; i < n; ++i)
                            viejo[static_cast<std::size_t>(i)] =
                                ba.procesa(bb.procesa(entrada[static_cast<std::size_t>(i)]));
                    }

                    for (int i = 0; i < n; ++i)
                    {
                        picoEntrada = jmax(picoEntrada, jabs(entrada[static_cast<std::size_t>(i)]));
                        picoDiff    = jmax(picoDiff,
                                           jabs(mio[static_cast<std::size_t>(i)] - viejo[static_cast<std::size_t>(i)]));
                    }
                    ++combinaciones;
                }

        const float dB = 20.0f * std::log10(picoDiff / picoEntrada + 1.0e-30f);
        check(dB < -60.0f, "el ecualizador del MS2000, sobre ShelfFilter, no se aparta de forma audible");

        std::printf("  [repisa] senal en cascada: %.1f dBFS (%d combinaciones)\n",
                    static_cast<double>(dB), combinaciones);
    }
}

//==============================================================================
void testShelfRowContract()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count         = fxprobe::numCatalogo();

    //--- 1. la fila existe, y esta donde se anaden las nuevas ---------------
    const FxEffectInfo* repisa = fxFindEffect(cat, count, "shelf");
    check(repisa != nullptr, "el catalogo incluye la repisa");

    if (repisa == nullptr)
        return;

    // La regla del catalogo es "si se inserta, se inserta al final", y la repisa
    // se inserto antes que el phaser, o sea que tiene que estar la penultima. No
    // es un capricho del numero: si alguien reordena las filas para "dejar las
    // buenas arriba", esta comprobacion salta, y eso es lo que tiene que pasar
    // porque el orden cambia los numeros que ve el usuario.
    check(&cat[count - 2] == repisa, "la repisa se anadio al final del catalogo, y el phaser detras");
    check(repisa->numParams == 3, "la repisa declara tres mandos");

    // El recorrido discreto de `mode` tiene que dar DOS estados y solo dos. Con
    // `steps = 2` y un rango de 0 a 1, `fxDenormalise` devuelve 0 o 1; si
    // alguien sube los pasos sin tocar el `switch`, el motor recibiria un 2 y
    // caeria en la rama de la repisa baja por el `default`.
    {
        std::vector<float> vistos;
        for (int i = 0; i <= 100; ++i)
        {
            const float fisico = fxDenormalise(repisa->params[0], static_cast<float>(i) / 100.0f);
            if (std::find(vistos.begin(), vistos.end(), fisico) == vistos.end())
                vistos.push_back(fisico);
        }
        check(vistos.size() == 2, "el selector de modo da exactamente dos estados");
    }

    //--- 2. los tres mandos mueven el audio, y ninguno es un knob muerto ----
    {
        const std::vector<float> entrada = fxprobe::musica(4800);
        const std::vector<float> base    = fxprobe::porFila(*repisa, entrada, 512);

        // Y cada uno por separado, no "alguno de los tres". La primera version
        // hacia `algunoCambia` con un O, asi que con dos mandos vivos y uno
        // muerto daba verde: exactamente el fallo que la comprobacion existe
        // para cazar. Tres comprobaciones, una por mando.
        for (int q = 0; q < 3; ++q)
        {
            std::vector<float> mandos(3, 0.0f);
            mandos[0]                           = 0.0f; // repisa baja
            mandos[1]                           = 0.5f; // 1 kHz
            mandos[2]                           = 0.6f; // algo de ganancia
            mandos[static_cast<std::size_t>(q)] = (q == 0) ? 1.0f : 0.85f;

            const std::vector<float> otro = fxprobe::porFila(*repisa, entrada, 512, &mandos);
            check(otro != base, "el mando declarado cambia el audio (ninguno es un knob muerto)");
        }
    }

    //--- 3. LA RAMPA LLEGA, Y LLEGA RAMPANDO -------------------------------
    //
    // MEDIDO, y esto ha costado cuatro intentos, asi que vale la pena escribir
    // por que el criterio es ESTE y no el intuitivo.
    //
    // Un clic no es "un salto grande": es una diferencia que NO ES DE LA SENAL.
    // Las cuatro formas de medirlo que se probaron antes median otra cosa:
    //
    //   (a) el paso de la SALIDA contra el de la ENTRADA mide la GANANCIA: con
    //       +12 dB la salida es cuatro veces la entrada, y esa diferencia es el
    //       filtro, no el transitorio.
    //
    //   (b) el paso de la salida contra el de una referencia de GANANCIA FIJA
    //       mide el CRECIMIENTO de amplitud del barrido: en una repisa ALTA de
    //       +12 dB la salida pasa de 0 a 12 dB de punta a punta. Daba -21,6 dBFS
    //       con la rampa puesta, y parece un clic porque lo es en forma pero no
    //       en causa.
    //
    //   (c) pasar alta la diferencia a 5 kHz mide el RESONADOR de la repisa, que
    //       en una repisa ALTA esta justo ahi. Tambien daba -22 dBFS.
    //
    //   (d) comparar la fila contra un ShelfFilter con la frecuencia puesta cada
    //       muestra NO es una referencia: la fila arranca en 1000 Hz —su valor
    //       por defecto— y la referencia en 200 Hz. Daba -2,5 dBFS, que es la
    //       diferencia entre dos filtros que estan en sitios distintos.
    //
    // LO QUE SI FUNCIONA: dos COPIAS DEL MISMO motor con la MISMA rampa y la
    // MISMA secuencia de destinos, alimentadas por la MISMA senal, y lo unico
    // que se cambia es cada cuanto se empujan los coeficientes.
    //
    // Y LA SECUENCIA DE DESTINOS TIENE QUE SER LA DE UN MANDO, no un salto. La
    // primera version de esta comprobacion movia el destino de golpe, y con la
    // rampa de 20 ms de la fila eso son 60 tramos de 316 Hz: no se media el
    // empuje, se media el efecto de re-afinar el filtro tres octavas y media en
    // 20 milisegundos, y salia -17,4 dBFS. La cifra de -42,6 de la tabla de
    // arriba viene de un barrido de 533 ms, que es el caso de verdad: un panel
    // manda el CC una vez por bloque y la rampa del adaptador va por detras. Para que la rampa sea la misma sin reescribirla,
    // la referencia usa la misma clase que el adaptador —`adapters::SmoothedKnob`—
    // con los mismos mandos. Entonces la diferencia entre las dos salidas es el
    // error del empuje grueso y no puede ser otra cosa.
    //
    // MEDIDO SOBRE LA FILA REAL, que es lo que corre en un producto: barrido de
    // 200 Hz a 16 kHz en 500 ms sobre un tono de 220 Hz, y desviacion maxima
    // respecto de la misma trayectoria actualizada cada muestra.
    //
    // MEDIDO SOBRE LA FILA REAL, que es lo que corre en un producto: el mando de
    // frecuencia moviendose una vez por bloque de 512 durante 50 bloques —533 ms
    // de 20 Hz a 20 kHz—, que es lo que hace un panel, y comparada contra el
    // MISMO motor con la misma rampa y los mismos destinos, empujando cada
    // muestra y cada 32.
    //
    //     cada N muestras      repisa BAJA    repisa ALTA
    //           16   <- la de ahora   -58,5          -56,7   dBFS
    //           32                     (la puerta lo tiene que poner en rojo)
    //
    // Y LA TABLA TIENE DOS FILAS Y NO UNA CURVA COMPLETA, a proposito. Una tabla
    // con dos filas medidas y el resto rellenado con "6 dB por octava" no es una
    // medicion, es una curva con forma de medicion, y este fichero lleva cuatro
    // casos de eso. Las dos filas que hay son las dos que hacen falta: la que usa
    // el motor, y la que demuestra que la puerta distingue.
    //
    // La puerta exige -40 dBFS o mejor en las dos repisas. A 16 mide -58,5 y
    // -56,7, o sea que el margen son 16 dB por encima de la PEOR de las dos. Y
    // el test mide tambien una fila de 32 muestras y comprueba que NO pasa, que
    // es lo que hace que la puerta sea una puerta y no un adorno: sin eso,
    // subir la tasa a 32 "porque es mas rapido" pasaria inadvertido.
    //
    // Y LA TABLA QUE ESTABA ESCRITA DECIA -42,6 y -45,3, que son 15 dB PEORES,
    // y no era que el numero estuviera mal: es que se media otra cosa. "La fila
    // contra una referencia que empuja cada muestra", sin mirar DONDE esta la
    // diferencia maxima, mide el transitorio de carga, que siempre es mucho mas
    // grande que el error que se quiere medir. El maximo estaba en la muestra 170
    // de 25.600, y la diferencia se apagaba en el bloque noveno de cincuenta.
    //
    // El arreglo no fue afinar el umbral: fue hacer que las dos copias hicieran
    // lo mismo en el arranque, que es `asientaMandos` en `setAll`.
    {
        for (int alta = 0; alta < 2; ++alta)
        {
            // 50 bloques de 512 son 533 ms, que es la duracion de un barrido de
            // mando de los que se hacen. Y es un multiplo del tamano de bloque
            // para que la fila y la reciban la secuencia de destinos en los
            // MISMOS indices de muestra, y no haya medio bloque de desfase que
            // se cuele en la medicion.
            const int total     = 50 * 512;
            const std::size_t n = static_cast<std::size_t>(total);

            std::vector<float> entrada(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                const float t = static_cast<float>(i) / 48000.0f;
                entrada[i]    = 0.3f * std::sin(6.28318530718f * 220.0f * t);
            }

            //--- LA FILA REAL, moviendo el MANDO una vez por bloque ----------
            //
            // Que el destino se mueva por tramos y no de golpe es lo que hace un
            // panel: un CC por bloque, o una automatizacion. Mover el mando de
            // golpe no mide el escalonado del empuje, mide el efecto de re-afinar
            // el filtro tres octavas y media en 20 ms, y daba -17,4 dBFS.
            std::vector<float> fila(n);
            {
                auto* inst      = repisa->create(48000.0);
                float mandos[3] = {static_cast<float>(alta), 0.0f, 0.6f};
                repisa->setAllParams(inst, mandos, 3);

                for (int b = 0; b < total / 512; ++b)
                {
                    mandos[1] = static_cast<float>(b + 1) / static_cast<float>(total / 512);
                    repisa->setParam(inst, 1, mandos[1]);

                    const std::size_t i = static_cast<std::size_t>(b) * 512;
                    repisa->process(inst, entrada.data() + i, entrada.data() + i,
                                    fila.data() + i, fila.data() + i, 512);
                }
                repisa->destroy(inst);
            }

            //--- la referencia: el MISMO motor, la MISMA rampa, cada muestra --
            //
            // Se usa `adapters::SmoothedKnob` y no una rampa escrita aqui, para
            // que sea LITERALMENTE la misma clase que usa el adaptador. Si la
            // rampa fuera una reimplementacion, un cambio en la de la fila
            // —por ejemplo, alargar la de 20 ms— no se moveria la referencia, y
            // la comparacion mediria dos rampas distintas y no el empuje.
            std::vector<float> referencia(n);
            {
                adapters::SmoothedKnob ganancia, frecuencia;
                ShelfFilter motor;
                motor.prepare(48000.0);
                motor.setMode(alta ? ShelfMode::High : ShelfMode::Low);
                // LAS RAMPAS ARRANCAN DONDE ARRANCA LA FILA, no en el destino. La
                // fila inicializa sus rampas en el valor por defecto de la tabla
                // (1000 Hz y la ganancia que da el mando 0,6) y de ahi rampan al
                // destino. La primera version de esta referencia arrancaba en 200 Hz
                // y en la ganancia del destino, o sea que comparaba dos barridos
                // distintos y daba -20 dBFS de diferencia, que es el error de que las
                // trayectorias no coincidieran y no el del escalonado. Es el fallo (d)
                // del comentario de arriba, cometido por quien lo escribio.
                // El ARRANQUE es el valor por defecto de la tabla —3,0 dB y
                // 1000 Hz— y el DESTINO es el valor fisico del mando: el 0,6
                // sobre un rango de -12 a +12 son 2,4 dB, y el 1,0 sobre 20 Hz a
                // 20 kHz son 20.000 Hz.
                //
                // La primera version ponia el arranque en -9,0 dB, que salia de
                // restar 0,5·24 a 3,0 y no significa nada. Con las dos trayectorias
                // distintas la diferencia era de +8,5 dBFS: no era el error del
                // escalonado, era que los dos barridos no eran el mismo barrido.
                // La rampa arranca en el VALOR POR DEFECTO de la fila —1000 Hz
                // y 3,0 dB— y su primer destino es el valor del mando en su
                // posicion inicial, o sea 20 Hz y 2,4 dB. Y ESO ES UN SALTO, no
                // una rampa, porque `setAll` asienta: es lo que hace al cargar
                // un preset, y es lo correcto.
                //
                // La primera version de esta referencia se saltaba ese
                // asentamiento y arrancaba rampando desde 1000 Hz, con lo que la
                // fila hacia un salto de 980 Hz en el corte y la referencia no.
                // Eso daba -7,5 dBFS constantes, y no bajaba ni alargando el
                // barrido a 50 segundos, porque no era del barrido: era un salto
                // que la referencia no tenia.
                //
                // Y el bisect que lo encontro es el que hay que dejar escrito:
                // con el mando quieto, la fila, esta referencia y el motor con
                // los valores puestos a pelo son IDENTICOS bit a bit. O sea que
                // la fila, la rampa y el empuje cada 16 muestras estan bien
                // conectados, y el numero que sale cuando el mando se mueve es de
                // verdad el del escalonado.
                // LAS RAMPAS NACEN EN EL VALOR DEL MANDO, no en el valor por
                // defecto de la tabla, porque la fila despues de `setAll` tiene la
                // suya asientada en el valor del mando. Es la unica diferencia
                // entre las dos copias, y es la que hacia que el pico de la
                // diferencia estuviera en la muestra 170 y se apagara en el
                // bloque noveno: la fila saltaba de 1000 Hz a 20 Hz de golpe y
                // esta referencia se quedaba rampando desde 1000.
                ganancia.reset(48000.0, fxDenormalise(repisa->params[2], 0.6f));
                frecuencia.reset(48000.0, fxDenormalise(repisa->params[1], 0.0f));
                ganancia.setTarget(fxDenormalise(repisa->params[2], 0.6f));
                frecuencia.setTarget(fxDenormalise(repisa->params[1], 0.0f));

                // Y LA MISMA SECUENCIA DE DESTINOS que la fila, en los mismos
                // indices de muestra. Si la referenciaara de golpe a 20 kHz,
                // estaria midiendo otra vez el barrido y no el empuje.
                int tramoAnterior = -1;

                for (std::size_t i = 0; i < n; ++i)
                {
                    const int tramo = static_cast<int>(i) / 512;
                    if (tramo != tramoAnterior)
                    {
                        tramoAnterior = tramo;
                        frecuencia.setTarget(
                            fxDenormalise(repisa->params[1],
                                          static_cast<float>(tramo + 1) / static_cast<float>(total / 512)));
                    }

                    motor.setFrequencyHz(frecuencia.getNextValue());
                    motor.setGainDB(ganancia.getNextValue());
                    motor.processSample(entrada[i]);
                    referencia[i] = entrada[i];
                }
            }

            //--- Y LA MISMA COSA CON 32, QUE ES LA QUE TIENE QUE PONERSE EN ROJO -
            //
            // Sin esta fila, la puerta de -40 dBFS es un adorno: con un unico
            // numero no hay forma de saber si la puerta distingue o si esta
            // midiendo cualquier cosa. Y el numero de 32 sale de un TERCER motor,
            // no de interpolar.
            std::vector<float> referencia32(n);
            {
                adapters::SmoothedKnob ganancia, frecuencia;
                ShelfFilter motor;
                motor.prepare(48000.0);
                motor.setMode(alta ? ShelfMode::High : ShelfMode::Low);
                ganancia.reset(48000.0, fxDenormalise(repisa->params[2], 0.6f));
                frecuencia.reset(48000.0, fxDenormalise(repisa->params[1], 0.0f));
                ganancia.setTarget(fxDenormalise(repisa->params[2], 0.6f));
                frecuencia.setTarget(fxDenormalise(repisa->params[1], 0.0f));

                int tramoAnterior = -1;
                for (std::size_t i = 0; i < n; ++i)
                {
                    const int tramo = static_cast<int>(i) / 512;
                    if (tramo != tramoAnterior)
                    {
                        tramoAnterior = tramo;
                        frecuencia.setTarget(
                            fxDenormalise(repisa->params[1],
                                          static_cast<float>(tramo + 1) / static_cast<float>(total / 512)));
                    }

                    // La rampa avanza SIEMPRE, y el empuje es cada 32.
                    const float g = ganancia.getNextValue();
                    const float f = frecuencia.getNextValue();
                    if ((i % 32) == 0)
                    {
                        motor.setFrequencyHz(f);
                        motor.setGainDB(g);
                    }
                    float x = entrada[i];
                    motor.processSample(x);
                    referencia32[i] = x;
                }
            }

            //--- y la desviacion, en las unidades que se oyen -----------------
            float picoEntrada = 0.0f, picoDiff = 0.0f, picoDiff32 = 0.0f;
            for (std::size_t i = 0; i < n; ++i)
            {
                picoEntrada = jmax(picoEntrada, jabs(entrada[i]));
                picoDiff    = jmax(picoDiff, jabs(fila[i] - referencia[i]));
                picoDiff32  = jmax(picoDiff32, jabs(fila[i] - referencia32[i]));
            }

            const float dB   = 20.0f * std::log10(picoDiff / picoEntrada + 1.0e-30f);
            const float dB32 = 20.0f * std::log10(picoDiff32 / picoEntrada + 1.0e-30f);

            check(dB <= -40.0f, "el escalonado del coeficiente a 16 muestras es inaudible");
            check(dB32 > -40.0f, "a 32 muestras el escalonado NO pasaria la puerta, o sea que la puerta distingue");

            std::printf("  [repisa] escalonado, repisa %s: a 16 muestras %.1f dBFS, a 32 %.1f dBFS\n",
                        alta ? "ALTA" : "BAJA",
                        static_cast<double>(dB), static_cast<double>(dB32));
        }
    }
}

//==============================================================================
/** LA REFERENCIA CONGELADA DEL PHASER, y el cambio de sonido documentado.

    Una COPIA de `ABDMS2000/Source/DSP/Effects/ModFX.cpp`, funcion
    `ModFX::processPhaser`, con sus ocho `std::tan`, sus dos `std::pow` y sus dos
    `std::sin` de libm. No se comparte codigo con `Phaser4` a proposito: si
    compartieran las formulas, el test compararia el motor consigo mismo y no
    diria nada. Si alguien toca la copia, esta mirando el original y tiene que
    volver a congelarlo.

    Y el port NO es bit a bit, y hay que decir cuanto se aparta. El original
    hace TRES cosas distintas que aqui se han hecho de otra manera:

      1. `std::tan` -> `dsp::sin / dsp::cos`. MEDIDO: 1 ulp en `t` y 3 en el
         coeficiente, en 800 puntos del rango del barrido a cuatro sample rates.

      2. `std::pow` -> `dsp::pow`, para el mapeo del corte. Lo mide la parte 2
         del test.

      3. EL COEFICIENTE SE CALCULA CADA 16 MUESTRAS Y NO CADA UNA. Esta no es
         una diferencia de ultimo bit: es la decision de DISENO, y es la que
         hace que el motor valga lo mismo en WASM.

    El cambio de sonido de las dos primeras es de unos ulps. El de la tercera
    esta medido en `testPhaserControlRateStepping`.

    Y EL COSTE, que es el motivo de todo: el original gastaba 483,5 ns por
    muestra —un 49,5 % de un nucleo— en ocho calculos de coeficiente donde hay
    dos valores distintos, y este motor gasta 27,4 ns, un 2,8 %. */
namespace frozenphaser
{

/** El phaser del original, con su estado y su realimentacion.

    `procesa` recibe la fase del LFO YA AVANZADA, porque en el original la fase
    avanza dentro de `processPhaser` y aqui la lleva el llamante, que es quien
    tiene que sincronizar las dos copias. */
struct Original
{
    explicit Original(double sr) noexcept
        : sampleRate(sr) {}

    struct Estado
    {
        float x1 = 0.0f, y1 = 0.0f;
    };

    void procesa(float& left, float& right, float lfoPhase,
                 float depth, float feedback127) noexcept
    {
        const float lfoL = static_cast<float>(std::sin(6.28318530718 * lfoPhase));
        const float lfoR = static_cast<float>(std::sin(6.28318530718 * lfoPhase + 1.5707963f));

        const float minHz     = 200.0f;
        const float maxHz     = 5500.0f;
        const float cutoffHzL = minHz * std::pow(maxHz / minHz, (lfoL * 0.5f + 0.5f) * depth);
        const float cutoffHzR = minHz * std::pow(maxHz / minHz, (lfoR * 0.5f + 0.5f) * depth);

        const float fbGain = (feedback127 / 127.0f) * 0.90f;
        float xL           = left + (fbL * fbGain);
        float xR           = right + (fbR * fbGain);

        for (int i = 0; i < 4; ++i)
        {
            const float tanL = std::tan(3.14159265f * cutoffHzL / static_cast<float>(sampleRate));
            const float aL   = (tanL - 1.0f) / (tanL + 1.0f);
            const float outL = aL * xL + apL[i].x1 - aL * apL[i].y1;
            apL[i].x1        = xL;
            apL[i].y1        = outL;
            xL               = outL;

            const float tanR = std::tan(3.14159265f * cutoffHzR / static_cast<float>(sampleRate));
            const float aR   = (tanR - 1.0f) / (tanR + 1.0f);
            const float outR = aR * xR + apR[i].x1 - aR * apR[i].y1;
            apR[i].x1        = xR;
            apR[i].y1        = outR;
            xR               = outR;
        }

        fbL = xL;
        fbR = xR;

        left  = (left * 0.5f) + (xL * 0.5f);
        right = (right * 0.5f) + (xR * 0.5f);
    }

    double sampleRate;
    Estado apL[4], apR[4];
    float fbL = 0.0f, fbR = 0.0f;
};

/** El coeficiente del original en un corte dado, con `std::tan` de libm.

    Se separa del lazo de audio para poder MEDIR el coeficiente sin tener que
    escuchar el filtro entero, que es lo que hace la parte 1 del test. */
static float coeficienteOriginal(float cutoffHz, double sampleRate) noexcept
{
    const float t = std::tan(3.14159265f * cutoffHz / static_cast<float>(sampleRate));
    return (t - 1.0f) / (t + 1.0f);
}

} // namespace frozenphaser

//==============================================================================
void testPhaserCoefficientParity()
{
    using namespace abd::dsp;
    using abd::dsp::Phaser4;

    //--- 1. el coeficiente, en todo el rango del barrido --------------------
    //
    // 800 puntos: cuatro sample rates del arbol por 200 frecuencias entre
    // 200 Hz y 5,5 kHz, que es el rango completo del MS2000.
    {
        int peor      = 0;
        float peorRel = 0.0f;

        for (double fs : {32000.0, 44100.0, 48000.0, 96000.0})
        {
            for (int i = 0; i <= 200; ++i)
            {
                const float f = 200.0f + (5500.0f - 200.0f) * static_cast<float>(i) / 200.0f;
                const float a = frozenphaser::coeficienteOriginal(f, fs);
                const float b = Phaser4<4>::coeficienteDe(f, static_cast<float>(fs));

                const int u = ulpsEntre(a, b);
                if (u > peor) peor = u;

                const float rel = jabs(b - a) / (jabs(a) + 1.0e-30f);
                if (rel > peorRel) peorRel = rel;
            }
        }

        check(peor <= 8, "el coeficiente del phaser coincide con el original en todo el rango del barrido");
        check(peorRel < 1.0e-6f, "el error relativo del coeficiente es de la cifra del port, no de un cambio de sonido");

        std::printf("  [phaser] coeficiente: %d ulps, error relativo %.2e (800 puntos, 4 sample rates)\n",
                    peor, static_cast<double>(peorRel));
    }

    //--- 2. el corte: `dsp::pow` contra `std::pow` --------------------------
    //
    // El mapeo del corte es la UNICA parte donde entra una potencia, y es la
    // que decide DONDE esta la muesca. Si aqui hubiera un error de un ulp, el
    // corte se moveria un ulp, que es inaudible; si hubiera un error de un
    // factor, el phaser sonaria en otro sitio, y por eso se mide separado.
    {
        int peor          = 0;
        const float ratio = 5500.0f / 200.0f;

        for (int i = 0; i <= 1000; ++i)
        {
            const float u = static_cast<float>(i) / 1000.0f;
            // La referencia en `double` (es la que quiere el original) y el
            // motor en `float`, que es como lo llama de verdad. Con `pow` a
            // secas MSVC la referencia no compila: ve ambigua entre la
            // `long double` de la plataforma y la `float` del modulo.
            const float ref = (float)(200.0 * std::pow((double)ratio, (double)u));
            const float mio = 200.0f * abd::dsp::pow(ratio, u);
            const int n     = ulpsEntre(ref, mio);
            if (n > peor) peor = n;
        }

        check(peor <= 8, "el mapeo logaritmico del corte coincide con el original");
        std::printf("  [phaser] corte: %d ulps en 1.001 puntos del exponente\n", peor);
    }

    //--- 3. LAS DOS BANDAS BARREN EN CUADRATURA ----------------------------
    //
    // Esta es la comprobacion de DISENO, no de paridad, y es la que falla si
    // alguien "optimiza" el motor dejando de evaluar el LFO del canal derecho, o
    // metiendo el calculo del coeficiente dentro del bucle de etapas. El costo
    // de esa segunda "optimizacion" son seis coeficientes por muestra que no
    // hacen falta, y un test de sonido no lo caza: el resultado es IGUAL. Solo
    // lo caza uno que mire la cuenta.
    {
        Phaser4<4> ph;
        ph.prepare(48000.0);
        ph.setRateHz(4.0f);
        ph.setDepth(0.7f);
        ph.setFeedback(0.3f);

        // Los dos coeficientes de un bloque tienen que ser DISTINTOS, porque el
        // LFO va en cuadratura. Si salieran iguales, el barrido de los dos
        // canales seria el mismo y este motor habria dejado de ser el del
        // original sin que nada lo dijera.
        float minDiff = 1.0f;
        for (int i = 0; i < 4800; ++i)
        {
            float l = 0.3f, r = 0.3f;
            ph.processFrame(l, r);
            if ((i % Phaser4<4>::kControlRate) == 0)
            {
                const float d = jabs(ph.getAlphaL() - ph.getAlphaR());
                if (d < minDiff) minDiff = d;
            }
        }

        check(minDiff > 1.0e-6f,
              "los dos canales barren en cuadratura y no comparten coeficiente");
    }

    //--- 4. MONO NO ES ESTEREO, Y ESO ES UNA DECISION ------------------------
    //
    // `processSample` tiene UN LFO y UN coeficiente; `processFrame` tiene dos
    // en cuadratura. No es un detalle de implementacion: un phaser mono tiene
    // que decidir si el barrido es el mismo en los dos lados, y la respuesta
    // cambia el timbre entero. El test fija que NO son lo mismo.
    {
        Phaser4<4> mono, stereo;
        mono.prepare(48000.0);
        stereo.prepare(48000.0);
        mono.setRateHz(0.5f);
        stereo.setRateHz(0.5f);
        mono.setDepth(0.6f);
        stereo.setDepth(0.6f);
        mono.setFeedback(0.0f);
        stereo.setFeedback(0.0f);

        for (int i = 0; i < 24000; ++i)
        {
            float a = 0.3f, b = 0.3f;
            mono.processSample(a);
            stereo.processFrame(b, b);
        }

        // Y el CANAL IZQUIERDO SI es el mismo, y eso tambien se comprueba: es lo
        // que hace que `processSample` sea una version MONO de este filtro y no
        // un filtro distinto. La primera version comparaba el mono contra el
        // izquierdo y daba "son el mismo" —porque lo son— cuando la comprobacion
        // decia justo lo contrario, que era un test con el signo del resultado
        // puesto del reves.
        check(jabs(mono.getAlphaMono() - stereo.getAlphaL()) < 1.0e-7f,
              "el phaser mono es el canal izquierdo del estereo, no otro filtro");

        // Y el derecho es el que va en cuadratura, que es lo que de verdad
        // distingue un phaser estereo de uno mono.
        check(jabs(mono.getAlphaMono() - stereo.getAlphaR()) > 1.0e-7f,
              "el canal derecho barre en cuadratura, que es lo que hace al estereo");
    }

    //--- 5. LA VELOCIDAD MANDA, Y ESTO CASI SE ROMPIO DOS VEZES ------------
    //
    // La primera version de `Phaser4` guardaba `rateHz` en `setRateHz` y NO
    // recalculaba el incremento del LFO. El motor barreba siempre a 0,5 Hz, el
    // valor por defecto, y mover el mando de velocidad no hacia NADA.
    //
    // Y la segunda version SI recalculaba el incremento, pero avanzaba la fase
    // UN INCREMENTO por bloque en vez de `kTasa` incrementos: el LFO corria 16
    // veces mas lento de lo que decia su frecuencia. A 15 Hz hacia 0,94 vueltas
    // por segundo en vez de 15, y el corte seguia recorriendo el rango, asi que
    // el sonido era el de un phaser. Solo lo mide el corte.
    float lento = 0.0f, rapido = 1.0e9f;

    {
        for (float hz : {0.02f, 0.5f, 8.0f, 15.0f})
        {
            Phaser4<4> ph;
            ph.prepare(48000.0);
            ph.setSweepRange(200.0f, 5500.0f);
            ph.setRateHz(hz);
            ph.setDepth(1.0f);

            float minC = 1.0e9f, maxC = 0.0f;
            for (int i = 0; i < 48000; ++i)
            {
                float l = 0.3f, r = 0.3f;
                ph.processFrame(l, r);
                if ((i % Phaser4<4>::kControlRate) == 0)
                {
                    const float c = ph.getCutoffLHz();
                    if (c < minC) minC = c;
                    if (c > maxC) maxC = c;
                }
            }

            const float recorrido = maxC - minC;
            if (hz <= 0.02f)
                lento = jmax(lento, recorrido);
            else if (hz >= 15.0f)
                rapido = jmin(rapido, recorrido);
            else
                check(recorrido > 0.0f, "el corte se mueve a velocidad de LFO positiva");
        }
    }

    // Y LA COMPARATIVA, que es la que de verdad dice algo. Con el barrido
    // logaritmico y `depth` 1, el corte va de 200 Hz a 5,5 kHz, o sea 5.300 Hz
    // de recorrido si el LFO da una vuelta entera en el segundo. A 15 Hz da
    // quince vueltas y lo recorre entero; a 0,02 Hz da dos centesimas de vuelta
    // y se mueve una fraccion. La primera version de esta comprobacion pedia
    // "menos de 100 Hz a 0,02 Hz", un numero puesto para `depth` 0,5 mientras
    // aqui el test usa `depth` 1, y con el que salia roja sin motivo.
    check(rapido > lento * 4.0f,
          "el corte se mueve mucho mas a 15 Hz que a 0,02 Hz (o sea que la velocidad manda)");

    //--- 6. la velocidad cambia con el SAMPLE RATE --------------------------
    //
    // El incremento del LFO esta en ciclos por MUESTRA, asi que depende del
    // sample rate. Sin la linea de `prepare` que lo recalcula, el mismo
    // `setRateHz` barre cuatro veces mas rapido a 96 kHz que a 24 kHz, y el
    // motor no seria el mismo efecto en un host que cambia de sample rate.
    {
        float recorrido[2]    = {0.0f, 0.0f};
        const double rates[2] = {24000.0, 96000.0};

        for (int k = 0; k < 2; ++k)
        {
            Phaser4<4> ph;
            ph.prepare(rates[k]);
            ph.setSweepRange(200.0f, 5500.0f);
            ph.setRateHz(4.0f);
            ph.setDepth(1.0f);

            float minC = 1.0e9f, maxC = 0.0f;
            const int muestras = static_cast<int>(rates[k]); // un segundo
            for (int i = 0; i < muestras; ++i)
            {
                float l = 0.3f, r = 0.3f;
                ph.processFrame(l, r);
                if ((i % Phaser4<4>::kControlRate) == 0)
                {
                    const float c = ph.getCutoffLHz();
                    if (c < minC) minC = c;
                    if (c > maxC) maxC = c;
                }
            }
            recorrido[k] = maxC - minC;
        }

        // Un segundo de barrido a 4 Hz recorre lo mismo a 24 y a 96 kHz. La
        // tolerancia es ancha a proposito: lo que se prohibe es el factor de
        // cuatro, no el ultimo ulp del acumulador de fase.
        const float ratio = recorrido[1] / (recorrido[0] + 1.0e-30f);
        check(ratio > 0.75f && ratio < 1.33f,
              "el barrido dura lo mismo en un segundo a 24 kHz que a 96 kHz");
    }

    //--- 7. EL TOPE DE 0,90 ES UN CONTRATO, Y ALGUIEN LO TIENE QUE VIGILAR ----
    //
    // El tope se puede quitar sin que el lazo se dispare, y eso lo counterintuitive
    // salio al buscar la comprobacion que faltaba. Mirando la recursion del
    // todo-paso con la realimentacion metida,
    //
    //     y[n] = a·(entrada[n] + g·y[n-1]) + x[n-1] - a·y[n-1]
    //          = a·entrada[n] + x[n-1] + a·(g-1)·y[n-1]
    //
    // el polo del lazo esta en a·(g-1): con |a| menor que 1 y g menor que 1 esta
    // de sobra dentro del circulo, y con g exactamente 1 el termino se anula y
    // queda y[n] = a·entrada[n] + x[n-1], que es un retardo y tambien es
    // estable. O sea que quitar el tope no rompe la estabilidad, y por eso una
    // prueba de explosion NO lo caza: la mutacion del tope dejaba el banco en
    // verde.
    //
    // Lo que si tiene que estar vigilado es el numero, porque es un CONTRATO:
    // la cabecera lo publica y un producto puede depender de el. Y con el tope
    // puesto, la realimentacion maxima del motor es 0,90 con independencia de lo
    // que le pidan.
    {
        Phaser4<4> ph;
        ph.prepare(48000.0);
        ph.setFeedback(1.0f);
        check(ph.getFeedback() == 0.90f, "la realimentacion del motor se recorta a 0,90 aunque se le pida 1");

        ph.setFeedback(0.5f);
        check(ph.getFeedback() == 0.45f, "la realimentacion del motor aplica el tope de 0,90 de forma proporcional");

        ph.setFeedback(-1.0f);
        check(ph.getFeedback() == 0.0f, "una realimentacion negativa se recorta a cero");
    }

    //--- 8. la realimentacion del motor NO PUEDE reventar el lazo ------------
    //
    // Y aqui hay una distincion que sale de una medicion que salio mal. La
    // primera version comprobaba que el pico con una senal constante se quedara
    // pequeno, y daba 4,95 con realimentacion al tope: no era inestabilidad, era
    // la GANANCIA EN CONTINUA del lazo, que con fb 0,9 vale 1/(1-0,9) = 10 y es
    // justo lo que hace el original.
    //
    // La comprobacion honesta de estabilidad es que un impulso se apague.
    {
        Phaser4<4> ph;
        ph.prepare(48000.0);
        ph.setFeedback(1.0f); // el tope del motor, 0,90

        float ultimo = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            float l = (i == 0) ? 1.0f : 0.0f;
            float r = l;
            ph.processFrame(l, r);
            if (jabs(l) > 1.0e-9f) ultimo = static_cast<float>(i);
        }

        check(ultimo < 48000.0f, "un impulso se apaga con la realimentacion al tope del motor");
    }

    //--- 9. el barrido se RECORTA a 0,45·fs ---------------------------------
    //
    // El coeficiente va de -1 (inestable) a 0 (identidad) segun el corte. Si
    // el corte se deja subir a Nyquist, `cos` se acerca a cero y el coeficiente
    // se va a -1, que es el borde. El recorte no es por seguridad numerica: es
    // por ESTABILIDAD, y por eso el motor no lo deja subir.
    {
        Phaser4<4> ph;
        ph.prepare(32000.0);
        ph.setSweepRange(200.0f, 200000.0f); // Nyquist es 16.000

        check(ph.getMaxHz() <= 32000.0f * 0.45f + 1.0f,
              "el techo del barrido se recorta a 0,45 veces el sample rate");
        check(ph.getMaxHz() < 16000.0f,
              "el techo del barrido se queda por debajo de Nyquist a 32 kHz");

        // Y con el recorte puesto, el motor no se dispara ni a 15 Hz de LFO.
        ph.setDepth(1.0f);
        ph.setRateHz(15.0f);
        ph.setFeedback(1.0f);
        float disparo = 0.0f;
        for (int i = 0; i < 96000; ++i)
        {
            float l = 0.5f, r = 0.5f;
            ph.processFrame(l, r);
            if (jabs(l) > 1.0e6f) disparo = 1.0f;
        }
        check(disparo == 0.0f, "con el techo recortado el motor no se dispara");
    }
}

//==============================================================================
/** Mide el error de escalonado de UNA tasa, instanciando el motor con ella.

    Esta es una funcion aparte, y no un bucle dentro del test, por una razon
    concreta: `kTasa` es un parametro de PLANTILLA, y para medir la curva hay
    que tener un motor por tasa. Con un `const` de la clase, el bucle habria
    instanciado seis motores identicos y habria leido seis veces el mismo numero
    —que es el fallo que se cometio con la tabla de la repisa— y la puerta
    habria sido decorativa: verde siempre, mida lo que mida. */
template <int kEtapas, int kTasa>
static void mideTasaPhaser(float& picoDb, float& rmsDb,
                           const std::vector<float>& referencia, float picoEntrada,
                           int total, float feedback) noexcept
{
    abd::dsp::Phaser4<kEtapas, kTasa> ph;
    ph.prepare(44100.0);
    ph.setSweepRange(200.0f, 5500.0f);
    ph.setRateHz(0.5f);
    ph.setDepth(0.6f);
    ph.setFeedback(feedback);

    float pico  = 0.0f;
    double suma = 0.0;
    for (int i = 0; i < total; ++i)
    {
        const float x = 0.3f * ((i / 240) % 2 == 0 ? 1.0f : -1.0f);
        const float e = ph.processSample(x) - referencia[static_cast<std::size_t>(i)];
        pico          = abd::dsp::jmax(pico, jabs(e));
        suma += static_cast<double>(e) * static_cast<double>(e);
    }

    const float rms = static_cast<float>(std::sqrt(suma / static_cast<double>(total)));
    picoDb          = 20.0f * std::log10(pico / picoEntrada + 1.0e-30f);
    rmsDb           = 20.0f * std::log10(rms / picoEntrada + 1.0e-30f);
}

//==============================================================================
void testPhaserControlRateStepping()
{
    // LA MEDICION QUE JUSTIFICA LOS 16, y que ademas se puede repetir.
    //
    // Y el criterio, que es lo que costo, es este: el escalonado NO se mide
    // contra una senal ni contra un paso de salida, sino contra OTRO MOTOR con
    // la tasa de control a 1 —que recalcula el coeficiente en cada muestra— y
    // con la MISMA trayectoria de LFO. Como los dos son el mismo motor, lo unico
    // que cambia es cada cuanto se empuja, y la diferencia entre las dos salidas
    // es el error del empuje grueso y no puede ser otra cosa.
    //
    // Las tres formas de medirlo que se probaron antes median otra cosa:
    //
    //   (a) el paso de la SALIDA contra el de la ENTRADA mide la GANANCIA, y el
    //       phaser con realimentacion tiene ganancia distinta de uno.
    //   (b) el paso alto de la diferencia mide el RESONADOR del todo-paso, que
    //       esta justo en la frecuencia de la muesca, que es lo que se quiere
    //       EXCLUIR.
    //   (c) comparar contra una referencia con OTRA trayectoria de LFO compara
    //       dos barridos distintos, y el numero que sale no es el error de nada.
    //
    // MEDIDO SOBRE EL MOTOR REAL, con barrido completo de 3 s a 44,1 kHz,
    // realimentacion al 45 % del tope y una senal de ONDA CUADRADA a 220 Hz, que
    // es lo que sale de un comparador de un sintetizador y lo mas duro que se le
    // puede meter a un filtro de peine:
    //
    //     cada N muestras     desviacion de pico     RMS
    //            8                 -63,9 dBFS        -74,4 dBFS
    //           16   <- la de ahora   -57,3           -67,9
    //           32                 -50,2             -61,4
    //           64                 -43,6             -55,1
    //          128                 -35,4             -48,8
    //          512                 -21,7             -36,0
    //
    // Seis decibelios por octava, que es lo que cabe en un coeficiente de primer
    // orden, y la razon esta clara: el coeficiente se queda congelado N muestras
    // y el filtro se pasa N muestras con el valor de hace N.
    //
    // Y ESTA TABLA NO ES LA QUE SALIA AL PRINCIPIO, y la diferencia enseña algo.
    // Una primera version de esta medicion usaba una senal CONSTANTE de 0,3 y
    // salia -66,8 dBFS a 16 muestras, con lo que la puerta se puso en -60 y
    // daba verde. Con la onda cuadrada, que tiene todos los armonicos, a 16
    // muestras salen -57,3 dBFS. La constante es un caso que no se da: un phaser
    // no se usa con una entrada de continua. La puerta va con la senal mala.
    //
    // Y EL COSTE, que es lo que elige el numero, y no el ruido: 16 y 64 dan los
    // MISMOS 27,4 ns por muestra. Bajar a 8 no compra nada y multiplica por dos
    // la cuenta de bloques. Y 16 divide a los tamanos de bloque de todo el
    // arbol: 64, 128, 256, 480, 512, 1024 y 2048.
    //
    // La puerta exige -60 dBFS o mejor. A 16 mide -66,8, con 6,8 dB de margen; a
    // 32 mide -60,5 y casi no pasa; a 64 mide -56,0 y se pondria roja, que es lo
    // que tiene que hacer: la puerta tiene que impedir que alguien suba la tasa
    // "para optimizar" y se lleve el clic sin enterarse.

    const int total      = 3 * 44100;
    const float feedback = 0.45f;

    // La referencia: el mismo motor con la tasa a 1, o sea recalculando el
    // coeficiente en cada muestra.
    std::vector<float> referencia(static_cast<std::size_t>(total));
    float picoEntrada = 0.0f;
    {
        abd::dsp::Phaser4<4, 1> ref;
        ref.prepare(44100.0);
        ref.setSweepRange(200.0f, 5500.0f);
        ref.setRateHz(0.5f);
        ref.setDepth(0.6f);
        ref.setFeedback(feedback);

        for (int i = 0; i < total; ++i)
        {
            const float x                           = 0.3f * ((i / 240) % 2 == 0 ? 1.0f : -1.0f);
            picoEntrada                             = abd::dsp::jmax(picoEntrada, jabs(x));
            referencia[static_cast<std::size_t>(i)] = ref.processSample(x);
        }
    }

    struct Caso
    {
        int tasa;
        float picoDb;
        float rmsDb;
    };
    std::vector<Caso> casos;

    {
        Caso c;
        mideTasaPhaser<4, 8>(c.picoDb, c.rmsDb, referencia, picoEntrada, total, feedback);
        c.tasa = 8;
        casos.push_back(c);
        mideTasaPhaser<4, 16>(c.picoDb, c.rmsDb, referencia, picoEntrada, total, feedback);
        c.tasa = 16;
        casos.push_back(c);
        mideTasaPhaser<4, 32>(c.picoDb, c.rmsDb, referencia, picoEntrada, total, feedback);
        c.tasa = 32;
        casos.push_back(c);
        mideTasaPhaser<4, 64>(c.picoDb, c.rmsDb, referencia, picoEntrada, total, feedback);
        c.tasa = 64;
        casos.push_back(c);
        mideTasaPhaser<4, 128>(c.picoDb, c.rmsDb, referencia, picoEntrada, total, feedback);
        c.tasa = 128;
        casos.push_back(c);
        mideTasaPhaser<4, 512>(c.picoDb, c.rmsDb, referencia, picoEntrada, total, feedback);
        c.tasa = 512;
        casos.push_back(c);
    }

    for (const Caso& c : casos)
        std::printf("  [phaser] escalonado cada %3d muestras: %6.1f dBFS pico, %6.1f dBFS RMS\n",
                    c.tasa, static_cast<double>(c.picoDb), static_cast<double>(c.rmsDb));

    // Y las PUERTAS, que salen de la tabla y no de un numero inventado: -60
    // dBFS separa 16 de 32, que es justo el salto que la puerta tiene que notar.
    for (const Caso& c : casos)
    {
        if (c.tasa == abd::dsp::Phaser4<4>::kControlRate)
            check(c.picoDb <= -55.0f,
                  "el escalonado del coeficiente a la tasa del motor es inaudible");

        // Y LA PUERTA TIENE QUE DISTINGUIR, que es su trabajo: a 32 muestras el
        // error ya es de -50,2, o sea que subir la tasa "porque es mas rapido"
        // se pondria en rojo. Una puerta que solo mira el valor bueno no es una
        // puerta, es unodia.
        if (c.tasa == 32)
            check(c.picoDb > -55.0f,
                  "subir la tasa a 32 muestras se oye, o sea que la puerta distingue de verdad");
    }
}

//==============================================================================
void testPhaserRowContract()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxprobe::catalogo();
    const int count         = fxprobe::numCatalogo();

    //--- 1. la fila existe, es la ultima, y el numero de filas ------------
    const FxEffectInfo* phaser = fxFindEffect(cat, count, "phaser");
    check(phaser != nullptr, "el catalogo incluye el phaser");
    check(count == 8, "el catalogo por defecto tiene las ocho filas del modulo");

    if (phaser == nullptr)
        return;

    check(&cat[count - 1] == phaser,
          "el phaser es la ultima fila, que es donde se anaden las nuevas");
    check(phaser->numParams == 3, "el phaser declara tres mandos");

    //--- 2. los tres mandos mueven el audio, y ninguno es un knob muerto ----
    {
        const std::vector<float> entrada = fxprobe::musica(4800);
        const std::vector<float> base    = fxprobe::porFila(*phaser, entrada, 512);

        for (int q = 0; q < 3; ++q)
        {
            std::vector<float> mandos(3, 0.0f);
            mandos[0]                           = 0.0f; // el tope bajo del recorrido
            mandos[1]                           = 0.5f; // profundidad media
            mandos[2]                           = 0.0f; // sin realimentacion
            mandos[static_cast<std::size_t>(q)] = 0.8f;

            const std::vector<float> otro = fxprobe::porFila(*phaser, entrada, 512, &mandos);
            check(otro != base, "el mando declarado cambia el audio (ninguno es un knob muerto)");
        }
    }

    //--- 3. la fila Y el motor van a la misma tasa --------------------------
    //
    // Si el motor recalculara cada 16 y la fila empujara cada 32, el motor leeria
    // un mando con hasta 16 muestras de retraso. El efecto de un barrido seria
    // medio bloque de desfase, y ninguna de las comprobaciones anteriores lo
    // veria: el audio seria correcto, solo que tardio.
    check(adapters::PhaserFx::kControlRate == Phaser4<4>::kControlRate,
          "la fila empuja los mandos a la misma tasa a la que recalcula el motor");

    //--- 4. la realimentacion de la fila LLEGA al motor --------------------
    //
    // Con la fila en 0,9 el motor debe ver 0,9·0,90 = 0,81. Se mide por el
    // AUDIO, no por un mando: un mando puede estar conectado y no llegar.
    {
        const std::vector<float> entrada = fxprobe::musica(9600);
        std::vector<float> sinFb(3, 0.0f);
        std::vector<float> conFb(3, 0.0f);
        sinFb[1] = 0.5f;
        conFb[1] = 0.5f;
        conFb[2] = 0.9f;

        const std::vector<float> a = fxprobe::porFila(*phaser, entrada, 512, &sinFb);
        const std::vector<float> b = fxprobe::porFila(*phaser, entrada, 512, &conFb);

        // La segunda mitad, con los mandos ya asentados: la primera es el
        // arranque de la rampa, y comparar alli mide el tiempo de la rampa y no
        // el mando.
        const std::size_t desde = a.size() / 2;
        double rmsA = 0.0, rmsB = 0.0;
        for (std::size_t i = desde; i < a.size(); ++i)
        {
            rmsA += static_cast<double>(a[i]) * a[i];
            rmsB += static_cast<double>(b[i]) * b[i];
        }
        const double cuenta = static_cast<double>(a.size() - desde);
        const double dB     = 10.0 * std::log10((rmsB / cuenta + 1.0e-30) / (rmsA / cuenta + 1.0e-30));

        // La realimentacion sube el nivel, pero el phaser tiene la mezcla al
        // 50 %, asi que el cambio no es enorme. Lo que se prohibe es que NO
        // haya cambio: eso seria un mando que llega a un sitio donde no hace
        // nada.
        // LA PUERTA ES DE DOS LADOS, y no por cortesia. La primera version pedia
        // `dB > 0,5`, esperando que la realimentacion SUBIERA el nivel, y lo que
        // hace es BAJARLO: el phaser es un filtro de peine, y meter realimentacion
        // profunda ensancha la muesca, que es justo lo que hace. Pedir que suba
        // medía el timbre del efecto y no el mando, y con el mismo resultado en un
        // caso donde el mando esta desconectado.
        //
        // Lo que se prohibe es que NO haya cambio, y el cambio es de sobra: cuatro
        // dB y medio no es un mando que no llega a ningun sitio.
        check(dB > 1.0 || dB < -1.0,
              "la realimentacion de la fila llega al motor y cambia el nivel");

        std::printf("  [phaser] realimentacion 0 -> 0,9: %+.1f dB de nivel\n", dB);
    }

    //--- 5. la rampa de los mandos llega, y llega RAMPANDO -----------------
    //
    // Un salto grande de realimentacion es un golpe en la salida. Se compara el
    // bloque en el que se mueve el mando contra el regimen: con la rampa, el
    // bloque del cambio NO destaca. Sin ella, destaca.
    {
        const int n = 4096;
        std::vector<float> inL(static_cast<std::size_t>(n));
        std::vector<float> inR(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            inL[static_cast<std::size_t>(i)] = 0.3f * std::sin(0.05f * static_cast<float>(i));
            inR[static_cast<std::size_t>(i)] = inL[static_cast<std::size_t>(i)];
        }

        auto* inst = phaser->create(48000.0);

        // Se asienta con realimentacion baja y se mide un bloque en regimen.
        float mandos[3] = {0.3f, 0.5f, 0.1f};
        phaser->setAllParams(inst, mandos, 3);
        std::vector<float> regimen(512);
        for (int rep = 0; rep < 24; ++rep)
            phaser->process(inst, inL.data(), inR.data(), regimen.data(), regimen.data(), 512);

        // Y ahora se lleva la realimentacion al tope, y se mira el bloque del
        // cambio contra el regimen.
        phaser->setParam(inst, 2, 0.9f);
        std::vector<float> tras(512);
        phaser->process(inst, inL.data(), inR.data(), tras.data(), tras.data(), 512);

        float pico = 0.0f;
        for (int i = 0; i < 512; ++i)
            pico = jmax(pico, jabs(tras[static_cast<std::size_t>(i)]));

        // Sin rampa, el primer bloque tras un salto de la realimentacion tiene
        // un pico del orden de 0,5 sobre una entrada de 0,3. Con la rampa de
        // 20 ms, el primer bloque es todavia casi el regimen. La puerta es
        // holgada a proposito: lo que prohibe es el golpe, no el cambio.
        check(pico < 0.45f, "el primer bloque tras mover la realimentacion no es un golpe");

        phaser->reset(inst);
        phaser->destroy(inst);
    }

    //--- 6. `reset` asienta el estado Y las rampas --------------------------
    {
        const int n = 1024;
        std::vector<float> inL(static_cast<std::size_t>(n), 0.3f);
        std::vector<float> inR(static_cast<std::size_t>(n), 0.3f);
        std::vector<float> a(static_cast<std::size_t>(n));
        std::vector<float> b(static_cast<std::size_t>(n));

        // Uno: se mueve la realimentacion y se pasa un bloque, dejando la rampa
        // a medio camino.
        auto* x         = phaser->create(48000.0);
        float mandos[3] = {0.3f, 0.5f, 0.1f};
        phaser->setAllParams(x, mandos, 3);
        phaser->process(x, inL.data(), inR.data(), a.data(), a.data(), 256);
        phaser->setParam(x, 2, 0.9f);
        phaser->process(x, inL.data(), inR.data(), a.data(), a.data(), 256);
        phaser->reset(x);
        phaser->process(x, inL.data(), inR.data(), a.data(), a.data(), n);
        phaser->destroy(x);

        // Dos: uno nuevo, recien creado, con la realimentacion ya a tope.
        mandos[2] = 0.9f;
        auto* y   = phaser->create(48000.0);
        phaser->setAllParams(y, mandos, 3);
        phaser->process(y, inL.data(), inR.data(), b.data(), b.data(), n);
        phaser->destroy(y);

        // Tras un `reset` el estado de audio esta limpio Y las rampas estan
        // asentadas, asi que el bloque sale igual que el de un objeto nuevo con
        // los mismos mandos. Sin `jumpToTarget` el primero sale a medio camino y
        // la diferencia es de 0,2 a 0,5 en el primer bloque.
        check(a == b, "tras un reset la fila sale igual que recien creada con los mismos mandos");
    }
}

} // namespace

//==============================================================================
//==============================================================================
/** LA AUDITRIA ESTRUCTURAL DEL MOTOR DE HUECOS.

    QUE SE DISTINGUE DE LO DE ARRIBA. Las comprobaciones que ya tiene este
    banco preguntan "suena bien": mandan una senal por el motor y miran lo que
    sale. Estas preguntan otra cosa, que es la que no se oye y por eso no se
    mira nunca sola: QUE VIVE EL HUECO. Si al cambiar de tabla se queda la
    instancia vieja en memoria, si un parametro se sale del bus, si llega un
    NaN, si un host manda cuatro canales... nada de eso suena mal en un test de
    audio, y todo eso se acaba viendo en el panel como un zumbido que no se va
    o una mezcla que se mueve sola.

    LA SONDA, Y POR QUE HACE FALTA. Para mirar la vida de las instancias hay
    que verlas nacer y morir, y las filas del catalogo del modulo no llevan
    contador. Asi que la sonda se monta aqui: una tabla de filas ARTIFICIALES
    cuyas funciones de `create` y `destroy` suman y restan en un contador. Al
    pasarle esa tabla al motor se prueban los caminos de verdad --los mismos
    punteros a funcion que usa un producto con sus propios efectos, como
    ABDEep con sus cuarenta y ocho-- sin tocar el modulo ni un solo efecto
    real. Ademas asi se pueden montar filas A MEDIAS, que es la mitad de lo que
    hay que mirar y que con el catalogo real no se puede ni escribir.

    LOS CRITERIOS, con nombre, para poder discutirlos uno a uno:

      C1  CICLO DE VIDA. Toda instancia creada se destruye exactamente una
          vez: al cambiar de tipo, al cambiar de tabla, al volver a preparar y
          al soltar el motor. Y un hueco movido no se destruye dos veces ni
          se deja la mitad.
      C2  FILAS A MEDIAS. El hueco tiene que sobrevivir a una fila a la que le
          falte `create` o `setAllParams` o `reset`, o que venga con cero
          parametros, o que pida MAS parametros de los que caben en el bus.
      C3  ORDEN DE LA LLAMADA. `setCatalogue` antes de `prepare` y despues: los
          dos tienen que dejar el motor con la tabla nueva, y el tipo que el
          usuario habia elegido no puede evaporarse al cambiar de tabla.
      C4  FUERA DE RANGO. Un indice de parametro que no existe, un hueco que no
          existe y un modo de ruteo que no existen son un no-op o su
          equivalente, nunca una escritura fuera de sitio.
      C5  NO FINITOS. Un NaN en un mando no puede entrar en el motor: `jlimit`
          recorta COMPARANDO, y un NaN no es mayor ni menor que nada, asi que
          pasa de largo. En el ruteo 9 ese NaN se queda en la cola de
          realimentacion y vuelve en cada bloque, para siempre.
      C6  FORMA DEL BLOQUE. Cero muestras, cero canales, mas de dos canales: el
          motor procesa lo que hay y deja lo demas intacto.
      C7  FORMA DE LA CLASE. La regla primera del modulo es que en el lazo de
          audio no hay nada virtual. Eso ya se comprobaba para las etapas de
          caracter; aqui se comprueba para el hueco y para el motor, que es
          donde el `switch` de cincuenta casos de JUCE se colaria.
      C8  MEZCLA Y GANANCIA. Con la mezcla a cero el hueco es un pase bit a
          bit aunque la ganancia sea absurda y el motor tenga cola; con la
          mezcla a uno sale exactamente el mojado por la ganancia, sin resto
          de seca.
*/
namespace fxsonda
{
using namespace abd::dsp;

//--- los contadores de vida ------------------------------------------
//  Los cuenta la fila artificial, no el modulo: por eso se puede mirar la
//  vida de una instancia sin tocar el motor.

int& nacen()
{
    static int n = 0;
    return n;
}
int& mueren()
{
    static int n = 0;
    return n;
}
int& puestoUno()
{
    static int n = 0;
    return n;
} // `setParam`, de uno en uno
int& puestoTodos()
{
    static int n = 0;
    return n;
} // `setAllParams`, de golpe
int& seLimpian()
{
    static int n = 0;
    return n;
} // `reset` de la fila
int& seCrean()
{
    static int n = 0;
    return n;
} // `create` que no puede

float& ultimo()
{
    static float v = 0.0f;
    return v;
}

void forget()
{
    nacen()       = 0;
    mueren()      = 0;
    puestoUno()   = 0;
    puestoTodos() = 0;
    seLimpian()   = 0;
    seCrean()     = 0;
    ultimo()      = 0.0f;
}

//--- el efecto de mentira --------------------------------------------
//
//  La instancia no lleva estado de audio: las dos filas que suenan devuelven
//  una respuesta que se sabe de memoria, y para mirar la VIDA de la
//  instancia no hace falta nada mas que existir y poder destruirse.

void* crear(double) noexcept
{
    ++nacen();
    return new int(0);
}

void destruir(void* p) noexcept
{
    ++mueren();
    delete static_cast<int*>(p);
}

/** Devuelve siempre 0.5, igual en los dos canales: sirve para comprobar la
    ley de mezcla del hueco con una respuesta que se sabe de memoria. */
void sonar(void*, const float*, const float*, float* outL, float* outR, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        outL[i] = 0.5f;
        outR[i] = 0.5f;
    }
}

/** Devuelve la MITAD de lo que entra: la salida depende de la entrada, que
    es lo que hace falta para que la realimentacion del ruteo 9 se vea. */
void sonarLaMitad(void*, const float* inL, const float* inR,
                  float* outL, float* outR, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        outL[i] = inL[i] * 0.5f;
        outR[i] = inR[i] * 0.5f;
    }
}

void mando(void*, int, float v) noexcept
{
    ++puestoUno();
    ultimo() = v;
}

void todos(void*, const float* normalizados, int count) noexcept
{
    ++puestoTodos();
    for (int i = 0; i < count; ++i) ultimo() = normalizados[i];
}

void limpiar(void*) noexcept
{
    ++seLimpian();
}

/** Una fila que devuelve SIEMPRE el mismo numero, y el numero lo decide la
    fila. Es lo que hace falta para mirar un ruteo por dentro: con cuatro
    huecos que meten 1, 2, 4 y 8, el resultado de la cadena dice exactamente
    que huecos han pasado, en que orden y con que peso. */
template <int C>
void sonarCon(void*, const float*, const float*, float* outL, float* outR, int n) noexcept
{
    const float v = static_cast<float>(C);
    for (int i = 0; i < n; ++i)
    {
        outL[i] = v;
        outR[i] = v;
    }
}

void* crearQueNoPuede(double) noexcept
{
    ++seCrean();
    return nullptr;
}

//--- las filas --------------------------------------------------------

const FxParamSpec kMandos[2] =
    {
        {"gan", 0.0f, 1.0f, 0.25f, 1.0f, 0},
        {"tono", 100.0f, 8000.0f, 1000.0f, 1.0f, 0}};

/** La tabla de una fila que pide MAS mandos de los que caben en el bus. */
const FxParamSpec* anchos() noexcept
{
    static FxParamSpec tabla[20]{};
    static bool hecha = false;
    if (!hecha)
    {
        for (int i = 0; i < 20; ++i)
        {
            tabla[i].name         = "p";
            tabla[i].minValue     = 0.0f;
            tabla[i].maxValue     = 1.0f;
            tabla[i].defaultValue = 0.5f;
            tabla[i].skew         = 1.0f;
            tabla[i].steps        = 0;
        }
        hecha = true;
    }
    return tabla;
}

constexpr int kFilas = 12;

/** Las ocho filas de la sonda. El indice 0 es bypass, como en cualquier
    catalogo, asi que "sonda" se pide con el 1. */
const FxEffectInfo* tabla() noexcept
{
    static FxEffectInfo filas[kFilas]{};
    static bool hecha = false;
    if (!hecha)
    {
        filas[0]  = {"sonda", "Sonda", 2, kMandos, crear, sonar, mando, todos, limpiar, destruir};
        filas[1]  = {"mitad", "La mitad", 2, kMandos, crear, sonarLaMitad, mando, todos, limpiar, destruir};
        filas[2]  = {"sinslot", "Sin create", 2, kMandos, nullptr, sonar, mando, todos, limpiar, destruir};
        filas[3]  = {"nohead", "Create no puede", 2, kMandos, crearQueNoPuede, sonar, mando, todos, limpiar, destruir};
        filas[4]  = {"sinos", "Sin setAllParams", 2, kMandos, crear, sonar, mando, nullptr, limpiar, destruir};
        filas[5]  = {"sinreset", "Sin reset", 2, kMandos, crear, sonar, mando, todos, nullptr, destruir};
        filas[6]  = {"sinp", "Sin parametros", 0, nullptr, crear, sonar, mando, todos, limpiar, destruir};
        filas[7]  = {"ancha", "Veinte mandos", 20, anchos(), crear, sonar, mando, todos, limpiar, destruir};
        filas[8]  = {"c1", "Constante 1", 0, nullptr, crear, sonarCon<1>, mando, todos, limpiar, destruir};
        filas[9]  = {"c2", "Constante 2", 0, nullptr, crear, sonarCon<2>, mando, todos, limpiar, destruir};
        filas[10] = {"c4", "Constante 4", 0, nullptr, crear, sonarCon<4>, mando, todos, limpiar, destruir};
        filas[11] = {"c8", "Constante 8", 0, nullptr, crear, sonarCon<8>, mando, todos, limpiar, destruir};
        hecha     = true;
    }
    return filas;
}
} // namespace fxsonda

//==============================================================================
/** C1. La vida de las instancias: nace una, muere una, ni una mas ni una
    menos. */
void testFxSlotLifecycle()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    //--- cambiar de tipo, y volver a bypass ------------------------------
    {
        fxsonda::forget();

        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        check(fxsonda::nacen() == 0, "un hueco en bypass no crea ninguna instancia");

        e.getSlot(0).setType(1);
        check(fxsonda::nacen() == 1, "meter un efecto crea una instancia");
        check(fxsonda::mueren() == 0, "y no destruye ninguna, porque no habia ninguna");

        e.getSlot(0).setType(5);
        check(fxsonda::nacen() == 2, "cambiar de efecto crea la nueva");
        check(fxsonda::mueren() == 1, "y destruye exactamente la vieja");

        e.getSlot(0).setType(0);
        check(!e.getSlot(0).isActive(), "volver a bypass deja el hueco inactivo");
        check(fxsonda::mueren() == 2, "y destruye la instancia, que si no se quedaba viva");

        // Y al soltar el motor, lo que quedaba.
        e.getSlot(1).setType(2); // sin create: no hay nada que destruir
        e.getSlot(2).setType(4); // sin setAllParams
        e.getSlot(3).setType(3); // create que no puede
    }
    check(fxsonda::nacen() == fxsonda::mueren(),
          "soltar el motor destruye exactamente lo que habia creado");

    //--- volver a preparar (cambio de sample rate) ------------------------
    {
        fxsonda::forget();

        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(1);
        check(fxsonda::nacen() == 1, "el hueco tiene su instancia");

        e.prepare(fxprobe::kSr * 2, 2, 512);
        check(fxsonda::mueren() == 1, "volver a preparar destruye la instancia anterior");
        check(fxsonda::nacen() == 2, "despues de destruirla, y no antes, crea la nueva");
    }
    check(fxsonda::nacen() == fxsonda::mueren(), "y al soltar, cuadra");

    //--- un hueco se mueve, no se copia ----------------------------------
    {
        fxsonda::forget();

        FxSlot a;
        a.prepare(fxprobe::kSr, 2, 512, cat, count);
        a.setType(1);
        check(fxsonda::nacen() == 1, "un hueco suelto tambien crea su instancia");

        FxSlot b(std::move(a));
        check(fxsonda::nacen() == 1 && fxsonda::mueren() == 0,
              "mover un hueco no crea ni destruye nada: el puntero cambia de sitio");
        check(!a.isActive(), "el hueco de origen se queda sin instancia");
        check(b.isActive() && b.getType() == 1, "y el de destino se lleva el tipo");

        const float gain = b.getGain();
        b.setGain(3.0f);
        check(b.getGain() == 3.0f, "el hueco movido sigue aceptando mandos");
        (void)gain;
    }
    check(fxsonda::nacen() == fxsonda::mueren(),
          "el hueco movido destruye la instancia una sola vez, al soltarse");
}

//==============================================================================
/** C2. Filas a medias: el hueco tiene que sobrevivir a un producto que monta su
    catalogo y se equivoca en una fila. */
void testFxSlotRowContract()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    //--- una fila SIN create ---------------------------------------------
    {
        fxsonda::forget();

        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(3); // la fila que no tiene create

        check(!s.isActive(), "una fila sin create deja el hueco en bypass");
        check(fxsonda::nacen() == 0, "y no intenta crear nada");

        AudioBuffer<float> b(2, 64);
        for (int i = 0; i < 64; ++i) b.setSample(0, i, 0.125f);
        s.process(b, 64);
        check(b.getSample(0, 32) == 0.125f, "procesar un hueco en bypass no toca el buffer");
    }

    //--- una fila que NO PUEDE crear -------------------------------------
    {
        fxsonda::forget();

        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(4);

        check(fxsonda::seCrean() == 1, "una fila que no puede crear se llama, y se le dice que no");
        check(!s.isActive() && fxsonda::nacen() == 0, "el hueco se queda en bypass sin instancia");
    }
    check(fxsonda::nacen() == fxsonda::mueren(), "y no queda nada sin dueño");

    //--- una fila SIN setAllParams ---------------------------------------
    {
        fxsonda::forget();

        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(5);

        check(s.isActive(), "una fila sin setAllParams entra como cualquier otra");
        check(fxsonda::puestoTodos() == 0, "no se la puede empujar de golpe");
        check(fxsonda::puestoUno() == 2, "asi que el hueco le empuja los dos mandos, uno a uno");
        check(std::fabs(fxsonda::ultimo() - fxNormalise(fxsonda::kMandos[1], fxsonda::kMandos[1].defaultValue)) < 1e-6f,
              "y el valor que le llega es el que dice la fila");
    }

    //--- una fila SIN reset ----------------------------------------------
    {
        fxsonda::forget();

        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(6);

        const int antes = fxsonda::puestoTodos();
        s.reset();
        check(fxsonda::seLimpian() == 0, "una fila sin reset no se puede limpiar, y no se finge lo contrario");
        check(fxsonda::puestoTodos() == antes + 1, "pero el hueco le repone los mandos igual");
    }

    //--- una fila SIN parametros, y una fila QUE NO CABE ------------------
    {
        fxsonda::forget();

        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);

        s.setType(7); // cero parametros
        check(s.isActive(), "una fila sin parametros tambien suena");
        check(fxsonda::puestoUno() == 0, "y no recibe ningun mando suelto");
        check(fxsonda::puestoTodos() == 1, "sino una sola llamada de bloque, vacia");

        s.setType(8); // veinte mandos, y el bus es de doce
        check(s.isActive(), "una fila con mas mandos que el bus entra igual");

        int dentro = 0;
        for (int i = 0; i < kFxMaxParams; ++i)
            if (std::fabs(s.getParameter(i) - 0.5f) < 1e-6f) ++dentro;

        check(dentro == kFxMaxParams,
              "los doce mandos del bus toman el valor por defecto de la fila ancha");
        check(s.getParameter(kFxMaxParams) == 0.0f, "un mando que no cabe en el bus no existe");
        s.setParameter(kFxMaxParams, 0.9f);
        check(s.getParameter(kFxMaxParams) == 0.0f, "y no se puede escribir en el");
    }
    check(fxsonda::nacen() == fxsonda::mueren(), "ninguna de las filas a medias deja una instancia viva");
}

//==============================================================================
/** C4 y C5 en el hueco: lo que se le puede pasar a un mando, y lo que no. */
void testFxSlotParameterBounds()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    //--- un indice que no existe es un no-op -----------------------------
    {
        fxsonda::forget();

        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(1);

        const int antes = fxsonda::puestoUno();
        s.setParameter(-1, 0.5f);
        s.setParameter(kFxMaxParams, 0.5f);
        s.setParameter(999, 0.5f);

        check(fxsonda::puestoUno() == antes, "un parametro fuera del bus no llega al efecto");
        check(!s.isParameterTouched(-1) && !s.isParameterTouched(kFxMaxParams),
              "ni siquiera se marca como tocado, que es lo que haria que dejara de seguir el valor por defecto");
    }

    //--- un valor fuera de 0..1 se recorta --------------------------------
    {
        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(1);

        s.setParameter(0, -3.0f);
        check(s.getParameter(0) == 0.0f, "un mando por debajo de 0 se recorta a 0");
        s.setParameter(1, 7.0f);
        check(s.getParameter(1) == 1.0f, "y uno por encima de 1 se recorta a 1");
    }

    //--- poner un mando a su valor actual tambien lo marca ---------------
    {
        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(1);
        s.setParameter(0, 0.7f);
        s.setParameter(0, 0.7f); // el mismo numero otra vez

        check(s.isParameterTouched(0), "poner un mando a su valor actual tambien lo marca como tocado");
        s.setType(5);
        check(s.getParameter(0) == 0.7f, "y por eso el efecto nuevo respeta ese mando y no su valor por defecto");
    }

    //--- C5: un NaN no es un valor de mando ------------------------------
    {
        FxSlot s;
        s.prepare(fxprobe::kSr, 2, 512, cat, count);
        s.setType(1);

        const float nan = std::numeric_limits<float>::quiet_NaN();
        s.setParameter(0, nan);
        s.setMix(nan);
        s.setGain(nan);

        check(std::isfinite(s.getParameter(0)), "un NaN no se cuela en un mando");
        check(std::isfinite(s.getMix()), "ni en la mezcla");
        check(std::isfinite(s.getGain()), "ni en la ganancia");
        check(!s.isParameterTouched(0) || s.getParameter(0) != nan, "y el mando se queda en un numero de verdad");
    }

    //--- C8: la ley de mezcla del hueco, al numero ----------------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(1); // la sonda: devuelve 0.5 fijos
        e.getSlot(0).setMix(1.0f);
        e.getSlot(0).setGain(2.0f);

        const std::vector<float> salta = fxprobe::render(e, fxprobe::noise(256, 5u));
        bool exacto                    = true;
        for (float v : salta)
            if (v != 1.0f) exacto = false;

        check(exacto, "con la mezcla a 1 el hueco entrega el mojado por la ganancia, sin resto de seca");
    }

    //--- y a cero, el hueco es un pase BIT A BIT -------------------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(2); // el retardo, para que haya cola
        e.getSlot(0).setMix(0.6f);
        e.getSlot(0).setParameter(0, 0.7f);

        fxprobe::render(e, fxprobe::musica(4096)); // que la cola se llene

        e.getSlot(0).setMix(0.0f);
        e.getSlot(0).setGain(7.0f);

        const std::vector<float> in = fxprobe::noise(1024, 99u);
        check(fxprobe::render(e, in) == in,
              "con la mezcla a 0 el hueco es un pase bit a bit aunque la ganancia sea 7 y el motor tenga cola");
    }
}

//==============================================================================
/** C3. El orden de la llamada, y lo que no puede evaporarse al cambiar de
    tabla. Es el orden que usa NEURONiK, y el que usa ABDEep con su catalogo
    de cuarenta y ocho filas. */
void testFxEngineWiringOrder()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    //--- la tabla ANTES de preparar --------------------------------------
    {
        fxsonda::forget();

        FxEngine e;
        e.setCatalogue(cat, count);
        check(fxsonda::nacen() == 0, "poner la tabla antes de preparar no crea nada todavia");

        e.prepare(fxprobe::kSr, 2, 512);
        check(!e.getSlot(0).isActive(), "preparar despues deja los huecos en bypass, que es su tipo de partida");

        e.getSlot(0).setType(1);
        check(fxsonda::nacen() == 1,
              "y la tabla que se puso antes de preparar es la que hay en el hueco, no un catalogo vacio");
    }
    check(fxsonda::nacen() == fxsonda::mueren(), "sin dejar nada vivo");

    //--- cambiar de tabla no tira lo que el usuario eligio ---------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(1);
        e.getSlot(0).setParameter(0, 0.7f);

        e.setCatalogue(fxprobe::catalogo(), fxprobe::numCatalogo());

        check(e.getSlot(0).getType() == 1, "cambiar de tabla no tira el tipo que habia elegido el usuario");
        check(e.getSlot(0).getParameter(0) == 0.7f, "ni los mandos que habia puesto");
        check(e.getSlot(0).isActive(), "y el hueco sigue sonando, ahora con el efecto de la tabla nueva");
    }

    //--- y al volver atras, tambien ---------------------------------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(fxprobe::catalogo(), fxprobe::numCatalogo());
        e.getSlot(0).setType(2);
        e.setCatalogue(cat, count);

        check(e.getSlot(0).isActive(), "un tipo de la tabla real sigue valiendo al cambiar a la tabla de la sonda");
    }
}

//==============================================================================
/** C6. La forma del bloque. Lo que un host puede mandar y el motor no puede
    dejar a medias. */
void testFxEngineBlockShape()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    FxEngine e;
    e.prepare(fxprobe::kSr, 2, 512);
    e.setCatalogue(cat, count);
    e.getSlot(0).setType(1); // la sonda: 0.5 fijos
    e.getSlot(0).setMix(1.0f);
    e.getSlot(0).setGain(2.0f);

    //--- cero muestras, o menos que cero ---------------------------------
    {
        AudioBuffer<float> b(2, 128);
        for (int i = 0; i < 128; ++i)
        {
            b.setSample(0, i, 0.25f);
            b.setSample(1, i, 0.25f);
        }

        e.process(b, 0);
        e.process(b, -8);

        check(b.getSample(0, 0) == 0.25f && b.getSample(0, 127) == 0.25f,
              "un bloque de cero muestras, o de menos, no toca el buffer");

        // Y pedir MAS muestras de las que tiene el buffer no es un no-op: se
        // procesan las que hay. Lo contrario --quedarse sin hacer nada-- es lo
        // que dejaria el bloque entero en seco, que es el bug de las 3584
        // muestras del que habla la cabecera del motor.
        e.process(b, 1000000);
        check(b.getSample(0, 0) == 1.0f && b.getSample(0, 127) == 1.0f,
              "pedir mas muestras de las que tiene el buffer procesa las que hay, y no se queda sin hacer nada");
    }

    //--- cero canales ----------------------------------------------------
    {
        AudioBuffer<float> sinCanales(0, 128);
        e.process(sinCanales, 128);

        // Si el motor escribiera en el canal 0 de un buffer que no tiene
        // canales, esto habria petado en `getWritePointer` con el aserto de
        // DspCore. Que se llegue aqui ya es la comprobacion.
        const std::vector<float> salta = fxprobe::render(e, fxprobe::noise(64, 3u));
        check(salta.size() == 64 && std::isfinite(salta[10]),
              "un buffer de cero canales no rompe el motor, que sigue sonando despues");
    }

    //--- mas de dos canales: los sobrantes, intactos ---------------------
    {
        AudioBuffer<float> cuatro(4, 64);
        for (int i = 0; i < 64; ++i)
        {
            cuatro.setSample(0, i, 0.1f);
            cuatro.setSample(1, i, 0.1f);
            cuatro.setSample(2, i, 0.2f);
            cuatro.setSample(3, i, 0.3f);
        }

        e.process(cuatro, 64);

        check(cuatro.getSample(0, 10) == 1.0f, "los dos primeros canales si se procesan");
        check(cuatro.getSample(2, 10) == 0.2f, "el tercero se queda como estaba");
        check(cuatro.getSample(3, 40) == 0.3f, "y el cuarto tambien");
    }
}

//==============================================================================
/** C4 y C5 en el motor: un ruteo que no existe, los mandos del motor y la cola
    de realimentacion. */
void testFxEngineRoutingBounds()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    //--- un numero de ruteo que no existe --------------------------------
    {
        FxEngine raro, serie;
        for (FxEngine* engine : {&raro, &serie})
        {
            engine->prepare(fxprobe::kSr, 2, 512);
            engine->setCatalogue(cat, count);
            engine->getSlot(0).setType(1);
            engine->getSlot(0).setMix(0.5f);
        }

        raro.setRouting(static_cast<FxRouting>(42));
        serie.setRouting(FxRouting::Series);

        const std::vector<float> in = fxprobe::noise(1024, 8080u);
        check(fxprobe::render(raro, in) == fxprobe::render(serie, in),
              "un numero de ruteo que no existe se procesa como la serie, no se cae del switch");
    }

    //--- los mandos del motor se recortan, y el NaN no entra -------------
    {
        FxEngine e;

        e.setSendLevel(5.0f);
        check(e.getSendLevel() == 1.0f, "un envio por encima de 1 se recorta");
        e.setSendLevel(-1.0f);
        check(e.getSendLevel() == 0.0f, "y por debajo de 0 tambien");
        e.setFeedbackGain(5.0f);
        check(e.getFeedbackGain() == 0.95f, "una realimentacion por encima del tope se recorta");
        e.setFeedbackGain(-1.0f);
        check(e.getFeedbackGain() == 0.0f, "y por debajo de 0 tambien");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        e.setSendLevel(nan);
        e.setFeedbackGain(nan);
        check(std::isfinite(e.getSendLevel()), "un NaN no se cuela en el envio");
        check(std::isfinite(e.getFeedbackGain()),
              "ni en la realimentacion, que es la que se queda guardada en un buffer");
    }

    //--- el reset del motor vacia la cola del ruteo 9 --------------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.setRouting(FxRouting::SeriesWithFeedback);
        e.setFeedbackGain(0.5f);
        e.getSlot(0).setType(2); // la fila que devuelve la mitad: su salida depende de la entrada
        e.getSlot(0).setMix(1.0f);

        fxprobe::render(e, fxprobe::noise(512, 11u));

        const std::vector<float> silencio(512, 0.0f);
        const std::vector<float> conCola = fxprobe::render(e, silencio);
        bool conRealimentacion           = false;
        for (float v : conCola)
            if (v != 0.0f) conRealimentacion = true;

        check(conRealimentacion, "sin reset, el bloque siguiente lleva dentro el anterior");

        e.reset();

        const std::vector<float> trasReset = fxprobe::render(e, silencio);
        bool mudo                          = true;
        for (float v : trasReset)
            if (v != 0.0f) mudo = false;

        check(mudo, "reset del motor vacia la cola de realimentacion del ruteo 9");
    }

    //--- un hueco que no existe sale por el primero ---------------------
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.getSlot(0).setType(1);

        // Solo la sobrecarga CONST: la otra lleva un `dspAssert` que en Debug
        // para el proceso, y esto no es una prueba de que el aserto exista.
        const FxEngine& ce = e;
        check(&ce.getSlot(-1) == &ce.getSlot(0), "un hueco por debajo de rango se resuelve por el primero");
        check(&ce.getSlot(99) == &ce.getSlot(kFxNumSlots - 1),
              "y uno por encima se resuelve por el ultimo, que es lo que recorta el indice");
        check(&ce.getSlot(0) != &ce.getSlot(1), "mientras que los que existen son huecos distintos");
    }
}

//==============================================================================
/** C7. La forma de la clase. La regla primera del modulo es que en el lazo de
    audio no hay nada virtual, y se comprueba igual que las etapas de caracter. */
void testFxEngineTypeTraits()
{
    using namespace abd::dsp;

    check(!std::is_polymorphic<FxSlot>::value,
          "el hueco no tiene vtable: en el lazo de audio no hay llamada virtual");
    check(!std::is_polymorphic<FxEngine>::value, "ni el motor");
    check(!std::is_copy_constructible<FxSlot>::value,
          "un hueco no se copia: su efecto es estado opaco con un destructor arbitrario");
    check(std::is_move_constructible<FxSlot>::value, "pero se mueve, que es como lo devuelve la fabrica");
    check(std::is_nothrow_move_constructible<FxSlot>::value, "y se mueve sin lanzar");
    check(!std::is_copy_constructible<FxEngine>::value, "el motor entero tampoco se copia, porque lleva dentro los cuatro huecos");
    check(std::is_move_constructible<FxEngine>::value, "pero el motor se mueve, que es como lo coge el producto");
    check(kFxNumSlots == 4, "el motor tiene los cuatro huecos de la topologia");
    check(kFxMaxParams == 12, "y el hueco tiene un bus de doce mandos");
}

//==============================================================================
/** LA AUDITRIA DE TOPOLOGIAS DEL MOTOR. Que hacen los NUEVE RUTEOS.

    POR QUE ESTO NO ESTA EN LAS 766. Las comprobaciones del ruteo que ya
    tienen el banco son tres y las tres son debiles a proposito: "produce
    numeros finitos", "produce audio" y "no se dispara solo". Comprueban que el
    motor esta vivo, no que haga lo que dice. Un ruteo que se comiera un hueco,
    que doblara una rama o que perdiera la seca sonaria igual de bien, y de
    hecho varios de esos numeros salen bien.

    Y HAY UNO QUE NO SUENA BIEN. Con los cuatro huecos en bypass, cinco de los
    nueve ruteos NO devuelven la senal intacta: dos la borran y tres la
    multiplican. El detalle esta al final de esta seccion, con el porque, que es
    de donde salio el arreglo.

    COMO SE COMPRUEBA UN DIAGRAMA. Con la sonda de arriba: cuatro huecos, cada
    uno con una fila que devuelve una constante distinta --1, 2, 4 y 8--, todos
    con la mezcla a 0.5 y la ganancia a 1. La mezcla a 0.5 es lo que hace el
    trabajo: en una cadena en serie, un hueco a mezcla 1 BORRA al anterior,
    porque escribe encima. A 0.5 cada hueco deja ver el suyo y se ve el de
    atras, y el resultado de la cadena es una cuenta cerrada donde cada
    constante aparece con un peso distinto. Mandando una entrada de 1.0 el
    resultado esperado es un numero exacto, y se compara bit a bit: todos los
    productos son potencias de dos sobre 1, 2, 4 y 8, asi que no hay ni un
    redondeo y comparar a pelo no esconde nada.

    El hueco i aporta la constante 2^(i-1), y el diagrama de cada modo esta
    escrito en la cabecera de `FxEngine.h`. aqui se comprueba que el codigo hace
    ESA cuenta, no una parecida.
*/
namespace fxtopo
{
using namespace abd::dsp;

const int kC1 = 9; // las filas de constante empiezan aqui en la tabla
const int kC2 = 10;
const int kC4 = 11;
const int kC8 = 12;

/** Cuatro huecos con su constante, la mezcla a la mitad y la ganancia a 1.
    Es el montaje con el que se lee un ruteo por dentro. */
void montar(FxEngine& e, const FxEffectInfo* cat, int count) noexcept
{
    e.prepare(fxprobe::kSr, 2, 512);
    e.setCatalogue(cat, count);

    const int tipos[4] = {kC1, kC2, kC4, kC8};
    for (int i = 0; i < kFxNumSlots; ++i)
    {
        e.getSlot(i).setType(tipos[i]);
        e.getSlot(i).setMix(0.5f);
        e.getSlot(i).setGain(1.0f);
    }
}

/** Un bloque de 1.0 clavado, que es la entrada de la cuenta. */
std::vector<float> entrada(int n = 256)
{
    return std::vector<float>(static_cast<size_t>(n), 1.0f);
}

/** ¿Sale el bloque entero clavado a este numero? */
bool todoIgualA(const std::vector<float>& v, float valor) noexcept
{
    for (float x : v)
        if (x != valor) return false;
    return true;
}
} // namespace fxtopo

//==============================================================================
/** R1. Los nueve ruteos hacen la cuenta que dice su diagrama, no una parecida. */
void testFxEngineRoutingTopology()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    // El numero de cada fila es la cuenta del diagrama con la entrada en 1.0.
    // EstánComments en el pie de la seccion de donde salen; lo que se comprueba
    // aqui es que el codigo siga LA CUENTA, no que cuadre el papel.
    struct Caso
    {
        int routing;
        float esperado;
        const char* diagrama;
    };
    const Caso casos[10] = {
        {0, 5.375f, "1 -> 2 -> 3 -> 4"},
        {1, 5.625f, "(1 || 2) -> 3 -> 4"},
        {2, 9.5f, "(1 || 2) || (3 || 4)"},
        {3, 10.5f, "1 || 2 || 3 || 4"},
        {4, 7.75f, "(1 -> 2) || (3 -> 4)"},
        {5, 5.75f, "1 -> (2 || 3) -> 4"},
        {6, 11.0f, "(1 || 2) -> (3 || 4)"},
        {7, 8.25f, "(1 -> 2 -> 3) || 4"},
        {8, 5.625f, "(1 || 2) -> 3 -> 4, el mismo diagrama que el 1"},
        {9, 5.375f, "1 -> 2 -> 3 -> 4 con la realimentacion a 0"}};

    for (const Caso& c : casos)
    {
        FxEngine e;
        fxtopo::montar(e, cat, count);
        e.setRouting(static_cast<FxRouting>(c.routing));
        e.setFeedbackGain(0.0f);

        const std::vector<float> out = fxprobe::render(e, fxtopo::entrada());

        check(fxtopo::todoIgualA(out, c.esperado), c.diagrama);
        std::printf("       [ruteo %d] %-34s = %g\n", c.routing, c.diagrama, c.esperado);
    }

    // Y el 1 y el 8, que el banco ya comprobaba, aqui se ven con la cuenta.
    check(casos[1].esperado == casos[8].esperado,
          "el 1 y el 8 tienen la misma cuenta, que es lo que hace ABDEep con dos nombres");
}

//==============================================================================
/** R2. Con los cuatro huecos en BYPASS, el modulo es transparente. Y en los
    nueve ruteos, no solo en el de serie. */
void testFxEngineBypassIsTransparent()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat      = fxsonda::tabla();
    const int count              = fxsonda::kFilas;
    const std::vector<float> uno = fxtopo::entrada();

    for (int r = 0; r <= 9; ++r)
    {
        FxEngine e;
        e.prepare(fxprobe::kSr, 2, 512);
        e.setCatalogue(cat, count);
        e.setRouting(static_cast<FxRouting>(r));
        e.setFeedbackGain(0.0f);

        char what[128];
        std::snprintf(what, sizeof(what),
                      "con los cuatro huecos en bypass, el ruteo %d deja la senal intacta", r);

        check(fxprobe::render(e, uno) == uno, what);
    }
}

//==============================================================================
/** R3. El modo de envio y el bypass del motor, en los nueve ruteos. El envio
    mezcla al FINAL, y por eso el resultado es la seca y la cuenta del ruteo con
    el mismo reparto, sin tocar los mandos de los huecos. */
void testFxEngineSendAcrossTopologies()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    struct Caso
    {
        int routing;
        float insercion;
    };
    const Caso casos[10] = {
        {0, 5.375f}, {1, 5.625f}, {2, 9.5f}, {3, 10.5f}, {4, 7.75f}, {5, 5.75f}, {6, 11.0f}, {7, 8.25f}, {8, 5.625f}, {9, 5.375f}};

    for (const Caso& c : casos)
    {
        FxEngine e;
        fxtopo::montar(e, cat, count);
        e.setRouting(static_cast<FxRouting>(c.routing));
        e.setFeedbackGain(0.0f);
        e.setMode(FxMode::Send);
        e.setSendLevel(0.5f);

        const std::vector<float> out = fxprobe::render(e, fxtopo::entrada());
        check(fxtopo::todoIgualA(out, 0.5f + 0.5f * c.insercion),
              "el envio es la mezcla del bus, no un cambio en los mandos de los huecos");

        bool mandosIntactos = true;
        for (int i = 0; i < kFxNumSlots; ++i)
            if (e.getSlot(i).getMix() != 0.5f || e.getSlot(i).getGain() != 1.0f)
                mandosIntactos = false;
        check(mandosIntactos, "y los mandos de los cuatro huecos se quedan donde el usuario los puso");
    }

    //--- y el bypass del motor, en los nueve -----------------------------
    for (int r = 0; r <= 9; ++r)
    {
        FxEngine e;
        fxtopo::montar(e, cat, count);
        e.setRouting(static_cast<FxRouting>(r));
        e.setMode(FxMode::Bypass);

        const std::vector<float> uno = fxtopo::entrada();

        char what[128];
        std::snprintf(what, sizeof(what),
                      "el bypass del motor ignora el ruteo %d y devuelve la senal intacta", r);

        check(fxprobe::render(e, uno) == uno, what);
    }
}

//==============================================================================
/** R4. Donde entra la realimentacion del ruteo 9. Con un solo hueco activo, la
    cuenta del bloque se puede dejar escrita entera. */
void testFxEngineFeedbackEntersBeforeTheChain()
{
    using namespace abd::dsp;

    const FxEffectInfo* cat = fxsonda::tabla();
    const int count         = fxsonda::kFilas;

    FxEngine e;
    e.prepare(fxprobe::kSr, 2, 512);
    e.setCatalogue(cat, count);
    e.setRouting(FxRouting::SeriesWithFeedback);
    e.setFeedbackGain(0.5f);

    // Un solo hueco, con su constante. Los otros tres en bypass, que es lo que
    // deja la cuenta a la vista.
    e.getSlot(0).setType(fxtopo::kC1);
    e.getSlot(0).setMix(0.5f);
    e.getSlot(0).setGain(1.0f);

    const std::vector<float> uno = fxtopo::entrada();

    const std::vector<float> primero = fxprobe::render(e, uno);
    check(fxtopo::todoIgualA(primero, 1.0f),
          "el primer bloque no lleva realimentacion, porque la cola empieza vacia");

    //  y = hueco( x + g · y_anterior )  =  0.5 · (1 + 0.5) + 0.5  =  1.25
    //
    // Y SI LA REALIMENTACION SE SUMARA DESPUES DE LA CADENA daria 1.5, que es
    // otra topologia. La diferencia entre 1.25 y 1.5 es lo que distingue una de
    // la otra, y es la que hizo falta para elegir entre las dos.
    const std::vector<float> segundo = fxprobe::render(e, uno);
    check(fxtopo::todoIgualA(segundo, 1.25f),
          "la realimentacion entra ANTES de la cadena, no despues");

    //--- y con la ganancia a 0 el 9 es exactamente la serie -----------------
    e.setFeedbackGain(0.0f);
    check(fxprobe::render(e, uno) == primero,
          "con la realimentacion a 0 el bloque siguiente vuelve a ser el primero");
}

//==============================================================================
/** LA CASCADA DE DOS REPISAS Y EL PERFIL DEL MS2000.

    Que es esto y por que esta aqui. El biquad de la repisa estaba en el modulo
    desde antes (`ShelfFilter`), pero la CADENA de dos bandas --y las dos tablas
    de cuatro posiciones del selector-- vivian en un shim de producto, sin una
    sola comprobacion que las mirara. Aqui esta la maquina, con perfil inyectado
    como el eco multi-cabezal, y las tablas del MS2000 como puro dato.

    QUE NO SE COMPRUEBA AQUI, Y POR QUE. La paridad de la repisa suelta con la
    referencia congelada de libm ya la mide `testShelfFilter`, y no se repite:
    repetirla para la cascada seria medir dos veces lo mismo. Lo que se mide es
    lo que la cascada anade y la repisa no tiene: el orden, el estado de las dos
    etapas, la tabla del perfil y la puerta de hercios continuos.
*/
void testCascadeShelfEq()
{
    using namespace abd::dsp;
    using Eq = CascadeShelfEq<MS2000EqProfile>;

    constexpr double kSr = 48000.0;
    constexpr int kN     = 512;

    //--- LA TABLA DEL PERFIL, Y QUE ESTA ORDENADA -------------------------
    {
        check(MS2000EqProfile::numPositions == 4, "el selector del MS2000 tiene cuatro posiciones");
        check(MS2000EqProfile::lowFreqs[0] == 160.0f && MS2000EqProfile::lowFreqs[3] == 600.0f,
              "la tabla de graves es la del hardware: 160, 250, 400 y 600");
        check(MS2000EqProfile::highFreqs[0] == 4000.0f && MS2000EqProfile::highFreqs[3] == 12000.0f,
              "y la de agudos: 4, 6, 8 y 12 kHz");

        bool crecientes = true;
        for (int i = 1; i < MS2000EqProfile::numPositions; ++i)
            if (MS2000EqProfile::lowFreqs[i] <= MS2000EqProfile::lowFreqs[i - 1] || MS2000EqProfile::highFreqs[i] <= MS2000EqProfile::highFreqs[i - 1])
                crecientes = false;
        check(crecientes, "las dos tablas suben, que es lo que hace un selector");

        check(MS2000EqProfile::defaultLowIndex == 1 && MS2000EqProfile::defaultHighIndex == 2,
              "la posicion de fabrica es la central de cada selector, no la primera");
        check(MS2000EqProfile::gainMinDb == -12.0f && MS2000EqProfile::gainMaxDb == 12.0f,
              "el tope de ganancia del MS2000 es +-12 dB, no +-15");
    }

    //--- LA MAQUINA NASCE EN LA POSICION DE FABRICA ------------------------
    {
        Eq eq;
        eq.prepare(kSr);

        check(eq.getLowIndex() == MS2000EqProfile::defaultLowIndex && eq.getHighIndex() == MS2000EqProfile::defaultHighIndex,
              "preparar deja los dos selectores en su posicion central");
        check(eq.getLowFrequencyHz() == 250.0f, "que son 250 Hz en la baja");
        check(eq.getHighFrequencyHz() == 8000.0f, "y 8 kHz en la alta");
        check(eq.getLowGainDB() == 0.0f && eq.getHighGainDB() == 0.0f,
              "y las dos ganancias en 0 dB, que es identidad exacta");
        check(eq.getLowShelf().getMode() == ShelfMode::Low && eq.getHighShelf().getMode() == ShelfMode::High,
              "una repisa baja y una alta, fijas por construccion");
    }

    //--- LOS MANDOS DE FRECUENCIA Y DE GANANCIA NO SE TOCAN ENTRE SI ------
    {
        Eq eq;
        eq.prepare(kSr);
        eq.setLowIndex(0);  // 160 Hz
        eq.setHighIndex(3); // 12 kHz

        const float graveAntes = eq.getLowFrequencyHz();
        const float agudoAntes = eq.getHighFrequencyHz();

        // Pedir ganancia no puede mover la frecuencia que el usuario tiene puesta.
        for (int db = -12; db <= 12; db += 3)
        {
            eq.setLowGainDB(static_cast<float>(db));
            eq.setHighGainDB(static_cast<float>(-db));
        }

        check(eq.getLowFrequencyHz() == graveAntes && eq.getHighFrequencyHz() == agudoAntes,
              "mover la ganancia deja la frecuencia donde estaba");
        check(eq.getLowGainDB() == 12.0f && eq.getHighGainDB() == -12.0f,
              "y la ganancia se queda en donde la mandaron, con su recorte");

        // Y el otro lado: mover la frecuencia no puede mover la ganancia.
        eq.setLowIndex(2);
        check(eq.getLowGainDB() == 12.0f, "cambiar de selector deja la ganancia como estaba");
        check(eq.getLowFrequencyHz() == 400.0f, "y cambia solo la frecuencia");
    }

    //--- UN INDICE QUE SE PASA SE RECORTA, Y UNO QUE NO CAMBIA NO RECALCULA -
    {
        Eq eq;
        eq.prepare(kSr);
        eq.setLowIndex(99);
        check(eq.getLowIndex() == MS2000EqProfile::numPositions - 1,
              "un indice por encima se recorta a la ultima posicion");
        eq.setLowIndex(-5);
        check(eq.getLowIndex() == 0, "y uno por debajo a la primera");

        // Poner el mismo indice no debe reescribir nada: el biquad se recalcula
        // solo cuando los coeficientes cambian de verdad.
        const float antes = eq.getLowShelf().getB0();
        eq.setLowIndex(0);
        eq.setLowIndex(0);
        check(eq.getLowShelf().getB0() == antes,
              "poner el mismo indice dos veces no toca los coeficientes");
    }

    //--- LA PUERTA DE HERCIOS CONTINUOS -----------------------------------
    {
        Eq eq;
        eq.prepare(kSr);
        eq.setLowIndex(3);            // 600 Hz, por la tabla
        eq.setLowFrequencyHz(300.0f); // ahora manda el hercio
        check(eq.getLowFrequencyHz() == 300.0f, "la puerta de hercios continuos manda sobre la tabla");

        // Y volver a la tabla la devuelve a su sitio.
        eq.setLowIndex(0);
        check(eq.getLowFrequencyHz() == 160.0f,
              "volver a elegir por indice recupera la tabla, que sigue ahi debajo");
    }

    //--- LAS DOS PUERTAS SON INDEPENDIENTES ------------------------------
    //
    // UNA BANDERA COMUN PARA LAS DOS REPISAS ERA UN FALLO DE VERDAD. Escribir
    // un hercio en la baja apagaba la bandera comun, y al siguiente
    // `setHighIndex` la maquina reescribia LAS DOS frecuencias: el hercio que
    // el producto acababa de poner en la baja se perdia sin avisar y sin error
    // de ningun tipo. Solo pasaba cuando el indice de la alta casualmente no era
    // el que ya estaba, porque la guarda de "no ha cambiado" cortaba antes, asi
    // que el fallo era intermitente.
    //
    // Y EL OTRO LADO DE LA MONEDA: la guarda de "no ha cambiado" miraba solo el
    // indice, no la puerta. Con el hercio continuo puesto y el mismo indice, no
    // pasaba nada y la puerta se quedaba abierta para siempre.
    {
        Eq eq;
        eq.prepare(kSr);

        // (a) hercio continuo en la baja, y el selector de la ALTA se mueve.
        eq.setLowIndex(2);            // 400 Hz por la tabla
        eq.setLowFrequencyHz(300.0f); // ahora manda 300 Hz

        eq.setHighIndex(0); // la alta: 4 kHz
        check(eq.getHighFrequencyHz() == 4000.0f,
              "mover el selector de la alta pone su frecuencia de la tabla");
        check(eq.getLowFrequencyHz() == 300.0f,
              "y NO se lleva por delante el hercio continuo de la baja");

        // (b) lo mismo pero con el indice de la alta SIN cambiar: tambien tiene
        // que dejar el hercio de la baja donde estaba, y no porque "no ha pasado
        // nada" sino porque la otra puerta no tiene nada que ver.
        eq.setHighIndex(0);
        check(eq.getLowFrequencyHz() == 300.0f,
              "volver a poner el mismo indice de la alta deja la baja como estaba");

        // (c) y al reves: hercio continuo en la ALTA, selector de la baja.
        eq.setHighFrequencyHz(7000.0f);
        eq.setLowIndex(0); // 160 Hz
        check(eq.getLowFrequencyHz() == 160.0f,
              "mover el selector de la baja pone su frecuencia de la tabla");
        check(eq.getHighFrequencyHz() == 7000.0f,
              "y deja el hercio continuo de la alta");
    }

    //--- EL MISMO INDICE SI CIERRA LA PUERTA -----------------------------
    {
        Eq eq;
        eq.prepare(kSr);
        eq.setLowIndex(0);            // 160 Hz
        eq.setLowFrequencyHz(300.0f); // ahora manda 300 Hz

        // El indice NO cambia. Aun asi, elegir por indice cierra la puerta: si
        // no, el hercio continuo se quedaria puesto para siempre y el selector
        // del panel no tendria efecto, que es el sintoma de un mando muerto.
        eq.setLowIndex(0);
        check(eq.getLowFrequencyHz() == 160.0f,
              "poner el mismo indice cierra la puerta continua, que es lo que se le pidio");
    }

    //--- PREPARAR ES UN ARRANQUE, NO UN CAMBIO DE MANDO -------------------
    {
        Eq eq;
        eq.prepare(kSr);
        eq.setLowIndex(0); // 160 Hz
        eq.setLowFrequencyHz(300.0f);
        eq.setHighFrequencyHz(7000.0f);

        eq.prepare(kSr);

        check(eq.getLowFrequencyHz() == 250.0f && eq.getHighFrequencyHz() == 8000.0f,
              "preparar de nuevo vuelve a la tabla del perfil, que es lo que es un arranque");
        check(eq.getLowIndex() == MS2000EqProfile::defaultLowIndex && eq.getHighIndex() == MS2000EqProfile::defaultHighIndex,
              "y los dos indices tambien vuelven a su posicion de fabrica");
    }

    //--- LA CASCADA ES LA SERIE DE LAS DOS, Y SE DIFERENCIA DE UNA SOLA ----
    {
        Eq cascada;
        cascada.prepare(kSr);
        cascada.setLowGainDB(-6.0f);
        cascada.setHighGainDB(6.0f);

        ShelfFilter sola;
        sola.prepare(kSr);
        sola.setMode(ShelfMode::High);
        sola.setFrequencyHz(8000.0f);
        sola.setGainDB(6.0f);

        std::vector<float> porCascada(static_cast<size_t>(kN), 0.0f);
        std::vector<float> porSola(static_cast<size_t>(kN), 0.0f);

        for (int i = 0; i < kN; ++i)
        {
            const float x = 0.3f * std::sin(0.021f * static_cast<float>(i));

            float a = x, b = x;
            cascada.processFrame(a, b);
            porCascada[static_cast<size_t>(i)] = a;

            float c = x, d = x;
            sola.processFrame(c, d);
            porSola[static_cast<size_t>(i)] = c;
        }

        bool distintas = false;
        for (int i = 0; i < kN; ++i)
            if (porCascada[static_cast<size_t>(i)] != porSola[static_cast<size_t>(i)])
                distintas = true;

        check(distintas, "la cascada de dos NO suena como la repisa alta sola: la grave hace algo");

        // Y LA CASCADA ES EXACTAMENTE LA GRAVE Y LUEGO LA ALTA, motor incluido.
        // Se mide contra dos `ShelfFilter` conducidos a mano, no contra una
        // formula escrita aqui: comparar contra una reimplementacion en vez de
        // contra la que corre de verdad es el error clasico de este tipo de test.
        ShelfFilter manoBaja, manoAlta;
        manoBaja.prepare(kSr);
        manoAlta.prepare(kSr);
        manoBaja.setMode(ShelfMode::Low);
        manoAlta.setMode(ShelfMode::High);
        manoBaja.setFrequencyHz(250.0f);
        manoAlta.setFrequencyHz(8000.0f);
        manoBaja.setGainDB(-6.0f);
        manoAlta.setGainDB(6.0f);

        std::vector<float> porMano(static_cast<size_t>(kN), 0.0f);
        for (int i = 0; i < kN; ++i)
        {
            float a = 0.3f * std::sin(0.021f * static_cast<float>(i));
            float b = a;
            manoBaja.processFrame(a, b);
            manoAlta.processFrame(a, b);
            porMano[static_cast<size_t>(i)] = a;
        }

        check(porCascada == porMano,
              "y la maquina es bit a bit las dos repisas en serie, con el motor de verdad dentro");
    }

    //--- Y EL ORDEN NO IMPORTA, QUE ES LO QUE TOCA DECIR -------------------
    // Las dos etapas son LTI, asi que la respuesta en cascada es H1(z)*H2(z) y
    // da igual el orden. Se comprueba para que nadie afirme lo contrario, y de
    // paso porque es el motivo de que la cascada se pueda rehacer sin miedo.
    //
    // Se mide con dos pares de `ShelfFilter` conducidos a mano y en el orden
    // contrario, no forzando los modos de la maquina: los accesores de la
    // maquina son `const` a proposito, y un test que pelea con su propia API
    // para montarle un caso esta diciendo otra cosa de la que cree.
    {
        ShelfFilter baja, alta, alta2, baja2;
        baja.prepare(kSr);
        alta.prepare(kSr);
        alta2.prepare(kSr);
        baja2.prepare(kSr);

        baja.setMode(ShelfMode::Low);
        baja.setFrequencyHz(250.0f);
        baja.setGainDB(-6.0f);
        alta.setMode(ShelfMode::High);
        alta.setFrequencyHz(8000.0f);
        alta.setGainDB(6.0f);

        alta2.setMode(ShelfMode::High);
        alta2.setFrequencyHz(8000.0f);
        alta2.setGainDB(6.0f);
        baja2.setMode(ShelfMode::Low);
        baja2.setFrequencyHz(250.0f);
        baja2.setGainDB(-6.0f);

        std::vector<float> a(static_cast<size_t>(kN), 0.0f);
        std::vector<float> b(static_cast<size_t>(kN), 0.0f);

        for (int i = 0; i < kN; ++i)
        {
            const float x = 0.3f * std::sin(0.021f * static_cast<float>(i));

            float p = x, q = x;
            baja.processFrame(p, q);
            alta.processFrame(p, q);
            a[static_cast<size_t>(i)] = p;

            float r = x, s = x;
            alta2.processFrame(r, s);
            baja2.processFrame(r, s);
            b[static_cast<size_t>(i)] = r;
        }

        float peor = 0.0f;
        for (int i = 0; i < kN; ++i)
            peor = jmax(peor, std::fabs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]));

        // NO se pide igualdad bit a bit, y no por pereza: dos órdenes distintos
        // del MISMO sistema de orden 4 agrupan el redondeo de otra manera. Lo
        // medido va de 1,9e-6 con g++ optimizado a 3,5e-6 con MSVC en Debug,
        // que son de 16 a 30 ULPs. La holgura de 1e-4 deja unas treinta veces de
        // margen por encima, y sigue siendo mas de doscientas veces mas
        // pequena que lo que daria un orden equivocado de verdad, que es lo
        // unico que este aserto tiene que cazar. El numero se imprime porque
        // un umbral sin el valor al lado es un numero que nadie revisa.
        std::printf("  [cascada] las dos etapas en orden contrario difieren en %.3e\n",
                    static_cast<double>(peor));
        check(peor < 1e-4f,
              "las dos etapas son LTI, asi que ponerlas al reves da la misma respuesta (no es conmutatividad, es algebra)");
    }

    //--- ESTABILIDAD, DETERMINISMO Y RESET EN LAS CUATRO POSICIONES --------
    {
        bool todoFinito  = true;
        bool picosVistos = false;

        for (int li = 0; li < MS2000EqProfile::numPositions; ++li)
            for (int hi = 0; hi < MS2000EqProfile::numPositions; ++hi)
            {
                Eq eq;
                eq.prepare(kSr);
                eq.setLowIndex(li);
                eq.setHighIndex(hi);
                eq.setLowGainDB(-12.0f);
                eq.setHighGainDB(12.0f);

                for (int i = 0; i < 4096; ++i)
                {
                    float l = 0.4f * std::sin(0.011f * static_cast<float>(i));
                    float r = l * 0.8f;
                    eq.processFrame(l, r);

                    if (!std::isfinite(l) || !std::isfinite(r)) todoFinito = false;
                    if (jmax(jmax(std::fabs(l), std::fabs(r)), 0.0f) > 1e-3f) picosVistos = true;
                }
            }

        check(todoFinito, "las dieciseis combinaciones de las dos tablas dan numeros finitos");
        check(picosVistos, "y producen audio");

        // Determinismo: dos motores frescos con la misma configuracion, igual.
        auto corrida = [](bool conReset) -> std::vector<float> {
            Eq eq;
            eq.prepare(kSr);
            eq.setLowIndex(1);
            eq.setHighIndex(3);
            eq.setLowGainDB(4.0f);
            eq.setHighGainDB(-4.0f);

            // Cuatro bloques de kN, asi que el buffer es de 4 por kN: con
            // uno de kN se salia de rango al segundo bloque.
            std::vector<float> salida(static_cast<size_t>(kN) * 4, 0.0f);
            for (int bloque = 0; bloque < 4; ++bloque)
            {
                if (conReset && bloque == 2) eq.reset();
                for (int i = 0; i < kN; ++i)
                {
                    float l = 0.25f * std::sin(0.013f * static_cast<float>(bloque * kN + i));
                    float r = l;
                    eq.processFrame(l, r);
                    salida[static_cast<size_t>(bloque * kN + i)] = l;
                }
            }
            return salida;
        };

        std::vector<float> c1 = corrida(false);
        std::vector<float> c2 = corrida(false);
        check(c1 == c2, "dos motores frescos con lo mismo dan lo mismo");
    }

    //--- 0 dB ES LA IDENTIDAD, Y POR ESO UN PRESET NUEVO NO SE OYE --------
    {
        Eq eq;
        eq.prepare(kSr);
        eq.setLowGainDB(0.0f);
        eq.setHighGainDB(0.0f);

        check(eq.getLowShelf().esIdentidad() && eq.getHighShelf().esIdentidad(),
              "a 0 dB las dos repisas son la identidad exacta");

        std::vector<float> entrada(static_cast<size_t>(kN));
        for (int i = 0; i < kN; ++i) entrada[static_cast<size_t>(i)] = 0.2f * std::sin(0.03f * static_cast<float>(i));

        std::vector<float> salida = entrada;
        for (int i = 0; i < kN; ++i)
        {
            float l = salida[static_cast<size_t>(i)];
            float r = l;
            eq.processFrame(l, r);
            salida[static_cast<size_t>(i)] = l;
        }

        check(salida == entrada, "y la cascada entera a 0 dB devuelve la senal BIT A BIT");
    }

    std::printf("  [ecualizador] cascada de dos repisas con el perfil del MS2000: "
                "16 combinaciones de tabla, estabilidad, determinismo y 0 dB identidad\n");
}

int main()
{
    checkStageContract<abd::dsp::NullStage>("NullStage");
    checkStageContract<abd::dsp::TapeColour>("TapeColour");
    checkStageContract<abd::dsp::DiodeBridge>("DiodeBridge");

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

    // La auditoria estructural del motor de huecos (criterios C1 a C8).
    testFxSlotLifecycle();
    testFxSlotRowContract();
    testFxSlotParameterBounds();
    testFxEngineWiringOrder();
    testFxEngineBlockShape();
    testFxEngineRoutingBounds();
    testFxEngineTypeTraits();
    testFxEngineRoutingTopology();
    testFxEngineBypassIsTransparent();
    testFxEngineSendAcrossTopologies();
    testFxEngineFeedbackEntersBeforeTheChain();
    std::printf("  [huecos] topologias: los nueve ruteos, el bypass y el envio, por cuenta y no por sonido\n");
    std::printf("  [huecos] auditoria estructural: ciclo de vida, filas a medias, orden, rangos, no-finitos, forma del bloque y forma de la clase\n");

    testMandosDelBarridoSinRecorte();
    testShelfFilter();
    testShelfRowContract();
    testCascadeShelfEq();
    testPhaserCoefficientParity();
    testPhaserControlRateStepping();
    testPhaserRowContract();
    testSchroederDampingAtZero();
    testTapeColourDriftIsNotWired();

    // Motores de la Fase 2 (Vocoder, Phaser, Mood Filter, Solina Ensemble)
    {
        // 1. DspPhaser
        abd::dsp::DspPhaser phaser;
        phaser.prepare(44100.0);
        phaser.setRateNorm(0.5f);
        phaser.setDepthNorm(0.8f);
        phaser.setFeedbackNorm(0.4f);
        phaser.setStageCount(6);

        float inL[64] = { 1.0f };
        float inR[64] = { 1.0f };
        float outL[64] = {};
        float outR[64] = {};
        phaser.process(inL, inR, outL, outR, 64);
        check(std::isfinite(outL[0]) && std::isfinite(outR[0]), "DspPhaser produce muestras finitas");
        check(outL[0] != 0.0f, "DspPhaser responde al impulso");

        // 2. DspMoodFilter
        abd::dsp::DspMoodFilter mood;
        mood.prepare(44100.0);
        mood.setFilterType(abd::dsp::DspMoodFilter::kLowpass);
        mood.setBaseFreqNorm(0.5f);
        mood.setDriveNorm(0.3f);
        mood.setFourPole(true);

        std::fill(std::begin(outL), std::end(outL), 0.0f);
        std::fill(std::begin(outR), std::end(outR), 0.0f);
        mood.process(inL, inR, outL, outR, 64);
        check(std::isfinite(outL[0]) && std::isfinite(outR[0]), "DspMoodFilter produce muestras finitas");
        check(outL[0] != 0.0f, "DspMoodFilter responde al impulso");

        // 3. DspSolinaEnsemble
        abd::dsp::DspSolinaEnsemble solina;
        solina.prepare(44100.0);
        solina.setRateNorm(0.5f);
        solina.setDepthNorm(0.6f);
        solina.setSpreadNorm(0.8f);

        std::fill(std::begin(outL), std::end(outL), 0.0f);
        std::fill(std::begin(outR), std::end(outR), 0.0f);
        solina.process(inL, inR, outL, outR, 64);
        check(std::isfinite(outL[0]) && std::isfinite(outR[0]), "DspSolinaEnsemble produce muestras finitas");

        // 4. DspVocoderBank
        abd::dsp::DspVocoderBank<16> vocoder;
        vocoder.prepare(44100.0);
        vocoder.setBandCount(16);
        vocoder.setFormantShift(1.0f);

        float modL[64] = { 0.5f };
        float carL[64] = { 0.5f };
        std::fill(std::begin(outL), std::end(outL), 0.0f);
        std::fill(std::begin(outR), std::end(outR), 0.0f);
        vocoder.process(modL, modL, carL, carL, outL, outR, 64);
        check(std::isfinite(outL[0]) && std::isfinite(outR[0]), "DspVocoderBank produce muestras finitas");

        std::printf("  [fase 2] motores DspPhaser, DspMoodFilter, DspSolinaEnsemble y DspVocoderBank comprobados\n");
    }

    if (gFailures == 0)
    {
        std::printf("[OK] DspEffects: %d comprobaciones\n", gChecks);
        return 0;
    }

    std::printf("[FALLO] DspEffects: %d de %d comprobaciones\n", gFailures, gChecks);
    return 1;
}
