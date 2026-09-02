#include "AutoUpdater.h"

namespace ABDShared
{

AutoUpdater::AutoUpdater(const AutoUpdaterConfig& cfg)
    : Thread("AutoUpdater"),
      config(cfg)
{
    loadLastCheckTime();
    
    if (config.checkOnStartup)
    {
        checkForUpdates(true);
    }
}

AutoUpdater::~AutoUpdater()
{
    stopThread(5000);
}

void AutoUpdater::checkForUpdates(bool silent)
{
    if (!enabled)
        return;

    manualCheckPending = !silent;
    triggerAsyncUpdate();
}

void AutoUpdater::setCheckInterval(int hours)
{
    config.checkIntervalHours = juce::jmax(1, hours);
}

void AutoUpdater::setEnabled(bool shouldEnable)
{
    enabled = shouldEnable;
    if (enabled && config.checkOnStartup)
    {
        checkForUpdates(true);
    }
}

void AutoUpdater::run()
{
    while (!threadShouldExit())
    {
        if (enabled)
        {
            auto now = juce::Time::getCurrentTime();
            auto hoursSinceLastCheck = (now - lastCheckTime).inHours();
            
            if (hoursSinceLastCheck >= config.checkIntervalHours)
            {
                checkForUpdates(false);
            }
        }
        
        wait(3600000);
    }
}

void AutoUpdater::handleAsyncUpdate()
{
    if (manualCheckPending || 
        (enabled && (juce::Time::getCurrentTime() - lastCheckTime).inHours() >= config.checkIntervalHours))
    {
        performCheck();
        manualCheckPending = false;
    }
}

void AutoUpdater::performCheck()
{
    UpdateInfo info;
    if (fetchLatestRelease(info))
    {
        lastUpdateInfo = info;
        lastCheckTime = juce::Time::getCurrentTime();
        saveLastCheckTime();

        if (isNewerVersion(info.version, config.currentVersion))
        {
            updateAvailable = true;
            notifyUpdateAvailable(info, manualCheckPending);
        }
        else
        {
            updateAvailable = false;
        }
    }
    manualCheckPending = false;
}

bool AutoUpdater::fetchLatestRelease(UpdateInfo& outInfo)
{
    juce::URL apiUrl(config.apiUrl + config.repoOwner + "/" + config.repoName + "/releases/latest");
    
    auto stream = std::unique_ptr<juce::InputStream>(apiUrl.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(10000)
            .withExtraHeaders("Accept: application/vnd.github.v3+json\nUser-Agent: " + config.userAgent)));

    if (!stream)
    {
        log("[AutoUpdater] Failed to connect to GitHub API");
        return false;
    }

    juce::String json = stream->readEntireStreamAsString();
    
    if (json.isEmpty())
    {
        log("[AutoUpdater] Empty response from GitHub API");
        return false;
    }

    try
    {
        parseReleaseJson(json, outInfo);
        return true;
    }
    catch (const std::exception& e)
    {
        log("[AutoUpdater] JSON parse error: " + juce::String(e.what()));
        return false;
    }
}

void AutoUpdater::parseReleaseJson(const juce::String& json, UpdateInfo& outInfo)
{
    auto result = juce::JSON::parse(json);
    if (!result.isObject())
        return;

    auto* obj = result.getDynamicObject();
    if (obj == nullptr)
        return;

    outInfo.version = obj->getProperty("tag_name").toString();
    outInfo.releaseNotes = obj->getProperty("body").toString();
    outInfo.releaseDate = obj->getProperty("published_at").toString();
    outInfo.isPrerelease = (bool) obj->getProperty("prerelease");

    auto assets = obj->getProperty("assets");
    if (assets.isArray())
    {
        auto* assetArray = assets.getArray();
        juce::String targetAsset = getPlatformAssetName();
        
        for (int i = 0; i < assetArray->size(); ++i)
        {
            auto* assetObj = assetArray->getUnchecked(i).getDynamicObject();
            if (assetObj)
            {
                juce::String name = assetObj->getProperty("name").toString();
                if (name == targetAsset)
                {
                    outInfo.downloadUrl = assetObj->getProperty("browser_download_url").toString();
                    outInfo.fileName = name;
                    break;
                }
            }
        }
    }
}

juce::String AutoUpdater::getPlatformAssetName() const
{
#if JUCE_WINDOWS
    return config.assetNames.windows;
#elif JUCE_MAC
    return config.assetNames.macos;
#elif JUCE_LINUX
    return config.assetNames.linux;
#else
    return "";
#endif
}

bool AutoUpdater::isNewerVersion(const juce::String& remote, const juce::String& local) const
{
    auto parseVersion = [](const juce::String& v) -> juce::Array<int>
    {
        juce::Array<int> parts;
        auto tokens = juce::StringArray::fromTokens(v, ".", "");
        for (auto& t : tokens)
            parts.add(t.getIntValue());
        return parts;
    };

    auto r = parseVersion(remote);
    auto l = parseVersion(local);
    const int numParts = juce::jmax(r.size(), l.size());
    
    for (int i = 0; i < numParts; ++i)
    {
        int rv = i < r.size() ? r[i] : 0;
        int lv = i < l.size() ? l[i] : 0;
        if (rv != lv)
            return rv > lv;
    }
    return false;
}

void AutoUpdater::notifyUpdateAvailable(const UpdateInfo& info, bool isManualCheck)
{
    if (updateCallback)
    {
        updateCallback(info, isManualCheck);
    }
    else
    {
        log("[AutoUpdater] Update available: " + info.version + " (current: " + config.currentVersion + ")");
    }
}

void AutoUpdater::log(const juce::String& message)
{
    if (config.logCallback)
        config.logCallback(message);
}

juce::File AutoUpdater::getStateFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(config.appName)
        .getChildFile("updater_state.json");
}

void AutoUpdater::saveLastCheckTime()
{
    auto file = getStateFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText("{\"lastCheck\":\"" + lastCheckTime.toISO8601(true) + "\"}");
}

void AutoUpdater::loadLastCheckTime()
{
    auto file = getStateFile();
    if (file.existsAsFile())
    {
        auto json = file.loadFileAsString();
        auto result = juce::JSON::parse(json);
        if (result.isObject())
        {
            auto timeStr = result.getDynamicObject()->getProperty("lastCheck").toString();
            if (timeStr.isNotEmpty())
                lastCheckTime = juce::Time::fromISO8601(timeStr);
        }
    }
}

} // namespace ABDShared
