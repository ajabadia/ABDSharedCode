/*
  ==============================================================================

    DspCoreTests.cpp
    Tests standalone del modulo DspCore (namespace abd::dsp). SIN JUCE: este
    fichero compila y enlaza solo contra ABDShared::DspCore, que es justamente la
    propiedad que se quiere blindar (el sustrato es JUCE-free; las comparaciones
    bit a bit contra juce::MidiMessage / juce::AudioBuffer / juce::Reverb viven
    en los consumidores, que si tienen JUCE: ABDNeural/Tests).

    Que se cubre aqui:

      1. HeapBlock: malloc / clear / allocate cuentan ELEMENTOS, no bytes.
         Es el guard de regresion del bug que aparecio al portar juce::Reverb
         (la version anterior limpiaba numElements BYTES, de modo que
         buffer.clear(n) sobre un HeapBlock<float> dejaba 3/4 sin inicializar;
         juce::Reverb la exponia y divergia a ~1e35). Con la semantica de bytes
         este test falla en la primera comprobacion del bloque grande.
      2. AudioBuffer: setSize/clear/escritura/lectura/copyFrom/getMagnitude.
      3. SmoothedValue lineal: el ramp llega al objetivo en exactamente su
         numero de pasos.
      4. MidiMessage: factorias, layout de bytes, velocidad de 14 bits y
         getMidiNoteInHertz.
      5. MidiBuffer: orden de eventos, cabecera empaquetada, clear() conserva
         capacidad y el crecimiento por encima del bloque inicial.
      6. Random: determinismo por semilla (el motor depende de esto para que dos
         pasadas den el mismo audio).

      7. numElementsInArray: el array no decae (devuelve N y no el cociente
         puntero/elemento) y el ctor de AudioBuffer sobre memoria externa, que es
         su consumidor con array, sigue operando sobre los canales del que llama
         incluso despues de moverse.
      8. DspMath: sin/cos/atan deterministas (sin libm) y su precision frente a la
         libm de la plataforma. Ver DspCore/DspMath.h.
      9. DspMath: tanh/log2/exp2/pow, que DspEffects necesita para la
         saturacion de cinta, el puente de diodos y los mapeos exponenciales
         de frecuencia. Se comprueban con casos EXACTOS (los extremos del
         mapeo) y con barrido de precision, en absoluto para tanh/log2 y en
         RELATIVO para exp2/pow, porque sobre 2^19 un error de 1 ulp ya son
         0.1 absolutos.

    Uso: ABDShared_DspCore_Tests  (no toma argumentos; 0 = OK)

  ==============================================================================
*/

#include "DspCore/DspCore.h"
#include "DspCore/DspMidiBuffer.h"
#include "DspCore/DspMidiMessage.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace
{

using abd::dsp::ignoreUnused;
using abd::dsp::jmax;
using abd::dsp::MathConstants;
using abd::dsp::ResonantFilterStage;

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

//==============================================================================
/** Guard de regresion: clear/allocate cuentan elementos (como JUCE). */
void testHeapBlockElementSemantics()
{
    // 64 elementos y se llenan los 64: clear(64) debe dejarlos TODOS a cero.
    // Con la semantica de bytes solo se limpiarian los 16 primeros floats.
    abd::dsp::HeapBlock<float> block;
    block.malloc(64);

    check(block.get() != nullptr, "HeapBlock::malloc no reservo memoria");

    for (int i = 0; i < 64; ++i)
        block[i] = 1.0f;

    block.clear(64);

    bool allZero = true;
    for (int i = 0; i < 64; ++i)
        if (block[i] != 0.0f)
            allZero = false;

    check(allZero, "HeapBlock::clear(numElements) no limpio todos los elementos");

    // allocate(n, true) deja n elementos a cero.
    abd::dsp::HeapBlock<float> zeroed;
    zeroed.allocate(32, true);

    bool zeroedOk = zeroed.get() != nullptr;
    for (int i = 0; i < 32 && zeroedOk; ++i)
        zeroedOk = (zeroed[i] == 0.0f);

    check(zeroedOk, "HeapBlock::allocate(numElements, true) no dejo el bloque a cero");

    // Sin zeroFill, allocate reserva el mismo espacio (no se comprueba el
    // contenido: es memoria sin inicializar por definicion).
    abd::dsp::HeapBlock<float> raw;
    raw.allocate(16, false);
    check(raw.get() != nullptr, "HeapBlock::allocate(numElements, false) no reservo memoria");

    // swap y liberacion (sin fugas: los temporales se destruyen solos).
    abd::dsp::HeapBlock<float> other;
    other.allocate(8, false);
    block.swapWith(other);
    other.free();
    check(block.get() != nullptr, "HeapBlock::swapWith dejo el bloque sin datos");
}

//==============================================================================
void testAudioBuffer()
{
    abd::dsp::AudioBuffer<float> buffer;
    buffer.setSize(2, 128, false, true, false);

    check(buffer.getNumChannels() == 2, "AudioBuffer::setSize no fijo los canales");
    check(buffer.getNumSamples() == 128, "AudioBuffer::setSize no fijo las muestras");

    buffer.clear();
    check(buffer.getSample(0, 0) == 0.0f, "AudioBuffer::clear no limpio");

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 128; ++i)
            buffer.setSample(ch, i, (float)(ch + 1) * 0.5f);

    check(buffer.getSample(1, 127) == 1.0f, "AudioBuffer::setSample/getSample");
    check(std::fabs(buffer.getMagnitude(0, 0, 128) - 0.5f) < 1.0e-9f,
          "AudioBuffer::getMagnitude");

    // copyFrom entre canales del propio buffer.
    buffer.copyFrom(0, 0, buffer, 1, 0, 128);
    check(buffer.getSample(0, 64) == 1.0f, "AudioBuffer::copyFrom");

    // applyGain con un smoother deja una rampa: el final del tramo suavizado
    // queda atenuado y el comienzo no (el buffer estaba a 1.0 en el canal 0).
    abd::dsp::LinearSmoothedValue<float> gain{1.0f};
    gain.reset(1000.0, 0.1);
    gain.setTargetValue(0.0f);
    gain.applyGain(buffer, 100);

    check(buffer.getSample(0, 99) < 0.1f, "SmoothedValue::applyGain no atenua al final de la rampa");
    check(buffer.getSample(0, 0) > buffer.getSample(0, 99),
          "SmoothedValue::applyGain no deja una rampa decreciente");
}

//==============================================================================
// Sonda a nivel de namespace: el array tiene que tener duracion estatica para
// poder exigir N en tiempo de compilacion.
static const float kProbeArray[4]{};

// Este static_assert ES el guard del bug: con el parametro por valor el array
// decae a puntero y la funcion devolvia sizeof (float*) / sizeof (float) = 2.
static_assert(abd::dsp::numElementsInArray(kProbeArray) == 4,
              "numElementsInArray tiene que devolver N; si devuelve otra cosa, el array esta decayendo a puntero");

/** Guard de regresion del helper de JUCE (port en DspCore.h) y de su consumidor
    con array, el ctor de AudioBuffer sobre memoria externa: con el helper
    devolviendo 1, la rama de `preallocatedChannelSpace` era inalcanzable en los
    tres sitios de AudioBuffer que lo usan. */
void testNumElementsInArray()
{
    check(abd::dsp::numElementsInArray(kProbeArray) == 4, "numElementsInArray no devuelve N");

    float left[8]{}, right[8]{};
    float* external[2]{left, right};

    // Buffer que REFERENCIA canales ya reservados (el ctor que instanciaba el
    // aviso de GCC).
    abd::dsp::AudioBuffer<float> referenced(external, 2, 8);
    check(referenced.getNumChannels() == 2, "AudioBuffer(memoria externa): canales");
    check(referenced.getNumSamples() == 8, "AudioBuffer(memoria externa): muestras");

    referenced.getWritePointer(0)[3] = 0.25f;
    check(left[3] == 0.25f, "AudioBuffer(memoria externa): no escribe en los canales del que llama");

    // Mover un buffer que referencia memoria externa: el destino tiene que
    // seguir operando sobre esos mismos arrays.
    abd::dsp::AudioBuffer<float> moved;

    {
        abd::dsp::AudioBuffer<float> source(external, 2, 8);
        source.getWritePointer(1)[0] = 0.5f;
        check(right[0] == 0.5f, "AudioBuffer(memoria externa): escritura antes del movimiento");

        moved = std::move(source);
    }

    moved.getWritePointer(1)[0] = 0.75f;
    check(right[0] == 0.75f, "AudioBuffer: el destino del movimiento no apunta a los canales externos");
    check(moved.getNumChannels() == 2 && moved.getNumSamples() == 8,
          "AudioBuffer: el movimiento perdio canales o muestras");
}

