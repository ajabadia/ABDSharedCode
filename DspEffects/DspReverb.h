/*
  ==============================================================================

    DspReverb.h
    Port libre de JUCE de juce::Reverb (Fase 1, paso 5/6).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). ABDNeural lo consumia desde Source/DSP/Effects/ y ahora
    lo toma de este modulo, que es el que puede reutilizar cualquier sintoma.

    Fuente: JUCE 8.0.12, juce_audio_basics/utilities/juce_Reverb.h.
    Freeverb: 8 comb filters en paralelo + 4 all-pass en serie por canal, con
    los tunings exactos de la referencia (a 44100 Hz) y el stereo spread de 23.

    Port LITERAL: mismos tunings, mismas ramas, mismo orden de operaciones,
    mismo redondeo. Diferencias, todas de politica y ninguna de algoritmo:

      - jassert           -> dspAssert.
      - JUCE_UNDENORMALISE -> dspUndenormalise (ver DspCore.h): el macro de
        JUCE solo existe en x86 y no es un no-op aritmetico, de modo que
        juce::Reverb calcula distinto en nativo y en WASM. El port es no-op
        uniforme para sostener la paridad bit a bit del motor. La variante
        fiel a JUCE vive tras DSP_UNDENORMALISE_JUCE_POLICY, para el test que
        demuestra que el port coincide bit a bit con juce::Reverb.
      - JUCE_BEGIN_IGNORE_WARNINGS_MSVC(6011) -> el mismo pragma, portable.
      - HeapBlock -> dsp::HeapBlock, SmoothedValue -> dsp::SmoothedValue y
        JUCE_DECLARE_NON_COPYABLE -> = delete (los ports de DspCore.h).
      - `gain` se inicializa al valor que deja setParameters() en el
        constructor de JUCE (0.015f): mismo comportamiento, sin lectura
        indefinida.

    Aqui solo vive el EFECTO. El envoltorio de producto (mapeo wet/dry y
    suavizado de parametros) esta en Effects/Reverb.h, para que este fichero
    sea reutilizable tal cual por otros sintomas.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"

//==============================================================================
// Port de JUCE_BEGIN/END_IGNORE_WARNINGS_MSVC (juce_core/system/
// juce_CompilerWarnings.h). JUCE rodea processStereo/processMono con el para
// silenciar el C6011 ("dereferencing null pointer") que MSVC emite sobre
// left[i] despues del jassert de no-null. El motor tiene una puerta de "0
// warnings", asi que el port necesita el mismo silencio.

#if defined (_MSC_VER)
  #define DSP_IGNORE_MSVC(warnings)            __pragma (warning (disable: warnings))
  #define DSP_BEGIN_IGNORE_WARNINGS_MSVC(w)    __pragma (warning (push)) DSP_IGNORE_MSVC (w)
  #define DSP_END_IGNORE_WARNINGS_MSVC         __pragma (warning (pop))
#else
  #define DSP_IGNORE_MSVC(warnings)
  #define DSP_BEGIN_IGNORE_WARNINGS_MSVC(w)
  #define DSP_END_IGNORE_WARNINGS_MSVC
#endif

namespace abd::dsp
{

//==============================================================================
/**
    Reverb estereo simple, basada en la tecnica y los tunings de FreeVerb.
    Usa setSampleRate() para prepararla y luego processStereo() o processMono().

    @see dsp::undernormalise
*/
class Reverb
{
public:
    //==============================================================================
    Reverb()
    {
        setParameters (Parameters());
        setSampleRate (44100.0);
    }

    //==============================================================================
    /** Parametros que usa un objeto Reverb. */
    struct Parameters
    {
        float roomSize   = 0.5f;     /**< Tamano de sala, 0 a 1.0, 1.0 es grande. */
        float damping    = 0.5f;     /**< Amortiguacion, 0 a 1.0, 1.0 es mas amortiguado. */
        float wetLevel   = 0.33f;    /**< Nivel wet, 0 a 1.0 */
        float dryLevel   = 0.4f;     /**< Nivel dry, 0 a 1.0 */
        float width      = 1.0f;     /**< Anchura, 0 a 1.0, 1.0 es muy ancha. */
        float freezeMode = 0.0f;     /**< Freeze: < 0.5 modo normal, > 0.5 deja el
                                          reverb en lazo de realimentacion continuo. */
    };

