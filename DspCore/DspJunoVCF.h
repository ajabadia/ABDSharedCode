#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "DspVcfVoicing.h"

namespace abd::dsp
{

constexpr float kJunoVCFPi = 3.14159265358979323846f;

// ============================================================
// Polyphase IIR resampler (Laurent de Soras) — 2x up/down
// ============================================================
static constexpr int kNumResamplerCoefs = 12;

static constexpr double kResamplerCoefs2x[kNumResamplerCoefs] = {
    0.036681502163648017, 0.13654762463195794, 0.27463175937945444,
    0.42313861743656711, 0.56109869787919531, 0.67754004997416184,
    0.76974183386322703, 0.83988962484963892, 0.89226081800387902,
    0.9315419599631839,  0.96209454837808417, 0.98781637073289585
};

struct Upsampler2x
{
    float coef[kNumResamplerCoefs] = {};
    float x[kNumResamplerCoefs] = {};
    float y[kNumResamplerCoefs] = {};

    inline void setCoefs(const double c[kNumResamplerCoefs]) noexcept
    {
        for (int i = 0; i < kNumResamplerCoefs; ++i)
            coef[i] = static_cast<float>(c[i]);
    }

    inline void clearBuffers() noexcept
    {
        std::memset(x, 0, sizeof(x));
        std::memset(y, 0, sizeof(y));
    }

    inline void processSample(float& out0, float& out1, float input) noexcept
    {
        float even = input;
        float odd  = input;
        for (int i = 0; i < kNumResamplerCoefs; i += 2)
        {
            float t0 = (even - y[i])     * coef[i]     + x[i];
            float t1 = (odd  - y[i + 1]) * coef[i + 1] + x[i + 1];
            x[i]     = even;   x[i + 1] = odd;
            y[i]     = t0;     y[i + 1] = t1;
            even = t0;          odd = t1;
        }
        out0 = even;
        out1 = odd;
    }
};

struct Downsampler2x
{
    float coef[kNumResamplerCoefs] = {};
    float x[kNumResamplerCoefs] = {};
    float y[kNumResamplerCoefs] = {};

    inline void setCoefs(const double c[kNumResamplerCoefs]) noexcept
    {
        for (int i = 0; i < kNumResamplerCoefs; ++i)
            coef[i] = static_cast<float>(c[i]);
    }

    inline void clearBuffers() noexcept
    {
        std::memset(x, 0, sizeof(x));
        std::memset(y, 0, sizeof(y));
    }

    inline float processSample(const float in[2]) noexcept
    {
        float spl0 = in[1];
        float spl1 = in[0];
        for (int i = 0; i < kNumResamplerCoefs; i += 2)
        {
            float t0 = (spl0 - y[i])     * coef[i]     + x[i];
            float t1 = (spl1 - y[i + 1]) * coef[i + 1] + x[i + 1];
            x[i]     = spl0;   x[i + 1] = spl1;
            y[i]     = t0;     y[i + 1] = t1;
            spl0 = t0;          spl1 = t1;
        }
        return 0.5f * (spl0 + spl1);
    }
};

namespace detail
{
    inline float junoFastTan(float x) noexcept
    {
        x = std::clamp(x, 0.0f, 1.425f);
        float x2 = x * x;
        float s = x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f
                  + x2 * (1.0f / 362880.0f + x2 * (-1.0f / 39916800.0f))))));
        float c = 1.0f + x2 * (-1.0f / 2.0f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f
                  + x2 * (1.0f / 40320.0f + x2 * (-1.0f / 3628800.0f)))));
        return (c > 1.0e-7f) ? (s / c) : (s * 1.0e7f);
    }
}

/**
 * JunoVCF_ZDF — TPT ZDF OTA Ladder Filter (IR3109 / 80017A model)
 *
 * Parametrized filter core with:
 *   - Direct cutoff frequency input (cutoffHz or normalized freq)
 *   - Selectable voicings (Juno106 / DeepMind) via VcfVoicing injection
 *   - High-quality 2x/4x polyphase IIR oversampling
 *   - Newton-Raphson 1-stage non-linear solver with Padé 3/3 tanh approximation
 */
class JunoVCF_ZDF
{
public:
    enum class Mode
    {
        Juno106,
        DeepMind
    };

    enum class PoleMode
    {
        FourPole,   // 24 dB/oct — 4 integrators in cascade
        TwoPole     // 12 dB/oct — 2 integrators in cascade
    };

    inline JunoVCF_ZDF()
    {
        mUp1.setCoefs(kResamplerCoefs2x);
        mUp2.setCoefs(kResamplerCoefs2x);
        mDown1.setCoefs(kResamplerCoefs2x);
        mDown2.setCoefs(kResamplerCoefs2x);
        setMode(Mode::DeepMind);
        reset();
    }

