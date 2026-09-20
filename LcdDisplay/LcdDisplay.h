#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <functional>

namespace abd::ui
{

/**
 * LcdDisplay — pantalla de caracteres universal de la suite (C++).
 * ================================================================
 *
 * Port del LcdDisplay de NEURONiK (Source/UI/LcdDisplay.h, retirado en c811b75,
 * recuperado de git) generalizado con las lecciones de las otras estirpes:
 *   - NEURONiK (nativo): autoscroll por línea, showParameterPreview con
 *     expiración, texto de reposo por línea.
 *   - ABDEep (script_lcd_core.js): los avisos transitorios son una COLA con
 *     prioridad y expiración.
 *   - CZ101 (lcdScroller.js): velocidad/pausa del scroll configurables, y el
 *     corte es por presupuesto de caracteres.
 *
 * La pantalla es un Component puro que NADA sabe del synth: las líneas de
 * reposo, el preview y los mensajes de la cola se fijan desde fuera; los
 * valores y la maquinaria de menú NO viven aquí (para el menú, LcdMenuManager).
 * Tematizable via setColours (LookAndFeel opcional del consumidor).
 *
 * Universal por diseño: no hay tipografías hardcodeadas del synth, no hay
 * colores fijos (defaults razonables sobreescribibles), y el carácter máximo
 * por línea es una propiedad del display (setWidthChars), no una constante.
 */
class LcdDisplay : public juce::Component,
                   private juce::Timer
{
public:
    LcdDisplay()
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
    }

    ~LcdDisplay() override { stopTimer(); }

    // --- configuración ---------------------------------------------------

    /** Presupuesto de caracteres por línea (16 clásico; 20, 40... también). */
    void setWidthChars (int chars)                  { widthChars = juce::jmax (1, chars); repaint(); }

    /** Velocidad del autoscroll: un carácter por `stepMs` (default ~180ms). */
    void setScrollStepMs (int stepMs)               { scrollStepTicks = juce::jmax (1, stepMs / tickMs); }

    /** Pausa antes de que empiece a desplazar un texto largo (default 2 s). */
    void setScrollPauseMs (int pauseMs)             { scrollPauseTicks = juce::jmax (0, pauseMs / tickMs); }

    /** Duración del preview/parámetro fugaz (default ~2.2 s, 15 ticks nativos). */
    void setPreviewDurationMs (int ms)              { previewDurationMs = juce::jmax (200, ms); }

    /** Colores (tema del synth). Defaults: LCD verde clásico autoiluminado. */
    void setColours (juce::Colour bg, juce::Colour text, juce::Colour border)
    {
        lcdBackground = bg; lcdText = text; lcdBorder = border;
        repaint();
    }

    // --- contenido --------------------------------------------------------

    /** Texto de reposo de una línea (0 o 1). El synth lo repinta al cambiar. */
    void setLine (int lineIdx, const juce::String& text)
    {
        if (! juce::isPositiveAndBelow (lineIdx, 2)) return;
        defaultLines[(size_t) lineIdx] = text;
        clearPreview (lineIdx);
        resetScroll ((size_t) lineIdx);
        repaint();
    }

    /**
     * Preview transitorio (herencia LcdDisplay.h): nombre+valor aparecen y la
     * línea vuelve a su reposo tras el timeout.
     */
    void showParameterPreview (const juce::String& paramName, const juce::String& value)
    {
        showLinePreview (0, paramName, previewDurationMs);
        showLinePreview (1, value, previewDurationMs);
    }

    /** Preview de una sola línea con duración explícita. */
    void showLinePreview (int lineIdx, const juce::String& text, int durationMs)
    {
        if (! juce::isPositiveAndBelow (lineIdx, 2)) return;
        auto& p = previews[(size_t) lineIdx];
        p.text = text;
        p.ticksLeft = juce::jmax (1, durationMs / tickMs);
        if (! isTimerRunning()) startTimerHz (timerHz);
        repaint();
    }

    /**
     * Mensaje transitorio con cola (herencia ABDEep): prioridad menor gana,
     * expira solo; a igualdad de prioridad, el más reciente. id estable para
     * reemplazar en caliente ("saving", "midi-learn"...).
     */
    void pushMessage (const juce::String& id, const juce::String& text,
                      int priority = 5, int durationMs = 2000)
    {
        auto& slot = queue[id];
        slot.text = text;
        slot.priority = priority;
        slot.expiresIn = juce::jmax (1, durationMs / tickMs);
        if (! isTimerRunning()) startTimerHz (timerHz);
        refreshQueue();
        repaint();
    }

    /** Quitar un mensaje concreto (cancelaciones). */
    void clearMessage (const juce::String& id)
    {
        queue.erase (id);
        refreshQueue();
        repaint();
    }

    /** Limpiar previews y cola; el reposo se queda. */
    void resetTransients()
    {
        for (auto& p : previews) { p.text.clear(); p.ticksLeft = 0; }
        queue.clear();
        refreshQueue();
        repaint();
    }

