/**
 * @file JuceStudioTopologyComponent.h
 * @brief JUCE component embedding the Studio Topology WebUI in WebView2.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <WebView2Bridge/JuceWebView2Component.h>
#include "StudioTopologyResourceProvider.h"
#include <nlohmann/json.hpp>

namespace abd::topology
{

/**
 * @class JuceStudioTopologyComponent
 * @brief Component that embeds the interactive Studio Topology WebUI,
 *        following the same architecture and calling conventions as JuceHardwareMidiPicker and JuceWebScopeComponent.
 */
class JuceStudioTopologyComponent : public abd::webview2::JuceWebView2Component
{
public:
    explicit JuceStudioTopologyComponent(const juce::String& initialTheme = "audiolab-light")
        : JuceWebView2Component(abd::topology::studioTopologyResourceProvider,
                                {},
                                initialTheme)
    {
    }

    ~JuceStudioTopologyComponent() override = default;

    /**
     * @brief Sends current studio topology data to the WebUI view.
     */
    void updateTopology(const nlohmann::json& topologyData)
    {
        lastTopologyJson = topologyData.dump();
        sendTopologyToWeb();
    }

protected:
    void onPageLoaded() override
    {
        JuceWebView2Component::onPageLoaded();
        if (!lastTopologyJson.empty())
        {
            sendTopologyToWeb();
        }
    }

private:
    void sendTopologyToWeb()
    {
        if (lastTopologyJson.empty()) return;

        juce::String js = "if (window.updateTopology) { window.updateTopology(" + juce::String(lastTopologyJson) + "); }";
        juce::MessageManager::callAsync([this, js]() {
            webBrowser.evaluateJavascript(js);
        });
    }

    std::string lastTopologyJson;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuceStudioTopologyComponent)
};

} // namespace abd::topology
