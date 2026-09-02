#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "AutoUpdaterConfig.h"

namespace ABDShared
{

class AutoUpdater final : private juce::Thread,
                          private juce::AsyncUpdater
{
public:
    struct UpdateInfo
    {
        juce::String version;
        juce::String releaseNotes;
        juce::String downloadUrl;
        juce::String fileName;
        juce::String sha256;
        juce::String releaseDate;
        bool isPrerelease = false;
    };

    explicit AutoUpdater(const AutoUpdaterConfig& config);
    ~AutoUpdater() override;

    void checkForUpdates(bool silent = false);
    void setCheckInterval(int hours);
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled; }
    juce::String getCurrentVersion() const { return config.currentVersion; }
    UpdateInfo getLastUpdateInfo() const { return lastUpdateInfo; }
    bool isUpdateAvailable() const { return updateAvailable; }

    using UpdateCallback = std::function<void(const UpdateInfo&, bool isManualCheck)>;
    void setUpdateCallback(UpdateCallback callback) { updateCallback = std::move(callback); }

private:
    void run() override;
    void handleAsyncUpdate() override;

    void performCheck();
    bool fetchLatestRelease(UpdateInfo& outInfo);
    void parseReleaseJson(const juce::String& json, UpdateInfo& outInfo);
    juce::String getPlatformAssetName() const;
    bool isNewerVersion(const juce::String& remote, const juce::String& local) const;
    void notifyUpdateAvailable(const UpdateInfo& info, bool isManualCheck);
    void log(const juce::String& message);
    juce::File getStateFile() const;
    void saveLastCheckTime();
    void loadLastCheckTime();

    AutoUpdaterConfig config;
    UpdateCallback updateCallback;
    UpdateInfo lastUpdateInfo;
    bool updateAvailable = false;
    bool enabled = true;
    bool manualCheckPending = false;
    juce::Time lastCheckTime;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoUpdater)
};

} // namespace ABDShared