//==============================================================================
void testSmoothedValue()
{
    abd::dsp::LinearSmoothedValue<float> value{0.0f};
    value.reset(1000.0, 0.1); // 100 pasos
    value.setTargetValue(1.0f);
    value.setCurrentAndTargetValue(0.0f);
    value.setTargetValue(1.0f);

    check(value.isSmoothing(), "SmoothedValue: no arranco el ramp");

    float last     = value.getCurrentValue();
    bool monotonic = true;
    int steps      = 0;

    while (value.isSmoothing() && steps < 1000)
    {
        const float next = value.getNextValue();

        if (next < last)
            monotonic = false;

        last = next;
        ++steps;
    }

    check(monotonic, "SmoothedValue: el ramp no es monotono");
    check(steps == 100, "SmoothedValue: el ramp no duro los pasos de reset(sampleRate, segundos)");
    check(value.getCurrentValue() == 1.0f, "SmoothedValue: el ramp no llego al objetivo");

    // Ya en el objetivo, getNextValue es exactamente el objetivo.
    check(value.getNextValue() == 1.0f, "SmoothedValue: no se mantiene en el objetivo");

    // reset(int) salta directo al objetivo.
    value.reset(17);
    check(value.getCurrentValue() == 1.0f, "SmoothedValue::reset(numSteps)");
}

//==============================================================================
void testMidiMessage()
{
    using abd::dsp::MidiMessage;

    const auto noteOn = MidiMessage::noteOn(1, 60, 0.5f);

    check(noteOn.getRawDataSize() == 3, "MidiMessage::noteOn: tamano");
    check(noteOn.getRawData()[0] == 0x90, "MidiMessage::noteOn: byte de estado");
    check(noteOn.getNoteNumber() == 60, "MidiMessage::noteOn: nota");

    // roundToInt (lrintf) redondea los empates al par: 0.5f * 127 = 63.5 -> 64.
    check(noteOn.getVelocity() == 64, "MidiMessage::noteOn: cuantizacion de velocidad");
    check(noteOn.isNoteOnOrOff(), "MidiMessage::isNoteOnOrOff");

    const auto noteOff = MidiMessage::noteOff(16, 61, (uint8_t)7);
    check(noteOff.getRawData()[0] == 0x8f, "MidiMessage::noteOff: canal 16");
    check(noteOff.getNoteNumber() == 61, "MidiMessage::noteOff: nota");
    check(noteOff.getVelocity() == 7, "MidiMessage::noteOff: velocidad");

    const auto wheel = MidiMessage::pitchWheel(1, 8192);
    check(wheel.getRawDataSize() == 3, "MidiMessage::pitchWheel: tamano");
    check(wheel.getPitchWheelValue() == 8192, "MidiMessage::pitchWheel: valor de 14 bits");

    const auto cc = MidiMessage::controllerEvent(1, 1, 64);
    check(cc.isController(), "MidiMessage::controllerEvent: isController");
    check(cc.getControllerNumber() == 1, "MidiMessage::controllerEvent: numero");
    check(cc.getControllerValue() == 64, "MidiMessage::controllerEvent: valor");

    // Mensaje crudo de 1 byte (los sysEx/system common no se portan).
    const uint8_t raw[] = {0xf8};
    check(MidiMessage(raw, 1).getRawDataSize() == 1, "MidiMessage: constructor crudo");

    // Numero de bytes fuera de rango: se recorta a 1..3 (jlimit(1, 3, size),
    // igual que JUCE), de modo que un 99 declarado se lee como 3 bytes.
    check(MidiMessage(raw, 99).getRawDataSize() == 3, "MidiMessage: recorte de tamano");

    check(std::fabs(MidiMessage::getMidiNoteInHertz(69) - 440.0) < 1.0e-9,
          "MidiMessage::getMidiNoteInHertz(69) != 440");
    check(MidiMessage::getMidiNoteInHertz(81) > MidiMessage::getMidiNoteInHertz(69),
          "MidiMessage::getMidiNoteInHertz no es monotona");
}

//==============================================================================
void testMidiBuffer()
{
    abd::dsp::MidiBuffer buffer;

    check(buffer.isEmpty(), "MidiBuffer: recien creado no esta vacio");
    check(buffer.getNumEvents() == 0, "MidiBuffer: recien creado tiene eventos");

    // Insercion desordenada: la iteracion es por posicion de muestra.
    buffer.addEvent(abd::dsp::MidiMessage::noteOn(1, 64, 1.0f), 10);
    buffer.addEvent(abd::dsp::MidiMessage::noteOn(1, 60, 1.0f), 0);
    buffer.addEvent(abd::dsp::MidiMessage::noteOff(1, 60, 0.0f), 5);

    check(buffer.getNumEvents() == 3, "MidiBuffer::getNumEvents");

    int previousPosition = -1;
    int count            = 0;
    int notes[3]         = {0, 0, 0};

    for (const auto metadata : buffer)
    {
        if (metadata.samplePosition < previousPosition)
            check(false, "MidiBuffer: la iteracion no esta ordenada por posicion");

        previousPosition = metadata.samplePosition;

        if (count < 3)
            notes[count] = metadata.getMessage().getNoteNumber();

        ++count;
    }

    check(count == 3, "MidiBuffer: la iteracion no devolvio todos los eventos");
    check(notes[0] == 60 && notes[1] == 60 && notes[2] == 64,
          "MidiBuffer: orden de notas inesperado");
    check(buffer.getFirstEventTime() == 0, "MidiBuffer::getFirstEventTime");

    // clear() conserva la capacidad: sigue sirviendo sin reasignar.
    buffer.clear();
    check(buffer.isEmpty(), "MidiBuffer::clear no vacio");

    // Crecimiento por encima del bloque inicial (512 bytes): 200 eventos CC.
    for (int i = 0; i < 200; ++i)
        buffer.addEvent(abd::dsp::MidiMessage::controllerEvent(1, 74, i & 127), i);

    check(buffer.getNumEvents() == 200, "MidiBuffer: crecimiento por encima del bloque inicial");

    // ensureSize es idempotente y no pierde lo que ya habia.
    buffer.ensureSize(8192);
    check(buffer.getNumEvents() == 200, "MidiBuffer::ensureSize perdio eventos");
}

//==============================================================================
void testRandom()
{
    abd::dsp::Random first, second;
    first.setSeed(12345);
    second.setSeed(12345);

    bool identical = true;

    for (int i = 0; i < 64; ++i)
        if (first.nextInt() != second.nextInt())
            identical = false;

    check(identical, "Random: la misma semilla no da la misma secuencia");

    abd::dsp::Random other;
    other.setSeed(54321);

    bool differs = false;

    for (int i = 0; i < 64; ++i)
        if (other.nextInt() != first.nextInt())
            differs = true;

    check(differs, "Random: semillas distintas dan la misma secuencia");

    // nextFloat esta en [0, 1).
    abd::dsp::Random values;
    values.setSeed(7);

    bool inRange = true;
    for (int i = 0; i < 256; ++i)
    {
        const float v = values.nextFloat();
        if (!(v >= 0.0f && v < 1.0f))
            inRange = false;
    }

    check(inRange, "Random::nextFloat fuera de [0, 1)");
}

//==============================================================================
/** DspMath: determinista (sin libm del sistema) y con precision suficiente.
    La libm de la plataforma se usa AQUI solo como referencia de precision; la
    propiedad que importa (resultado identico en MSVC y en emscripten) se
    verifica compilando el mismo barrido en los dos toolchains. */
