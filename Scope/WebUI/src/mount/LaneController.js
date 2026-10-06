/**
 * ABDScope Lane Controller
 * ========================
 * Encapsulates one independent visual channel / lane: owns the renderer set,
 * active mode/tap state, canvas resolution (HiDPI), freeze and destruction.
 * DOM construction and visual updates are delegated to LaneView.
 */

import { OscilloscopeRenderer } from '../renderers/OscilloscopeRenderer.js';
import { SpectrumRenderer } from '../renderers/SpectrumRenderer.js';
import { LissajousRenderer } from '../renderers/LissajousRenderer.js';
import { PhaseMeterRenderer } from '../renderers/PhaseMeterRenderer.js';
import { SpectrogramRenderer } from '../renderers/SpectrogramRenderer.js';
import { downloadCanvasAsPng } from '../utils/exportImage.js';
import { LaneView } from './LaneView.js';
import { COMPACT_MODES } from './mountLayout.js';

export class LaneController {
  constructor(options = {}) {
    this.index = options.index ?? 0;
    this.enabledModes = options.enabledModes || ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'];
    this.availableTaps = options.availableTaps || [{ id: 'master', name: 'Master Out' }];
    this.activeMode = options.initialMode || this.enabledModes[0];
    this.activeTap = options.defaultTap || this.availableTaps[0]?.id || 'master';

    // Requested column span: 1 for compact modes (lissajous, phase), 2 for panoramic modes
    this.requestedColSpan = options.colSpan || (COMPACT_MODES.includes(this.activeMode) ? 1 : 2);
    this.effectiveColSpan = this.requestedColSpan;

    this.onModeChange = options.onModeChange || null;
    this.onTapChange = options.onTapChange || null;
    this.onColSpanChange = options.onColSpanChange || null;

    this.view = new LaneView({
      index: this.index,
      enabledModes: this.enabledModes,
      availableTaps: this.availableTaps,
      activeMode: this.activeMode,
      activeTap: this.activeTap,
      requestedColSpan: this.requestedColSpan
    });

    // Public element aliases (mount & tests depend on them)
    this.rootElement = this.view.rootElement;
    this.headerElement = this.view.headerElement;
    this.canvasWrap = this.view.canvasWrap;
    this.canvasElement = this.view.canvasElement;
    this.modeButtons = this.view.modeButtons;
    this.tapSelectElement = this.view.tapSelectElement;
    this.infoBadgeElement = this.view.infoBadgeElement;
    this.colSpanBtn = this.view.colSpanBtn;
    this.freezeBtn = this.view.freezeBtn;
    this.snapshotBtn = this.view.snapshotBtn;

    this.renderers = new Map();
    this.activeRenderer = null;
    this.isFrozen = false;
    this.resizeObserver = null;

    this._wireEvents();
    this._initRenderers();
    this._initResizeObserver();
    this.setMode(this.activeMode);
    this.setEffectiveColSpan(this.effectiveColSpan);
  }

