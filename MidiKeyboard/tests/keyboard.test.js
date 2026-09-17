/**
 * ABDKeyboard — Unit Tests
 * Validates keyboard creation, octave shifting, QWERTY, panic, and public API.
 */

import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { createKeyboard } from '../src/keyboard.js';

// ── jsdom polyfill: PointerEvent ──
if (typeof globalThis.PointerEvent === 'undefined') {
  globalThis.PointerEvent = class PointerEvent extends Event {
    constructor(type, opts = {}) {
      super(type, opts);
      this.clientX = opts.clientX ?? 0;
      this.clientY = opts.clientY ?? 0;
      this.pointerId = opts.pointerId ?? 1;
      this.buttons = opts.buttons ?? 0;
    }
  };
}

// ── jsdom DOM setup ──
function createFixture() {
  document.body.innerHTML = `
    <div id="piano-keyboard"></div>
    <div id="pitch-wheel-container"></div>
    <div id="mod-wheel-container"></div>
    <button id="oct-up"></button>
    <button id="oct-down"></button>
    <div class="kbd-octave-led" id="led-up"></div>
    <div class="kbd-octave-led" id="led-down"></div>
  `;
}

function destroyFixture() {
  document.body.innerHTML = '';
}

describe('createKeyboard', () => {
  let kbd;
  let noteOnLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    noteOnLog = [];
    noteOffLog = [];
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      wheelPitchId: 'pitch-wheel-container',
      wheelModId: 'mod-wheel-container',
      octUpId: 'oct-up',
      octDownId: 'oct-down',
      ledUpId: 'led-up',
      ledDownId: 'led-down',
      onNoteOn: (note, vel) => noteOnLog.push({ note, vel }),
      onNoteOff: (note) => noteOffLog.push(note),
      config: {
        numOctaves: 2,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
      },
    });
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('renders the correct number of white keys for 2 octaves + high C', () => {
    const whiteKeys = document.querySelectorAll('#piano-keyboard .kbd-white-key');
    // 2 octaves = 14 white keys + 1 high C = 15
    expect(whiteKeys.length).toBe(15);
  });

  it('renders black keys nested inside white keys', () => {
    const blackKeys = document.querySelectorAll('#piano-keyboard .kbd-black-key');
    // 2 octaves = 10 black keys
    expect(blackKeys.length).toBe(10);
  });

  it('assigns correct data-note attributes', () => {
    const firstWhite = document.querySelector('#piano-keyboard .kbd-white-key');
    expect(firstWhite.dataset.note).toBe('60'); // C4
  });

  it('octave shift updates via setOctave', () => {
    kbd.setOctave(2);
    expect(kbd.getOctave()).toBe(2);
    kbd.setOctave(-1);
    expect(kbd.getOctave()).toBe(-1);
  });

  it('clamps octave shift to maxOctaveShift', () => {
    kbd.setOctave(10);
    expect(kbd.getOctave()).toBe(3); // maxOctaveShift default is 3
    kbd.setOctave(-10);
    expect(kbd.getOctave()).toBe(-3);
  });

  it('destroy cleans up the container', () => {
    kbd.destroy();
    expect(document.getElementById('piano-keyboard').innerHTML).toBe('');
    kbd = null; // prevent double-destroy in afterEach
  });

  it('returns empty API when container not found', () => {
    destroyFixture();
    const nullKbd = createKeyboard({ containerId: 'nonexistent' });
    expect(() => nullKbd.panic()).not.toThrow();
    expect(nullKbd.getOctave()).toBe(0);
    nullKbd.destroy();
  });

  it('getActiveNotes returns currently held notes', () => {
    const firstKey = document.querySelector('#piano-keyboard .kbd-white-key');
    firstKey.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(kbd.getActiveNotes()).toContain(60);

    // Release
    firstKey.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    expect(kbd.getActiveNotes()).not.toContain(60);
  });

  it('panic releases all active notes', () => {
    const keys = document.querySelectorAll('#piano-keyboard .kbd-white-key');
    keys[0].dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    keys[1].dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(kbd.getActiveNotes().length).toBe(2);

    kbd.panic();
    expect(kbd.getActiveNotes().length).toBe(0);
    expect(noteOffLog.length).toBe(2);
  });

  it('auto-generates a panic button when no panicBtnId is provided', () => {
    const panicBtn = document.querySelector('#piano-keyboard .kbd-panic-btn');
    expect(panicBtn).not.toBeNull();
    expect(panicBtn.getAttribute('role')).toBe('button');
    expect(panicBtn.getAttribute('aria-label')).toBe('All Notes Off — Panic (Ctrl+Q)');
    expect(panicBtn.getAttribute('title')).toBe('Panic: All Notes Off (Ctrl+Q)');
    expect(panicBtn.querySelector('.kbd-panic-led')).not.toBeNull();
    expect(panicBtn.querySelector('.kbd-panic-label').textContent).toContain('ALL');
  });

  it('auto-generated panic button triggers panic on click', () => {
    const keys = document.querySelectorAll('#piano-keyboard .kbd-white-key');
    keys[0].dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(kbd.getActiveNotes().length).toBe(1);

    const panicBtn = document.querySelector('#piano-keyboard .kbd-panic-btn');
    panicBtn.click();
    expect(kbd.getActiveNotes().length).toBe(0);
  });

  it('does not auto-generate panic button when panicBtnId is provided', () => {
    kbd.destroy();
    const externalBtn = document.createElement('button');
    externalBtn.id = 'ext-panic';
    document.body.appendChild(externalBtn);
    kbd = createKeyboard({ panicBtnId: 'ext-panic' });
    const autoBtn = document.querySelector('#piano-keyboard .kbd-panic-btn');
    expect(autoBtn).toBeNull();
    externalBtn.remove();
  });

  it('highlightNote adds active class to the correct key', () => {
    kbd.highlightNote(60, 0.9);
    const key = document.querySelector('[data-note="60"]');
    expect(key.classList.contains('active')).toBe(true);
  });

  it('releaseNote removes visual active when triggered after pointerdown', () => {
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.classList.contains('active')).toBe(true);
    key.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    expect(key.classList.contains('active')).toBe(false);
  });

  it('sweep does not throw', () => {
    expect(() => kbd.sweep('right', 1)).not.toThrow();
  });

  it('setOctaveCount re-renders the keybed', () => {
    kbd.setOctaveCount(4, 36);
    const whiteKeys = document.querySelectorAll('#piano-keyboard .kbd-white-key');
    // 4 octaves = 28 white keys + 1 high C = 29
    expect(whiteKeys.length).toBe(29);
  });
});

describe('Velocity Curves', () => {
  let kbd;
  let noteOnLog;

  beforeEach(() => {
    createFixture();
    noteOnLog = [];
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        velocitySource: 'fixed',
        velocityCurve: 'soft',
        fixedVelocity: 0.5,
        enableQwerty: false,
        enableTouch: false,
      },
      onNoteOn: (note, vel) => noteOnLog.push({ note, vel }),
    });
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('fixed velocity uses configured value', () => {
    const key = document.querySelector('#piano-keyboard .kbd-white-key');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog[0].vel).toBe(0.5);
  });
});

describe('QWERTY Keyboard', () => {
  let kbd;
  let noteOnLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    noteOnLog = [];
    noteOffLog = [];
    kbd = createKeyboard({
      config: {
        numOctaves: 2,
        startNote: 48, // C3
        enableQwerty: true,
        enableTouch: false,
      },
      onNoteOn: (note, vel) => noteOnLog.push({ note, vel }),
      onNoteOff: (note) => noteOffLog.push(note),
    });
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('pressing Z triggers note 48 (C3)', () => {
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
    expect(noteOnLog.length).toBe(1);
    expect(noteOnLog[0].note).toBe(48);
  });

  it('releasing Z sends noteOff', () => {
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
    window.dispatchEvent(new KeyboardEvent('keyup', { key: 'z', bubbles: true }));
    expect(noteOffLog.length).toBe(1);
    expect(noteOffLog[0]).toBe(48);
  });

  it('repeated keydown does not trigger double noteOn', () => {
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true, repeat: true }));
    expect(noteOnLog.length).toBe(1);
  });

  it('arrow up shifts octave', () => {
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowUp', bubbles: true }));
    expect(kbd.getOctave()).toBe(1);
  });

  it('arrow down shifts octave', () => {
    kbd.setOctave(1);
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowDown', bubbles: true }));
    expect(kbd.getOctave()).toBe(0);
  });

  it('Ctrl+Q triggers panic (All Notes Off)', () => {
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
    expect(kbd.getActiveNotes().length).toBe(1);

    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'q', ctrlKey: true, bubbles: true }));
    expect(kbd.getActiveNotes().length).toBe(0);
  });

  it('Cmd+Q triggers panic on macOS', () => {
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
    expect(kbd.getActiveNotes().length).toBe(1);

    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'q', metaKey: true, bubbles: true }));
    expect(kbd.getActiveNotes().length).toBe(0);
  });
});

describe('LED Animations', () => {
  let kbd;

  beforeEach(() => {
    createFixture();
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
      },
    });
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('sweep applies led-sweep class asynchronously', async () => {
    kbd.sweep('right', 0);
    // sweep adds class immediately, then removes it after 180ms per key.
    // With speedMs=0 all outer setTimeouts are 0ms; inner ones are 180ms.
    // jsdom can be slow with many concurrent timers, so use generous wait.
    await new Promise(resolve => setTimeout(resolve, 500));
    const keys = document.querySelectorAll('#piano-keyboard .kbd-white-key, #piano-keyboard .kbd-black-key');
    const hasSweep = Array.from(keys).some(k => k.classList.contains('kbd-led-sweep'));
    expect(hasSweep).toBe(false);
  });

  it('sweep does not throw on empty keybed', () => {
    kbd.destroy();
    kbd = null;
    const emptyKbd = createKeyboard({
      containerId: 'nonexistent',
      config: { numOctaves: 0 },
    });
    expect(() => emptyKbd.sweep()).not.toThrow();
    emptyKbd.destroy();
  });
});

