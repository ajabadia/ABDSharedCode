#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace abd::hw
{

/**
 * @brief Descriptor for Roland AIRA submodule I/O capacities (Section 10.5, RF-25, RF-26).
 */
struct SubmoduleDescriptor
{
    uint8_t typeId { 0x00 };
    std::string name { "EMPTY" };
    int numInputs { 0 };
    int numOutputs { 0 };
    int numParameters { 0 };
};

/**
 * @brief Strict hardware routing and topology validator.
 * 
 * Enforces RF-25 (blocking illegal connections like output->output before reaching the hardware)
 * and RF-26 (validating against the normative 31-submodule catalog).
 */
class RoutingValidator
{
public:
    RoutingValidator()
    {
        initSubmoduleCatalog();
    }

    [[nodiscard]] const SubmoduleDescriptor* getDescriptor(uint8_t typeId) const noexcept
    {
        if (typeId < catalog.size())
            return &catalog[typeId];
        return nullptr;
    }

    /**
     * @brief Validates whether a patch cable connection is legally permitted by the hardware.
     * @param sourceId Virtual source ID (0x00..0x15)
     * @param destId Virtual destination ID (0x00..0x21)
     * @param slotSubmoduleTypes Array of 6 slot types currently configured
     * @param errorMessage Optional string to receive the rejection reason
     * @return true if valid, false if blocked (RF-25)
     */
    bool validateConnection(uint8_t sourceId, uint8_t destId, 
                            const std::array<uint8_t, 6>& slotSubmoduleTypes,
                            std::string* errorMessage = nullptr) const
    {
        // 1. Validate Source ID boundaries (0x00 = Input 1, up to 0x15 = Slot 6 Out 2)
        if (sourceId > 0x15)
        {
            if (errorMessage) *errorMessage = "Invalid Source ID: exceeds 0x15";
            return false;
        }

        // 2. Validate Destination ID boundaries (0x00 = Out 1, up to 0x21 = Slot 6 In 4)
        if (destId > 0x21)
        {
            if (errorMessage) *errorMessage = "Invalid Destination ID: exceeds 0x21";
            return false;
        }

        // 3. Rule: Output -> Output is strictly illegal
        // Dest 0x00 and 0x01 are physical Main Outputs (legal).
        // But connecting a Source directly to a submodule output is impossible (sources are outputs).

        // 4. Validate Slot Submodule I/O limits (RF-26)
        // Submodule sources: 0x0A..0x15 (Slots 1..6, 2 outputs each)
        if (sourceId >= 0x0A && sourceId <= 0x15)
        {
            int slotIdx = (sourceId - 0x0A) / 2;
            int outIdx = (sourceId - 0x0A) % 2;
            uint8_t typeId = slotSubmoduleTypes[slotIdx];
            const auto* desc = getDescriptor(typeId);

            if (desc == nullptr || desc->numOutputs <= outIdx)
            {
                if (errorMessage) 
                    *errorMessage = "Slot " + std::to_string(slotIdx + 1) + " submodule (" + 
                                    (desc ? desc->name : "Unknown") + ") does not have Output " + std::to_string(outIdx + 1);
                return false;
            }
        }

        // Submodule destinations: 0x0A..0x21 (Slots 1..6, 4 inputs each)
        if (destId >= 0x0A && destId <= 0x21)
        {
            int slotIdx = (destId - 0x0A) / 4;
            int inIdx = (destId - 0x0A) % 4;
            uint8_t typeId = slotSubmoduleTypes[slotIdx];
            const auto* desc = getDescriptor(typeId);

            if (desc == nullptr || desc->numInputs <= inIdx)
            {
                if (errorMessage) 
                    *errorMessage = "Slot " + std::to_string(slotIdx + 1) + " submodule (" + 
                                    (desc ? desc->name : "Unknown") + ") does not have Input " + std::to_string(inIdx + 1);
                return false;
            }
        }

        return true;
    }

private:
    void initSubmoduleCatalog()
    {
        catalog.resize(32);
        catalog[0x00] = { 0x00, "EMPTY", 0, 0, 0 };
        catalog[0x01] = { 0x01, "LPF (Low Pass Filter)", 2, 1, 3 };
        catalog[0x02] = { 0x02, "HPF (High Pass Filter)", 2, 1, 3 };
        catalog[0x03] = { 0x03, "BPF (Band Pass Filter)", 2, 1, 3 };
        catalog[0x04] = { 0x04, "NOTCH (Notch Filter)", 2, 1, 3 };
        catalog[0x05] = { 0x05, "AMP (Amplifier/VCA)", 2, 1, 2 };
        catalog[0x06] = { 0x06, "MIXER", 4, 1, 4 };
        catalog[0x07] = { 0x07, "CROSSFADER", 3, 1, 2 };
        catalog[0x08] = { 0x08, "DELAY", 2, 1, 4 };
        catalog[0x09] = { 0x09, "SHORT DELAY", 2, 1, 3 };
        catalog[0x0A] = { 0x0A, "CHORUS", 2, 2, 4 };
        catalog[0x0B] = { 0x0B, "FLANGER", 2, 2, 4 };
        catalog[0x0C] = { 0x0C, "PHASER", 2, 2, 4 };
        catalog[0x0D] = { 0x0D, "RING MODULATOR", 2, 1, 2 };
        catalog[0x0E] = { 0x0E, "DISTORTION", 2, 1, 3 };
        catalog[0x0F] = { 0x0F, "FUZZ", 2, 1, 3 };
        catalog[0x10] = { 0x10, "OVERDRIVE", 2, 1, 3 };
        catalog[0x11] = { 0x11, "BIT CRUSHER", 2, 1, 3 };
        catalog[0x12] = { 0x12, "SAMPLE & HOLD", 2, 1, 2 };
        catalog[0x13] = { 0x13, "ENV FOLLOWER", 2, 2, 3 };
        catalog[0x14] = { 0x14, "ADSR ENVELOPE", 1, 2, 4 };
        catalog[0x15] = { 0x15, "AR ENVELOPE", 1, 2, 3 };
        catalog[0x16] = { 0x16, "LFO", 1, 2, 4 };
        catalog[0x17] = { 0x17, "NOISE GENERATOR", 0, 2, 2 };
        catalog[0x18] = { 0x18, "LOGIC (AND/OR/XOR)", 2, 2, 2 };
        catalog[0x19] = { 0x19, "INVERTER / RECTIFIER", 2, 2, 2 };
        catalog[0x1A] = { 0x1A, "ATTENUATOR", 2, 1, 2 };
        catalog[0x1B] = { 0x1B, "PANNER", 2, 2, 2 };
        catalog[0x1C] = { 0x1C, "COMPRESSOR", 2, 1, 4 };
        catalog[0x1D] = { 0x1D, "OCTAVER", 2, 2, 3 };
        catalog[0x1E] = { 0x1E, "PITCH SHIFTER", 2, 1, 4 };
        catalog[0x1F] = { 0x1F, "REVERB", 2, 2, 4 };
    }

    std::vector<SubmoduleDescriptor> catalog;
};

} // namespace abd::hw