    inline void setMode(Mode m) noexcept
    {
        mMode = m;
        invalidateCoefficientCaches();
        if (m == Mode::Juno106)
        {
            mVoicing.resonanceCurve = [](float r) {
                return ResK_J106(r);
            };
            mVoicing.gainCompCurve = [](float) {
                return 1.0f;
            };
            mVoicing.stageSaturationAmount = 1.0f;
        }
        else if (m == Mode::DeepMind)
        {
            mVoicing.resonanceCurve = [](float r) {
                return ResK_J106(r) * VcfCalibration::kDeepMindResCurveScale;
            };
            mVoicing.gainCompCurve = [](float r) {
                float a = VcfCalibration::kDeepMindGainCompStrength;
                return 1.0f / (1.0f + a * r * r);
            };
            mVoicing.stageSaturationAmount = VcfCalibration::kDeepMindStageSaturation;
        }
    }

    inline void setPoleMode(PoleMode m) noexcept
    {
        mPoleMode = m;
        invalidateCoefficientCaches();
    }

    inline void setVoicing(const VcfVoicing& voicing) noexcept
    {
        mVoicing = voicing;
        invalidateCoefficientCaches();
    }

    inline void reset() noexcept
    {
        s.fill(0.0f);
        lastOutput = 0.0f;
        mUp1.clearBuffers();
        mUp2.clearBuffers();
        mDown1.clearBuffers();
        mDown2.clearBuffers();
        mInputEnv = 0.0f;
        invalidateCoefficientCaches();
    }

    inline void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        mEnvDecay = std::exp(-1.0f / (VcfCalibration::kInputEnvTauSec * static_cast<float>(sampleRate)
                                        * static_cast<float>(mOversample)));
        reset();
    }

    inline void setOversample(int factor) noexcept
    {
        int prev = mOversample;
        mOversample = (factor <= 1) ? 1 : (factor == 2) ? 2 : 4;
        mEnvDecay = std::exp(-1.0f / (VcfCalibration::kInputEnvTauSec * static_cast<float>(sampleRate)
                                        * static_cast<float>(mOversample)));
        if (mOversample == 4 && prev == 2)
        {
            mUp2.clearBuffers();
            mDown2.clearBuffers();
        }
    }

    inline Mode getMode() const noexcept { return mMode; }
    inline PoleMode getPoleMode() const noexcept { return mPoleMode; }
    inline int getOversample() const noexcept { return mOversample; }

    /**
     * Process a single sample through the ZDF ladder filter.
     * @param input   Audio sample (-1..1)
     * @param frq     Normalized frequency (cutoffHz / Nyquist), 0..0.5
     * @param res     Resonance 0..1 (mapped internally to feedback k)
     * @return        Filtered output
     */
    inline float process(float input, float frq, float res) noexcept
    {
        frq = std::clamp(frq, 0.00001f, VcfCalibration::kMaxNormalizedFreq);

        float k = 0.0f;
        if (mVoicing.resonanceCurve)
            k = mVoicing.resonanceCurve(res);
        else
            k = ResK_J106(res);

        k = SoftClipK(k);
        float kPassed = k;

        if (frq > 0.5f)
            k *= std::max(1.0f - (frq - 0.5f) * 1.0f, 0.5f);

        if (std::abs(mLastOutFrq - frq) > kJunoVCFCacheEps ||
            std::abs(mLastOutK - k) > kJunoVCFCacheEps)
        {
            mLastOutFrq = frq;
            mLastOutK = k;
            mCachedFreqComp = FreqCompensationClamped(k, frq * 0.25f);
        }
        mFreqComp = mCachedFreqComp;

        if (mOversample == 4)
            lastOutput = process4x(input, frq, res, kPassed);
        else if (mOversample == 2)
            lastOutput = process2x(input, frq, res, kPassed);
        else
            lastOutput = processSampleInternal(input, frq, res, kPassed);

        return lastOutput;
    }

    // Resonance curve: J106 polynomial fit (public for calibration/tests)
    static inline float ResK_J106(float res) noexcept
    {
        float r2 = res * res;
        float r3 = r2 * res;
        float r4 = r2 * r2;
        return 1.24f * (4.7116f * res - 6.5743f * r2 + 13.4633f * r3 - 8.2197f * r4);
    }

    // Soft-clip resonance above k=4.0 (OTA gain compression)
    static inline float SoftClipK(float k) noexcept
    {
        if (k > 4.0f)
        {
            float excess = k - 4.0f;
            k = 4.0f + excess / (1.0f + excess * 0.2f);
        }
        return std::min(k, 6.6f);
    }

