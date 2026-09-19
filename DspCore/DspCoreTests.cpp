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

    Uso: ABDShared_DspCore_Tests  (no toma argumentos; 0 = OK)

  ==============================================================================
*/

#include "DspCore/DspCore.h"
#include "DspCore/DspMidiBuffer.h"
#include "DspCore/DspMidiMessage.h"

#include <cmath>
#include <cstdio>
#include <utility>

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

//==============================================================================
/** Guard de regresion: clear/allocate cuentan elementos (como JUCE). */
void testHeapBlockElementSemantics()
{
    // 64 elementos y se llenan los 64: clear(64) debe dejarlos TODOS a cero.
    // Con la semantica de bytes solo se limpiarian los 16 primeros floats.
    abd::dsp::HeapBlock<float> block;
    block.malloc (64);

    check (block.get() != nullptr, "HeapBlock::malloc no reservo memoria");

    for (int i = 0; i < 64; ++i)
        block[i] = 1.0f;

    block.clear (64);

    bool allZero = true;
    for (int i = 0; i < 64; ++i)
        if (block[i] != 0.0f)
            allZero = false;

    check (allZero, "HeapBlock::clear(numElements) no limpio todos los elementos");

    // allocate(n, true) deja n elementos a cero.
    abd::dsp::HeapBlock<float> zeroed;
    zeroed.allocate (32, true);

    bool zeroedOk = zeroed.get() != nullptr;
    for (int i = 0; i < 32 && zeroedOk; ++i)
        zeroedOk = (zeroed[i] == 0.0f);

    check (zeroedOk, "HeapBlock::allocate(numElements, true) no dejo el bloque a cero");

    // Sin zeroFill, allocate reserva el mismo espacio (no se comprueba el
    // contenido: es memoria sin inicializar por definicion).
    abd::dsp::HeapBlock<float> raw;
    raw.allocate (16, false);
    check (raw.get() != nullptr, "HeapBlock::allocate(numElements, false) no reservo memoria");

    // swap y liberacion (sin fugas: los temporales se destruyen solos).
    abd::dsp::HeapBlock<float> other;
    other.allocate (8, false);
    block.swapWith (other);
    other.free();
    check (block.get() != nullptr, "HeapBlock::swapWith dejo el bloque sin datos");
}

//==============================================================================
void testAudioBuffer()
{
    abd::dsp::AudioBuffer<float> buffer;
    buffer.setSize (2, 128, false, true, false);

    check (buffer.getNumChannels() == 2, "AudioBuffer::setSize no fijo los canales");
    check (buffer.getNumSamples() == 128, "AudioBuffer::setSize no fijo las muestras");

    buffer.clear();
    check (buffer.getSample (0, 0) == 0.0f, "AudioBuffer::clear no limpio");

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 128; ++i)
            buffer.setSample (ch, i, (float) (ch + 1) * 0.5f);

    check (buffer.getSample (1, 127) == 1.0f, "AudioBuffer::setSample/getSample");
    check (std::fabs (buffer.getMagnitude (0, 0, 128) - 0.5f) < 1.0e-9f,
           "AudioBuffer::getMagnitude");

    // copyFrom entre canales del propio buffer.
    buffer.copyFrom (0, 0, buffer, 1, 0, 128);
    check (buffer.getSample (0, 64) == 1.0f, "AudioBuffer::copyFrom");

    // applyGain con un smoother deja una rampa: el final del tramo suavizado
    // queda atenuado y el comienzo no (el buffer estaba a 1.0 en el canal 0).
    abd::dsp::LinearSmoothedValue<float> gain { 1.0f };
    gain.reset (1000.0, 0.1);
    gain.setTargetValue (0.0f);
    gain.applyGain (buffer, 100);

    check (buffer.getSample (0, 99) < 0.1f, "SmoothedValue::applyGain no atenua al final de la rampa");
    check (buffer.getSample (0, 0) > buffer.getSample (0, 99),
           "SmoothedValue::applyGain no deja una rampa decreciente");
}

