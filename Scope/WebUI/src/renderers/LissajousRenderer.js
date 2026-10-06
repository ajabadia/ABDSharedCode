/**
 * ABDScope Lissajous & Goniometer (X-Y Vectorscope) Renderer
 * ==========================================================
 * Real-time stereo phase and spatial correlation visualizer.
 * Rotates channels 45° (Mid/Side) for traditional studio vectorscope orientation,
 * combined with analog CRT phosphor persistence.
 *
 * Constraints:
 * - Zero allocations in steady-state render() loops.
 * - Under 170 lines of code (Single Responsibility Principle).
 */

import { BaseRenderer } from './BaseRenderer.js';

const INV_SQRT2 = 0.7071067811865475; // 1 / sqrt(2) for 45° rotation

export class LissajousRenderer extends BaseRenderer {
  constructor() {
    super('lissajous');
    this.gain = 1.0;
    this.persistence = 0.75; // Smooth phosphor cloud
  }

  render(frame, options = {}) {
    if (!this.ctx || this.isDestroyed || !frame) return;

    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;
    const centerX = Math.floor(w / 2);
    const centerY = Math.floor(h / 2);
    const radius = Math.min(centerX, centerY) * 0.88;

    // 1. Analog Phosphor Trail
    const persist = options.persistence ?? this.persistence;
    ctx.fillStyle = `rgba(8, 12, 18, ${Math.max(0.1, 1.0 - persist)})`;
    ctx.fillRect(0, 0, w, h);

    // 2. Reticle / Circular Goniometer Grid
    if (options.grid !== false) {
      this._drawGoniometerGrid(centerX, centerY, radius, options);
    }

    const timeDataL = frame.timeDataL;
    const timeDataR = frame.timeDataR || timeDataL; // Fallback to mono if single channel
    const numSamples = frame.numSamples || timeDataL?.length || 0;
    if (numSamples <= 2 || frame.signalType === 'control') return;

    const gainFactor = (options.gain || this.gain) * radius;

    // 3. Render Lissajous / Vectorscope Trace (Rotated 45° M/S)
    ctx.save();
    ctx.lineWidth = 1.4;
    ctx.strokeStyle = this.resolveColor(options.traceColor, '--scope-trace-l', '#00c3ff');
    ctx.beginPath();

    for (let i = 0; i < numSamples; ++i) {
      const l = timeDataL[i] || 0.0;
      const r = timeDataR[i] || 0.0;

      // 45-degree rotation for standard M/S stereo vectorscope:
      // X = (L - R) / sqrt(2)  -> Stereo Difference (Side)
      // Y = (L + R) / sqrt(2)  -> Mono Sum (Mid)
      const xRot = (l - r) * INV_SQRT2 * gainFactor;
      const yRot = (l + r) * INV_SQRT2 * gainFactor;

      const px = centerX + xRot;
      const py = centerY - yRot; // Invert Y for screen coordinates

      if (i === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    }

    ctx.stroke();

    // Phosphor glow
    if (persist > 0.4) {
      ctx.shadowBlur = 8;
      ctx.shadowColor = ctx.strokeStyle;
      ctx.stroke();
    }

    ctx.restore();
  }

  _drawGoniometerGrid(cx, cy, r, options) {
    const ctx = this.ctx;
    const color = options.gridColor || 'rgba(255, 255, 255, 0.07)';

    ctx.save();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;

    // Concentric reference circles
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.arc(cx, cy, r * 0.5, 0, Math.PI * 2);
    ctx.stroke();

    // M/S and L/R diagonal axes
    ctx.beginPath();
    // Vertical (Mid / Mono Sum axis)
    ctx.moveTo(cx, cy - r);
    ctx.lineTo(cx, cy + r);
    // Horizontal (Side / Stereo Diff axis)
    ctx.moveTo(cx - r, cy);
    ctx.lineTo(cx + r, cy);
    // 45° Diagonals (L and R axes)
    const d = r * INV_SQRT2;
    ctx.moveTo(cx - d, cy - d);
    ctx.lineTo(cx + d, cy + d);
    ctx.moveTo(cx - d, cy + d);
    ctx.lineTo(cx + d, cy - d);
    ctx.stroke();

    ctx.restore();
  }
}
