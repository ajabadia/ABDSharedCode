#include "HardwareMidiDetector.h"
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <set>

namespace abd::hwid
{

HardwareMidiDetector::HardwareMidiDetector(std::vector<HardwareContract> contracts)
    : registeredContracts(std::move(contracts))
{
}

HardwareMidiDetector::~HardwareMidiDetector()
{
}

void HardwareMidiDetector::setContracts(std::vector<HardwareContract> contracts)
{
    registeredContracts = std::move(contracts);
}

std::vector<uint8_t> HardwareMidiDetector::parseHexBytes(const std::string& hexStr)
{
    std::vector<uint8_t> bytes;
    std::stringstream ss(hexStr);
    std::string token;
    while (ss >> token)
    {
        if (token.rfind("0x", 0) == 0 || token.rfind("0X", 0) == 0)
            token = token.substr(2);

        try
        {
            auto val = std::stoul(token, nullptr, 16);
            bytes.push_back(static_cast<uint8_t>(val & 0xFF));
        }
        catch (...)
        {
        }
    }
    return bytes;
}

juce::MidiMessage HardwareMidiDetector::makeIdentityRequest(uint8_t deviceId)
{
    const uint8_t sysexBytes[] = { 0x7E, deviceId, 0x06, 0x01 };
    return juce::MidiMessage::createSysExMessage(sysexBytes, sizeof(sysexBytes));
}

std::vector<juce::MidiMessage> HardwareMidiDetector::buildDetectionQueries(const std::vector<HardwareContract>& contracts)
{
    std::vector<juce::MidiMessage> queries;

    queries.push_back(makeIdentityRequest(0x7F));

    std::vector<std::string> seen;
    auto alreadySeen = [&seen](const std::string& hex) {
        for (const auto& s : seen)
            if (s == hex) return true;
        return false;
    };

    for (const auto& c : contracts)
    {
        auto sysexHex = c.autoDetectSysEx;
        if (sysexHex.empty())
            sysexHex = c.midiIdentity.sysexHeaderHex;

        if (sysexHex.empty() || alreadySeen(sysexHex))
            continue;

        auto bytes = parseHexBytes(sysexHex);
        if (bytes.empty())
            continue;

        queries.push_back(juce::MidiMessage::createSysExMessage(bytes.data(), static_cast<int>(bytes.size())));
        seen.push_back(sysexHex);
    }

    return queries;
}

bool HardwareMidiDetector::parseIdentityReply(const juce::MidiMessage& msg,
                                              DiscoveredDevice& outDevice,
                                              const std::vector<HardwareContract>& contracts)
{
    if (!msg.isSysEx() || contracts.empty())
        return false;

    const auto* data = msg.getSysExData();
    const int size = msg.getSysExDataSize();

    if (data == nullptr || size < 4)
        return false;

    int bestScore = 0;
    DiscoveredDevice bestDev;

    for (const auto& c : contracts)
    {
        if (!c.midiIdentity.sysexHeaderHex.empty())
        {
            auto headerBytes = parseHexBytes(c.midiIdentity.sysexHeaderHex);
            if (!headerBytes.empty() && size >= static_cast<int>(headerBytes.size()))
            {
                bool matches = true;
                for (size_t i = 0; i < headerBytes.size(); ++i)
                {
                    if (data[i] != headerBytes[i])
                    {
                        matches = false;
                        break;
                    }
                }
                if (matches && bestScore < 10)
                {
                    bestScore = 10;
                    bestDev.hardwareId = c.id;
                    bestDev.displayName = c.displayName;
                    bestDev.manufacturer = c.midiIdentity.manufacturer;
                    bestDev.model = c.midiIdentity.model;
                    bestDev.isSysExVerified = true;
                }
            }
        }

        if (data[0] == 0x7E && data[2] == 0x06 && data[3] == 0x02 && size >= 5)
        {
            if (c.midiIdentity.manufacturerIdHex.empty())
                continue;

            auto mfgBytes = parseHexBytes(c.midiIdentity.manufacturerIdHex);
            if (mfgBytes.empty() || size < 4 + static_cast<int>(mfgBytes.size()))
                continue;

            bool mfgMatches = true;
            for (size_t i = 0; i < mfgBytes.size(); ++i)
            {
                if (data[4 + i] != mfgBytes[i])
                {
                    mfgMatches = false;
                    break;
                }
            }
            if (!mfgMatches)
                continue;

            int familyOffset = 4 + static_cast<int>(mfgBytes.size());

            if (!c.midiIdentity.modelIdHex.empty())
            {
                auto modelBytes = parseHexBytes(c.midiIdentity.modelIdHex);
                int modelOffset = familyOffset + 2;

                bool modelMatches = false;
                if (size >= modelOffset + static_cast<int>(modelBytes.size()))
                {
                    modelMatches = true;
                    for (size_t i = 0; i < modelBytes.size(); ++i)
                    {
                        if (data[modelOffset + i] != modelBytes[i])
                        {
                            modelMatches = false;
                            break;
                        }
                    }
                }

                if (!modelMatches)
                {
                    if (size >= familyOffset + static_cast<int>(modelBytes.size()))
                    {
                        modelMatches = true;
                        for (size_t i = 0; i < modelBytes.size(); ++i)
                        {
                            if (data[familyOffset + i] != modelBytes[i])
                            {
                                modelMatches = false;
                                break;
                            }
                        }
                    }
                }

                if (modelMatches && bestScore < 10)
                {
                    bestScore = 10;
                    bestDev.deviceId = data[1];
                    bestDev.hardwareId = c.id;
                    bestDev.displayName = c.displayName;
                    bestDev.manufacturer = c.midiIdentity.manufacturer;
                    bestDev.model = c.midiIdentity.model;
                    bestDev.isSysExVerified = true;

                    int revOffset = modelOffset + static_cast<int>(modelBytes.size());
                    if (size >= revOffset + 4)
                    {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%02X%02X.%02X%02X",
                                      data[revOffset], data[revOffset + 1],
                                      data[revOffset + 2], data[revOffset + 3]);
                        bestDev.firmwareVersion = buf;
                    }
                }
            }
            else if (bestScore < 1)
            {
                bestScore = 1;
                bestDev.deviceId = data[1];
                bestDev.hardwareId = c.id;
                bestDev.displayName = c.displayName;
                bestDev.manufacturer = c.midiIdentity.manufacturer;
                bestDev.model = c.midiIdentity.model;
                bestDev.isSysExVerified = true;
            }
        }
    }

