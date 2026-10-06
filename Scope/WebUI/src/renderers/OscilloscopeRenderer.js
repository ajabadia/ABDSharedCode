/**
 * ABDScope Oscilloscope Renderer
 * ==============================
 * High-performance time-domain visualizer with analog phosphor persistence,
 * sub-sample zero-crossing phase lock, adaptive octave cycle scaling, and theme colors.
 *
 * Constraints:
 * - Zero allocations in steady-state render() loops.
 * - Under 180 lines of code (Single Responsibility Principle).
 */

import { BaseRenderer } from './BaseRenderer.js';

export class OscilloscopeRenderer extends BaseRenderer {
  constructor() {
    super('oscilloscope');
    this.gain = 1.0;
    this.timebase = 1.0;
    this.persistence = 0.0;
    this.autoFit = true;
    this.targetCycles = 0; // 0 = adaptive octave scaling, > 0 = fixed cycle count
  }

  render(frame, options = {}) {
    if (!this.ctx || this.isDestroyed || !frame) return;

    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;
    const isControl = frame.signalType === 'control';
    const midY = isControl ? Math.floor(h * 0.8) : Math.floor(h / 2);
    const scaleY = isControl ? h * 0.7 : midY * 0.88;

    // 1. Clear or Analog Phosphor Trail
    const persist = options.persistence ?? this.persistence;
    const bgClear = this.resolveColor(options.bgColor, '--scope-bg', 'rgba(8, 12, 18, 0.94)');
    if (persist > 0.01) {
      ctx.fillStyle = this.colorWithAlpha(bgClear, Math.max(0.08, 1.0 - persist));
      ctx.fillRect(0, 0, w, h);
    } else {
      this.clear(bgClear);
    }

    // 2. Reticle / Grid
    if (options.grid !== false) {
      this.drawGrid({
        divisionsX: 8,
        divisionsY: isControl ? 5 : 4,
        color: this.resolveColor(options.gridColor, '--scope-grid', 'rgba(255, 255, 255, 0.06)'),
        centerColor: isControl ? 'transparent' : this.resolveColor(options.centerColor, '--scope-grid-center', 'rgba(255, 255, 255, 0.12)')
      });
    }

    const timeDataL = frame.timeDataL;
    const timeDataR = frame.timeDataR;
    const numSamples = frame.numSamples || timeDataL?.length || 0;
    if (numSamples <= 2) return;

    // 3. Sub-Sample Zero-Crossing Lock
    const triggerOffset = isControl ? 0 : (frame.triggerIndex || 0);
    const subSampleFrac = isControl ? 0.0 : (frame.triggerFraction || 0.0);
    const maxAvailable = Math.max(8, numSamples - triggerOffset - 2);

    // 4. Adaptive Cycle Count or Timebase
    let visibleSamples;
    const autoFit = options.autoFit ?? this.autoFit;
    const freq = frame.estimatedFrequencyHz || 0;

    if (autoFit && !isControl && freq >= 18 && freq <= 16000) {
      const sampleRate = frame.sampleRate || 44100;
      const periodSamples = sampleRate / freq;

      // Adaptive cycle scaling by octave (more cycles for higher pitch, fewer for bass)
      let cycles = options.cycles || this.targetCycles;
      if (!cycles || cycles <= 0) {
        if (freq < 80) cycles = 1;
        else if (freq < 180) cycles = 2;
        else if (freq < 500) cycles = 3;
        else if (freq < 1200) cycles = 5;
        else cycles = 8;
      }

      const targetSamples = Math.round(periodSamples * cycles);
      visibleSamples = Math.max(16, Math.min(maxAvailable, targetSamples));
    } else {
      const timebaseFactor = options.timebase || this.timebase;
      visibleSamples = Math.min(maxAvailable, Math.max(8, Math.floor(numSamples * timebaseFactor)));
    }

    if (visibleSamples <= 2) return;

    const gainFactor = options.gain || this.gain;
    const stepX = w / (visibleSamples - 1);

    // 5. Render Channel R (Stereo audio)
    if (timeDataR && options.channel !== 'Left' && !isControl) {
      ctx.save();
      ctx.lineWidth = 1.6;
      ctx.strokeStyle = this.resolveColor(options.traceR, '--scope-trace-r', '#ff007f');
      ctx.beginPath();

      for (let i = 0; i < visibleSamples; ++i) {
        const sPos = triggerOffset + i + subSampleFrac;
        const i0 = Math.max(0, Math.floor(sPos));
        const i1 = Math.min(numSamples - 1, i0 + 1);
        const f = sPos - i0;
        const raw = (timeDataR[i0] * (1.0 - f) + timeDataR[i1] * f) * gainFactor;
        const y = midY - (raw * scaleY);
        const x = i * stepX;

        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
      ctx.restore();
    }

    // 6. Render Channel L / Mono / CV Signal
    if (timeDataL && options.channel !== 'Right') {
      ctx.save();
      ctx.lineWidth = isControl ? 2.5 : 2.0;
      ctx.strokeStyle = isControl
        ? this.resolveColor(options.traceCv, '--scope-accent', '#ffaa00')
        : this.resolveColor(options.traceL, '--scope-trace-l', '#00c3ff');
      ctx.beginPath();

      for (let i = 0; i < visibleSamples; ++i) {
        const sPos = triggerOffset + i + subSampleFrac;
        const i0 = Math.max(0, Math.floor(sPos));
        const i1 = Math.min(numSamples - 1, i0 + 1);
        const f = sPos - i0;
        const raw = (timeDataL[i0] * (1.0 - f) + timeDataL[i1] * f) * gainFactor;
        const y = midY - (raw * scaleY);
        const x = i * stepX;

        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();

      // Phosphor Glow
      ctx.shadowBlur = persist > 0.3 ? 6 : 3;
      ctx.shadowColor = ctx.strokeStyle;
      ctx.stroke();

      ctx.restore();
    }
  }
}