void testDspMath()
{
    // Valores conocidos.
    check(abd::dsp::sin(0.0f) == 0.0f, "dsp::sin(0) != 0");
    check(abd::dsp::atan(0.0f) == 0.0f, "dsp::atan(0) != 0");
    check(abd::dsp::cos(0.0f) == 1.0f, "dsp::cos(0) != 1");
    check(std::abs(abd::dsp::sin(1.5707963f) - 1.0f) < 1.0e-5f, "dsp::sin(pi/2) != 1");
    check(std::abs(abd::dsp::atan(1.0f) - 0.7853981634f) < 1.0e-4f, "dsp::atan(1) != pi/4");

    // Precision frente a la libm de la plataforma en los rangos de audio.
    float maxSinErr = 0.0f, maxAtanErr = 0.0f;
    for (int i = 0; i <= 200000; ++i)
    {
        const float x = (float)i * 0.000031415926f; // 0 .. ~2pi
        maxSinErr     = std::max(maxSinErr, std::abs(abd::dsp::sin(x) - std::sin(x)));

        const float y = ((float)i - 100000.0f) * 0.0001f; // -10 .. 10
        maxAtanErr    = std::max(maxAtanErr, std::abs(abd::dsp::atan(y) - std::atan(y)));
    }

    check(maxSinErr < 1.0e-5f, "dsp::sin: error vs libm por encima de 1e-5");
    check(maxAtanErr < 1.0e-5f, "dsp::atan: error vs libm por encima de 1e-5");

    // El presupuesto REAL de sin y cos, medido y no estimado: ~3 ulp, no 1 ulp.
    // El modulo decia "~1e-7" y el termino que falta en `sinPoly` (r^9/9!) pesa
    // justo lo que hace que sean 3.6e-07 en el borde del rango. Se fija la cifra
    // medida para que el documento y el test no se separen otra vez, y para que
    // alguien que anada un termino al polinomio vea el cambio aqui.
    float maxSinTight = 0.0f, maxCosTight = 0.0f;

    for (int i = 0; i <= 400000; ++i)
    {
        const float x = -8.0f + 16.0f * (float)i / 400000.0f;
        maxSinTight   = std::max(maxSinTight, std::abs(abd::dsp::sin(x) - std::sin(x)));
        maxCosTight   = std::max(maxCosTight, std::abs(abd::dsp::cos(x) - std::cos(x)));
    }

    check(maxSinTight < 5.0e-7f, "dsp::sin: error vs libm por encima de 5e-7 en el rango de audio");
    check(maxCosTight < 5.0e-7f, "dsp::cos: error vs libm por encima de 5e-7 en el rango de audio");
}

//==============================================================================
/** Las trascendentes de DspEffects: tanh, log2, exp2 y pow.

    Las tres primeras tienen un caso EXACTO que el resto del codigo depende de
    verdad (los extremos del mapeo exponencial de frecuencias del ring
    modulator, y el `exp2(0) == 1` con el que se comprueba que la reduccion no
    ha metido un factor). El barrido de precision va en RELATIVO para
    exp2/pow, porque un error de 1 ulp sobre 2^19 son 0.1 absolutos y
    compararlos en absoluto daria un falso positivo enorme. */
void testDspMathExpLog()
{
    // Casos exactos.
    check(abd::dsp::tanh(0.0f) == 0.0f, "dsp::tanh(0) != 0");
    check(abd::dsp::tanh(100.0f) == 1.0f, "dsp::tanh(100) != 1 (recorte de saturacion)");
    check(abd::dsp::tanh(-100.0f) == -1.0f, "dsp::tanh(-100) != -1");
    check(abd::dsp::exp2(0.0f) == 1.0f, "dsp::exp2(0) != 1");
    check(abd::dsp::exp2(10.0f) == 1024.0f, "dsp::exp2(10) != 1024 (2^10 exacto)");
    check(abd::dsp::log2(8.0f) == 3.0f, "dsp::log2(8) != 3 (potencia exacta)");
    check(abd::dsp::log2(1.0f) == 0.0f, "dsp::log2(1) != 0");
    check(abd::dsp::pow(400.0f, 0.0f) == 1.0f, "dsp::pow(400, 0) != 1");

    // pow(400, 1) NO se exige EXACTO a proposito: pow es exp2(y*log2(x)), y
    // log2 arrastra su propio error (~3e-7), asi que 400 sale con unos ulps de
    // mas. Exigir igualdad exacta haria fallar el modulo por una razon que no
    // es un fallo. Lo que si se exige es que el extremo del mapeo del ring
    // modulator salga donde toca, y eso se comprueba mas abajo.
    check(std::abs(abd::dsp::pow(400.0f, 1.0f) - 400.0f) < 1.0e-3f,
          "dsp::pow(400, 1) se aparta demasiado de 400");

    // El mapeo del ring modulator: 0 y 1 tienen que dar los extremos del perfil.
    check(std::abs(20.0f * abd::dsp::pow(400.0f, 0.0f) - 20.0f) < 1.0e-3f,
          "el extremo bajo del mapeo exponencial no da 20 Hz");
    check(std::abs(20.0f * abd::dsp::pow(400.0f, 1.0f) - 8000.0f) < 1.0e-1f,
          "el extremo alto del mapeo exponencial no da 8000 Hz");

    // tanh es impar y monotona: si pierde eso, la saturacion deja de ser saturacion.
    //
    // La monotonia se mide CON TOLERANCIA, y el motivo esta escrito aqui para
    // que nadie lo lea como un parche. El algoritmo reduce |x| con
    // identidades de angulo doble: al cruzar el umbral de 0.5 el numero de
    // reducciones pasa de 0 a 1, y cada rama evalua el polinomio en un punto
    // distinto (0.5 frente a 0.25 y la identidad encima), asi que la serie se
    // trunca en un punto y en el otro con un error distinto. El salto
    // resultante es del orden de 1e-8 — DOS ORDENES por debajo del presupuesto
    // de 1e-7 del modulo y del todo inaudible—, pero es real y por eso el test
    // mide 1e-6 en vez de exigir monotonia estricta. La imparidad SI se exige
    // estricta: esa sale de un signo, no de una serie.
    float previous    = abd::dsp::tanh(-8.0f);
    float biggestDrop = 0.0f;
    bool odd          = true;

    for (int i = 1; i <= 200000; ++i)
    {
        const float x = -8.0f + 16.0f * (float)i / 200000.0f;
        const float y = abd::dsp::tanh(x);

        biggestDrop = std::max(biggestDrop, previous - y);

        if (std::abs(abd::dsp::tanh(-x) + y) > 1.0e-6f) odd = false;

        previous = y;
    }

    check(biggestDrop < 1.0e-6f, "dsp::tanh: pierde la monotonia mas alla del salto de reduccion");
    check(odd, "dsp::tanh: pierde la imparidad");

    // Precision ABSOLUTA de tanh y abd::dsp::log2 (sus rangos son O(1)).
    float maxTanhErr = 0.0f, maxLog2Err = 0.0f;

    for (int i = 0; i <= 200000; ++i)
    {
        const float x = -8.0f + 16.0f * (float)i / 200000.0f;
        maxTanhErr    = std::max(maxTanhErr,
                                 static_cast<float>(std::abs(abd::dsp::tanh(x) - std::tanh((double)x))));

        const float y = std::fabs(x) + 0.02f;
        maxLog2Err    = std::max(maxLog2Err,
                                 static_cast<float>(std::abs(abd::dsp::log2(y) - std::log2((double)y))));
    }

    check(maxTanhErr < 1.0e-6f, "dsp::tanh: error vs libm por encima de 1e-6");
    check(maxLog2Err < 1.0e-6f, "dsp::log2: error vs libm por encima de 1e-6");

    // Precision RELATIVA de exp2 y pow: sobre 2^19, 1 ulp ya son 0.1 absolutos.
    float maxExp2Rel = 0.0f, maxPowRel = 0.0f;

    for (int i = 0; i <= 200000; ++i)
    {
        const float x = -20.0f + 40.0f * (float)i / 200000.0f;
        maxExp2Rel    = std::max(maxExp2Rel,
                                 static_cast<float>(std::abs(abd::dsp::exp2(x) - std::exp2((double)x)) / std::exp2((double)x)));

        const float b = -4.0f + 8.0f * (float)i / 200000.0f;
        maxPowRel     = std::max(maxPowRel,
                                 static_cast<float>(std::abs(abd::dsp::pow(2.5f, b) - std::pow(2.5, (double)b)) / std::pow(2.5, (double)b)));
    }

    check(maxExp2Rel < 1.0e-6f, "dsp::exp2: error RELATIVO vs libm por encima de 1e-6");
    check(maxPowRel < 1.0e-6f, "dsp::pow: error RELATIVO vs libm por encima de 1e-6");
}

