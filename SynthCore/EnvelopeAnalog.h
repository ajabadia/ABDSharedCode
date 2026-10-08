#pragma once

namespace abd::synth
{
    /**
     * @brief Generador de envolvente ADSR analógico de 4 fases con curvatura ajustable por etapa.
     * Núcleo agnóstico en C++20 sin dependencias de GUI ni de JUCE.
     *
     * Incorpora curvatura no lineal continua por etapa (-1..+1: exp/linear/log),
     * modulación dinámica de curvatura por matriz, desplazamiento de sustain,
     * escala temporal dinámica (para emulación de drift analógico),
     * modo de repetición en bucle (loopMode) y modo de un solo disparo (bypassSustain / One-Shot).
     */
    class EnvelopeAnalog
    {
    public:
        enum class Stage
        {
            kIdle,
            kAttack,
            kDecay,
            kSustain,
            kRelease
        };

        EnvelopeAnalog();
        ~EnvelopeAnalog() = default;

        void setSampleRate(double sampleRate);
        void setParameters(float attackTimeSec, float decayTimeSec, float sustainLevel, float releaseTimeSec);
        void setCurves(float attackCurve, float decayCurve, float sustainCurve, float releaseCurve);

        /** Desplazamiento del nivel de sustain (suma sobre sustainLevel, clamp 0..1). */
        void setSustainOffset(float offset);
        /** Modulación dinámica de curvatura por etapa (-1..1), sumada a la curva base. */
        void setCurveModulation(Stage stage, float modAmount);

        void trigger();
        void release();
        void reset();

        float nextSample();
        bool isActive() const { return currentStage != Stage::kIdle; }
        Stage getCurrentStage() const { return currentStage; }
        /** Nivel actual de la envolvente (para decisiones de asignación y robo de voz). */
        float getCurrentLevel() const { return currentLevel; }

        /** Nivel de sustain efectivo: clamp(sustainLevel + sustainOffset, 0, 1). */
        float sustainEfectivo() const;

        /**
         * Ajusta dinámicamente la velocidad de la fase actual.
         * scale=1.0 → velocidad normal.
         * scale>1.0 → fase más rápida (menor duración).
         * scale<1.0 → fase más lenta (mayor duración).
         * Se usa para aplicar envTimeDrift del Analog Drift Engine.
         */
        void setTimeScale(float scale);

        /**
         * Activa/desactiva el modo Loop.
         * En modo Loop, al completar la fase Decay (entrar a Sustain),
         * la envolvente se re-triggerea automáticamente a Attack.
         * Release() desactiva el loop temporalmente para que el release
         * complete y la envolvente retorne a Idle.
         */
        void setLoopMode(bool loop);
        bool getLoopMode() const { return loopMode; }

        /**
         * Modo One-Shot (voice.envelopeTriggerMode=3): al completar Decay, salta
         * directamente a Release (sin fase Sustain). El note-off se ignora en
         * el host, así que la envolvente completa sola su ciclo.
         */
        void setBypassSustain(bool bypass);
        bool getBypassSustain() const { return bypassSustain; }

    private:
        double sampleRate = 44100.0;
        Stage currentStage = Stage::kIdle;

        // Tiempos y niveles de destino
        float attackTime = 0.01f;
        float decayTime = 0.1f;
        float sustainLevel = 1.0f;
        float releaseTime = 0.2f;

        // Curvaturas (-1.0 a 1.0: < 0 exp, 0 linear, > 0 log)
        float attackCurve = 0.0f;
        float decayCurve = 0.0f;
        float sustainCurve = 0.0f;
        float releaseCurve = 0.0f;

        // Desplazamiento del sustain (-1..1) y modulación de curva por etapa (-1..1)
        float sustainOffset = 0.0f;
        float curveModulation[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // indexado por (int)Stage

        // Estado interno de la fase actual
        double currentProgress = 0.0; // 0.0 a 1.0 dentro de la fase actual
        double progressIncrement = 0.0;
        double currentStageDurationSec = 0.01; // duración de la fase actual (para setTimeScale)
        float timeScale = 1.0f;  // escala de tiempo activa (para guards + changeStage)
        float startLevel = 0.0f;
        float targetLevel = 0.0f;
        float currentLevel = 0.0f;

        bool loopMode = false;
        bool bypassSustain = false;

        void changeStage(Stage newStage);
        float applyCurve(float progress, float curveAmount);
    };
}
