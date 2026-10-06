/**
 * ABD Bank Manager — C++ Core Module Implementation
 */

#include "ABDBankManagerCore.h"

#include <utility>

namespace ABD::BankManager
{
namespace
{

const juce::Identifier rootType{"ABDBankManager"};
const juce::Identifier libraryType{"Library"};
const juce::Identifier bankType{"Bank"};
const juce::Identifier patchType{"Patch"};

juce::var stringArrayToVar(const juce::StringArray& values)
{
    juce::Array<juce::var> result;
    for (const auto& value : values)
        result.add(value);
    return juce::var(result);
}

juce::StringArray varToStringArray(const juce::var& value)
{
    juce::StringArray result;
    if (auto* array = value.getArray())
    {
        for (const auto& item : *array)
            if (item.isString())
                result.add(item.toString());
    }
    return result;
}

juce::String encodeMemoryBlock(const juce::MemoryBlock& block)
{
    return juce::Base64::toBase64(block.getData(), block.getSize());
}

bool decodeMemoryBlock(const juce::var& value, juce::MemoryBlock& destination)
{
    if (!value.isString())
        return false;

    juce::MemoryBlock decoded;
    juce::MemoryOutputStream output(decoded, false);
    if (!juce::Base64::convertFromBase64(output, value.toString()))
        return false;

    output.flush();
    destination = decoded;
    return true;
}

juce::var getVarProperty(const juce::var& object, const char* name, const juce::var& fallback = {})
{
    return object.hasProperty(name) ? object[name] : fallback;
}

juce::var getObjectProperty(const juce::DynamicObject& object, const char* name, const juce::var& fallback = {})
{
    return object.hasProperty(name) ? object.getProperty(name) : fallback;
}

void setCommonPatchProperties(juce::ValueTree& node, const Patch& patch)
{
    node.setProperty("id", patch.id, nullptr);
    node.setProperty("index", patch.index, nullptr);
    node.setProperty("name", patch.name, nullptr);
    node.setProperty("category", patch.category, nullptr);
    node.setProperty("author", patch.author, nullptr);
    node.setProperty("tags", stringArrayToVar(patch.tags), nullptr);
    node.setProperty("notes", patch.notes, nullptr);
    node.setProperty("originAddress", patch.originAddress, nullptr);
    node.setProperty("originModel", patch.originModel, nullptr);
    node.setProperty("originBank", patch.originBank, nullptr);
    node.setProperty("rawDataBase64", encodeMemoryBlock(patch.rawData), nullptr);
    node.setProperty("hardwareIds", stringArrayToVar(patch.hardwareIds), nullptr);
    node.setProperty("parameters", patch.parameters, nullptr);
    node.setProperty("isFavorite", patch.isFavorite, nullptr);
    node.setProperty("rating", patch.rating, nullptr);
    node.setProperty("versionNumber", patch.versionNumber, nullptr);
    node.setProperty("previousVersionId", patch.previousVersionId, nullptr);
    node.setProperty("fingerprint", patch.fingerprint, nullptr);
    node.setProperty("creationDate", patch.creationDate, nullptr);
    node.setProperty("modifiedDate", patch.modifiedDate, nullptr);
    node.setProperty("importSource", patch.importSource, nullptr);
    node.setProperty("importDate", patch.importDate, nullptr);
}

Patch patchFromValueTree(const juce::ValueTree& node)
{
    Patch patch;
    patch.id            = node.getProperty("id").toString();
    patch.index         = static_cast<int>(node.getProperty("index", 0));
    patch.name          = node.getProperty("name").toString();
    patch.category      = node.getProperty("category").toString();
    patch.author        = node.getProperty("author").toString();
    patch.tags          = varToStringArray(node.getProperty("tags"));
    patch.notes         = node.getProperty("notes").toString();
    patch.originAddress = node.getProperty("originAddress").toString();
    patch.originModel   = node.getProperty("originModel").toString();
    patch.originBank    = node.getProperty("originBank").toString();
    decodeMemoryBlock(node.getProperty("rawDataBase64"), patch.rawData);
    patch.hardwareIds       = varToStringArray(node.getProperty("hardwareIds"));
    patch.parameters        = node.getProperty("parameters", juce::var());
    patch.isFavorite        = static_cast<bool>(node.getProperty("isFavorite", false));
    patch.rating            = static_cast<int>(node.getProperty("rating", 0));
    patch.versionNumber     = static_cast<int>(node.getProperty("versionNumber", 1));
    patch.previousVersionId = node.getProperty("previousVersionId").toString();
    patch.fingerprint       = node.getProperty("fingerprint").toString();
    patch.creationDate      = node.getProperty("creationDate").toString();
    patch.modifiedDate      = node.getProperty("modifiedDate").toString();
    patch.importSource      = node.getProperty("importSource").toString();
    patch.importDate        = node.getProperty("importDate").toString();
    return patch;
}

void setCommonBankProperties(juce::ValueTree& node, const Bank& bank)
{
    node.setProperty("id", bank.id, nullptr);
    node.setProperty("name", bank.name, nullptr);
    node.setProperty("modelId", bank.modelId, nullptr);
    node.setProperty("hardwareIds", stringArrayToVar(bank.hardwareIds), nullptr);
    node.setProperty("manufacturer", bank.manufacturer, nullptr);
    node.setProperty("isFactory", bank.isFactory, nullptr);
    node.setProperty("isLocked", bank.isLocked, nullptr);
    node.setProperty("source", bank.source, nullptr);
    node.setProperty("imageUrl", bank.imageUrl, nullptr);
    node.setProperty("description", bank.description, nullptr);
    node.setProperty("bankAuthor", bank.bankAuthor, nullptr);
    node.setProperty("license", bank.license, nullptr);
    node.setProperty("tags", stringArrayToVar(bank.tags), nullptr);
    node.setProperty("bankNotes", bank.bankNotes, nullptr);
    node.setProperty("firmwareCompat", bank.firmwareCompat, nullptr);
    node.setProperty("knownIssues", bank.knownIssues, nullptr);
    node.setProperty("creationDate", bank.creationDate, nullptr);
    node.setProperty("modifiedDate", bank.modifiedDate, nullptr);
}

Bank bankFromValueTree(const juce::ValueTree& node)
{
    Bank bank;
    bank.id             = node.getProperty("id").toString();
    bank.name           = node.getProperty("name").toString();
    bank.modelId        = node.getProperty("modelId").toString();
    bank.hardwareIds    = varToStringArray(node.getProperty("hardwareIds"));
    bank.manufacturer   = node.getProperty("manufacturer").toString();
    bank.isFactory      = static_cast<bool>(node.getProperty("isFactory", false));
    bank.isLocked       = static_cast<bool>(node.getProperty("isLocked", false));
    bank.source         = node.getProperty("source").toString();
    bank.imageUrl       = node.getProperty("imageUrl").toString();
    bank.description    = node.getProperty("description").toString();
    bank.bankAuthor     = node.getProperty("bankAuthor").toString();
    bank.license        = node.getProperty("license").toString();
    bank.tags           = varToStringArray(node.getProperty("tags"));
    bank.bankNotes      = node.getProperty("bankNotes").toString();
    bank.firmwareCompat = node.getProperty("firmwareCompat").toString();
    bank.knownIssues    = node.getProperty("knownIssues").toString();
    bank.creationDate   = node.getProperty("creationDate").toString();
    bank.modifiedDate   = node.getProperty("modifiedDate").toString();

    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        const auto child = node.getChild(i);
        if (child.hasType(patchType))
            bank.patches.add(patchFromValueTree(child));
    }