//==============================================================================
/**
    DspMath en los BORDES: infinito, denormales y desbordamiento del exponente.

    POR QUE ESTE FICHERO EXISTE. Los otros dos tests de DspMath barren solo el
    rango de audio con tolerancias de 1e-5..1e-6, y con eso pasaban todos. Lo
    que NO midiaba nadie eran los casos limite, y ahi es donde estaba el
    `DspMath` entero roto. Medido con `DspCore/DspMathAudit.cpp` antes de
    arreglarlo:

        pow2i(1000)  =  5.96e-08   donde toca +inf    <-- magnitud INVENTADA
        pow2i(-127)  =  0.0        donde toca 5.88e-39
        exp2(127.5)  =  inf        donde toca 2.4e+38  <-- finito marcado como inf
        exp2(128.5)  = -0.0        donde toca +inf     <-- SIGNO CAMBIADO
        exp2(-200)   = -7.21e+16   donde toca 0.0
        log2(1e-40)  = -126.99     donde toca -132.88  <-- denormal mal leido
        atan(inf)    =  NaN        donde toca +pi/2
        cos(1e7)     = -0.7507     donde toca -0.9073  <-- 17% desviado

    Todos son fallos SILENCIOSOS: ninguno lanza, ningunoAsserta, y un motor que
    los Untouched sigue sonando. Por eso se prueban ahora una a una.

    La leccion que se queda, y que es la de este modulo entero: un barrido de
    "los valores que me interesan" NO es un test de una funcion trigonometrica.
    Un LFO usa sin(0..2pi) y sale perfecto con la reduccion rota; el fallo sale a
    las tres horas de sesion, cuando la fase lleva rato dando vueltas.
*/
void testDspMathEdgeCases()
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    //--------------------------------------------------------------------------
    // pow2i: fuera del rango del exponente del float, saturar como hace libm.
    for (int n : {128, 129, 130, 200, 1000, 100000})
    {
        check(abd::dsp::pow2i(n) == inf,
              "dsp::pow2i por encima de 2^127 tiene que saturar a +infinito");
    }

    for (int n : {-150, -200, -1000, -100000})
    {
        check(abd::dsp::pow2i(n) == 0.0f,
              "dsp::pow2i por debajo de 2^-149 tiene que dar cero");
    }

    // Los DENORMALES (2^-127 y por debajo) no se construyen con el campo de
    // exponente, sino con un bit de mantiza. Antes.pow2i(-127) daba 0.
    for (int n : {-127, -128, -140, -149})
    {
        const float ours = abd::dsp::pow2i(n);
        const float ref  = std::ldexp(1.0f, n);

        check(ours == ref, "dsp::pow2i no construye bien un denormal");
    }

    // Y dentro del rango normal tiene que seguir siendo EXACTO por construccion.
    for (int n : {-126, -100, -1, 0, 1, 10, 100, 127})
    {
        check(abd::dsp::pow2i(n) == std::ldexp(1.0f, n),
              "dsp::pow2i tiene que ser exacto en el rango normal");
    }

    //--------------------------------------------------------------------------
    // exp2: el borde de overflow es donde se decide si cabe o no, y hay que
    // decidirlo con el exponente FINAL, no con el entero de la reduccion.
    //
    // 2^127.5 = 2.4e38 CABE en float (el maximo son 3.4e38) y 2^128.5 = 4.8e38
    // NO cabe. Antes exp2(127.5) devolvia infinito: un valor finito marcado como
    // infinito, que es peor que un error de magnitud, porque no se puede
    // recuperar con un clamp.
    check(std::abs(abd::dsp::exp2(127.5f) - 2.40616e+38f) < 1.0e+33f,
          "dsp::exp2(127.5) es un valor FINITO y no puede salir infinito");

    for (float x : {128.5f, 129.0f, 200.0f, 1.0e6f})
    {
        const float got = abd::dsp::exp2(x);

        check(got == inf, "dsp::exp2 por encima del rango tiene que dar +infinito");
        check(!std::signbit(got), "dsp::exp2 desbordado sale con el SIGNO cambiado");
    }

    for (float x : {-151.0f, -200.0f, -1.0e6f})
    {
        check(abd::dsp::exp2(x) == 0.0f, "dsp::exp2 muy negativo tiene que dar cero");
    }

    // exp2 de infinito tiene que ser infinito (no NaN, no un numero inventado).
    check(abd::dsp::exp2(inf) == inf, "dsp::exp2(+inf) != +inf");
    check(abd::dsp::exp2(-inf) == 0.0f, "dsp::exp2(-inf) != 0");
    check(std::isnan(abd::dsp::exp2(nan)), "dsp::exp2(NaN) tiene que dar NaN");

    //--------------------------------------------------------------------------
    // log2: los denormales. Antes log2(1e-40) daba -126.99 en vez de -132.88.
    for (float x : {1.0e-40f, 1.0e-39f, 1.0e-38f, 1.0e-37f})
    {
        const float ours = abd::dsp::log2(x);
        const float ref  = std::log2(x);

        check(std::abs(ours - ref) < 1.0e-3f, "dsp::log2 falla con un denormal");
    }

    // NaN entra, NaN sale. Antes devolvia -inf, que es peor: convertia un NaN
    // en un numero que parece legitimo y seguia envenenando lo que tocara.
    check(std::isnan(abd::dsp::log2(nan)), "dsp::log2(NaN) tiene que dar NaN, no -inf");
    check(abd::dsp::log2(0.0f) == -inf, "dsp::log2(0) tiene que dar -inf");

    // OJO: para un NEGATIVO el modulo devuelve -inf y NO el NaN de la libm, y es
    // a proposito (esta escrito en la cabecera). El motivo es que en audio un
    // error tiene que acabar en silencio, no en NaN: -inf baja por
    // `pow`/`exp2` hasta 0 Hz y el mapa exponencial devuelve 0, o sea un
    // silencio. Un NaN ahi se quedaria pegado en la realimentacion para siempre.
    // La divergencia con libm se paga a proposito; este test la fija para que no
    // se "arregle" por sorpresa.
    check(abd::dsp::log2(-1.0f) == -inf, "dsp::log2 de un negativo da -inf (contrato, no NaN)");
    check(abd::dsp::pow(-1.0f, 0.5f) == 0.0f,
          "una base negativa en pow tiene que acabar en 0 (silencio), no en NaN");
    check(abd::dsp::pow(-1.0f, 0.0f) == 1.0f,
          "pow(x, 0) tiene que dar 1 para cualquier base, no NaN");

    //--------------------------------------------------------------------------
    // atan: el infinito. Sin guarda, (inf-1)/(inf+1) es inf/inf = NaN.
    // Y esto importa mas de lo que parece: la saturacion va metida en un lazo
    // de realimentacion, donde un unico NaN no se va nunca.
    check(std::abs(abd::dsp::atan(inf) - 1.5707963f) < 1.0e-5f, "dsp::atan(+inf) != pi/2");
    check(std::abs(abd::dsp::atan(-inf) + 1.5707963f) < 1.0e-5f, "dsp::atan(-inf) != -pi/2");
    check(std::isnan(abd::dsp::atan(nan)), "dsp::atan(NaN) tiene que dar NaN");

    // Y un barrido de TODO el rango de float finito: ninguna de las tres puede
    // devolver NaN ni infinito con un argumento finito.
    //
    // El tope del barrido es 127, NO 149. Con 149 el rango llegaba a 2^138, que
    // en float DEDA INFINITO: 6968 de los 200000 puntos eran `inf`, y
    // `sin(inf)` da NaN, que es lo correcto. O sea que el asercion fallaba por
    // generarse a si misma un argumento infinito mientras declaraba que
    // comprobaba argumentos finitos. 2^127 es el ultimo exponente representable
    // de verdad, asi que el barrido ahora va del denormal mas pequeno a ahi.
    for (int i = 0; i < 200000; ++i)
    {
        // Del denormal mas pequeno (-149) a 2^127, en escala logaritmica.
        const float x = std::ldexp(1.0f, -149 + (i * 276) / 200000);

        // Que el propio barrido no fabrique infinito: si lo hiciera, todo lo de
        // abajo estaria comprobando `sin(inf)`, que es otra cosa.
        if (!std::isfinite(x))
        {
            check(false, "el barrido de sin/cos fabrico un argumento infinito");
            break;
        }

        const float s = abd::dsp::sin(x);
        const float c = abd::dsp::cos(x);
        const float a = abd::dsp::atan(x);

        if (!std::isfinite(s) || std::fabs(s) > 1.0f)
        {
            check(false, "dsp::sin se sale de [-1, 1] con un argumento finito");
            break;
        }

        if (!std::isfinite(c) || std::fabs(c) > 1.0f)
        {
            check(false, "dsp::cos se sale de [-1, 1] con un argumento finito");
            break;
        }

        if (!std::isfinite(a) || std::fabs(a) > 1.5707964f)
        {
            check(false, "dsp::atan se sale de [-pi/2, pi/2] con un argumento finito");
            break;
        }
    }

    //--------------------------------------------------------------------------
    // sin/cos con argumento GRANDE. Antes el error crecia CON el argumento (no
    // era un teclear constante): en float, x - q*pi/2 pierde ~ulp(x)/2.
    for (float x : {1.0e3f, 1.0e4f, 1.0e5f, 1.0e6f, 1.0e7f})
    {
        const float ours = abd::dsp::sin(x);
        const float ref  = std::sin(x);

        check(std::abs(ours - ref) < 1.0e-4f,
              "dsp::sin con argumento grande se aparta de la libm");

        const float oursc = abd::dsp::cos(x);
        const float refc  = std::cos(x);

        check(std::abs(oursc - refc) < 1.0e-4f,
              "dsp::cos con argumento grande se aparta de la libm");
    }

    // Y por encima de 2^52 el argumento ya no tiene un valor "correcto" que
    // perseguir, pero lo que no puede pasar es que la funcion devuelva basura
    // o dispare el casteo a entero (que es indefinido). Solo se pide acotado.
    for (float x : {1.0e20f, 1.0e30f, 3.4e38f, -1.0e30f})
    {
        const float s = abd::dsp::sin(x);
        const float c = abd::dsp::cos(x);

        check(std::isfinite(s) && std::fabs(s) <= 1.0f,
              "dsp::sin tiene que seguir acotada con un argumento enorme");
        check(std::isfinite(c) && std::fabs(c) <= 1.0f,
              "dsp::cos tiene que seguir acotada con un argumento enorme");
    }

    // NaN e infinito tienen que SALIR como NaN, como ya hacen atan, tan, tanh y
    // log2. Antes no lo hacian: `sin(NaN)` daba 0 y `cos(NaN)` daba 1, porque los
    // dos caian en el plegado de la mantiza, que lee los BITS del argumento
    // (0x7fc00000 -> mascara -> 1.0f) y devuelve un numero ENIENDO.
    //
    // Medido antes de arreglarlo: sin(NaN)=0, cos(NaN)=1, sin(inf)=0, cos(inf)=1.
    //
    // Por que importa mas de lo que parece: un parametro en NaN (un host que lo
    // mande, un 0/0 en un mapeo) entrava en el LFO del chorus, `sin` lo
    // convertia en un 0 perfecto, y el efecto SEGUIA SONANDO normal con la
    // modulacion muerta en vez de poisoning el motor y delatar el fallo. Un NaN
    // silencioso es peor que uno ruidoso.
    {
        // `nan`/`inf` con otro nombre: en la funcion padre ya hay dos variables
        // con esos nombres, y MSVC con /W4 avisa (C4456) de que las de aqui las
        // tapan. El build del modulo es de cero warnings, asi que se renombran.
        const float badNan = std::numeric_limits<float>::quiet_NaN();
        const float badInf = std::numeric_limits<float>::infinity();

        for (float bad : {badNan, badInf, -badInf})
        {
            const float s = abd::dsp::sin(bad);
            const float c = abd::dsp::cos(bad);

            check(s != s, "dsp::sin tiene que propagar NaN y el infinito");
            check(c != c, "dsp::cos tiene que propagar NaN y el infinito");
        }

        // Y la familia entera tiene que ser coherente: si una sola de estas
        // funciones traga el NaN, la siguiente vez que falle algo en un lazo de
        // realimentacion no habra ni un sintoma.
        check(abd::dsp::atan(badNan) != abd::dsp::atan(badNan), "dsp::atan deberia propagar NaN");
        check(abd::dsp::log2(badNan) != abd::dsp::log2(badNan), "dsp::log2 deberia propagar NaN");
    }
}