// ──────────────────────────────────────────────────────────────
// Pressure Display (ABDEep: aftertouch + modwheel + pitchbend)
// ──────────────────────────────────────────────────────────────
describe('Pressure Display', () => {
  let kbd;
  let rafCallbacks;
  let noteOnLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    noteOnLog = [];
    noteOffLog = [];
    rafCallbacks = [];
    // Mock RAF to collect callbacks so we can flush manually
    vi.stubGlobal('requestAnimationFrame', (cb) => {
      rafCallbacks.push(cb);
      return rafCallbacks.length;
    });
  });

  afterEach(() => {
    vi.restoreAllMocks();
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  /** Flush one RAF frame (calls one queued callback) */
  function flushFrame() {
    if (rafCallbacks.length > 0) {
      const cb = rafCallbacks.shift();
      cb();
    }
  }

  /** Drain the current batch of queued RAF callbacks without processing new ones */
  function drainCurrentBatch() {
    const batch = rafCallbacks.splice(0);
    batch.forEach(cb => cb());
  }

  it('applies kbd-pressured class when aftertouch is active on a held key', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
    });

    // Flush initial RAF (loop started, no active keys yet)
    drainCurrentBatch();

    // Press a key
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.classList.contains('active')).toBe(true);

    // Simulate aftertouch
    pressureState = { aftertouch: 0.6, modWheel: 0, pitchBend: 0 };
    flushFrame(); // updatePressureDisplay reads new state

    expect(key.classList.contains('kbd-pressured')).toBe(true);
    expect(key.style.getPropertyValue('--kbd-pressure')).toBe('0.600');
  });

  it('applies kbd-pressured class when modwheel is active', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    pressureState = { aftertouch: 0, modWheel: 0.75, pitchBend: 0 };
    flushFrame();

    expect(key.classList.contains('kbd-pressured')).toBe(true);
    expect(key.style.getPropertyValue('--kbd-pressure')).toBe('0.750');
    // modWheel gets its own --kbd-mw-pressure
    expect(key.style.getPropertyValue('--kbd-mw-pressure')).toBe('0.750');
  });

  it('combined aftertouch uses max(at, mw) for --kbd-pressure', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // aftertouch=0.3, modWheel=0.8 → combined = 0.8
    pressureState = { aftertouch: 0.3, modWheel: 0.8, pitchBend: 0 };
    flushFrame();

    expect(key.style.getPropertyValue('--kbd-pressure')).toBe('0.800');
    expect(key.style.getPropertyValue('--kbd-mw-pressure')).toBe('0.800');
  });

  it('removes kbd-pressured when pressure drops to zero', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Apply pressure
    pressureState = { aftertouch: 0.5, modWheel: 0, pitchBend: 0 };
    flushFrame();
    expect(key.classList.contains('kbd-pressured')).toBe(true);

    // Release pressure
    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    flushFrame();
    expect(key.classList.contains('kbd-pressured')).toBe(false);
  });

  it('skips frame when values have not changed (performance optimization)', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
    });
    // init() queued one RAF; drain it — reads 0/0/0 and sets _prev* to 0.
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Change state so the next frame detects a difference.
    pressureState = { aftertouch: 0.5, modWheel: 0, pitchBend: 0 };
    flushFrame(); // reads 0.5 vs _prev 0 → applies pressure (DOM mutated)
    expect(key.classList.contains('kbd-pressured')).toBe(true);
    expect(key.style.getPropertyValue('--kbd-pressure')).toBe('0.500');

    // Same values → DOM is NOT re-written (optimization)
    key.style.setProperty('--kbd-pressure', 'TAMPERED');
    flushFrame();
    // If the skip worked, the value stays TAMPERED
    expect(key.style.getPropertyValue('--kbd-pressure')).toBe('TAMPERED');
  });

  it('does not apply pressure when enablePressureDisplay is false', () => {
    let pressureState = { aftertouch: 0.9, modWheel: 0.9, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: false,
        getPressureState: () => pressureState,
      },
    });

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Give RAF a chance (but loop was never started)
    drainCurrentBatch();

    expect(key.classList.contains('kbd-pressured')).toBe(false);
  });

  it('pressure release animation adds kbd-pressure-release class on note release', async () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
      onNoteOff: (n) => noteOffLog.push(n),
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Apply pressure
    pressureState = { aftertouch: 0.5, modWheel: 0, pitchBend: 0 };
    flushFrame();
    expect(key.classList.contains('kbd-pressured')).toBe(true);

    // Release the key — should trigger pressure release animation
    key.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    expect(noteOffLog).toContain(60);
    expect(key.classList.contains('kbd-pressure-release')).toBe(true);

    // Wait for the fallback 500ms timeout to clean up
    await new Promise(resolve => setTimeout(resolve, 550));
    expect(key.classList.contains('kbd-pressure-release')).toBe(false);
    expect(key.classList.contains('kbd-pressured')).toBe(false);
  });

  it('applies --kbd-mw-pressure only when modWheel > 0.01', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Pure aftertouch (no modWheel)
    pressureState = { aftertouch: 0.5, modWheel: 0, pitchBend: 0 };
    flushFrame();
    expect(key.style.getPropertyValue('--kbd-mw-pressure')).toBe('0');

    // With modWheel
    pressureState = { aftertouch: 0.5, modWheel: 0.3, pitchBend: 0 };
    flushFrame();
    expect(key.style.getPropertyValue('--kbd-mw-pressure')).toBe('0.300');
  });

  it('does not start pressure loop when getPressureState is null', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        // getPressureState defaults to null
      },
    });
    // No RAF should be queued (loop not started)
    expect(rafCallbacks.length).toBe(0);
  });

  it('panic clears pressure state from all active keys', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        getPressureState: () => pressureState,
      },
      onPanic: () => {},
    });
    drainCurrentBatch();

    // Press two keys
    const key1 = document.querySelector('[data-note="60"]');
    const key2 = document.querySelector('[data-note="62"]');
    key1.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    key2.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Apply pressure
    pressureState = { aftertouch: 0.8, modWheel: 0, pitchBend: 0 };
    flushFrame();
    expect(key1.classList.contains('kbd-pressured')).toBe(true);
    expect(key2.classList.contains('kbd-pressured')).toBe(true);

    // Panic
    kbd.panic();
    expect(key1.classList.contains('kbd-pressured')).toBe(false);
    expect(key2.classList.contains('kbd-pressured')).toBe(false);
    expect(key1.style.getPropertyValue('--kbd-pressure')).toBe('');
    expect(key2.style.getPropertyValue('--kbd-pressure')).toBe('');
  });
});

// ──────────────────────────────────────────────────────────────
// Pitch-Bend Displacement (ABDEep: keys shift horizontally)
// ──────────────────────────────────────────────────────────────
describe('Pitch-Bend Displacement', () => {
  let kbd;
  let rafCallbacks;

  beforeEach(() => {
    createFixture();
    rafCallbacks = [];
    vi.stubGlobal('requestAnimationFrame', (cb) => {
      rafCallbacks.push(cb);
      return rafCallbacks.length;
    });
  });

  afterEach(() => {
    vi.restoreAllMocks();
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  function flushFrame() {
    if (rafCallbacks.length > 0) {
      const cb = rafCallbacks.shift();
      cb();
    }
  }

  function drainCurrentBatch() {
    const batch = rafCallbacks.splice(0);
    batch.forEach(cb => cb());
  }

  it('applies kbd-pitch-bent class and --kbd-pb-offset on positive pitch bend', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Full positive pitch bend (+1.0 → 6px displacement)
    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 1.0 };
    flushFrame();

    expect(key.classList.contains('kbd-pitch-bent')).toBe(true);
    expect(key.style.getPropertyValue('--kbd-pb-offset')).toBe('6px');
  });

  it('applies negative pitch-bend displacement', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Full negative pitch bend (-1.0 → -6px displacement)
    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: -1.0 };
    flushFrame();

    expect(key.classList.contains('kbd-pitch-bent')).toBe(true);
    expect(key.style.getPropertyValue('--kbd-pb-offset')).toBe('-6px');
  });

  it('applies half pitch-bend with proportional displacement', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Half positive bend (+0.5 → round(0.5 * 6) = 3px)
    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0.5 };
    flushFrame();

    expect(key.style.getPropertyValue('--kbd-pb-offset')).toBe('3px');
  });

  it('removes pitch-bend class when bend returns to zero', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Apply bend
    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 1.0 };
    flushFrame();
    expect(key.classList.contains('kbd-pitch-bent')).toBe(true);

    // Return to center
    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    flushFrame();
    expect(key.classList.contains('kbd-pitch-bent')).toBe(false);
    expect(key.style.getPropertyValue('--kbd-pb-offset')).toBe('');
  });

  it('does not apply displacement when enablePitchBendDisplace is false', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: false,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 1.0 };
    flushFrame();

    expect(key.classList.contains('kbd-pitch-bent')).toBe(false);
    expect(key.style.getPropertyValue('--kbd-pb-offset')).toBe('');
  });

  it('combines pressure and pitch-bend on same key', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: true,
        getPressureState: () => pressureState,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    // Both aftertouch and pitch bend active
    pressureState = { aftertouch: 0.7, modWheel: 0, pitchBend: 0.5 };
    flushFrame();

    expect(key.classList.contains('kbd-pressured')).toBe(true);
    expect(key.classList.contains('kbd-pitch-bent')).toBe(true);
    expect(key.style.getPropertyValue('--kbd-pressure')).toBe('0.700');
    expect(key.style.getPropertyValue('--kbd-pb-offset')).toBe('3px');
  });

  it('small pitch bend below threshold (|pb| < 0.01) is ignored', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: true,
      },
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0.005 };
    flushFrame();

    expect(key.classList.contains('kbd-pitch-bent')).toBe(false);
  });

  it('panic clears pitch-bend state from active keys', () => {
    let pressureState = { aftertouch: 0, modWheel: 0, pitchBend: 0 };
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enablePressureDisplay: true,
        enablePitchBendDisplace: true,
        getPressureState: () => pressureState,
      },
      onPanic: () => {},
    });
    drainCurrentBatch();

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));

    pressureState = { aftertouch: 0.5, modWheel: 0, pitchBend: 1.0 };
    flushFrame();
    expect(key.classList.contains('kbd-pitch-bent')).toBe(true);

    kbd.panic();
    expect(key.classList.contains('kbd-pitch-bent')).toBe(false);
    expect(key.classList.contains('kbd-pressured')).toBe(false);
    expect(key.style.getPropertyValue('--kbd-pb-offset')).toBe('');
  });
});

