/*
  ==============================================================================

    Dm12SpaceEchoProfile.h
    Perfil del Space Echo RE-201 según la implementación hardware del Behringer DeepMind 12.

    UBICACIÓN CANÓNICA: ABDSharedCode/DspEffects/profiles/Dm12SpaceEchoProfile.h

    QUÉ ES:
      Perfil específico para `abd::dsp::MultiHeadEcho` que replica con precisión
      los 5 modos de espaciado de cabezales y la respuesta de delay del efecto
      FX Type 39 del DeepMind 12.

    LOS 5 MODOS DEL DEEPMIND 12 (Subdivisiones rítmicas virtuales):
      - Modo A (0): Cabezal 1 solo (0.33x del retardo base, ganancia 1.0).
      - Modo B (1): Cabezal 1 (0.25x, ganancia 0.8) + Cabezal 2 (0.50x, ganancia 0.7).
      - Modo C (2): Cabezal 1 (0.20x, ganancia 0.9) + Cabezal 3 (0.65x, ganancia 0.6).
      - Modo D (3): Cabezal 2 (0.35x, ganancia 0.8) + Cabezal 3 (0.55x, ganancia 0.7).
      - Modo E (4): Cabezal 1 (0.15x) + Cabezal 2 (0.35x) + Cabezal 3 (0.60x).

    RELACIÓN CON Re201Profile:
      `Re201Profile.h` modela el selector rotatorio de 12 posiciones del panel frontal
      del hardware Roland RE-201 analógico original. Este perfil (`Dm12SpaceEchoProfile`)
      modela el mapeo específico seleccionado por Behringer/Klark Teknik para el rack
      digital del DeepMind 12.

  ==============================================================================
*/

#pragma once

namespace abd::dsp
{

struct Dm12SpaceEchoProfile
{
    struct Mode
    {
        bool head[3];
        bool reverb;
        float headRatio[3];
        float headGain[3];
    };

    static constexpr int numModes = 5;

    static constexpr Mode modes[5] =
    {
        // Modo A (0): Cabezal 1 solo
        { {true,  false, false}, true, {0.33f, 0.00f, 0.00f}, {1.0f, 0.0f, 0.0f} },
        // Modo B (1): Cabezal 1 + 2
        { {true,  true,  false}, true, {0.25f, 0.50f, 0.00f}, {0.8f, 0.7f, 0.0f} },
        // Modo C (2): Cabezal 1 + 3
        { {true,  false, true }, true, {0.20f, 0.00f, 0.65f}, {0.9f, 0.0f, 0.6f} },
        // Modo D (3): Cabezal 2 + 3
        { {false, true,  true }, true, {0.00f, 0.35f, 0.55f}, {0.0f, 0.8f, 0.7f} },
        // Modo E (4): Cabezales 1 + 2 + 3
        { {true,  true,  true }, true, {0.15f, 0.35f, 0.60f}, {0.7f, 0.7f, 0.6f} }
    };

    static float getHeadRatio(int mode, int head) noexcept
    {
        return modes[mode].headRatio[head];
    }

    static float getHeadGain(int mode, int head, float /*defaultGain*/) noexcept
    {
        return modes[mode].headGain[head];
    }

    static constexpr float headRatio[3] = {0.33f, 0.50f, 0.65f};
    static constexpr float headRightScale[3] = {0.95f, 0.90f, 0.92f};

    // Recorrido de tiempo de retardo: 120 ms a 1500 ms escalado por techo de 1.5
    static constexpr float minDelaySeconds = 0.12f * 1.5f; // 0.180s
    static constexpr float maxDelaySeconds = 1.50f * 1.5f; // 2.250s
    static constexpr float dryGain         = 1.0f;
    static constexpr float headOutputGain  = 1.0f;
    static constexpr float toneFrequencyHz = 800.0f;
    static constexpr float tankTimeL       = 0.080f;
    static constexpr float tankTimeR       = 0.110f;
    static constexpr float wowHz           = 0.5f;
    static constexpr float flutterHz       = 8.0f;
    static constexpr float wowAmount       = 0.015f;
    static constexpr float flutterAmount   = 0.005f;
    static constexpr float colourDrive     = 0.25f;
    static constexpr float colourHiss      = 0.008f;
};

} // namespace abd::dsp