//==============================================================================
// Sonda a nivel de namespace: el array tiene que tener duracion estatica para
// poder exigir N en tiempo de compilacion.
static const float kProbeArray[4] {};

// Este static_assert ES el guard del bug: con el parametro por valor el array
// decae a puntero y la funcion devolvia sizeof (float*) / sizeof (float) = 2.
static_assert (abd::dsp::numElementsInArray (kProbeArray) == 4,
               "numElementsInArray tiene que devolver N; si devuelve otra cosa, el array esta decayendo a puntero");

/** Guard de regresion del helper de JUCE (port en DspCore.h) y de su consumidor
    con array, el ctor de AudioBuffer sobre memoria externa: con el helper
    devolviendo 1, la rama de `preallocatedChannelSpace` era inalcanzable en los
    tres sitios de AudioBuffer que lo usan. */
void testNumElementsInArray()
{
    check (abd::dsp::numElementsInArray (kProbeArray) == 4, "numElementsInArray no devuelve N");

    float left[8] {}, right[8] {};
    float* external[2] { left, right };

    // Buffer que REFERENCIA canales ya reservados (el ctor que instanciaba el
    // aviso de GCC).
    abd::dsp::AudioBuffer<float> referenced (external, 2, 8);
    check (referenced.getNumChannels() == 2, "AudioBuffer(memoria externa): canales");
    check (referenced.getNumSamples() == 8, "AudioBuffer(memoria externa): muestras");

    referenced.getWritePointer (0)[3] = 0.25f;
    check (left[3] == 0.25f, "AudioBuffer(memoria externa): no escribe en los canales del que llama");

    // Mover un buffer que referencia memoria externa: el destino tiene que
    // seguir operando sobre esos mismos arrays.
    abd::dsp::AudioBuffer<float> moved;

    {
        abd::dsp::AudioBuffer<float> source (external, 2, 8);
        source.getWritePointer (1)[0] = 0.5f;
        check (right[0] == 0.5f, "AudioBuffer(memoria externa): escritura antes del movimiento");

        moved = std::move (source);
    }

    moved.getWritePointer (1)[0] = 0.75f;
    check (right[0] == 0.75f, "AudioBuffer: el destino del movimiento no apunta a los canales externos");
    check (moved.getNumChannels() == 2 && moved.getNumSamples() == 8,
           "AudioBuffer: el movimiento perdio canales o muestras");
}

//==============================================================================
void testSmoothedValue()
{
    abd::dsp::LinearSmoothedValue<float> value { 0.0f };
    value.reset (1000.0, 0.1);            // 100 pasos
    value.setTargetValue (1.0f);
    value.setCurrentAndTargetValue (0.0f);
    value.setTargetValue (1.0f);

    check (value.isSmoothing(), "SmoothedValue: no arranco el ramp");

    float last = value.getCurrentValue();
    bool monotonic = true;
    int steps = 0;

    while (value.isSmoothing() && steps < 1000)
    {
        const float next = value.getNextValue();

        if (next < last)
            monotonic = false;

        last = next;
        ++steps;
    }

    check (monotonic, "SmoothedValue: el ramp no es monotono");
    check (steps == 100, "SmoothedValue: el ramp no duro los pasos de reset(sampleRate, segundos)");
    check (value.getCurrentValue() == 1.0f, "SmoothedValue: el ramp no llego al objetivo");

    // Ya en el objetivo, getNextValue es exactamente el objetivo.
    check (value.getNextValue() == 1.0f, "SmoothedValue: no se mantiene en el objetivo");

    // reset(int) salta directo al objetivo.
    value.reset (17);
    check (value.getCurrentValue() == 1.0f, "SmoothedValue::reset(numSteps)");
}

