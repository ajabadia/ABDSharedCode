#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace abd::synth
{
    /**
     * @brief Secuenciador de control algorítmico determinista y portable en C++20.
     * Núcleo agnóstico de framework sin dependencias de GUI ni de JUCE.
     *
     * POR QUÉ ESTA CLASE EXISTE:
     * El secuenciador de control actúa como una FUENTE DE MODULACIÓN que genera
     * señales de control continuas por muestra (slew/glide) a partir de un patrón
     * de 1 a 32 pasos bipolares, sincronizado por división métrica sobre el BPM maestro.
     * Además, su tasa de slew es modulable a través de la matriz de modulación.
     */
    class ControlSequencer
    {
    public:
        ControlSequencer() = default;

        void prepare(double sampleRate);

        void setEnabled(bool on)     { enabled = on; }
        /** 0-15, el rango confirmado por NRPN (manual: el resto va por menú). */
        void setClockDivider(int div){ clockDivider = std::clamp(div, 0, 15); }
        /** Pasos activos: 1 a 32. */
        void setLength(int steps)    { length = std::clamp(steps, 1, 32); }
        /** Swing normalizado 0..1: 0 = recto (50%), 1 = máximo (75%). */
        void setSwing(float swing)   { swingAmount = std::clamp(swing, 0.0f, 1.0f); }
        /** 0 = bucle, 1 = reinicia con la tecla y no buclea, 2 = las dos. */
        void setKeyLoopMode(int mode){ keyLoopMode = std::clamp(mode, 0, 2); }
        /** Velocidad del glide, normalizada 0..1. La modulación de matriz se le suma encima. */
        void setSlewRate(float rate) { slewRate = std::clamp(rate, 0.0f, 1.0f); }
        /** Cuánto le mete la matriz de modulación al slew, aparte del mando. */
        void setSlewModulation(float m) { slewMod = m; }

        /** Paso de 1 a 32, en bipolar: -1 a +1 (el byte 0 es un -1 aquí). */
        void setStep(int stepOneBased, float bipolar);

        /**
         * Tempo: el BPM maestro, que comparte con el arpegiador. De aquí sale la
         * duración del paso con el divisor de esta tabla.
         */
        void setMasterBpm(float bpm) { masterBpm = bpm < 20.0f ? 20.0f : bpm; }

        void reset(); // el "key sync": volver al primer paso

        /** El valor bipolar de esta muestra, ya con el slew aplicado. */
        float nextSample();

        bool isEnabled() const      { return enabled; }
        int  getCurrentStep() const { return stepIndex; }

        /** Los multiplicadores de negras por paso según divisor de reloj. */
        static float quartersPerStep(int clockDivider);

    private:
        bool  enabled = false;
        int   clockDivider = 13; // 1/16, el valor por defecto del manual
        int   length = 1;
        int   keyLoopMode = 0;
        float swingAmount = 0.0f;
        float slewRate = 0.0f;
        float slewMod = 0.0f;
        float masterBpm = 120.0f;

        double sampleRate = 44100.0;
        double samplesPerStep = 22050.0;
        double phase = 0.0;
        int    stepIndex = 0;
        bool   corrio = false; // el modo "no buclea": ya dio su pasada

        float steps[32] = {};
        float currentValue = 0.0f;
        float targetValue = 0.0f;
    };
}
