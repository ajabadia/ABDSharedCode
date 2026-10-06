#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace ABD::BankManager
{

/**
 * Transport-agnostic MIDI byte pipe for the WebUI bridge.
 *
 * The WebUI builds complete SysEx messages from the model contracts and sends
 * them through the WebView adapter as `hardware.send`; incoming bytes from the
 * hardware are forwarded to the WebUI as `hardware.receive` events. This class
 * knows nothing about MIDI devices — the host (plugin/editor) injects a
 * transport via setSendFunction().
 */
class HardwareMidiPipe
{
public:
    /** Sends a complete message (typically one or more SysEx) to the hardware. */
    using SendFunction = std::function<bool(const juce::MemoryBlock& message)>;
    /** Receives a complete message (typically one SysEx) from the hardware. */
    using ReceiveCallback = std::function<void(const juce::MemoryBlock& message)>;

    void setSendFunction(SendFunction fn) { sendFunction = std::move(fn); }
    void setReceiveCallback(ReceiveCallback cb) { receiveCallback = std::move(cb); }

    /** Outgoing: WebUI → hardware. Returns false (and reports an error) when no transport is available. */
    bool sendToHardware(const juce::MemoryBlock& message)
    {
        if (!sendFunction)
        {
            lastError = "No MIDI transport available";
            return false;
        }
        if (message.isEmpty())
        {
            lastError = "Empty MIDI message";
            return false;
        }
        if (!sendFunction(message))
        {
            lastError = "MIDI transport failed to send";
            return false;
        }
        lastError = {};
        return true;
    }

    /** Incoming: hardware → WebUI. No-op when the WebUI is not listening. */
    void receiveFromHardware(const juce::MemoryBlock& message)
    {
        if (message.isEmpty() || receiveCallback == nullptr)
            return;
        receiveCallback(message);
    }

    const juce::String& getLastError() const noexcept { return lastError; }

private:
    SendFunction sendFunction;
    ReceiveCallback receiveCallback;
    juce::String lastError;
};

} // namespace ABD::BankManager
