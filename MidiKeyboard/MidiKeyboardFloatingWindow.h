/**
 * @file MidiKeyboardFloatingWindow.h
 * @brief Native floating tool window embedding the Virtual MIDI Keyboard WebUI.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "JuceMidiKeyboardComponent.h"
#include <functional>

namespace abd::keyboard
{

/**
 * @class MidiKeyboardFloatingWindow
 * @brief Floating window hosting the interactive Virtual MIDI Keyboard WebUI.
 */
class MidiKeyboardFloatingWindow : public juce::DocumentWindow
{
public:
    explicit MidiKeyboardFloatingWindow(const juce::String& initialTheme = "audiolab",
                                        std::function<void(const juce::MidiMessage&)> midiCallback = nullptr,
                                        std::function<void()> onClose = nullptr)
        : DocumentWindow(juce::String::fromUTF8(u8"Teclado Virtual MIDI"),
                         juce::Colour(0xff070a0e),
                         DocumentWindow::allButtons),
          onCloseCallback(std::move(onClose))
    {
        setUsingNativeTitleBar(true);

        auto comp = std::make_unique<JuceMidiKeyboardComponent>(initialTheme);
        comp->onMidiMessage = std::move(midiCallback);
        keyboardCompPtr = comp.get();
        setContentOwned(comp.release(), true);

        setResizable(true, true);
        setResizeLimits(540, 180, 1920, 800);
        centreWithSize(760, 240);
        setAlwaysOnTop(true);
    }

    ~MidiKeyboardFloatingWindow() override = default;

    void setMidiCallback(std::function<void(const juce::MidiMessage&)> cb)
    {
        if (keyboardCompPtr != nullptr)
            keyboardCompPtr->onMidiMessage = std::move(cb);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onCloseCallback)
            onCloseCallback();
    }

    void setTheme(const juce::String& themeName, juce::Colour bgColour = juce::Colour(0xff070a0e))
    {
        setBackgroundColour(bgColour);
        if (keyboardCompPtr != nullptr)
            keyboardCompPtr->setTheme(themeName.toStdString());
        repaint();
    }

private:
    JuceMidiKeyboardComponent* keyboardCompPtr { nullptr };
    std::function<void()> onCloseCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardFloatingWindow)
};

} // namespace abd::keyboard
