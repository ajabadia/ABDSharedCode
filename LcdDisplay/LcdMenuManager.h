#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace abd::ui
{

/**
 * LcdMenuManager — máquina de estados del menú del LCD, universal (C++).
 * ======================================================================
 *
 * Generalización del LcdMenuManager de NEURONiK (Source/UI, retirado en
 * c811b75, recuperado de git). Lo que ERA particular y ahora es universal:
 *   - El árbol de menú se INYECTA (setupMenu hardcodeaba GLOBAL/RESONATOR/
 *     FILTER/EFFECTS/MIDI CONTROL según engineType): cada synth pasa sus
 *     LcdMenuItem. Profundidad libre (el nativo se quedaba en 2 niveles).
 *   - Los efectos son CALLBACKS (el editor nativo aplicaba parámetros a mano):
 *     onEdit/onAction/onPreview std::function, opcionalmente por item.
 *   - ItemType::Parameter / MidiCC / Action se conservan (el tipo cc marca el
 *     flujo de aprendizaje MIDI CC del 8.3).
 *
 * La máquina no toca parámetros ni pinta: entrega decisiones. El render de
 * las dos líneas replica al nativo (línea 1 = dónde estoy, línea 2 = ítem con
 * cursor '>'), y en Idle devuelve vacío para que el synth pinte su reposo.
 */
class LcdMenuManager
{
public:
    LcdMenuManager() = default;

    enum class State { Idle, Navigation, Edit };

    enum class ItemType { Parameter, MidiCC, Action };

    struct LcdMenuItem
    {
        juce::String label;
        juce::String paramId;
        ItemType type = ItemType::Parameter;
        std::vector<LcdMenuItem> sub;

        LcdMenuItem() = default;
        LcdMenuItem (const char* lbl, const char* pid, ItemType t = ItemType::Parameter)
            : label (lbl), paramId (pid), type (t) {}
        LcdMenuItem (juce::String lbl, std::vector<LcdMenuItem> children)
            : label (std::move (lbl)), sub (std::move (children)) {}
    };

    // --- hooks -------------------------------------------------------------

    /** En EDIT, Encoder/cursores mueven el valor: dir puede ser cualquier entero. */
    std::function<void (const LcdMenuItem&, int)> onEdit;

    /** Un ítem Action se confirma con OK. */
    std::function<void (const LcdMenuItem&)> onAction;

    /** En IDLE, Encoder gira: preview rápido de parámetros (opcional). */
    std::function<void (const LcdMenuItem&, int)> onPreview;

    // --- árbol -------------------------------------------------------------
    // Nota de propiedad: la pila de navegación guarda PUNTEROS a los vectores
    // del árbol, así que setMenu() resetea la navegación (y el synth debe
    // dejar el árbol estable mientras se navega; el equivalente JS no tiene
    // esta restricción porque copia).

    /** Reemplazar el árbol del menú (datos del synth; puede cambiar en vivo). */
    void setMenu (std::vector<LcdMenuItem> items)
    {
        rootItems = std::move (items);
        exitToIdle();
    }

    const std::vector<LcdMenuItem>& getMenu() const { return rootItems; }

    // --- interacción (misma superficie que el nativo) -----------------------

    void onMenuPress()
    {
        if (state == State::Edit)
        {
            state = State::Navigation; // cancelar edición
            editing = nullptr;
        }
        else if (state == State::Navigation)
        {
            if (path.size() > 1) path.pop_back(); // subir un nivel
            else exitToIdle();
        }
        else
        {
            state = State::Navigation;
            path = { { &rootItems, 0 } };
        }
    }

    void onOkPress()
    {
        if (state != State::Navigation) { if (state == State::Edit) { editing = nullptr; state = State::Navigation; } return; }
        auto* item = currentItem();
        if (item == nullptr) return;

        if (! item->sub.empty())
        {
            path.push_back ({ &item->sub, 0 });
        }
        else if (item->type == ItemType::Action)
        {
            if (onAction) onAction (*item);
        }
        else
        {
            editing = item;
            state = State::Edit;
        }
    }

    void onEncoderRotate (int delta)
    {
        if (state == State::Edit)
        {
            if (editing != nullptr && onEdit) onEdit (*editing, delta);
        }
        else if (state == State::Navigation)
        {
            auto& level = path.back();
            const int size = (int) level.items->size();
            if (size > 0) level.index = ((level.index + delta) % size + size) % size;
        }
        else if (! rootItems.empty() && onPreview)
        {
            previewIndex = previewIndex < 0
                ? (delta > 0 ? 0 : (int) rootItems.size() - 1)
                : ((previewIndex + delta) % (int) rootItems.size() + (int) rootItems.size()) % (int) rootItems.size();
            onPreview (rootItems[(size_t) previewIndex], delta);
        }
    }

    /** Cursores: ‹ › = ±1, ^ v = ±5 (ajuste grueso), como el D-pad del 8.3. */
    void onArrow (int dir)
    {
        onEncoderRotate (dir);
    }

    // --- getters (los que tenía el nativo, más el fotograma) ----------------

    State getState() const { return state; }
    bool isEditing() const { return state == State::Edit; }
    bool isInSubMenu() const { return path.size() > 1; }

    juce::String getLine1() const
    {
        if (state == State::Idle) return {};
        if (path.size() <= 1) return "MAIN MENU";
        const auto& parentLevel = path[path.size() - 2];
        return parentLevel.items->at ((size_t) parentLevel.index).label;
    }

    juce::String getLine2() const
    {
        if (state != State::Navigation) return {};
        auto* item = currentItem();
        return item != nullptr ? ">" + item->label : juce::String (">");
    }

    const LcdMenuItem* getCurrentItem() const
    {
        return state == State::Navigation ? currentItem() : nullptr;
    }

    const LcdMenuItem* getEditingItem() const { return editing; }

    struct Snapshot
    {
        State state = State::Idle;
        int depth = 0;
        juce::StringArray breadcrumb;
    };

    Snapshot getSnapshot() const
    {
        Snapshot s;
        s.state = state;
        s.depth = (int) path.size();
        for (const auto& level : path)
            s.breadcrumb.add (level.items->at ((size_t) level.index).label);
        return s;
    }

private:
    struct Level
    {
        const std::vector<LcdMenuItem>* items = nullptr;
        int index = 0;
    };

    std::vector<LcdMenuItem> rootItems;
    std::vector<Level> path; // punteros a los vectores del árbol del synth
    const LcdMenuItem* editing = nullptr;
    int previewIndex = -1;
    State state = State::Idle;

    const LcdMenuItem* currentItem() const
    {
        if (path.empty()) return nullptr;
        const auto& level = path.back();
        if (level.items->empty()) return nullptr;
        return &level.items->at ((size_t) level.index);
    }

    void exitToIdle()
    {
        state = State::Idle;
        path.clear();
        editing = nullptr;
        previewIndex = -1;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdMenuManager)
};

} // namespace abd::ui