    return bank;
}

juce::ValueTree patchToValueTree(const Patch& patch)
{
    juce::ValueTree node(patchType);
    setCommonPatchProperties(node, patch);
    return node;
}

juce::ValueTree bankToValueTree(const Bank& bank)
{
    juce::ValueTree node(bankType);
    setCommonBankProperties(node, bank);
    for (const auto& patch : bank.patches)
        node.addChild(patchToValueTree(patch), -1, nullptr);
    return node;
}

juce::var patchToVar(const Patch& patch)
{
    auto object         = juce::var(new juce::DynamicObject());
    auto* dynamicObject = object.getDynamicObject();
    dynamicObject->setProperty("id", patch.id);
    dynamicObject->setProperty("index", patch.index);
    dynamicObject->setProperty("name", patch.name);
    dynamicObject->setProperty("category", patch.category);
    dynamicObject->setProperty("author", patch.author);
    dynamicObject->setProperty("tags", stringArrayToVar(patch.tags));
    dynamicObject->setProperty("notes", patch.notes);
    dynamicObject->setProperty("originAddress", patch.originAddress);
    dynamicObject->setProperty("originModel", patch.originModel);
    dynamicObject->setProperty("originBank", patch.originBank);
    dynamicObject->setProperty("rawDataBase64", encodeMemoryBlock(patch.rawData));
    dynamicObject->setProperty("hardwareIds", stringArrayToVar(patch.hardwareIds));
    dynamicObject->setProperty("parameters", patch.parameters);
    dynamicObject->setProperty("isFavorite", patch.isFavorite);
    dynamicObject->setProperty("rating", patch.rating);
    dynamicObject->setProperty("versionNumber", patch.versionNumber);
    dynamicObject->setProperty("previousVersionId", patch.previousVersionId);
    dynamicObject->setProperty("fingerprint", patch.fingerprint);
    dynamicObject->setProperty("creationDate", patch.creationDate);
    dynamicObject->setProperty("modifiedDate", patch.modifiedDate);
    dynamicObject->setProperty("importSource", patch.importSource);
    dynamicObject->setProperty("importDate", patch.importDate);
    return object;
}

juce::var bankToVar(const Bank& bank)
{
    auto object         = juce::var(new juce::DynamicObject());
    auto* dynamicObject = object.getDynamicObject();
    dynamicObject->setProperty("id", bank.id);
    dynamicObject->setProperty("name", bank.name);
    dynamicObject->setProperty("modelId", bank.modelId);
    dynamicObject->setProperty("hardwareIds", stringArrayToVar(bank.hardwareIds));
    dynamicObject->setProperty("manufacturer", bank.manufacturer);
    dynamicObject->setProperty("isFactory", bank.isFactory);
    dynamicObject->setProperty("isLocked", bank.isLocked);
    dynamicObject->setProperty("source", bank.source);
    dynamicObject->setProperty("imageUrl", bank.imageUrl);
    dynamicObject->setProperty("description", bank.description);
    dynamicObject->setProperty("bankAuthor", bank.bankAuthor);
    dynamicObject->setProperty("license", bank.license);
    dynamicObject->setProperty("tags", stringArrayToVar(bank.tags));
    dynamicObject->setProperty("bankNotes", bank.bankNotes);
    dynamicObject->setProperty("firmwareCompat", bank.firmwareCompat);
    dynamicObject->setProperty("knownIssues", bank.knownIssues);
    dynamicObject->setProperty("creationDate", bank.creationDate);
    dynamicObject->setProperty("modifiedDate", bank.modifiedDate);

    juce::Array<juce::var> patches;
    for (const auto& patch : bank.patches)
        patches.add(patchToVar(patch));
    dynamicObject->setProperty("patches", juce::var(patches));
    return object;
}

} // namespace

