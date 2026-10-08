#pragma once
#include <cstdint>

namespace abd::synth
{
    /**
     * @brief Generador de LFO multionda modelado analógicamente (Seno, Triángulo,
     * Cuadrada, Rampas, Sample & Hold, Sample & Glide).
     *
     * Núcleo agnóstico en C++20 sin dependencias de GUI ni de JUCE.
     * Incorpora curva analógica de fade-in delay (40% silencio, 60% rampa lineal),
     * filtro de slew/glide en tiempo continuo, y rango de frecuencia extendido
     * de 0.005 Hz a 1280.0 Hz (audio-rate).
     */
    class LfoAnalog
    {
    public:
        enum class Shape
        {
            kSine = 0,
            kTriangle,
            kSquare,
            kRampUp,
            kRampDown,
            kSampleHold,
            kSampleGlide
        };

        LfoAnalog();
        ~LfoAnalog() = default;

        void setSampleRate(double sampleRate);
        void setRate(float rateHz);
        void setShape(int shapeIndex);
        void setKeySync(bool sync);
        void setDelay(float delaySec);
        void setSlew(float slewAmount);

        /**
         * Escalas de rango para deep-dive / calibración / parámetros extendidos
         * (default 1.0 = comportamiento base):
         * - rateScale: multiplica la frecuencia efectiva (hot-path, con guard).
         * - delayScale: multiplica el delay/fade-in en segundos.
         * - slewScale: multiplica el tiempo de transición del slew limiter.
         */
        void setRateScale(float scale);
        void setDelayScale(float scale);
        void setSlewScale(float scale);

        void reset();
        void trigger();
        void setPhase(double newPhase);
        double getPhase() const;

        float nextSample();
        float getUnipolar() const;

    private:
        double sampleRate = 44100.0;
        Shape currentShape = Shape::kSine;
        float rate = 1.0f;
        bool keySync = true;
        float delayTime = 0.0f;
        float slew = 0.0f;

        // Escalas de rango (default 1.0: sin alterar el comportamiento base)
        float rateScale = 1.0f;
        float delayScale = 1.0f;
        float slewScale = 1.0f;

        double phase = 0.0;
        double phaseIncrement = 0.0;
        double delaySamplesElapsed = 0.0;

        float lastOutput = 0.0f;
        float targetSH = 0.0f;
        float currentSH = 0.0f;

        uint32_t randomSeed = 123456789u;

        float generateRawWave();
        void updatePhaseIncrement();
        float nextRandomFloat();
    };
}
