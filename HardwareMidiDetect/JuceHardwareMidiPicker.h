/**
 * @file JuceHardwareMidiPicker.h
 * @brief Plug-and-play JUCE WebView2 component for MIDI hardware detection.
 * @details The host prepares the bridge by injecting a MidiHardwareBackend.
 *          Detection is performed by the shared C++ HardwareMidiDetector;
 *          the embedded WebUI is a pure view that renders the detected device list
 *          and relays the user's selection back via callback.
 *          Webview scaffolding (webview2 backend, resource provider wiring,
 *          theme state/reload, pageLoaded hook) is inherited from the shared
 *          WebView2Bridge JuceWebView2Component base.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include "MidiHardwareBackend.h"
#include "HardwareMidiDetector.h"
#include "HardwareMidiPickerResourceProvider.h"
#include "WebView2Bridge/JuceWebView2Component.h"

namespace abd::hwid
{

/**
 * @struct HardwarePickResult
 * @brief Result returned to the host when the picker closes with a selection.
 */
struct HardwarePickResult
{
    bool cancelled { true };

    // Single selection (maxResults == 1)
    std::string hardwareId;
    std::string displayName;

    // Multi selection (maxResults > 1)
    std::vector<std::string> hardwareIds;
    std::vector<std::string> displayNames;

    // Common metadata
    std::string manufacturer;
    std::string model;
    std::string firmwareVersion;

    // All detected devices (for debugging / context)
    std::vector<DiscoveredDevice> allDetected;
};

using HardwarePickCallback = std::function<void(const HardwarePickResult&)>;

/**
 * @class JuceHardwareMidiPicker
 * @brief JUCE Component that embeds the HardwareMidiPicker WebUI in WebView2,
 *        runs detection via HardwareMidiDetector, and bridges to host MidiHardwareBackend.
 */
class JuceHardwareMidiPicker : public abd::webview2::JuceWebView2Component
{
public:
    JuceHardwareMidiPicker(MidiHardwareBackend& backend,
                           HardwarePickCallback onResult,
                           const std::vector<HardwareContract>& contracts = {},
                           const HardwareMidiDetector::DetectionConfig& config = {},
                           const juce::String& initialTheme = "audiolab-light")
        : JuceWebView2Component(abd::hwid::hardwareMidiPickerResourceProvider,
                                { { "nativeEvent", [this](const juce::var& message) {
                                       onNativeEvent(message);
                                   } } },
                                initialTheme),
          midiBackend(backend),
          detector(contracts),
          currentConfig(config),
          resultCallback(std::move(onResult))
    {
    }

    ~JuceHardwareMidiPicker() override
    {
        midiBackend.setReceiveCallback(nullptr);
        midiBackend.stopListening();
    }

    /** @brief Update the contracts used for detection (e.g. after late loading). */
    void setContracts(const std::vector<HardwareContract>& contracts)
    {
        detector.setContracts(contracts);
    }

    /** @brief Update detection configuration. */
    void setConfig(const HardwareMidiDetector::DetectionConfig& config)
    {
        currentConfig = config;
    }

    /** @brief Set the visual theme (ms2000, cz101, deepmind, juno, audiolab, audiolab-light, etc.). */
    using JuceWebView2Component::setTheme;

    /** @brief Push detection config (maxResults, autoSelectIfSingle) to the WebUI. */
    void pushConfigToWebUI()
    {
        juce::DynamicObject::Ptr cfg = new juce::DynamicObject();
        cfg->setProperty("maxResults", currentConfig.maxResults);
        cfg->setProperty("autoSelectIfSingle", currentConfig.autoSelectIfSingle);
        juce::String js = "if (window.__setConfig) window.__setConfig(" + juce::JSON::toString(juce::var(cfg.get())) + ");";
        juce::MessageManager::callAsync([this, js]() { getWebBrowser().evaluateJavascript(js); });
    }