BankManagerCore::BankManagerCore()
{
    // Forward incoming hardware bytes to the WebUI as base64 JSON events
    // (only while the WebUI has asked to listen, to avoid chatter).
    hardwareMidiPipe.setReceiveCallback([this](const juce::MemoryBlock& message) {
        if (!hardwareListen)
            return;

        auto data = juce::var(new juce::DynamicObject());
        data.getDynamicObject()->setProperty(
            "payload", juce::Base64::toBase64(message.getData(), message.getSize()));
        data.getDynamicObject()->setProperty("size", static_cast<int>(message.getSize()));
        sendToWebUI("hardware.receive", data);
    });
}

void BankManagerCore::setLibrary(Library nextLibrary)
{
    library          = std::move(nextLibrary);
    currentBankIndex = juce::jlimit(0, juce::jmax(0, library.banks.size() - 1), currentBankIndex);

    if (library.banks.isEmpty())
        currentPatchIndex = 0;
    else
        currentPatchIndex = juce::jlimit(0,
                                         juce::jmax(0, library.banks.getReference(currentBankIndex).patches.size() - 1),
                                         currentPatchIndex);
}

juce::ValueTree BankManagerCore::toValueTree() const
{
    juce::ValueTree root(rootType);
    root.setProperty("schemaVersion", valueTreeSchemaVersion, nullptr);
    root.setProperty("currentBankIndex", currentBankIndex, nullptr);
    root.setProperty("currentPatchIndex", currentPatchIndex, nullptr);

    juce::ValueTree libraryNode(libraryType);
    libraryNode.setProperty("version", library.version, nullptr);
    libraryNode.setProperty("activeBankId", library.activeBankId, nullptr);
    libraryNode.setProperty("activePresetIndex", library.activePresetIndex, nullptr);
    libraryNode.setProperty("lastImportPath", library.lastImportPath, nullptr);
    libraryNode.setProperty("lastExportPath", library.lastExportPath, nullptr);

    for (const auto& bank : library.banks)
        libraryNode.addChild(bankToValueTree(bank), -1, nullptr);

    root.addChild(libraryNode, -1, nullptr);
    return root;
}

