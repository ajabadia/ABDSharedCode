#include "HardwareMidiPickerResourceProvider.h"
#include "WebView2Bridge/WebView2ResourceProvider.h"
#include "HardwareMidiPickerAssets.h"

namespace abd::hwid
{

namespace
{

// Static catalog description of the picker's juce_add_binary_data assets
// (generated header HardwareMidiPickerAssets.h). The generic WebView2Bridge
// provider does the URL normalization, MIME resolution and two-pass lookup.
const abd::webview2::BinaryAssetsCatalog& pickerAssetsCatalog()
{
    static const abd::webview2::BinaryAssetsCatalog catalog {
        HardwareMidiPickerAssets::namedResourceListSize,
        HardwareMidiPickerAssets::namedResourceList,
        HardwareMidiPickerAssets::originalFilenames,
        HardwareMidiPickerAssets::getNamedResource
    };
    return catalog;
}

} // namespace

std::optional<juce::WebBrowserComponent::Resource> hardwareMidiPickerResourceProvider(const juce::String& url)
{
    // Embedded binary assets first (index.html, JS, ...), then the shared
    // ABDSharedAssets filesystem fallback for styles/, models/, brands/.
    return abd::webview2::webView2ResourceProvider(url, pickerAssetsCatalog(),
                                                   { "styles/", "models/", "brands/" });
}

} // namespace abd::hwid
