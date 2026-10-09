/**
 * @file WebView2ResourceProvider.cpp
 * @brief Shared WebBrowserComponent resource provider for embedded WebUI assets.
 * @author ABDSynths
 * @date 2026
 */

#include "WebView2ResourceProvider.h"
#include <cstddef>
#include <cstring>
#include <juce_core/juce_core.h>
#include <vector>

namespace abd::webview2
{

juce::String getMimeTypeForFilename(const juce::String& filename)
{
    if (filename.endsWithIgnoreCase(".html")) return "text/html";
    if (filename.endsWithIgnoreCase(".css")) return "text/css";
    if (filename.endsWithIgnoreCase(".js") || filename.endsWithIgnoreCase(".mjs")) return "application/javascript";
    if (filename.endsWithIgnoreCase(".png")) return "image/png";
    if (filename.endsWithIgnoreCase(".jpg") || filename.endsWithIgnoreCase(".jpeg")) return "image/jpeg";
    if (filename.endsWithIgnoreCase(".webp")) return "image/webp";
    if (filename.endsWithIgnoreCase(".svg")) return "image/svg+xml";
    if (filename.endsWithIgnoreCase(".ttf")) return "font/ttf";
    if (filename.endsWithIgnoreCase(".woff")) return "font/woff";
    if (filename.endsWithIgnoreCase(".woff2")) return "font/woff2";
    if (filename.endsWithIgnoreCase(".json")) return "application/json";
    if (filename.endsWithIgnoreCase(".webmanifest")) return "application/manifest+json";
    return "application/octet-stream";
}

juce::String normalizeResourcePath(const juce::String& url)
{
    juce::String path = url;

    // Strip query string and fragment before resolving the resource filename
    const int queryIndex = path.indexOfChar('?');
    if (queryIndex != -1)
        path = path.substring(0, queryIndex);
    const int fragIndex = path.indexOfChar('#');
    if (fragIndex != -1)
        path = path.substring(0, fragIndex);

    // Strip scheme and host if present (e.g. juce://backend/path -> /path)
    if (path.startsWith("juce://"))
    {
        const int hostEndIndex = path.indexOf(7, "/");
        path                   = (hostEndIndex != -1) ? path.substring(hostEndIndex) : "/";
    }
    else if (path.startsWith("https://juce.backend"))
        path = path.substring(20);
    else if (path.startsWith("http://localhost"))
        path = path.substring(16);
    else if (path.startsWith("https://localhost"))
        path = path.substring(17);

    if (path == "/" || path.isEmpty()) path = "/index.html";
    if (path.startsWith("/")) path = path.substring(1);

    // URL-decode the path (handles spaces encoded as %20, etc.)
    return juce::URL::removeEscapeChars(path);
}

std::optional<juce::WebBrowserComponent::Resource> resolveEmbeddedAsset(const juce::String& decodedPath,
                                                                        const BinaryAssetsCatalog& catalog)
{
    // Let JUCE WebBrowserComponent serve its built-in frontend script
    if (decodedPath == "juce.js" || decodedPath.endsWith("/juce.js"))
        return std::nullopt;

    juce::String normalizedPath = decodedPath.replace("\\", "/");
    while (normalizedPath.startsWith("/"))
        normalizedPath = normalizedPath.substring(1);

    int binSize         = 0;
    const char* binData = nullptr;

    // Pass 1: Suffix / relative path matching against originalFilenames
    for (int i = 0; i < catalog.namedResourceListSize; ++i)
    {
        juce::String orig = juce::String::fromUTF8(catalog.originalFilenames[i]).replace("\\", "/");
        if (orig.endsWithIgnoreCase("/" + normalizedPath) || orig.equalsIgnoreCase(normalizedPath))
        {
            binData = catalog.getNamedResource(catalog.namedResourceList[i], binSize);
            if (binData != nullptr)
                break;
        }
    }

    // Pass 2: Flattened full-path resource identifier fallback (e.g. "abdbank_root_html")
    if (binData == nullptr)
    {
        juce::String resourceName = normalizedPath.replace("/", "_").replace("\\", "_")
                                                 .replace(".", "_").replace("-", "_").replace(" ", "_");
        if (juce::CharacterFunctions::isDigit(resourceName[0]))
            resourceName = "_" + resourceName;
        binData = catalog.getNamedResource(resourceName.toRawUTF8(), binSize);
    }

    // Pass 3: Basename matching for Vite-bundled assets (e.g. assets/index.css)
    if (binData == nullptr && normalizedPath.startsWith("assets/"))
    {
        juce::String basename = normalizedPath.fromLastOccurrenceOf("/", false, false);
        if (basename.isNotEmpty())
        {
            juce::String flattenedBasename = basename.replace(".", "_").replace("-", "_").replace(" ", "_");
            binData = catalog.getNamedResource(flattenedBasename.toRawUTF8(), binSize);
        }
    }

    // Pass 4: Root-level basename fallback (guarded against nested collisions)
    if (binData == nullptr && normalizedPath.length() > 0 && !normalizedPath.containsChar('/'))
    {
        juce::String flattenedName = normalizedPath.replace(".", "_").replace("-", "_").replace(" ", "_");
        if (juce::CharacterFunctions::isDigit(flattenedName[0]))
            flattenedName = "_" + flattenedName;
        binData = catalog.getNamedResource(flattenedName.toRawUTF8(), binSize);
    }

    if (binData != nullptr)
    {
        std::vector<std::byte> bytes(static_cast<size_t>(binSize));
        std::memcpy(bytes.data(), binData, static_cast<size_t>(binSize));
        return juce::WebBrowserComponent::Resource{std::move(bytes), getMimeTypeForFilename(normalizedPath).toStdString()};
    }

    return std::nullopt;
}

static juce::File findSharedAssetsRoot()
{
    // Try to locate ABDSharedAssets relative to the current executable
    juce::File exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File dir     = exeFile.getParentDirectory();

    // Walk up to find ABDSharedAssets (monorepo root is typically 4-6 levels up from build artefacts)
    for (int i = 0; i < 8 && dir.exists(); ++i)
    {
        const juce::File candidate = dir.getChildFile("ABDSharedAssets");
        if (candidate.isDirectory())
            return candidate;
        dir = dir.getParentDirectory();
    }

    // Fallback: check common monorepo locations relative to CWD
    juce::File cwd = juce::File::getCurrentWorkingDirectory();
    for (int i = 0; i < 6 && cwd.exists(); ++i)
    {
        const juce::File candidate = cwd.getChildFile("ABDSharedAssets");
        if (candidate.isDirectory())
            return candidate;
        cwd = cwd.getParentDirectory();
    }

    // Fallback: try the known monorepo root
    const juce::File projectRoot = exeFile.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory();
    const juce::File candidate   = projectRoot.getChildFile("ABDSharedAssets");
    if (candidate.isDirectory())
        return candidate;

    return {};
}

std::optional<juce::WebBrowserComponent::Resource> resolveSharedAssetFile(const juce::String& decodedPath)
{
    static const juce::File assetsRoot = findSharedAssetsRoot();
    if (!assetsRoot.isDirectory())
        return std::nullopt;

    const juce::File file = assetsRoot.getChildFile(decodedPath);
    if (!file.existsAsFile())
        return std::nullopt;

    juce::MemoryBlock data;
    if (!file.loadFileAsData(data))
        return std::nullopt;

    std::vector<std::byte> bytes(static_cast<size_t>(data.getSize()));
    std::memcpy(bytes.data(), data.getData(), static_cast<size_t>(data.getSize()));
    return juce::WebBrowserComponent::Resource{std::move(bytes), getMimeTypeForFilename(file.getFileName()).toStdString()};
}

std::optional<juce::WebBrowserComponent::Resource> webView2ResourceProvider(const juce::String& url,
                                                                            const BinaryAssetsCatalog& catalog,
                                                                            const std::vector<juce::String>& sharedAssetPrefixes)
{
    const juce::String decodedPath = normalizeResourcePath(url);

    // First: embedded binary assets (index.html, JS, CSS, ...)
    if (auto embedded = resolveEmbeddedAsset(decodedPath, catalog))
        return embedded;

    // Second: optional ABDSharedAssets filesystem fallback for allowed prefixes
    if (!sharedAssetPrefixes.empty())
    {
        for (const auto& prefix : sharedAssetPrefixes)
        {
            if (decodedPath.startsWith(prefix))
                return resolveSharedAssetFile(decodedPath);
        }
    }

    return std::nullopt;
}

} // namespace abd::webview2