    /** Texto visible ahora mismo en una línea (para tests/QA). */
    juce::String getVisibleLine (int lineIdx) const
    {
        if (! juce::isPositiveAndBelow (lineIdx, 2)) return {};
        if (previews[(size_t) lineIdx].ticksLeft > 0)
            return previews[(size_t) lineIdx].text;
        if (lineIdx == 0 && activeMessage.text.isNotEmpty())
            return activeMessage.text;
        return defaultLines[(size_t) lineIdx];
    }

    // --- juce::Component ---------------------------------------------------

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (2);
        g.setColour (lcdBackground);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (lcdBorder);
        g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);

        g.setColour (lcdText);
        auto area = r.toFloat();
        const float lineH = area.getHeight() / (float) linesShown;
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                      lineH * 0.62f, juce::Font::plain));
        for (int i = 0; i < linesShown; ++i)
        {
            auto lineBox = area.removeFromTop (lineH);
            auto text = getVisibleLine (i);
            const auto& s = scrolls[(size_t) i];
            if (text.length() > widthChars && s.offset > 0)
                text = text.substring (s.offset);
            g.drawText (text, lineBox.toNearestInt(), juce::Justification::centredLeft);
        }
    }

private:
    static constexpr int linesShown = 2;
    static constexpr int tickMs = 100;    // grano del timer (decima el trabajo)
    static constexpr int timerHz = 10;

    struct Preview { juce::String text; int ticksLeft = 0; };
    struct Scroll  { int offset = 0; int direction = 1; int holdTicks = 0; int stepCountdown = 0; };
    struct Message { juce::String text; int priority = 5; int expiresIn = 0; };

    int widthChars     = 16;
    int scrollStepTicks = 2;   // 180ms ~ 2 ticks de 100ms
    int scrollPauseTicks = 20; // 2s ~ 20 ticks
    int previewDurationMs = 2200;

    juce::Colour lcdBackground { juce::Colour (0xff102216) };
    juce::Colour lcdText       { juce::Colour (0xff44ff77) };
    juce::Colour lcdBorder     { juce::Colour (0x3044ff77) };

    std::array<juce::String, 2> defaultLines {};
    std::array<Preview, 2> previews {};
    std::array<Scroll, 2> scrolls {};

    std::map<juce::String, Message> queue; // herencia ABDEep (prioridad+expira)
    Message activeMessage;

    void clearPreview (int lineIdx)
    {
        if (juce::isPositiveAndBelow (lineIdx, 2)) previews[(size_t) lineIdx] = {};
    }

    void resetScroll (size_t lineIdx) { scrolls[lineIdx] = {}; }

    void refreshQueue()
    {
        const Message* best = nullptr;
        for (auto& [id, m] : queue)
        {
            juce::ignoreUnused (id);
            if (best == nullptr
                || m.priority < best->priority
                || (m.priority == best->priority && m.expiresIn > best->expiresIn))
                best = &m;
        }
        activeMessage = best != nullptr ? *best : Message {};
    }

    void timerCallback() override
    {
        bool dirty = false;

        for (auto& p : previews)
            if (p.ticksLeft > 0) { p.ticksLeft -= 1; dirty = true; }

        for (auto it = queue.begin(); it != queue.end();)
        {
            it->second.expiresIn -= 1;
            if (it->second.expiresIn <= 0) { it = queue.erase (it); dirty = true; }
            else ++it;
        }
        refreshQueue();

        for (size_t i = 0; i < scrolls.size(); ++i)
        {
            auto& s = scrolls[i];
            const auto& text = getVisibleLine ((int) i);
            if (text.length() <= widthChars) { if (s.offset != 0) { s = {}; dirty = true; } continue; }

            if (s.holdTicks < scrollPauseTicks) { s.holdTicks += 1; continue; }
            if (s.stepCountdown > 0) { s.stepCountdown -= 1; continue; }
            s.stepCountdown = scrollStepTicks - 1; // este tick ya es un paso
            s.offset += s.direction;
            const int maxOffset = (int) text.length() - widthChars;
            if (s.offset <= 0)                  { s.offset = 0; s.direction = 1; }
            if (s.offset >= maxOffset)          { s.offset = maxOffset; s.direction = -1; }
            dirty = true;
        }

        if (dirty) repaint();

        // Parar SOLO si no queda nada animado: ni previews, ni cola, ni un
        // scroll en marcha (antes se congelaba el scroll al expirar un aviso).
        bool anyScroll = false;
        for (int i = 0; i < linesShown; ++i)
            if (getVisibleLine (i).length() > widthChars) { anyScroll = true; break; }
        if (previews[0].ticksLeft == 0 && previews[1].ticksLeft == 0
            && queue.empty() && ! anyScroll)
            stopTimer();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LcdDisplay)
};

} // namespace abd::ui
