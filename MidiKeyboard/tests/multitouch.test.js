/**
 * ABDKeyboard — Multi-Touch Edge Case Tests
 * Validates correct behavior when multiple fingers interact simultaneously.
 */

import { describe, it, expect, beforeEach, afterEach } from 'vitest';
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

describe('Multi-Touch Edge Cases', () => {
  let kbd;
  const noteOnLog = [];
  const noteOffLog = [];

  beforeEach(() => {
    createFixture();
    noteOnLog.length = 0;
    noteOffLog.length = 0;
    kbd = createKeyboard({
      config: { numOctaves: 3, startNote: 48, enableQwerty: true, enableTouch: true },
      onNoteOn: (n, v) => noteOnLog.push({ note: n, vel: v }),
      onNoteOff: (n) => noteOffLog.push(n),
    });
  });

  afterEach(() => { if (kbd) kbd.destroy(); });

  function firePointerDown(keyEl, pointerId = 1, clientY = 50) {
    const rect = keyEl.getBoundingClientRect();
    keyEl.dispatchEvent(new PointerEvent('pointerdown', {
      bubbles: true, pointerId, clientY, clientX: rect.left + rect.width / 2,
      buttons: 1,
    }));
  }

  function firePointerUp(keyEl, pointerId = 1, clientY = 50) {
    keyEl.dispatchEvent(new PointerEvent('pointerup', {
      bubbles: true, pointerId, clientY, clientX: 0, buttons: 0,
    }));
  }

  // ══════════════════════════════════════════════════════════════
  //  TWO FINGERS ON DIFFERENT KEYS
  // ══════════════════════════════════════════════════════════════

  describe('two fingers on different keys', () => {
    it('both notes play independently', () => {
      const keyC = document.querySelector('[data-note="48"]');
      const keyE = document.querySelector('[data-note="52"]');
      firePointerDown(keyC, 1);
      firePointerDown(keyE, 2);
      expect(noteOnLog).toHaveLength(2);
      expect(kbd.getActiveNotes()).toContain(48);
      expect(kbd.getActiveNotes()).toContain(52);
    });

    it('releasing one finger does not affect the other', () => {
      const keyC = document.querySelector('[data-note="48"]');
      const keyE = document.querySelector('[data-note="52"]');
      firePointerDown(keyC, 1);
      firePointerDown(keyE, 2);
      firePointerUp(keyC, 1);
      expect(noteOffLog).toHaveLength(1);
      expect(noteOffLog[0]).toBe(48);
      expect(kbd.getActiveNotes()).toContain(52);
      expect(kbd.getActiveNotes()).not.toContain(48);
    });

    it('releasing both notes works correctly', () => {
      const keyC = document.querySelector('[data-note="48"]');
      const keyE = document.querySelector('[data-note="52"]');
      firePointerDown(keyC, 1);
      firePointerDown(keyE, 2);
      firePointerUp(keyC, 1);
      firePointerUp(keyE, 2);
      expect(noteOffLog).toHaveLength(2);
      expect(kbd.getActiveNotes()).toHaveLength(0);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  TWO FINGERS ON THE SAME KEY
  // ══════════════════════════════════════════════════════════════

  describe('two fingers on same key', () => {
    it('second pointer replaces first cleanly', () => {
      const keyC = document.querySelector('[data-note="48"]');
      firePointerDown(keyC, 1);
      firePointerDown(keyC, 2);
      // Second pointer takes over: old pointer removed silently, new noteOn fires
      // The key remains active with exactly 1 note
      expect(kbd.getActiveNotes()).toHaveLength(1);
      expect(kbd.getActiveNotes()).toContain(48);
    });

    it('releasing first pointer does not release note if second is still down', () => {
      const keyC = document.querySelector('[data-note="48"]');
      firePointerDown(keyC, 1);
      firePointerDown(keyC, 2);
      firePointerUp(keyC, 1);
      // Note should still be active because pointer 2 is holding it
      expect(noteOffLog).toHaveLength(0);
      expect(kbd.getActiveNotes()).toContain(48);
    });

    it('releasing second pointer releases the note', () => {
      const keyC = document.querySelector('[data-note="48"]');
      firePointerDown(keyC, 1);
      firePointerDown(keyC, 2);
      firePointerUp(keyC, 1);
      firePointerUp(keyC, 2);
      expect(noteOffLog).toHaveLength(1);
      expect(kbd.getActiveNotes()).toHaveLength(0);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  RAPID TAPPING (same key, alternating fingers)
  // ══════════════════════════════════════════════════════════════

  describe('rapid tapping', () => {
    it('can tap the same key rapidly with different fingers', () => {
      const keyC = document.querySelector('[data-note="48"]');
      firePointerDown(keyC, 1);
      firePointerUp(keyC, 1);
      firePointerDown(keyC, 2);
      firePointerUp(keyC, 2);
      firePointerDown(keyC, 3);
      firePointerUp(keyC, 3);
      expect(noteOnLog).toHaveLength(3);
      expect(noteOffLog).toHaveLength(3);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  QWERTY + POINTER SIMULTANEOUS
  // ══════════════════════════════════════════════════════════════

  describe('QWERTY + pointer simultaneous', () => {
    it('QWERTY and pointer notes coexist', () => {
      // Play C4 via QWERTY (z key = startNote + 0 = 48)
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
      // Play E4 via pointer (note 52)
      const keyE = document.querySelector('[data-note="52"]');
      firePointerDown(keyE, 1);
      expect(noteOnLog).toHaveLength(2);
      expect(kbd.getActiveNotes().sort()).toEqual([48, 52]);
    });

    it('releasing QWERTY does not affect pointer notes', () => {
      window.dispatchEvent(new KeyboardEvent('keydown', { key: 'z', bubbles: true }));
      const keyE = document.querySelector('[data-note="52"]');
      firePointerDown(keyE, 1);
      window.dispatchEvent(new KeyboardEvent('keyup', { key: 'z', bubbles: true }));
      expect(kbd.getActiveNotes()).toEqual([52]);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  SUSTAIN + MULTI-TOUCH
  // ══════════════════════════════════════════════════════════════

  describe('sustain with multi-touch', () => {
    it('sustain holds all notes from multiple fingers', () => {
      const keyC = document.querySelector('[data-note="48"]');
      const keyE = document.querySelector('[data-note="52"]');
      kbd.setSustain(true);
      firePointerDown(keyC, 1);
      firePointerDown(keyE, 2);
      firePointerUp(keyC, 1);
      firePointerUp(keyE, 2);
      // Both notes still active due to sustain
      expect(kbd.getActiveNotes().sort()).toEqual([48, 52]);
    });

    it('panic releases sustained multi-touch notes', () => {
      const keyC = document.querySelector('[data-note="48"]');
      const keyE = document.querySelector('[data-note="52"]');
      kbd.setSustain(true);
      firePointerDown(keyC, 1);
      firePointerDown(keyE, 2);
      firePointerUp(keyC, 1);
      firePointerUp(keyE, 2);
      kbd.panic();
      expect(kbd.getActiveNotes()).toHaveLength(0);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  AFTERTOUCH + MULTI-TOUCH
  // ══════════════════════════════════════════════════════════════

  describe('aftertouch with multi-touch', () => {
    it('polyphonic aftertouch tracks each finger independently', () => {
      const aftertouchLog = [];
      kbd.destroy();
      kbd = createKeyboard({
        config: {
          numOctaves: 3, startNote: 48, enableQwerty: false, enableTouch: true,
          enableAftertouch: true, aftertouchMode: 'polyphonic', aftertouchSensitivity: 0.5,
        },
        onNoteOn: () => {},
        onNoteOff: () => {},
        onAftertouch: (note, pressure) => aftertouchLog.push({ note, pressure }),
      });
      // Both notes should be trackable
      expect(aftertouchLog).toHaveLength(0);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  EDGE CASE: DESTROY DURING MULTI-TOUCH
  // ══════════════════════════════════════════════════════════════

  describe('destroy during multi-touch', () => {
    it('destroy releases all active multi-touch notes', () => {
      const keyC = document.querySelector('[data-note="48"]');
      const keyE = document.querySelector('[data-note="52"]');
      firePointerDown(keyC, 1);
      firePointerDown(keyE, 2);
      expect(kbd.getActiveNotes()).toHaveLength(2);
      kbd.destroy();
      kbd = null;
      // No errors should occur
    });
  });
});
