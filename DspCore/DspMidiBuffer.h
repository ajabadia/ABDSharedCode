/*
  ==============================================================================

    DspMidiBuffer.h
    Contenedor de eventos MIDI del nucleo DSP, libre de JUCE (Fase 1, 4/6).

    Port de juce::MidiBuffer (juce_audio_basics/midi/juce_MidiBuffer.h/.cpp)
    con la MISMA semantica observable:

      - Empaquetado identico por evento: [int32 samplePosition][uint16 size]
        [size bytes de mensaje], consecutivos y sin padding.
      - addEvent INSERTA ordenado por samplePosition (port literal de
        MidiBufferHelpers::findEventAfter: el evento nuevo va despues de todos
        los que tienen tiempo <= al suyo), asi que la iteracion entrega los
        eventos en el mismo orden que JUCE, empates incluidos (FIFO).
      - La iteracion produce metadatos con data/numBytes/samplePosition y
        getMessage(), igual que MidiMessageMetadata.

    Diferencias documentadas (ninguna observable para el motor):
      1. addEvent recibe el tamano explicito; JUCE lo deduce del primer byte
         (MidiBufferHelpers::findActualEventLength). El adaptador JUCE hace
         esa deduccion antes de cruzar la frontera, con el helper de JUCE.
      2. ensureSize() permite reservar para no asignar en el hilo de audio:
         clear() conserva la capacidad, igual que JUCE.

  ==============================================================================
*/

#pragma once

#include "DspCore.h"
#include "DspMidiMessage.h"

#include <cstdint>
#include <cstring>

namespace abd::dsp
{

class MidiBuffer
{
public:
    /** Tamano de la cabecera de cada evento empaquetado. */
    static constexpr int kEventHeaderSize = (int) sizeof (int32_t) + (int) sizeof (uint16_t);

    /** Vista de un evento, con la misma forma que juce::MidiMessageMetadata. */
    struct Metadata
    {
        const uint8_t* data = nullptr;
        int numBytes = 0;
        int samplePosition = 0;

        /** Construye el mensaje del evento (copia, como JUCE). */
        MidiMessage getMessage() const { return MidiMessage (data, numBytes); }
    };

    /** Iterador de solo lectura suficiente para el range-for del motor. */
    class Iterator
    {
    public:
        explicit Iterator (const uint8_t* eventData) noexcept : data (eventData) {}

        Metadata operator*() const noexcept
        {
            return { data + kEventHeaderSize,
                     (int) eventDataSize (data),
                     eventSamplePosition (data) };
        }

        Iterator& operator++() noexcept
        {
            data += kEventHeaderSize + (int) eventDataSize (data);
            return *this;
        }

        bool operator== (const Iterator& other) const noexcept { return data == other.data; }
        bool operator!= (const Iterator& other) const noexcept { return data != other.data; }

    private:
        const uint8_t* data;
    };

    MidiBuffer() = default;

    MidiBuffer (const MidiBuffer&) = delete;
    MidiBuffer& operator= (const MidiBuffer&) = delete;

    //==============================================================================
    /** Reserva capacidad para evitar asignaciones en el hilo de audio. */
    void ensureSize (int numBytes)                { ensureCapacity (numBytes); }

    /** Vacia el buffer conservando la capacidad y el bloque ya reservado. */
    void clear() noexcept                         { usedBytes = 0; }

    bool isEmpty() const noexcept                 { return usedBytes == 0; }

    int getNumEvents() const noexcept
    {
        int n = 0;

        for (const uint8_t* d = getData(); d < getData() + usedBytes; ++n)
            d += kEventHeaderSize + (int) eventDataSize (d);

        return n;
    }

    int getFirstEventTime() const noexcept        { return usedBytes > 0 ? eventSamplePosition (getData()) : 0; }

    //==============================================================================
    /** Anade un evento; la posicion se inserta ordenada (port de findEventAfter). */
    void addEvent (const MidiMessage& m, int samplePosition)
    {
        addEvent (m.getRawData(), m.getRawDataSize(), samplePosition);
    }

    /** Anade datos crudos con tamano explicito. */
    void addEvent (const uint8_t* newData, int numBytes, int samplePosition)
    {
        if (newData == nullptr || numBytes <= 0 || numBytes > 0xffff)
            return;

        const int itemSize = kEventHeaderSize + numBytes;
        const int offset = findEventAfter (samplePosition);

        ensureCapacity (usedBytes + itemSize);

        if (usedBytes + itemSize > capacity)   // sin memoria: se descarta el evento
            return;

        auto* base = getMutableData();
        std::memmove (base + offset + itemSize, base + offset, (size_t) (usedBytes - offset));

        uint8_t* d = base + offset;
        const auto position32 = (int32_t) samplePosition;
        const auto size16 = (uint16_t) numBytes;
        std::memcpy (d, &position32, sizeof (int32_t));
        std::memcpy (d + sizeof (int32_t), &size16, sizeof (uint16_t));
        std::memcpy (d + kEventHeaderSize, newData, (size_t) numBytes);

        usedBytes += itemSize;
    }

    //==============================================================================
    Iterator begin() const noexcept               { return Iterator (getData()); }
    Iterator end() const noexcept                 { return Iterator (getData() + usedBytes); }

private:
    //==============================================================================
    /** Port de MidiBufferHelpers::getEventTime. */
    static int eventSamplePosition (const uint8_t* d) noexcept
    {
        int32_t v = 0;
        std::memcpy (&v, d, sizeof (int32_t));
        return (int) v;
    }

    /** Port de MidiBufferHelpers::getEventDataSize. */
    static uint16_t eventDataSize (const uint8_t* d) noexcept
    {
        uint16_t v = 0;
        std::memcpy (&v, d + sizeof (int32_t), sizeof (uint16_t));
        return v;
    }

    /** Port de MidiBufferHelpers::findEventAfter. */
    int findEventAfter (int samplePosition) const noexcept
    {
        int offset = 0;

        while (offset < usedBytes && eventSamplePosition (getData() + offset) <= samplePosition)
            offset += kEventHeaderSize + (int) eventDataSize (getData() + offset);

        return offset;
    }

    const uint8_t* getData() const noexcept       { return storage.get() != nullptr ? storage.get() : emptyStorage; }
    uint8_t* getMutableData() noexcept            { return storage.get(); }

    /** Crecimiento amortizado; el bloque anterior se libera al destruir el temporal. */
    void ensureCapacity (int numBytes)
    {
        if (numBytes <= capacity)
            return;

        const int newCapacity = dsp::jmax (numBytes, capacity > 0 ? capacity * 2 : 512);

        dsp::HeapBlock<uint8_t> grown;
        grown.allocate ((size_t) newCapacity, false);

        if (grown.get() == nullptr)               // sin memoria: se conserva lo que habia
            return;

        if (usedBytes > 0 && storage.get() != nullptr)
            std::memcpy (grown.get(), storage.get(), (size_t) usedBytes);

        storage = std::move (grown);
        capacity = newCapacity;
    }

    static constexpr uint8_t emptyStorage[1] { 0 };

    dsp::HeapBlock<uint8_t> storage;
    int capacity = 0;
    int usedBytes = 0;
};

} // namespace abd::dsp
