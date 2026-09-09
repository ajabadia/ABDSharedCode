/**
 * @file LutEvaluatorSimd.h
 * @brief High-performance SIMD LUT evaluator for 2D batched parameter matrices and 1D Phase Distortion curves.
 * @author ABDSynths
 * @date 2026
 *
 * Consumes alignas(16) AbdBatchedPoint tables produced by LutExporter and 1D response curves.
 * Implements branchless vectorized bilinear and linear interpolation for audio blocks and polyphonic voices.
 */

#pragma once

#include <immintrin.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace abd::lutdsp
{

/**
 * @brief 16-byte aligned batched point matching LutExporter's binary contract.
 */
struct alignas(16) AbdBatchedPoint
{
    float p1;          // Parameter X (Normalized [0.0..1.0])
    float p2;          // Parameter Y (Normalized [0.0..1.0])
    float mu;          // Primary Value Mean (Control response / Cutoff / etc.)
    float sigma;       // Standard Deviation (Analog thermal drift / chaos)
    float sec_mu;      // Secondary Value Mean (e.g. Q factor / resonance)
    float sec_sigma;   // Secondary Value StdDev
    float thd_percent; // Harmonic Distortion %
    float reserved;    // 16-byte padding for SIMD boundary guarantee
};

/**
 * @brief Metric selector for multi-dimensional evaluation.
 */
enum class LutMetric
{
    PrimaryMean,       // mu
    PrimaryStdDev,     // sigma
    SecondaryMean,     // sec_mu
    SecondaryStdDev,   // sec_sigma
    ThdPercent         // thd_percent
};

/**
 * @brief SIMD evaluator for 1D response curves (e.g., Casio CZ DCW Phase Distortion curves).
 * Branchless SIMD linear interpolation supporting sample-by-sample and block processing.
 */
class LutEvaluator1DSimd
{
public:
    LutEvaluator1DSimd() = default;
    ~LutEvaluator1DSimd() = default;

    /**
     * @brief Loads normalized response curve points and prepares 16-byte aligned lookup table.
     * @param normalizedPoints Vector of response values (e.g., 100 DCW sweep points).
     */
    void loadLutData(const std::vector<float>& normalizedPoints)
    {
        if (normalizedPoints.empty())
        {
            alignedLut.clear();
            lutSize = 0;
            lutScaleFactor = 0.0f;
            return;
        }

        lutSize = static_cast<int>(normalizedPoints.size());
        lutScaleFactor = (lutSize > 1) ? static_cast<float>(lutSize - 1) : 0.0f;

        // Size padded up to multiple of 4 for safe SIMD boundary access
        size_t paddedSize = static_cast<size_t>((lutSize + 3) & ~3);
        alignedLut.assign(paddedSize, normalizedPoints.back());
        std::copy(normalizedPoints.begin(), normalizedPoints.end(), alignedLut.begin());
    }

    [[nodiscard]] int getLutSize() const noexcept { return lutSize; }
    [[nodiscard]] float getScaleFactor() const noexcept { return lutScaleFactor; }
    [[nodiscard]] bool isEmpty() const noexcept { return lutSize == 0; }

    /**
     * @brief Branchless evaluation of 4 normalized X inputs [0.0, 1.0] simultaneously.
     * Computes Y = Y0 + t * (Y1 - Y0) using 128-bit SIMD registers.
     */
    inline __m128 evaluateSingleValueSimd(__m128 normalizedX) const noexcept
    {
        if (lutSize <= 0)
            return _mm_setzero_ps();

        if (lutSize == 1)
            return _mm_set1_ps(alignedLut[0]);

#if defined(__AVX2__) || defined(__SSE2__)
        __m128 zero = _mm_setzero_ps();
        __m128 maxCoord = _mm_set1_ps(lutScaleFactor);

        // Clamp normalizedX to [0.0, 1.0] and scale to table index space [0, lutSize - 1]
        __m128 scaledX = _mm_min_ps(_mm_max_ps(_mm_mul_ps(normalizedX, maxCoord), zero), maxCoord);

        // Truncate to integer index I0
        __m128i idx0 = _mm_cvttps_epi32(scaledX);
        __m128 floorX = _mm_cvtepi32_ps(idx0);

        // Fractional weight t = scaledX - floor(scaledX) in [0.0, 1.0)
        __m128 t = _mm_sub_ps(scaledX, floorX);

        alignas(16) int i0[4];
        _mm_store_si128(reinterpret_cast<__m128i*>(i0), idx0);

        // Fetch Y0 and Y1 branchless with boundary clamping
        alignas(16) float y0[4];
        alignas(16) float y1[4];
        const int maxIdx = lutSize - 1;

        for (int i = 0; i < 4; ++i)
        {
            int c0 = i0[i];
            int c1 = (c0 < maxIdx) ? (c0 + 1) : maxIdx;
            y0[i] = alignedLut[static_cast<size_t>(c0)];
            y1[i] = alignedLut[static_cast<size_t>(c1)];
        }

        __m128 vY0 = _mm_load_ps(y0);
        __m128 vY1 = _mm_load_ps(y1);
        __m128 diff = _mm_sub_ps(vY1, vY0);

#if defined(__AVX2__) && defined(__FMA__)
        return _mm_fmadd_ps(t, diff, vY0);
#else
        return _mm_add_ps(vY0, _mm_mul_ps(t, diff));
#endif
#else
        alignas(16) float inX[4];
        alignas(16) float outY[4];
        _mm_store_ps(inX, normalizedX);
        for (int i = 0; i < 4; ++i)
            outY[i] = evaluateSingle(inX[i]);
        return _mm_load_ps(outY);
#endif
    }

    /**
     * @brief Scalar evaluation with identical mathematical formulation.
     */
    [[nodiscard]] inline float evaluateSingle(float normalizedX) const noexcept
    {
        if (lutSize <= 0)
            return 0.0f;
        if (lutSize == 1)
            return alignedLut[0];

        float clampedX = std::clamp(normalizedX, 0.0f, 1.0f);
        float scaled = clampedX * lutScaleFactor;
        int i0 = static_cast<int>(scaled);
        int i1 = std::min(i0 + 1, lutSize - 1);
        float t = scaled - static_cast<float>(i0);

        return alignedLut[static_cast<size_t>(i0)] + t * (alignedLut[static_cast<size_t>(i1)] - alignedLut[static_cast<size_t>(i0)]);
    }

    /**
     * @brief In-place SIMD audio block processor.
     * Transforms audio samples or modulation streams in 4-sample SIMD chunks.
     */
    void processBlockSimd(float* audioData, int numSamples) const noexcept
    {
        if (audioData == nullptr || numSamples <= 0 || lutSize <= 0)
            return;

        int i = 0;
        int simdBlocks = numSamples & ~3;

        for (; i < simdBlocks; i += 4)
        {
            __m128 inVec = _mm_loadu_ps(audioData + i);
            __m128 outVec = evaluateSingleValueSimd(inVec);
            _mm_storeu_ps(audioData + i, outVec);
        }

        for (; i < numSamples; ++i)
        {
            audioData[i] = evaluateSingle(audioData[i]);
        }
    }

    /**
     * @brief Dual-channel (Stereo) in-place SIMD audio block processor.
     */
    void processStereoBlockSimd(float* left, float* right, int numSamples) const noexcept
    {
        processBlockSimd(left, numSamples);
        processBlockSimd(right, numSamples);
    }

private:
    std::vector<float> alignedLut;
    float lutScaleFactor { 0.0f };
    int lutSize { 0 };
};

class LutEvaluatorSimd
{
public:
    LutEvaluatorSimd() = default;
    ~LutEvaluatorSimd() = default;

    /**
     * @brief Evaluates 4 polyphonic voices simultaneously via 128-bit SIMD / scalar fallback.
     */
    static inline __m128 evaluateBilinear4Voices(const float* p1_4x,
                                                  const float* p2_4x,
                                                  const AbdBatchedPoint* lut,
                                                  int gridSize,
                                                  LutMetric metric = LutMetric::PrimaryMean) noexcept
    {
#if defined(__AVX2__) || defined(__SSE2__)
        // 1. Load 4 voices normalized coordinates [0.0, 1.0]
        __m128 x_vec = _mm_loadu_ps(p1_4x);
        __m128 y_vec = _mm_loadu_ps(p2_4x);

        // Clamp & scale to grid coordinate space [0.0, gridSize - 1]
        __m128 grid_max = _mm_set1_ps(static_cast<float>(gridSize - 1));
        __m128 zero = _mm_setzero_ps();
        x_vec = _mm_min_ps(_mm_max_ps(_mm_mul_ps(x_vec, grid_max), zero), grid_max);
        y_vec = _mm_min_ps(_mm_max_ps(_mm_mul_ps(y_vec, grid_max), zero), grid_max);

        // 2. Truncate to lower grid indices
        __m128i x_idx = _mm_cvttps_epi32(x_vec);
        __m128i y_idx = _mm_cvttps_epi32(y_vec);

        // Calculate fractional interpolation weights t and u in [0.0, 1.0)
        __m128 x_floor = _mm_cvtepi32_ps(x_idx);
        __m128 y_floor = _mm_cvtepi32_ps(y_idx);
        __m128 t = _mm_sub_ps(x_vec, x_floor);
        __m128 u = _mm_sub_ps(y_vec, y_floor);

        // Store indices to temporary aligned buffer for linear addressing
        alignas(16) int rx[4];
        alignas(16) int ry[4];
        _mm_store_si128(reinterpret_cast<__m128i*>(rx), x_idx);
        _mm_store_si128(reinterpret_cast<__m128i*>(ry), y_idx);

        // 3. Load 4 neighbor corners for all 4 voices (Branchless)
        alignas(16) float f00[4], f10[4], f01[4], f11[4];

        for (int i = 0; i < 4; ++i)
        {
            int idx00 = (ry[i] * gridSize) + rx[i];
            int idx10 = idx00 + (rx[i] < gridSize - 1 ? 1 : 0);
            int idx01 = idx00 + (ry[i] < gridSize - 1 ? gridSize : 0);
            int idx11 = idx01 + (rx[i] < gridSize - 1 ? 1 : 0);

            f00[i] = extractMetric(lut[idx00], metric);
            f10[i] = extractMetric(lut[idx10], metric);
            f01[i] = extractMetric(lut[idx01], metric);
            f11[i] = extractMetric(lut[idx11], metric);
        }

        __m128 v00 = _mm_load_ps(f00);
        __m128 v10 = _mm_load_ps(f10);
        __m128 v01 = _mm_load_ps(f01);
        __m128 v11 = _mm_load_ps(f11);

        // 4. Vectorized bilinear weights: (1-t)*(1-u)*v00 + t*(1-u)*v10 + (1-t)*u*v01 + t*u*v11
        __m128 one = _mm_set1_ps(1.0f);
        __m128 one_minus_t = _mm_sub_ps(one, t);
        __m128 one_minus_u = _mm_sub_ps(one, u);

        __m128 w00 = _mm_mul_ps(one_minus_t, one_minus_u);
        __m128 w10 = _mm_mul_ps(t, one_minus_u);
        __m128 w01 = _mm_mul_ps(one_minus_t, u);
        __m128 w11 = _mm_mul_ps(t, u);

#if defined(__AVX2__) && defined(__FMA__)
        __m128 res = _mm_mul_ps(w00, v00);
        res = _mm_fmadd_ps(w10, v10, res);
        res = _mm_fmadd_ps(w01, v01, res);
        res = _mm_fmadd_ps(w11, v11, res);
        return res;
#else
        __m128 p0 = _mm_mul_ps(w00, v00);
        __m128 p1 = _mm_mul_ps(w10, v10);
        __m128 p2 = _mm_mul_ps(w01, v01);
        __m128 p3 = _mm_mul_ps(w11, v11);
        return _mm_add_ps(_mm_add_ps(p0, p1), _mm_add_ps(p2, p3));
#endif

#else
        return evaluateBilinear4VoicesScalar(p1_4x, p2_4x, lut, gridSize, metric);
#endif
    }

    /**
     * @brief Scalar fallback with exact identical mathematical formulation.
     */
    static inline __m128 evaluateBilinear4VoicesScalar(const float* p1_4x,
                                                        const float* p2_4x,
                                                        const AbdBatchedPoint* lut,
                                                        int gridSize,
                                                        LutMetric metric = LutMetric::PrimaryMean) noexcept
    {
        alignas(16) float result[4];
        float maxCoord = static_cast<float>(gridSize - 1);

        for (int i = 0; i < 4; ++i)
        {
            float x = std::clamp(p1_4x[i] * maxCoord, 0.0f, maxCoord);
            float y = std::clamp(p2_4x[i] * maxCoord, 0.0f, maxCoord);

            int x0 = static_cast<int>(x);
            int y0 = static_cast<int>(y);
            int x1 = std::min(x0 + 1, gridSize - 1);
            int y1 = std::min(y0 + 1, gridSize - 1);

            float t = x - static_cast<float>(x0);
            float u = y - static_cast<float>(y0);

            float v00 = extractMetric(lut[(y0 * gridSize) + x0], metric);
            float v10 = extractMetric(lut[(y0 * gridSize) + x1], metric);
            float v01 = extractMetric(lut[(y1 * gridSize) + x0], metric);
            float v11 = extractMetric(lut[(y1 * gridSize) + x1], metric);

            result[i] = (1.0f - t) * (1.0f - u) * v00
                      + t * (1.0f - u) * v10
                      + (1.0f - t) * u * v01
                      + t * u * v11;
        }

        return _mm_load_ps(result);
    }

    /**
     * @brief Evaluates 8 polyphonic voices simultaneously via 256-bit AVX2 registers.
     */
#if defined(__AVX2__)
    static inline __m256 evaluateBilinear8Voices(const float* p1_8x,
                                                 const float* p2_8x,
                                                 const AbdBatchedPoint* lut,
                                                 int gridSize,
                                                 LutMetric metric = LutMetric::PrimaryMean) noexcept
    {
        __m128 lowRes  = evaluateBilinear4Voices(p1_8x, p2_8x, lut, gridSize, metric);
        __m128 highRes = evaluateBilinear4Voices(p1_8x + 4, p2_8x + 4, lut, gridSize, metric);

        __m256 combined = _mm256_castps128_ps256(lowRes);
        return _mm256_insertf128_ps(combined, highRes, 1);
    }
#endif

private:
    static inline float extractMetric(const AbdBatchedPoint& pt, LutMetric metric) noexcept
    {
        switch (metric)
        {
            case LutMetric::PrimaryMean:     return pt.mu;
            case LutMetric::PrimaryStdDev:   return pt.sigma;
            case LutMetric::SecondaryMean:   return pt.sec_mu;
            case LutMetric::SecondaryStdDev: return pt.sec_sigma;
            case LutMetric::ThdPercent:      return pt.thd_percent;
            default:                         return pt.mu;
        }
    }
};

} // namespace abd::lutdsp