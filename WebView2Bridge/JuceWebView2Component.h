/**
 * @file JuceWebView2Component.h
 * @brief Base JUCE Component embedding a WebUI in WebView2.
 * @details Extracts the scaffolding shared by every ABDSynths WebView2 component
 *          (HardwareMidiDetect's JuceHardwareMidiPicker today, ABDScope's
 *          JuceWebScopeComponent next): webview2 backend + native integration,
 *          resource provider wiring, theme state/reload with theme query param,
 *          pageLoaded hook and bounds plumbing. Subclasses inject their own
 *          resource provider, extra event listeners and pageLoaded work.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <functional>
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
#include <optional>
#include <string>

namespace abd::webview2
{

/**
 * @class JuceWebView2Component
 * @brief Reusable base for JUCE Components that embed a module WebUI in WebView2.
 *
 * Owns the juce::WebBrowserComponent and its Options (webview2 backend, native
 * integration, resource provider). Registers a `pageLoaded` listener that calls
 * the virtual onPageLoaded() hook (default: re-apply the stored theme). Theme
 * state is a single currentTheme used by setTheme, applyStoredTheme and reload.
 */
class JuceWebView2Component : public juce::Component
{
public:
    using ResourceProvider = std::function<std::optional<juce::WebBrowserComponent::Resource>(const juce::String&)>;
    using EventListener    = std::function<void(const juce::var&)>;
    using EventListeners   = std::map<juce::String, EventListener>;
    using NativeFunction   = std::function<void(const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion)>;
    using NativeFunctions  = std::map<juce::String, NativeFunction>;

    explicit JuceWebView2Component(ResourceProvider provider,
                                   EventListeners extraListeners       = {},
                                   const juce::String& initialTheme    = "audiolab-light",
                                   NativeFunctions nativeFunctions     = {},
                                   const juce::File& userDataFolder    = {},
                                   const juce::String& initialUrl      = {})
        : webBrowser(buildOptions(std::move(provider), std::move(extraListeners),
                                  std::move(nativeFunctions), userDataFolder,
                                  [this]() { onPageLoaded(); })),
          currentTheme(initialTheme.toStdString()),
          startUrl(initialUrl)
    {
        addAndMakeVisible(webBrowser);
        reload(startUrl);
    }

    explicit JuceWebView2Component(juce::WebBrowserComponent::Options options,
                                   const juce::String& initialTheme = "audiolab-light",
                                   const juce::String& initialUrl   = {})
        : webBrowser(std::move(options)),
          currentTheme(initialTheme.toStdString()),
          startUrl(initialUrl)
    {
        addAndMakeVisible(webBrowser);
        reload(startUrl);
    }

    ~JuceWebView2Component() override = default;

    /** @brief Set the visual theme and re-apply it to the loaded page. */
    void setTheme(const std::string& themeName)
    {
        currentTheme = themeName;
        applyStoredTheme();
    }

    /** @brief Apply the stored theme via JS after the page is loaded. */
    virtual void applyStoredTheme()
    {
        if (currentTheme.empty()) return;
        const juce::String theme(currentTheme);
        const juce::String js =
            "(() => {"
            "  const t = '" +
            theme + "';"
                    "  document.documentElement.setAttribute('data-theme', t);"
                    "  if (document.body) {"
                    "    document.body.setAttribute('data-theme', t);"
                    "    document.body.dataset.theme = t;"
                    "    const classes = Array.from(document.body.classList).filter(c => !c.startsWith('theme-') && !c.startsWith('skin-'));"
                    "    classes.push('theme-' + t, 'skin-' + t);"
                    "    document.body.className = classes.join(' ');"
                    "  }"
                    "  if (typeof window.setTheme === 'function') {"
                    "    window.setTheme(t);"
                    "  }"
                    "})();";
        juce::MessageManager::callAsync([this, js]() { webBrowser.evaluateJavascript(js); });
    }

    /** @brief Reload the WebUI, keeping the current theme as a query param if using root URL. */
    void reload(const juce::String& overrideUrl = {})
    {
        juce::String rootUrl = overrideUrl.isNotEmpty() ? overrideUrl : juce::WebBrowserComponent::getResourceProviderRoot();
        if (overrideUrl.isEmpty() && !currentTheme.empty())
            rootUrl += (rootUrl.containsChar('?') ? "&theme=" : "?theme=") + juce::String(currentTheme);
        webBrowser.goToURL(rootUrl);
    }

    /** @brief Thread-safe / visible emit of event to WebUI (native -> JS channel). */
    void emitEventIfBrowserIsVisible(const juce::String& eventId, const juce::var& data)
    {
        webBrowser.emitEventIfBrowserIsVisible(eventId, data);
    }

    /** @brief Access the underlying WebBrowserComponent (e.g. for sizing, JS eval). */
    [[nodiscard]] juce::WebBrowserComponent& getWebBrowser() noexcept { return webBrowser; }

    void resized() override
    {
        webBrowser.setBounds(getLocalBounds());
    }

protected:
    /** @brief Called when the WebUI fires `pageLoaded`. Default re-applies the theme. */
    virtual void onPageLoaded()
    {
        applyStoredTheme();
    }

    juce::WebBrowserComponent webBrowser;
    std::string currentTheme;
    juce::String startUrl;

public:
    static juce::WebBrowserComponent::Options buildOptions(ResourceProvider provider,
                                                           EventListeners extraListeners,
                                                           NativeFunctions nativeFunctions,
                                                           const juce::File& userDataFolder,
                                                           const std::function<void()>& onPageLoaded)
    {
        auto options = juce::WebBrowserComponent::Options{}
                           .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                           .withNativeIntegrationEnabled(true)
                           .withResourceProvider(std::move(provider))
                           .withEventListener("pageLoaded", [onPageLoaded](const juce::var&) {
                               if (onPageLoaded) onPageLoaded();
                           });

        if (userDataFolder != juce::File{})
        {
            options = options.withWinWebView2Options(
                juce::WebBrowserComponent::Options::WinWebView2()
                    .withUserDataFolder(userDataFolder));
        }

        for (const auto& [eventId, listener] : extraListeners)
            options = options.withEventListener(eventId, listener);

        for (const auto& [funcName, func] : nativeFunctions)
            options = options.withNativeFunction(funcName, func);

        return options;
    }

    static juce::WebBrowserComponent::Options buildOptions(ResourceProvider provider,
                                                           EventListeners extraListeners,
                                                           const std::function<void()>& onPageLoaded)
    {
        return buildOptions(std::move(provider), std::move(extraListeners), {}, {}, onPageLoaded);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JuceWebView2Component)
};

} // namespace abd::webview2