/**
 * ABDScope Data Frame Engine
 * ==========================
 * Normalizes incoming audio/telemetry packets into structured ScopeDataFrame contracts.
 * Computes RMS, Peak, and stereo Phase Correlation metrics.
 *
 * Constraints:
 * - Pure mathematical transformations, zero DOM dependencies.
 * - Under 200 lines of code (Single Responsibility Principle).
 */

import { processTrigger } from './trigger.js';

/**
 * Calculate Root-Mean-Square (RMS) amplitude of a buffer segment.
 * @param {Float32Array} buffer - Sample buffer
 * @param {number} [start=0] - Start sample index
 * @param {number} [length] - Number of samples to process
 * @returns {number} RMS amplitude (0.0 to 1.0)
 */
export function calculateRms(buffer, start = 0, length = null) {
  if (!buffer || buffer.length === 0) return 0.0;
  const len = length !== null ? Math.min(length, buffer.length - start) : buffer.length - start;
  if (len <= 0) return 0.0;

  let sumSquares = 0.0;
  const end = start + len;
  for (let i = start; i < end; ++i) {
    const s = buffer[i];
    sumSquares += s * s;
  }

  return Math.sqrt(sumSquares / len);
}

/**
 * Calculate absolute peak amplitude of a buffer segment.
 * @param {Float32Array} buffer - Sample buffer
 * @param {number} [start=0] - Start sample index
 * @param {number} [length] - Number of samples to process
 * @returns {number} Peak amplitude (0.0 to 1.0+)
 */
export function calculatePeak(buffer, start = 0, length = null) {
  if (!buffer || buffer.length === 0) return 0.0;
  const len = length !== null ? Math.min(length, buffer.length - start) : buffer.length - start;
  if (len <= 0) return 0.0;

  let max = 0.0;
  const end = start + len;
  for (let i = start; i < end; ++i) {
    const abs = Math.abs(buffer[i]);
    if (abs > max) max = abs;
  }

  return max;
}

/**
 * Compute stereo phase correlation coefficient between Left and Right channels.
 * Formula: sum(L * R) / sqrt(sum(L^2) * sum(R^2))
 * @param {Float32Array} bufferL - Left channel samples
 * @param {Float32Array} bufferR - Right channel samples
 * @param {number} [length] - Number of samples to process
 * @returns {number} Correlation coefficient [-1.0 (Anti-phase), 0.0 (Wide Stereo), +1.0 (Mono)]
 */
export function calculatePhaseCorrelation(bufferL, bufferR, length = null) {
  if (!bufferL || !bufferR) return 1.0;
  const len = length !== null
    ? Math.min(length, bufferL.length, bufferR.length)
    : Math.min(bufferL.length, bufferR.length);

  if (len <= 0) return 1.0;

  let sumLR = 0.0;
  let sumL2 = 0.0;
  let sumR2 = 0.0;

  for (let i = 0; i < len; ++i) {
    const l = bufferL[i];
    const r = bufferR[i];
    sumLR += l * r;
    sumL2 += l * l;
    sumR2 += r * r;
  }

  const denominator = Math.sqrt(sumL2 * sumR2);
  if (denominator < 0.000001) return 1.0; // Silence considered correlated

  const coeff = sumLR / denominator;
  return Math.max(-1.0, Math.min(1.0, coeff));
}

/**
 * Build and normalize a complete ScopeDataFrame from raw inputs.
 * @param {Object} raw - Raw input properties
 * @param {Float32Array} raw.timeDataL - Left or Mono time domain samples
 * @param {Float32Array} [raw.timeDataR] - Right time domain samples (optional)
 * @param {Float32Array} [raw.spectrumDb] - FFT frequency magnitudes in dB (optional)
 * @param {string} [raw.signalType='audio'] - 'audio' | 'control'
 * @param {number} [raw.sampleRate=44100] - Audio sample rate in Hz
 * @param {Object} [options] - Normalization and trigger options
 * @returns {Object} Normalized ScopeDataFrame
 */
