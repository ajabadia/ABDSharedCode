/**
 * ABD Bank Manager — WebView transport adapter implementation
 */

#include "BankManagerWebViewAdapter.h"

#include <utility>

namespace ABD::BankManager
{
namespace
{

juce::var getProperty(const juce::var& object, const char* name)
{
    return object.hasProperty(name) ? object[name] : juce::var();
}

juce::String getStringProperty(const juce::var& object, const char* name)
{
    const auto value = getProperty(object, name);
    return value.isString() ? value.toString() : juce::String();
}

juce::var getPayload(const juce::var& message)
{
    const auto data = getProperty(message, "data");
    return data.isVoid() ? message : data;
}

} // namespace

BankManagerWebViewAdapter::BankManagerWebViewAdapter(BankManagerCore& coreToAdapt)
    : core(coreToAdapt)
{
    core.setWebUIMessageHandler([this](const juce::String& event, const juce::var& data) {
        handleCoreEvent(event, data);
    });
}

BankManagerWebViewAdapter::~BankManagerWebViewAdapter()
{
    core.setWebUIMessageHandler({});
}

void BankManagerWebViewAdapter::setPostMessageCallback(PostMessageCallback callback)
{
    postMessageCallback = std::move(callback);
}

void BankManagerWebViewAdapter::setFactoryContentExportCallback(FactoryContentExportCallback callback)
{
    factoryContentExportCallback = std::move(callback);
}

void BankManagerWebViewAdapter::handleWebViewMessage(const juce::String& json)
{
    juce::var message;
    const auto result = juce::JSON::parse(json, message);
    if (result.failed())
    {
        postError("Invalid WebView JSON: " + result.getErrorMessage());
        return;
    }

    handleWebViewMessage(message);
}

void BankManagerWebViewAdapter::handleWebViewMessage(const juce::var& message)
{
    if (!message.isObject())
    {
        postError("WebView message must be a JSON object");
        return;
    }

    auto action = getStringProperty(message, "action");
    if (action.isEmpty())
        action = getStringProperty(message, "type");

    if (action == "requestFullState")
        action = "getState";

    if (action.isEmpty())
    {
        postError("WebView message is missing action");
        return;
    }

    const auto payload = getPayload(message);
    if (action == "prepareFactoryContent")
    {
        if (factoryContentExportCallback == nullptr)
        {
            postError("Factory content export is unavailable in this host");
            return;
        }

        const auto result = factoryContentExportCallback(payload);
        handleCoreEvent("factoryContentExported", result);
        return;
    }

    core.handleWebUIMessage(action, payload);
}

void BankManagerWebViewAdapter::handleCoreEvent(const juce::String& event,
                                                const juce::var& data)
{
    if (postMessageCallback == nullptr)
        return;

    auto message = juce::var(new juce::DynamicObject());
    auto* object = message.getDynamicObject();
    object->setProperty("action", event);
    object->setProperty("data", data);
    object->setProperty("schemaVersion", BankManagerCore::valueTreeSchemaVersion);
    postMessageCallback(juce::JSON::toString(message, true));
}

void BankManagerWebViewAdapter::postError(const juce::String& message)
{
    auto data = juce::var(new juce::DynamicObject());
    data.getDynamicObject()->setProperty("message", message);
    handleCoreEvent("error", data);
}

} // namespace ABD::BankManager