    if (bestScore > 0)
    {
        outDevice = bestDev;
        return true;
    }

    return false;
}

std::optional<DiscoveredDevice> HardwareMidiDetector::matchFromPortNames(const juce::MidiDeviceInfo& inDev,
                                                                          const juce::MidiDeviceInfo& outDev,
                                                                          const std::vector<HardwareContract>& contracts)
{
    juce::String combined = inDev.name + " " + outDev.name;
    int bestMatchLength = 0;
    std::optional<DiscoveredDevice> bestDevice;

    for (const auto& c : contracts)
    {
        for (const auto& kw : c.midiIdentity.portNameMatches)
        {
            if (kw.empty()) continue;
            if (combined.containsIgnoreCase(juce::String(kw)))
            {
                if (static_cast<int>(kw.length()) > bestMatchLength)
                {
                    bestMatchLength = static_cast<int>(kw.length());
                    DiscoveredDevice dev;
                    dev.inDevice = inDev;
                    dev.outDevice = outDev;
                    dev.hardwareId = c.id;
                    dev.displayName = c.displayName;
                    dev.manufacturer = c.midiIdentity.manufacturer;
                    dev.model = c.midiIdentity.model;
                    dev.isSysExVerified = false;
                    bestDevice = dev;
                }
            }
        }

        if (c.midiIdentity.model.length() >= 4 && combined.containsIgnoreCase(juce::String(c.midiIdentity.model)))
        {
            if (static_cast<int>(c.midiIdentity.model.length()) > bestMatchLength)
            {
                bestMatchLength = static_cast<int>(c.midiIdentity.model.length());
                DiscoveredDevice dev;
                dev.inDevice = inDev;
                dev.outDevice = outDev;
                dev.hardwareId = c.id;
                dev.displayName = c.displayName;
                dev.manufacturer = c.midiIdentity.manufacturer;
                dev.model = c.midiIdentity.model;
                dev.isSysExVerified = false;
                bestDevice = dev;
            }
        }
    }

    return bestDevice;
}