void BankManagerCore::fromValueTree(const juce::ValueTree& vt)
{
    if (!vt.hasType(rootType))
        return;

    currentBankIndex  = static_cast<int>(vt.getProperty("currentBankIndex", 0));
    currentPatchIndex = static_cast<int>(vt.getProperty("currentPatchIndex", 0));

    const auto libraryNode = vt.getChildWithName(libraryType);
    if (libraryNode.isValid())
    {
        Library restored;
        restored.version           = static_cast<int>(libraryNode.getProperty("version", 1));
        restored.activeBankId      = libraryNode.getProperty("activeBankId").toString();
        restored.activePresetIndex = static_cast<int>(libraryNode.getProperty("activePresetIndex", 0));
        restored.lastImportPath    = libraryNode.getProperty("lastImportPath").toString();
        restored.lastExportPath    = libraryNode.getProperty("lastExportPath").toString();

        for (int i = 0; i < libraryNode.getNumChildren(); ++i)
        {
            const auto bankNode = libraryNode.getChild(i);
            if (bankNode.hasType(bankType))
                restored.banks.add(bankFromValueTree(bankNode));
        }

        library = std::move(restored);
    }

    setLibrary(library);
}

void BankManagerCore::selectPreset(int bankIndex, int patchIndex)
{
    if (library.banks.isEmpty())
    {
        currentBankIndex  = 0;
        currentPatchIndex = 0;
        return;
    }

    currentBankIndex          = juce::jlimit(0, library.banks.size() - 1, bankIndex);
    const auto& bank          = library.banks.getReference(currentBankIndex);
    currentPatchIndex         = bank.patches.isEmpty() ? 0 : juce::jlimit(0, bank.patches.size() - 1, patchIndex);
    library.activeBankId      = bank.id;
    library.activePresetIndex = currentPatchIndex;
}

void BankManagerCore::setWebUIMessageHandler(WebUIMessageHandler handler)
{
    webUIMessageHandler = std::move(handler);
}