  _wireEvents() {
    this.modeButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const mode = btn.dataset.mode;
        this.setMode(mode);
        this.setRequestedColSpan(COMPACT_MODES.includes(mode) ? 1 : 2);
        if (typeof this.onModeChange === 'function') {
          this.onModeChange(this.index, this.activeMode);
        }
      });
    });

    if (this.tapSelectElement) {
      this.tapSelectElement.addEventListener('change', (e) => {
        this.setActiveTap(e.target.value);
        if (typeof this.onTapChange === 'function') {
          this.onTapChange(this.index, this.activeTap);
        }
      });
    }

    this.colSpanBtn.addEventListener('click', () => {
      const nextSpan = this.requestedColSpan === 1 ? 2 : 1;
      this.setRequestedColSpan(nextSpan);
      if (typeof this.onColSpanChange === 'function') {
        this.onColSpanChange(this.index, nextSpan);
      }
    });

    this.freezeBtn.addEventListener('click', () => {
      this.freeze(!this.isFrozen);
    });

    this.snapshotBtn.addEventListener('click', () => {
      downloadCanvasAsPng(this.canvasElement, `abd-scope-lane${this.index + 1}-${this.activeMode}.png`);
    });
  }

  _registerRenderer(mode, RendererClass) {
    if (!this.enabledModes.includes(mode)) return;
    const renderer = new RendererClass();
    renderer.init(this.canvasElement);
    this.renderers.set(mode, renderer);
  }

  _initRenderers() {
    this._registerRenderer('oscilloscope', OscilloscopeRenderer);
    this._registerRenderer('spectrum', SpectrumRenderer);
    this._registerRenderer('lissajous', LissajousRenderer);
    this._registerRenderer('phase', PhaseMeterRenderer);
    this._registerRenderer('spectrogram', SpectrogramRenderer);
  }

  _initResizeObserver() {
    if (typeof ResizeObserver === 'undefined') return;

    this.resizeObserver = new ResizeObserver((entries) => {
      if (!entries[0] || !this.canvasWrap) return;
      const rect = entries[0].contentRect;
      if (rect.width > 0 && rect.height > 0) {
        const dpr = typeof window !== 'undefined' ? window.devicePixelRatio || 1 : 1;
        this._updateCanvasResolution(rect.width, rect.height, dpr);
      }
    });
    this.resizeObserver.observe(this.canvasWrap);
  }

  /* ───────── State & span management ───────── */

  setRequestedColSpan(span = 1) {
    this.requestedColSpan = Math.max(1, Math.min(2, span));
    this.view.setColSpanButton(this.requestedColSpan);
  }

  setEffectiveColSpan(span = 1) {
    this.effectiveColSpan = Math.max(1, Math.min(2, span));
    this.view.setDataColSpan(this.effectiveColSpan);
    if (this.canvasWrap) {
      const rect = this.canvasWrap.getBoundingClientRect();
      if (rect.width > 0 && rect.height > 0) {
        const dpr = typeof window !== 'undefined' ? window.devicePixelRatio || 1 : 1;
        this._updateCanvasResolution(rect.width, rect.height, dpr);
      }
    }
  }

  getColSpan() {
    return this.effectiveColSpan;
  }

  setColSpan(span = 1) {
    this.setRequestedColSpan(span);
    this.setEffectiveColSpan(span);
  }

  setMode(mode) {
    if (!this.renderers.has(mode)) return;
    this.activeMode = mode;
    this.activeRenderer = this.renderers.get(mode);
    this.view.setModeActive(mode);
  }

  getActiveMode() {
    return this.activeMode;
  }

  setActiveTap(tapId) {
    this.activeTap = tapId;
    this.view.setTapValue(tapId);
  }

  getActiveTap() {
    return this.activeTap;
  }

  setAvailableTaps(taps = []) {
    this.availableTaps = taps;
    this.view.setTapOptions(taps, this.activeTap);
  }

  freeze(frozen = true) {
    this.isFrozen = frozen;
    this.view.setFreezeActive(frozen);
    this.renderers.forEach(r => {
      if (typeof r.freeze === 'function') r.freeze(frozen);
    });
  }

  /* ───────── Rendering & HiDPI resizing ───────── */

  render(dataFrame, options = {}) {
    if (!this.activeRenderer || !dataFrame || this.isFrozen) return;

    if (dataFrame.detectedNoteName && dataFrame.estimatedFrequencyHz > 0) {
      this.view.setInfoText(`${dataFrame.detectedNoteName} (${Math.round(dataFrame.estimatedFrequencyHz)} Hz)`);
    } else if (dataFrame.rmsDb !== undefined && dataFrame.rmsDb > -96) {
      this.view.setInfoText(`${Math.round(dataFrame.rmsDb)} dB`);
    } else {
      this.view.setInfoText('');
    }

    this.activeRenderer.render(dataFrame, options);
  }

  _updateCanvasResolution(width, height, dpr = 1) {
    if (!this.canvasElement) return;

    this.canvasElement.width = Math.floor(width * dpr);
    this.canvasElement.height = Math.floor(height * dpr);
    this.canvasElement.style.width = `${width}px`;
    this.canvasElement.style.height = `${height}px`;

    this.renderers.forEach(r => {
      if (typeof r.resize === 'function') r.resize(width, height, dpr);
    });
  }

  resize(width, height, dpr = 1) {
    if (!this.canvasElement) return;

    const canvasH = Math.max(50, height - (this.headerElement?.offsetHeight ?? 26));
    const effectiveW = this.effectiveColSpan === 1 ? Math.floor(width / 2) : width;
    this._updateCanvasResolution(effectiveW, canvasH, dpr);
  }

  destroy() {
    if (this.resizeObserver) {
      this.resizeObserver.disconnect();
      this.resizeObserver = null;
    }
    this.renderers.forEach(r => {
      if (typeof r.destroy === 'function') r.destroy();
    });
    this.renderers.clear();
    this.activeRenderer = null;
    this.rootElement?.remove();
    this.rootElement = null;
    this.view = null;
  }
}
