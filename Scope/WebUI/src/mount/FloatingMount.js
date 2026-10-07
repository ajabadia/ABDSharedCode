/**
 * ABDScope Floating Modal Mount
 * =============================
 * Renders the scope inside a draggable, resizable, floating desktop modal with
 * close button. Hidden until open(); supports VU meters and multi-lane layouts
 * identically to the embedded mount via MountBase.
 */

import { MountBase } from './MountBase.js';
import { buildHeaderHtml, buildShellHtml } from './mountDom.js';

export class FloatingMount extends MountBase {
  constructor(options = {}) {
    super(options);
    this.isOpen = false;

    this._createDOM();
    this._initDragging();
    this._postMount();
  }

  _createDOM() {
    this.widget = document.createElement('div');
    this.widget.className = 'abd-scope-root abd-scope-floating';
    this.widget.style.display = 'none';

    const headerHtml = buildHeaderHtml({
      options: this.options,
      maxLanes: this.maxLanes,
      layout: this.layout,
      always: true,
      dragHandle: true,
      closeButton: true,
      titleFallback: 'OSCILLOSCOPE & SPECTRUM'
    });
    this.widget.innerHTML = buildShellHtml({ showVuMeters: !!this.options.showVuMeters, headerHtml });
    this.wrapper = this.widget;

    const targetContainer = this.options.containerId
      ? document.getElementById(this.options.containerId) || document.body
      : document.body;
    targetContainer.appendChild(this.widget);

    this._queryShell();
    this._bindHeaderControls();

    const closeBtn = this.widget.querySelector('.abd-scope-close-btn');
    if (closeBtn) closeBtn.onclick = () => this.close();
  }

  _initDragging() {
    const handle = this.headerElement;
    if (!handle) return;

    let startX = 0;
    let startY = 0;

    const onPointerMove = (e) => {
      const dx = e.clientX - startX;
      const dy = e.clientY - startY;
      startX = e.clientX;
      startY = e.clientY;
      this.widget.style.top = `${this.widget.offsetTop + dy}px`;
      this.widget.style.left = `${this.widget.offsetLeft + dx}px`;
      this.widget.style.right = 'auto';
    };

    const onPointerUp = () => {
      document.removeEventListener('pointermove', onPointerMove);
      document.removeEventListener('pointerup', onPointerUp);
    };

    handle.addEventListener('pointerdown', (e) => {
      if (e.target.closest('button') || e.target.closest('select')) return;
      e.preventDefault();
      startX = e.clientX;
      startY = e.clientY;
      document.addEventListener('pointermove', onPointerMove);
      document.addEventListener('pointerup', onPointerUp);
    });
  }

  _postMount() {
    this._rebuildLanes();
    this._initResizeObserver();
  }

  _isVisible() {
    return this.isOpen;
  }

  open() {
    if (this.isDestroyed) return;
    this.isOpen = true;
    this.widget.style.display = 'flex';
    this.widget.classList.add('active');

    const rect = this.bodyElement.getBoundingClientRect();
    this._dispatchResize(rect.width || 480, rect.height || 260);
  }

  close() {
    this.isOpen = false;
    if (this.widget) {
      this.widget.style.display = 'none';
      this.widget.classList.remove('active');
    }
    this.onClose();
  }

  toggle() {
    if (this.isOpen) this.close();
    else this.open();
  }

  destroy() {
    if (this.isDestroyed) return;
    this.isOpen = false;
    super.destroy();
    this.widget = null;
  }
}
