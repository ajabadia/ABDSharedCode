#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace abd::scope {

std::optional<juce::WebBrowserComponent::Resource> scopeResourceProvider(const juce::String& url);

} // namespace abd::scope