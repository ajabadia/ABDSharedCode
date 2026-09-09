/**
 * @file HardwareMidiHotplugMonitor.h
 * @brief Reactive USB MIDI hotplug monitor for ABDSharedCode::HardwareMidiDetect.
 * @details Periodically evaluates available MIDI I/O ports and matches newly
 *          connected ports against registered hardware contracts.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "HardwareContract.h"
#include "HardwareMidiDetector.h"
#include <vector>
#include <functional>

namespace abd::hwid
{

class HardwareMidiHotplugMonitor : private juce::Timer
{
public:
    HardwareMidiHotplugMonitor() = default;
    ~HardwareMidiHotplugMonitor() override { stopMonitoring(); }

    void setContracts(std::vector<HardwareContract> contracts)
    {
        registeredContracts = std::move(contracts);
    }

    template <typename ExternalContract>
    void setContractsFromExternal(const std::vector<ExternalContract>& contracts)
    {
        registeredContracts.clear();
        registeredContracts.reserve(contracts.size());
        for (const auto& c : contracts)
        {
            HardwareContract hc;
            hc.id = c.id;
            hc.displayName = c.displayName;
            hc.brand = c.brand;
            hc.brandLogo = c.brandLogo;
            hc.modelImage = c.modelImage;
            hc.midiIdentity.portNameMatches = c.midiIdentity.portNameMatches;
            hc.midiIdentity.manufacturer = c.midiIdentity.manufacturer;
            hc.midiIdentity.model = c.midiIdentity.model;
            registeredContracts.push_back(std::move(hc));
        }
    }

    void startMonitoring(int pollIntervalMs = 1500)
    {
        if (pollIntervalMs < 500) pollIntervalMs = 500;
        refreshBaseline();
        startTimer(pollIntervalMs);
    }

    void stopMonitoring()
    {
        stopTimer();
    }

    [[nodiscard]] bool isMonitoring() const noexcept
    {
        return isTimerRunning();
    }

    void refreshBaseline()
    {
        lastOutputs = juce::MidiOutput::getAvailableDevices();
        lastInputs = juce::MidiInput::getAvailableDevices();
    }

    /**
     * @brief Evaluates differences between current lists and last known state.
     *        Can be called directly for unit testing without waiting on timers.
     */
    void evaluateLists(const juce::Array<juce::MidiDeviceInfo>& currentOuts,
                       const juce::Array<juce::MidiDeviceInfo>& currentIns)
    {
        // 1. Detect newly plugged devices
        for (const auto& outDev : currentOuts)
        {
            bool wasKnown = false;
            for (const auto& prevOut : lastOutputs)
            {
                if (prevOut.identifier == outDev.identifier || prevOut.name == outDev.name)
                {
                    wasKnown = true;
                    break;
                }
            }

            if (!wasKnown)
            {
                juce::MidiDeviceInfo inDev;
                for (const auto& inCandidate : currentIns)
                {
                    if (inCandidate.name == outDev.name || inCandidate.identifier == outDev.identifier)
                    {
                        inDev = inCandidate;
                        break;
                    }
                }

                auto match = HardwareMidiDetector::matchFromPortNames(inDev, outDev, registeredContracts);
                if (match.has_value())
                {
                    if (onDevicePlugged)
                        onDevicePlugged(*match);
                }
                else
                {
                    DiscoveredDevice genericDev;
                    genericDev.hardwareId = "generic_midi_synth";
                    genericDev.displayName = outDev.name.toStdString();
                    genericDev.outDevice = outDev;
                    genericDev.inDevice = inDev;
                    if (onDevicePlugged)
                        onDevicePlugged(genericDev);
                }
            }
        }

        // 2. Detect unplugged devices
        for (const auto& prevOut : lastOutputs)
        {
            bool stillPresent = false;
            for (const auto& curOut : currentOuts)
            {
                if (curOut.identifier == prevOut.identifier || curOut.name == prevOut.name)
                {
                    stillPresent = true;
                    break;
                }
            }

            if (!stillPresent)
            {
                if (onDeviceUnplugged)
                    onDeviceUnplugged(prevOut.name);
            }
        }

        lastOutputs = currentOuts;
        lastInputs = currentIns;
    }

    std::function<void(const DiscoveredDevice& device)> onDevicePlugged;
    std::function<void(const juce::String& portName)> onDeviceUnplugged;

private:
    void timerCallback() override
    {
        auto curOuts = juce::MidiOutput::getAvailableDevices();
        auto curIns = juce::MidiInput::getAvailableDevices();
        evaluateLists(curOuts, curIns);
    }

    std::vector<HardwareContract> registeredContracts;
    juce::Array<juce::MidiDeviceInfo> lastOutputs;
    juce::Array<juce::MidiDeviceInfo> lastInputs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareMidiHotplugMonitor)
};

} // namespace abd::hwid
