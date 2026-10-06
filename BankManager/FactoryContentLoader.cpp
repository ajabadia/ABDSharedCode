#include "FactoryContentLoader.h"

#include <memory>

#if defined(ABD_FACTORY_CONTENT_EMBEDDED)
#include <ABDFactoryContent.h>
#endif

namespace ABD::BankManager
{
namespace
{

const juce::Identifier bankType{"Bank"};
const juce::Identifier patchType{"Patch"};

juce::var property(const juce::var& object, const char* name, const juce::var& fallback = {})
{
    return object.isObject() && object.hasProperty(name) ? object[name] : fallback;
}

juce::String stringProperty(const juce::var& object, const char* name)
{
    return property(object, name).toString();
}

juce::StringArray stringArrayProperty(const juce::var& object, const char* name)
{
    juce::StringArray result;
    if (auto* values = property(object, name).getArray())
        for (const auto& value : *values)
            if (value.isString())
                result.add(value.toString());
    return result;
}

void addDataUrl(const juce::MemoryBlock& image, const juce::String& filename, Bank& bank)
{
    if (image.isEmpty())
        return;

    auto extension = filename.fromLastOccurrenceOf(".", false, false).toLowerCase();
    auto mime      = extension == "png" ? "image/png"
                                        : (extension == "jpg" || extension == "jpeg" ? "image/jpeg" : "image/webp");

    juce::MemoryOutputStream encoded;
    juce::Base64::convertToBase64(encoded, image.getData(), image.getSize());
    bank.imageUrl = "data:" + juce::String(mime) + ";base64," + encoded.toString();
}

juce::Result readZipEntry(juce::ZipFile& zip,
                          const juce::String& filename,
                          juce::MemoryBlock& destination,
                          size_t maximumSize)
{
    const auto* entry = zip.getEntry(filename);
    if (entry == nullptr)
        return juce::Result::fail("Missing ZIP entry: " + filename);

    if (entry->uncompressedSize < 0 || static_cast<uint64_t>(entry->uncompressedSize) > maximumSize)
        return juce::Result::fail("ZIP entry is too large: " + filename);

    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(*entry));
    if (stream == nullptr)
        return juce::Result::fail("Cannot read ZIP entry: " + filename);

    stream->readIntoMemoryBlock(destination, -1);
    return juce::Result::ok();
}

Patch patchFromManifest(juce::ZipFile& zip,
                        const juce::var& patchValue,
                        int fallbackIndex,
                        juce::Result& result)
{
    Patch patch;
    patch.id = stringProperty(patchValue, "id");
    if (patch.id.isEmpty())
        patch.id = "factory-patch-" + juce::String(fallbackIndex);
    patch.index             = static_cast<int>(property(patchValue, "index", fallbackIndex));
    patch.name              = stringProperty(patchValue, "name");
    patch.category          = stringProperty(patchValue, "category");
    patch.author            = stringProperty(patchValue, "author");
    patch.tags              = stringArrayProperty(patchValue, "tags");
    patch.notes             = stringProperty(patchValue, "notes");
    patch.hardwareIds       = stringArrayProperty(patchValue, "hardwareIds");
    patch.parameters        = property(patchValue, "parameters");
    patch.isFavorite        = static_cast<bool>(property(patchValue, "isFavorite", false));
    patch.rating            = static_cast<int>(property(patchValue, "rating", 0));
    patch.versionNumber     = static_cast<int>(property(patchValue, "versionNumber", 1));
    patch.previousVersionId = stringProperty(patchValue, "previousVersionId");
    patch.fingerprint       = stringProperty(patchValue, "fingerprint");
    patch.creationDate      = stringProperty(patchValue, "creationDate");
    patch.modifiedDate      = stringProperty(patchValue, "modifiedDate");
    patch.importSource      = stringProperty(patchValue, "importSource");
    patch.importDate        = stringProperty(patchValue, "importDate");

    const auto rawDataFile = stringProperty(patchValue, "rawDataFile");
    if (rawDataFile.isEmpty() || rawDataFile.contains("..") || rawDataFile.startsWithChar('/') || rawDataFile.containsChar('\\'))
    {
        result = juce::Result::fail("Invalid patch path in factory content");
        return patch;
    }

    result = readZipEntry(zip, rawDataFile, patch.rawData, 1024 * 1024);
    return patch;
}

} // namespace

