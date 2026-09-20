// Sonda de compilacion del modulo LcdDisplay (no se instala en nadie):
// instanciar las dos clases y ejercitar la maquina valida el header contra
// JUCE real. Cualquier consumidor que quiera verificar enlaza este TU.
#include "LcdDisplay.h"
#include "LcdMenuManager.h"

namespace abd::ui::probe
{

struct Probe
{
    LcdDisplay display;
    LcdMenuManager menu;

    Probe()
    {
        menu.setMenu ({
            { "GLOBAL", { { "MASTER VOL", "masterLevel" }, { "MIDI CH", "midiChannel" } } },
            { "PANIC", "RESET_ALL", LcdMenuManager::ItemType::Action },
        });

        menu.onEdit = [] (const LcdMenuManager::LcdMenuItem& item, int dir) {
            juce::ignoreUnused (item, dir);
        };
        menu.onAction = [] (const LcdMenuManager::LcdMenuItem& item) { juce::ignoreUnused (item); };
        menu.onPreview = [] (const LcdMenuManager::LcdMenuItem& item, int dir) { juce::ignoreUnused (item, dir); };

        display.setLine (0, "NEURONiK");
        display.setLine (1, "BANK A");
        display.pushMessage ("saving", "GUARDANDO...", 1, 1500);
        display.showParameterPreview ("CUTOFF", "0.75");

        menu.onMenuPress();
        menu.onOkPress();
        menu.onEncoderRotate (1);
        display.setLine (0, menu.getLine1());
        display.setLine (1, menu.getLine2());
    }
};

[[maybe_unused]] static Probe probe;

} // namespace abd::ui::probe