juce::var BankManagerCore::toWebUIState(const Library& state,
                                        int selectedBankIndex,
                                        int selectedPatchIndex)
{
    auto object         = juce::var(new juce::DynamicObject());
    auto* dynamicObject = object.getDynamicObject();
    dynamicObject->setProperty("schemaVersion", valueTreeSchemaVersion);
    dynamicObject->setProperty("version", state.version);
    dynamicObject->setProperty("activeBankId", state.activeBankId);
    dynamicObject->setProperty("activePresetIndex", state.activePresetIndex);
    dynamicObject->setProperty("currentBankIndex", selectedBankIndex);
    dynamicObject->setProperty("currentPatchIndex", selectedPatchIndex);
    dynamicObject->setProperty("lastImportPath", state.lastImportPath);
    dynamicObject->setProperty("lastExportPath", state.lastExportPath);

    juce::Array<juce::var> banks;
    for (const auto& bank : state.banks)
        banks.add(bankToVar(bank));
    dynamicObject->setProperty("banks", juce::var(banks));
    return object;
}

int BankManagerCore::findBankIndex(const juce::var& data) const
{
    if (data.isString())
    {
        for (int i = 0; i < library.banks.size(); ++i)
            if (library.banks.getReference(i).id == data.toString())
                return i;
    }

    const auto bankId = getVarProperty(data, "bankId").toString();
    if (bankId.isNotEmpty())
    {
        for (int i = 0; i < library.banks.size(); ++i)
            if (library.banks.getReference(i).id == bankId)
                return i;
    }

    return static_cast<int>(getVarProperty(data, "bankIndex", 0));
}

int BankManagerCore::findPatchIndex(const Bank& bank, const juce::var& data) const
{
    const auto patchId = getVarProperty(data, "patchId").toString();
    if (patchId.isNotEmpty())
    {
        for (int i = 0; i < bank.patches.size(); ++i)
            if (bank.patches.getReference(i).id == patchId)
                return i;
    }

    return static_cast<int>(getVarProperty(data, "patchIndex", 0));
}

