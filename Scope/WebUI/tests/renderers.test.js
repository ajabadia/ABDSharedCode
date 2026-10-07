import { describe, it, expect, beforeEach, vi } from 'vitest';
import { OscilloscopeRenderer } from '../src/renderers/OscilloscopeRenderer.js';
import { SpectrumRenderer } from '../src/renderers/SpectrumRenderer.js';
import { VuMeterRenderer } from '../src/renderers/VuMeterRenderer.js';
import { LissajousRenderer } from '../src/renderers/LissajousRenderer.js';
import { PhaseMeterRenderer } from '../src/renderers/PhaseMeterRenderer.js';
import { SpectrogramRenderer } from '../src/renderers/SpectrogramRenderer.js';
import { createDataFrame } from '../src/frame.js';

function createMockCanvas(w = 300, h = 150) {
  const canvas = document.createElement('canvas');
  Object.defineProperty(canvas, 'clientWidth', { value: w, configurable: true });
  Object.defineProperty(canvas, 'clientHeight', { value: h, configurable: true });
  const ctx = {
    fillStyle: '',
    strokeStyle: '',
    lineWidth: 1,
    font: '',
    textAlign: '',
    shadowBlur: 0,
    shadowColor: '',
    setTransform: vi.fn(),
    scale: vi.fn(),
    clearRect: vi.fn(),
    fillRect: vi.fn(),
    strokeRect: vi.fn(),
    fillText: vi.fn(),
    beginPath: vi.fn(),
    closePath: vi.fn(),
    moveTo: vi.fn(),
    lineTo: vi.fn(),
    arc: vi.fn(),
    stroke: vi.fn(),
    fill: vi.fn(),
    save: vi.fn(),
    restore: vi.fn(),
    drawImage: vi.fn(),
    createImageData: vi.fn((width, height) => ({ data: new Uint8ClampedArray(width * height * 4) })),
    putImageData: vi.fn(),
    createLinearGradient: vi.fn(() => ({ addColorStop: vi.fn() }))
  };
  canvas.getContext = vi.fn(() => ctx);
  return canvas;
}

describe('Renderers: OscilloscopeRenderer', () => {
  let canvas;
  let renderer;

  beforeEach(() => {
    canvas = createMockCanvas(300, 150);
    renderer = new OscilloscopeRenderer();
    renderer.init(canvas, { width: 300, height: 150 });
  });

  it('should initialize and resize canvas backing store with HiDPI scale', () => {
    renderer.resize(400, 200, 2);
    expect(canvas.width).toBe(800);
    expect(canvas.height).toBe(400);
    expect(canvas.style.width).toBe('400px');
    expect(canvas.style.height).toBe('200px');
  });

  it('should render standard audio frame without throwing', () => {
    const frame = createDataFrame({
      timeDataL: new Float32Array(512).fill(0.5),
      timeDataR: new Float32Array(512).fill(-0.5)
    });

    expect(() => renderer.render(frame)).not.toThrow();
    expect(renderer.ctx.stroke).toHaveBeenCalled();
  });

  it('should render control signals (CV/LFO) with adapted scale', () => {
    const frame = createDataFrame({
      timeDataL: new Float32Array(512).fill(0.8),
      signalType: 'control'
    });

    expect(() => renderer.render(frame, { traceCv: '#ffaa00' })).not.toThrow();
    expect(renderer.ctx.stroke).toHaveBeenCalled();
  });

  it('should clean up on destroy()', () => {
    renderer.destroy();
    expect(renderer.isDestroyed).toBe(true);
    expect(renderer.canvas).toBeNull();
  });
});

describe('Renderers: SpectrumRenderer', () => {
  let canvas;
  let renderer;

  beforeEach(() => {
    canvas = createMockCanvas(300, 150);
    renderer = new SpectrumRenderer();
    renderer.init(canvas, { width: 300, height: 150 });
  });

  it('should render logarithmic spectrum with peak hold', () => {
    const spectrum = new Float32Array(512).fill(-20.0);
    spectrum[40] = -6.0;

    const frame = createDataFrame({
      timeDataL: new Float32Array(512),
      spectrumDb: spectrum,
      sampleRate: 44100
    });

    expect(() => renderer.render(frame)).not.toThrow();
    expect(renderer.peakHoldBuffer).not.toBeNull();
    expect(renderer.peakHoldBuffer.length).toBe(300);
    expect(renderer.ctx.fill).toHaveBeenCalled();
  });

  it('should clean up buffers on destroy()', () => {
    renderer.destroy();
    expect(renderer.isDestroyed).toBe(true);
    expect(renderer.peakHoldBuffer).toBeNull();
  });
});

