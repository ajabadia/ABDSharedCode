/**
 * @file StudioTopologyFloatingWindow.h
 * @brief Native floating tool window embedding the Studio Topology WebUI in ABDSharedCode.
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "JuceStudioTopologyComponent.h"
#include <functional>

namespace abd::topology
{

/**
 * @class StudioTopologyFloatingWindow
 * @brief Floating window embedding the interactive studio wiring topology WebUI.
 */
class StudioTopologyFloatingWindow : public juce::DocumentWindow
{
public:
    explicit StudioTopologyFloatingWindow(const juce::String& initialTheme = "audiolab-light",
                                          std::function<void()> onClose = nullptr)
        : DocumentWindow(juce::String::fromUTF8(u8"Estudio - Topología Interactiva de Conexiones"),
                         juce::Colour(0xff0c0e12),
                         DocumentWindow::allButtons),
          currentThemeName(initialTheme),
          onCloseCallback(std::move(onClose))
    {
        setUsingNativeTitleBar(true);

        auto comp = std::make_unique<JuceStudioTopologyComponent>(initialTheme);
        topologyCompPtr = comp.get();
        setContentOwned(comp.release(), true);

        setResizable(true, true);
        setResizeLimits(700, 480, 2560, 1440);
        centreWithSize(1000, 680);
        setAlwaysOnTop(true);
    }

    ~StudioTopologyFloatingWindow() override = default;

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onCloseCallback)
            onCloseCallback();
    }

    void updateTopology(const nlohmann::json& topologyData)
    {
        if (topologyCompPtr != nullptr)
        {
            topologyCompPtr->updateTopology(topologyData);
        }
    }

    void setTheme(const juce::String& themeName, juce::Colour bgColour = juce::Colour(0xff0c0e12))
    {
        currentThemeName = themeName;
        setBackgroundColour(bgColour);
        if (topologyCompPtr != nullptr)
        {
            topologyCompPtr->setTheme(themeName.toStdString());
        }
        repaint();
    }

private:
    juce::String currentThemeName;
    JuceStudioTopologyComponent* topologyCompPtr { nullptr };
    std::function<void()> onCloseCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioTopologyFloatingWindow)
};

} // namespace abd::topology