    //==============================================================================
    /** Devuelve los parametros actuales. */
    const Parameters& getParameters() const noexcept    { return parameters; }

    /** Aplica un juego de parametros nuevo.
        No intenta bloquear el reverb: si se llama en paralelo al procesado
        pueden aparecer artefactos, igual que en JUCE.
    */
    void setParameters (const Parameters& newParams)
    {
        const float wetScaleFactor = 3.0f;
        const float dryScaleFactor = 2.0f;

        const float wet = newParams.wetLevel * wetScaleFactor;
        dryGain.setTargetValue (newParams.dryLevel * dryScaleFactor);
        wetGain1.setTargetValue (0.5f * wet * (1.0f + newParams.width));
        wetGain2.setTargetValue (0.5f * wet * (1.0f - newParams.width));

        gain = isFrozen (newParams.freezeMode) ? 0.0f : 0.015f;
        parameters = newParams;
        updateDamping();
    }

    //==============================================================================
    /** Fija el sample rate que usara el reverb.
        Hay que llamarlo antes de los metodos de procesado.
    */
    void setSampleRate (const double sampleRate)
    {
        dspAssert (sampleRate > 0);

        static const short combTunings[] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }; // (at 44100Hz)
        static const short allPassTunings[] = { 556, 441, 341, 225 };
        const int stereoSpread = 23;
        const int intSampleRate = (int) sampleRate;

        for (int i = 0; i < numCombs; ++i)
        {
            comb[0][i].setSize ((intSampleRate * combTunings[i]) / 44100);
            comb[1][i].setSize ((intSampleRate * (combTunings[i] + stereoSpread)) / 44100);
        }

        for (int i = 0; i < numAllPasses; ++i)
        {
            allPass[0][i].setSize ((intSampleRate * allPassTunings[i]) / 44100);
            allPass[1][i].setSize ((intSampleRate * (allPassTunings[i] + stereoSpread)) / 44100);
        }

        const double smoothTime = 0.01;
        damping .reset (sampleRate, smoothTime);
        feedback.reset (sampleRate, smoothTime);
        dryGain .reset (sampleRate, smoothTime);
        wetGain1.reset (sampleRate, smoothTime);
        wetGain2.reset (sampleRate, smoothTime);
    }

    /** Limpia los buffers del reverb. */
    void reset()
    {
        for (int j = 0; j < numChannels; ++j)
        {
            for (int i = 0; i < numCombs; ++i)
                comb[j][i].clear();

            for (int i = 0; i < numAllPasses; ++i)
                allPass[j][i].clear();
        }
    }

    //==============================================================================
    /** Aplica el reverb a dos canales estereo. */
    void processStereo (float* const left, float* const right, const int numSamples) noexcept
    {
        DSP_BEGIN_IGNORE_WARNINGS_MSVC (6011)
        dspAssert (left != nullptr && right != nullptr);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = (left[i] + right[i]) * gain;
            float outL = 0, outR = 0;

            const float damp    = damping.getNextValue();
            const float feedbck = feedback.getNextValue();

            for (int j = 0; j < numCombs; ++j)  // acumula los comb filters en paralelo
            {
                outL += comb[0][j].process (input, damp, feedbck);
                outR += comb[1][j].process (input, damp, feedbck);
            }

            for (int j = 0; j < numAllPasses; ++j)  // los all-pass en serie
            {
                outL = allPass[0][j].process (outL);
                outR = allPass[1][j].process (outR);
            }

            const float dry  = dryGain.getNextValue();
            const float wet1 = wetGain1.getNextValue();
            const float wet2 = wetGain2.getNextValue();

            left[i]  = outL * wet1 + outR * wet2 + left[i]  * dry;
            right[i] = outR * wet1 + outL * wet2 + right[i] * dry;
        }
        DSP_END_IGNORE_WARNINGS_MSVC
    }

    /** Aplica el reverb a un unico canal mono. */
    void processMono (float* const samples, const int numSamples) noexcept
    {
        DSP_BEGIN_IGNORE_WARNINGS_MSVC (6011)
        dspAssert (samples != nullptr);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = samples[i] * gain;
            float output = 0;

            const float damp    = damping.getNextValue();
            const float feedbck = feedback.getNextValue();

            for (int j = 0; j < numCombs; ++j)  // acumula los comb filters en paralelo
                output += comb[0][j].process (input, damp, feedbck);

            for (int j = 0; j < numAllPasses; ++j)  // los all-pass en serie
                output = allPass[0][j].process (output);

            const float dry  = dryGain.getNextValue();
            const float wet1 = wetGain1.getNextValue();

            samples[i] = output * wet1 + samples[i] * dry;
        }
        DSP_END_IGNORE_WARNINGS_MSVC
    }