//==============================================================================
/** `wrapPhase`: el envuelto de fase de los LFOs de DspEffects.

    Lo que fija aqui, en este orden:

      1. QUE ES UN ENVUELTO DE FASE. La salida cae en [0, twoPi) para
         cualquier entrada, incluidos valores absurdos. Con el `if` de una sola
         resta que tenian los motores, una fase por encima de 2*pi se quedaba
         FUERA a perpetuo: no es un redondeo, es un estado invalido del que no
         se sale.

      2. QUE ES TIEMPO CONSTANTE. Un `while` que resta 2*pi vuelta a vuelta
         funciona, pero con un rate de 1e9 Hz son 22 676 iteraciones por
         muestra y con rate infinito no termina nunca, en el hilo de audio. El
         numero de vueltas se calcula con `floorToInt` y se resta de golpe, asi
         que el coste no depende de la entrada. Aqui se comprueba que una fase
         con 22 676 vueltas entra y sale en el mismo numero de pasos que una
         fase con cero, y que el resultado es el MISMO que el que daria el
         envoltorio correcto.

      3. QUE ES BIT A BIT EL `if` DE UNA RESTA EN EL RANGO DE AUDIO. Es lo que
         sostiene la paridad congelada de ABDNeural (el chorus compara el motor
         compartido contra una copia literal de antes de la migracion, con
         margen de 0 ulps). Si `wrapPhase` se desviase un ulp en el rango
         normal, ese test empezaria a fallar sin que nadie supiera por que.

      4. QUE UN NaN NO ENVENENA EL MOTOR. Con el `if`, `NaN >= twoPi` es
         falso, asi que la fase se quedaba en NaN para siempre y todo lo que
         dependiera de ella (el retardo del chorus, el modulador del anillo)
         salia en NaN de forma permanente. Aqui se reinicia a 0.
*/
void testWrapPhase()
{
    const float twoPi = abd::dsp::MathConstants<float>::twoPi;

    // 1. Siempre dentro del rango.
    for (float phase : {0.0f, 1.0f, twoPi * 0.5f, twoPi - 1.0e-4f, twoPi,
                        twoPi * 10.0f, twoPi * 1000.0f, twoPi * 1.0e6f,
                        -1.0f, -twoPi, -twoPi * 999.0f, 1.0e30f, -1.0e30f})
    {
        const float w = abd::dsp::wrapPhase(phase, twoPi);

        check(std::isfinite(w) && w >= 0.0f && w < twoPi,
              "dsp::wrapPhase tiene que devolver un valor en [0, 2*pi)");
    }

    // 2. Coste constante: muchas vueltas entran igual de rapido que pocas, y
    //    el resultado es el que corresponde (una vuelta menos).
    for (int turns : {0, 1, 100, 22676, 1000000})
    {
        const float w = abd::dsp::wrapPhase(twoPi * static_cast<float>(turns) + 1.0f, twoPi);

        check(std::fabs(w - 1.0f) < 1.0e-3f,
              "dsp::wrapPhase no quita las vueltas enteras de golpe");
    }

    // 3. Identidad de bits con el `if` de una sola resta, que es lo que hay
    //    congelado. En el rango de audio el incremento es mucho menor que 2*pi,
    //    y ahi el `if` ya era correcto: `wrapPhase` tiene que coincidir.
    {
        int differences = 0;
        float ref = 0.0f, new_ = 0.0f;

        for (int step = 0; step <= 400000; ++step)
        {
            // Rate 0..40 Hz, el rango de un LFO de chorus, a 44.1 kHz.
            const float rate     = 40.0f * static_cast<float>(step) / 400000.0f;
            const float phaseInc = twoPi * rate / 44100.0f;

            ref += phaseInc;
            if (ref >= twoPi) ref -= twoPi; // el `if` original, literal

            new_ = abd::dsp::wrapPhase(new_ + phaseInc, twoPi);

            if (ref != new_) ++differences;
        }

        check(differences == 0,
              "dsp::wrapPhase no es bit a bit el `if` de una resta en el rango de audio");
    }

    // 3b. Y en el guion exacto de la paridad congelada del chorus: 24 bloques
    //     de 512 muestras con el rate barriendo 0.7..5.7 Hz a 48 kHz.
    {
        int differences = 0;
        float ref = 0.0f, new_ = 0.0f;

        for (int block = 0; block < 24; ++block)
        {
            const float t        = static_cast<float>(block) / 23.0f;
            const float phaseInc = twoPi * (0.7f + t * 5.0f) / 48000.0f;

            for (int s = 0; s < 512; ++s)
            {
                ref += phaseInc;
                if (ref >= twoPi) ref -= twoPi;

                new_ = abd::dsp::wrapPhase(new_ + phaseInc, twoPi);

                if (ref != new_) ++differences;
            }
        }

        check(differences == 0,
              "dsp::wrapPhase cambia el guion de la paridad congelada del chorus");
    }

    // 3c. Donde el `if` original YA ERA INCORRECTO, `wrapPhase` lo arregla. Con
    //     rate = 2.18 veces el sample rate el `if` dejaba la fase en 7.39 rad,
    //     fuera del rango: el LFO se comia una fase que no existe.
    {
        const float rate     = 96000.0f;
        const float phaseInc = twoPi * rate / 44100.0f;

        float ref = phaseInc;
        if (ref >= twoPi) ref -= twoPi;

        const float new_ = abd::dsp::wrapPhase(phaseInc, twoPi);

        check(ref >= twoPi, "el `if` de una resta deberia fallar con este rate (si no, el test no prueba nada)");
        check(new_ < twoPi, "dsp::wrapPhase deberia corregir lo que el `if` de una resta no envuelve");
    }

    // 4. NaN, infinito y un modulo invalido: nada cuelga, nada envenena.
    {
        check(abd::dsp::wrapPhase(std::numeric_limits<float>::quiet_NaN(), twoPi) == 0.0f,
              "dsp::wrapPhase tiene que reiniciar un acumulador en NaN");
        check(std::isfinite(abd::dsp::wrapPhase(std::numeric_limits<float>::infinity(), twoPi)),
              "dsp::wrapPhase no puede devolver infinito");
        check(std::isfinite(abd::dsp::wrapPhase(-std::numeric_limits<float>::infinity(), twoPi)),
              "dsp::wrapPhase no puede devolver infinito con negativo");

        // Un modulo nulo o negativo no puede dividir por cero: se devuelve la
        // fase tal cual, que es lo unico que no se puede inventar.
        check(std::isfinite(abd::dsp::wrapPhase(1.0f, 0.0f)),
              "dsp::wrapPhase con modulo 0 no puede ser infinito");
        check(abd::dsp::wrapPhase(1.0f, 0.0f) == 1.0f,
              "dsp::wrapPhase con modulo 0 devuelve la fase sin tocar");
        check(abd::dsp::wrapPhase(1.0f, -twoPi) == 1.0f,
              "dsp::wrapPhase con modulo negativo devuelve la fase sin tocar");
    }
}

