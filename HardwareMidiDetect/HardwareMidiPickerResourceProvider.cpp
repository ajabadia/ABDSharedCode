#include "HardwareMidiPickerResourceProvider.h"
#include <juce_core/juce_core.h>
#include <cstring>
#include <vector>
#include <cstddef>
#include "HardwareMidiPickerAssets.h"

namespace abd::hwid
{

static juce::String getMimeTypeForFilename(const juce::String& filename)
{
    if (filename.endsWithIgnoreCase(".html")) return "text/html";
    if (filename.endsWithIgnoreCase(".css"))  return "text/css";
    if (filename.endsWithIgnoreCase(".js") || filename.endsWithIgnoreCase(".mjs")) return "application/javascript";
    if (filename.endsWithIgnoreCase(".png"))  return "image/png";
    if (filename.endsWithIgnoreCase(".jpg") || filename.endsWithIgnoreCase(".jpeg")) return "image/jpeg";
    if (filename.endsWithIgnoreCase(".webp")) return "image/webp";
    if (filename.endsWithIgnoreCase(".svg"))  return "image/svg+xml";
    if (filename.endsWithIgnoreCase(".ttf"))  return "font/ttf";
    if (filename.endsWithIgnoreCase(".woff")) return "font/woff";
    if (filename.endsWithIgnoreCase(".woff2")) return "font/woff2";
    if (filename.endsWithIgnoreCase(".json")) return "application/json";
    if (filename.endsWithIgnoreCase(".webmanifest")) return "application/manifest+json";
    return "application/octet-stream";
}

std::optional<juce::WebBrowserComponent::Resource> hardwareMidiPickerResourceProvider(const juce::String& url)
{
    juce::String path = url;

    if (path.startsWith("juce://"))
    {
        int hostEndIndex = path.indexOf(7, "/");
        if (hostEndIndex != -1)
            path = path.substring(hostEndIndex);
        else
            path = "/";
    }
    else if (path.startsWith("https://juce.backend")) path = path.substring(20);
    else if (path.startsWith("http://localhost"))    path = path.substring(16);
    else if (path.startsWith("https://localhost"))   path = path.substring(17);

    if (path == "/" || path.isEmpty()) path = "/index.html";
    if (path.startsWith("/")) path = path.substring(1);

    juce::String decodedPath = juce::URL::removeEscapeChars(path);

    if (decodedPath == "juce.js" || decodedPath.endsWith("/juce.js"))
        return std::nullopt; // Let JUCE WebBrowserComponent serve its built-in frontend script

    int binSize = 0;
    const char* binData = nullptr;

    juce::String filename = decodedPath.fromLastOccurrenceOf("/", false, false);
    if (filename.isEmpty())
        filename = decodedPath;

    // Pass 1: Direct match by filename against originalFilenames
    for (int i = 0; i < HardwareMidiPickerAssets::namedResourceListSize; ++i)
    {
        juce::String orig = juce::String::fromUTF8(HardwareMidiPickerAssets::originalFilenames[i]);
        if (orig.equalsIgnoreCase(filename))
        {
            binData = HardwareMidiPickerAssets::getNamedResource(HardwareMidiPickerAssets::namedResourceList[i], binSize);
            if (binData != nullptr)
                break;
        }
    }

    // Pass 2: Fallback to flattened resource identifier
    if (binData == nullptr)
    {
        juce::String flattenedName = filename.replace(".", "_").replace("-", "_").replace(" ", "_");
        if (juce::CharacterFunctions::isDigit(flattenedName[0]))
            flattenedName = "_" + flattenedName;
        binData = HardwareMidiPickerAssets::getNamedResource(flattenedName.toRawUTF8(), binSize);
    }

    if (binData != nullptr)
    {
        std::vector<std::byte> bytes(static_cast<size_t>(binSize));
        std::memcpy(bytes.data(), binData, static_cast<size_t>(binSize));
        return juce::WebBrowserComponent::Resource { std::move(bytes), getMimeTypeForFilename(filename).toStdString() };
    }

    return std::nullopt;
}

} // namespace abd::hwid