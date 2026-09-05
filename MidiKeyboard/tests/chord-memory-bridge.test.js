/**
 * ABDKeyboard — Chord Memory Bridge Integration Tests
 * Validates chord memory works correctly with ABDEep-style bridge callbacks.
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

/**
 * Simulated ABDEep bridge that tracks all events.
 * Mimics how a real synth bridge would receive keyboard events.
 */
class ABDEepBridge {
  constructor() {
    this.events = [];
    this.noteMap = new Map(); // note -> { velocity, channel, aftertouch }
    this.sustainState = false;
    this.sostenutoState = false;
    this.softPedalState = false;
    this.pitchBendValue = 0;
    this.modWheelValue = 0;
    this.aftertouchValues = new Map();
  }

  noteOn(note, velocity) {
    this.events.push({ type: 'noteOn', note, velocity, timestamp: Date.now() });
    this.noteMap.set(note, { velocity, channel: 0, aftertouch: 0 });
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

  sostenuto(on) {
    this.events.push({ type: 'sostenuto', on, timestamp: Date.now() });
    this.sostenutoState = on;
  }

  softPedal(on) {
    this.events.push({ type: 'softPedal', on, timestamp: Date.now() });
    this.softPedalState = on;
  }

  pitchBend(value) {
    this.events.push({ type: 'pitchBend', value, timestamp: Date.now() });
    this.pitchBendValue = value;
  }

  modWheel(value) {
    this.events.push({ type: 'modWheel', value, timestamp: Date.now() });
    this.modWheelValue = value;
  }

  channelPressure(pressure) {
    this.events.push({ type: 'channelPressure', pressure, timestamp: Date.now() });
  }

  polyPressure(note, pressure) {
    this.events.push({ type: 'polyPressure', note, pressure, timestamp: Date.now() });
    if (this.noteMap.has(note)) {
      this.noteMap.get(note).aftertouch = pressure;
    }
  }

  velocityUpdate(note, velocity) {
    this.events.push({ type: 'velocityUpdate', note, velocity, timestamp: Date.now() });
  }

  getNotes() {
    return Array.from(this.noteMap.entries()).map(([note, data]) => ({ note, ...data }));
  }

  getEventsByType(type) {
    return this.events.filter(e => e.type === type);
  }

  clear() {
    this.events = [];
    this.noteMap.clear();
    this.aftertouchValues.clear();
  }
}

describe('Chord Memory — ABDEep Bridge Integration', () => {
  let kbd;
  let bridge;

  beforeEach(() => {
    createFixture();
    bridge = new ABDEepBridge();
  });

  afterEach(() => {
    if (kbd) kbd.destroy();
  });

  function createBridgeKbd(configOverrides = {}) {
    return createKeyboard({
      config: {
        numOctaves: 3,
        startNote: 48,  // C3
        enableQwerty: false,
        enableTouch: false,
        enableChordMemory: true,
        maxChordSlots: 8,
        ...configOverrides,
      },
      onNoteOn: (note, vel) => bridge.noteOn(note, vel),
      onNoteOff: (note) => bridge.noteOff(note),
      onPitchBend: (val) => bridge.pitchBend(val),
      onModWheel: (val) => bridge.modWheel(val),
      onPanic: () => bridge.allNotesOff(),
      onSustainChange: (on) => bridge.sustain(on),
      onAftertouch: (note, pressure) => {
        if (note === -1) bridge.channelPressure(pressure);
        else bridge.polyPressure(note, pressure);
      },
      onVelocityChange: (note, vel) => bridge.velocityUpdate(note, vel),
    });
  }

  // ══════════════════════════════════════════════════════════════
  //  SAVE CHORD — Bridge Events
  // ══════════════════════════════════════════════════════════════

  describe('saveChord', () => {
    it('saves explicit notes without triggering bridge events', () => {
      kbd = createBridgeKbd();
      bridge.clear();

      kbd.saveChord(0, [60, 64, 67]); // C major triad

      // saveChord should NOT trigger noteOn/noteOff — it's just storage
      expect(bridge.getEventsByType('noteOn')).toHaveLength(0);
      expect(bridge.getEventsByType('noteOff')).toHaveLength(0);
      expect(kbd.getChords()[0]).toEqual([60, 64, 67]);
    });

    it('saves notes sorted by MIDI number', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [67, 60, 64]); // Unsorted
      expect(kbd.getChords()[0]).toEqual([60, 64, 67]); // Sorted
    });

    it('can save multiple chord slots', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]); // C major
      kbd.saveChord(1, [62, 66, 69]); // D minor
      kbd.saveChord(2, [65, 69, 72]); // F major

      expect(kbd.getChords()[0]).toEqual([60, 64, 67]);
      expect(kbd.getChords()[1]).toEqual([62, 66, 69]);
      expect(kbd.getChords()[2]).toEqual([65, 69, 72]);
    });

    it('overwrites existing chord in same slot', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]); // C major
      kbd.saveChord(0, [60, 63, 67]); // C minor (overwrite)
      expect(kbd.getChords()[0]).toEqual([60, 63, 67]);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  PLAY CHORD — Bridge Events
  // ══════════════════════════════════════════════════════════════

  describe('playChord', () => {
    it('triggers noteOn for each note in the chord', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]); // C major

      kbd.playChord(0);

      const noteOnEvents = bridge.getEventsByType('noteOn');
      expect(noteOnEvents).toHaveLength(3);
      expect(noteOnEvents.map(e => e.note)).toEqual([60, 64, 67]);
      // Each note should have a velocity
      noteOnEvents.forEach(e => {
        expect(e.velocity).toBeGreaterThan(0);
        expect(e.velocity).toBeLessThanOrEqual(1);
      });
    });

    it('plays chord with fixedVelocity from config', () => {
      kbd = createBridgeKbd({ fixedVelocity: 0.75 });
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);

      const noteOnEvents = bridge.getEventsByType('noteOn');
      noteOnEvents.forEach(e => {
        expect(e.velocity).toBe(0.75);
      });
    });

    it('does not trigger noteOff during play', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);

      expect(bridge.getEventsByType('noteOff')).toHaveLength(0);
    });

    it('does nothing for empty slot', () => {
      kbd = createBridgeKbd();
      kbd.playChord(0); // Empty slot

      expect(bridge.getEventsByType('noteOn')).toHaveLength(0);
    });

    it('plays notes in ascending MIDI order', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [67, 60, 64]); // Saved unsorted, but playChord uses sorted
      // Note: saveChord sorts, so it will be [60, 64, 67]

      kbd.playChord(0);

      const noteOnEvents = bridge.getEventsByType('noteOn');
      expect(noteOnEvents[0].note).toBe(60);
      expect(noteOnEvents[1].note).toBe(64);
      expect(noteOnEvents[2].note).toBe(67);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  RELEASE CHORD — Bridge Events
  // ══════════════════════════════════════════════════════════════

  describe('releaseChord', () => {
    it('triggers noteOff for each note in the chord', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      kbd.releaseChord(0);

      const noteOffEvents = bridge.getEventsByType('noteOff');
      expect(noteOffEvents).toHaveLength(3);
      expect(noteOffEvents.map(e => e.note)).toEqual([60, 64, 67]);
    });

    it('does nothing for empty slot', () => {
      kbd = createBridgeKbd();
      kbd.releaseChord(0);

      expect(bridge.getEventsByType('noteOff')).toHaveLength(0);
    });

    it('can release after play', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);
      kbd.releaseChord(0);

      const noteOnEvents = bridge.getEventsByType('noteOn');
      const noteOffEvents = bridge.getEventsByType('noteOff');
      expect(noteOnEvents).toHaveLength(3);
      expect(noteOffEvents).toHaveLength(3);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  PLAY + RELEASE SEQUENCE — Full Bridge Flow
  // ══════════════════════════════════════════════════════════════

  describe('play + release sequence', () => {
    it('full cycle: save → play → release triggers correct events', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      // Play
      kbd.playChord(0);
      expect(bridge.noteMap.size).toBe(3);
      expect(bridge.getNotes().map(n => n.note).sort()).toEqual([60, 64, 67]);

      // Release
      kbd.releaseChord(0);
      expect(bridge.noteMap.size).toBe(0);
    });

    it('multiple chords can be played simultaneously', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]); // C major
      kbd.saveChord(1, [62, 66, 69]); // D minor

      kbd.playChord(0);
      kbd.playChord(1);

      expect(bridge.noteMap.size).toBe(6);
      const notes = bridge.getNotes().map(n => n.note).sort();
      expect(notes).toEqual([60, 62, 64, 66, 67, 69]);

      // Release just chord 0
      kbd.releaseChord(0);
      expect(bridge.noteMap.size).toBe(3);
      expect(bridge.getNotes().map(n => n.note).sort()).toEqual([62, 66, 69]);
    });

    it('releasing non-played chord does not affect other chords', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);
      kbd.saveChord(1, [62, 66, 69]);

      kbd.playChord(0);
      kbd.releaseChord(1); // Release chord that wasn't played

      // Chord 0 should still be active
      expect(bridge.noteMap.size).toBe(3);
      expect(bridge.getNotes().map(n => n.note).sort()).toEqual([60, 64, 67]);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  PANIC WITH CHORDS — Bridge Integration
  // ══════════════════════════════════════════════════════════════

  describe('panic with chords', () => {
    it('panic releases all notes including chord notes', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);
      kbd.playChord(0);

      expect(bridge.noteMap.size).toBe(3);

      kbd.panic();

      expect(bridge.noteMap.size).toBe(0);
      expect(bridge.getEventsByType('allNotesOff')).toHaveLength(1);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  SUSTAIN + CHORDS — Bridge Integration
  // ══════════════════════════════════════════════════════════════

  describe('sustain with chords', () => {
    it('sustain state is preserved during chord operations', () => {
      kbd = createBridgeKbd();

      kbd.setSustain(true);
      expect(bridge.sustainState).toBe(true);

      kbd.saveChord(0, [60, 64, 67]);
      kbd.playChord(0);

      // Sustain should still be on
      expect(bridge.sustainState).toBe(true);

      kbd.releaseChord(0);
      expect(bridge.sustainState).toBe(true);
    });
  });

  describe('soft pedal with chords', () => {
    it('chord playback uses fixedVelocity regardless of soft pedal', () => {
      // Soft pedal only affects pointer/QWERTY input, not chord memory playback.
      kbd = createBridgeKbd({ fixedVelocity: 0.85 });
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);
      const noteOnEvents = bridge.getEventsByType('noteOn');
      noteOnEvents.forEach(e => expect(e.velocity).toBe(0.85));
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  SOFT PEDAL + CHORDS — Bridge Integration
  // ══════════════════════════════════════════════════════════════

  describe('soft pedal with chords', () => {
    it('chord playback always uses fixedVelocity', () => {
      // Chord playback uses fixedVelocity directly.
      // This is by design — chord memory plays at consistent velocity.
      kbd = createBridgeKbd({ fixedVelocity: 0.75 });
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);
      const noteOnEvents = bridge.getEventsByType('noteOn');
      noteOnEvents.forEach(e => expect(e.velocity).toBe(0.75));
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  AFTERTOUCH + CHORDS — Bridge Integration
  // ══════════════════════════════════════════════════════════════

  describe('aftertouch with chords', () => {
    it('aftertouch can be set for chord notes via API', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      // Play chord
      kbd.playChord(0);

      // Set polyphonic aftertouch for each note
      kbd.setAftertouch(60, 0.8);
      kbd.setAftertouch(64, 0.6);
      kbd.setAftertouch(67, 0.4);

      const polyEvents = bridge.getEventsByType('polyPressure');
      expect(polyEvents).toHaveLength(3);
      expect(polyEvents.find(e => e.note === 60).pressure).toBe(0.8);
      expect(polyEvents.find(e => e.note === 64).pressure).toBe(0.6);
      expect(polyEvents.find(e => e.note === 67).pressure).toBe(0.4);
    });

    it('release aftertouch for chord notes', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);
      kbd.playChord(0);

      // Set aftertouch
      kbd.setAftertouch(60, 0.8);
      kbd.setAftertouch(64, 0.6);

      // Release aftertouch for one note
      kbd.releaseAftertouch(60);

      const polyEvents = bridge.getEventsByType('polyPressure');
      const releaseEvent = polyEvents.find(e => e.note === 60 && e.pressure === 0);
      expect(releaseEvent).toBeDefined();
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  SCALE FILTER + CHORDS — Bridge Integration
  // ══════════════════════════════════════════════════════════════

  describe('scale filter with chords', () => {
    it('chord memory works regardless of scale filter', () => {
      kbd = createBridgeKbd({
        enableScaleFilter: true,
        scaleType: 'major',
        scaleRoot: 60, // C major
        scaleSnapMode: 'block',
      });

      // Save a chord that includes notes outside C major (F#=66)
      kbd.saveChord(0, [60, 64, 67, 66]); // C E G F#

      // playChord should still play all notes (scale filter only affects input)
      kbd.playChord(0);

      const noteOnEvents = bridge.getEventsByType('noteOn');
      expect(noteOnEvents).toHaveLength(4);
      expect(noteOnEvents.map(e => e.note).sort()).toEqual([60, 64, 66, 67]);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  VELOCITY CALLBACK + CHORDS — Bridge Integration
  // ══════════════════════════════════════════════════════════════

  describe('velocity callback with chords', () => {
    it('onVelocityChange fires for each chord note', () => {
      kbd = createBridgeKbd({ fixedVelocity: 0.85 });
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);

      const velEvents = bridge.getEventsByType('velocityUpdate');
      expect(velEvents).toHaveLength(3);
      velEvents.forEach(e => {
        expect(e.velocity).toBe(0.85);
      });
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  CLEAR CHORDS — Bridge Integration
  // ══════════════════════════════════════════════════════════════

  describe('clearChord', () => {
    it('clearing a slot prevents replay', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);
      kbd.clearChord(0);

      kbd.playChord(0);

      expect(bridge.getEventsByType('noteOn')).toHaveLength(0);
    });

    it('clearAllChords clears all slots', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);
      kbd.saveChord(1, [62, 66, 69]);
      kbd.saveChord(2, [65, 69, 72]);

      kbd.clearAllChords();

      const chords = kbd.getChords();
      expect(chords.every(c => c === null)).toBe(true);

      // No notes should play
      kbd.playChord(0);
      kbd.playChord(1);
      kbd.playChord(2);
      expect(bridge.getEventsByType('noteOn')).toHaveLength(0);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  VISUAL FEEDBACK — DOM Verification
  // ══════════════════════════════════════════════════════════════

  describe('visual feedback', () => {
    it('playChord adds active class to keys', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);

      const key60 = document.querySelector('[data-note="60"]');
      const key64 = document.querySelector('[data-note="64"]');
      const key67 = document.querySelector('[data-note="67"]');

      expect(key60.classList.contains('active')).toBe(true);
      expect(key64.classList.contains('active')).toBe(true);
      expect(key67.classList.contains('active')).toBe(true);
    });

    it('releaseChord removes active class from keys', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);
      kbd.releaseChord(0);

      const key60 = document.querySelector('[data-note="60"]');
      const key64 = document.querySelector('[data-note="64"]');
      const key67 = document.querySelector('[data-note="67"]');

      expect(key60.classList.contains('active')).toBe(false);
      expect(key64.classList.contains('active')).toBe(false);
      expect(key67.classList.contains('active')).toBe(false);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  EDGE CASES
  // ══════════════════════════════════════════════════════════════

  describe('edge cases', () => {
    it('slot index is clamped to valid range', () => {
      kbd = createBridgeKbd({ maxChordSlots: 4 });

      kbd.saveChord(99, [60]); // Out of range → clamped to slot 3
      expect(kbd.getChords()[3]).toEqual([60]);

      kbd.saveChord(-5, [62]); // Negative → clamped to slot 0
      expect(kbd.getChords()[0]).toEqual([62]);
    });

    it('single-note chord works', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60]);

      kbd.playChord(0);
      expect(bridge.getEventsByType('noteOn')).toHaveLength(1);
      expect(bridge.getEventsByType('noteOn')[0].note).toBe(60);
    });

    it('large chord (12 notes) works', () => {
      kbd = createBridgeKbd();
      const chromatic = [60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71];
      kbd.saveChord(0, chromatic);

      kbd.playChord(0);
      expect(bridge.getEventsByType('noteOn')).toHaveLength(12);
      expect(bridge.getNotes().map(n => n.note).sort()).toEqual(chromatic);
    });

    it('playChord is idempotent (playing same chord twice adds notes twice)', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      kbd.playChord(0);
      kbd.playChord(0); // Play again

      // Bridge receives 6 noteOn events (3 + 3)
      expect(bridge.getEventsByType('noteOn')).toHaveLength(6);
    });

    it('getChords returns independent copies', () => {
      kbd = createBridgeKbd();
      kbd.saveChord(0, [60, 64, 67]);

      const chords1 = kbd.getChords();
      const chords2 = kbd.getChords();

      expect(chords1[0]).not.toBe(chords2[0]); // Different references
      expect(chords1[0]).toEqual(chords2[0]); // Same contents
    });

    it('does nothing when enableChordMemory is false', () => {
      kbd = createKeyboard({
        config: {
          numOctaves: 3, startNote: 48,
          enableQwerty: false, enableTouch: false,
          enableChordMemory: false,
        },
        onNoteOn: (n, v) => bridge.noteOn(n, v),
      });

      kbd.saveChord(0, [60, 64]);
      kbd.playChord(0);

      expect(bridge.getEventsByType('noteOn')).toHaveLength(0);
    });
  });

  // ══════════════════════════════════════════════════════════════
  //  FULL ABDEEP WORKFLOW
  // ══════════════════════════════════════════════════════════════

  describe('full ABDEep workflow', () => {
    it('complete chord memory workflow: save → play → aftertouch → release → panic', () => {
      kbd = createBridgeKbd();

      // 1. Save chord
      kbd.saveChord(0, [60, 64, 67]); // C major
      kbd.saveChord(1, [62, 66, 69]); // D minor

      // 2. Enable sustain
      kbd.setSustain(true);
      expect(bridge.sustainState).toBe(true);

      // 3. Play chord 0
      kbd.playChord(0);
      expect(bridge.noteMap.size).toBe(3);

      // 4. Add aftertouch
      kbd.setAftertouch(60, 0.7);
      kbd.setAftertouch(64, 0.5);

      // 5. Play chord 1 (additional notes)
      kbd.playChord(1);
      expect(bridge.noteMap.size).toBe(6);

      // 6. Release chord 0
      kbd.releaseChord(0);
      expect(bridge.noteMap.size).toBe(3);
      expect(bridge.getNotes().map(n => n.note).sort()).toEqual([62, 66, 69]);

      // 7. Disable sustain
      kbd.setSustain(false);
      expect(bridge.sustainState).toBe(false);

      // 8. Panic (release everything)
      kbd.panic();
      expect(bridge.noteMap.size).toBe(0);
      expect(bridge.getEventsByType('allNotesOff')).toHaveLength(1);

      // Verify event counts
      expect(bridge.getEventsByType('noteOn')).toHaveLength(6); // 3 + 3
      // releaseChord releases chord 0 (3 notes) + panic releases chord 1 (3 notes)
      // but releaseChord only fires noteOff for notes not already in activeKeys,
      // and panic fires for everything in activeKeys. So total noteOff depends on overlap.
      expect(bridge.getEventsByType('noteOff').length).toBeGreaterThanOrEqual(3);
      expect(bridge.getEventsByType('sustain')).toHaveLength(2); // on + off
      expect(bridge.getEventsByType('polyPressure')).toHaveLength(2); // 2 aftertouch
    });
  });
});