export function createDataFrame(raw = {}, options = {}) {
  if (raw && raw.taps && typeof raw.taps === 'object') {
    const normalizedTaps = {};
    for (const [key, tapPacket] of Object.entries(raw.taps)) {
      normalizedTaps[key] = createDataFrame(tapPacket, options);
    }
    return { taps: normalizedTaps };
  }
  const safeRaw = raw || {};
  const safeOpts = options || {};
  const signalType = safeRaw.signalType ?? (safeOpts.signalType === 'control' ? 'control' : 'audio');
  const sampleRate = safeRaw.sampleRate ?? 44100;

  const timeDataL = (raw.timeDataL instanceof Float32Array)
    ? raw.timeDataL
    : (Array.isArray(raw.timeDataL) ? new Float32Array(raw.timeDataL) : new Float32Array(0));
  const timeDataR = (raw.timeDataR instanceof Float32Array)
    ? raw.timeDataR
    : (Array.isArray(raw.timeDataR) ? new Float32Array(raw.timeDataR) : null);
  const numSamples = timeDataL.length;

  let spectrumDb = (raw.spectrumDb instanceof Float32Array)
    ? raw.spectrumDb
    : (Array.isArray(raw.spectrumDb) ? new Float32Array(raw.spectrumDb) : null);

  if (!spectrumDb && timeDataL.length >= 512 && signalType !== 'control') {
    spectrumDb = computeSpectrumDb(timeDataL, sampleRate);
  }
  const spectrumBins = spectrumDb ? spectrumDb.length : 0;

  // Trigger and Frequency Estimation
  const trigger = processTrigger(timeDataL, sampleRate, { ...options, signalType });

  // Amplitude and Phase Metrics
  const rmsL = raw.rmsL ?? calculateRms(timeDataL);
  const rmsR = raw.rmsR ?? (timeDataR ? calculateRms(timeDataR) : rmsL);
  const peakL = raw.peakL ?? calculatePeak(timeDataL);
  const peakR = raw.peakR ?? (timeDataR ? calculatePeak(timeDataR) : peakL);

  const phaseCorrelation = raw.phaseCorrelation ?? (
    timeDataR ? calculatePhaseCorrelation(timeDataL, timeDataR) : 1.0
  );

  return {
    signalType,
    timeDataL,
    timeDataR,
    numSamples,
    sampleRate,
    triggerIndex: trigger.triggerIndex,
    triggerFraction: trigger.triggerFraction ?? 0.0,
    estimatedFrequencyHz: trigger.estimatedFrequencyHz,
    detectedNoteName: trigger.detectedNoteName,
    spectrumDb,
    spectrumBins,
    rmsL,
    rmsR,
    peakL,
    peakR,
    phaseCorrelation
  };
}

/**
 * Fast Radix-2 Real FFT fallback to compute spectrum magnitudes in dBfs when raw.spectrumDb is omitted.
 * @param {Float32Array} timeData - Input PCM buffer
 * @param {number} sampleRate - Sample rate in Hz
 * @returns {Float32Array|null}
 */
export function computeSpectrumDb(timeData, sampleRate = 44100) {
  const n = 512;
  if (!timeData || timeData.length < n) return null;

  const real = new Float32Array(n);
  const imag = new Float32Array(n);

  for (let i = 0; i < n; ++i) {
    const w = 0.5 * (1.0 - Math.cos((2.0 * Math.PI * i) / (n - 1)));
    real[i] = timeData[i] * w;
  }

  let j = 0;
  for (let i = 0; i < n - 1; ++i) {
    if (i < j) {
      const tr = real[i]; real[i] = real[j]; real[j] = tr;
      const ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
    }
    let k = n >> 1;
    while (k <= j) {
      j -= k;
      k >>= 1;
    }
    j += k;
  }

  for (let len = 2; len <= n; len <<= 1) {
    const half = len >> 1;
    const angle = (-2.0 * Math.PI) / len;
    const wStepR = Math.cos(angle);
    const wStepI = Math.sin(angle);
    for (let i = 0; i < n; i += len) {
      let wr = 1.0;
      let wi = 0.0;
      for (let k = 0; k < half; ++k) {
        const uR = real[i + k];
        const uI = imag[i + k];
        const vR = real[i + k + half] * wr - imag[i + k + half] * wi;
        const vI = real[i + k + half] * wi + imag[i + k + half] * wr;
        real[i + k] = uR + vR;
        imag[i + k] = uI + vI;
        real[i + k + half] = uR - vR;
        imag[i + k + half] = uI - vI;
        const nextWr = wr * wStepR - wi * wStepI;
        wi = wr * wStepI + wi * wStepR;
        wr = nextWr;
      }
    }
  }

  const bins = n >> 1;
  const specDb = new Float32Array(bins);
  const norm = 2.0 / n;
  for (let i = 0; i < bins; ++i) {
    const mag = Math.sqrt(real[i] * real[i] + imag[i] * imag[i]) * norm;
    specDb[i] = mag > 1e-5 ? Math.max(-96.0, 20.0 * Math.log10(mag)) : -96.0;
  }
  return specDb;
}