private:
    // ---- OTA saturation functions (Pade 3/3 tanh approximation) ----
    static inline float OTASat(float x) noexcept
    {
        if (x > 3.f) return 1.f;
        if (x < -3.f) return -1.f;
        float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    static inline float OTASatDeriv(float x) noexcept
    {
        if (x > 3.f || x < -3.f) return 0.f;
        float x2 = x * x;
        float d = 27.f + 9.f * x2;
        return 27.f * (27.f - 3.f * x2) / (d * d);
    }

    // Non-linear stage solver via Newton-Raphson
    static inline float NLStage(float& st, float x, float g, float g1, float otaScale) noexcept
    {
        float y = st + g1 * (x - st);
        float diff = x - y;
        float sd = diff * otaScale;
        float t = OTASat(sd) / otaScale;
        float f = y - st - g * t;
        float df = 1.f + g * OTASatDeriv(sd);
        y -= f / df;
        st = 2.f * y - st;
        return y;
    }

    static inline float FreqCompensationClamped(float k, float frq) noexcept
    {
        float lowQ = std::max(1.0f, 0.42f * std::pow(std::max(frq, 1e-6f), -0.12f));
        float logdist = std::log(std::max(frq, 1e-6f) / 0.012f);
        lowQ += 0.20f * std::exp(-logdist * logdist / 1.0f);
        float blend = std::min(k * k * 0.0625f, 1.f);
        return lowQ + blend * (1.f - lowQ);
    }

    static inline float InputComp(float k, float frq) noexcept
    {
        float qComp = 0.379f + 0.087f * k;
        float freqGain = std::pow(std::max(frq, 1e-6f) * (1.f / 0.00445f), -0.10f);
        freqGain = std::clamp(freqGain, 0.65f, 1.2f);
        return qComp * freqGain;
    }

    static constexpr float kOTAScaleBase = 0.35f;
    static inline float OTAScaleForFreq(float frq, float res = 0.f) noexcept
    {
        float scale = kOTAScaleBase;
        if (frq < 0.005f)
        {
            float blend = std::max(frq / 0.005f, 0.15f);
            scale *= blend;
        }
        if (res > 0.f)
        {
            float resK = ResK_J106(res);
            float resBlend = std::min(resK * resK * 0.0625f, 1.f);
            scale = scale + resBlend * (kOTAScaleBase - scale);
        }
        return scale;
    }

    inline void invalidateCoefficientCaches() noexcept
    {
        mLastOutFrq = -1.0f;
        mLastOutK = -1.0f;
        mLastIntFrq = -1.0f;
        mLastIntRes = -1.0f;
        mLastIntK = -1.0f;
        mLastIntFreqComp = -1.0f;
    }

    inline float process2x(float input, float frq, float res, float k) noexcept
    {
        float up[2], down[2];
        mUp1.processSample(up[0], up[1], input);

        float frq2x = frq * 0.5f;
        down[0] = processSampleInternal(up[0], frq2x, res, k);
        down[1] = processSampleInternal(up[1], frq2x, res, k);

        return mDown1.processSample(down);
    }

    inline float process4x(float input, float frq, float res, float k) noexcept
    {
        float frq4x = frq * 0.25f;

        float up2x[2];
        mUp1.processSample(up2x[0], up2x[1], input);

        float down4x[2], down2x[2];

        float up4x_a[2];
        mUp2.processSample(up4x_a[0], up4x_a[1], up2x[0]);
        down4x[0] = processSampleInternal(up4x_a[0], frq4x, res, k);
        down4x[1] = processSampleInternal(up4x_a[1], frq4x, res, k);
        down2x[0] = mDown2.processSample(down4x);

        float up4x_b[2];
        mUp2.processSample(up4x_b[0], up4x_b[1], up2x[1]);
        down4x[0] = processSampleInternal(up4x_b[0], frq4x, res, k);
        down4x[1] = processSampleInternal(up4x_b[1], frq4x, res, k);
        down2x[1] = mDown2.processSample(down4x);

        return mDown1.processSample(down2x);
    }

    inline float processSampleInternal(float input, float frq, float res, float k) noexcept
    {
        mNoiseSeed = mNoiseSeed * 196314165u + 907633515u;
        float white = static_cast<float>(mNoiseSeed) / static_cast<float>(0xFFFFFFFFu) * 2.0f - 1.0f;
        mInputEnv = std::max(std::abs(input), mInputEnv * mEnvDecay);
        float stateEnergy = std::abs(s[0]) + std::abs(s[1]) + std::abs(s[2]) + std::abs(s[3]);
        float energy = std::max(mInputEnv, stateEnergy);
        float noiseLevel = VcfCalibration::kNoiseLevelBase / (static_cast<float>(mOversample) * (1.0f + energy * VcfCalibration::kNoiseEnergyGain));
        input += white * noiseLevel;

        if (frq > 0.5f)
            k *= std::max(1.0f - (frq - 0.5f) * 1.0f, 0.5f);

        float g, g1, comp;
        if (std::abs(mLastIntFrq - frq) > kJunoVCFCacheEps ||
            std::abs(mLastIntRes - res) > kJunoVCFCacheEps ||
            std::abs(mLastIntK - k) > kJunoVCFCacheEps ||
            mLastIntFreqComp != mFreqComp)
        {
            mLastIntFrq = frq;
            mLastIntRes = res;
            mLastIntK = k;
            mLastIntFreqComp = mFreqComp;
            g = detail::junoFastTan(frq * kJunoVCFPi * 0.5f);
            g *= mFreqComp;
            g1 = g / (1.0f + g);
            comp = InputComp(k, frq);
            mCachedG = g;
            mCachedG1 = g1;
            mCachedComp = comp;
        }
        else
        {
            g = mCachedG;
            g1 = mCachedG1;
            comp = mCachedComp;
        }

        float G = 0.0f;
        float S = 0.0f;

        if (mPoleMode == PoleMode::TwoPole)
        {
            G = g1 * g1;
            S = s[0] * g1 + s[1];
        }
        else
        {
            G = g1 * g1 * g1 * g1;
            S = s[0] * g1 * g1 * g1 + s[1] * g1 * g1 + s[2] * g1 + s[3];
        }

        float kFbScale = 4.20f * std::clamp((k - 2.5f) * 1.0f, 0.3f, 1.0f);
        float fbSig = OTASat(S * kFbScale) / kFbScale;

        float u = (input * comp * mVoicing.stageSaturationAmount - k * fbSig) / (1.0f + k * G);

        float stateAmp = std::abs(s[3]);
        float dfGain = 1.0f / std::sqrt(1.0f + 0.6f * stateAmp * stateAmp);
        dfGain = std::max(dfGain, 0.65f);

        float hfFade = std::clamp((0.12f - frq) * 25.0f, 0.0f, 1.0f);
        dfGain = 1.0f - hfFade * (1.0f - dfGain);
        float g1NL = g1 / dfGain;
        g1NL = std::min(g1NL, 0.98f);

        float gNL = g1NL / (1.0f - g1NL);
        float ota = OTAScaleForFreq(frq, res);

        float lp1 = NLStage(s[0], u,   gNL, g1NL, ota);
        float lp2 = NLStage(s[1], lp1, gNL, g1NL, ota);

        float output = 0.0f;
        if (mPoleMode == PoleMode::TwoPole)
        {
            output = lp2;
        }
        else
        {
            float lp3 = NLStage(s[2], lp2, gNL, g1NL, ota);
            float lp4 = NLStage(s[3], lp3, gNL, g1NL, ota);
            output = lp4;
        }

        for (auto& st : s)
        {
            if (std::abs(st) < 1.0e-15f)
                st = 0.0f;
        }

        float frqFactor  = std::clamp((frq - VcfCalibration::kOutTameFreqFloor) * VcfCalibration::kOutTameFreqSlope, 0.0f, 1.0f);
        float resFactor  = std::clamp((k - VcfCalibration::kOutTameResStart) * VcfCalibration::kOutTameResSlope, 0.0f, 1.0f);
        float outputScale = 1.0f - frqFactor * resFactor * VcfCalibration::kOutTameMaxReduction;

        if (mVoicing.gainCompCurve)
            outputScale *= mVoicing.gainCompCurve(res);

        return output * VcfCalibration::kOutputScale * outputScale;
    }

    std::array<float, 4> s{};
    float lastOutput = 0.0f;

    double sampleRate = 44100.0;
    int mOversample = 1;

    uint32_t mNoiseSeed = 123456789u;
    float mInputEnv = 0.f;
    float mEnvDecay = 0.999f;
    float mFreqComp = 1.f;

    static constexpr float kJunoVCFCacheEps = 1.0e-4f;

    float mLastOutFrq = -1.0f;
    float mLastOutK = -1.0f;
    float mCachedFreqComp = 1.f;

    float mLastIntFrq = -1.0f;
    float mLastIntRes = -1.0f;
    float mLastIntK = -1.0f;
    float mLastIntFreqComp = -1.0f;
    float mCachedG = 0.0f;
    float mCachedG1 = 0.0f;
    float mCachedComp = 0.0f;

    Upsampler2x mUp1, mUp2;
    Downsampler2x mDown1, mDown2;

    Mode mMode = Mode::DeepMind;
    PoleMode mPoleMode = PoleMode::FourPole;
    VcfVoicing mVoicing;
};

} // namespace abd::dsp
