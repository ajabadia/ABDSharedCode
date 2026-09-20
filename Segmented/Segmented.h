#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace abd::ui
{

/**
 * Segmented — selector de opciones en linea, universal de la suite (C++).
 * ================================================================
 *
 * Contraparte nativa del `Segmented` de @abdsynths/shared (components/
 * segmented.js): la lista corta que se muestra entera — cada opcion es su
 * propio boton en una fila plana — frente al Combo (lista desplegable) para
 * las listas largas. Mismo reparto de trabajo que en la web: 2-4 opciones
 * visibles (motor, forma de onda) en segmentos; 9+ (divisiones ritmicas) en
 * combo desplegable.
 *
 * El valor es el INDICE de la opcion (como en la web y como los choices del
 * APVTS): setValue/setActive no notifican (estado que llega de fuera),
 * onClick del usuario si. Las opciones deshabilitadas existen (gating por
 * motor, como optionEngines en la web): un segmento vetado no responde y el
 * valor que cae en el se CONSERVA (estado del host, se pinta divergente).
 *
 * Comportamiento radio: botones con togglestate en un radio group — pulsar
 * uno apaga el resto; el activo queda "hundido" (accent).
 *
 * Universal por diseño: sin colores fijos (setColours), sin tipografias del
 * synth, y las opciones son un StringArray que pone el consumidor. Nada
 * aqui toca parametros ni procesadores.
 */
class Segmented : public juce::Component
{
public:
    Segmented()
    {
        setInterceptsMouseClicks (false, true);
    }

    ~Segmented() override = default;

    /** Opciones de la fila. Reconstruye los botones (la cantidad puede cambiar). */
    void setSegments (const juce::StringArray& segmentLabels)
    {
        labels = segmentLabels;
        buttons.clear();

        for (const auto& label : labels)
        {
            auto button = std::make_unique<juce::TextButton> (label);
            button->setClickingTogglesState (true);
            button->setRadioGroupId (radioGroup);
            button->setColour (juce::TextButton::buttonColourId, trackColour);
            button->setColour (juce::TextButton::buttonOnColourId, activeColour);
            button->setColour (juce::TextButton::textColourOffId, textColour);
            button->setColour (juce::TextButton::textColourOnId, activeTextColour);
            addAndMakeVisible (*button);
            buttons.push_back (std::move (button));
        }

        rebuildAccessibility();
        refreshAll();
        resized();
    }

    /** Indices vetados (gating): el segmento no responde y se atenúa. */
    void setDisabledIndices (std::vector<int> indices)
    {
        disabled = std::move (indices);
        refreshAll();
    }

    /** Nota explicativa del veto (tooltip), como `note` en la web. */
    void setDisabledNote (const juce::String& note) { disabledNote = note; }

    /** Valor por indice. No notifica (estado que llega del synth). */
    void setActive (int index)
    {
        active = index;
        refreshAll();
    }

    int getActive() const { return active; }

    /** Callback de EDICION de usuario (indice elegido). */
    std::function<void (int)> onChange;

    /** Paleta tematica; el LookAndFeel del synth puede sobreescribir luego. */
    void setColours (juce::Colour track, juce::Colour text, juce::Colour activeFill, juce::Colour activeText)
    {
        trackColour = track;
        textColour = text;
        activeColour = activeFill;
        activeTextColour = activeText;

        for (auto& button : buttons)
        {
            button->setColour (juce::TextButton::buttonColourId, trackColour);
            button->setColour (juce::TextButton::buttonOnColourId, activeColour);
            button->setColour (juce::TextButton::textColourOffId, textColour);
            button->setColour (juce::TextButton::textColourOnId, activeTextColour);
        }
    }

    void paint (juce::Graphics&) override {}

    void resized() override
    {
        auto bounds = getLocalBounds();
        const int count = (int) buttons.size();

        if (count == 0)
            return;

        const int share = bounds.getWidth() / count;
        int x = bounds.getX();

        for (auto& button : buttons)
        {
            button->setBounds (x, bounds.getY(), share, bounds.getHeight());
            x += share;
        }
    }

private:
    void rebuildAccessibility()
    {
        for (auto& button : buttons)
            button->onClick = [this, index = (int) (button.get() - buttons[0].get())]
            {
                // Un segmento vetado no emite (coherente con la web).
                if (isDisabled (index) || index == active)
                {
                    refreshAll();
                    return;
                }

                active = index;
                refreshAll();

                if (onChange)
                    onChange (active);
            };
    }

    bool isDisabled (int index) const
    {
        return std::find (disabled.begin(), disabled.end(), index) != disabled.end();
    }

    /** Empuja estado + paleta a los botones (una sola verdad: `active`). */
    void refreshAll()
    {
        for (auto& button : buttons)
        {
            const int index = (int) (button.get() - buttons[0].get());

            button->setEnabled (! isDisabled (index));
            button->setTooltip (isDisabled (index) ? disabledNote : juce::String());
            // Programatico: sin notificacion (el radio group apaga los demas).
            button->setToggleState (index == active, juce::dontSendNotification);
        }
    }

    juce::StringArray labels;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::vector<int> disabled;
    juce::String disabledNote;
    int active = 0;
    int radioGroup = 0;   // 0 = grupo propio por instancia (JUCE lo asigna unico)

    juce::Colour trackColour       { juce::Colour (0xff141d2b) };
    juce::Colour textColour        { juce::Colour (0xff7e9bb5) };
    juce::Colour activeColour      { juce::Colour (0xff00c3ff) };
    juce::Colour activeTextColour  { juce::Colour (0xff0a0e14) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
};

} // namespace abd::ui