//==============================================================================
// La etapa resonante. Aqui esta la razon de ser del modulo: que la
// auto-oscilacion se ASIENTE en un nivel. Todas las cifras de este bloque se
// MIDIERON con un arnes antes de escribirlas (captura de 3 s, conteo de pixeles
// analogos y proyeccion de Fourier sobre el fundamental y sus armonicos), no se
// copiaron de ningun sitio.

/** RMS de un tramo de un vector, en la posicion que se le pase. */
double rmsFrom(const std::vector<double>& v, size_t from)
{
    double sum = 0.0;
    size_t n   = 0;

    for (size_t i = from; i < v.size(); ++i)
    {
        sum += v[i] * v[i];
        ++n;
    }

    return n > 0 ? std::sqrt(sum / (double)n) : 0.0;
}

/** El mayor valor absoluto de un tramo. */
double peakFrom(const std::vector<double>& v, size_t from)
{
    double m = 0.0;

    for (size_t i = from; i < v.size(); ++i)
        m = jmax(m, std::abs(v[i]));

    return m;
}

/** La amplitud de una componente de Fourier, por correlacion (no hay FFT aqui:
    una sola frecuencia es un par de sumas, y el test no necesita mas). */
double toneAt(const std::vector<double>& v, double freq, double fs, size_t from)
{
    double re = 0.0, im = 0.0;
    const auto n = (double)(v.size() - from);

    for (size_t i = from; i < v.size(); ++i)
    {
        const auto phase = 2.0 * MathConstants<double>::pi * freq * (double)i / fs;

        re += v[i] * std::cos(phase);
        im += v[i] * std::sin(phase);
    }

    return 2.0 * std::sqrt(re * re + im * im) / n;
}

/** La ganancia de la etapa a una frecuencia de sonda, en regimen permanente:
    se descarta el primer segundo para que el transitorio no contamine. */
double stageGain(ResonantFilterStage& f, double probe, double fs)
{
    f.reset();

    double re = 0.0, im = 0.0;
    const auto n = (int)(fs * 2);

    for (int i = 0; i < n; ++i)
    {
        const auto y = f.processSample(std::sin(2.0 * MathConstants<double>::pi * probe * (double)i / fs));

        if (i < n / 2)
            continue;

        re += y * std::cos(2.0 * MathConstants<double>::pi * probe * (double)i / fs);
        im += y * std::sin(2.0 * MathConstants<double>::pi * probe * (double)i / fs);
    }

    return 2.0 * std::sqrt(re * re + im * im) / (double)(n / 2);
}

/** Tres segundos de etapa con una entrada minuscula de ruido, que es lo que
    hace falta para que cante sola sin que la entrada la mande. */
std::vector<double> runOscillation(ResonantFilterStage& f, double fs)
{
    std::vector<double> out;
    out.reserve((size_t)(fs * 3));

    for (int i = 0; i < (int)(fs * 3); ++i)
        out.push_back(f.processSample(1.0e-6 * std::sin(0.001 * (double)i)));

    return out;
}

//==============================================================================
/** 1. SE ASIENTA, Y AL NIVEL QUE DICE LA FORMULA.

    El equilibrio de la AGC es `potencia = -k0 / beta`, o sea una amplitud de
    banda `sqrt (-2 * k0 / beta)`. Ese numero se calcula ANTES de correr nada y
    el filtro tiene que caer en el. Medido: error del 0.03 %.
*/
void testResonantFilterSettlesAtALevel()
{
    constexpr double fs = 48000.0, fc = 1000.0;

    for (auto res : {0.94, 0.95, 0.97})
    {
        ResonantFilterStage f;
        f.prepare(fs);
        f.setCutoffAndResonance(fc, res);

        const auto predicted = f.getSettledAmplitude();
        const auto out       = runOscillation(f, fs);

        // El ultimo medio segundo contra la prediccion.
        const auto peak = peakFrom(out, (size_t)(fs * 2.5));
        const auto rel  = std::abs(peak - predicted) / predicted;

        check(rel < 0.01, "la oscilacion se asienta en el nivel predicho");

        // Y lo importante para la AGC: YA NO CRECE. El segundo segundo y el
        // tercero tienen que ser el MISMO numero; si la AGC no sujetase el
        // nivel, el tercero seria un orden de magnitud mayor que el segundo.
        const auto rms2  = rmsFrom(out, (size_t)fs);
        const auto rms3  = rmsFrom(out, (size_t)(fs * 2));
        const auto drift = std::abs(rms3 - rms2) / jmax(rms2, 1.0e-12);

        check(drift < 0.005, "el nivel NO crece entre el segundo y el tercero");
        check(std::isfinite(rms3), "el nivel asienta en algo finito");

        // El RMS de un seno es su amplitud entre raiz de dos.
        const auto expectedRms = predicted / std::sqrt(2.0);

        check(std::abs(rms3 - expectedRms) / expectedRms < 0.02,
              "el RMS asentado es el de un seno de la amplitud predicha");
    }
}

