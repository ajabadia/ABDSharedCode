import { describe, it, expect } from 'vitest';
import {
  calculateRms,
  calculatePeak,
  calculatePhaseCorrelation,
  createDataFrame
} from '../src/frame.js';

describe('Frame Engine: calculateRms & calculatePeak', () => {
  it('should compute theoretical RMS for full-scale sine wave (~0.7071)', () => {
    const numSamples = 2048;
    const buffer = new Float32Array(numSamples);
    for (let i = 0; i < numSamples; ++i) {
      buffer[i] = 1.0 * Math.sin(2 * Math.PI * 440 * (i / 44100));
    }

    const rms = calculateRms(buffer);
    expect(rms).toBeCloseTo(0.7071, 2);
  });

  it('should compute RMS = 1.0 for unit square wave', () => {
    const buffer = new Float32Array(512);
    for (let i = 0; i < 512; ++i) {
      buffer[i] = i % 2 === 0 ? 1.0 : -1.0;
    }
    expect(calculateRms(buffer)).toBeCloseTo(1.0, 4);
    expect(calculatePeak(buffer)).toBe(1.0);
  });

  it('should compute absolute peak correctly for transient signals', () => {
    const buffer = new Float32Array(128).fill(0.1);
    buffer[42] = -0.85;
    expect(calculatePeak(buffer)).toBeCloseTo(0.85, 4);
  });

  it('should return 0.0 for empty or invalid buffer', () => {
    expect(calculateRms(new Float32Array(0))).toBe(0.0);
    expect(calculatePeak(null)).toBe(0.0);
  });
});

describe('Frame Engine: calculatePhaseCorrelation', () => {
  it('should return +1.0 for identical in-phase stereo channels (Mono)', () => {
    const bufferL = new Float32Array(512);
    const bufferR = new Float32Array(512);
    for (let i = 0; i < 512; ++i) {
      const val = Math.sin(i * 0.1);
      bufferL[i] = val;
      bufferR[i] = val;
    }

    expect(calculatePhaseCorrelation(bufferL, bufferR)).toBeCloseTo(1.0, 4);
  });

  it('should return -1.0 for inverted 180-degree out-of-phase channels', () => {
    const bufferL = new Float32Array(512);
    const bufferR = new Float32Array(512);
    for (let i = 0; i < 512; ++i) {
      const val = Math.sin(i * 0.1);
      bufferL[i] = val;
      bufferR[i] = -val; // inverted
    }

    expect(calculatePhaseCorrelation(bufferL, bufferR)).toBeCloseTo(-1.0, 4);
  });

  it('should return ~0.0 for orthogonal/quadrature stereo signals (Sine vs Cosine)', () => {
    const bufferL = new Float32Array(2048);
    const bufferR = new Float32Array(2048);
    for (let i = 0; i < 2048; ++i) {
      bufferL[i] = Math.sin(2 * Math.PI * 10 * (i / 2048));
      bufferR[i] = Math.cos(2 * Math.PI * 10 * (i / 2048));
    }

    expect(calculatePhaseCorrelation(bufferL, bufferR)).toBeCloseTo(0.0, 2);
  });
});

describe('Frame Engine: createDataFrame Normalization', () => {
  it('should build a complete ScopeDataFrame with computed metrics', () => {
    const bufferL = new Float32Array(1024);
    const bufferR = new Float32Array(1024);
    for (let i = 0; i < 1024; ++i) {
      bufferL[i] = 0.8 * Math.sin(2 * Math.PI * 440 * (i / 44100));
      bufferR[i] = 0.6 * Math.sin(2 * Math.PI * 440 * (i / 44100));
    }

    const frame = createDataFrame({
      timeDataL: bufferL,
      timeDataR: bufferR,
      sampleRate: 44100
    });

    expect(frame.signalType).toBe('audio');
    expect(frame.numSamples).toBe(1024);
    expect(frame.sampleRate).toBe(44100);
    expect(frame.triggerIndex).toBeGreaterThan(0);
    expect(frame.estimatedFrequencyHz).toBeCloseTo(440, -1);
    expect(frame.detectedNoteName).toBe('A4');
    expect(frame.rmsL).toBeCloseTo(0.8 * 0.7071, 2);
    expect(frame.rmsR).toBeCloseTo(0.6 * 0.7071, 2);
    expect(frame.peakL).toBeCloseTo(0.8, 2);
    expect(frame.peakR).toBeCloseTo(0.6, 2);
    expect(frame.phaseCorrelation).toBeCloseTo(1.0, 2);
  });

  it('should handle control signal packet (CV/LFO) with trigger bypassed', () => {
    const buffer = new Float32Array(512).fill(0.75);
    const frame = createDataFrame({
      timeDataL: buffer,
      signalType: 'control'
    });

    expect(frame.signalType).toBe('control');
    expect(frame.triggerIndex).toBe(0);
    expect(frame.estimatedFrequencyHz).toBe(0);
    expect(frame.detectedNoteName).toBe('');
    expect(frame.peakL).toBe(0.75);
    expect(frame.phaseCorrelation).toBe(1.0);
  });

  it('should use pre-calculated bridge metrics when provided in raw input', () => {
    const frame = createDataFrame({
      timeDataL: new Float32Array(128),
      rmsL: 0.42,
      rmsR: 0.38,
      peakL: 0.95,
      peakR: 0.88,
      phaseCorrelation: 0.73
    });

    expect(frame.rmsL).toBe(0.42);
    expect(frame.rmsR).toBe(0.38);
    expect(frame.peakL).toBe(0.95);
    expect(frame.peakR).toBe(0.88);
    expect(frame.phaseCorrelation).toBe(0.73);
  });
});
