/**
 * ABDScope Logarithmic Spectrum FFT Renderer
 * ===========================================
 * Studio-grade audio spectrum analyzer (20 Hz to 20 kHz) with logarithmic frequency mapping,
 * smooth ballistic decay, translucent gradient filling, and peak-hold indicators.
 *
 * Constraints:
 * - Zero allocations in steady-state render() loops.
 * - Under 180 lines of code (Single Responsibility Principle).
 */

import { BaseRenderer } from './BaseRenderer.js';

const MIN_FREQ = 20.0;
const MAX_FREQ = 20000.0;
const LOG_MIN_FREQ = Math.log10(MIN_FREQ);
const LOG_MAX_FREQ = Math.log10(MAX_FREQ);
const LOG_RANGE = LOG_MAX_FREQ - LOG_MIN_FREQ;

export class SpectrumRenderer extends BaseRenderer {
  constructor() {
    super('spectrum');
    this.peakHoldBuffer = null;
    this.decayRate = 0.5; // ~30 dB/s smooth studio ballistic decay
    this.minDb = -96.0;
    this.maxDb = 0.0;
  }

  render(frame, options = {}) {
    if (!this.ctx || this.isDestroyed || !frame) return;

    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;
    const spectrumDb = frame.spectrumDb;
    const bins = spectrumDb ? spectrumDb.length : 0;

    // 1. Clear background
    this.clear(options.bgColor || this.resolveColor(null, '--scope-bg', 'rgba(8, 12, 18, 0.94)'));

    // 2. Render Spectrum Grid (dB & Frequency lines)
    this._drawSpectrumGrid(options);

    if (bins < 8 || frame.signalType === 'control') return;

    const sampleRate = frame.sampleRate || 44100;
    const nyquist = sampleRate / 2;
    const binWidthHz = nyquist / bins;
    const decay = options.decayRate ?? this.decayRate;

    // Allocate / reuse peak-hold buffer
    if (!this.peakHoldBuffer || this.peakHoldBuffer.length !== w) {
      this.peakHoldBuffer = new Float32Array(w).fill(this.minDb);
    }

    // 3. Compute screen curve points via Logarithmic mapping
    const minDb = options.minDb ?? this.minDb;
    const maxDb = options.maxDb ?? this.maxDb;
    const dbRange = maxDb - minDb;

    ctx.save();
    ctx.beginPath();
    ctx.moveTo(0, h);

    const stepPx = Math.max(1, Math.floor(w / 180));
    for (let x = 0; x < w; x += stepPx) {
      // Map screen pixel X to logarithmic frequency (Hz)
      const normX = x / w;
      const freqHz = Math.pow(10, LOG_MIN_FREQ + normX * LOG_RANGE);
      const binIdx = Math.min(bins - 1, Math.max(0, Math.floor(freqHz / binWidthHz)));

      let db = spectrumDb[binIdx];
      if (isNaN(db) || db < minDb) db = minDb;
      if (db > maxDb) db = maxDb;

      // Peak-hold decay
      let peak = this.peakHoldBuffer[x];
      if (db >= peak) {
        peak = db;
      } else {
        peak = Math.max(minDb, peak - decay);
      }
      this.peakHoldBuffer[x] = peak;

      const normY = (db - minDb) / dbRange;
      const y = h - (normY * h);

      if (x === 0) ctx.lineTo(x, y);
      else ctx.lineTo(x, y);
    }

    ctx.lineTo(w, h);
    ctx.closePath();

    // 4. Fill Translucent Gradient (Theme Adaptive)
    const strokeColor = this.resolveColor(options.strokeColor, '--scope-spectrum', '#00e676');
    const gradient = ctx.createLinearGradient(0, 0, 0, h);
    gradient.addColorStop(0, this.colorWithAlpha(strokeColor, 0.45));
    gradient.addColorStop(0.6, this.colorWithAlpha(strokeColor, 0.15));
    gradient.addColorStop(1, 'rgba(0, 0, 0, 0.0)');
    ctx.fillStyle = gradient;
    ctx.fill();

    // 5. Stroke Spectrum Curve
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1.8;
    ctx.stroke();

    // 6. Stroke Peak-Hold Trace
    ctx.beginPath();
    ctx.strokeStyle = options.peakColor || 'rgba(255, 255, 255, 0.4)';
    ctx.lineWidth = 1.0;
    for (let x = 0; x < w; x += stepPx) {
      const normY = (this.peakHoldBuffer[x] - minDb) / dbRange;
      const y = h - (normY * h);
      if (x === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();

    ctx.restore();
  }

  _drawSpectrumGrid(options) {
    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;

    ctx.save();
    ctx.strokeStyle = options.gridColor || this.resolveColor(null, '--scope-grid', 'rgba(255, 255, 255, 0.06)');
    ctx.lineWidth = 1;
    ctx.beginPath();

    // Frequency markers: 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k
    const freqs = [50, 100, 200, 500, 1000, 2000, 5000, 10000];
    for (const f of freqs) {
      const normX = (Math.log10(f) - LOG_MIN_FREQ) / LOG_RANGE;
      const x = Math.floor(normX * w) + 0.5;
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
    }

    // dB markers: -12, -24, -48, -72
    const dbs = [-12, -24, -48, -72];
    for (const db of dbs) {
      const normY = (db - this.minDb) / (this.maxDb - this.minDb);
      const y = Math.floor(h - (normY * h)) + 0.5;
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
    }

    ctx.stroke();
    ctx.restore();
  }

  destroy() {
    super.destroy();
    this.peakHoldBuffer = null;
  }
}