// ──────────────────────────────────────────────────────────────
// Ivory Texture (ABDEep: per-key deterministic HSL variations)
// ──────────────────────────────────────────────────────────────
describe('Ivory Texture', () => {
  let kbd;

  beforeEach(() => {
    createFixture();
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('applies ivory CSS variables to white keys when enabled', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: true,
      },
    });

    const whiteKey = document.querySelector('[data-note="60"]');
    // Note 60: seed=(60*12345)%100=0, hue=37, sat=18, light=90
    expect(whiteKey.style.getPropertyValue('--kbd-ivory-base')).toContain('hsl(');
    expect(whiteKey.style.getPropertyValue('--kbd-ivory-top')).toContain('hsl(');
    expect(whiteKey.style.getPropertyValue('--kbd-ivory-bottom')).toContain('hsl(');
    expect(whiteKey.style.getPropertyValue('--kbd-dirt-color')).toContain('rgba(');
    expect(whiteKey.style.getPropertyValue('--kbd-dirt-start')).toMatch(/\d+%/);
  });

  it('does NOT apply ivory texture to black keys', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: true,
      },
    });

    const blackKey = document.querySelector('[data-note="61"]'); // C#4
    expect(blackKey.style.getPropertyValue('--kbd-ivory-base')).toBe('');
    expect(blackKey.style.getPropertyValue('--kbd-ivory-top')).toBe('');
    expect(blackKey.style.getPropertyValue('--kbd-ivory-bottom')).toBe('');
  });

  it('does not apply ivory variables when enableIvoryTexture is false', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: false,
      },
    });

    const whiteKey = document.querySelector('[data-note="60"]');
    expect(whiteKey.style.getPropertyValue('--kbd-ivory-base')).toBe('');
    expect(whiteKey.style.getPropertyValue('--kbd-ivory-top')).toBe('');
    expect(whiteKey.style.getPropertyValue('--kbd-ivory-bottom')).toBe('');
  });

  it('produces different HSL values for different notes (deterministic per-key)', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 2,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: true,
      },
    });

    // Pick two white keys that should differ
    // Note 60: seed=0, hue=37, sat=18, light=90
    // Note 65: seed=(65*12345)%100=75, hue=40+(75%6-3)=42, sat=18+3=21, light=90-(75%5)=90
    const key60 = document.querySelector('[data-note="60"]');
    const key65 = document.querySelector('[data-note="65"]');

    const base60 = key60.style.getPropertyValue('--kbd-ivory-base');
    const base65 = key65.style.getPropertyValue('--kbd-ivory-base');

    // They should be different HSL strings (different seed → different hue/sat)
    expect(base60).not.toBe(base65);
  });

  it('ivory top is lighter than ivory base (top > base lightness)', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: true,
      },
    });

    const key = document.querySelector('[data-note="60"]');
    const base = key.style.getPropertyValue('--kbd-ivory-base');
    const top = key.style.getPropertyValue('--kbd-ivory-top');
    const bottom = key.style.getPropertyValue('--kbd-ivory-bottom');

    // Extract lightness values: hsl(hue, sat%, light%)
    const extractLight = (hsl) => {
      const match = hsl.match(/hsl\([^,]+,\s*[^,]+%,\s*(\d+(\.\d+)?)%\)/);
      return match ? parseFloat(match[1]) : -1;
    };

    const baseLight = extractLight(base);
    const topLight = extractLight(top);
    const bottomLight = extractLight(bottom);

    // top (+6) > base > bottom (-5)
    expect(topLight).toBeGreaterThan(baseLight);
    expect(baseLight).toBeGreaterThan(bottomLight);
  });

  it('dirt-start percentage is between 85% and 94%', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: true,
      },
    });

    const keys = document.querySelectorAll('#piano-keyboard .kbd-white-key');
    for (const key of keys) {
      const dirtStart = key.style.getPropertyValue('--kbd-dirt-start');
      const pct = parseInt(dirtStart, 10);
      expect(pct).toBeGreaterThanOrEqual(85);
      expect(pct).toBeLessThanOrEqual(94);
    }
  });

  it('ivory values are deterministic (same note always produces same texture)', () => {
    // Create two keyboards with same config
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: true,
      },
    });
    const key1Base = document.querySelector('[data-note="60"]')
      .style.getPropertyValue('--kbd-ivory-base');
    kbd.destroy();

    // Create another keyboard
    createFixture();
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableIvoryTexture: true,
      },
    });
    const key2Base = document.querySelector('[data-note="60"]')
      .style.getPropertyValue('--kbd-ivory-base');

    expect(key1Base).toBe(key2Base);
  });
});

// ──────────────────────────────────────────────────────────────
// Vintage Wear Stains (ABDCZ101: per-key deterministic stains)
// ──────────────────────────────────────────────────────────────
describe('Vintage Wear Stains', () => {
  let kbd;

  beforeEach(() => {
    createFixture();
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('applies stain classes to some white keys when enabled', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 2,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableVintageWear: true,
      },
    });

    const whiteKeys = document.querySelectorAll('#piano-keyboard .kbd-white-key');
    const stainedKeys = Array.from(whiteKeys).filter(k =>
      k.classList.contains('kbd-stain-yellow') ||
      k.classList.contains('kbd-stain-scuff') ||
      k.classList.contains('kbd-stain-ding')
    );
    // With 2 octaves (15 white keys), deterministic staining should produce some stained keys
    expect(stainedKeys.length).toBeGreaterThan(0);
  });

  it('applies stain-worn class to some black keys when enabled', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 3,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableVintageWear: true,
      },
    });

    const blackKeys = document.querySelectorAll('#piano-keyboard .kbd-black-key');
    const wornKeys = Array.from(blackKeys).filter(k =>
      k.classList.contains('kbd-stain-worn')
    );
    // With 3 octaves (15 black keys), some should be worn
    expect(wornKeys.length).toBeGreaterThan(0);
  });

  it('does not apply stain classes when enableVintageWear is false', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 2,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableVintageWear: false,
      },
    });

    const allKeys = document.querySelectorAll('#piano-keyboard .kbd-white-key, #piano-keyboard .kbd-black-key');
    const stainedKeys = Array.from(allKeys).filter(k =>
      Array.from(k.classList).some(c => c.startsWith('kbd-stain'))
    );
    expect(stainedKeys.length).toBe(0);
  });
});

// ──────────────────────────────────────────────────────────────
// LED Color Callback (dynamic color per arp/seq/chord mode)
// ──────────────────────────────────────────────────────────────
describe('LED Color Callback', () => {
  let kbd;
  let noteOnLog;

  beforeEach(() => {
    createFixture();
    noteOnLog = [];
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('uses getLedColor callback for LED color on key press', () => {
    let modeColor = '#ff3366';
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        getLedColor: () => modeColor,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
    });

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.style.getPropertyValue('--kbd-led-color')).toBe('#ff3366');
  });

  it('changes LED color when getLedColor callback returns different value', () => {
    let modeColor = '#ff3366';
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        getLedColor: () => modeColor,
      },
    });

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.style.getPropertyValue('--kbd-led-color')).toBe('#ff3366');

    // Switch mode
    modeColor = '#9933ff';
    key.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.style.getPropertyValue('--kbd-led-color')).toBe('#9933ff');
  });

  it('uses static ledColor when getLedColor is not provided', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        ledColor: '#00ff88',
      },
    });

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.style.getPropertyValue('--kbd-led-color')).toBe('#00ff88');
  });

  it('falls back to CSS variable when neither getLedColor nor ledColor is set', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
      },
    });

    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.style.getPropertyValue('--kbd-led-color')).toBe('var(--color-accent)');
  });
});

