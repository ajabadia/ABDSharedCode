/**
 * ABDKeyboard — ABDCZ101 Bridge Integration Tests
 * Validates the CZ-101 preset works correctly with bridge-style callbacks.
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { createKeyboard, CZ101_PRESET } from '../src/keyboard.js';

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

class CZ101Bridge {
  constructor() {
    this.events = [];
    this.noteMap = new Map();
    this.sustainState = false;
  }

  noteOn(note, velocity) {
    this.events.push({ type: 'noteOn', note, velocity, timestamp: Date.now() });
    this.noteMap.set(note, { velocity });
  }

  noteOff(note) {
    this.events.push({ type: 'noteOff', note, timestamp: Date.now() });
    this.noteMap.delete(note);
  }

  allNotesOff() {
    this.events.push({ type: 'allNotesOff', timestamp: Date.now() });
    this.noteMap.clear();
  }

  sustain(on) {
    this.events.push({ type: 'sustain', on, timestamp: Date.now() });
    this.sustainState = on;
  }

  pitchBend(value) {
    this.events.push({ type: 'pitchBend', value, timestamp: Date.now() });
  }

  modWheel(value) {
    this.events.push({ type: 'modWheel', value, timestamp: Date.now() });
  }

  getNotes() { return Array.from(this.noteMap.entries()).map(([note, d]) => ({ note, ...d })); }
  getEventsByType(type) { return this.events.filter(e => e.type === type); }
  clear() { this.events = []; this.noteMap.clear(); }
}

describe('ABDCZ101 Bridge Integration', () => {
  let kbd;
  let bridge;

  beforeEach(() => {
    createFixture();
    bridge = new CZ101Bridge();
  });

  afterEach(() => { if (kbd) kbd.destroy(); });

  function createCZ101Kbd(overrides = {}) {
    return createKeyboard({
      ...CZ101_PRESET,
      ...overrides,
      config: {
        ...CZ101_PRESET,
        ...(overrides.config || {}),
      },
      onNoteOn: (note, vel) => bridge.noteOn(note, vel),
      onNoteOff: (note) => bridge.noteOff(note),
      onPitchBend: (val) => bridge.pitchBend(val),
      onModWheel: (val) => bridge.modWheel(val),
      onPanic: () => bridge.allNotesOff(),
      onSustainChange: (on) => bridge.sustain(on),
    });
  }

  // ══════════════════════════════════════════════════════════════
  //  CZ-101 PRESET
  // ══════════════════════════════════════════════════════════════

  describe('CZ-101 Preset', () => {
    it('exports CZ101_PRESET with correct defaults', () => {
      expect(CZ101_PRESET.numOctaves).toBe(4);
      expect(CZ101_PRESET.startNote).toBe(48); // C3
      expect(CZ101_PRESET.enableVintageWear).toBe(true);
      expect(CZ101_PRESET.enableQwerty).toBe(true);
      expect(CZ101_PRESET.enableIvoryTexture).toBe(false);
      expect(CZ101_PRESET.enablePressureDisplay).toBe(false);
    });

    it('creates 49 keys (4 octaves + high C)', () => {
      kbd = createCZ101Kbd();
      const keys = document.querySelectorAll('#piano-keyboard .kbd-white-key, #piano-keyboard .kbd-black-key');
      // 4 octaves × 12 notes + high C = 49 keys
      expect(keys.length).toBe(49);
    });

    it('starts at C3 (MIDI 48)', () => {
      kbd = createCZ101Kbd();
      const firstKey = document.querySelector('#piano-keyboard [data-note]');
      expect(parseInt(firstKey.dataset.note)).toBe(48);
    });

    it('ends at C7 (MIDI 96)', () => {
      kbd = createCZ101Kbd();
      const allKeys = document.querySelectorAll('#piano-keyboard [data-note]');
      const lastKey = allKeys[allKeys.length - 1];
      expect(parseInt(lastKey.dataset.note)).toBe(96);
    });

    it('has vintage wear stains enabled', () => {
      kbd = createCZ101Kbd();
      const stainedKeys = document.querySelectorAll('#piano-keyboard .kbd-stain-yellow, #piano-keyboard .kbd-stain-scuff, #piano-keyboard .kbd-stain-ding');
      expect(stainedKeys.length).toBeGreaterThan(0);
    });

    it('has no wheel elements when wheelPitchId is null', () => {
      kbd = createCZ101Kbd({ wheelPitchId: null, wheelModId: null });
      // Wheels should not be rendered
      const pitchWheel = document.getElementById('pitch-wheel-container');
      const modWheel = document.getElementById('mod-wheel-container');
      // They exist in DOM but have no slider children
      expect(pitchWheel.querySelector('.kbd-wheel-slider')).toBeNull();
      expect(modWheel.querySelector('.kbd-wheel-slider')).toBeNull();
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  QWERTY CZ-101 RANGE
  // ══════════════════════════════════════════════════════════════

  describe('QWERTY CZ-101 range', () => {
    it('Z key plays C3 (MIDI 48)', () => {
      kbd = createCZ101Kbd();
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
      expect(bridge.getEventsByType('noteOn')).toHaveLength(1);
      expect(bridge.getEventsByType('noteOn')[0].note).toBe(48);
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'z', bubbles: true }));
    });

    it('Q key plays C4 (MIDI 60)', () => {
      kbd = createCZ101Kbd();
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'q', bubbles: true }));
      expect(bridge.getEventsByType('noteOn')).toHaveLength(1);
      expect(bridge.getEventsByType('noteOn')[0].note).toBe(60);
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'q', bubbles: true }));
    });

    it('all QWERTY keys produce notes within CZ-101 range', () => {
      kbd = createCZ101Kbd();
      const qwertyKeys = ['z', 's', 'x', 'd', 'c', 'v', 'g', 'b', 'h', 'n', 'j', 'm', ',', 'l', '.', ';', '/'];
      for (const key of qwertyKeys) {
        bridge.clear();
        window.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true }));
        if (bridge.getEventsByType('noteOn').length > 0) {
          const note = bridge.getEventsByType('noteOn')[0].note;
          expect(note).toBeGreaterThanOrEqual(48); // C3
          expect(note).toBeLessThanOrEqual(96);    // C7
        }
        window.dispatchEvent(new KeyboardEvent('keyup', { key, bubbles: true }));
      }
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  SUSTAIN PEDAL (CC#64)
  // ══════════════════════════════════════════════════════════════

  describe('Sustain pedal', () => {
    it('toggleSustain fires bridge.sustain', () => {
      kbd = createCZ101Kbd();
      kbd.toggleSustain();
      expect(bridge.sustainState).toBe(true);
      expect(bridge.getEventsByType('sustain')).toHaveLength(1);
      expect(bridge.getEventsByType('sustain')[0].on).toBe(true);

      kbd.toggleSustain();
      expect(bridge.sustainState).toBe(false);
      expect(bridge.getEventsByType('sustain')).toHaveLength(2);
      expect(bridge.getEventsByType('sustain')[1].on).toBe(false);
    });

    it('panic releases sustain', () => {
      kbd = createCZ101Kbd();
      kbd.setSustain(true);
      expect(bridge.sustainState).toBe(true);

      kbd.panic();
      expect(bridge.sustainState).toBe(false);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  PANIC
  // ══════════════════════════════════════════════════════════════

  describe('Panic', () => {
    it('panic releases all notes and triggers allNotesOff', () => {
      kbd = createCZ101Kbd();
      // Play some notes via QWERTY
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'x', bubbles: true }));
      expect(bridge.noteMap.size).toBe(2);

      kbd.panic();
      expect(bridge.noteMap.size).toBe(0);
      expect(bridge.getEventsByType('allNotesOff')).toHaveLength(1);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  OCTAVE SHIFT
  // ══════════════════════════════════════════════════════════════

  describe('Octave shift', () => {
    it('octave shift transposes QWERTY notes', () => {
      kbd = createCZ101Kbd();
      kbd.setOctave(1); // Shift up 1 octave (+12 semitones)

      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
      // Z = 48 + 12 = 60 (C4)
      expect(bridge.getEventsByType('noteOn')[0].note).toBe(60);
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'z', bubbles: true }));
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  FULL CZ-101 WORKFLOW
  // ══════════════════════════════════════════════════════════════

  describe('Full CZ-101 workflow', () => {
    it('complete workflow: play notes → sustain → panic', () => {
      kbd = createCZ101Kbd();

      // 1. Play C major chord via QWERTY
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true })); // C3
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'c', bubbles: true })); // E3
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'v', bubbles: true })); // G3
      expect(bridge.noteMap.size).toBe(3);

      // 2. Enable sustain
      kbd.setSustain(true);
      expect(bridge.sustainState).toBe(true);

      // 3. Release keys (sustain holds them)
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'z', bubbles: true }));
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'c', bubbles: true }));
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'v', bubbles: true }));

      // 4. Panic releases everything
      kbd.panic();
      expect(bridge.noteMap.size).toBe(0);
      expect(bridge.sustainState).toBe(false);
      expect(bridge.getEventsByType('allNotesOff')).toHaveLength(1);

      // Verify event sequence
      const noteOns = bridge.getEventsByType('noteOn');
      const noteOffs = bridge.getEventsByType('noteOff');
      expect(noteOns).toHaveLength(3);
      expect(noteOffs.length).toBeGreaterThanOrEqual(3);
    });
  });
});