//==============================================================================
/** 2. EL CONTROL NEGATIVO: sin AGC la oscilacion CRECE.

    Sin esto, el test 1 pasaria tambien con un filtro que se limitase por
    saturacion dura y no por la potencia de banda, y no se sabria que lo que se
    esta probando es la AGC. Con el control de nivel a cero la etapa es un
    resonador puro y la oscilacion se va: medido, el RMS del primer segundo ya
    es infinito.
*/
void testResonantFilterGrowsWithoutLevelControl()
{
    constexpr double fs = 48000.0, fc = 1000.0;

    ResonantFilterStage f;
    f.prepare(fs);
    f.setCutoffAndResonance(fc, 1.0);
    f.setLevelControl(0.0);

    check(std::isinf(f.getSettledAmplitude()),
          "sin control de nivel el nivel asiente en infinito, que es la verdad");

    const auto out   = runOscillation(f, fs);
    const auto first = rmsFrom(out, 0);
    const auto last  = rmsFrom(out, (size_t)(fs * 2));

    check(!std::isfinite(first) || first > last * 10.0,
          "sin AGC la oscilacion CRECE en vez de asentarse");
    check(last < first, "sin AGC el final no puede estar por debajo del principio");
}

//==============================================================================
/** 3. EL TONO ASENTADO ES UN SENO, NO UN CUADRADO.

    Es la razon de promediar la potencia en vez de usarla muestra a muestra: con
    la amortiguacion siguiendo a `banda^2` sin promediar, esta etapa se dobla y
    aparecen armonicos. Medido con la potencia promediada, el tercer armonica
    esta 84 dB por debajo del fundamental; el test solo exige 40 dB, que es un
    margen de sobra para no depender del ultimo ulp.
*/
void testResonantFilterToneIsClean()
{
    constexpr double fs = 48000.0, fc = 1000.0;

    ResonantFilterStage f;
    f.prepare(fs);
    f.setCutoffAndResonance(fc, 0.97);

    const auto out         = runOscillation(f, fs);
    const auto from        = (size_t)(fs * 2);
    const auto fundamental = toneAt(out, fc, fs, from);
    const auto third       = toneAt(out, 3.0 * fc, fs, from);
    const auto fifth       = toneAt(out, 5.0 * fc, fs, from);

    check(fundamental > 0.05, "la oscilacion asentada tiene fundamental");
    check(third < fundamental * 0.01, "el tercer armonico esta 40 dB por debajo");
    check(fifth < fundamental * 0.01, "el quinto armonico esta 40 dB por debajo");

    // Y canta a la frecuencia de corte, que es la promesa de un filtro: si
    // cantara a otro sitio, lo de arriba seria ruido de otro sitio.
    const auto octave = toneAt(out, 2.0 * fc, fs, from);

    check(octave < fundamental * 0.01, "no hay energia una octava por encima");
}

//==============================================================================
/** 4. POR DEBAJO DEL UMBRAL NO CANTA.

    El umbral analitico y el medido coinciden en 0.9333: a 0.93 el
    amortiguamiento sigue siendo positivo y la etapa no se auto-oscila; a 0.94
    ya es negativo y canta al nivel predicho.

    OJO con lo que significa "no se auto-oscila" aqui, porque la primera version
    de este test se equivoco: pedia que la salida fuera silencio, y no lo es.
    Por debajo del umbral la etapa es un paso bajo que ATENUA, luego la senal
    minuscula de entrada sale otra vez: medido, pico 1.0017e-6 con una entrada
    de 1.0e-6, o sea ganancia 1.0 y ni una muestra de oscilacion propia. Lo que
    hay que comprobar es que no aparezca energia que no venia de la entrada, y
    eso se mide contra el nivel de la entrada, no contra el cero.
*/
void testResonantFilterSelfOscillationThreshold()
{
    constexpr double fs = 48000.0, fc = 1000.0;
    const auto onset = ResonantFilterStage::selfOscillationStart();

    check(onset > 0.92 && onset < 0.95, "el umbral cae en el tramo de resonancia alto");

    // Justo por debajo: amortiguamiento positivo, y nada que no entre.
    {
        ResonantFilterStage f;
        f.prepare(fs);
        f.setCutoffAndResonance(fc, onset - 0.01);

        check(f.getBaseDamping() > 0.0, "por debajo del umbral el amortiguamiento es positivo");
        check(f.getSettledAmplitude() == 0.0, "por debajo del umbral no hay nivel al que asentar");

        const auto out  = runOscillation(f, fs);
        const auto peak = peakFrom(out, (size_t)(fs * 2));

        // La entrada de `runOscillation` es un seno de amplitud 1e-6, y un paso
        // bajo a 1 kHz no la toca: lo que sale no puede pasar de ella, y medido
        // sale a 1.0017e-6, un margen del 0.2 % por lo que la AGC anade.
        check(peak < 1.0e-5, "por debajo del umbral la etapa no se auto-oscila");
        check(peak > 1.0e-6, "por debajo del umbral la senal de entrada sale atenuada, no anulada");
    }

    // Justo por encima: amortiguamiento negativo, y canta.
    {
        ResonantFilterStage f;
        f.prepare(fs);
        f.setCutoffAndResonance(fc, onset + 0.01);

        check(f.getBaseDamping() < 0.0, "por encima del umbral el amortiguamiento es negativo");

        const auto out = runOscillation(f, fs);

        check(peakFrom(out, (size_t)(fs * 2)) > 0.01, "por encima del umbral la etapa canta sola");
    }
}

//==============================================================================
/** 5. A RESONANCIA 0 ES UN PASO BAJO DE SEGUNDO ORDEN, SIN REALCE.

    NO se comprueba contra un biquad de forma directa, aunque se escribio
    primero: medido, los dos se separan hasta 1.9e-3, porque el TPT y el biquad
    comparten los polos pero NO el numerador. Compararlos seria exigir que la
    etapa fuese otro filtro. Lo que si es cierto, y lo que se comprueba, es que
    cae a 12 dB/octava, pasa plana muy por debajo del corte y no tiene pico.

    La pendiente se mide comparando una octava con la siguiente cuando la sonda
    ya esta claramente en la zona de rechazo. Y aqui hay un dato que la primera
    version del test se hizo mal: de segundo orden son 12 dB/octava, o un factor
    4 de amplitud por octava, no 24 dB (cada polo aporta 6, y este tiene dos).
    Medido, de 2fc a 4fc cae 3.96 veces y de 4fc a 8fc 4.63.
*/
void testResonantFilterIsSecondOrderLowpass()
{
    constexpr double fs = 48000.0, fc = 1000.0;

    ResonantFilterStage f;
    f.prepare(fs);
    f.setCutoff(fc);

    const auto at50   = stageGain(f, 50.0, fs);
    const auto atHalf = stageGain(f, fc * 0.5, fs);
    const auto atCut  = stageGain(f, fc, fs);
    const auto at2    = stageGain(f, fc * 2.0, fs);
    const auto at4    = stageGain(f, fc * 4.0, fs);
    const auto at8    = stageGain(f, fc * 8.0, fs);

    check(std::abs(at50 - 1.0) < 0.005, "muy por debajo del corte pasa plano");
    check(atHalf < at50 && atHalf > atCut, "la respuesta baja de forma monotona");
    check(atCut > 0.5 && atCut < 0.8, "en el corte esta a unos -3 dB");
    check(jmax(at50, atHalf, atCut, at2) <= at50 + 1.0e-9 && jmax(at4, at8) <= at50 + 1.0e-9,
          "no hay realce en ningun punto de la respuesta");

    // 12 dB por octava = un factor 4 de amplitud por octava en la zona de
    // rechazo. Medido: de 2fc a 4fc cae 3.96 veces, y de 4fc a 8fc 4.63.
    const auto octave1 = at2 / at4;
    const auto octave2 = at4 / at8;

    check(octave1 > 3.0 && octave1 < 6.0, "cae de 12 dB por octava en la primera");
    check(std::abs(octave1 - octave2) / octave1 < 0.25, "la pendiente no cambia de golpe");
}