//==============================================================================
void testMidiMessage()
{
    using abd::dsp::MidiMessage;

    const auto noteOn = MidiMessage::noteOn (1, 60, 0.5f);

    check (noteOn.getRawDataSize() == 3, "MidiMessage::noteOn: tamano");
    check (noteOn.getRawData()[0] == 0x90, "MidiMessage::noteOn: byte de estado");
    check (noteOn.getNoteNumber() == 60, "MidiMessage::noteOn: nota");

    // roundToInt (lrintf) redondea los empates al par: 0.5f * 127 = 63.5 -> 64.
    check (noteOn.getVelocity() == 64, "MidiMessage::noteOn: cuantizacion de velocidad");
    check (noteOn.isNoteOnOrOff(), "MidiMessage::isNoteOnOrOff");

    const auto noteOff = MidiMessage::noteOff (16, 61, (uint8_t) 7);
    check (noteOff.getRawData()[0] == 0x8f, "MidiMessage::noteOff: canal 16");
    check (noteOff.getNoteNumber() == 61, "MidiMessage::noteOff: nota");
    check (noteOff.getVelocity() == 7, "MidiMessage::noteOff: velocidad");

    const auto wheel = MidiMessage::pitchWheel (1, 8192);
    check (wheel.getRawDataSize() == 3, "MidiMessage::pitchWheel: tamano");
    check (wheel.getPitchWheelValue() == 8192, "MidiMessage::pitchWheel: valor de 14 bits");

    const auto cc = MidiMessage::controllerEvent (1, 1, 64);
    check (cc.isController(), "MidiMessage::controllerEvent: isController");
    check (cc.getControllerNumber() == 1, "MidiMessage::controllerEvent: numero");
    check (cc.getControllerValue() == 64, "MidiMessage::controllerEvent: valor");

    // Mensaje crudo de 1 byte (los sysEx/system common no se portan).
    const uint8_t raw[] = { 0xf8 };
    check (MidiMessage (raw, 1).getRawDataSize() == 1, "MidiMessage: constructor crudo");

    // Numero de bytes fuera de rango: se recorta a 1..3 (jlimit(1, 3, size),
    // igual que JUCE), de modo que un 99 declarado se lee como 3 bytes.
    check (MidiMessage (raw, 99).getRawDataSize() == 3, "MidiMessage: recorte de tamano");

    check (std::fabs (MidiMessage::getMidiNoteInHertz (69) - 440.0) < 1.0e-9,
           "MidiMessage::getMidiNoteInHertz(69) != 440");
    check (MidiMessage::getMidiNoteInHertz (81) > MidiMessage::getMidiNoteInHertz (69),
           "MidiMessage::getMidiNoteInHertz no es monotona");
}

//==============================================================================
void testMidiBuffer()
{
    abd::dsp::MidiBuffer buffer;

    check (buffer.isEmpty(), "MidiBuffer: recien creado no esta vacio");
    check (buffer.getNumEvents() == 0, "MidiBuffer: recien creado tiene eventos");

    // Insercion desordenada: la iteracion es por posicion de muestra.
    buffer.addEvent (abd::dsp::MidiMessage::noteOn (1, 64, 1.0f), 10);
    buffer.addEvent (abd::dsp::MidiMessage::noteOn (1, 60, 1.0f), 0);
    buffer.addEvent (abd::dsp::MidiMessage::noteOff (1, 60, 0.0f), 5);

    check (buffer.getNumEvents() == 3, "MidiBuffer::getNumEvents");

    int previousPosition = -1;
    int count = 0;
    int notes[3] = { 0, 0, 0 };

    for (const auto metadata : buffer)
    {
        if (metadata.samplePosition < previousPosition)
            check (false, "MidiBuffer: la iteracion no esta ordenada por posicion");

        previousPosition = metadata.samplePosition;

        if (count < 3)
            notes[count] = metadata.getMessage().getNoteNumber();

        ++count;
    }

    check (count == 3, "MidiBuffer: la iteracion no devolvio todos los eventos");
    check (notes[0] == 60 && notes[1] == 60 && notes[2] == 64,
           "MidiBuffer: orden de notas inesperado");
    check (buffer.getFirstEventTime() == 0, "MidiBuffer::getFirstEventTime");

    // clear() conserva la capacidad: sigue sirviendo sin reasignar.
    buffer.clear();
    check (buffer.isEmpty(), "MidiBuffer::clear no vacio");

    // Crecimiento por encima del bloque inicial (512 bytes): 200 eventos CC.
    for (int i = 0; i < 200; ++i)
        buffer.addEvent (abd::dsp::MidiMessage::controllerEvent (1, 74, i & 127), i);

    check (buffer.getNumEvents() == 200, "MidiBuffer: crecimiento por encima del bloque inicial");

    // ensureSize es idempotente y no pierde lo que ya habia.
    buffer.ensureSize (8192);
    check (buffer.getNumEvents() == 200, "MidiBuffer::ensureSize perdio eventos");
}

