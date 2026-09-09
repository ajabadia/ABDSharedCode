/**
 * @file WebView2ResourceProvider.h
 * @brief Shared WebBrowserComponent resource provider for embedded WebUI assets.
 * @details Generic implementation used by every ABDSynths module that embeds a
 *          WebUI in WebView2 (HardwareMidiDetect today, ABDScope next). It
 *          normalizes WebBrowserComponent URLs, resolves files from the module's
 *          juce_add_binary_data catalog, and can optionally fall back to the
 *          ABDSharedAssets filesystem for shared styles/models/brands.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>
#include <vector>

namespace abd::webview2
{

/**
 * @brief Description of a juce_add_binary_data generated asset catalog.
 * @details Every generated catalog namespace (e.g. `ABDScopeWebAssets`,
 *          `HardwareMidiPickerAssets`) exposes the same four symbols; fill this
 *          struct from your module's generated header.
 */
struct BinaryAssetsCatalog
{
    int namedResourceListSize = 0;
    const char* const* namedResourceList = nullptr;
    const char* const* originalFilenames = nullptr;
    const char* (*getNamedResource)(const char*, int&) = nullptr;
};

/** @brief MIME type for a resource filename (html/css/js/png/jpg/webp/svg/fonts/json). */
juce::String getMimeTypeForFilename(const juce::String& filename);

/**
 * @brief Normalize a WebBrowserComponent URL into a module-relative resource path.
 * @details Strips query string, fragment, scheme and host (juce://, https://juce.backend,
 *          localhost), defaults to /index.html and URL-decodes the result.
 */
juce::String normalizeResourcePath(const juce::String& url);

/**
 * @brief Resolve a normalized, URL-decoded path against an embedded binary catalog.
 * @details Two-pass lookup: direct filename match against originalFilenames, then a
 *          flattened resource-identifier fallback. Returns nullopt for `juce.js`
 *          (served by JUCE itself) and unknown assets.
 */
std::optional<juce::WebBrowserComponent::Resource> resolveEmbeddedAsset(const juce::String& decodedPath,
                                                                        const BinaryAssetsCatalog& catalog);

/**
 * @brief Optional filesystem fallback for shared assets (styles/models/brands).
 * @details Locates ABDSharedAssets relative to the executable/CWD and serves the
 *          file when present. Returns nullopt when ABDSharedAssets is unavailable.
 */
std::optional<juce::WebBrowserComponent::Resource> resolveSharedAssetFile(const juce::String& decodedPath);

/**
 * @brief Full resource provider pipeline: normalize -> embedded -> shared-assets fallback.
 * @param url               Raw URL requested by WebBrowserComponent.
 * @param catalog           The module's embedded binary asset catalog.
 * @param sharedAssetPrefixes Optional path prefixes that may be served from the
 *                          ABDSharedAssets filesystem (e.g. {"styles/", "models/", "brands/"}).
 *                          Empty (default) disables the filesystem fallback.
 */
std::optional<juce::WebBrowserComponent::Resource> webView2ResourceProvider(const juce::String& url,
                                                                            const BinaryAssetsCatalog& catalog,
                                                                            const std::vector<juce::String>& sharedAssetPrefixes = {});

} // namespace abd::webview2