    /** @brief Run a full scan with current config and present results in the WebUI. */
    void startPick()
    {
        auto results = detector.scanAllPorts(currentConfig, 350);
        lastDetectedDevices = results;

        if (currentConfig.autoSelectIfSingle && currentConfig.maxResults == 1 && results.size() == 1)
        {
            HardwarePickResult result;
            result.cancelled = false;
            result.hardwareId = results[0].hardwareId;
            result.displayName = results[0].displayName;
            result.manufacturer = results[0].manufacturer;
            result.model = results[0].model;
            result.firmwareVersion = results[0].firmwareVersion;
            result.allDetected = results;
            if (resultCallback)
                resultCallback(result);
            return;
        }
        pushConfigToWebUI();
        pushDevicesToWebUI(results);
    }

    /** @brief Run a scan with a specific config (overrides currentConfig). */
    void startPick(const HardwareMidiDetector::DetectionConfig& config)
    {
        currentConfig = config;
        startPick();
    }

    /** @brief Access the detector for advanced use (headless scans, etc.). */
    [[nodiscard]] HardwareMidiDetector& getDetector() noexcept { return detector; }

private:
    /** @brief Re-push config to the WebUI once the page is up (theme is re-applied by the base). */
    void onPageLoaded() override
    {
        pushConfigToWebUI();
    }

    void pushDevicesToWebUI(const std::vector<DiscoveredDevice>& devices)
    {
        juce::Array<juce::var> list;
        for (const auto& d : devices)
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty("id", juce::String(d.hardwareId));
            obj->setProperty("displayName", juce::String(d.displayName));
            obj->setProperty("manufacturer", juce::String(d.manufacturer));
            obj->setProperty("model", juce::String(d.model));
            obj->setProperty("firmwareVersion", juce::String(d.firmwareVersion));
            obj->setProperty("inPortName", d.inDevice.name);
            obj->setProperty("outPortName", d.outDevice.name);
            obj->setProperty("portIndex", d.portIndex);
            obj->setProperty("deviceId", static_cast<int>(d.deviceId));
            obj->setProperty("isSysExVerified", d.isSysExVerified);
            obj->setProperty("modelImage", juce::String(d.modelImage));
            obj->setProperty("brandLogo", juce::String(d.brandLogo));
            list.add(juce::var(obj));
        }
        juce::String js = "if (window.__setDetectedDevices) window.__setDetectedDevices(" + juce::JSON::toString(juce::var(list)) + ");";
        juce::MessageManager::callAsync([this, js]() { getWebBrowser().evaluateJavascript(js); });
    }

    void onNativeEvent(const juce::var& message)
    {
        if (!message.isObject()) return;

        auto action = message.getProperty("action", "").toString();

        if (action == "hardware.detect")
        {
            startPick();
        }
        else if (action == "hardware.refreshPorts")
        {
            midiBackend.refreshPorts();
            startPick();
        }
        else if (action == "hardware.result")
        {
            HardwarePickResult result;
            result.cancelled = message.getProperty("cancelled", true);

            if (!result.cancelled)
            {
                if (currentConfig.maxResults == 1)
                {
                    result.hardwareId = message.getProperty("hardwareId", "").toString().toStdString();
                    result.displayName = message.getProperty("displayName", "").toString().toStdString();
                }
                else
                {
                    auto idsVar = message.getProperty("hardwareIds", juce::var());
                    auto namesVar = message.getProperty("displayNames", juce::var());
                    if (idsVar.isArray())
                    {
                        for (const auto& v : *idsVar.getArray())
                            result.hardwareIds.push_back(v.toString().toStdString());
                    }
                    if (namesVar.isArray())
                    {
                        for (const auto& v : *namesVar.getArray())
                            result.displayNames.push_back(v.toString().toStdString());
                    }
                }

                result.manufacturer = message.getProperty("manufacturer", "").toString().toStdString();
                result.model = message.getProperty("model", "").toString().toStdString();
                result.firmwareVersion = message.getProperty("firmwareVersion", "").toString().toStdString();
            }

            result.allDetected = lastDetectedDevices;

            midiBackend.setReceiveCallback(nullptr);
            midiBackend.stopListening();

            if (resultCallback)
                resultCallback(result);
        }
    }

    MidiHardwareBackend& midiBackend;
    HardwareMidiDetector detector;
    HardwareMidiDetector::DetectionConfig currentConfig;
    HardwarePickCallback resultCallback;
    std::vector<DiscoveredDevice> lastDetectedDevices;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuceHardwareMidiPicker)
};

} // namespace abd::hwid