void HardwareMidiDetector::handleIncomingMidiMessage(juce::MidiInput* /*source*/, const juce::MidiMessage& message)
{
    DiscoveredDevice dev;
    if (parseIdentityReply(message, dev, registeredContracts))
    {
        const juce::ScopedLock sl(scanLock);
        currentScanResults.push_back(std::move(dev));
    }
}

std::vector<DiscoveredDevice> HardwareMidiDetector::scanAllPorts(const DetectionConfig& config, int timeoutMs)
{
    std::vector<DiscoveredDevice> discovered;

    auto midiInputs = juce::MidiInput::getAvailableDevices();
    auto midiOutputs = juce::MidiOutput::getAvailableDevices();

    if (midiOutputs.isEmpty())
        return discovered;

    auto queries = buildDetectionQueries(registeredContracts);

    for (int outIdx = 0; outIdx < midiOutputs.size(); ++outIdx)
    {
        const auto& outDevInfo = midiOutputs[outIdx];
        juce::MidiDeviceInfo inDevInfo;
        bool hasInput = false;

        for (const auto& inCandidate : midiInputs)
        {
            if (inCandidate.name == outDevInfo.name || inCandidate.identifier == outDevInfo.identifier)
            {
                inDevInfo = inCandidate;
                hasInput = true;
                break;
            }
        }
        if (!hasInput && !midiInputs.isEmpty())
        {
            inDevInfo = midiInputs[0];
            hasInput = true;
        }

        auto outPort = juce::MidiOutput::openDevice(outDevInfo.identifier);
        if (outPort == nullptr) continue;

        std::unique_ptr<juce::MidiInput> inPort;
        if (hasInput)
        {
            inPort = juce::MidiInput::openDevice(inDevInfo.identifier, this);
            if (inPort != nullptr)
            {
                {
                    const juce::ScopedLock sl(scanLock);
                    currentScanResults.clear();
                }
                inPort->start();
            }
        }

        for (const auto& q : queries)
            outPort->sendMessageNow(q);

        juce::Thread::sleep(std::clamp(timeoutMs, 50, 600));

        bool foundSysEx = false;
        {
            const juce::ScopedLock sl(scanLock);
            if (!currentScanResults.empty())
            {
                for (auto& item : currentScanResults)
                {
                    item.inDevice = inDevInfo;
                    item.outDevice = outDevInfo;
                    item.portIndex = outIdx;
                    discovered.push_back(item);
                }
                foundSysEx = true;
            }
        }

        if (inPort != nullptr)
        {
            inPort->stop();
            inPort.reset();
        }

        if (!foundSysEx && hasInput && config.includeHeuristic)
        {
            auto heuristicDev = matchFromPortNames(inDevInfo, outDevInfo, registeredContracts);
            if (heuristicDev.has_value())
            {
                auto dev = heuristicDev.value();
                dev.portIndex = outIdx;
                discovered.push_back(dev);
            }
        }
    }

    // Apply whitelist filter (allowedHardwareIds)
    if (!config.allowedHardwareIds.empty())
    {
        std::vector<DiscoveredDevice> filtered;
        filtered.reserve(discovered.size());
        for (auto& dev : discovered)
        {
            if (std::find(config.allowedHardwareIds.begin(), config.allowedHardwareIds.end(), dev.hardwareId) != config.allowedHardwareIds.end())
            {
                filtered.push_back(dev);
            }
        }
        discovered = std::move(filtered);
    }

    // Filter by SysEx verification requirement
    if (config.requireSysExVerified)
    {
        std::vector<DiscoveredDevice> filtered;
        filtered.reserve(discovered.size());
        for (auto& dev : discovered)
        {
            if (dev.isSysExVerified)
                filtered.push_back(dev);
        }
        discovered = std::move(filtered);
    }

    // Enrich with contract data (modelImage, brandLogo)
    for (auto& dev : discovered)
    {
        for (const auto& c : registeredContracts)
        {
            if (c.id == dev.hardwareId)
            {
                dev.modelImage = c.midiIdentity.modelIdHex.empty() ? "" : ("models/" + dev.hardwareId + ".png");
                dev.brandLogo = c.midiIdentity.manufacturer.empty() ? "" : ("brands/" + dev.manufacturer + "-logo.svg");
                break;
            }
        }
    }

    // Apply maxResults limit
    if (config.maxResults > 0 && static_cast<int>(discovered.size()) > config.maxResults)
    {
        discovered.resize(config.maxResults);
    }

    return discovered;
}

} // namespace abd::hwid