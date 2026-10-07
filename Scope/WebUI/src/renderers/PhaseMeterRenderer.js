/**
 * ABDScope Stereo Phase Correlation Meter Renderer
 * =================================================
 * Real-time stereo phase correlation analyzer with ballistic smoothing,
 * color-coded compatibility zones (-1.0 to +1.0), and numerical readout.
 *
 * Constraints:
 * - Zero allocations in steady-state render() loops.
 * - Under 160 lines of code (Single Responsibility Principle).
 */

import { BaseRenderer } from './BaseRenderer.js';

export class PhaseMeterRenderer extends BaseRenderer {
  constructor() {
    super('phase');
    this.smoothedCorr = 1.0;
    this.smoothingFactor = 0.15; // Smooth needle damping
  }

  render(frame, options = {}) {
    if (!this.ctx || this.isDestroyed || !frame) return;

    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;

    // 1. Clear background
    this.clear(options.bgColor || 'rgba(8, 12, 18, 0.94)');

    const rawCorr = frame.phaseCorrelation ?? 1.0;
    // Ballistic smoothing
    this.smoothedCorr += (rawCorr - this.smoothedCorr) * (options.smoothing || this.smoothingFactor);

    const barH = Math.max(16, Math.floor(h * 0.22));
    const barY = Math.floor((h - barH) / 2);
    const paddingX = Math.floor(w * 0.1);
    const barW = w - (paddingX * 2);
    const midX = Math.floor(w / 2);

    // 2. Draw Slot Background & Scale Markers
    ctx.save();
    ctx.fillStyle = 'rgba(0, 0, 0, 0.6)';
    ctx.fillRect(paddingX, barY, barW, barH);
    ctx.strokeStyle = options.borderColor || 'rgba(255, 255, 255, 0.15)';
    ctx.strokeRect(paddingX, barY, barW, barH);

    // Zone Markers (-1, 0, +1)
    ctx.font = '10px monospace';
    ctx.fillStyle = '#7e9bb5';
    ctx.textAlign = 'center';
    ctx.fillText('-1.0 (Anti-Phase)', paddingX + 20, barY - 8);
    ctx.fillText('0.0 (Stereo)', midX, barY - 8);
    ctx.fillText('+1.0 (Mono)', paddingX + barW - 20, barY - 8);

    // Center vertical tick
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
    ctx.beginPath();
    ctx.moveTo(midX, barY - 3);
    ctx.lineTo(midX, barY + barH + 3);
    ctx.stroke();

    // 3. Draw Colored Correlation Bar
    // Correlation mapped from [-1, +1] -> [0, 1]
    const norm = (this.smoothedCorr + 1.0) / 2.0;
    const markerX = paddingX + Math.floor(norm * barW);

    let color = '#00e676'; // Green (> +0.2)
    if (this.smoothedCorr < -0.2) color = '#ff3344'; // Red (< -0.2)
    else if (this.smoothedCorr < 0.2) color = '#ffea00'; // Yellow (-0.2 to +0.2)

    // Bar fill from center (0.0) to current correlation
    const fillWidth = markerX - midX;
    ctx.fillStyle = color;
    if (fillWidth !== 0) {
      ctx.fillRect(Math.min(midX, markerX), barY + 2, Math.abs(fillWidth), barH - 4);
    }

    // Needle Cursor
    ctx.fillStyle = '#ffffff';
    ctx.shadowBlur = 6;
    ctx.shadowColor = color;
    ctx.fillRect(markerX - 2, barY - 2, 4, barH + 4);

    // 4. Numerical readout at bottom
    ctx.shadowBlur = 0;
    ctx.fillStyle = color;
    ctx.font = 'bold 12px monospace';
    ctx.textAlign = 'center';
    const sign = this.smoothedCorr >= 0 ? '+' : '';
    ctx.fillText(`PHASE CORRELATION: ${sign}${this.smoothedCorr.toFixed(2)}`, midX, barY + barH + 24);

    ctx.restore();
  }
}
