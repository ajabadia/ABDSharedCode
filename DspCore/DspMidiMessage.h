/*
  ==============================================================================

    DspMidiMessage.h
    Mensaje MIDI del nucleo DSP, libre de JUCE (Fase 1, paso 4/6).

    Port LITERAL de JUCE 8.0.12 (juce_audio_basics/midi/juce_MidiMessage.h/.cpp)
    con el mismo orden de operaciones: los predicados y los getters devuelven
    exactamente el mismo resultado que los de JUCE para cualquier mensaje de
    canal/voz de 1 a 3 bytes. La conversion float->byte usa el mismo
    dsp::roundToInt (truco de doble precision, empates al par) que
    juce::roundToInt, asi que la cuantizacion de velocity no cambia ni un byte.

    Alcance (a proposito): note on/off, pitch wheel, aftertouch de canal y
    polifonico y control change, que es TODO lo que el motor interpreta.
    SysEx, meta-eventos y mensajes de mas de 3 bytes no forman parte del
    contrato del motor: el adaptador JUCE los descarta antes de cruzar la
    frontera (Runtime/JuceMidiAdapter.h).

    Diferencias documentadas respecto a juce::MidiMessage (ninguna afecta al
    audio):
      1. Sin timeStamp: el motor nunca lo lee; la posicion dentro del bloque
         viaja en el MidiBuffer (metadata.samplePosition), igual que en JUCE.
      2. Almacenamiento fijo de 3 bytes; la cola no usada se pone a cero en
         vez de quedar sin inicializar (mas determinista para WASM; en MIDI
         valido esa cola es inalcanzable porque la longitud la fija el status).
      3. Solo los constructores/factorias y getters del subconjunto anterior.

  ==============================================================================
*/

#pragma once

#include "DspCore.h"

#include <array>
#include <cstdint>

namespace abd::dsp
{

/** Port del subconjunto de juce::MidiMessage que consume el motor DSP. */
class MidiMessage
{
public:
    //==============================================================================
    /** Crea el mismo mensaje por defecto que JUCE: SysEx vacio {0xF0, 0xF7}. */
    MidiMessage() noexcept
    {
        packed[0] = 0xf0;
        packed[1] = 0xf7;
        size = 2;
    }

    /** Copia numBytes del buffer crudo (1..3). La cola se pone a cero. */
    MidiMessage (const uint8_t* rawData, int numBytes) noexcept
    {
        size = jlimit (1, 3, numBytes);

        for (int i = 0; i < size; ++i)
            packed[(size_t) i] = rawData[i];
    }

    MidiMessage (int byte1, int byte2) noexcept
    {
        packed[0] = (uint8_t) byte1;
        packed[1] = (uint8_t) byte2;
        size = 2;
    }

    MidiMessage (int byte1, int byte2, int byte3) noexcept
    {
        packed[0] = (uint8_t) byte1;
        packed[1] = (uint8_t) byte2;
        packed[2] = (uint8_t) byte3;
        size = 3;
    }

    //==============================================================================
    /** Puntero a los datos crudos (status, data1, data2). Port de getRawData(). */
    const uint8_t* getRawData() const noexcept          { return packed.data(); }

    /** Numero de bytes del mensaje. Port de getRawDataSize(). */
    int getRawDataSize() const noexcept                 { return size; }

    //==============================================================================
    /** Port literal de MidiMessage::getChannel(). */
    int getChannel() const noexcept
    {
        const auto* data = getRawData();

        if ((data[0] & 0xf0) != 0xf0)
            return (data[0] & 0xf) + 1;

        return 0;
    }

    //==============================================================================
    // Notas (mismos cuerpos y mismos defaults que JUCE: isNoteOff trata
    // note-on con velocity 0 como note-off).

    bool isNoteOn (bool returnTrueForVelocity0 = false) const noexcept
    {
        const auto* data = getRawData();

        return ((data[0] & 0xf0) == 0x90)
                 && (returnTrueForVelocity0 || data[2] != 0);
    }

    bool isNoteOff (bool returnTrueForNoteOnVelocity0 = true) const noexcept
    {
        const auto* data = getRawData();

        return ((data[0] & 0xf0) == 0x80)
                || (returnTrueForNoteOnVelocity0 && (data[2] == 0) && ((data[0] & 0xf0) == 0x90));
    }

    bool isNoteOnOrOff() const noexcept
    {
        const auto d = getRawData()[0] & 0xf0;
        return (d == 0x90) || (d == 0x80);
    }

