/**
 * @file StudioTopologyController.cpp
 * @brief Implementation of universal StudioTopologyController in ABDSharedCode.
 * @author ABDSynths
 * @date 2026
 */

#include "StudioTopologyController.h"

namespace abd::topology
{

StudioTopologyController::StudioTopologyController() = default;

StudioTopologyController::~StudioTopologyController()
{
    closeWindow();
}

void StudioTopologyController::closeWindow()
{
    if (topologyFloatingWindow != nullptr)
    {
        topologyFloatingWindow->setVisible(false);
        topologyFloatingWindow = nullptr;
    }
}

bool StudioTopologyController::isWindowVisible() const noexcept
{
    return topologyFloatingWindow != nullptr && topologyFloatingWindow->isVisible();
}

void StudioTopologyController::updateTheme(const juce::String& themeName, juce::Colour bgColour)
{
    if (topologyFloatingWindow != nullptr)
    {
        topologyFloatingWindow->setTheme(themeName, bgColour);
    }
}

void StudioTopologyController::updateIfVisible(const TopologyContext& ctx)
{
    if (isWindowVisible())
    {
        auto root = buildTopologyPayload(ctx);
        topologyFloatingWindow->setTheme(ctx.theme, ctx.backgroundColour);
        topologyFloatingWindow->updateTopology(root);
    }
}

void StudioTopologyController::toggleWindow(const TopologyContext& ctx)
{
    if (topologyFloatingWindow == nullptr)
    {
        topologyFloatingWindow = std::make_unique<StudioTopologyFloatingWindow>(ctx.theme);
    }

    auto root = buildTopologyPayload(ctx);
    topologyFloatingWindow->setTheme(ctx.theme, ctx.backgroundColour);
    topologyFloatingWindow->updateTopology(root);

    if (topologyFloatingWindow->isVisible())
    {
        topologyFloatingWindow->toFront(true);
    }
    else
    {
        topologyFloatingWindow->setVisible(true);
        topologyFloatingWindow->toFront(true);
    }
}

nlohmann::json StudioTopologyController::buildTopologyPayload(const TopologyContext& ctx)
{
    nlohmann::json root;

    // 1. Center Target Hero
    root["target"] = {
        { "name", ctx.target.name.toStdString() },
        { "category", ctx.target.category.toStdString() },
        { "details", ctx.target.details.toStdString() },
        { "image", ctx.target.imageRelPath.toStdString() },
        { "hasMidi", ctx.target.hasMidi },
        { "isVirtual", ctx.target.isVirtualPlugin }
    };

    // 2. Detected Devices & Interfaces
    root["devices"] = nlohmann::json::array();
    root["connections"] = nlohmann::json::array();

    auto detectedInterfaces = AudioMidiInterfaceDetector::detectInterfaces(ctx.deviceManager);
    for (size_t i = 0; i < detectedInterfaces.size(); ++i)
    {
        const auto& iface = detectedInterfaces[i];
        std::string devId = "dev_" + std::to_string(i);

        std::string ifaceDetails = "Detected in OS";
        if (iface.isAudioConnected && iface.isMidiInConnected && iface.isMidiOutConnected)
            ifaceDetails = "Audio & MIDI I/O Assigned";
        else if (iface.isAudioConnected)
            ifaceDetails = "Audio Assigned";
        else if (iface.isMidiInConnected || iface.isMidiOutConnected)
            ifaceDetails = "MIDI Assigned";

        nlohmann::json portsJson = nlohmann::json::array();
        for (const auto& p : iface.ports)
        {
            portsJson.push_back({
                { "id", p.portId.toStdString() },
                { "name", p.name.toStdString() },
                { "type", p.type.toStdString() },
                { "connected", p.isConnected }
            });
        }

        bool connectsToTarget = false;
        if (!ctx.target.isVirtualPlugin && iface.isAudioConnected)
            connectsToTarget = true;
        if (ctx.target.hasMidi && (iface.isMidiInConnected || iface.isMidiOutConnected))
            connectsToTarget = true;

        root["devices"].push_back({
            { "id", devId },
            { "name", iface.displayName.toStdString() },
            { "details", ifaceDetails },
            { "image", iface.imageRelPath.toStdString() },
            { "assigned", connectsToTarget },
            { "hasAudio", iface.hasAudioHardware },
            { "hasMidi", iface.hasMidiHardware },
            { "ports", portsJson }
        });

        // 3. Routing Cables
        for (const auto& p : iface.ports)
        {
            if (!p.isConnected) continue;

            // Physical Audio Cables: Only when target is physical hardware
            if (!ctx.target.isVirtualPlugin)
            {
                if (p.type == "audioOut")
                {
                    root["connections"].push_back({ { "type", "audioOut" }, { "from", devId }, { "to", "target" }, { "fromPort", p.portId.toStdString() } });
                }
                else if (p.type == "audioIn")
                {
                    root["connections"].push_back({ { "type", "audioIn" }, { "from", "target" }, { "to", devId }, { "toPort", p.portId.toStdString() } });
                }
            }

            // MIDI Cables: For any MIDI-capable target (physical synth or virtual VST instrument)
            if (ctx.target.hasMidi)
            {
                if (p.type == "midiOut")
                {
                    root["connections"].push_back({ { "type", "midiOut" }, { "from", devId }, { "to", "target" }, { "fromPort", p.portId.toStdString() } });
                }
                else if (p.type == "midiIn")
                {
                    root["connections"].push_back({ { "type", "midiIn" }, { "from", "target" }, { "to", devId }, { "toPort", p.portId.toStdString() } });
                }
            }
        }
    }

    return root;
}

} // namespace abd::topology
