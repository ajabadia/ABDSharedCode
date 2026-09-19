/**
 * @file StudioTopologyResourceProvider.cpp
 * @brief Implementation of StudioTopologyResourceProvider.
 * @author ABDSynths
 * @date 2026
 */

#include "StudioTopologyResourceProvider.h"
#include <WebView2Bridge/WebView2ResourceProvider.h>
#include <StudioTopologyAssets.h>

namespace abd::topology
{

namespace
{

const abd::webview2::BinaryAssetsCatalog& topologyAssetsCatalog()
{
    static const abd::webview2::BinaryAssetsCatalog catalog {
        StudioTopologyAssets::namedResourceListSize,
        StudioTopologyAssets::namedResourceList,
        StudioTopologyAssets::originalFilenames,
        StudioTopologyAssets::getNamedResource
    };
    return catalog;
}

} // namespace

std::optional<juce::WebBrowserComponent::Resource> studioTopologyResourceProvider(const juce::String& url)
{
    return abd::webview2::webView2ResourceProvider(url, topologyAssetsCatalog(),
                                                   { "styles/", "models/", "interfaces/", "brands/" });
}

} // namespace abd::topology