juce::Result FactoryContentLoader::loadBankFromZip(const void* data,
                                                   size_t dataSize,
                                                   Bank& destination)
{
    if (data == nullptr || dataSize == 0)
        return juce::Result::fail("Factory bank is empty");
    if (dataSize > 50 * 1024 * 1024)
        return juce::Result::fail("Factory bank exceeds the maximum ZIP size");

    juce::MemoryInputStream memStream(data, dataSize, false);
    juce::ZipFile zip(memStream);
    if (zip.getNumEntries() <= 0)
        return juce::Result::fail("Factory bank is not a valid ZIP");

    juce::MemoryBlock manifestData;
    auto result = readZipEntry(zip, "manifest.json", manifestData, 1024 * 1024);
    if (result.failed())
        return result;

    juce::var manifest;
    const auto parseResult = juce::JSON::parse(
        juce::String::fromUTF8(static_cast<const char*>(manifestData.getData()),
                               static_cast<int>(manifestData.getSize())),
        manifest);
    if (parseResult.failed() || !manifest.isObject())
        return juce::Result::fail("Factory manifest.json is invalid JSON");

    if (static_cast<int>(property(manifest, "version", 0)) != supportedManifestVersion || stringProperty(manifest, "format") != "abdbank")
    {
        return juce::Result::fail("Unsupported factory manifest format or version");
    }

    const auto bankValue = property(manifest, "bank");
    if (!bankValue.isObject())
        return juce::Result::fail("Factory manifest has no bank object");

    Bank loaded;
    loaded.id              = stringProperty(bankValue, "id");
    loaded.name            = stringProperty(bankValue, "name");
    loaded.modelId         = stringProperty(bankValue, "modelId");
    loaded.hardwareIds     = stringArrayProperty(bankValue, "hardwareIds");
    loaded.manufacturer    = stringProperty(bankValue, "manufacturer");
    loaded.isFactory       = true;
    loaded.isLocked        = true;
    loaded.includeInBundle = true;
    loaded.source          = stringProperty(bankValue, "source");
    loaded.description     = stringProperty(bankValue, "description");
    loaded.bankAuthor      = stringProperty(bankValue, "bankAuthor");
    loaded.license         = stringProperty(bankValue, "license");
    loaded.tags            = stringArrayProperty(bankValue, "tags");
    loaded.bankNotes       = stringProperty(bankValue, "bankNotes");
    loaded.firmwareCompat  = stringProperty(bankValue, "firmwareCompat");
    loaded.knownIssues     = stringProperty(bankValue, "knownIssues");
    loaded.creationDate    = stringProperty(bankValue, "creationDate");
    loaded.modifiedDate    = stringProperty(bankValue, "modifiedDate");

    const auto imageFile = stringProperty(bankValue, "imageUrl");
    if (imageFile.isNotEmpty() && !imageFile.contains("..") && !imageFile.startsWithChar('/') && !imageFile.containsChar('\\'))
    {
        juce::MemoryBlock image;
        if (readZipEntry(zip, imageFile, image, 1024 * 1024).wasOk())
            addDataUrl(image, imageFile, loaded);
    }

    const auto patches = property(manifest, "patches");
    if (auto* patchArray = patches.getArray())
    {
        if (patchArray->size() > 128)
            return juce::Result::fail("Factory bank contains too many patches");

        for (int i = 0; i < patchArray->size(); ++i)
        {
            juce::Result patchResult = juce::Result::ok();
            auto patch               = patchFromManifest(zip, patchArray->getReference(i), i, patchResult);
            if (patchResult.failed())
                return patchResult;
            loaded.patches.add(std::move(patch));
        }
    }

    if (loaded.id.isEmpty() || loaded.modelId.isEmpty() || loaded.name.isEmpty())
        return juce::Result::fail("Factory bank is missing required metadata");

    destination = std::move(loaded);
    return juce::Result::ok();
}

FactoryContentLoadResult FactoryContentLoader::loadEmbedded()
{
    FactoryContentLoadResult result;

#if defined(ABD_FACTORY_CONTENT_EMBEDDED)
    for (int i = 0; i < ABDFactoryContent::namedResourceListSize; ++i)
    {
        const auto filename = juce::String(ABDFactoryContent::originalFilenames[i]);
        if (!filename.endsWithIgnoreCase(".abdbank"))
            continue;

        ++result.discovered;
        int size          = 0;
        const auto* bytes = ABDFactoryContent::getNamedResource(ABDFactoryContent::namedResourceList[i], size);
        Bank bank;
        const auto loadResult = loadBankFromZip(bytes, static_cast<size_t>(juce::jmax(0, size)), bank);
        if (loadResult.failed())
        {
            result.errors.add(filename + ": " + loadResult.getErrorMessage());
            continue;
        }

        result.library.banks.add(std::move(bank));
        ++result.loaded;
    }
#endif

    result.library.version = 1;
    if (!result.library.banks.isEmpty())
        result.library.activeBankId = result.library.banks.getReference(0).id;
    return result;
}

} // namespace ABD::BankManager
