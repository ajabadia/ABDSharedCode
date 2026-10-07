import { vi } from 'vitest';

globalThis.requestAnimationFrame = (cb) => setTimeout(cb, 16);
globalThis.cancelAnimationFrame = (id) => clearTimeout(id);
globalThis.ResizeObserver = class ResizeObserver {
  observe() {}
  disconnect() {}
  unobserve() {}
};

HTMLCanvasElement.prototype.getContext = vi.fn(() => ({
  fillRect: vi.fn(),
  clearRect: vi.fn(),
  strokeRect: vi.fn(),
  beginPath: vi.fn(),
  moveTo: vi.fn(),
  lineTo: vi.fn(),
  stroke: vi.fn(),
  fill: vi.fn(),
  arc: vi.fn(),
  fillText: vi.fn(),
  setTransform: vi.fn(),
  scale: vi.fn(),
  save: vi.fn(),
  restore: vi.fn(),
  drawImage: vi.fn(),
  createImageData: vi.fn(() => ({ data: new Uint8ClampedArray() })),
  putImageData: vi.fn(),
  createLinearGradient: vi.fn(() => ({
    addColorStop: vi.fn()
  })),
  canvas: { width: 300, height: 150, clientWidth: 300, clientHeight: 150 }
}));

HTMLElement.prototype.getBoundingClientRect = vi.fn(() => ({
  width: 300,
  height: 150,
  top: 0,
  left: 0,
  right: 300,
  bottom: 150
}));

HTMLCanvasElement.prototype.toBlob = vi.fn((cb) => cb(new Blob()));
HTMLCanvasElement.prototype.toDataURL = vi.fn(() => 'data:image/png;base64,');