#pragma once

#if defined(JUCE_VERSION) || __has_include(<juce_gui_extra/juce_gui_extra.h>)
#include "../Core/ScopeDataCollector.h"
#include "../Core/ScopeFrameSerializer.h"
#include "ScopeResourceProvider.h"
#include <WebView2Bridge/JuceWebView2Component.h>
#include <atomic>
#include <cstddef>
#include <juce_gui_extra/juce_gui_extra.h>
#include <limits>
#include <string>
#include <vector>

namespace abd::scope
{

/**
 * @brief High-performance JUCE GUI Component encapsulating ABDScope WebUI inside WebView2.
 *
 * Inherits from abd::webview2::JuceWebView2Component for turnkey WebView2 backend initialization,
 * native integration, resource serving, theme synchronization, and lifecycle management.
 * Adds real-time decimation pump (30 FPS default) feeding probe telemetry to the WebUI canvas,
 * with on-demand tap subscription optimization.
 *
 * ## Tap activation policy (on-demand, backwards compatible)
 * - Before the WebUI sends any `SET_ACTIVE_TAP` message, every registered tap stays active so
 *   any lane renders out of the box (all-active fallback).
 * - The first `SET_ACTIVE_TAP { tapId, laneIdx }` message switches the component to on-demand
 *   mode: after that, only taps referenced by at least one lane subscription remain active.
 *   Inactive taps therefore cost ~0 CPU in the audio thread (see ScopeTap).
 */
class JuceWebScopeComponent : public abd::webview2::JuceWebView2Component,
                              private juce::Timer
{
public:
    explicit JuceWebScopeComponent(ScopeDataCollector& collector,
                                   double initialSampleRate         = 44100.0,
                                   int refreshRateHz                = 30,
                                   const juce::String& initialTheme = "audiolab")
        : abd::webview2::JuceWebView2Component(
              abd::scope::scopeResourceProvider,
              {{"SET_ACTIVE_TAP", [this](const juce::var& message) {
                    handleTapSubscription(message);
                }}},
              initialTheme),
          scopeCollector(collector), sampleRate(initialSampleRate)
    {
        // All-active fallback until the WebUI subscribes lanes (see class docs).
        activateAllTaps();

        if (refreshRateHz > 0)
            startTimerHz(refreshRateHz);
    }

    ~JuceWebScopeComponent() override
    {
        stopTimer();
    }

    void setSampleRate(double newSampleRate) noexcept
    {
        sampleRate.store(newSampleRate, std::memory_order_relaxed);
    }

    void selectTap(const std::string& tapName)
    {
        scopeCollector.selectTap(tapName);
    }

    [[nodiscard]] const std::string& getTheme() const noexcept { return currentTheme; }

private:
    static constexpr size_t NO_TAP = (std::numeric_limits<size_t>::max)();

    /**
     * All-active fallback used before any lane subscription arrives.
     */
    void activateAllTaps() noexcept
    {
        for (size_t i = 0; i < scopeCollector.getTapCount(); ++i)
        {
            auto* tap = scopeCollector.getTap(i);
            if (tap != nullptr)
                tap->setActive(true);
        }
    }

    /**
     * Handle `SET_ACTIVE_TAP { tapId, laneIdx }` posted by the WebUI when a lane
     * subscribes (or re-subscribes) to a probe. The first message switches the
     * component to on-demand mode; afterwards only lane-referenced taps stay active.
     */
    void handleTapSubscription(const juce::var& message)
    {
        const juce::String tapId = message["tapId"].toString();
        const int laneIdx        = static_cast<int>(message["laneIdx"]);

        if (tapId.isEmpty() || laneIdx < 0 || laneIdx > 64) return;

        const size_t tapIndex = scopeCollector.findTapIndex(tapId.toStdString());
        if (tapIndex == ScopeDataCollector::npos) return;

        if (m_laneTaps.size() <= static_cast<size_t>(laneIdx))
            m_laneTaps.resize(static_cast<size_t>(laneIdx) + 1, NO_TAP);

        if (m_laneTaps[static_cast<size_t>(laneIdx)] == tapIndex)
            return; // Lane already subscribed to this tap

        m_laneTaps[static_cast<size_t>(laneIdx)] = tapIndex;
        syncActiveTaps();
    }

    /**
     * Activate exactly the taps referenced by lane subscriptions.
     * No-op while m_laneTaps is empty (all-active fallback).
     */
    void syncActiveTaps() noexcept
    {
        if (m_laneTaps.empty()) return;

        const size_t total = scopeCollector.getTapCount();
        std::vector<size_t> referenceCount(total, 0);
        for (const size_t laneTap : m_laneTaps)
        {
            if (laneTap != NO_TAP && laneTap < total)
                referenceCount[laneTap]++;
        }

        for (size_t i = 0; i < total; ++i)
        {
            auto* tap = scopeCollector.getTap(i);
            if (tap != nullptr)
                tap->setActive(referenceCount[i] > 0);
        }
    }

    void timerCallback() override
    {
        const size_t count = scopeCollector.getTapCount();
        if (count == 0) return;

        const float sr = static_cast<float>(sampleRate.load(std::memory_order_relaxed));

        // Bundle every currently active tap so each subscribed lane receives its probe.
        std::string bundle = R"({"taps":{)";
        bool first         = true;
        for (size_t i = 0; i < count; ++i)
        {
            auto* tap = scopeCollector.getTap(i);
            if (tap == nullptr || !tap->isActive()) continue;

            std::string tapJson = frameSerializer.serializeActiveFrame(tap, sr);
            if (!tapJson.empty())
            {
                if (!first) bundle += ",";
                bundle += "\"" + getTapSlug(tap) + "\":" + tapJson;
                first = false;
            }
        }
        bundle += "}}";

        if (!first)
        {
            juce::String js = "if (window.__pushScopeFrame) { window.__pushScopeFrame(" + juce::String(bundle) + "); }";
            webBrowser.evaluateJavascript(js);
        }
    }

    ScopeDataCollector& scopeCollector;
    ScopeFrameSerializer frameSerializer{512};
    std::atomic<double> sampleRate{44100.0};

    std::vector<size_t> m_laneTaps; ///< laneIdx -> registered tap index (NO_TAP if unsubscribed)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuceWebScopeComponent)
};

} // namespace abd::scope
#endif
