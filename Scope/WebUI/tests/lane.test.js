import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { LaneController } from '../src/mount/LaneController.js';
import { createScope } from '../src/scope.js';

describe('Multi-Lane Architecture: LaneController', () => {
  let lane;

  beforeEach(() => {
    lane = new LaneController({
      index: 0,
      initialMode: 'oscilloscope',
      enabledModes: ['oscilloscope', 'spectrum', 'lissajous'],
      availableTaps: [
        { id: 'master', name: 'Master Out' },
        { id: 'osc1', name: 'Oscillator 1' }
      ]
    });
  });

  afterEach(() => {
    if (lane) lane.destroy();
  });

  it('should initialize DOM structure with canvas and controls', () => {
    expect(lane.rootElement).toBeDefined();
    expect(lane.rootElement.classList.contains('abd-scope-lane')).toBe(true);
    expect(lane.canvasElement).toBeDefined();
    expect(lane.getActiveMode()).toBe('oscilloscope');
    expect(lane.getActiveTap()).toBe('master');
  });

  it('should switch visual mode cleanly', () => {
    lane.setMode('spectrum');
    expect(lane.getActiveMode()).toBe('spectrum');
    expect(lane.activeRenderer).toBeDefined();
  });

  it('should switch active tap probe', () => {
    lane.setActiveTap('osc1');
    expect(lane.getActiveTap()).toBe('osc1');
  });

  it('should support toggling 1-column vs 2-column grid span', () => {
    expect(lane.getColSpan()).toBe(2);
    lane.setColSpan(1);
    expect(lane.getColSpan()).toBe(1);
    expect(lane.rootElement.getAttribute('data-col-span')).toBe('1');
    lane.setColSpan(2);
    expect(lane.getColSpan()).toBe(2);
  });

  it('should handle resize calculation without errors', () => {
    expect(() => lane.resize(400, 150, 1)).not.toThrow();
  });

  it('should freeze and unfreeze individual lane renderers', () => {
    expect(() => lane.freeze(true)).not.toThrow();
    expect(lane.isFrozen).toBe(true);
    expect(() => lane.freeze(false)).not.toThrow();
    expect(lane.isFrozen).toBe(false);
  });
});

describe('Scope Multi-Lane Orchestration in createScope', () => {
  let container;
  let scope;

  beforeEach(() => {
    container = document.createElement('div');
    container.id = 'test-scope-lane-container';
    document.body.appendChild(container);

    scope = createScope({
      containerId: 'test-scope-lane-container',
      maxLanes: 3,
      layout: 'dual',
      availableTaps: [
        { id: 'master', name: 'Master Out' },
        { id: 'osc1', name: 'Oscillator 1' }
      ]
    });
  });

  afterEach(() => {
    if (scope) scope.destroy();
    if (container) container.remove();
  });

  it('should create 2 lanes when layout is dual', () => {
    expect(scope.layout).toBe('2');
    expect(scope.mount.lanes.length).toBe(2);
  });

  it('should allow dynamic layout switching between single, dual, and triple', () => {
    scope.setLayout('1');
    expect(scope.mount.lanes.length).toBe(1);

    scope.setLayout('3');
    expect(scope.mount.lanes.length).toBe(3);

    scope.setLayout('2');
    expect(scope.mount.lanes.length).toBe(2);
  });

  it('should allow configuring individual lane modes and taps', () => {
    scope.setLaneConfig(0, { mode: 'oscilloscope', tapId: 'osc1' });
    scope.setLaneConfig(1, { mode: 'spectrum', tapId: 'master' });

    expect(scope.getLane(0).getActiveMode()).toBe('oscilloscope');
    expect(scope.getLane(0).getActiveTap()).toBe('osc1');
    expect(scope.getLane(1).getActiveMode()).toBe('spectrum');
    expect(scope.getLane(1).getActiveTap()).toBe('master');
  });

  it('should preserve existing lane modes and taps when layout changes', () => {
    scope.setLayout('1');
    scope.setLaneConfig(0, { mode: 'lissajous', tapId: 'osc1' });
    expect(scope.getLane(0).getActiveMode()).toBe('lissajous');
    expect(scope.getLane(0).getActiveTap()).toBe('osc1');

    // Switch to 3 lanes: Lane 0 must still be lissajous on osc1!
    scope.setLayout('3');
    expect(scope.getLane(0).getActiveMode()).toBe('lissajous');
    expect(scope.getLane(0).getActiveTap()).toBe('osc1');
  });

  it('should pick the first non-duplicated mode and tap when a new lane is added', () => {
    scope.setLayout('1');
    scope.setLaneConfig(0, { mode: 'lissajous', tapId: 'osc1' });

    // Expand to 2 lanes: Lane 1 should pick first unused mode ('oscilloscope') and unused tap ('master')
    scope.setLayout('2');
    expect(scope.getLane(0).getActiveMode()).toBe('lissajous');
    expect(scope.getLane(0).getActiveTap()).toBe('osc1');
    expect(scope.getLane(1).getActiveMode()).toBe('oscilloscope');
    expect(scope.getLane(1).getActiveTap()).toBe('master');
  });
});