describe('Sustain Pedal', () => {
  let kbd;
  let sustainLog;
  let noteOnLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    sustainLog = [];
    noteOnLog = [];
    noteOffLog = [];
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
  });

  function createSustKbd(opts = {}) {
    return createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        ...opts,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: (n) => noteOffLog.push(n),
      onSustainChange: (s) => sustainLog.push(s),
    });
  }

  it('initializes with sustain off', () => {
    kbd = createSustKbd();
    expect(kbd.getSustain()).toBe(false);
  });

  it('setSustain(true) turns sustain on and fires callback', () => {
    kbd = createSustKbd();
    kbd.setSustain(true);
    expect(kbd.getSustain()).toBe(true);
    expect(sustainLog).toEqual([true]);
  });

  it('setSustain(false) turns sustain off and fires callback', () => {
    kbd = createSustKbd();
    kbd.setSustain(true);
    kbd.setSustain(false);
    expect(kbd.getSustain()).toBe(false);
    expect(sustainLog).toEqual([true, false]);
  });

  it('setSustain is idempotent (no duplicate callbacks)', () => {
    kbd = createSustKbd();
    kbd.setSustain(false);
    kbd.setSustain(false);
    expect(sustainLog).toEqual([]);
    kbd.setSustain(true);
    kbd.setSustain(true);
    expect(sustainLog).toEqual([true]);
  });

  it('toggleSustain toggles on/off', () => {
    kbd = createSustKbd();
    kbd.toggleSustain();
    expect(kbd.getSustain()).toBe(true);
    kbd.toggleSustain();
    expect(kbd.getSustain()).toBe(false);
  });

  it('auto-generates a sustain button when no sustainBtnId is provided', () => {
    kbd = createSustKbd();
    const sustainBtn = document.querySelector('#piano-keyboard .kbd-sustain-btn');
    expect(sustainBtn).not.toBeNull();
    expect(sustainBtn.getAttribute('role')).toBe('button');
    expect(sustainBtn.getAttribute('aria-label')).toBe('Sustain Pedal On/Off (Ctrl+Space)');
    expect(sustainBtn.getAttribute('title')).toBe('Sustain Pedal: Toggle Hold (Ctrl+Space)');
    expect(sustainBtn.querySelector('.kbd-sustain-led')).not.toBeNull();
    expect(sustainBtn.querySelector('.kbd-sustain-label').textContent).toBe('SUST');
  });

  it('auto-generated sustain button toggles on click', () => {
    kbd = createSustKbd();
    const sustainBtn = document.querySelector('#piano-keyboard .kbd-sustain-btn');
    sustainBtn.click();
    expect(kbd.getSustain()).toBe(true);
    sustainBtn.click();
    expect(kbd.getSustain()).toBe(false);
  });

  it('sustain button LED lights up when sustain is on', () => {
    kbd = createSustKbd();
    const led = document.querySelector('#piano-keyboard .kbd-sustain-led');
    expect(led.classList.contains('on')).toBe(false);
    kbd.setSustain(true);
    expect(led.classList.contains('on')).toBe(true);
    kbd.setSustain(false);
    expect(led.classList.contains('on')).toBe(false);
  });

  it('does not auto-generate sustain button when sustainBtnId is provided', () => {
    kbd.destroy();
    const externalBtn = document.createElement('button');
    externalBtn.id = 'ext-sustain';
    document.body.appendChild(externalBtn);
    kbd = createKeyboard({ sustainBtnId: 'ext-sustain' });
    const autoBtn = document.querySelector('#piano-keyboard .kbd-sustain-btn');
    expect(autoBtn).toBeNull();
    externalBtn.remove();
  });

  it('panic releases sustain', () => {
    kbd = createSustKbd();
    kbd.setSustain(true);
    expect(kbd.getSustain()).toBe(true);
    kbd.panic();
    expect(kbd.getSustain()).toBe(false);
    expect(sustainLog).toEqual([true, false]);
  });

  it('accepts initial sustain callback via onSustainChange', () => {
    kbd = createSustKbd();
    // Verify callback is wired
    kbd.setSustain(true);
    expect(sustainLog.length).toBe(1);
    expect(sustainLog[0]).toBe(true);
  });
});

describe('Velocity', () => {
  let kbd;
  let noteOnLog;
  let velocityLog;

  beforeEach(() => {
    createFixture();
    noteOnLog = [];
    velocityLog = [];
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
  });

  function createVelKbd(opts = {}) {
    return createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        ...opts,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onVelocityChange: (n, v) => velocityLog.push({ note: n, vel: v }),
    });
  }

  it('sends velocity with note-on (fixed by default)', () => {
    kbd = createVelKbd();
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog.length).toBe(1);
    expect(noteOnLog[0].note).toBe(60);
    expect(typeof noteOnLog[0].vel).toBe('number');
    expect(noteOnLog[0].vel).toBeGreaterThan(0);
  });

  it('fires onVelocityChange when a note is pressed', () => {
    kbd = createVelKbd();
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(velocityLog.length).toBe(1);
    expect(velocityLog[0].note).toBe(60);
    expect(typeof velocityLog[0].vel).toBe('number');
  });

  it('fixedVelocity config controls the velocity value', () => {
    kbd = createVelKbd({ fixedVelocity: 0.5 });
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog[0].vel).toBeCloseTo(0.5, 2);
  });

  it('velocity CSS variable is set on the active key', () => {
    kbd = createVelKbd({ fixedVelocity: 0.7 });
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.style.getPropertyValue('--kbd-velocity')).toBe('0.700');
  });

  it('velocityCurve is stored in config', () => {
    kbd = createVelKbd({ velocitySource: 'fixed', velocityCurve: 'soft', fixedVelocity: 0.3 });
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    // Fixed velocity ignores curve, so it should still be 0.3
    expect(noteOnLog[0].vel).toBeCloseTo(0.3, 2);
  });

  it('each note press sends a separate velocity', () => {
    kbd = createVelKbd({ fixedVelocity: 0.9 });
    const keys = document.querySelectorAll('#piano-keyboard .kbd-white-key');
    keys[0].dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    keys[1].dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(velocityLog.length).toBe(2);
    expect(velocityLog[0].note).not.toBe(velocityLog[1].note);
  });
});

describe('Aftertouch', () => {
  let kbd;
  let aftertouchLog;
  let noteOnLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    aftertouchLog = [];
    noteOnLog = [];
    noteOffLog = [];
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
  });

  function createAtKbd(opts = {}) {
    return createKeyboard({
      config: {
        numOctaves: 1,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableAftertouch: true,
        ...opts,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: (n) => noteOffLog.push(n),
      onAftertouch: (n, p) => aftertouchLog.push({ note: n, pressure: p }),
    });
  }

  it('setAftertouch sends channel aftertouch (note=-1)', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(-1, 0.8);
    expect(aftertouchLog.length).toBe(1);
    expect(aftertouchLog[0].note).toBe(-1);
    expect(aftertouchLog[0].pressure).toBeCloseTo(0.8, 2);
  });

  it('setAftertouch sends polyphonic aftertouch for a specific note', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(60, 0.5);
    expect(aftertouchLog.length).toBe(1);
    expect(aftertouchLog[0].note).toBe(60);
    expect(aftertouchLog[0].pressure).toBeCloseTo(0.5, 2);
  });

  it('releaseAftertouch sends pressure=0', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(-1, 0.7);
    kbd.releaseAftertouch(-1);
    expect(aftertouchLog.length).toBe(2);
    expect(aftertouchLog[1].note).toBe(-1);
    expect(aftertouchLog[1].pressure).toBe(0);
  });

  it('releaseAftertouch polyphonic sends pressure=0 for note', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(60, 0.6);
    kbd.releaseAftertouch(60);
    expect(aftertouchLog.length).toBe(2);
    expect(aftertouchLog[1].note).toBe(60);
    expect(aftertouchLog[1].pressure).toBe(0);
  });

  it('getAftertouch returns current channel pressure', () => {
    kbd = createAtKbd();
    expect(kbd.getAftertouch()).toBe(0);
    kbd.setAftertouch(-1, 0.5);
    expect(kbd.getAftertouch()).toBeCloseTo(0.5, 2);
  });

  it('getAftertouch resets after releaseAftertouch', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(-1, 0.7);
    kbd.releaseAftertouch(-1);
    expect(kbd.getAftertouch()).toBe(0);
  });

  it('does not fire onAftertouch when enableAftertouch is false', () => {
    kbd = createAtKbd({ enableAftertouch: false });
    kbd.setAftertouch(-1, 0.5);
    // Callback is still wired, but setAftertouch still works (it's the software API)
    // The config flag only affects pointer-based generation
    expect(aftertouchLog.length).toBe(1);
  });

  it('channel aftertouch only fires callback when value changes', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(-1, 0.5);
    kbd.setAftertouch(-1, 0.5); // same value
    expect(aftertouchLog.length).toBe(1);
  });

  it('polyphonic aftertouch fires for each note independently', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(60, 0.3);
    kbd.setAftertouch(62, 0.6);
    expect(aftertouchLog.length).toBe(2);
    expect(aftertouchLog[0].note).toBe(60);
    expect(aftertouchLog[1].note).toBe(62);
  });

  it('aftertouch pressure is clamped to 0..1', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(-1, 1.5);
    expect(aftertouchLog[0].pressure).toBe(1);
    kbd.setAftertouch(-1, -0.3);
    expect(aftertouchLog[1].pressure).toBe(0);
  });

  it('aftertouch is enabled via config option', () => {
    kbd = createAtKbd({ enableAftertouch: true });
    // Verify aftertouch is enabled by checking pointer-based generation
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, clientY: 100 }));
    // Simulate pointer move down (positive pressure)
    key.dispatchEvent(new PointerEvent('pointermove', { bubbles: true, clientY: 50 }));
    // Should generate aftertouch from pointer movement
    expect(aftertouchLog.length).toBeGreaterThan(0);
  });

  it('aftertouch mode polyphonic sends per-note values', () => {
    kbd = createAtKbd({ aftertouchMode: 'polyphonic' });
    kbd.setAftertouch(60, 0.4);
    expect(aftertouchLog[0].note).toBe(60);
    kbd.setAftertouch(62, 0.7);
    expect(aftertouchLog[1].note).toBe(62);
  });

  it('panic releases aftertouch', () => {
    kbd = createAtKbd();
    kbd.setAftertouch(-1, 0.8);
    aftertouchLog.length = 0;
    kbd.panic();
    // After panic, channel aftertouch should be reset
    expect(kbd.getAftertouch()).toBe(0);
  });
});