void BankManagerCore::loadWebUIState(const juce::var& data)
{
    Library restored;
    restored.version           = static_cast<int>(getVarProperty(data, "version", 1));
    restored.activeBankId      = getVarProperty(data, "activeBankId").toString();
    restored.activePresetIndex = static_cast<int>(getVarProperty(data, "activePresetIndex", 0));
    restored.lastImportPath    = getVarProperty(data, "lastImportPath").toString();
    restored.lastExportPath    = getVarProperty(data, "lastExportPath").toString();

    const auto banks = getVarProperty(data, "banks");
    if (auto* bankArray = banks.getArray())
    {
        for (const auto& bankValue : *bankArray)
        {
            if (auto* bankObject = bankValue.getDynamicObject())
            {
                Bank bank;
                bank.id             = bankObject->getProperty("id").toString();
                bank.name           = bankObject->getProperty("name").toString();
                bank.modelId        = bankObject->getProperty("modelId").toString();
                bank.hardwareIds    = varToStringArray(bankObject->getProperty("hardwareIds"));
                bank.manufacturer   = bankObject->getProperty("manufacturer").toString();
                bank.isFactory      = static_cast<bool>(getObjectProperty(*bankObject, "isFactory", false));
                bank.isLocked       = static_cast<bool>(getObjectProperty(*bankObject, "isLocked", false));
                bank.source         = bankObject->getProperty("source").toString();
                bank.imageUrl       = bankObject->getProperty("imageUrl").toString();
                bank.description    = bankObject->getProperty("description").toString();
                bank.bankAuthor     = bankObject->getProperty("bankAuthor").toString();
                bank.license        = bankObject->getProperty("license").toString();
                bank.tags           = varToStringArray(bankObject->getProperty("tags"));
                bank.bankNotes      = bankObject->getProperty("bankNotes").toString();
                bank.firmwareCompat = bankObject->getProperty("firmwareCompat").toString();
                bank.knownIssues    = bankObject->getProperty("knownIssues").toString();
                bank.creationDate   = bankObject->getProperty("creationDate").toString();
                bank.modifiedDate   = bankObject->getProperty("modifiedDate").toString();

                const auto patches = getObjectProperty(*bankObject, "patches");
                if (auto* patchArray = patches.getArray())
                {
                    for (const auto& patchValue : *patchArray)
                    {
                        if (auto* patchObject = patchValue.getDynamicObject())
                        {
                            Patch patch;
                            patch.id            = patchObject->getProperty("id").toString();
                            patch.index         = static_cast<int>(getObjectProperty(*patchObject, "index", 0));
                            patch.name          = patchObject->getProperty("name").toString();
                            patch.category      = patchObject->getProperty("category").toString();
                            patch.author        = patchObject->getProperty("author").toString();
                            patch.tags          = varToStringArray(patchObject->getProperty("tags"));
                            patch.notes         = patchObject->getProperty("notes").toString();
                            patch.originAddress = patchObject->getProperty("originAddress").toString();
                            patch.originModel   = patchObject->getProperty("originModel").toString();
                            patch.originBank    = patchObject->getProperty("originBank").toString();
                            decodeMemoryBlock(patchObject->getProperty("rawDataBase64"), patch.rawData);
                            patch.hardwareIds       = varToStringArray(patchObject->getProperty("hardwareIds"));
                            patch.parameters        = getObjectProperty(*patchObject, "parameters");
                            patch.isFavorite        = static_cast<bool>(getObjectProperty(*patchObject, "isFavorite", false));
                            patch.rating            = static_cast<int>(getObjectProperty(*patchObject, "rating", 0));
                            patch.versionNumber     = static_cast<int>(getObjectProperty(*patchObject, "versionNumber", 1));
                            patch.previousVersionId = patchObject->getProperty("previousVersionId").toString();
                            patch.fingerprint       = patchObject->getProperty("fingerprint").toString();
                            patch.creationDate      = patchObject->getProperty("creationDate").toString();
                            patch.modifiedDate      = patchObject->getProperty("modifiedDate").toString();
                            patch.importSource      = patchObject->getProperty("importSource").toString();
                            patch.importDate        = patchObject->getProperty("importDate").toString();
                            bank.patches.add(std::move(patch));
                        }
                    }
                }
                restored.banks.add(std::move(bank));
            }
        }
    }

    currentBankIndex  = static_cast<int>(getVarProperty(data, "currentBankIndex", 0));
    currentPatchIndex = static_cast<int>(getVarProperty(data, "currentPatchIndex", 0));
    setLibrary(std::move(restored));
}

