/**
 * ABDScope Mount Base
 * ===================
 * Shared multi-lane orchestration for EmbeddedMount and FloatingMount:
 * lane rebuild + state preservation, grid column spans, resize observers,
 * frame routing, tap subscription announcements and full destroy().
 * Shell/toolbar DOM markup lives in mountDom.js (pure helpers);
 * mount-specific DOM/behavior lives in the subclasses.
 */

import { LaneController } from './LaneController.js';
import {
  parseLaneCount,
  pickNextAvailable,
  applyEffectiveColSpans,
  computeLaneHeight,
  resolveFrameForLane,
  DEFAULT_ENABLED_MODES,
  DEFAULT_AVAILABLE_TAPS,
  COMPACT_MODES
} from './mountLayout.js';

export class MountBase {
  constructor(options = {}) {
    this.options = options;
    this.maxLanes = Math.max(1, parseInt(options.maxLanes || 1, 10));
    this.layout = String(parseLaneCount(options.layout || 1, this.maxLanes));
    this.isDestroyed = false;
    this.isOpen = true; // Embedded is always visible; Floating overrides to false

    this.onModeSelect = options.onModeSelect || (() => {});
    this.onResize = options.onResize || (() => {});
    this.onClose = options.onClose || (() => {});
    this.onFreezeToggle = options.onFreezeToggle || (() => {});
    this.onSnapshot = options.onSnapshot || (() => {});
    this.onTapChange = options.onTapChange || (() => {});
    this.onLayoutChange = options.onLayoutChange || (() => {});

    this.lanes = [];
    this.wrapper = null;
    this.headerElement = null;
    this.bodyElement = null;
    this.lanesContainer = null;
    this.vuContainer = null;
    this.layoutButtons = [];
    this.resizeObserver = null;
  }

  /* ───────────────────────── DOM shell helpers ───────────────────────── */

  _queryShell() {
    this.headerElement = this.wrapper.querySelector('.abd-scope-header');
    this.bodyElement = this.wrapper.querySelector('.abd-scope-body');
    this.lanesContainer = this.wrapper.querySelector('.abd-scope-lanes-container');
    this.vuContainer = this.wrapper.querySelector('.abd-scope-vu-container');
    this.layoutButtons = Array.from(this.wrapper.querySelectorAll('.abd-scope-layout-btn'));
  }

  _bindHeaderControls() {
    if (!this.headerElement) return;
    this.headerElement.addEventListener('click', (e) => {
      const layoutBtn = e.target.closest('.abd-scope-layout-btn');
      if (layoutBtn && !layoutBtn.disabled) {
        const l = layoutBtn.dataset.layout;
        this.setLayout(l);
        this.onLayoutChange(l);
        return;
      }
      if (e.target.closest('#scope-freeze-btn')) {
        const btn = e.target.closest('#scope-freeze-btn');
        const isFrozen = btn.classList.toggle('active');
        btn.textContent = isFrozen ? 'RESUME' : 'FREEZE';
        this.onFreezeToggle(isFrozen);
        return;
      }
      if (e.target.closest('#scope-snapshot-btn')) {
        this.onSnapshot();
      }
    });
  }

  /* ───────────────────────── Lane lifecycle ───────────────────────── */

