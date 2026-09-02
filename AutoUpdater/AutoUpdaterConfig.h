#pragma once

#include <juce_core/juce_core.h>

namespace ABDShared
{

struct AutoUpdaterConfig
{
    juce::String currentVersion;

    juce::String repoOwner = "ajabadia";
    juce::String repoName;
    juce::String apiUrl = "https://api.github.com/repos/";

    int checkIntervalHours = 24;
    bool checkOnStartup = true;
    bool allowPrerelease = false;

    juce::String appName;
    juce::String userAgent;

    struct PlatformAssets
    {
        juce::String windows;
        juce::String macos;
        juce::String linux;
    };

    PlatformAssets assetNames;

    using LogCallback = std::function<void(const juce::String&)>;
    LogCallback logCallback;
};

} // namespace ABDShared
