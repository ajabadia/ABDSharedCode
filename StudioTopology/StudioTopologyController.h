/**
 * @file StudioTopologyController.h
 * @brief Universal controller managing the StudioTopologyFloatingWindow lifecycle
 *        and JSON payload building across all ABD applications (AudioLab, Bank Manager, Synths).
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <nlohmann/json.hpp>
#include <memory>
#include "StudioTopologyFloatingWindow.h"
#include "AudioMidiInterfaceDetector.h"

namespace abd::topology
{

/**
 * @struct TopologyTargetInfo
 * @brief Describes the active target hardware device, synthesizer, or virtual instrument.
 */
struct TopologyTargetInfo
{
    juce::String name { "No Target Selected" };
    juce::String category { "Hardware Device" };
    juce::String details { "Direct Loopback" };
    juce::String imageRelPath { "models/generic-digital-keyboard.png" };
    bool hasMidi { false };
    bool isVirtualPlugin { false };
};

/**
 * @struct TopologyContext
 * @brief Bundles audio device manager and target state to render the studio topology graph.
 */
struct TopologyContext
{
    juce::AudioDeviceManager& deviceManager;
    TopologyTargetInfo target;
    juce::String theme { "audiolab-light" };
    juce::Colour backgroundColour { juce::Colour(0xff0c0e12) };
};

/**
 * @class StudioTopologyController
 * @brief Shared controller managing the StudioTopologyFloatingWindow lifecycle
 *        and JSON payload generation across all ABD applications (AudioLab, Bank Manager, Synths).
 */
class StudioTopologyController
{
public:
    StudioTopologyController();
    ~StudioTopologyController();

    /**
     * @brief Toggles visibility or shows the Studio Topology Floating Window with updated state.
     */
    void toggleWindow(const TopologyContext& ctx);

    /**
     * @brief Refreshes topology state only if the window is currently visible.
     */
    void updateIfVisible(const TopologyContext& ctx);

    /**
     * @brief Updates the theme on the active window if open.
     */
    void updateTheme(const juce::String& themeName, juce::Colour bgColour);

    /**
     * @brief Closes and releases the floating window.
     */
    void closeWindow();

    /**
     * @brief Checks whether the floating window is currently instantiated and visible.
     */
    [[nodiscard]] bool isWindowVisible() const noexcept;

    /**
     * @brief Builds the JSON structure representing devices, target hero, ports, and wiring cables.
     */
    static nlohmann::json buildTopologyPayload(const TopologyContext& ctx);

private:
    std::unique_ptr<StudioTopologyFloatingWindow> topologyFloatingWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioTopologyController)
};

} // namespace abd::topology