  _rebuildLanes() {
    const previousStates = this.lanes.map(lane => ({
      mode: lane.getActiveMode(),
      tapId: lane.getActiveTap(),
      requestedColSpan: lane.requestedColSpan,
      isFrozen: lane.isFrozen
    }));

    this.lanes.forEach(l => l.destroy());
    this.lanes = [];
    if (this.lanesContainer) this.lanesContainer.innerHTML = '';

    const enabledModes = this.options.enabledModes || DEFAULT_ENABLED_MODES;
    const availableTaps = this.options.availableTaps || DEFAULT_AVAILABLE_TAPS;
    const numLanes = parseLaneCount(this.layout, this.maxLanes);
    const selectedModes = [];
    const selectedTaps = [];

    for (let i = 0; i < numLanes; ++i) {
      const prevState = previousStates[i];

      const initMode = prevState
        ? prevState.mode
        : (pickNextAvailable(enabledModes, selectedModes) || this.options.defaultMode || enabledModes[0]);
      selectedModes.push(initMode);

      const initTap = prevState
        ? prevState.tapId
        : (pickNextAvailable(availableTaps, selectedTaps) || (availableTaps[0]?.id ?? 'master'));
      selectedTaps.push(initTap);

      const initColSpan = prevState
        ? prevState.requestedColSpan
        : (COMPACT_MODES.includes(initMode) ? 1 : 2);
      const initFrozen = prevState ? prevState.isFrozen : false;

      const lane = new LaneController({
        index: i,
        initialMode: initMode,
        colSpan: initColSpan,
        enabledModes,
        availableTaps,
        defaultTap: initTap,
        onModeChange: (laneIdx, mode) => {
          this._updateGridLanes();
          this.onModeSelect(mode, laneIdx);
        },
        onColSpanChange: () => { this._updateGridLanes(); },
        onTapChange: (laneIdx, tapId) => { this.onTapChange(tapId, laneIdx); }
      });

      if (initFrozen) lane.freeze(true);

      this.lanes.push(lane);
      this.lanesContainer.appendChild(lane.rootElement);
    }

    this._updateGridLanes();
    this._triggerResize();

    // Tell the host which probe each lane uses (mount, layout change, or user change).
    this.lanes.forEach((lane, i) => this.onTapChange(lane.getActiveTap(), i));
  }

  _updateGridLanes() {
    applyEffectiveColSpans(this.lanes);
  }

  setLayout(layout) {
    const parsed = parseLaneCount(layout, this.maxLanes);
    if (this.layout === String(parsed)) return;
    this.layout = String(parsed);

    this.layoutButtons.forEach(btn => {
      btn.classList.toggle('active', btn.dataset.layout === String(parsed));
    });
    this._rebuildLanes();
  }

  /* ───────────────────────── Resize & dispatch ───────────────────────── */

  _initResizeObserver() {
    if (typeof ResizeObserver === 'undefined') return;

    this.resizeObserver = new ResizeObserver((entries) => {
      if (this.isDestroyed || !this._isVisible() || !entries[0]) return;
      const rect = entries[0].contentRect;
      if (rect.width > 0 && rect.height > 0) {
        this._dispatchResize(rect.width, rect.height);
      }
    });
    this.resizeObserver.observe(this.bodyElement || this.wrapper);
  }

  _isVisible() {
    return true;
  }

  _triggerResize() {
    if (!this.bodyElement) return;
    const rect = this.bodyElement.getBoundingClientRect();
    if (rect.width > 0 && rect.height > 0) {
      this._dispatchResize(rect.width, rect.height);
    }
  }

  _dispatchResize(totalW, totalH) {
    const dpr = typeof window !== 'undefined' ? window.devicePixelRatio || 1 : 1;
    if (this.lanes.length === 0) return;

    const laneH = computeLaneHeight(totalH, this.lanes.length, this.options.minLaneHeight || 130);
    this.lanes.forEach(lane => lane.resize(totalW, laneH, dpr));
    this.onResize(totalW, totalH, dpr);
  }

  /* ───────────────────────── Public API surface ───────────────────────── */

  get canvas() {
    return this.lanes[0]?.canvasElement ?? null;
  }

  setActiveTab(modeName) {
    if (this.lanes[0]) this.lanes[0].setMode(modeName);
  }

  setActiveTap(tapId) {
    if (this.lanes[0]) this.lanes[0].setActiveTap(tapId);
  }

  freeze(isFrozen) {
    this.lanes.forEach(l => l.freeze(isFrozen));
  }

  render(dataFrame, options = {}) {
    if (this.isDestroyed || !this._isVisible() || !dataFrame) return;

    this.lanes.forEach(lane => {
      const laneFrame = resolveFrameForLane(dataFrame, lane.activeTap);
      if (laneFrame) lane.render(laneFrame, options);
    });
  }

  destroy() {
    if (this.isDestroyed) return;
    this.isDestroyed = true;

    if (this.resizeObserver) {
      this.resizeObserver.disconnect();
      this.resizeObserver = null;
    }
    this.lanes.forEach(l => l.destroy());
    this.lanes = [];
    if (this.wrapper && this.wrapper.parentNode) {
      this.wrapper.parentNode.removeChild(this.wrapper);
    }
    this.wrapper = null;
    this.headerElement = null;
    this.bodyElement = null;
    this.lanesContainer = null;
    this.vuContainer = null;
    this.layoutButtons = [];
  }
}