void BankManagerCore::handleUpdateMetadata(const juce::var& data)
{
    const int bankIndex = findBankIndex(data);
    if (!juce::isPositiveAndBelow(bankIndex, library.banks.size()))
        return;

    auto& bank            = library.banks.getReference(bankIndex);
    const auto bankObject = data.getDynamicObject();
    if (bankObject == nullptr)
        return;

    const auto patchValue = getObjectProperty(*bankObject, "patch");
    if (auto* patchObject = patchValue.getDynamicObject())
    {
        const int patchIndex = findPatchIndex(bank, patchValue);
        if (juce::isPositiveAndBelow(patchIndex, bank.patches.size()))
        {
            auto& patch = bank.patches.getReference(patchIndex);
            if (patchObject->hasProperty("name")) patch.name = patchObject->getProperty("name").toString();
            if (patchObject->hasProperty("category")) patch.category = patchObject->getProperty("category").toString();
            if (patchObject->hasProperty("author")) patch.author = patchObject->getProperty("author").toString();
            if (patchObject->hasProperty("notes")) patch.notes = patchObject->getProperty("notes").toString();
            if (patchObject->hasProperty("isFavorite")) patch.isFavorite = static_cast<bool>(patchObject->getProperty("isFavorite"));
            if (patchObject->hasProperty("rating")) patch.rating = static_cast<int>(patchObject->getProperty("rating"));
        }
    }

    if (bankObject->hasProperty("bank"))
    {
        if (auto* bankMetadata = bankObject->getProperty("bank").getDynamicObject())
        {
            if (bankMetadata->hasProperty("name")) bank.name = bankMetadata->getProperty("name").toString();
            if (bankMetadata->hasProperty("description")) bank.description = bankMetadata->getProperty("description").toString();
            if (bankMetadata->hasProperty("bankAuthor")) bank.bankAuthor = bankMetadata->getProperty("bankAuthor").toString();
            if (bankMetadata->hasProperty("license")) bank.license = bankMetadata->getProperty("license").toString();
            if (bankMetadata->hasProperty("bankNotes")) bank.bankNotes = bankMetadata->getProperty("bankNotes").toString();
        }
    }

    library.activeBankId = bank.id;
    currentBankIndex     = bankIndex;
}

void BankManagerCore::handleWebUIMessage(const juce::String& type, const juce::var& data)
{
    if (type == "hardware.send")
    {
        handleHardwareSend(data);
        return;
    }

    if (type == "hardware.listen")
    {
        hardwareListen = data.isVoid() || static_cast<bool>(data);
        sendToWebUI("hardware.listen.ack", juce::var(hardwareListen ? 1 : 0));
        return;
    }

    if (type == "getState" || type == "requestState")
    {
        sendToWebUI("state", toWebUIState(library, currentBankIndex, currentPatchIndex));
        return;
    }

    if (type == "setState")
    {
        loadWebUIState(data);
        sendToWebUI("state", toWebUIState(library, currentBankIndex, currentPatchIndex));
        return;
    }

    if (type == "selectPreset")
    {
        const int bankIndex  = findBankIndex(data);
        const int patchIndex = library.banks.isEmpty() || !juce::isPositiveAndBelow(bankIndex, library.banks.size())
                                   ? 0
                                   : findPatchIndex(library.banks.getReference(bankIndex), data);
        selectPreset(bankIndex, patchIndex);
        sendToWebUI("presetSelected", toWebUIState(library, currentBankIndex, currentPatchIndex));
        return;
    }

    if (type == "updateMetadata")
    {
        handleUpdateMetadata(data);
        sendToWebUI("state", toWebUIState(library, currentBankIndex, currentPatchIndex));
        return;
    }

    sendToWebUI("error", juce::var("Unknown WebUI message: " + type));
}

void BankManagerCore::handleHardwareSend(const juce::var& data)
{
    // JSON bridge: bytes travel base64-encoded in the "payload" property.
    const auto payloadVar = data.getProperty("payload", juce::var());

    if (!payloadVar.isString())
    {
        sendToWebUI("hardware.error", juce::var("hardware.send: payload must be a base64 string"));
        return;
    }

    juce::MemoryOutputStream memStream;
    juce::Base64::convertFromBase64(memStream, payloadVar.toString());
    const auto block = juce::MemoryBlock(memStream.getData(), memStream.getDataSize());
    if (block.isEmpty())
    {
        sendToWebUI("hardware.error", juce::var("hardware.send: payload is empty or not valid base64"));
        return;
    }

    if (hardwareMidiPipe.sendToHardware(block))
        sendToWebUI("hardware.sent", juce::var(static_cast<int>(block.getSize())));
    else
        sendToWebUI("hardware.error", juce::var(hardwareMidiPipe.getLastError()));
}

void BankManagerCore::sendToWebUI(const juce::String& event, const juce::var& data)
{
    if (webUIMessageHandler != nullptr)
        webUIMessageHandler(event, data);
}

} // namespace ABD::BankManager
