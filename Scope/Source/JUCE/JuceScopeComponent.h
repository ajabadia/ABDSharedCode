#pragma once

#if defined(JUCE_VERSION) || __has_include(<juce_gui_basics/juce_gui_basics.h>)
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../Core/ScopeTap.h"
#include "../Core/TriggerDetector.h"

namespace abd::scope {

/**
 * Visual mode selector for native JUCE rendering.
 */
enum class NativeScopeMode {
    Oscilloscope,
    Spectrum,
    Lissajous,
    PhaseMeter
};

/**
 * Native JUCE GUI Component for pure C++ hosts (e.g. ABDAudioLab).
 * Zero WebView2 dependencies — renders directly via juce::Graphics with sub-sample
 * phase-locking, adaptive octave scaling, and stereo M/S vectorscope.
 */
class JuceScopeComponent : public juce::Component,
                           private juce::Timer
{
public:
    explicit JuceScopeComponent(ScopeTap* tapToMonitor = nullptr, float sampleRate = 44100.0f)
        : m_tap(tapToMonitor),
          m_sampleRate(sampleRate),
          m_samplesL(1024, 0.0f),
          m_samplesR(1024, 0.0f),
          m_fft(10), // 1024-point FFT
          m_fftData(2048, 0.0f),
          m_spectrumDb(512, -96.0f)
    {
        setOpaque(true);
        startTimerHz(30); // 30 FPS visual refresh
    }

    ~JuceScopeComponent() override {
        stopTimer();
    }

    void setTap(ScopeTap* newTap) noexcept {
        m_tap = newTap;
    }

    void setSampleRate(float sampleRate) noexcept {
        m_sampleRate = sampleRate;
    }

    void setMode(NativeScopeMode mode) noexcept {
        m_mode = mode;
        repaint();
    }

    void setTraceColour(juce::Colour colourL, juce::Colour colourR = juce::Colour(0xffff007f)) noexcept {
        m_traceColourL = colourL;
        m_traceColourR = colourR;
        repaint();
    }

    void setBackgroundColour(juce::Colour colour) noexcept {
        m_bgColour = colour;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        const float w = bounds.getWidth();
        const float h = bounds.getHeight();
        const float midY = h * 0.5f;

        // 1. Dark Theme Background
        g.fillAll(m_bgColour);

        // 2. Reticle Grid
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        for (int x = 1; x < 8; ++x) {
            const float px = (w / 8.0f) * static_cast<float>(x);
            g.drawVerticalLine(static_cast<int>(px), 0.0f, h);
        }
        for (int y = 1; y < 4; ++y) {
            const float py = (h / 4.0f) * static_cast<float>(y);
            g.drawHorizontalLine(static_cast<int>(py), 0.0f, w);
        }
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawHorizontalLine(static_cast<int>(midY), 0.0f, w);

        if (m_samplesL.empty() || m_tap == nullptr) return;

        // 3. Dispatch Mode Painter
        switch (m_mode) {
            case NativeScopeMode::Oscilloscope:
                paintOscilloscope(g, w, h, midY);
                break;
            case NativeScopeMode::Spectrum:
                paintSpectrum(g, w, h);
                break;
            case NativeScopeMode::Lissajous:
                paintLissajous(g, w, h);
                break;
            case NativeScopeMode::PhaseMeter:
                paintPhaseMeter(g, w, h, midY);
                break;
        }

        // 4. Detected Note Badge
        if (!m_triggerResult.noteName.empty() && m_mode == NativeScopeMode::Oscilloscope) {
            g.setColour(juce::Colour(0xff00e676));
            g.setFont(11.0f);
            juce::String label = juce::String(m_triggerResult.noteName.data(), m_triggerResult.noteName.size())
                               + " (" + juce::String(static_cast<int>(m_triggerResult.estimatedFrequencyHz)) + " Hz)";
            g.drawText(label, getLocalBounds().reduced(8), juce::Justification::topRight, false);
        }
    }

private:
    void paintOscilloscope(juce::Graphics& g, float w, float h, float midY) {
        const size_t numSamples = m_samplesL.size();
        if (numSamples < 16) return;

        const size_t triggerOffset = m_triggerResult.triggerIndex;
        const float frac = m_triggerResult.triggerFraction;
        const float freq = m_triggerResult.estimatedFrequencyHz;

        // Adaptive cycle scaling by octave (identically to WebUI)
        size_t samplesToDisplay = numSamples - triggerOffset;
        if (freq > 20.0f && m_sampleRate > 0.0f) {
            const float cycleSamples = m_sampleRate / freq;
            float targetCycles = 4.0f;
            if (freq < 80.0f) targetCycles = 1.0f;
            else if (freq < 180.0f) targetCycles = 2.0f;
            else if (freq < 500.0f) targetCycles = 3.0f;
            else if (freq < 1200.0f) targetCycles = 5.0f;
            else targetCycles = 8.0f;

            const size_t needed = static_cast<size_t>(std::round(cycleSamples * targetCycles));
            if (needed >= 16 && (triggerOffset + needed + 2) <= numSamples) {
                samplesToDisplay = needed;
            }
        }

        if (samplesToDisplay < 2) return;
        const float stepX = w / static_cast<float>(samplesToDisplay - 1);
        const float scaleY = midY * 0.88f;

        // Render Left Channel
        juce::Path wavePathL;
        for (size_t i = 0; i < samplesToDisplay; ++i) {
            const float sPos = static_cast<float>(triggerOffset + i) + frac;
            const size_t idx0 = static_cast<size_t>(sPos);
            const size_t idx1 = std::min(idx0 + 1, numSamples - 1);
            const float interpFrac = sPos - static_cast<float>(idx0);

            const float sample = (1.0f - interpFrac) * m_samplesL[idx0] + interpFrac * m_samplesL[idx1];
            const float x = static_cast<float>(i) * stepX;
            const float y = midY - (sample * scaleY);

            if (i == 0) wavePathL.startNewSubPath(x, y);
            else wavePathL.lineTo(x, y);
        }

        g.setColour(m_traceColourL);
        g.strokePath(wavePathL, juce::PathStrokeType(1.8f));

        // Render Right Channel (Stereo Overlay)
        if (!m_samplesR.empty()) {
            juce::Path wavePathR;
            for (size_t i = 0; i < samplesToDisplay; ++i) {
                const float sPos = static_cast<float>(triggerOffset + i) + frac;
                const size_t idx0 = static_cast<size_t>(sPos);
                const size_t idx1 = std::min(idx0 + 1, numSamples - 1);
                const float interpFrac = sPos - static_cast<float>(idx0);

                const float sample = (1.0f - interpFrac) * m_samplesR[idx0] + interpFrac * m_samplesR[idx1];
                const float x = static_cast<float>(i) * stepX;
                const float y = midY - (sample * scaleY);

                if (i == 0) wavePathR.startNewSubPath(x, y);
                else wavePathR.lineTo(x, y);
            }

            g.setColour(m_traceColourR.withAlpha(0.65f));
            g.strokePath(wavePathR, juce::PathStrokeType(1.4f));
        }
    }

    void paintSpectrum(juce::Graphics& g, float w, float h) {
        juce::Path specPath;
        specPath.startNewSubPath(0.0f, h);

        const size_t bins = m_spectrumDb.size();
        for (size_t i = 0; i < bins; ++i) {
            const float normX = static_cast<float>(i) / static_cast<float>(bins - 1);
            const float x = normX * w;
            const float db = m_spectrumDb[i];
            const float normY = juce::jlimit(0.0f, 1.0f, (db + 96.0f) / 96.0f);
            const float y = h - (normY * h);
            specPath.lineTo(x, y);
        }
        specPath.lineTo(w, h);
        specPath.closeSubPath();

        // Gradient Fill
        juce::ColourGradient grad(m_traceColourL.withAlpha(0.35f), 0.0f, 0.0f,
                                  juce::Colours::transparentBlack, 0.0f, h, false);
        g.setGradientFill(grad);
        g.fillPath(specPath);

        g.setColour(m_traceColourL);
        g.strokePath(specPath, juce::PathStrokeType(1.6f));
    }

    void paintLissajous(juce::Graphics& g, float w, float h) {
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const float radius = std::min(cx, cy) * 0.85f;
        const float invSqrt2 = 0.70710678f;

        juce::Path lissPath;
        const size_t num = std::min(m_samplesL.size(), m_samplesR.size());
        for (size_t i = 0; i < num; ++i) {
            const float l = m_samplesL[i];
            const float r = m_samplesR[i];
            const float xRot = (l - r) * invSqrt2 * radius;
            const float yRot = (l + r) * invSqrt2 * radius;
            const float px = cx + xRot;
            const float py = cy - yRot;

            if (i == 0) lissPath.startNewSubPath(px, py);
            else lissPath.lineTo(px, py);
        }

        g.setColour(m_traceColourL.withAlpha(0.85f));
        g.strokePath(lissPath, juce::PathStrokeType(1.2f));
    }

    void paintPhaseMeter(juce::Graphics& g, float w, float h, float midY) {
        float sumLR = 0.0f, sumLL = 0.0f, sumRR = 0.0f;
        const size_t num = std::min(m_samplesL.size(), m_samplesR.size());
        for (size_t i = 0; i < num; ++i) {
            const float l = m_samplesL[i];
            const float r = m_samplesR[i];
            sumLR += l * r;
            sumLL += l * l;
            sumRR += r * r;
        }
        const float denom = std::sqrt(sumLL * sumRR);
        const float corr = denom > 1e-6f ? juce::jlimit(-1.0f, 1.0f, sumLR / denom) : 1.0f;

        const float barW = w * 0.8f;
        const float startX = w * 0.1f;
        const float barH = 12.0f;
        const float barY = midY - (barH * 0.5f);

        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(startX, barY, barW, barH, 4.0f);

        const float normPos = (corr + 1.0f) * 0.5f; // 0.0 to 1.0
        const float pointerX = startX + normPos * barW;

        juce::Colour corrColour = corr > 0.5f ? juce::Colour(0xff00e676)
                                : (corr > 0.0f ? juce::Colour(0xffffaa00) : juce::Colour(0xffff3344));

        g.setColour(corrColour);
        g.fillRoundedRectangle(pointerX - 4.0f, barY - 2.0f, 8.0f, barH + 4.0f, 3.0f);
    }

    void timerCallback() override {
        if (m_tap == nullptr || !m_tap->isActive()) return;

        const size_t available = m_tap->getAvailableRead();
        if (available >= m_samplesL.size()) {
            m_tap->read(m_samplesL.data(), m_samplesR.data(), m_samplesL.size());
            m_triggerResult = TriggerDetector::process(m_samplesL.data(), m_samplesL.size(), m_sampleRate);

            if (m_mode == NativeScopeMode::Spectrum) {
                computeFftSpectrum();
            }

            repaint();
        }
    }

    void computeFftSpectrum() {
        std::fill(m_fftData.begin(), m_fftData.end(), 0.0f);
        std::copy(m_samplesL.begin(), m_samplesL.end(), m_fftData.begin());

        m_fft.performFrequencyOnlyForwardTransform(m_fftData.data());

        const size_t bins = m_spectrumDb.size();
        for (size_t i = 0; i < bins; ++i) {
            const float mag = m_fftData[i];
            const float db = mag > 1e-5f ? 20.0f * std::log10(mag) - 40.0f : -96.0f;
            m_spectrumDb[i] = juce::jlimit(-96.0f, 0.0f, db);
        }
    }

    ScopeTap* m_tap { nullptr };
    float m_sampleRate { 44100.0f };
    NativeScopeMode m_mode { NativeScopeMode::Oscilloscope };
    juce::Colour m_traceColourL { 0xff00c3ff };
    juce::Colour m_traceColourR { 0xffff007f };
    juce::Colour m_bgColour { 0xff080c12 };

    std::vector<float> m_samplesL;
    std::vector<float> m_samplesR;
    TriggerResult m_triggerResult;

    juce::dsp::FFT m_fft;
    std::vector<float> m_fftData;
    std::vector<float> m_spectrumDb;
};

} // namespace abd::scope
#endif
