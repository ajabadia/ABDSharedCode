/**
 * ABDScope Main Factory
 * =====================
 * Primary entry point for instantiating universal audio scopes in any ABDSynths project.
 * Supports Single, Dual Split, and Triple Multi-Lane visual analysis.
 */

import { EmbeddedMount } from './mount/EmbeddedMount.js';
import { FloatingMount } from './mount/FloatingMount.js';
import { AnalyserInput } from './input/AnalyserInput.js';
import { PushInput } from './input/PushInput.js';
import { VuMeterRenderer } from './renderers/VuMeterRenderer.js';
import { copyCanvasToClipboard, downloadCanvasAsPng } from './utils/exportImage.js';

/**
 * Factory to create an ABDScope instance.
 * @param {Object} config - Configuration options
 * @returns {Object} Scope instance controller
 */
export function createScope(config = {}) {
  const mountMode = config.mountMode || 'embedded';
  const enabledModes = config.enabledModes || ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'];
  let currentMode = config.defaultMode || enabledModes[0] || 'oscilloscope';

  let activeInput = null;
  let isFrozen = false;
  let isDestroyed = false;
  let mount = null;
  let vuMeter = null;
  const legacyRenderers = new Map();

  // Handle mode selection callback
  const onModeSelect = (mode, laneIdx = 0) => {
    if (laneIdx === 0) currentMode = mode;
    if (config.onModeSelect) config.onModeSelect(mode, laneIdx);
  };

  // Handle resize callback
  const onResize = (w, h, dpr) => {
    if (vuMeter && mount && mount.vuContainer) {
      vuMeter.resize(mount.vuContainer.clientWidth || 24, h, dpr);
    }
  };

  const onFreezeToggle = (frozen) => {
    isFrozen = frozen;
    if (mount && mount.freeze) mount.freeze(frozen);
  };

  const onSnapshot = () => {
    if (mount && mount.canvas) {
      downloadCanvasAsPng(mount.canvas, 'abd-scope-capture.png');
    }
  };

  // Mount instantiation
  const mountOptions = {
    ...config,
    maxLanes: config.maxLanes || 1,
    enabledModes,
    defaultMode: currentMode,
    onModeSelect,
    onResize,
    onFreezeToggle,
    onSnapshot
  };

  mount = mountMode === 'floating'
    ? new FloatingMount(mountOptions)
    : new EmbeddedMount(config.containerId, mountOptions);

  vuMeter = (config.showVuMeters && mount.vuContainer)
    ? new VuMeterRenderer(mount.vuContainer)
    : null;

  // Frame processing and dispatch
  const handleFrame = (frame) => {
    if (isDestroyed || isFrozen || !frame) return;

    // Render multi-lane mount (per-lane note/Hz badges update inside each lane)
    if (mount.render) {
      mount.render(frame, config.renderOptions || {});
    }

    // Legacy renderers support
    const legacyR = legacyRenderers.get(currentMode);
    if (legacyR) {
      legacyR.render(frame, config.renderOptions || {});
    }

    if (vuMeter) {
      vuMeter.render(frame);
    }
  };

  // Controller API
  const instance = {
    mount,
    get currentMode() { return currentMode; },
    get isDestroyed() { return isDestroyed; },
    get isOpen() { return mount.isOpen ?? true; },
    get layout() { return mount.layout || 'single'; },

    registerRenderer(modeName, rendererInstance) {
      if (!rendererInstance) return;
      legacyRenderers.set(modeName, rendererInstance);
      if (mount.canvas) {
        rendererInstance.init(mount.canvas, config.rendererOptions || {});
      }
    },

    setMode(modeName) {
      currentMode = modeName;
      mount.setActiveTab?.(modeName);
    },

    setLayout(layout) {
      if (mount.setLayout) mount.setLayout(layout);
    },

    getLane(laneIndex = 0) {
      return mount.lanes?.[laneIndex] ?? null;
    },

    setLaneConfig(laneIndex, { mode, tapId }) {
      const lane = mount.lanes?.[laneIndex];
      if (!lane) return;
      if (mode) lane.setMode(mode);
      if (tapId) lane.setActiveTap(tapId);
    },

    connectAnalyser(analyserNode, options = {}) {
      if (activeInput) activeInput.destroy();
      activeInput = new AnalyserInput(analyserNode, { ...config, ...options });
      activeInput.start(handleFrame);
      return activeInput;
    },

    pushFrame(rawPacket) {
      if (!activeInput || !(activeInput instanceof PushInput)) {
        if (activeInput) activeInput.destroy();
        activeInput = new PushInput(config);
        activeInput.start(handleFrame);
      }
      return activeInput.push(rawPacket);
    },

    async captureFrame() {
      return mount.canvas ? copyCanvasToClipboard(mount.canvas) : false;
    },

    downloadFrame(filename) {
      if (mount.canvas) downloadCanvasAsPng(mount.canvas, filename);
    },

    setActiveTap(tapId) {
      if (mount.setActiveTap) mount.setActiveTap(tapId);
      if (config.onTapChange) config.onTapChange(tapId);
    },

    open() {
      if (mount.open) mount.open();
    },

    close() {
      if (mount.close) mount.close();
    },

    toggle() {
      if (mount.toggle) mount.toggle();
    },

    destroy() {
      if (isDestroyed) return;
      isDestroyed = true;

      if (activeInput) {
        activeInput.destroy();
        activeInput = null;
      }

      if (vuMeter) {
        vuMeter.destroy();
      }

      legacyRenderers.forEach(r => r.destroy());
      legacyRenderers.clear();

      mount.destroy();
    }
  };

  return instance;
}
