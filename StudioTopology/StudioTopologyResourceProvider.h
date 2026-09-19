/**
 * @file StudioTopologyResourceProvider.h
 * @brief Resource provider for the StudioTopology WebUI.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace abd::topology
{

/**
 * @brief Serves embedded binary assets (index.html, JS, CSS) from StudioTopologyAssets
 *        and falls back to ABDSharedAssets filesystem for styles/, models/, interfaces/.
 */
std::optional<juce::WebBrowserComponent::Resource> studioTopologyResourceProvider(const juce::String& url);

} // namespace abd::topology