private:
    //==============================================================================
    static bool isFrozen (const float freezeMode) noexcept  { return freezeMode >= 0.5f; }

    void updateDamping() noexcept
    {
        const float roomScaleFactor = 0.28f;
        const float roomOffset = 0.7f;
        const float dampScaleFactor = 0.4f;

        if (isFrozen (parameters.freezeMode))
            setDamping (0.0f, 1.0f);
        else
            setDamping (parameters.damping * dampScaleFactor,
                        parameters.roomSize * roomScaleFactor + roomOffset);
    }

    void setDamping (const float dampingToUse, const float roomSizeToUse) noexcept
    {
        damping.setTargetValue (dampingToUse);
        feedback.setTargetValue (roomSizeToUse);
    }

    //==============================================================================
    class CombFilter
    {
    public:
        CombFilter() noexcept {}

        void setSize (const int size)
        {
            if (size != bufferSize)
            {
                bufferIndex = 0;
                buffer.malloc ((size_t) size);
                bufferSize = size;
            }

            clear();
        }

        void clear() noexcept
        {
            last = 0;
            buffer.clear ((size_t) bufferSize);
        }

        float process (const float input, const float damp, const float feedbackLevel) noexcept
        {
            const float output = buffer[bufferIndex];
            last = (output * (1.0f - damp)) + (last * damp);
            dspUndenormalise (last);

            float temp = input + (last * feedbackLevel);
            dspUndenormalise (temp);
            buffer[bufferIndex] = temp;
            bufferIndex = (bufferIndex + 1) % bufferSize;
            return output;
        }

    private:
        HeapBlock<float> buffer;
        int bufferSize = 0, bufferIndex = 0;
        float last = 0.0f;

        CombFilter (const CombFilter&) = delete;
        CombFilter& operator= (const CombFilter&) = delete;
    };

    //==============================================================================
    class AllPassFilter
    {
    public:
        AllPassFilter() noexcept {}

        void setSize (const int size)
        {
            if (size != bufferSize)
            {
                bufferIndex = 0;
                buffer.malloc ((size_t) size);
                bufferSize = size;
            }

            clear();
        }

        void clear() noexcept
        {
            buffer.clear ((size_t) bufferSize);
        }

        float process (const float input) noexcept
        {
            const float bufferedValue = buffer[bufferIndex];
            float temp = input + (bufferedValue * 0.5f);
            dspUndenormalise (temp);
            buffer[bufferIndex] = temp;
            bufferIndex = (bufferIndex + 1) % bufferSize;
            return bufferedValue - input;
        }

    private:
        HeapBlock<float> buffer;
        int bufferSize = 0, bufferIndex = 0;

        AllPassFilter (const AllPassFilter&) = delete;
        AllPassFilter& operator= (const AllPassFilter&) = delete;
    };

    //==============================================================================
    enum { numCombs = 8, numAllPasses = 4, numChannels = 2 };

    Parameters parameters;
    float gain = 0.015f;

    CombFilter comb [numChannels][numCombs];
    AllPassFilter allPass [numChannels][numAllPasses];

    SmoothedValue<float> damping, feedback, dryGain, wetGain1, wetGain2;

    dspDeclareNonCopyableWithLeakDetector (Reverb)
};

} // namespace abd::dsp
