/**
 * ABDScope Embedded Mount
 * =======================
 * Embeds the scope component directly within a designated DOM container element.
 * All multi-lane orchestration is inherited from MountBase.
 */

import { MountBase } from './MountBase.js';
import { buildHeaderHtml, buildShellHtml } from './mountDom.js';

export class EmbeddedMount extends MountBase {
  constructor(container, options = {}) {
    super(options);

    this.container = typeof container === 'string'
      ? document.getElementById(container)
      : container;

    if (!this.container) {
      throw new Error(`[ABDScope:EmbeddedMount] Container element not found: "${container}"`);
    }

    this._createDOM();
    this._postMount();
  }

  _createDOM() {
    this.wrapper = document.createElement('div');
    this.wrapper.className = 'abd-scope-root abd-scope-embedded';

    const headerHtml = buildHeaderHtml({ options: this.options, maxLanes: this.maxLanes, layout: this.layout });
    this.wrapper.innerHTML = buildShellHtml({ showVuMeters: !!this.options.showVuMeters, headerHtml });

    this.container.appendChild(this.wrapper);
    this._queryShell();
    this._bindHeaderControls();
  }

  /** MountBase hook: rebuild lanes + resize observer after the DOM is ready. */
  _postMount() {
    this._rebuildLanes();
    this._initResizeObserver();
  }

  destroy() {
    if (this.isDestroyed) return;
    super.destroy();
    this.container = null;
  }
}
