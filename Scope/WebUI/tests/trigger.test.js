import { describe, it, expect } from 'vitest';
import {
  findZeroCrossing,
  estimateFundamentalFrequency,
  frequencyToNoteName,
  processTrigger
} from '../src/trigger.js';

// Helper to generate synthetic audio buffers
function generateSineWave(freqHz, sampleRate, numSamples, phaseOffset = 0, amplitude = 0.9) {
  const buffer = new Float32Array(numSamples);
  for (let i = 0; i < numSamples; ++i) {
    buffer[i] = amplitude * Math.sin(2 * Math.PI * freqHz * (i / sampleRate) + phaseOffset);
  }
  return buffer;
}

function generateSawWave(freqHz, sampleRate, numSamples) {
  const buffer = new Float32Array(numSamples);
  const period = sampleRate / freqHz;
  for (let i = 0; i < numSamples; ++i) {
    buffer[i] = 2.0 * ((i % period) / period) - 1.0;
  }
  return buffer;
}

function generateSquareWave(freqHz, sampleRate, numSamples) {
  const buffer = new Float32Array(numSamples);
  const period = sampleRate / freqHz;
  for (let i = 0; i < numSamples; ++i) {
    buffer[i] = (i % period) < (period / 2) ? 0.8 : -0.8;
  }
  return buffer;
}

describe('Trigger Engine: findZeroCrossing', () => {
  it('should find positive zero crossing on a clean sine wave with phase offset', () => {
    const sampleRate = 44100;
    const freq = 440;
    // Start at negative peak (phase = -PI/2), so first positive crossing is at 1/4 cycle
    const buffer = generateSineWave(freq, sampleRate, 1024, -Math.PI / 2);
    const expectedCrossing = sampleRate / freq / 4; // ~25 samples

    const triggerIdx = findZeroCrossing(buffer, { hysteresis: 0.02 });
    expect(triggerIdx).toBeGreaterThan(0);
    expect(Math.abs(triggerIdx - expectedCrossing)).toBeLessThan(1.5);
  });

  it('should reject minor noise around zero due to hysteresis band', () => {
    const sampleRate = 44100;
    const buffer = generateSineWave(440, sampleRate, 1024, -Math.PI / 2);

    // Add noise near zero that is within hysteresis threshold (+/- 0.02)
    for (let i = 0; i < 20; ++i) {
      buffer[i] = (Math.random() - 0.5) * 0.015; // small ripple
    }
    // Deep negative dip to arm
    buffer[20] = -0.5;
    buffer[21] = -0.6;
    buffer[22] = -0.2;
    buffer[23] = 0.3; // cross!

    const triggerIdx = findZeroCrossing(buffer, { hysteresis: 0.05, searchStart: 15 });
    expect(triggerIdx).toBeGreaterThanOrEqual(22);
    expect(triggerIdx).toBeLessThanOrEqual(24);
  });

  it('should return 0 for silence or empty buffer', () => {
    expect(findZeroCrossing(new Float32Array(512))).toBe(0);
    expect(findZeroCrossing(new Float32Array(0))).toBe(0);
    expect(findZeroCrossing(null)).toBe(0);
  });
});

describe('Trigger Engine: estimateFundamentalFrequency', () => {
  it('should accurately estimate 440 Hz for A4 sine wave', () => {
    const buffer = generateSineWave(440, 44100, 2048);
    const freq = estimateFundamentalFrequency(buffer, 44100);
    expect(freq).toBeCloseTo(440, -0.5); // within ~2 Hz
  });

  it('should accurately estimate 261.6 Hz for Middle C (C4)', () => {
    const buffer = generateSineWave(261.63, 44100, 2048);
    const freq = estimateFundamentalFrequency(buffer, 44100);
    expect(freq).toBeCloseTo(261.6, -0.5);
  });

  it('should estimate frequency for sawtooth and square waves', () => {
    const sawBuffer = generateSawWave(500, 44100, 2048);
    const sawFreq = estimateFundamentalFrequency(sawBuffer, 44100);
    expect(sawFreq).toBeCloseTo(500, -0.5);

    const sqBuffer = generateSquareWave(300, 44100, 2048);
    const sqFreq = estimateFundamentalFrequency(sqBuffer, 44100);
    expect(sqFreq).toBeCloseTo(300, -0.5);
  });

  it('should accurately track sub-bass frequencies (< 140 Hz down to 30 Hz)', () => {
    // 55 Hz = A1 bass note
    const bufferA1 = generateSineWave(55.0, 44100, 4096);
    const freqA1 = estimateFundamentalFrequency(bufferA1, 44100);
    expect(freqA1).toBeCloseTo(55.0, 0);
    expect(frequencyToNoteName(freqA1)).toBe('A1');

    // 65.4 Hz = C2 bass note
    const bufferC2 = generateSawWave(65.4, 44100, 4096);
    const freqC2 = estimateFundamentalFrequency(bufferC2, 44100);
    expect(freqC2).toBeCloseTo(65.4, 0);
    expect(frequencyToNoteName(freqC2)).toBe('C2');

    // 30.0 Hz sub-bass
    const buffer30 = generateSineWave(30.0, 44100, 4096);
    const freq30 = estimateFundamentalFrequency(buffer30, 44100);
    expect(freq30).toBeCloseTo(30.0, 0);
  });

  it('should return 0 for DC offset or silence', () => {
    const dcBuffer = new Float32Array(1024).fill(0.5);
    expect(estimateFundamentalFrequency(dcBuffer, 44100)).toBe(0);
  });
});

describe('Trigger Engine: frequencyToNoteName', () => {
  it('should identify standard reference pitches correctly', () => {
    expect(frequencyToNoteName(440.0)).toBe('A4');
    expect(frequencyToNoteName(261.63)).toBe('C4');
    expect(frequencyToNoteName(82.41)).toBe('E2');
    expect(frequencyToNoteName(110.0)).toBe('A2');
    expect(frequencyToNoteName(880.0)).toBe('A5');
  });

  it('should return empty string for out-of-range or invalid frequencies', () => {
    expect(frequencyToNoteName(0)).toBe('');
    expect(frequencyToNoteName(-100)).toBe('');
    expect(frequencyToNoteName(50000)).toBe('');
  });
});

describe('Trigger Engine: processTrigger Orchestrator', () => {
  it('should process audio signal and return complete metadata', () => {
    const buffer = generateSineWave(440, 44100, 1024, -Math.PI / 2);
    const result = processTrigger(buffer, 44100, { signalType: 'audio' });

    expect(result.triggerIndex).toBeGreaterThan(0);
    expect(result.estimatedFrequencyHz).toBeCloseTo(440, -1);
    expect(result.detectedNoteName).toBe('A4');
  });

  it('should bypass trigger and frequency estimation for control signals (LFO/CV)', () => {
    const buffer = generateSineWave(5, 44100, 1024); // 5 Hz LFO
    const result = processTrigger(buffer, 44100, { signalType: 'control' });

    expect(result.triggerIndex).toBe(0);
    expect(result.estimatedFrequencyHz).toBe(0);
    expect(result.detectedNoteName).toBe('');
  });
});
