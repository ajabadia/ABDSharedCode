/**
 * ABD Bank Manager — WebView transport adapter
 *
 * Converts JSON messages from a WebView host into BankManagerCore calls and
 * forwards core events back as JSON. The adapter deliberately knows nothing
 * about WebView2 or any other browser implementation.
 */

#pragma once

#include "ABDBankManagerCore.h"

#include <functional>

namespace ABD::BankManager
{

class BankManagerWebViewAdapter
{
public:
    using PostMessageCallback          = std::function<void(const juce::String& json)>;
    using FactoryContentExportCallback = std::function<juce::var(const juce::var& data)>;

    explicit BankManagerWebViewAdapter(BankManagerCore& core);
    ~BankManagerWebViewAdapter();

    void setPostMessageCallback(PostMessageCallback callback);
    void setFactoryContentExportCallback(FactoryContentExportCallback callback);

    // Accept a JSON object received from the host WebView.
    void handleWebViewMessage(const juce::String& json);
    void handleWebViewMessage(const juce::var& message);

    BankManagerCore& getCore() noexcept { return core; }
    const BankManagerCore& getCore() const noexcept { return core; }

private:
    void handleCoreEvent(const juce::String& event, const juce::var& data);
    void postError(const juce::String& message);

    BankManagerCore& core;
    PostMessageCallback postMessageCallback;
    FactoryContentExportCallback factoryContentExportCallback;
};

} // namespace ABD::BankManager
