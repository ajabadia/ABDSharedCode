#include "HardwareContractRegistry.h"
#include <fstream>

namespace abd::hwid
{

bool HardwareContractRegistry::loadContractsFromDirectory(const juce::File& contractsDir)
{
    if (!contractsDir.isDirectory())
    {
        lastErrorMessage = "Contracts directory does not exist: " + contractsDir.getFullPathName().toStdString();
        juce::Logger::writeToLog("[hardware-contract-registry] " + juce::String(lastErrorMessage));
        return false;
    }

    auto files = contractsDir.findChildFiles(juce::File::findFiles, false, "*.json");
    if (files.isEmpty())
    {
        lastErrorMessage = "No JSON contracts found in directory: " + contractsDir.getFullPathName().toStdString();
        juce::Logger::writeToLog("[hardware-contract-registry] " + juce::String(lastErrorMessage));
        return false;
    }

    std::vector<HardwareContract> loadedContracts;
    std::vector<std::pair<std::string, nlohmann::json>> loadedRaw;

    for (const auto& file : files)
    {
        try
        {
            std::ifstream ifs(file.getFullPathName().toStdString());
            if (!ifs.is_open()) continue;

            nlohmann::json j;
            ifs >> j;

            HardwareContract c;
            std::string defaultId = file.getFileNameWithoutExtension().toStdString();
            c.id = j.value("id", defaultId);
            c.schemaVersion = j.value("schemaVersion", std::string("2.0"));
            c.displayName = j.value("displayName", std::string("Unnamed Hardware"));
            c.description = j.value("description", std::string(""));
            c.deviceType = j.value("deviceType", j.value("category", std::string("MANUAL_EURORACK")));
            c.brand = j.value("brand", std::string(""));
            c.brandLogo = j.value("brandLogo", std::string(""));
            c.modelImage = j.value("modelImage", std::string(""));

            // Parse "midiIdentification" (or legacy "midiIdentity") identity subset.
            const auto* midiObj = j.contains("midiIdentification") && j["midiIdentification"].is_object()
                ? &j["midiIdentification"]
                : (j.contains("midiIdentity") && j["midiIdentity"].is_object() ? &j["midiIdentity"] : nullptr);

            if (midiObj != nullptr)
            {
                c.manufacturer = midiObj->value("manufacturer", std::string(""));
                c.model = midiObj->value("model", std::string(""));
                c.modelIdHex = midiObj->value("modelIdHex", std::string(""));
                c.autoDetectSysEx = midiObj->value("autoDetectSysEx", std::string(""));

                c.midiIdentity.manufacturer = c.manufacturer;
                c.midiIdentity.manufacturerIdHex = midiObj->value("manufacturerIdHex", std::string(""));
                c.midiIdentity.model = c.model;
                c.midiIdentity.modelIdHex = c.modelIdHex;
                c.midiIdentity.familyIdHex = midiObj->value("familyIdHex", std::string(""));
                c.midiIdentity.sysexHeaderHex = midiObj->value("sysexHeaderHex", std::string(""));

                if (midiObj->contains("portNameMatches") && (*midiObj)["portNameMatches"].is_array())
                {
                    for (const auto& item : (*midiObj)["portNameMatches"])
                    {
                        if (item.is_string())
                            c.midiIdentity.portNameMatches.push_back(item.get<std::string>());
                    }
                }
            }

            loadedContracts.push_back(c);
            loadedRaw.emplace_back(c.id, std::move(j));
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("Error parsing hardware contract " + file.getFileName() + ": " + juce::String(e.what()));
        }
    }

    if (!loadedContracts.empty())
    {
        contracts = std::move(loadedContracts);
        rawJsons = std::move(loadedRaw);
        lastErrorMessage.clear();
        return true;
    }

    lastErrorMessage = "Failed to parse valid contracts from directory: " + contractsDir.getFullPathName().toStdString();
    return false;
}

const HardwareContract* HardwareContractRegistry::findContractById(const std::string& id) const noexcept
{
    for (const auto& c : contracts)
        if (c.id == id)
            return &c;
    return nullptr;
}

std::optional<nlohmann::json> HardwareContractRegistry::getRawContractJson(const std::string& id) const noexcept
{
    for (const auto& [cid, raw] : rawJsons)
        if (cid == id)
            return raw;
    return std::nullopt;
}

} // namespace abd::hwid