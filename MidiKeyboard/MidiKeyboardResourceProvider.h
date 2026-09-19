/**
 * @file MidiKeyboardResourceProvider.h
 * @brief Resource provider for the MidiKeyboard WebUI.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace abd::keyboard
{

/**
 * @brief Serves index.html and resources for the Virtual MIDI Keyboard WebUI,
 *        falling back to ABDSharedAssets filesystem for icons and assets.
 */
std::optional<juce::WebBrowserComponent::Resource> midiKeyboardResourceProvider(const juce::String& url);

} // namespace abd::keyboard