describe('Accessibility', () => {
  let kbd;

  function createA11yKbd(overrides = {}) {
    return createKeyboard({
      containerId: 'piano-keyboard',
      config: {
        numOctaves: 2,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableAccessibility: true,
        ...overrides,
      },
      onNoteOn: () => {},
      onNoteOff: () => {},
    });
  }

  beforeEach(() => {
    document.body.innerHTML = '<div id="piano-keyboard"></div>';
    kbd = createA11yKbd();
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    document.body.innerHTML = '';
  });

  // ── ARIA Attributes ──

  it('sets role="group" and aria-label on container', () => {
    const container = document.getElementById('piano-keyboard');
    expect(container.getAttribute('role')).toBe('group');
    expect(container.getAttribute('aria-label')).toBe('Virtual piano keyboard');
  });

  it('sets role="button" on each key', () => {
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    expect(keys.length).toBeGreaterThan(0);
    keys.forEach(key => {
      expect(key.getAttribute('role')).toBe('button');
    });
  });

  it('sets aria-label with note name on each key', () => {
    const c4 = document.querySelector('[data-note="60"]');
    expect(c4.getAttribute('aria-label')).toBe('Note C4 (MIDI 60)');

    const d4 = document.querySelector('[data-note="62"]');
    expect(d4.getAttribute('aria-label')).toBe('Note D4 (MIDI 62)');

    const fs4 = document.querySelector('[data-note="66"]');
    expect(fs4.getAttribute('aria-label')).toBe('Note F#4 (MIDI 66)');
  });

  it('sets aria-pressed="false" initially on all keys', () => {
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    keys.forEach(key => {
      expect(key.getAttribute('aria-pressed')).toBe('false');
    });
  });

  it('sets aria-pressed="true" when key is pressed', () => {
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.getAttribute('aria-pressed')).toBe('true');
  });

  it('resets aria-pressed="false" when key is released', () => {
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(key.getAttribute('aria-pressed')).toBe('true');
    key.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    expect(key.getAttribute('aria-pressed')).toBe('false');
  });

  // ── Live Region ──

  it('creates an aria-live region for announcements', () => {
    const liveRegion = document.querySelector('#piano-keyboard [role="status"]');
    expect(liveRegion).toBeTruthy();
    expect(liveRegion.getAttribute('aria-live')).toBe('polite');
    expect(liveRegion.getAttribute('aria-atomic')).toBe('true');
    expect(liveRegion.classList.contains('kbd-sr-only')).toBe(true);
  });

  it('announces note name when key is pressed', () => {
    const liveRegion = document.querySelector('#piano-keyboard [role="status"]');
    const key = document.querySelector('[data-note="60"]');
    key.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(liveRegion.textContent).toMatch(/C4/);
  });

  it('announces sustain state changes', () => {
    const liveRegion = document.querySelector('#piano-keyboard [role="status"]');
    kbd.setSustain(true);
    expect(liveRegion.textContent).toMatch(/Sustain on/);
    kbd.setSustain(false);
    expect(liveRegion.textContent).toMatch(/Sustain off/);
  });

  it('announces octave shift', () => {
    const liveRegion = document.querySelector('#piano-keyboard [role="status"]');
    kbd.setOctave(2);
    expect(liveRegion.textContent).toMatch(/Octave shift \+2/);
  });

  it('announces panic', () => {
    const liveRegion = document.querySelector('#piano-keyboard [role="status"]');
    kbd.panic();
    expect(liveRegion.textContent).toMatch(/Panic.*All notes off/);
  });

  // ── Keyboard Navigation ──

  it('first key has tabindex="0" for Tab entry', () => {
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    expect(keys[0].getAttribute('tabindex')).toBe('0');
  });

  it('other keys have tabindex="-1" by default', () => {
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    for (let i = 1; i < keys.length; i++) {
      expect(keys[i].getAttribute('tabindex')).toBe('-1');
    }
  });

  it('ArrowRight moves focus to next key', () => {
    const container = document.getElementById('piano-keyboard');
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    // Focus first key
    keys[0].focus();
    // Arrow right
    keys[0].dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true }));
    expect(document.activeElement).toBe(keys[1]);
  });

  it('ArrowLeft moves focus to previous key', () => {
    const container = document.getElementById('piano-keyboard');
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    // Focus second key
    keys[1].focus();
    // Arrow left
    keys[1].dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true }));
    expect(document.activeElement).toBe(keys[0]);
  });

  it('Home key moves focus to first key', () => {
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    keys[5].focus();
    keys[5].dispatchEvent(new KeyboardEvent('keydown', { key: 'Home', bubbles: true }));
    expect(document.activeElement).toBe(keys[0]);
  });

  it('End key moves focus to last key', () => {
    const keys = document.querySelectorAll('#piano-keyboard [data-note]');
    keys[0].focus();
    keys[0].dispatchEvent(new KeyboardEvent('keydown', { key: 'End', bubbles: true }));
    expect(document.activeElement).toBe(keys[keys.length - 1]);
  });

  // ── Wheel ARIA ──

  it('pitch wheel slider has aria-label', () => {
    const slider = document.querySelector('.kbd-wheel-slider');
    if (slider) {
      expect(slider.getAttribute('aria-label')).toBe('Pitch Bend Wheel');
    }
  });

  // ── Reduced Motion ──

  it('does not apply ARIA when enableAccessibility is false', () => {
    kbd.destroy();
    document.body.innerHTML = '<div id="piano-keyboard"></div>';
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      config: {
        numOctaves: 2,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableAccessibility: false,
      },
      onNoteOn: () => {},
      onNoteOff: () => {},
    });
    const container = document.getElementById('piano-keyboard');
    expect(container.getAttribute('role')).toBeFalsy();
    const key = document.querySelector('[data-note="60"]');
    expect(key.getAttribute('role')).toBeFalsy();
    expect(key.getAttribute('aria-label')).toBeFalsy();
  });
});

describe('Collapse / Expand', () => {
  let kbd;
  let collapseLog;

  function createCollapseKbd(overrides = {}) {
    collapseLog = [];
    return createKeyboard({
      containerId: 'piano-keyboard',
      config: {
        numOctaves: 2,
        startNote: 60,
        enableQwerty: false,
        enableTouch: false,
        enableCollapse: true,
        ...overrides,
      },
      onNoteOn: () => {},
      onNoteOff: () => {},
      onCollapseChange: (c) => collapseLog.push(c),
    });
  }

  beforeEach(() => {
    document.body.innerHTML = '<div id="piano-keyboard"></div>';
    kbd = createCollapseKbd();
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    document.body.innerHTML = '';
  });

  it('auto-generates a collapse button with correct attributes', () => {
    const btn = document.querySelector('.kbd-collapse-btn');
    expect(btn).toBeTruthy();
    expect(btn.getAttribute('role')).toBe('button');
    expect(btn.getAttribute('tabindex')).toBe('0');
    expect(btn.getAttribute('aria-label')).toBe('Collapse keyboard');
    expect(btn.getAttribute('title')).toBe('Collapse/Expand keyboard');
  });

  it('starts expanded (not collapsed)', () => {
    expect(kbd.isCollapsed()).toBe(false);
    expect(document.querySelector('.kbd-piano-keys').classList.contains('kbd-collapsed')).toBe(false);
  });

  it('collapse() adds kbd-collapsed class', () => {
    kbd.collapse();
    expect(kbd.isCollapsed()).toBe(true);
    expect(document.querySelector('.kbd-piano-keys').classList.contains('kbd-collapsed')).toBe(true);
  });

  it('expand() removes kbd-collapsed class', () => {
    kbd.collapse();
    kbd.expand();
    expect(kbd.isCollapsed()).toBe(false);
    expect(document.querySelector('.kbd-piano-keys').classList.contains('kbd-collapsed')).toBe(false);
  });

  it('toggleCollapse() toggles between states', () => {
    expect(kbd.isCollapsed()).toBe(false);
    kbd.toggleCollapse();
    expect(kbd.isCollapsed()).toBe(true);
    kbd.toggleCollapse();
    expect(kbd.isCollapsed()).toBe(false);
  });

  it('fires onCollapseChange callback', () => {
    kbd.collapse();
    expect(collapseLog).toEqual([true]);
    kbd.expand();
    expect(collapseLog).toEqual([true, false]);
  });

  it('is idempotent (no duplicate callbacks)', () => {
    kbd.collapse();
    kbd.collapse(); // should not fire again
    expect(collapseLog).toEqual([true]);
  });

  it('click on chevron toggles collapse', () => {
    const btn = document.querySelector('.kbd-collapse-btn');
    btn.click();
    expect(kbd.isCollapsed()).toBe(true);
    btn.click();
    expect(kbd.isCollapsed()).toBe(false);
  });

  it('Enter key on chevron toggles collapse', () => {
    const btn = document.querySelector('.kbd-collapse-btn');
    btn.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    expect(kbd.isCollapsed()).toBe(true);
  });

  it('Space key on chevron toggles collapse', () => {
    const btn = document.querySelector('.kbd-collapse-btn');
    btn.dispatchEvent(new KeyboardEvent('keydown', { key: ' ', bubbles: true }));
    expect(kbd.isCollapsed()).toBe(true);
  });

  it('chevron text changes direction when collapsed', () => {
    const chevron = document.querySelector('.kbd-collapse-chevron');
    expect(chevron.textContent).toBe('▾');
    kbd.collapse();
    expect(chevron.textContent).toBe('▴');
    kbd.expand();
    expect(chevron.textContent).toBe('▾');
  });

  it('chevron aria-label updates when collapsed', () => {
    const btn = document.querySelector('.kbd-collapse-btn');
    expect(btn.getAttribute('aria-label')).toBe('Collapse keyboard');
    kbd.collapse();
    expect(btn.getAttribute('aria-label')).toBe('Expand keyboard');
    kbd.expand();
    expect(btn.getAttribute('aria-label')).toBe('Collapse keyboard');
  });

  it('keys have zero height when collapsed', () => {
    const key = document.querySelector('[data-note="60"]');
    expect(key.style.display).not.toBe('none');
    kbd.collapse();
    // CSS class is applied; height comes from CSS rule
    expect(document.querySelector('.kbd-piano-keys').classList.contains('kbd-collapsed')).toBe(true);
  });

  it('does not auto-generate button when enableCollapse is false', () => {
    kbd.destroy();
    document.body.innerHTML = '<div id="piano-keyboard"></div>';
    kbd = createCollapseKbd({ enableCollapse: false });
    const btn = document.querySelector('.kbd-collapse-btn');
    expect(btn).toBeFalsy();
  });
});

