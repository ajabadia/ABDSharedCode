#include "CasioCzVirtualController.h"

namespace abd::hw
{

CasioCzVirtualController::CasioCzVirtualController()
{
    initializeCanonicalCzOpcodes();
}

CasioCzVirtualController::~CasioCzVirtualController()
{
    disconnect();
}

void CasioCzVirtualController::setTargetDeviceIdentifier(const juce::String& portSubstring)
{
    targetPortName = portSubstring;
}

void CasioCzVirtualController::setMidiChannel(int channel) noexcept
{
    midiChannel = juce::jlimit(1, 16, channel);
}

void CasioCzVirtualController::registerCustomOpcode(int paramIndex, uint8_t opcodeMSB, uint8_t opcodeLSB, int maxRaw)
{
    opcodeRegistry[paramIndex] = { opcodeMSB, opcodeLSB, maxRaw };
}

bool CasioCzVirtualController::loadParameterMappingJson(const juce::String& jsonString)
{
    try
    {
        auto j = nlohmann::json::parse(jsonString.toStdString());
        if (j.contains("opcodes") && j["opcodes"].is_array())
        {
            for (const auto& op : j["opcodes"])
            {
                if (!op.is_object()) continue;
                int idx = op.value("index", -1);
                if (idx < 0) continue;

                uint8_t msb = static_cast<uint8_t>(op.value("msb", 0));
                uint8_t lsb = static_cast<uint8_t>(op.value("lsb", 0));
                int maxRaw = op.value("maxRaw", 99);
                registerCustomOpcode(idx, msb, lsb, maxRaw);
            }
            return true;
        }
    }
    catch (...)
    {
        return false;
    }
    return false;
}

bool CasioCzVirtualController::connect()
{
    disconnect();

    auto devices = juce::MidiOutput::getAvailableDevices();
    int targetIdx = -1;

    for (int i = 0; i < devices.size(); ++i)
    {
        if (devices[i].name.containsIgnoreCase(targetPortName) || devices[i].name.containsIgnoreCase("loopMIDI"))
        {
            targetIdx = i;
            break;
        }
    }

    if (targetIdx == -1 && !devices.isEmpty())
        targetIdx = 0;

    if (targetIdx != -1)
    {
        midiDevice = juce::MidiOutput::openDevice(devices[targetIdx].identifier);
        isCurrentlyConnected = (midiDevice != nullptr);
        return isCurrentlyConnected;
    }

    return false;
}

void CasioCzVirtualController::disconnect()
{
    midiDevice.reset();
    isCurrentlyConnected = false;
}

bool CasioCzVirtualController::isConnected() const noexcept
{
    return isCurrentlyConnected;
}

bool CasioCzVirtualController::setParameter(int paramIndex, float normalizedValue)
{
    auto it = opcodeRegistry.find(paramIndex);
    if (it == opcodeRegistry.end()) return false;

    int rawVal = static_cast<int>(std::round(normalizedValue * static_cast<float>(it->second.maxRawValue)));
    return setParameterRaw(paramIndex, rawVal);
}

bool CasioCzVirtualController::setParameterRaw(int paramIndex, int rawValue)
{
    auto it = opcodeRegistry.find(paramIndex);
    if (it == opcodeRegistry.end()) return false;

    int boundedVal = juce::jlimit(0, it->second.maxRawValue, rawValue);
    return sendCzParameter(it->second.msb, it->second.lsb, boundedVal);
}

bool CasioCzVirtualController::sendMidiMessage(const juce::MidiMessage& msg)
{
    sentMessages.push_back(msg);

    if (midiDevice != nullptr)
    {
        midiDevice->sendMessageNow(msg);
        if (settlingDelayMs > 0)
        {
            juce::Thread::sleep(settlingDelayMs);
        }
        return true;
    }
    return true;
}

bool CasioCzVirtualController::sendCzParameter(uint8_t opcodeMSB, uint8_t opcodeLSB, int value)
{
    uint8_t msn = static_cast<uint8_t>((value >> 4) & 0x0F);
    uint8_t lsn = static_cast<uint8_t>(value & 0x0F);

    uint8_t sysexPayload[7] = {
        0x44, // Casio Manufacturer ID
        0x00, // Sub-status
        static_cast<uint8_t>(midiChannel - 1),
        opcodeMSB,
        opcodeLSB,
        msn,
        lsn
    };

    auto msg = juce::MidiMessage::createSysExMessage(sysexPayload, sizeof(sysexPayload));
    return sendMidiMessage(msg);
}

bool CasioCzVirtualController::sendCzEnvelopeStep(int envType, int line, int stepIndex, int rate, int level)
{
    uint8_t baseMsb = 0x10;
    if (envType == 1) baseMsb = 0x20;
    if (envType == 2) baseMsb = 0x30;

    if (line == 2) baseMsb += 0x40;

    uint8_t rateLsb  = static_cast<uint8_t>((stepIndex - 1) * 2);
    uint8_t levelLsb = static_cast<uint8_t>(rateLsb + 1);

    bool rOk = sendCzParameter(baseMsb, rateLsb, rate);
    bool lOk = sendCzParameter(baseMsb, levelLsb, level);
    return (rOk && lOk);
}

void CasioCzVirtualController::initializeCanonicalCzOpcodes()
{
    registerCustomOpcode(1,  0x00, 0x01, 99);  // DCO1 Waveform Choice
    registerCustomOpcode(2,  0x00, 0x02, 3);   // DCO1 Octave Select
    registerCustomOpcode(10, 0x00, 0x0A, 99);  // DCW1 Key Follow
    registerCustomOpcode(20, 0x00, 0x14, 99);  // DCA1 Key Follow
    registerCustomOpcode(50, 0x00, 0x3C, 3);   // Line Select
    registerCustomOpcode(51, 0x00, 0x3D, 99);  // Detune Amount

    for (int step = 1; step <= 8; ++step)
    {
        uint8_t rateLsb  = static_cast<uint8_t>((step - 1) * 2);
        uint8_t levelLsb = static_cast<uint8_t>(rateLsb + 1);
        registerCustomOpcode(100 + step, 0x20, rateLsb, 99);
        registerCustomOpcode(110 + step, 0x20, levelLsb, 99);
    }

    for (int step = 1; step <= 8; ++step)
    {
        uint8_t rateLsb  = static_cast<uint8_t>((step - 1) * 2);
        uint8_t levelLsb = static_cast<uint8_t>(rateLsb + 1);
        registerCustomOpcode(200 + step, 0x30, rateLsb, 99);
        registerCustomOpcode(210 + step, 0x30, levelLsb, 99);
    }
}

} // namespace abd::hw
