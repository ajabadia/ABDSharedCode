#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <juce_core/juce_core.h>

namespace abd::hw
{

class SysexPresetGenerator
{
public:
    static std::vector<uint8_t> createNeutralCalibrationPatch(const std::string& hardwareId)
    {
        std::vector<uint8_t> syx;

        if (hardwareId == "roland_juno106" || hardwareId == "roland_juno60")
        {
            syx = { 0xF0, 0x41, 0x32, 0x00 };
            std::vector<uint8_t> params = {
                0x40, 0x01, 0x00, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00,
                0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00, 0x00
            };
            syx.insert(syx.end(), params.begin(), params.end());
            syx.push_back(0xF7);
        }
        else if (hardwareId == "behringer_pro800" || hardwareId == "pro800")
        {
            syx = { 0xF0, 0x00, 0x20, 0x32, 0x00, 0x2C, 0x01, 0x00 };
            for (int i = 0; i < 20; ++i)
                syx.push_back(0x40);
            syx.push_back(0xF7);
        }
        else if (hardwareId == "korg_ms2000" || hardwareId == "korg_ms2000r")
        {
            syx = { 0xF0, 0x42, 0x30, 0x58, 0x40 };
            for (int i = 0; i < 32; ++i)
                syx.push_back(0x00);
            syx.push_back(0xF7);
        }
        else if (hardwareId == "casio_cz101")
        {
            syx = { 0xF0, 0x44, 0x00, 0x00, 0x70, 0x01 };
            for (int i = 0; i < 16; ++i)
                syx.push_back(0x00);
            syx.push_back(0xF7);
        }
        else
        {
            syx = { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
        }

        return syx;
    }

    static bool exportToSyxFile(const std::vector<uint8_t>& syxBytes, const juce::File& file)
    {
        if (syxBytes.empty())
            return false;

        juce::FileOutputStream out(file);
        if (!out.openedOk())
            return false;

        return out.write(syxBytes.data(), syxBytes.size());
    }
};

} // namespace abd::hw