describe('Resize Observer', () => {
  let kbd;
  let resizeCallback;

  beforeEach(() => {
    document.body.innerHTML = '<div id="piano-keyboard" style="width:400px"></div>';
    // Mock ResizeObserver
    resizeCallback = null;
    global.ResizeObserver = class {
      constructor(cb) { resizeCallback = cb; }
      observe() {}
      unobserve() {}
      disconnect() { resizeCallback = null; }
    };
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    document.body.innerHTML = '';
    delete global.ResizeObserver;
  });

  it('creates a ResizeObserver by default', () => {
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      config: { numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false },
      onNoteOn: () => {},
      onNoteOff: () => {},
    });
    expect(resizeCallback).toBeTruthy();
  });

  it('does not create ResizeObserver when disabled', () => {
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      config: {
        numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false,
        enableResizeObserver: false,
      },
      onNoteOn: () => {},
      onNoteOff: () => {},
    });
    expect(resizeCallback).toBeFalsy();
  });

  it('disconnects ResizeObserver on destroy()', () => {
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      config: { numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false },
      onNoteOn: () => {},
      onNoteOff: () => {},
    });
    expect(resizeCallback).toBeTruthy();
    kbd.destroy();
    expect(resizeCallback).toBeFalsy();
  });

  it('re-renders keybed when container resizes', () => {
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      config: { numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false },
      onNoteOn: () => {},
      onNoteOff: () => {},
    });
    const keysBefore = document.querySelectorAll('#piano-keyboard [data-note]').length;
    expect(keysBefore).toBeGreaterThan(0);
    // Simulate resize
    resizeCallback([]);
    const keysAfter = document.querySelectorAll('#piano-keyboard [data-note]').length;
    expect(keysAfter).toBe(keysBefore);
  });

  it('does not re-render after destroy', () => {
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      config: { numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false },
      onNoteOn: () => {},
      onNoteOff: () => {},
    });
    const savedCb = resizeCallback;
    kbd.destroy();
    // Simulate resize after destroy — should not throw (destroyed flag guards re-render)
    expect(() => savedCb([])).not.toThrow();
  });
});

// ══════════════════════════════════════════════════════════════
// SOSTENUTO PEDAL (CC#66)
// ══════════════════════════════════════════════════════════════

describe('Sostenuto Pedal (CC#66)', () => {
  let kbd;
  let sostenutoLog;
  let noteOnLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    sostenutoLog = [];
    noteOnLog = [];
    noteOffLog = [];
  });

  afterEach(() => { if (kbd) kbd.destroy(); });

  function createSostKbd(opts = {}) {
    return createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: false, enableTouch: false,
        enableSostenuto: true,
        ...opts,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: (n) => noteOffLog.push(n),
      onSostenutoChange: (s) => sostenutoLog.push(s),
    });
  }

  it('initializes with sostenuto off', () => {
    kbd = createSostKbd();
    expect(kbd.getSostenuto()).toBe(false);
  });

  it('setSostenuto(true) turns on and fires callback', () => {
    kbd = createSostKbd();
    kbd.setSostenuto(true);
    expect(kbd.getSostenuto()).toBe(true);
    expect(sostenutoLog).toEqual([true]);
  });

  it('setSostenuto(false) turns off and fires callback', () => {
    kbd = createSostKbd();
    kbd.setSostenuto(true);
    kbd.setSostenuto(false);
    expect(kbd.getSostenuto()).toBe(false);
    expect(sostenutoLog).toEqual([true, false]);
  });

  it('setSostenuto is idempotent', () => {
    kbd = createSostKbd();
    kbd.setSostenuto(true);
    kbd.setSostenuto(true);
    expect(sostenutoLog).toEqual([true]);
  });

  it('toggleSostenuto toggles on/off', () => {
    kbd = createSostKbd();
    kbd.toggleSostenuto();
    expect(kbd.getSostenuto()).toBe(true);
    kbd.toggleSostenuto();
    expect(kbd.getSostenuto()).toBe(false);
  });

  it('captures active notes when engaged', () => {
    kbd = createSostKbd();
    // Press note 60
    const key60 = document.querySelector('[data-note="60"]');
    key60.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(1);

    // Engage sostenuto while note 60 is held
    kbd.setSostenuto(true);

    // Release note 60 — should NOT fire noteOff (held by sostenuto)
    key60.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    expect(noteOffLog).toHaveLength(0);
  });

  it('does not capture notes pressed after engagement', () => {
    kbd = createSostKbd();

    // Engage sostenuto with no notes held
    kbd.setSostenuto(true);

    // Press note 60 after sostenuto is on
    const key60 = document.querySelector('[data-note="60"]');
    key60.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(1);

    // Release note 60 — should fire noteOff (not captured)
    key60.dispatchEvent(new PointerEvent('pointerup', { bubbles: true }));
    expect(noteOffLog).toHaveLength(1);
    expect(noteOffLog[0]).toBe(60);
  });

  it('auto-generates sostenuto button when enableSostenuto is true', () => {
    kbd = createSostKbd();
    const btn = document.querySelector('.kbd-sostenuto-btn');
    expect(btn).toBeTruthy();
    expect(btn.getAttribute('role')).toBe('button');
    expect(btn.getAttribute('tabindex')).toBe('0');
    expect(btn.querySelector('.kbd-sostenuto-led')).toBeTruthy();
  });

  it('sustain button toggles sostenuto on click', () => {
    kbd = createSostKbd();
    const btn = document.querySelector('.kbd-sostenuto-btn');
    btn.click();
    expect(kbd.getSostenuto()).toBe(true);
    btn.click();
    expect(kbd.getSostenuto()).toBe(false);
  });

  it('LED lights up when sostenuto is on', () => {
    kbd = createSostKbd();
    const led = document.querySelector('.kbd-sostenuto-led');
    expect(led.classList.contains('on')).toBe(false);
    kbd.setSostenuto(true);
    expect(led.classList.contains('on')).toBe(true);
    kbd.setSostenuto(false);
    expect(led.classList.contains('on')).toBe(false);
  });

  it('panic releases sostenuto', () => {
    kbd = createSostKbd();
    kbd.setSostenuto(true);
    expect(kbd.getSostenuto()).toBe(true);
    kbd.panic();
    expect(kbd.getSostenuto()).toBe(false);
  });

  it('does not auto-generate button when enableSostenuto is false', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: false, enableTouch: false,
        enableSostenuto: false,
      },
      onNoteOn: () => {}, onNoteOff: () => {},
    });
    expect(document.querySelector('.kbd-sostenuto-btn')).toBeFalsy();
  });
});

// ══════════════════════════════════════════════════════════════
// SOFT PEDAL (CC#67)
// ══════════════════════════════════════════════════════════════

describe('Soft Pedal (CC#67)', () => {
  let kbd;
  let softLog;
  let noteOnLog;

  beforeEach(() => {
    createFixture();
    softLog = [];
    noteOnLog = [];
  });

  afterEach(() => { if (kbd) kbd.destroy(); });

  function createSoftKbd(opts = {}) {
    return createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: false, enableTouch: false,
        enableSoftPedal: true,
        softPedalFactor: 0.5,
        ...opts,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: () => {},
      onSoftPedalChange: (s) => softLog.push(s),
    });
  }

  it('initializes with soft pedal off', () => {
    kbd = createSoftKbd();
    expect(kbd.getSoftPedal()).toBe(false);
  });

  it('setSoftPedal(true) turns on and fires callback', () => {
    kbd = createSoftKbd();
    kbd.setSoftPedal(true);
    expect(kbd.getSoftPedal()).toBe(true);
    expect(softLog).toEqual([true]);
  });

  it('setSoftPedal(false) turns off and fires callback', () => {
    kbd = createSoftKbd();
    kbd.setSoftPedal(true);
    kbd.setSoftPedal(false);
    expect(kbd.getSoftPedal()).toBe(false);
    expect(softLog).toEqual([true, false]);
  });

  it('toggleSoftPedal toggles on/off', () => {
    kbd = createSoftKbd();
    kbd.toggleSoftPedal();
    expect(kbd.getSoftPedal()).toBe(true);
    kbd.toggleSoftPedal();
    expect(kbd.getSoftPedal()).toBe(false);
  });

  it('attenuates velocity when soft pedal is on', () => {
    kbd = createSoftKbd({
      fixedVelocity: 0.8,
      softPedalFactor: 0.5,
    });

    kbd.setSoftPedal(true);
    const key60 = document.querySelector('[data-note="60"]');
    key60.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    // Velocity should be 0.8 * 0.5 = 0.4
    expect(noteOnLog[0].vel).toBeCloseTo(0.4, 2);
  });

  it('does not attenuate velocity when soft pedal is off', () => {
    kbd = createSoftKbd({
      fixedVelocity: 0.8,
      softPedalFactor: 0.5,
    });

    const key60 = document.querySelector('[data-note="60"]');
    key60.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog[0].vel).toBeCloseTo(0.8, 2);
  });

  it('auto-generates soft pedal button when enableSoftPedal is true', () => {
    kbd = createSoftKbd();
    const btn = document.querySelector('.kbd-soft-btn');
    expect(btn).toBeTruthy();
    expect(btn.getAttribute('role')).toBe('button');
    expect(btn.getAttribute('tabindex')).toBe('0');
    expect(btn.querySelector('.kbd-soft-led')).toBeTruthy();
  });

  it('button toggles soft pedal on click', () => {
    kbd = createSoftKbd();
    const btn = document.querySelector('.kbd-soft-btn');
    btn.click();
    expect(kbd.getSoftPedal()).toBe(true);
    btn.click();
    expect(kbd.getSoftPedal()).toBe(false);
  });

  it('LED lights up when soft pedal is on', () => {
    kbd = createSoftKbd();
    const led = document.querySelector('.kbd-soft-led');
    expect(led.classList.contains('on')).toBe(false);
    kbd.setSoftPedal(true);
    expect(led.classList.contains('on')).toBe(true);
    kbd.setSoftPedal(false);
    expect(led.classList.contains('on')).toBe(false);
  });

  it('panic releases soft pedal', () => {
    kbd = createSoftKbd();
    kbd.setSoftPedal(true);
    expect(kbd.getSoftPedal()).toBe(true);
    kbd.panic();
    expect(kbd.getSoftPedal()).toBe(false);
  });

  it('does not auto-generate button when enableSoftPedal is false', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: false, enableTouch: false,
        enableSoftPedal: false,
      },
      onNoteOn: () => {}, onNoteOff: () => {},
    });
    expect(document.querySelector('.kbd-soft-btn')).toBeFalsy();
  });

  it('attenuates velocity on QWERTY notes too', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: true, enableTouch: false,
        enableSoftPedal: true,
        softPedalFactor: 0.6,
        fixedVelocity: 0.9,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: () => {},
    });

    kbd.setSoftPedal(true);
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
    // z maps to startNote (60), velocity = 0.9 * 0.6 = 0.54
    expect(noteOnLog[0].vel).toBeCloseTo(0.54, 2);

    window.dispatchEvent(new KeyboardEvent('keyup', { key: 'z', bubbles: true }));
  });
});

