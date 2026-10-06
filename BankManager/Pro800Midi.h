#pragma once

#include <array>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

namespace ABD::BankManager
{

class Pro800MidiTransport
{
public:
    static juce::MemoryBlock buildDumpRequest(int slot);
    static int parseResponse(const juce::MemoryBlock& message, juce::MemoryBlock& rawData);
    static juce::MemoryBlock buildPatchDump(const juce::MemoryBlock& rawData, int slot);
};

} // namespace ABD::BankManager