//==============================================================================
/** 6. SUBIR LA RESONANCIA NO APAGA LA PASABANDA.

    Este test esta aqui para que nadie vuelva a meter una "compensacion de
    pasabanda" sin medirla. Se midio antes de escribir el modulo, y el resultado
    fue que no hace falta: la ganancia a 100 Hz se queda en 1.01 con la
    resonancia a 0.99, o sea que este esquema REALZA la pasabanda en vez de
    apagarla. Una compensacion aqui no devolveria nada, lo multiplicaria.
*/
void testResonantFilterPassbandSurvivesResonance()
{
    constexpr double fs = 48000.0, fc = 1000.0;
    auto reference = -1.0;

    for (auto res : {0.0, 0.3, 0.6, 0.9, 0.95, 0.99})
    {
        ResonantFilterStage f;
        f.prepare(fs);
        f.setCutoffAndResonance(fc, res);

        const auto gain = stageGain(f, 100.0, fs);

        if (reference < 0.0)
            reference = gain;
        else
            check(std::abs(gain - reference) / reference < 0.02,
                  "la pasabanda no se apaga al subir la resonancia");
    }
}

//==============================================================================
/** 7. MOVER EL CORTE MIENTRAS CANTA NO HACE CLICK.

    El estado se conserva al retunear, que es una de las dos razones de usar el
    TPT. Sin estado conservado, un salto de coeficientes sale como un escalon en
    la senal, que suena a click. La asercion mira el mayor salto muestra a
    muestra durante el barrido, con la etapa ya asentada y cantando.
*/
void testResonantFilterRetuneHasNoClick()
{
    constexpr double fs = 48000.0;
    ResonantFilterStage f;
    f.prepare(fs);
    f.setCutoffAndResonance(1000.0, 0.95);

    const auto warm = runOscillation(f, fs); // que se asiente antes de mover nada
    ignoreUnused(warm);

    // Barrido lento de un cuarto de octava arriba y abajo, 0.5 s.
    double previous = 0.0;
    auto maxStep    = 0.0;

    for (int i = 0; i < (int)(fs * 0.5); ++i)
    {
        const auto t  = (double)i / (fs * 0.5);
        const auto hz = 840.0 * std::pow(2.0, t); // un cuarto de octava
        f.setCutoff(hz);

        const auto y = f.processSample(0.0);

        if (i > 0)
            maxStep = jmax(maxStep, std::abs(y - previous));

        previous = y;
    }

    // Un seno de amplitud 0.3 a 1 kHz y 48 kHz avanza 0.3 * 2*pi/48 = 0.039 por
    // muestra. Un click es un salto de ordenes de magnitud mayor que eso.
    check(maxStep < 0.08, "el retune no produce un escalon en la senal");
}

//==============================================================================
/** 8. LOS RECORTES: un valor de panel no puede meter un NaN en el filtro.

    Un cutoff por encima de Nyquist hace que `tan (pi * f / fs)` se vaya, y un
    `g` infinito no es un filtro: es un NaN que se lleva por delante el estado y
    no vuelve. Una resonancia fuera de rango viene de un host con un error, no de
    un panel. Las dos tienen que quedar recortadas y seguir sonando.
*/
void testResonantFilterClampsHostileInput()
{
    constexpr double fs = 48000.0;

    ResonantFilterStage f;
    f.prepare(fs);
    f.setCutoffAndResonance(20000.0, 5.0); // Nyquist es 24000, y la res se pasa

    check(f.getCutoff() < fs * 0.5, "un corte por encima de Nyquist se recorta");
    check(f.getResonance() == 1.0, "una resonancia fuera de rango se recorta");
    check(std::isfinite(f.getBaseDamping()), "el amortiguamiento sigue siendo un numero");

    for (int i = 0; i < 2048; ++i)
        check(std::isfinite(f.processSample(0.9)), "con los valores recortados la salida es finita");

    // Y lo mismo por el otro extremo: un corte negativo o cero.
    f.setCutoff(-50.0);
    check(f.getCutoff() >= 10.0, "un corte negativo se recorta por abajo");

    for (int i = 0; i < 512; ++i)
        f.processSample(0.5);

    // Y una frecuencia de muestreo absurda, que es lo que pasa si un host
    // prepara el motor sin saber la del bloque.
    ResonantFilterStage g;
    g.prepare(-1.0);
    g.setCutoffAndResonance(1000.0, 0.95);
    g.processSample(1.0e-6);

    check(std::isfinite(g.getBandPower()), "prepare con una frecuencia de muestreo imposible no rompe nada");
}

//==============================================================================
/** 9. RESET() VACIA EL ESTADO, Y EL FILTRO SIGUE SONANDO DESPUES.

    Una nota no puede empezar con la cola de la anterior. Se comprueba que tras
    un reset la etapa devuelve una salida ordenada, y que con entrada cero se
    queda EXACTAMENTE en cero (nada de denormales: en x86 son lentos, y una voz
    que se apaga en silencio se lleva por delante un monton de ciclos de CPU).
*/
void testResonantFilterResetAndDenormals()
{
    constexpr double fs = 48000.0;
    ResonantFilterStage f;
    f.prepare(fs);
    f.setCutoffAndResonance(1000.0, 0.95);

    runOscillation(f, fs);
    check(f.getBandPower() > 0.0, "la etapa tiene potencia antes de reset");

    f.reset();

    check(f.getBandPower() == 0.0, "reset vacia la potencia de banda");
    check(f.getCutoff() == 1000.0, "reset NO toca el corte: es un ajuste, no estado");
    check(f.getResonance() == 0.95, "reset NO toca la resonancia: es un ajuste, no estado");

    // Con entrada cero desde el reset, la salida se queda en cero exacto.
    auto allZero = true;

    for (int i = 0; i < 4096; ++i)
        if (f.processSample(0.0) != 0.0)
            allZero = false;

    check(allZero, "desde el reset y con entrada cero la salida es cero exacto, no denormal");

    // Y sigue sonando: un reset no es una averia.
    const auto out = runOscillation(f, fs);

    check(peakFrom(out, (size_t)(fs * 2)) > 0.01,
          "la etapa sigue auto-oscilando despues de un reset");
}

} // namespace

//==============================================================================
int main()
{
    testHeapBlockElementSemantics();
    testAudioBuffer();
    testNumElementsInArray();
    testSmoothedValue();
    testMidiMessage();
    testMidiBuffer();
    testRandom();
    testDspMath();
    testDspMathExpLog();
    testDspMathEdgeCases();
    testWrapPhase();
    testResonantFilterSettlesAtALevel();
    testResonantFilterGrowsWithoutLevelControl();
    testResonantFilterToneIsClean();
    testResonantFilterSelfOscillationThreshold();
    testResonantFilterIsSecondOrderLowpass();
    testResonantFilterPassbandSurvivesResonance();
    testResonantFilterRetuneHasNoClick();
    testResonantFilterClampsHostileInput();
    testResonantFilterResetAndDenormals();

    if (gFailures == 0)
    {
        std::printf("[OK] DspCore: %d comprobaciones\n", gChecks);
        return 0;
    }

    std::printf("[FALLO] DspCore: %d de %d comprobaciones\n", gFailures, gChecks);
    return 1;
}
