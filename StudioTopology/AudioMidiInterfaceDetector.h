/**
 * @file AudioMidiInterfaceDetector.h
 * @brief Detection, grouping, and port modeling of Audio and MIDI host interfaces (Mac Audio/MIDI Setup style).
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include <set>

namespace abd::topology
{

/**
 * @struct InterfacePortInfo
 * @brief Represents an individual audio channel or MIDI port on an interface.
 */
struct InterfacePortInfo
{
    juce::String portId;       // e.g. "audio_out", "audio_in", "midi_out_0", "midi_in_0"
    juce::String name;         // e.g. "Salida 1-2", "loopMIDI Port 001"
    juce::String type;         // "audioOut", "audioIn", "midiOut", "midiIn"
    bool isConnected { false };// Whether this specific port is configured and in active use by the software
};

/**
 * @struct DetectedInterfaceInfo
 * @brief Represents a recognized physical or virtual host audio/MIDI interface containing multiple ports.
 */
struct DetectedInterfaceInfo
{
    juce::String id;                 // e.g. "presonus_audiobox_usb", "roland_mx_1", "loopmidi_hub", "realtek_audio"
    juce::String displayName;        // e.g. "PreSonus AudioBox USB"
    juce::String brand;              // e.g. "PreSonus", "Roland", "Generic"
    juce::String imageRelPath;       // e.g. "interfaces/presonus-audiobox-usb.png"
    bool isAudioConnected { false }; // At least one audio port is active in current software
    bool isMidiInConnected { false };// At least one MIDI in is active
    bool isMidiOutConnected { false };// At least one MIDI out is active
    bool isPresentInSystem { false };// Detected in Windows/system devices
    bool hasAudioHardware { true };
    bool hasMidiHardware { true };
    juce::String matchedAudioDeviceName;
    juce::String matchedMidiDeviceName;

    std::vector<InterfacePortInfo> ports;

    [[nodiscard]] bool isAnyActiveInSoftware() const noexcept
    {
        for (const auto& p : ports)
            if (p.isConnected) return true;
        return isAudioConnected || isMidiInConnected || isMidiOutConnected;
    }
};

/**
 * @class AudioMidiInterfaceDetector
 * @brief Scans active audio drivers, available system audio devices, and MIDI ports, grouping multi-port drivers into single interfaces.
 */