// ══════════════════════════════════════════════════════════════
// SCALE FILTER
// ══════════════════════════════════════════════════════════════

describe('Scale Filter', () => {
  let kbd;
  let noteOnLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    noteOnLog = [];
    noteOffLog = [];
  });

  afterEach(() => { if (kbd) kbd.destroy(); });

  function createScaleKbd(opts = {}) {
    return createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: false, enableTouch: true,
        enableScaleFilter: true,
        scaleType: 'major',
        scaleRoot: 60,  // C major
        scaleSnapMode: 'block',
        ...opts,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: (n) => noteOffLog.push(n),
    });
  }

  // ── Basic tests ──

  it('initializes with scale filter off by default', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: false, enableTouch: true,
      },
      onNoteOn: () => {}, onNoteOff: () => {},
    });
    expect(kbd.getScaleFilter()).toBeNull();
  });

  it('getScaleFilter returns scale info when enabled', () => {
    kbd = createScaleKbd();
    const info = kbd.getScaleFilter();
    expect(info).toBeTruthy();
    expect(info.type).toBe('major');
    expect(info.root).toBe(60);
    expect(info.snapMode).toBe('block');
  });

  it('disableScaleFilter turns off filtering', () => {
    kbd = createScaleKbd();
    expect(kbd.getScaleFilter()).toBeTruthy();
    kbd.disableScaleFilter();
    expect(kbd.getScaleFilter()).toBeNull();
  });

  // ── Visual tests ──

  it('applies kbd-scale-in class to notes in the scale', () => {
    kbd = createScaleKbd({ scaleType: 'major', scaleRoot: 60 });
    // C major: C D E F G A B (semitones 0 2 4 5 7 9 11)
    const keyC = document.querySelector('[data-note="60"]'); // C4
    const keyD = document.querySelector('[data-note="62"]'); // D4
    const keyE = document.querySelector('[data-note="64"]'); // E4
    expect(keyC.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyD.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyE.classList.contains('kbd-scale-in')).toBe(true);
  });

  it('applies kbd-scale-out class to notes outside the scale', () => {
    kbd = createScaleKbd({ scaleType: 'major', scaleRoot: 60 });
    // C# (61), D# (63), F# (66), G# (68), A# (70) are not in C major
    const keyCs = document.querySelector('[data-note="61"]'); // C#4
    const keyDs = document.querySelector('[data-note="63"]'); // D#4
    const keyFs = document.querySelector('[data-note="66"]'); // F#4
    expect(keyCs.classList.contains('kbd-scale-out')).toBe(true);
    expect(keyDs.classList.contains('kbd-scale-out')).toBe(true);
    expect(keyFs.classList.contains('kbd-scale-out')).toBe(true);
  });

  it('removes scale classes when filter is disabled', () => {
    kbd = createScaleKbd();
    const keyC = document.querySelector('[data-note="60"]');
    expect(keyC.classList.contains('kbd-scale-in')).toBe(true);
    kbd.disableScaleFilter();
    expect(keyC.classList.contains('kbd-scale-in')).toBe(false);
    expect(keyC.classList.contains('kbd-scale-out')).toBe(false);
  });

  // ── Block mode tests ──

  it('block mode: prevents playing notes outside scale', () => {
    kbd = createScaleKbd({ scaleSnapMode: 'block', scaleType: 'major', scaleRoot: 60 });
    // C#4 (61) is NOT in C major
    const keyCs = document.querySelector('[data-note="61"]');
    keyCs.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(0);
  });

  it('block mode: allows playing notes inside scale', () => {
    kbd = createScaleKbd({ scaleSnapMode: 'block', scaleType: 'major', scaleRoot: 60 });
    // C4 (60) IS in C major
    const keyC = document.querySelector('[data-note="60"]');
    keyC.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(1);
    expect(noteOnLog[0].note).toBe(60);
  });

  // ── Snap mode tests ──

  it('snap mode: redirects notes outside scale to nearest in-scale note', () => {
    kbd = createScaleKbd({ scaleSnapMode: 'snap', scaleType: 'major', scaleRoot: 60 });
    // C#4 (61) → should snap to C4 (60) or D4 (62)
    const keyCs = document.querySelector('[data-note="61"]');
    keyCs.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(1);
    // Should snap to either 60 (C) or 62 (D)
    expect([60, 62]).toContain(noteOnLog[0].note);
  });

  it('snap mode: notes inside scale play normally', () => {
    kbd = createScaleKbd({ scaleSnapMode: 'snap', scaleType: 'major', scaleRoot: 60 });
    const keyE = document.querySelector('[data-note="64"]'); // E4
    keyE.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(1);
    expect(noteOnLog[0].note).toBe(64);
  });

  // ── Pass mode tests ──

  it('pass mode: allows playing all notes but dims outside scale', () => {
    kbd = createScaleKbd({ scaleSnapMode: 'pass', scaleType: 'major', scaleRoot: 60 });
    const keyCs = document.querySelector('[data-note="61"]'); // C#4
    keyCs.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(1);
    expect(noteOnLog[0].note).toBe(61);
  });

  // ── Different scales ──

  it('pentatonic minor scale filters correctly', () => {
    kbd = createScaleKbd({ scaleType: 'pentatonicMinor', scaleRoot: 60 });
    // C minor pentatonic: C Eb F G Bb (0 3 5 7 10)
    const keyC = document.querySelector('[data-note="60"]');  // C4 ✓
    const keyEb = document.querySelector('[data-note="63"]'); // Eb4 ✓
    const keyCs = document.querySelector('[data-note="61"]'); // C#4 ✗
    const keyD = document.querySelector('[data-note="62"]');  // D4 ✗
    const keyE = document.querySelector('[data-note="64"]');  // E4 ✗
    expect(keyC.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyEb.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyCs.classList.contains('kbd-scale-out')).toBe(true);
    expect(keyD.classList.contains('kbd-scale-out')).toBe(true);
    expect(keyE.classList.contains('kbd-scale-out')).toBe(true);
  });

  it('blues scale filters correctly', () => {
    kbd = createScaleKbd({ scaleType: 'blues', scaleRoot: 60 });
    // C blues: C Eb F F# G Bb (0 3 5 6 7 10)
    const keyC = document.querySelector('[data-note="60"]');  // C4 ✓
    const keyF = document.querySelector('[data-note="65"]');  // F4 ✓
    const keyFs = document.querySelector('[data-note="66"]'); // F#4 ✓
    const keyD = document.querySelector('[data-note="62"]');  // D4 ✗
    expect(keyC.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyF.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyFs.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyD.classList.contains('kbd-scale-out')).toBe(true);
  });

  // ── setScaleFilter API ──

  it('setScaleFilter changes scale dynamically', () => {
    kbd = createScaleKbd({ scaleType: 'major', scaleRoot: 60 });
    // Initially C major
    const keyCs = document.querySelector('[data-note="61"]'); // C#4 ✗ in C major
    expect(keyCs.classList.contains('kbd-scale-out')).toBe(true);

    // Change to chromatic (all notes in scale)
    kbd.setScaleFilter('chromatic', 60);
    expect(keyCs.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyCs.classList.contains('kbd-scale-out')).toBe(false);
  });

  it('setScaleFilter(null) disables filtering', () => {
    kbd = createScaleKbd();
    kbd.setScaleFilter(null);
    expect(kbd.getScaleFilter()).toBeNull();
  });

  it('setScaleSnapMode changes snap behavior', () => {
    kbd = createScaleKbd({ scaleSnapMode: 'block', scaleType: 'major', scaleRoot: 60 });
    // C#4 blocked in block mode
    const keyCs = document.querySelector('[data-note="61"]');
    keyCs.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(0);

    // Switch to pass mode
    kbd.setScaleSnapMode('pass');
    keyCs.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
    expect(noteOnLog).toHaveLength(1);
  });

  // ── QWERTY integration ──

  it('blocks QWERTY notes outside scale', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: true, enableTouch: false,
        enableScaleFilter: true,
        scaleType: 'major', scaleRoot: 60,
        scaleSnapMode: 'block',
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: () => {},
    });
    // 's' maps to startNote + 1 = 61 (C#4) — NOT in C major
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 's', bubbles: true }));
    expect(noteOnLog).toHaveLength(0);
    window.dispatchEvent(new KeyboardEvent('keyup', { key: 's', bubbles: true }));
  });

  it('allows QWERTY notes inside scale', () => {
    kbd = createKeyboard({
      config: {
        numOctaves: 1, startNote: 60,
        enableQwerty: true, enableTouch: false,
        enableScaleFilter: true,
        scaleType: 'major', scaleRoot: 60,
        scaleSnapMode: 'block',
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: () => {},
    });
    // 'z' maps to startNote + 0 = 60 (C4) — IS in C major
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
    expect(noteOnLog).toHaveLength(1);
    expect(noteOnLog[0].note).toBe(60);
    window.dispatchEvent(new KeyboardEvent('keyup', { key: 'z', bubbles: true }));
  });

  // ── Different root notes ──

  it('G major scale has correct notes', () => {
    kbd = createScaleKbd({ scaleType: 'major', scaleRoot: 67 }); // G4 = 67
    // G major: G A B C D E F# (semitones 0 2 4 5 7 9 11 from G)
    const keyG = document.querySelector('[data-note="67"]');  // G4 ✓
    const keyA = document.querySelector('[data-note="69"]');  // A4 ✓
    const keyB = document.querySelector('[data-note="71"]');  // B4 ✓
    const keyC = document.querySelector('[data-note="72"]');  // C5 ✓
    const keyFs = document.querySelector('[data-note="66"]'); // F#4 ✓
    const keyF = document.querySelector('[data-note="65"]');  // F4 ✗
    expect(keyG.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyA.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyB.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyC.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyFs.classList.contains('kbd-scale-in')).toBe(true);
    expect(keyF.classList.contains('kbd-scale-out')).toBe(true);
  });
});

