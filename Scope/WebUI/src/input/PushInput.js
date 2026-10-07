/**
 * ABDScope Push Input Adapter
 * ===========================
 * Receives externally pushed telemetry frames from C++ JUCE IPC Bridge or WASM.
 * Normalizes packets and invokes listeners without maintaining an internal loop.
 *
 * Constraints:
 * - Pure adapter, zero DOM dependencies.
 * - Under 80 lines (Single Responsibility Principle).
 */

import { createDataFrame } from '../frame.js';

export class PushInput {
  /**
   * @param {Object} [options] - Configuration defaults
   */
  constructor(options = {}) {
    this.options = options;
    this.onFrameCallback = null;
    this.lastFrame = null;
  }

  /**
   * Register frame listener.
   * @param {Function} callback - Receives ScopeDataFrame
   */
  start(callback) {
    if (typeof callback !== 'function') {
      throw new Error('[ABDScope:PushInput] callback must be a function');
    }
    this.onFrameCallback = callback;
  }

  /**
   * Push an external raw packet into the scope pipeline.
   * @param {Object} rawPacket - Raw audio/telemetry packet from bridge
   * @returns {Object} Normalized ScopeDataFrame
   */
  push(rawPacket) {
    if (!rawPacket || this.isDestroyed) return null;

    const frame = createDataFrame(rawPacket, this.options || {});
    this.lastFrame = frame;

    if (this.onFrameCallback) {
      this.onFrameCallback(frame);
    }

    return frame;
  }

  /**
   * Stop receiving frames.
   */
  stop() {
    this.onFrameCallback = null;
  }

  /**
   * Explicit cleanup.
   */
  destroy() {
    this.isDestroyed = true;
    this.stop();
    this.lastFrame = null;
    this.options = null;
  }
}
