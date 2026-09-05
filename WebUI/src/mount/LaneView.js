/**
 * ABDScope Lane View
 * ==================
 * Pure DOM construction + visual state for one lane: header toolbar (badge,
 * mode buttons, probe selector, info badge, colSpan/freeze/snapshot buttons)
 * and the canvas viewport. No renderers, no resize observers, no state logic —
 * LaneController owns those.
 */

import { SCOPE_ICONS } from '../icons.js';

export class LaneView {
  constructor(options = {}) {
    this.index = options.index ?? 0;
    this.enabledModes = options.enabledModes || [];
    this.availableTaps = options.availableTaps || [];
    this.activeMode = options.activeMode || this.enabledModes[0];
    this.activeTap = options.activeTap || this.availableTaps[0]?.id || 'master';
    this.requestedColSpan = options.requestedColSpan ?? 2;

    this.modeButtons = [];
    this._build();
  }

  _build() {
    this.rootElement = document.createElement('div');
    this.rootElement.className = 'abd-scope-lane';
    this.rootElement.setAttribute('data-lane-index', String(this.index));
    this.rootElement.setAttribute('data-col-span', String(this.requestedColSpan));

    this.headerElement = document.createElement('div');
    this.headerElement.className = 'abd-scope-lane-header';

    const leftGroup = document.createElement('div');
    leftGroup.className = 'abd-scope-lane-group-left';

    const laneBadge = document.createElement('span');
    laneBadge.className = 'abd-scope-lane-badge';
    laneBadge.textContent = `L${this.index + 1}`;
    laneBadge.title = `Lane ${this.index + 1}`;
    leftGroup.appendChild(laneBadge);

    if (this.enabledModes.length > 1) {
      const modeGroup = document.createElement('div');
      modeGroup.className = 'abd-scope-lane-mode-group';

      this.enabledModes.forEach(mode => {
        const btn = document.createElement('button');
        btn.className = `abd-scope-lane-mode-btn ${mode === this.activeMode ? 'active' : ''}`;
        btn.dataset.mode = mode;
        btn.textContent = this._formatModeButtonLabel(mode);
        btn.title = this._formatModeName(mode);
        modeGroup.appendChild(btn);
        this.modeButtons.push(btn);
      });
      leftGroup.appendChild(modeGroup);
    }

    const rightGroup = document.createElement('div');
    rightGroup.className = 'abd-scope-lane-group-right';

    if (this.availableTaps.length > 1) {
      this.tapSelectElement = document.createElement('select');
      this.tapSelectElement.className = 'abd-scope-lane-select abd-scope-lane-tap-select';
      this.tapSelectElement.title = 'Signal Input / Telemetry Tap';
      this._populateTapOptions();
      rightGroup.appendChild(this.tapSelectElement);
    }

    this.infoBadgeElement = document.createElement('span');
    this.infoBadgeElement.className = 'abd-scope-lane-info';
    rightGroup.appendChild(this.infoBadgeElement);

    this.colSpanBtn = document.createElement('button');
    this.colSpanBtn.className = 'abd-scope-lane-tool-btn abd-scope-lane-span-btn';
    this.colSpanBtn.title = 'Toggle Column Width (Full 100% / Half 50%)';
    this.colSpanBtn.textContent = this.requestedColSpan === 1 ? '½' : '1';
    rightGroup.appendChild(this.colSpanBtn);

    this.freezeBtn = document.createElement('button');
    this.freezeBtn.className = 'abd-scope-lane-tool-btn abd-scope-lane-freeze-btn';
    this.freezeBtn.title = 'Freeze / Hold this Lane';
    this.freezeBtn.innerHTML = `
      ${SCOPE_ICONS.freeze}
    `;
    rightGroup.appendChild(this.freezeBtn);

    this.snapshotBtn = document.createElement('button');
    this.snapshotBtn.className = 'abd-scope-lane-tool-btn abd-scope-lane-snapshot-btn';
    this.snapshotBtn.title = 'Export PNG Snapshot of this Lane';
    this.snapshotBtn.innerHTML = `
      ${SCOPE_ICONS.camera}
    `;
    rightGroup.appendChild(this.snapshotBtn);

    this.headerElement.appendChild(leftGroup);
    this.headerElement.appendChild(rightGroup);

    this.canvasWrap = document.createElement('div');
    this.canvasWrap.className = 'abd-scope-lane-canvas-wrap';

    this.canvasElement = document.createElement('canvas');
    this.canvasElement.className = 'abd-scope-canvas abd-scope-lane-canvas';
    this.canvasWrap.appendChild(this.canvasElement);

    this.rootElement.appendChild(this.headerElement);
    this.rootElement.appendChild(this.canvasWrap);
  }

  _populateTapOptions() {
    this.tapSelectElement.innerHTML = '';
    this.availableTaps.forEach(tap => {
      const opt = document.createElement('option');
      opt.value = tap.id;
      opt.textContent = tap.name;
      if (tap.id === this.activeTap) opt.selected = true;
      this.tapSelectElement.appendChild(opt);
    });
  }

  /* ───────── Visual state updates (no behavior) ───────── */

  setModeActive(mode) {
    this.modeButtons.forEach(btn => {
      btn.classList.toggle('active', btn.dataset.mode === mode);
    });
  }

  setTapOptions(taps, activeTap) {
    this.availableTaps = taps || [];
    if (this.tapSelectElement) {
      this.tapSelectElement.innerHTML = '';
      this.availableTaps.forEach(tap => {
        const opt = document.createElement('option');
        opt.value = tap.id;
        opt.textContent = tap.name;
        if (tap.id === activeTap) opt.selected = true;
        this.tapSelectElement.appendChild(opt);
      });
    }
  }

  setTapValue(tapId) {
    if (this.tapSelectElement && this.tapSelectElement.value !== tapId) {
      this.tapSelectElement.value = tapId;
    }
  }

  setColSpanButton(span) {
    this.requestedColSpan = span;
    if (this.colSpanBtn) {
      this.colSpanBtn.textContent = span === 1 ? '½' : '1';
      this.colSpanBtn.classList.toggle('active', span === 2);
    }
  }

  setFreezeActive(frozen) {
    if (this.freezeBtn) {
      this.freezeBtn.classList.toggle('active', frozen);
    }
  }

  setDataColSpan(span) {
    this.rootElement.setAttribute('data-col-span', String(span));
  }

  setInfoText(text) {
    if (this.infoBadgeElement) {
      this.infoBadgeElement.textContent = text || '';
    }
  }

  /* ───────── Label formatting helpers ───────── */

  _formatModeButtonLabel(mode) {
    switch (mode) {
      case 'oscilloscope': return 'OSC';
      case 'spectrum':     return 'FFT';
      case 'lissajous':    return 'LISS';
      case 'phase':        return 'PHASE';
      case 'spectrogram':  return 'WATERFALL';
      default: return mode.slice(0, 4).toUpperCase();
    }
  }

  _formatModeName(mode) {
    switch (mode) {
      case 'oscilloscope': return 'Oscilloscope';
      case 'spectrum':     return 'FFT Spectrum';
      case 'lissajous':    return 'Lissajous';
      case 'phase':        return 'Phase Meter';
      case 'spectrogram':  return 'Spectrogram Waterfall';
      default: return mode.charAt(0).toUpperCase() + mode.slice(1);
    }
  }
}