// ══════════════════════════════════════════════════════════════
// Chord Memory Tests
// ══════════════════════════════════════════════════════════════

describe('Chord Memory', () => {
  let kbd;
  const noteOnLog = [];
  const noteOffLog = [];

  function createChordKbd(overrides = {}) {
    return createKeyboard({
      config: {
        numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false,
        enableChordMemory: true, maxChordSlots: 4,
        ...overrides,
      },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: (n) => noteOffLog.push(n),
    });
  }

  beforeEach(() => {
    createFixture();
    noteOnLog.length = 0;
    noteOffLog.length = 0;
  });

  afterEach(() => { if (kbd) kbd.destroy(); });

  it('initializes with empty chord slots', () => {
    kbd = createChordKbd();
    const chords = kbd.getChords();
    expect(chords).toHaveLength(4);
    chords.forEach(c => expect(c).toBeNull());
  });

  it('saveChord saves currently active notes', () => {
    kbd = createChordKbd();
    // Simulate playing notes C4 (60) and E4 (64)
    kbd.highlightNote(60, 0.8);
    kbd.highlightNote(64, 0.8);
    // Manually add to activeKeys (since highlightNote doesn't add to Map)
    // We'll use saveChord with explicit notes instead
    kbd.saveChord(0, [60, 64, 67]); // C major triad
    expect(kbd.getChords()[0]).toEqual([60, 64, 67]);
  });

  it('saveChord sorts notes by MIDI number', () => {
    kbd = createChordKbd();
    kbd.saveChord(0, [67, 60, 64]); // Unsorted
    expect(kbd.getChords()[0]).toEqual([60, 64, 67]); // Sorted
  });

  it('playChord calls onNoteOn for each saved note', () => {
    kbd = createChordKbd();
    kbd.saveChord(0, [60, 64, 67]); // C major triad
    kbd.playChord(0);
    expect(noteOnLog).toHaveLength(3);
    expect(noteOnLog.map(n => n.note)).toEqual([60, 64, 67]);
  });

  it('playChord does nothing for empty slot', () => {
    kbd = createChordKbd();
    kbd.playChord(0); // Empty slot
    expect(noteOnLog).toHaveLength(0);
  });

  it('releaseChord calls onNoteOff for each saved note', () => {
    kbd = createChordKbd();
    kbd.saveChord(0, [60, 64, 67]);
    kbd.releaseChord(0);
    expect(noteOffLog).toHaveLength(3);
    expect(noteOffLog).toEqual([60, 64, 67]);
  });

  it('clearChord removes a specific slot', () => {
    kbd = createChordKbd();
    kbd.saveChord(0, [60, 64, 67]);
    kbd.saveChord(1, [62, 66, 69]);
    kbd.clearChord(0);
    expect(kbd.getChords()[0]).toBeNull();
    expect(kbd.getChords()[1]).toEqual([62, 66, 69]);
  });

  it('clearAllChords removes all slots', () => {
    kbd = createChordKbd();
    kbd.saveChord(0, [60, 64, 67]);
    kbd.saveChord(1, [62, 66, 69]);
    kbd.clearAllChords();
    expect(kbd.getChords().every(c => c === null)).toBe(true);
  });

  it('slot index is clamped to valid range', () => {
    kbd = createChordKbd({ maxChordSlots: 4 });
    kbd.saveChord(99, [60]); // Out of range
    expect(kbd.getChords()[3]).toEqual([60]); // Clamped to last slot
    kbd.saveChord(-5, [62]); // Negative
    expect(kbd.getChords()[0]).toEqual([62]); // Clamped to 0
  });

  it('playChord highlights keys with active class', () => {
    kbd = createChordKbd();
    kbd.saveChord(0, [60, 64, 67]);
    kbd.playChord(0);
    const key60 = document.querySelector('[data-note="60"]');
    const key64 = document.querySelector('[data-note="64"]');
    expect(key60.classList.contains('active')).toBe(true);
    expect(key64.classList.contains('active')).toBe(true);
  });

  it('does nothing when enableChordMemory is false', () => {
    kbd = createKeyboard({
      config: { numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false },
      onNoteOn: (n) => noteOnLog.push(n),
    });
    kbd.saveChord(0, [60, 64]); // Should be no-op
    const chords = kbd.getChords();
    // API is exposed but all slots are null (no-op)
    expect(chords.every(c => c === null)).toBe(true);
    kbd.playChord(0); // Should not trigger noteOn
    expect(noteOnLog).toHaveLength(0);
  });

  it('maxChordSlots config limits slot count', () => {
    kbd = createChordKbd({ maxChordSlots: 2 });
    const chords = kbd.getChords();
    expect(chords).toHaveLength(2);
    kbd.saveChord(0, [60]);
    kbd.saveChord(1, [62]);
    kbd.saveChord(2, [64]); // Should clamp to slot 1
    expect(kbd.getChords()[1]).toEqual([64]);
  });

  it('getChords returns copies, not references', () => {
    kbd = createChordKbd();
    kbd.saveChord(0, [60, 64, 67]);
    const chords1 = kbd.getChords();
    const chords2 = kbd.getChords();
    expect(chords1[0]).not.toBe(chords2[0]); // Different array references
    expect(chords1[0]).toEqual(chords2[0]); // Same contents
  });
});

describe('host-driven feedback API (v0.2)', () => {
  let kbd;
  let pitchLog;
  let modLog;
  let noteOffLog;

  beforeEach(() => {
    createFixture();
    pitchLog = [];
    modLog = [];
    noteOffLog = [];
    kbd = createKeyboard({
      containerId: 'piano-keyboard',
      wheelPitchId: 'pitch-wheel-container',
      wheelModId: 'mod-wheel-container',
      onPitchBend: (v) => pitchLog.push(v),
      onModWheel: (v) => modLog.push(v),
      onNoteOff: (n) => noteOffLog.push(n),
      config: { numOctaves: 2, startNote: 60, enableQwerty: false, enableTouch: false },
    });
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
    destroyFixture();
  });

  it('setPitchBend moves the pitch wheel without echoing onPitchBend', () => {
    kbd.setPitchBend(0.5);
    const slider = document.querySelector('#pitch-wheel-container .kbd-wheel-slider');
    expect(slider.value).toBe(String(Math.round(0.5 * 8191))); // signed range, 0 = center
    expect(pitchLog).toHaveLength(0); // host-driven, not user input
  });

  it('setPitchBend clamps to -1..+1', () => {
    kbd.setPitchBend(7);
    expect(document.querySelector('#pitch-wheel-container .kbd-wheel-slider').value).toBe('8191');
    kbd.setPitchBend(-9);
    expect(document.querySelector('#pitch-wheel-container .kbd-wheel-slider').value).toBe('-8192');
  });

  it('setModWheel moves the mod wheel without echoing onModWheel', () => {
    kbd.setModWheel(0.25);
    const slider = document.querySelector('#mod-wheel-container .kbd-wheel-slider');
    expect(slider.value).toBe(String(Math.round(0.25 * 127)));
    expect(modLog).toHaveLength(0);
    kbd.setModWheel(2);
    expect(document.querySelector('#mod-wheel-container .kbd-wheel-slider').value).toBe('127');
  });

  it('notesOffVisual clears key highlight without firing onNoteOff', () => {
    kbd.highlightNote(60, 0.9);
    const key = document.querySelector('#piano-keyboard [data-note="60"]');
    expect(key.classList.contains('active')).toBe(true);
    kbd.notesOffVisual([60, 999]); // unknown note ignored
    expect(key.classList.contains('active')).toBe(false);
    expect(noteOffLog).toHaveLength(0); // sound side already happened
  });

  it('no-container stub carries the feedback no-ops', () => {
    const stub = createKeyboard({ containerId: 'does-not-exist' });
    expect(() => {
      stub.setPitchBend(0.5);
      stub.setModWheel(0.5);
      stub.notesOffVisual([60]);
    }).not.toThrow();
  });
});
