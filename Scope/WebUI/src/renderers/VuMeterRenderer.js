/**
 * ABDScope Companion VU Meter Renderer
 * =====================================
 * High-performance stereo level meter (Left & Right) with RMS fill,
 * peak-hold markers, and ballistic decay.
 *
 * Constraints:
 * - Zero allocations in steady-state render() loops.
 * - Under 150 lines of code (Single Responsibility Principle).
 */

export class VuMeterRenderer {
  /**
   * @param {HTMLElement} container - DOM container element (.abd-scope-vu-container)
   */
  constructor(container) {
    this.container = container;
    this.canvas = null;
    this.ctx = null;
    this.width = 24;
    this.height = 100;
    this.peakL = 0;
    this.peakR = 0;
    this.peakDecay = 0.015;
    this.isDestroyed = false;

    this._createCanvas();
  }

  _createCanvas() {
    if (!this.container) return;

    this.container.innerHTML = '';
    this.canvas = document.createElement('canvas');
    this.canvas.className = 'abd-scope-vu-canvas';
    this.container.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d', { alpha: true });

    const w = this.container.clientWidth || 24;
    const h = this.container.clientHeight || 120;
    this.resize(w, h);
  }

  resize(w, h, dpr = 1) {
    if (!this.canvas || this.isDestroyed) return;

    this.width = Math.max(12, Math.floor(w));
    this.height = Math.max(20, Math.floor(h));
    const deviceDpr = Math.max(1, dpr);

    this.canvas.width = Math.floor(this.width * deviceDpr);
    this.canvas.height = Math.floor(this.height * deviceDpr);
    this.canvas.style.width = `${this.width}px`;
    this.canvas.style.height = `${this.height}px`;

    if (this.ctx) {
      this.ctx.setTransform(1, 0, 0, 1, 0, 0);
      this.ctx.scale(deviceDpr, deviceDpr);
    }
  }

  render(frame) {
    if (!this.ctx || this.isDestroyed || !frame) return;

    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;

    const rmsL = Math.min(1.0, frame.rmsL || 0.0);
    const rmsR = Math.min(1.0, frame.rmsR ?? rmsL);
    const rawPeakL = Math.min(1.0, frame.peakL || 0.0);
    const rawPeakR = Math.min(1.0, frame.peakR ?? rawPeakL);

    // Peak decay
    this.peakL = rawPeakL >= this.peakL ? rawPeakL : Math.max(0, this.peakL - this.peakDecay);
    this.peakR = rawPeakR >= this.peakR ? rawPeakR : Math.max(0, this.peakR - this.peakDecay);

    // Clear
    ctx.clearRect(0, 0, w, h);

    const barW = Math.max(3, Math.floor((w - 5) / 2));
    const xL = 1;
    const xR = xL + barW + 2;

    this._drawBar(ctx, xL, 0, barW, h, rmsL, this.peakL);
    this._drawBar(ctx, xR, 0, barW, h, rmsR, this.peakR);
  }

  _drawBar(ctx, x, y, w, h, rmsVal, peakVal) {
    // 1. Dark Slot
    ctx.fillStyle = 'rgba(0, 0, 0, 0.6)';
    ctx.fillRect(x, y, w, h);

    // 2. Filled RMS Bar with Gradient
    const fillH = Math.floor(rmsVal * h);
    if (fillH > 0) {
      const grad = ctx.createLinearGradient(0, h, 0, 0);
      grad.addColorStop(0, '#00e676'); // Green (-inf to -12 dB)
      grad.addColorStop(0.7, '#ffea00'); // Yellow (-6 dB)
      grad.addColorStop(0.95, '#ff3344'); // Red (0 dB clip)

      ctx.fillStyle = grad;
      ctx.fillRect(x, h - fillH, w, fillH);
    }

    // 3. Peak-Hold Bar (Tick)
    const peakY = Math.floor(h - (peakVal * h));
    ctx.fillStyle = peakVal >= 0.98 ? '#ff3344' : '#ffffff';
    ctx.fillRect(x, Math.max(0, Math.min(h - 2, peakY)), w, 2);
  }

  destroy() {
    this.isDestroyed = true;
    if (this.canvas && this.canvas.parentNode) {
      this.canvas.parentNode.removeChild(this.canvas);
    }
    this.canvas = null;
    this.ctx = null;
    this.container = null;
  }
}
