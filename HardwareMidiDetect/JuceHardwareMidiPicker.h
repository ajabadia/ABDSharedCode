/**
 * @file JuceHardwareMidiPicker.h
 * @brief Plug-and-play JUCE WebView2 component for MIDI hardware detection.
 * @details Modeled on ABDScope's JuceWebScopeComponent. The host prepares the
 *          bridge by injecting a MidiHardwareBackend (its own MidiOutput /
 *          MidiInput transport). The shared WebUI handles all detection logic and
 *          UX; this component serves the WebUI (embedded binary assets), forwards
 *          the inbound MIDI bytes to it, and relays the picked result (or cancel)
 *          back to the host via a callback.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include "MidiHardwareBackend.h"
#include "HardwareMidiPickerResourceProvider.h"

namespace abd::hwid
{

/**
 * @struct HardwarePickResult
 * @brief Result returned to the host when the picker closes with a selection.
 */
struct HardwarePickResult
{
    bool cancelled { true };
    std::string hardwareId;        /**< contract id, e.g. "korg_ms2000" */
    std::string displayName;       /**< human name, e.g. "Korg MS2000 / MS2000R" */
    std::string manufacturer;
    std::string model;
    std::string firmwareVersion;
};

using HardwarePickCallback = std::function<void(const HardwarePickResult&)>;

/**
 * @class JuceHardwareMidiPicker
 * @brief JUCE Component that embeds the HardwareMidiPicker WebUI in WebView2 and
 *        bridges it to a host-provided MidiHardwareBackend.
 */
class JuceHardwareMidiPicker : public juce::Component
{
public:
    JuceHardwareMidiPicker(MidiHardwareBackend& backend, HardwarePickCallback onResult)
        : midiBackend(backend),
          resultCallback(std::move(onResult)),
          webBrowser(juce::WebBrowserComponent::Options{}
                         .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                         .withNativeIntegrationEnabled(true)
                         .withResourceProvider(abd::hwid::hardwareMidiPickerResourceProvider)
                         .withEventListener("nativeEvent", [this](const juce::var& message) {
                             onNativeEvent(message);
                         }))
    {
        addAndMakeVisible(webBrowser);
        reload();
    }

    ~JuceHardwareMidiPicker() override
    {
        midiBackend.setReceiveCallback(nullptr);
        midiBackend.stopListening();
    }

    void resized() override
    {
        webBrowser.setBounds(getLocalBounds());
    }

    /** @brief Navigate back to the picker root (re-runs detection UI). */
    void reload()
    {
        auto rootUrl = juce::WebBrowserComponent::getResourceProviderRoot();
        webBrowser.goToURL(rootUrl);
    }

    /** @brief Re-open / refresh the picker and re-enable listening. */
    void startPick()
    {
        reload();
    }

    /** @brief Access the underlying WebBrowserComponent (e.g. for sizing). */
    [[nodiscard]] juce::WebBrowserComponent& getWebBrowser() noexcept { return webBrowser; }

private:
    void onNativeEvent(const juce::var& message)
    {
        if (!message.isObject()) return;

        auto action = message.getProperty("action", "").toString();

        if (action == "hardware.send")
        {
            // WebUI asks to send raw SysEx bytes to the physical MIDI out.
            auto b64 = message.getProperty("payload", "").toString();
            juce::MemoryOutputStream mem;
            if (juce::Base64::convertFromBase64(mem, b64))
            {
                const auto* data = static_cast<const uint8_t*>(mem.getData());
                const size_t size = mem.getDataSize();
                midiBackend.sendBytes(std::vector<uint8_t>(data, data + size));
            }
        }
        else if (action == "hardware.listen")
        {
            midiBackend.startListening();
            midiBackend.setReceiveCallback([this](const std::vector<uint8_t>& bytes) {
                // Forward inbound bytes to the WebUI on the message thread.
                juce::MessageManager::callAsync([this, bytes]() {
                    juce::MemoryBlock mb(bytes.data(), bytes.size());
                    auto b64 = mb.toBase64Encoding();
                    juce::String js = "if (window.__pushMidiBytes) { window.__pushMidiBytes("
                                    + juce::String("'") + b64 + juce::String("'); }");
                    webBrowser.evaluateJavascript(js);
                });
            });
        }
        else if (action == "hardware.stop")
        {
            midiBackend.setReceiveCallback(nullptr);
            midiBackend.stopListening();
        }
        else if (action == "hardware.result")
        {
            // WebUI finished: either a matched device or a cancel.
            HardwarePickResult result;
            result.cancelled = message.getProperty("cancelled", true);
            result.hardwareId = message.getProperty("hardwareId", "").toString().toStdString();
            result.displayName = message.getProperty("displayName", "").toString().toStdString();
            result.manufacturer = message.getProperty("manufacturer", "").toString().toStdString();
            result.model = message.getProperty("model", "").toString().toStdString();
            result.firmwareVersion = message.getProperty("firmwareVersion", "").toString().toStdString();

            midiBackend.setReceiveCallback(nullptr);
            if (resultCallback)
                resultCallback(result);
        }
    }

    MidiHardwareBackend& midiBackend;
    HardwarePickCallback resultCallback;
    juce::WebBrowserComponent webBrowser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuceHardwareMidiPicker)
};

} // namespace abd::hwid