//==============================================================================
void testRandom()
{
    abd::dsp::Random first, second;
    first.setSeed (12345);
    second.setSeed (12345);

    bool identical = true;

    for (int i = 0; i < 64; ++i)
        if (first.nextInt() != second.nextInt())
            identical = false;

    check (identical, "Random: la misma semilla no da la misma secuencia");

    abd::dsp::Random other;
    other.setSeed (54321);

    bool differs = false;

    for (int i = 0; i < 64; ++i)
        if (other.nextInt() != first.nextInt())
            differs = true;

    check (differs, "Random: semillas distintas dan la misma secuencia");

    // nextFloat esta en [0, 1).
    abd::dsp::Random values;
    values.setSeed (7);

    bool inRange = true;
    for (int i = 0; i < 256; ++i)
    {
        const float v = values.nextFloat();
        if (! (v >= 0.0f && v < 1.0f))
            inRange = false;
    }

    check (inRange, "Random::nextFloat fuera de [0, 1)");
}

//==============================================================================
/** DspMath: determinista (sin libm del sistema) y con precision suficiente.
    La libm de la plataforma se usa AQUI solo como referencia de precision; la
    propiedad que importa (resultado identico en MSVC y en emscripten) se
    verifica compilando el mismo barrido en los dos toolchains. */
void testDspMath()
{
    // Valores conocidos.
    check (abd::dsp::sin (0.0f) == 0.0f, "dsp::sin(0) != 0");
    check (abd::dsp::atan (0.0f) == 0.0f, "dsp::atan(0) != 0");
    check (abd::dsp::cos (0.0f) == 1.0f, "dsp::cos(0) != 1");
    check (std::abs (abd::dsp::sin (1.5707963f) - 1.0f) < 1.0e-5f, "dsp::sin(pi/2) != 1");
    check (std::abs (abd::dsp::atan (1.0f) - 0.7853981634f) < 1.0e-4f, "dsp::atan(1) != pi/4");

    // Precision frente a la libm de la plataforma en los rangos de audio.
    float maxSinErr = 0.0f, maxAtanErr = 0.0f;
    for (int i = 0; i <= 200000; ++i)
    {
        const float x = (float) i * 0.000031415926f;          // 0 .. ~2pi
        maxSinErr = std::max (maxSinErr, std::abs (abd::dsp::sin (x) - std::sin (x)));

        const float y = ((float) i - 100000.0f) * 0.0001f;    // -10 .. 10
        maxAtanErr = std::max (maxAtanErr, std::abs (abd::dsp::atan (y) - std::atan (y)));
    }

    check (maxSinErr < 1.0e-5f, "dsp::sin: error vs libm por encima de 1e-5");
    check (maxAtanErr < 1.0e-5f, "dsp::atan: error vs libm por encima de 1e-5");
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

    if (gFailures == 0)
    {
        std::printf ("[OK] DspCore: %d comprobaciones\n", gChecks);
        return 0;
    }

    std::printf ("[FALLO] DspCore: %d de %d comprobaciones\n", gFailures, gChecks);
    return 1;
}
