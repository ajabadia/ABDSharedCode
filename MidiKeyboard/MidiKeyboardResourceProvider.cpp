/**
 * @file MidiKeyboardResourceProvider.cpp
 * @brief Implementation of MidiKeyboardResourceProvider.
 * @author ABDSynths
 * @date 2026
 */

#include "MidiKeyboardResourceProvider.h"
#include <WebView2Bridge/WebView2ResourceProvider.h>
#include <juce_core/juce_core.h>

namespace abd::keyboard
{

namespace
{
static juce::File findMidiKeyboardDir()
{
    juce::File exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File dir = exeFile.getParentDirectory();

    for (int i = 0; i < 8 && dir.exists(); ++i)
    {
        const juce::File candidate = dir.getChildFile("ABDSharedCode").getChildFile("MidiKeyboard");
        if (candidate.isDirectory())
            return candidate;
        dir = dir.getParentDirectory();
    }

    juce::File cwd = juce::File::getCurrentWorkingDirectory();
    for (int i = 0; i < 6 && cwd.exists(); ++i)
    {
        const juce::File candidate = cwd.getChildFile("ABDSharedCode").getChildFile("MidiKeyboard");
        if (candidate.isDirectory())
            return candidate;
        const juce::File candidate2 = cwd.getChildFile("..").getChildFile("ABDSharedCode").getChildFile("MidiKeyboard");
        if (candidate2.isDirectory())
            return candidate2;
        cwd = cwd.getParentDirectory();
    }

    return {};
}
} // namespace

std::optional<juce::WebBrowserComponent::Resource> midiKeyboardResourceProvider(const juce::String& url)
{
    const juce::String decodedPath = abd::webview2::normalizeResourcePath(url);

    if (decodedPath == "juce.js" || decodedPath.endsWith("/juce.js"))
        return std::nullopt;

    auto kbdDir = findMidiKeyboardDir();
    if (kbdDir.isDirectory())
    {
        // 1. Direct file in MidiKeyboard root or subfolders (e.g. src/keyboard.js, src/keyboard.css)
        juce::File targetFile = kbdDir.getChildFile(decodedPath);
        if (targetFile.existsAsFile())
        {
            juce::MemoryBlock mb;
            if (targetFile.loadFileAsData(mb))
            {
                std::vector<std::byte> bytes(mb.getSize());
                std::memcpy(bytes.data(), mb.getData(), mb.getSize());
                return juce::WebBrowserComponent::Resource { std::move(bytes),
                    abd::webview2::getMimeTypeForFilename(targetFile.getFileName()).toStdString() };
            }
        }

        // 2. Check in MidiKeyboard/resources (e.g. index.html)
        juce::File resFile = kbdDir.getChildFile("resources").getChildFile(decodedPath);
        if (resFile.existsAsFile())
        {
            juce::MemoryBlock mb;
            if (resFile.loadFileAsData(mb))
            {
                std::vector<std::byte> bytes(mb.getSize());
                std::memcpy(bytes.data(), mb.getData(), mb.getSize());
                return juce::WebBrowserComponent::Resource { std::move(bytes),
                    abd::webview2::getMimeTypeForFilename(resFile.getFileName()).toStdString() };
            }
        }
    }

    // 3. Shared assets fallback (ABDSharedAssets styles, themes, components, images)
    return abd::webview2::resolveSharedAssetFile(decodedPath);
}

} // namespace abd::keyboard
