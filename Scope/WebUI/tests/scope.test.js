import { describe, it, expect, vi, beforeEach } from 'vitest';
import { createScope } from '../src/scope.js';

// Setup minimal jsdom-like DOM environment for tests
beforeEach(() => {
  document.body.innerHTML = '<div id="test-container"></div>';
});

describe('Scope Main Factory: createScope', () => {
  it('should instantiate an embedded scope with canvas and tabs', () => {
    const scope = createScope({
      containerId: 'test-container',
      enabledModes: ['oscilloscope', 'spectrum'],
      defaultMode: 'oscilloscope'
    });

    expect(scope.currentMode).toBe('oscilloscope');
    expect(scope.isDestroyed).toBe(false);

    const canvas = document.querySelector('.abd-scope-canvas');
    expect(canvas).not.toBeNull();

    const tabs = document.querySelectorAll('.abd-scope-lane-mode-btn');
    expect(tabs.length).toBe(2);

    scope.destroy();
    expect(scope.isDestroyed).toBe(true);
    expect(document.querySelector('.abd-scope-root')).toBeNull();
  });

  it('should instantiate a floating modal scope and support open/close/toggle', () => {
    const scope = createScope({
      mountMode: 'floating',
      enabledModes: ['oscilloscope', 'spectrum']
    });

    expect(scope.isOpen).toBe(false);
    scope.open();
    expect(scope.isOpen).toBe(true);

    scope.close();
    expect(scope.isOpen).toBe(false);

    scope.toggle();
    expect(scope.isOpen).toBe(true);

    scope.destroy();
    expect(scope.isDestroyed).toBe(true);
  });

  it('should register renderers and dispatch frames to the active renderer', () => {
    const scope = createScope({
      containerId: 'test-container',
      enabledModes: ['oscilloscope']
    });

    const mockRenderer = {
      init: vi.fn(),
      resize: vi.fn(),
      render: vi.fn(),
      destroy: vi.fn()
    };

    scope.registerRenderer('oscilloscope', mockRenderer);
    expect(mockRenderer.init).toHaveBeenCalledTimes(1);

    const testPacket = {
      timeDataL: new Float32Array([0.1, 0.2, 0.3]),
      sampleRate: 44100
    };

    scope.pushFrame(testPacket);
    expect(mockRenderer.render).toHaveBeenCalledTimes(1);

    scope.destroy();
    expect(mockRenderer.destroy).toHaveBeenCalledTimes(1);
  });
});