    int getNoteNumber() const noexcept                  { return getRawData()[1]; }

    uint8_t getVelocity() const noexcept                { return isNoteOnOrOff() ? getRawData()[2] : 0; }

    float getFloatVelocity() const noexcept             { return getVelocity() * (1.0f / 127.0f); }

    //==============================================================================
    // Pitch wheel.

    bool isPitchWheel() const noexcept                  { return (getRawData()[0] & 0xf0) == 0xe0; }

    int getPitchWheelValue() const noexcept
    {
        dspAssert (isPitchWheel());   // misma asercion que JUCE (solo Debug)
        const auto* data = getRawData();
        return data[1] | (data[2] << 7);
    }

    //==============================================================================
    // Aftertouch (polifonico y de canal).

    bool isAftertouch() const noexcept                  { return (getRawData()[0] & 0xf0) == 0xa0; }

    int getAfterTouchValue() const noexcept
    {
        dspAssert (isAftertouch());
        return getRawData()[2];
    }

    bool isChannelPressure() const noexcept             { return (getRawData()[0] & 0xf0) == 0xd0; }

    int getChannelPressureValue() const noexcept
    {
        dspAssert (isChannelPressure());
        return getRawData()[1];
    }

    //==============================================================================
    // Control change.

    bool isController() const noexcept                  { return (getRawData()[0] & 0xf0) == 0xb0; }

    int getControllerNumber() const noexcept
    {
        dspAssert (isController());
        return getRawData()[1];
    }

    int getControllerValue() const noexcept
    {
        dspAssert (isController());
        return getRawData()[2];
    }

    //==============================================================================
    // Factorias (port literal de las de JUCE, helpers incluidos).

    /** Port de MidiMessage::floatValueToMidiByte (mismo dsp::roundToInt). */
    static uint8_t floatValueToMidiByte (float valueBetween0and1) noexcept
    {
        return (uint8_t) dsp::jlimit (0, 127, dsp::roundToInt (valueBetween0and1 * 127.0f));
    }

    static MidiMessage noteOn (int channel, int noteNumber, uint8_t velocity) noexcept
    {
        return MidiMessage (initialByte (0x90, channel), noteNumber & 127, validVelocity (velocity));
    }

    static MidiMessage noteOn (int channel, int noteNumber, float velocity) noexcept
    {
        return noteOn (channel, noteNumber, floatValueToMidiByte (velocity));
    }

    static MidiMessage noteOff (int channel, int noteNumber, uint8_t velocity) noexcept
    {
        return MidiMessage (initialByte (0x80, channel), noteNumber & 127, validVelocity (velocity));
    }

    static MidiMessage noteOff (int channel, int noteNumber, float velocity) noexcept
    {
        return noteOff (channel, noteNumber, floatValueToMidiByte (velocity));
    }

    static MidiMessage pitchWheel (int channel, int position) noexcept
    {
        return MidiMessage (initialByte (0xe0, channel), position & 127, (position >> 7) & 127);
    }

    static MidiMessage channelPressureChange (int channel, int pressure) noexcept
    {
        return MidiMessage (initialByte (0xd0, channel), pressure & 0x7f);
    }

    static MidiMessage aftertouchChange (int channel, int noteNumber, int aftertouch) noexcept
    {
        return MidiMessage (initialByte (0xa0, channel), noteNumber & 0x7f, aftertouch & 0x7f);
    }

    static MidiMessage controllerEvent (int channel, int controllerType, int value) noexcept
    {
        return MidiMessage (initialByte (0xb0, channel), controllerType & 127, value & 127);
    }

    //==============================================================================
    /** Port literal de MidiMessage::getMidiNoteInHertz. */
    static double getMidiNoteInHertz (int noteNumber, double frequencyOfA = 440.0) noexcept
    {
        return frequencyOfA * std::pow (2.0, (noteNumber - 69) / 12.0);
    }

private:
    //==============================================================================
    /** Port de MidiHelpers::initialByte. */
    static uint8_t initialByte (int type, int channel) noexcept
    {
        return (uint8_t) (type | dsp::jlimit (0, 15, channel - 1));
    }

    /** Port de MidiHelpers::validVelocity. */
    static uint8_t validVelocity (int v) noexcept
    {
        return (uint8_t) dsp::jlimit (0, 127, v);
    }

    std::array<uint8_t, 3> packed { 0, 0, 0 };
    int size = 2;
};

} // namespace abd::dsp
