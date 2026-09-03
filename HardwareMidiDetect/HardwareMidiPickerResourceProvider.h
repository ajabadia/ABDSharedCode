/**
 * @file HardwareMidiPickerResourceProvider.h
 * @brief WebBrowserComponent resource provider for the HardwareMidiPicker WebUI.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace abd::hwid
{

/**
 * @brief Serves the embedded HardwareMidiPicker WebUI binary assets.
 */
std::optional<juce::WebBrowserComponent::Resource> hardwareMidiPickerResourceProvider(const juce::String& url);

} // namespace abd::hwid