class AudioMidiInterfaceDetector
{
public:
    static std::vector<DetectedInterfaceInfo> detectInterfaces(juce::AudioDeviceManager& deviceManager)
    {
        std::vector<DetectedInterfaceInfo> results;

        juce::String currentAudioDevice;
        if (auto* dev = deviceManager.getCurrentAudioDevice())
            currentAudioDevice = dev->getName();

        auto midiInputs = juce::MidiInput::getAvailableDevices();
        auto midiOutputs = juce::MidiOutput::getAvailableDevices();

        auto isMidiInActive = [&](const juce::String& identifier) {
            return deviceManager.isMidiInputDeviceEnabled(identifier);
        };

        auto isMidiOutActive = [&](const juce::String& name) {
            if (auto* defOut = deviceManager.getDefaultMidiOutput())
                return defOut->getName().equalsIgnoreCase(name);
            return false;
        };

        auto hasMidiPattern = [&](const juce::String& pattern) {
            for (const auto& dev : midiInputs)
                if (dev.name.containsIgnoreCase(pattern)) return true;
            for (const auto& dev : midiOutputs)
                if (dev.name.containsIgnoreCase(pattern)) return true;
            return false;
        };

        // 1. PreSonus AudioBox USB
        {
            bool audioMatches = currentAudioDevice.containsIgnoreCase("AudioBox");
            bool midiMatches = hasMidiPattern("AudioBox") || hasMidiPattern("PreSonus");

            if (audioMatches || midiMatches)
            {
                DetectedInterfaceInfo presonus;
                presonus.id = "presonus_audiobox_usb";
                presonus.displayName = "PreSonus AudioBox USB";
                presonus.brand = "PreSonus";
                presonus.imageRelPath = "interfaces/presonus-audiobox-usb.png";
                presonus.hasAudioHardware = true;
                presonus.hasMidiHardware = true;
                presonus.isPresentInSystem = true;

                if (audioMatches)
                {
                    presonus.isAudioConnected = true;
                    presonus.matchedAudioDeviceName = currentAudioDevice;
                    presonus.ports.push_back({ "audio_out", "Main Out 1-2", "audioOut", true });
                    presonus.ports.push_back({ "audio_in",  "Main In 1-2",  "audioIn",  true });
                }
                else
                {
                    presonus.ports.push_back({ "audio_out", "Main Out 1-2", "audioOut", false });
                    presonus.ports.push_back({ "audio_in",  "Main In 1-2",  "audioIn",  false });
                }

                // Match PreSonus MIDI Ports
                for (const auto& dev : midiInputs)
                {
                    if (dev.name.containsIgnoreCase("AudioBox") || dev.name.containsIgnoreCase("PreSonus"))
                    {
                        bool active = isMidiInActive(dev.identifier);
                        if (active) presonus.isMidiInConnected = true;
                        presonus.ports.push_back({ "midi_in_" + dev.identifier, dev.name, "midiIn", active });
                    }
                }
                for (const auto& dev : midiOutputs)
                {
                    if (dev.name.containsIgnoreCase("AudioBox") || dev.name.containsIgnoreCase("PreSonus"))
                    {
                        bool active = isMidiOutActive(dev.name);
                        if (active) presonus.isMidiOutConnected = true;
                        presonus.ports.push_back({ "midi_out_" + dev.identifier, dev.name, "midiOut", active });
                    }
                }

                results.push_back(presonus);
            }
        }

        // 2. Roland AIRA MX-1
        {
            bool audioMatches = currentAudioDevice.containsIgnoreCase("MX-1") ||
                               (currentAudioDevice.containsIgnoreCase("AIRA") && currentAudioDevice.containsIgnoreCase("MX"));
            bool midiMatches = hasMidiPattern("MX-1") || (hasMidiPattern("AIRA") && hasMidiPattern("MX"));

            if (audioMatches || midiMatches)
            {
                DetectedInterfaceInfo roland;
                roland.id = "roland_mx_1";
                roland.displayName = "Roland AIRA MX-1";
                roland.brand = "Roland";
                roland.imageRelPath = "interfaces/roland-mx-1.png";
                roland.hasAudioHardware = true;
                roland.hasMidiHardware = true;
                roland.isPresentInSystem = true;

                if (audioMatches)
                {
                    roland.isAudioConnected = true;
                    roland.matchedAudioDeviceName = currentAudioDevice;
                    roland.ports.push_back({ "audio_out", "Master Out 1-2", "audioOut", true });
                    roland.ports.push_back({ "audio_in",  "Return In 1-2",  "audioIn",  true });
                }
                else
                {
                    roland.ports.push_back({ "audio_out", "Master Out 1-2", "audioOut", false });
                    roland.ports.push_back({ "audio_in",  "Return In 1-2",  "audioIn",  false });
                }

                for (const auto& dev : midiInputs)
                {
                    if (dev.name.containsIgnoreCase("MX-1") || (dev.name.containsIgnoreCase("AIRA") && dev.name.containsIgnoreCase("MX")))
                    {
                        bool active = isMidiInActive(dev.identifier);
                        if (active) roland.isMidiInConnected = true;
                        roland.ports.push_back({ "midi_in_" + dev.identifier, dev.name, "midiIn", active });
                    }
                }
                for (const auto& dev : midiOutputs)
                {
                    if (dev.name.containsIgnoreCase("MX-1") || (dev.name.containsIgnoreCase("AIRA") && dev.name.containsIgnoreCase("MX")))
                    {
                        bool active = isMidiOutActive(dev.name);
                        if (active) roland.isMidiOutConnected = true;
                        roland.ports.push_back({ "midi_out_" + dev.identifier, dev.name, "midiOut", active });
                    }
                }

                results.push_back(roland);
            }
        }

        // 3. Audio Host Interface (Active Soundcard, e.g. Realtek High Definition Audio)
        bool anyBrandedHasAudio = false;
        for (const auto& iface : results)
        {
            if (iface.isAudioConnected)
            {
                anyBrandedHasAudio = true;
                break;
            }
        }

        if (!anyBrandedHasAudio && currentAudioDevice.isNotEmpty())
        {
            DetectedInterfaceInfo audioIface;
            audioIface.id = "active_host_audio";
            audioIface.displayName = currentAudioDevice;
            audioIface.brand = "Windows Audio";
            audioIface.imageRelPath = "interfaces/generic-audio-midi-interface.png";
            audioIface.hasAudioHardware = true;
            audioIface.hasMidiHardware = false;
            audioIface.isAudioConnected = true;
            audioIface.isPresentInSystem = true;
            audioIface.matchedAudioDeviceName = currentAudioDevice;

            audioIface.ports.push_back({ "audio_out", "Output 1-2", "audioOut", true });
            audioIface.ports.push_back({ "audio_in",  "Input 1-2",  "audioIn",  true });

            results.push_back(audioIface);
        }

        // 4. Group Virtual & Generic MIDI Ports into logical multi-port interfaces (Mac style)
        std::map<juce::String, std::vector<juce::MidiDeviceInfo>> groupedInputs;
        std::map<juce::String, std::vector<juce::MidiDeviceInfo>> groupedOutputs;

        auto getFamilyKey = [](const juce::String& name) -> juce::String {
            if (name.containsIgnoreCase("loopMIDI")) return "loopMIDI Virtual Hub";
            if (name.containsIgnoreCase("LoopBe"))   return "LoopBe Internal MIDI";
            if (name.containsIgnoreCase("teVirtualMIDI")) return "teVirtualMIDI Router";
            if (name.containsIgnoreCase("Wavetable")) return "Microsoft GS Wavetable";
            return name; // Standalone port
        };

        for (const auto& mDev : midiInputs)
        {
            if (mDev.name.containsIgnoreCase("AudioBox") || mDev.name.containsIgnoreCase("MX-1"))
                continue;
            groupedInputs[getFamilyKey(mDev.name)].push_back(mDev);
        }

        for (const auto& mDev : midiOutputs)
        {
            if (mDev.name.containsIgnoreCase("AudioBox") || mDev.name.containsIgnoreCase("MX-1"))
                continue;
            groupedOutputs[getFamilyKey(mDev.name)].push_back(mDev);
        }

        // Collect all distinct family keys
        std::set<juce::String> allFamilies;
        for (const auto& kv : groupedInputs) allFamilies.insert(kv.first);
        for (const auto& kv : groupedOutputs) allFamilies.insert(kv.first);

        for (const auto& family : allFamilies)
        {
            DetectedInterfaceInfo hub;
            hub.id = "midi_hub_" + juce::File::createLegalFileName(family);
            hub.displayName = family;
            hub.brand = family.containsIgnoreCase("Virtual") || family.containsIgnoreCase("loop") ? "Virtual Driver" : "MIDI";
            hub.imageRelPath = "interfaces/generic-audio-midi-interface.png";
            hub.hasAudioHardware = false;
            hub.hasMidiHardware = true;
            hub.isPresentInSystem = true;

            // Add all inputs in this family
            if (groupedInputs.count(family))
            {
                for (const auto& port : groupedInputs[family])
                {
                    bool active = isMidiInActive(port.identifier);
                    if (active) hub.isMidiInConnected = true;
                    hub.ports.push_back({ "midi_in_" + juce::File::createLegalFileName(port.name), port.name, "midiIn", active });
                }
            }

            // Add all outputs in this family
            if (groupedOutputs.count(family))
            {
                for (const auto& port : groupedOutputs[family])
                {
                    bool active = isMidiOutActive(port.name);
                    if (active) hub.isMidiOutConnected = true;
                    hub.ports.push_back({ "midi_out_" + juce::File::createLegalFileName(port.name), port.name, "midiOut", active });
                }
            }

            results.push_back(hub);
        }

        return results;
    }
};

} // namespace abd::topology

// Backward compatibility alias for ABDAudioLab codebase
namespace abdaudiolab::hardware
{
    using InterfacePortInfo = abd::topology::InterfacePortInfo;
    using DetectedInterfaceInfo = abd::topology::DetectedInterfaceInfo;
    using AudioMidiInterfaceDetector = abd::topology::AudioMidiInterfaceDetector;
}
