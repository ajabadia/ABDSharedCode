import { describe, it, expect, vi } from 'vitest';
import { AnalyserInput } from '../src/input/AnalyserInput.js';
import { PushInput } from '../src/input/PushInput.js';

describe('Input Adapters: PushInput', () => {
  it('should accept external raw packet and deliver normalized ScopeDataFrame to listener', () => {
    const input = new PushInput();
    const mockCallback = vi.fn();

    input.start(mockCallback);

    const rawPacket = {
      timeDataL: new Float32Array([0.1, 0.2, 0.3]),
      sampleRate: 48000,
      signalType: 'audio'
    };

    const frame = input.push(rawPacket);

    expect(mockCallback).toHaveBeenCalledTimes(1);
    expect(mockCallback).toHaveBeenCalledWith(frame);
    expect(frame.numSamples).toBe(3);
    expect(frame.sampleRate).toBe(48000);
    expect(input.lastFrame).toBe(frame);
  });

  it('should cleanly unregister callback on destroy()', () => {
    const input = new PushInput();
    const mockCallback = vi.fn();

    input.start(mockCallback);
    input.destroy();

    input.push({ timeDataL: new Float32Array(10) });
    expect(mockCallback).not.toHaveBeenCalled();
    expect(input.lastFrame).toBeNull();
  });
});

describe('Input Adapters: AnalyserInput', () => {
  function createMockAnalyserNode() {
    return {
      fftSize: 512,
      frequencyBinCount: 256,
      context: { sampleRate: 44100 },
      getFloatTimeDomainData: vi.fn((buf) => {
        for (let i = 0; i < buf.length; ++i) buf[i] = Math.sin(i * 0.1);
      }),
      getFloatFrequencyData: vi.fn((buf) => {
        buf.fill(-30.0);
      })
    };
  }

  it('should sample mock AnalyserNode and generate valid ScopeDataFrame', () => {
    const mockNode = createMockAnalyserNode();
    const input = new AnalyserInput(mockNode);

    const frame = input.sample();

    expect(mockNode.getFloatTimeDomainData).toHaveBeenCalledTimes(1);
    expect(mockNode.getFloatFrequencyData).toHaveBeenCalledTimes(1);
    expect(frame.numSamples).toBe(512);
    expect(frame.spectrumBins).toBe(256);
    expect(frame.spectrumDb[0]).toBe(-30.0);
  });

  it('should throw error if initialized without valid AnalyserNode', () => {
    expect(() => new AnalyserInput(null)).toThrow();
  });

  it('should stop sampling loop and clean references on destroy()', () => {
    const mockNode = createMockAnalyserNode();
    const input = new AnalyserInput(mockNode);
    expect(input.analyserL).toBeDefined();

    input.destroy();
    expect(input.isRunning).toBe(false);
    expect(input.analyserL).toBeNull();
    expect(input.analyserR).toBeNull();
    expect(input.timeBufferL).toBeNull();
    expect(input.timeBufferR).toBeNull();
    expect(input.freqBuffer).toBeNull();
    expect(input.onFrameCallback).toBeNull();
  });
});
