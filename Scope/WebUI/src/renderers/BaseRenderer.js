/**
 * ABDScope Base Renderer Contract
 * ===============================
 * Abstract base class for all mode renderers (Oscilloscope, Spectrum, Lissajous, etc.).
 * Handles canvas scaling (HiDPI / devicePixelRatio), grid reticle, and cleanup.
 *
 * Constraints:
 * - Zero allocations in steady-state render() loops.
 * - Under 180 lines of code (Single Responsibility Principle).
 */

export class BaseRenderer {
  /**
   * @param {string} modeName - Unique mode identifier (e.g. 'oscilloscope', 'spectrum')
   */
  constructor(modeName) {
    if (!modeName) {
      throw new Error('[ABDScope:BaseRenderer] modeName must be specified');
    }
    this.modeName = modeName;
    this.canvas = null;
    this.ctx = null;
    this.width = 0;
    this.height = 0;
    this.dpr = 1;
    this.isDestroyed = false;
    this.options = {};
  }

  /**
   * Bind an HTML5 Canvas to this renderer and initialize context.
   * @param {HTMLCanvasElement} canvas
   * @param {Object} [options] - Initial render options
   */
  init(canvas, options = {}) {
    if (!canvas || typeof canvas.getContext !== 'function') {
      throw new Error(`[ABDScope:${this.modeName}] init() requires a valid HTMLCanvasElement`);
    }
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d', { alpha: true });
    this.options = { ...options };
    this.isDestroyed = false;
    const w = options.width || canvas.clientWidth || 300;
    const h = options.height || canvas.clientHeight || 150;
    this.resize(w, h, options.dpr || 1);
  }

  attach(canvas, options = {}) {
    this.init(canvas, options);
  }

  /**
   * Resize canvas buffer for crisp HiDPI rendering.
   * @param {number} width - CSS width in pixels
   * @param {number} height - CSS height in pixels
   * @param {number} [dpr=1] - Device pixel ratio
   */
  resize(width, height, dpr = 1) {
    if (!this.canvas || !this.ctx || this.isDestroyed) return;

    this.width = Math.max(10, Math.floor(width));
    this.height = Math.max(10, Math.floor(height));
    this.dpr = Math.max(1, dpr);

    this.canvas.width = Math.floor(this.width * this.dpr);
    this.canvas.height = Math.floor(this.height * this.dpr);
    if (this.canvas.style) {
      this.canvas.style.width = `${this.width}px`;
      this.canvas.style.height = `${this.height}px`;
    }

    this.ctx.setTransform(1, 0, 0, 1, 0, 0); // Reset transform
    this.ctx.scale(this.dpr, this.dpr);
  }

  /**
   * Clear the entire canvas viewport.
   * @param {string} [bgColor='rgba(8, 12, 18, 0.94)'] - Background fill
   */
  clear(bgColor = 'rgba(8, 12, 18, 0.94)') {
    if (!this.ctx || this.isDestroyed) return;
    this.ctx.fillStyle = bgColor;
    this.ctx.fillRect(0, 0, this.width, this.height);
  }

  /**
   * Draw standard studio reticle / graticule.
   * @param {Object} [options]
   */
  drawGrid(options = {}) {
    if (!this.ctx || this.isDestroyed) return;

    const ctx = this.ctx;
    const w = this.width;
    const h = this.height;
    const divX = options.divisionsX || 8;
    const divY = options.divisionsY || 4;
    const gridColor = options.color || 'rgba(255, 255, 255, 0.06)';
    const centerColor = options.centerColor || 'rgba(255, 255, 255, 0.12)';

    ctx.save();
    ctx.lineWidth = 1;
    ctx.strokeStyle = gridColor;

    // Vertical subdivisions
    const stepX = w / divX;
    for (let x = stepX; x < w - 1; x += stepX) {
      ctx.beginPath();
      ctx.moveTo(Math.floor(x) + 0.5, 0);
      ctx.lineTo(Math.floor(x) + 0.5, h);
      ctx.stroke();
    }

    // Horizontal subdivisions
    const stepY = h / divY;
    for (let y = stepY; y < h - 1; y += stepY) {
      ctx.beginPath();
      ctx.moveTo(0, Math.floor(y) + 0.5);
      ctx.lineTo(w, Math.floor(y) + 0.5);
      ctx.stroke();
    }

    // Center Crosshairs (Subtle emphasis)
    ctx.strokeStyle = centerColor;
    ctx.beginPath();
    ctx.moveTo(0, Math.floor(h / 2) + 0.5);
    ctx.lineTo(w, Math.floor(h / 2) + 0.5);
    ctx.moveTo(Math.floor(w / 2) + 0.5, 0);
    ctx.lineTo(Math.floor(w / 2) + 0.5, h);
    ctx.stroke();

    ctx.restore();
  }

  /**
   * Resolves CSS custom properties (var(--...)) or tokens to computed canvas colors.
   * @param {string} [val] - Explicit color override or var(--...) expression
   * @param {string} [cssVarName='--scope-accent'] - CSS custom property name to read from DOM
   * @param {string} [fallback='#00c3ff'] - Fallback color
   * @returns {string} Canvas-ready computed color string
   */
  resolveColor(val, cssVarName = '--scope-accent', fallback = '#00c3ff') {
    if (val && typeof val === 'string' && !val.startsWith('var(')) {
      return val;
    }
    if (typeof window !== 'undefined' && this.canvas) {
      let varName = cssVarName;
      if (val && typeof val === 'string' && val.startsWith('var(')) {
        const match = val.match(/var\(\s*([^,\)]+)/);
        if (match) varName = match[1].trim();
      }
      const targetElem = this.canvas.closest ? (this.canvas.closest('.abd-scope-root') || document.body) : this.canvas;
      const computed = getComputedStyle(targetElem).getPropertyValue(varName).trim();
      if (computed) return computed;
    }
    return fallback;
  }

  /**
   * Converts any CSS color (#hex or rgb(...)) to an RGBA string with custom alpha.
   * @param {string} color - Color string
   * @param {number} [alpha=1.0] - Alpha channel (0.0 to 1.0)
   * @returns {string} rgba(...) color string
   */
  colorWithAlpha(color, alpha = 1.0) {
    if (!color) return `rgba(0, 195, 255, ${alpha})`;
    const trimmed = color.trim();
    if (trimmed.startsWith('#')) {
      let hex = trimmed.slice(1);
      if (hex.length === 3) hex = hex.split('').map(c => c + c).join('');
      const r = parseInt(hex.slice(0, 2), 16) || 0;
      const g = parseInt(hex.slice(2, 4), 16) || 0;
      const b = parseInt(hex.slice(4, 6), 16) || 0;
      return `rgba(${r}, ${g}, ${b}, ${alpha})`;
    }
    if (trimmed.startsWith('rgb')) {
      const m = trimmed.match(/\d+/g);
      if (m && m.length >= 3) {
        return `rgba(${m[0]}, ${m[1]}, ${m[2]}, ${alpha})`;
      }
    }
    return trimmed;
  }

  /**
   * Abstract render method — implemented by mode renderers.
   * @param {Object} frame - ScopeDataFrame
   * @param {Object} [options] - Mode render options
   */
  render(frame, options = {}) {
    throw new Error(`[ABDScope:${this.modeName}] render() must be implemented by subclass`);
  }

  /**
   * Clean up resources and unbind canvas.
   */
  destroy() {
    this.isDestroyed = true;
    this.canvas = null;
    this.ctx = null;
    this.options = null;
  }
}
