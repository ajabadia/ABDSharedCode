/**
 * ABDScope Trigger Engine
 * =======================
 * Pure mathematical algorithms for zero-crossing detection with adaptive hysteresis,
 * sub-sample interpolation, sub-bass fundamental frequency tracking (20 Hz - 20 kHz),
 * and MIDI note identification.
 *
 * Constraints:
 * - Pure functions, zero DOM dependencies, zero allocations in steady loops.
 * - Under 200 lines of code (Single Responsibility Principle).
 */

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

/**
 * Detect zero-crossing index with hysteresis and sub-sample precision.
 * Supports deep sub-bass (< 140 Hz down to 20 Hz) via peak-scaled hysteresis.
 * @param {Float32Array} buffer - PCM audio samples (-1.0 to 1.0)
 * @param {Object} [options] - Detection configuration
 * @returns {number} Fractional sample index where positive zero-crossing occurs, or 0
 */
export function findZeroCrossing(buffer, options = {}) {
  const len = buffer?.length ?? 0;
  if (len < 4) return 0;

  // Compute peak amplitude for adaptive hysteresis if not explicitly provided
  let peak = 0.0;
  const scanLen = Math.min(len, 1024);
  for (let i = 0; i < scanLen; ++i) {
    const abs = Math.abs(buffer[i]);
    if (abs > peak) peak = abs;
  }

  const triggerLevel = options.triggerLevel ?? 0.0;
  const defaultHys = Math.max(0.005, Math.min(0.15, peak * 0.12));
  const hysteresis = Math.max(0.001, options.hysteresis ?? defaultHys);
  const armThreshold = triggerLevel - hysteresis;
  const fireThreshold = triggerLevel + hysteresis;

  const start = Math.max(1, options.searchStart ?? 1);
  const end = Math.min(len - 1, options.searchEnd ?? Math.floor(len * 0.92));

  let isArmed = false;

  for (let i = start; i < end; ++i) {
    const sample = buffer[i];

    if (!isArmed) {
      if (sample < armThreshold) {
        isArmed = true;
      }
    } else {
      if (sample >= fireThreshold) {
        const prev = buffer[i - 1];
        const denom = sample - prev;
        const frac = denom > 0.00001 ? (triggerLevel - prev) / denom : 0.0;
        return (i - 1) + Math.max(0.0, Math.min(1.0, frac));
      }
    }
  }

  return 0;
}

/**
 * Estimate fundamental frequency (Hz) with sub-bass robustness (20 Hz - 20 kHz).
 * Uses multi-period averaging across extended search window (up to 4096 samples).
 * @param {Float32Array} buffer - PCM audio samples
 * @param {number} [sampleRate=44100] - Audio sample rate in Hz
 * @param {Object} [options] - Configuration
 * @returns {number} Estimated frequency in Hz (0 if silence or undetectable)
 */
export function estimateFundamentalFrequency(buffer, sampleRate = 44100, options = {}) {
  const len = buffer?.length ?? 0;
  if (len < 32 || sampleRate <= 0) return 0;

  // Compute signal peak to adapt hysteresis dynamically
  let peak = 0.0;
  const scanLimit = Math.min(len, 2048);
  for (let i = 0; i < scanLimit; ++i) {
    const abs = Math.abs(buffer[i]);
    if (abs > peak) peak = abs;
  }
  if (peak < 0.005) return 0; // Silence

  const defaultHys = Math.max(0.005, Math.min(0.18, peak * 0.15));
  const hysteresis = options.hysteresis ?? defaultHys;
  const armThreshold = -hysteresis;
  const fireThreshold = hysteresis;

  const crossings = [];
  let isArmed = false;
  const maxSearch = Math.min(len - 1, 4096);

  for (let i = 1; i < maxSearch && crossings.length < 24; ++i) {
    const sample = buffer[i];

    if (!isArmed) {
      if (sample < armThreshold) isArmed = true;
    } else {
      if (sample >= fireThreshold) {
        const prev = buffer[i - 1];
        const denom = sample - prev;
        const frac = denom > 0.00001 ? (0.0 - prev) / denom : 0.0;
        crossings.push((i - 1) + Math.max(0.0, Math.min(1.0, frac)));
        isArmed = false;
      }
    }
  }

  if (crossings.length < 2) return 0;

  // Calculate period deltas
  let totalPeriod = 0;
  const numPeriods = crossings.length - 1;
  for (let i = 0; i < numPeriods; ++i) {
    totalPeriod += (crossings[i + 1] - crossings[i]);
  }

  const avgPeriodSamples = totalPeriod / numPeriods;
  if (avgPeriodSamples <= 1.0) return 0;

  const freq = sampleRate / avgPeriodSamples;
  return (freq >= 18.0 && freq <= 22000.0) ? Math.round(freq * 10) / 10 : 0;
}

/**
 * Convert frequency in Hz to nearest MIDI note name with octave (e.g. 440 Hz -> "A4", 55 Hz -> "A1").
 * @param {number} freqHz - Frequency in Hz
 * @returns {string} Formatted note name (e.g. "A4", "C#3", "A1") or empty string if invalid
 */
export function frequencyToNoteName(freqHz) {
  if (freqHz < 16.0 || freqHz > 20000.0) return '';

  const midiNum = 69 + 12 * Math.log2(freqHz / 440.0);
  const roundedMidi = Math.round(midiNum);
  if (roundedMidi < 0 || roundedMidi > 127) return '';

  const noteIdx = ((roundedMidi % 12) + 12) % 12;
  const octave = Math.floor(roundedMidi / 12) - 1;

  return `${NOTE_NAMES[noteIdx]}${octave}`;
}

/**
 * High-level trigger processor for a single audio frame.
 * @param {Float32Array} buffer - PCM audio buffer
 * @param {number} [sampleRate=44100] - Audio sample rate in Hz
 * @param {Object} [options] - Options (signalType, hysteresis, etc.)
 * @returns {{ triggerIndex: number, triggerFraction: number, estimatedFrequencyHz: number, detectedNoteName: string }}
 */
export function processTrigger(buffer, sampleRate = 44100, options = {}) {
  if (options.signalType === 'control' || !buffer || buffer.length === 0) {
    return { triggerIndex: 0, triggerFraction: 0, estimatedFrequencyHz: 0, detectedNoteName: '' };
  }

  const rawCrossing = findZeroCrossing(buffer, options);
  const triggerIndex = Math.floor(rawCrossing);
  const triggerFraction = rawCrossing - triggerIndex;
  const estimatedFrequencyHz = estimateFundamentalFrequency(buffer, sampleRate, options);
  const detectedNoteName = frequencyToNoteName(estimatedFrequencyHz);

  return {
    triggerIndex,
    triggerFraction,
    estimatedFrequencyHz,
    detectedNoteName
  };
}
