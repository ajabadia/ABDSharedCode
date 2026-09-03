/**
 * @file HardwareContractRegistry.h
 * @brief Loads hardware identity contracts from a JSON directory.
 * @details Parses the "midiIdentification" subset of each contract JSON. Keeps
 *          the raw per-file JSON available so consumers can extract
 *          product-specific sections (e.g. "functions", "controls") without
 *          coupling the shared module to any domain.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>
#include <juce_core/juce_core.h>
#include "HardwareContract.h"

namespace abd::hwid
{

/**
 * @class HardwareContractRegistry
 * @brief Loads and exposes HardwareContract identity entries.
 */
class HardwareContractRegistry
{
public:
    HardwareContractRegistry() = default;
    ~HardwareContractRegistry() = default;

    /**
     * @brief Load all *.json contracts from a directory.
     * @return True if at least one valid contract was loaded.
     */
    bool loadContractsFromDirectory(const juce::File& contractsDir);

    /** @brief True once at least one contract parsed successfully. */
    [[nodiscard]] bool hasContracts() const noexcept { return !contracts.empty(); }

    /** @brief All loaded identity contracts. */
    [[nodiscard]] const std::vector<HardwareContract>& getContracts() const noexcept { return contracts; }

    /** @brief Find an identity contract by id, or nullptr. */
    [[nodiscard]] const HardwareContract* findContractById(const std::string& id) const noexcept;

    /**
     * @brief Raw parsed JSON for a loaded contract id.
     * @return The nlohmann::json object, or std::nullopt if id not loaded.
     */
    [[nodiscard]] std::optional<nlohmann::json> getRawContractJson(const std::string& id) const noexcept;

    /** @brief Last error message (empty on success). */
    [[nodiscard]] const std::string& getLastError() const noexcept { return lastErrorMessage; }

private:
    std::vector<HardwareContract> contracts;
    std::vector<std::pair<std::string, nlohmann::json>> rawJsons;
    std::string lastErrorMessage;
};

} // namespace abd::hwid