describe('Renderers: LissajousRenderer', () => {
  let canvas;
  let renderer;

  beforeEach(() => {
    canvas = createMockCanvas(300, 150);
    renderer = new LissajousRenderer();
    renderer.init(canvas, { width: 300, height: 150 });
  });

  it('should render rotated 45 M/S vectorscope trace', () => {
    const frame = createDataFrame({
      timeDataL: new Float32Array(512).fill(0.5),
      timeDataR: new Float32Array(512).fill(-0.5)
    });

    expect(() => renderer.render(frame)).not.toThrow();
    expect(renderer.ctx.stroke).toHaveBeenCalled();
  });

  it('should clean up on destroy()', () => {
    renderer.destroy();
    expect(renderer.isDestroyed).toBe(true);
  });
});

describe('Renderers: PhaseMeterRenderer', () => {
  let canvas;
  let renderer;

  beforeEach(() => {
    canvas = createMockCanvas(300, 150);
    renderer = new PhaseMeterRenderer();
    renderer.init(canvas, { width: 300, height: 150 });
  });

  it('should render phase correlation meter and smooth damping', () => {
    const frame = createDataFrame({
      timeDataL: new Float32Array(512),
      phaseCorrelation: 0.85
    });

    expect(() => renderer.render(frame)).not.toThrow();
    expect(renderer.smoothedCorr).toBeCloseTo(0.97, 1);
    expect(renderer.ctx.fillText).toHaveBeenCalled();
  });
});

describe('Renderers: SpectrogramRenderer', () => {
  let canvas;
  let renderer;

  beforeEach(() => {
    canvas = createMockCanvas(300, 150);
    renderer = new SpectrogramRenderer();
    renderer.init(canvas, { width: 300, height: 150 });
    // Mock offscreen context
    renderer.offscreenCtx = {
      drawImage: vi.fn(),
      createImageData: vi.fn((w, h) => ({ data: new Uint8ClampedArray(w * h * 4) })),
      putImageData: vi.fn(),
      fillStyle: '',
      fillRect: vi.fn()
    };
  });

  it('should scroll and render spectrogram cascade', () => {
    const spectrum = new Float32Array(256).fill(-30.0);
    const frame = createDataFrame({
      timeDataL: new Float32Array(512),
      spectrumDb: spectrum,
      sampleRate: 44100
    });

    expect(() => renderer.render(frame)).not.toThrow();
    expect(renderer.ctx.drawImage).toHaveBeenCalled();
  });

  it('should clean offscreen buffer on destroy()', () => {
    renderer.destroy();
    expect(renderer.isDestroyed).toBe(true);
    expect(renderer.offscreenCanvas).toBeNull();
  });
});

describe('Renderers: VuMeterRenderer', () => {
  let container;
  let vuMeter;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    vuMeter = new VuMeterRenderer(container);
    if (vuMeter.canvas) {
      const mockCtx = {
        clearRect: vi.fn(),
        fillRect: vi.fn(),
        setTransform: vi.fn(),
        scale: vi.fn(),
        createLinearGradient: vi.fn(() => ({ addColorStop: vi.fn() }))
      };
      vuMeter.ctx = mockCtx;
    }
  });

  it('should create canvas inside container and render stereo VU levels', () => {
    const frame = createDataFrame({
      timeDataL: new Float32Array(512),
      rmsL: 0.7,
      rmsR: 0.5,
      peakL: 0.9,
      peakR: 0.8
    });

    expect(() => vuMeter.render(frame)).not.toThrow();
    expect(vuMeter.peakL).toBe(0.9);
    expect(vuMeter.peakR).toBe(0.8);
    expect(vuMeter.ctx.fillRect).toHaveBeenCalled();
  });

  it('should clean up and remove canvas on destroy()', () => {
    vuMeter.destroy();
    expect(vuMeter.isDestroyed).toBe(true);
    expect(container.querySelector('.abd-scope-vu-canvas')).toBeNull();
  });
});
