/**
 * @file JuceMidiKeyboardComponent.h
 * @brief JUCE component embedding the Virtual MIDI Keyboard in WebView2.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <WebView2Bridge/JuceWebView2Component.h>
#include "MidiKeyboardResourceProvider.h"
#include <functional>

namespace abd::keyboard
{

/**
 * @class JuceMidiKeyboardComponent
 * @brief Embeds the interactive Virtual MIDI Keyboard WebUI, routing noteOn, noteOff,
 *        pitchBend, modWheel, sustain, and allNotesOff into juce::MidiMessage callbacks.
 */
class JuceMidiKeyboardComponent : public abd::webview2::JuceWebView2Component
{
public:
    explicit JuceMidiKeyboardComponent(const juce::String& initialTheme = "ms2000")
        : JuceWebView2Component(abd::keyboard::midiKeyboardResourceProvider,
                                buildEventListeners(),
                                initialTheme)
    {
    }

    ~JuceMidiKeyboardComponent() override = default;

    std::function<void(const juce::MidiMessage&)> onMidiMessage;

private:
    EventListeners buildEventListeners()
    {
        EventListeners listeners;

        listeners["noteOn"] = [this](const juce::var& v) {
            if (!onMidiMessage) return;
            int note = v["note"];
            float vel = static_cast<float>(static_cast<double>(v["velocity"]));
            if (note >= 0 && note <= 127)
                onMidiMessage(juce::MidiMessage::noteOn(1, note, vel));
        };

        listeners["noteOff"] = [this](const juce::var& v) {
            if (!onMidiMessage) return;
            int note = v["note"];
            if (note >= 0 && note <= 127)
                onMidiMessage(juce::MidiMessage::noteOff(1, note, 0.0f));
        };

        listeners["pitchBend"] = [this](const juce::var& v) {
            if (!onMidiMessage) return;
            int val = v["value"];
            val = juce::jlimit(0, 16383, val);
            onMidiMessage(juce::MidiMessage::pitchWheel(1, val));
        };

        listeners["modWheel"] = [this](const juce::var& v) {
            if (!onMidiMessage) return;
            int val = v["value"];
            val = juce::jlimit(0, 127, val);
            onMidiMessage(juce::MidiMessage::controllerEvent(1, 1, val));
        };

        listeners["sustain"] = [this](const juce::var& v) {
            if (!onMidiMessage) return;
            bool active = v["active"];
            onMidiMessage(juce::MidiMessage::controllerEvent(1, 64, active ? 127 : 0));
        };

        listeners["allNotesOff"] = [this](const juce::var&) {
            if (!onMidiMessage) return;
            onMidiMessage(juce::MidiMessage::allNotesOff(1));
            onMidiMessage(juce::MidiMessage::allControllersOff(1));
        };

        return listeners;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuceMidiKeyboardComponent)
};

} // namespace abd::keyboard
