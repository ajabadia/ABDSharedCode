/**
 * ABDKeyboard — Utils Unit Tests
 * =================================
 * Tests for pure utility functions extracted from keyboard.js.
 * These are stateless and don't need DOM fixtures or keyboard instances.
 */

import { describe, it, expect } from 'vitest';
import {
  QWERTY_MAP, NOTE_NAMES, OCTAVE_PATTERN, midiToName,
  applyVelocityCurve, SCALE_INTERVALS, isInScale, snapToScale,
  CZ101_PRESET, applyIvoryTexture, applyVintageWear,
} from '../src/utils.js';

// ══════════════════════════════════════════════════════════════
//  QWERTY_MAP
// ══════════════════════════════════════════════════════════════

describe('QWERTY_MAP', () => {
  it('maps z to offset 0 (C)', () => {
    expect(QWERTY_MAP['z']).toBe(0);
  });

  it('maps q to offset 12 (C of upper row)', () => {
    expect(QWERTY_MAP['q']).toBe(12);
  });

  it('has 37 key mappings', () => {
    expect(Object.keys(QWERTY_MAP)).toHaveLength(37);
  });

  it('lower row covers 0–16 (17 keys)', () => {
    const lower = ['z','s','x','d','c','v','g','b','h','n','j','m',',','l','.',';','/'];
    const offsets = lower.map(k => QWERTY_MAP[k]);
    expect(offsets).toEqual([0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]);
  });

  it('upper row covers 12–31 (20 keys)', () => {
    const upper = ['q','2','w','3','e','r','5','t','6','y','7','u','i','9','o','0','p','[','=',']'];
    const offsets = upper.map(k => QWERTY_MAP[k]);
    expect(offsets[0]).toBe(12);
    expect(offsets[offsets.length - 1]).toBe(31);
  });
});

// ══════════════════════════════════════════════════════════════
//  NOTE_NAMES
// ══════════════════════════════════════════════════════════════

describe('NOTE_NAMES', () => {
  it('has 12 note names', () => {
    expect(NOTE_NAMES).toHaveLength(12);
  });

  it('starts with C and ends with B', () => {
    expect(NOTE_NAMES[0]).toBe('C');
    expect(NOTE_NAMES[11]).toBe('B');
  });

  it('includes all sharps', () => {
    expect(NOTE_NAMES).toContain('C#');
    expect(NOTE_NAMES).toContain('D#');
    expect(NOTE_NAMES).toContain('F#');
    expect(NOTE_NAMES).toContain('G#');
    expect(NOTE_NAMES).toContain('A#');
  });
});

// ══════════════════════════════════════════════════════════════
//  OCTAVE_PATTERN
// ══════════════════════════════════════════════════════════════

describe('OCTAVE_PATTERN', () => {
  it('has 7 entries (one per white key in an octave)', () => {
    expect(OCTAVE_PATTERN).toHaveLength(7);
  });

  it('C has sharp at offset 1 (C#)', () => {
    expect(OCTAVE_PATTERN[0]).toEqual({ offset: 0, sharp: 1 });
  });

  it('E has no sharp (no E# in standard layout)', () => {
    expect(OCTAVE_PATTERN[2]).toEqual({ offset: 4, sharp: null });
  });

  it('B has no sharp (no B# in standard layout)', () => {
    expect(OCTAVE_PATTERN[6]).toEqual({ offset: 11, sharp: null });
  });

  it('all offsets are ascending', () => {
    const offsets = OCTAVE_PATTERN.map(p => p.offset);
    expect(offsets).toEqual([0, 2, 4, 5, 7, 9, 11]);
  });
});

// ══════════════════════════════════════════════════════════════
//  midiToName
// ══════════════════════════════════════════════════════════════

describe('midiToName', () => {
  it('converts MIDI 60 to C4', () => {
    expect(midiToName(60)).toBe('C4');
  });

  it('converts MIDI 0 to C-1', () => {
    expect(midiToName(0)).toBe('C-1');
  });

  it('converts MIDI 127 to G9', () => {
    expect(midiToName(127)).toBe('G9');
  });

  it('converts MIDI 61 to C#4', () => {
    expect(midiToName(61)).toBe('C#4');
  });

  it('converts MIDI 69 to A4 (concert pitch)', () => {
    expect(midiToName(69)).toBe('A4');
  });

  it('converts MIDI 48 to C3', () => {
    expect(midiToName(48)).toBe('C3');
  });

  it('handles all 12 note names in octave 4', () => {
    const expected = ['C4','C#4','D4','D#4','E4','F4','F#4','G4','G#4','A4','A#4','B4'];
    for (let i = 0; i < 12; i++) {
      expect(midiToName(60 + i)).toBe(expected[i]);
    }
  });
});

// ══════════════════════════════════════════════════════════════
//  applyVelocityCurve
// ══════════════════════════════════════════════════════════════

describe('applyVelocityCurve', () => {
  it('normal curve returns input unchanged', () => {
    expect(applyVelocityCurve(0.5, 'normal')).toBe(0.5);
  });

  it('normal curve with unknown type returns input', () => {
    expect(applyVelocityCurve(0.5, 'unknown')).toBe(0.5);
  });

  it('linear curve returns input unchanged', () => {
    expect(applyVelocityCurve(0.7, 'linear')).toBe(0.7);
  });

  it('soft curve squares the value (slower attack)', () => {
    expect(applyVelocityCurve(0.5, 'soft')).toBeCloseTo(0.25);
    expect(applyVelocityCurve(0.8, 'soft')).toBeCloseTo(0.64);
  });

  it('hard curve takes square root (faster attack)', () => {
    expect(applyVelocityCurve(0.25, 'hard')).toBeCloseTo(0.5);
    expect(applyVelocityCurve(0.64, 'hard')).toBeCloseTo(0.8);
  });

  it('fixed curve returns constant ~0.787', () => {
    const result = applyVelocityCurve(0, 'fixed');
    expect(result).toBeCloseTo(100 / 127);
  });

  it('fixed curve ignores input value', () => {
    expect(applyVelocityCurve(0, 'fixed')).toBe(applyVelocityCurve(1, 'fixed'));
  });

  it('soft curve at extremes: 0→0, 1→1', () => {
    expect(applyVelocityCurve(0, 'soft')).toBe(0);
    expect(applyVelocityCurve(1, 'soft')).toBe(1);
  });

  it('hard curve at extremes: 0→0, 1→1', () => {
    expect(applyVelocityCurve(0, 'hard')).toBe(0);
    expect(applyVelocityCurve(1, 'hard')).toBe(1);
  });
});

// ══════════════════════════════════════════════════════════════
//  SCALE_INTERVALS
// ══════════════════════════════════════════════════════════════

describe('SCALE_INTERVALS', () => {
  it('has 16 scales defined', () => {
    expect(Object.keys(SCALE_INTERVALS)).toHaveLength(16);
  });

  it('major scale has 7 notes', () => {
    expect(SCALE_INTERVALS.major).toHaveLength(7);
  });

  it('major scale intervals are [0,2,4,5,7,9,11]', () => {
    expect(SCALE_INTERVALS.major).toEqual([0, 2, 4, 5, 7, 9, 11]);
  });

  it('minor scale intervals are [0,2,3,5,7,8,10]', () => {
    expect(SCALE_INTERVALS.minor).toEqual([0, 2, 3, 5, 7, 8, 10]);
  });

  it('pentatonic major has 5 notes', () => {
    expect(SCALE_INTERVALS.pentatonicMajor).toHaveLength(5);
  });

  it('blues scale has 6 notes', () => {
    expect(SCALE_INTERVALS.blues).toHaveLength(6);
  });

  it('chromatic scale has all 12 notes', () => {
    expect(SCALE_INTERVALS.chromatic).toHaveLength(12);
  });

  it('all intervals are within 0–11', () => {
    for (const [, intervals] of Object.entries(SCALE_INTERVALS)) {
      for (const i of intervals) {
        expect(i).toBeGreaterThanOrEqual(0);
        expect(i).toBeLessThanOrEqual(11);
      }
    }
  });

  it('all interval arrays are sorted ascending', () => {
    for (const [, intervals] of Object.entries(SCALE_INTERVALS)) {
      for (let i = 1; i < intervals.length; i++) {
        expect(intervals[i]).toBeGreaterThan(intervals[i - 1]);
      }
    }
  });
});

// ══════════════════════════════════════════════════════════════
//  isInScale
// ══════════════════════════════════════════════════════════════

describe('isInScale', () => {
  const major = SCALE_INTERVALS.major;

  it('C (60) is in C major (root=60)', () => {
    expect(isInScale(60, 60, major)).toBe(true);
  });

  it('D (62) is in C major', () => {
    expect(isInScale(62, 60, major)).toBe(true);
  });

  it('C# (61) is NOT in C major', () => {
    expect(isInScale(61, 60, major)).toBe(false);
  });

  it('F# (66) is NOT in C major', () => {
    expect(isInScale(66, 60, major)).toBe(false);
  });

  it('C (72) is in C major (octave wrap)', () => {
    expect(isInScale(72, 60, major)).toBe(true);
  });

  it('C (48) is in C major (lower octave)', () => {
    expect(isInScale(48, 60, major)).toBe(true);
  });

  it('works with different root: D major', () => {
    // D major scale notes: D E F# G A B C#
    expect(isInScale(62, 62, major)).toBe(true);  // D (root)
    expect(isInScale(64, 62, major)).toBe(true);  // E
    expect(isInScale(66, 62, major)).toBe(true);  // F#
    expect(isInScale(67, 62, major)).toBe(true);  // G
    expect(isInScale(61, 62, major)).toBe(true);  // C# (semitone 11 from D, leading tone)
  });

  it('negative MIDI notes are handled correctly', () => {
    expect(isInScale(-12, 0, major)).toBe(true); // C at -1 octave
  });
});

// ══════════════════════════════════════════════════════════════
//  snapToScale
// ══════════════════════════════════════════════════════════════

describe('snapToScale', () => {
  const major = SCALE_INTERVALS.major;

  it('returns same note if already in scale', () => {
    expect(snapToScale(60, 60, major)).toBe(60); // C in C major
    expect(snapToScale(62, 60, major)).toBe(62); // D in C major
    expect(snapToScale(64, 60, major)).toBe(64); // E in C major
  });

  it('snaps C# (61) up to D (62) in C major', () => {
    // semitone 1: offset +1 → 2 (D, in major) → snaps UP
    expect(snapToScale(61, 60, major)).toBe(62);
  });

  it('snaps F# (66) up to G (67) in C major', () => {
    // semitone 6: offset +1 → 7 (G, in major) → snaps UP
    expect(snapToScale(66, 60, major)).toBe(67);
  });

  it('snaps Bb (70) up to B (71) in C major', () => {
    // semitone 10: offset +1 → 11 (B, in major) → snaps UP
    expect(snapToScale(70, 60, major)).toBe(71);
  });

  it('snaps Ab (68) up to A (69) in C major', () => {
    // semitone 8: offset +1 → 9 (A, in major) → snaps UP
    expect(snapToScale(68, 60, major)).toBe(69);
  });

  it('works with pentatonic scale (fewer notes = more snapping)', () => {
    const penta = SCALE_INTERVALS.pentatonicMajor; // [0,2,4,7,9]
    expect(snapToScale(60, 60, penta)).toBe(60); // C in C penta
    expect(snapToScale(63, 60, penta)).toBe(64); // Eb → E (offset +1 → 4)
    expect(snapToScale(65, 60, penta)).toBe(64); // F → E (offset -1 → 4)
  });

  it('snaps to nearest, preferring upward when equidistant', () => {
    // For C# (semitone 1) in C major, nearest are C (0, offset -1) and D (2, offset +1)
    // Algorithm checks +offset first → snaps UP to D
    expect(snapToScale(61, 60, major)).toBe(62); // C# → D (up)
  });
});

// ══════════════════════════════════════════════════════════════
//  CZ101_PRESET
// ══════════════════════════════════════════════════════════════

describe('CZ101_PRESET', () => {
  it('has 4 octaves', () => {
    expect(CZ101_PRESET.numOctaves).toBe(4);
  });

  it('starts at MIDI 48 (C3)', () => {
    expect(CZ101_PRESET.startNote).toBe(48);
  });

  it('enables vintage wear', () => {
    expect(CZ101_PRESET.enableVintageWear).toBe(true);
  });

  it('enables QWERTY', () => {
    expect(CZ101_PRESET.enableQwerty).toBe(true);
  });

  it('disables ivory texture', () => {
    expect(CZ101_PRESET.enableIvoryTexture).toBe(false);
  });

  it('disables pressure display', () => {
    expect(CZ101_PRESET.enablePressureDisplay).toBe(false);
  });

  it('disables pitch-bend displacement', () => {
    expect(CZ101_PRESET.enablePitchBendDisplace).toBe(false);
  });

  it('disables aftertouch', () => {
    expect(CZ101_PRESET.enableAftertouch).toBe(false);
  });
});

// ══════════════════════════════════════════════════════════════
//  applyIvoryTexture
// ══════════════════════════════════════════════════════════════

describe('applyIvoryTexture', () => {
  function createMockKey() {
    const styles = {};
    return {
      style: {
        setProperty: (prop, val) => { styles[prop] = val; },
        getPropertyValue: (prop) => styles[prop] || '',
      },
      _styles: styles,
    };
  }

  it('sets --kbd-ivory-base CSS variable', () => {
    const key = createMockKey();
    applyIvoryTexture(key, 60);
    expect(key._styles['--kbd-ivory-base']).toMatch(/^hsl\(\d+, \d+%, \d+%\)$/);
  });

  it('sets --kbd-ivory-top CSS variable (lighter)', () => {
    const key = createMockKey();
    applyIvoryTexture(key, 60);
    expect(key._styles['--kbd-ivory-top']).toMatch(/^hsl\(/);
  });

  it('sets --kbd-ivory-bottom CSS variable (darker)', () => {
    const key = createMockKey();
    applyIvoryTexture(key, 60);
    expect(key._styles['--kbd-ivory-bottom']).toMatch(/^hsl\(/);
  });

  it('sets --kbd-dirt-color with opacity', () => {
    const key = createMockKey();
    applyIvoryTexture(key, 60);
    expect(key._styles['--kbd-dirt-color']).toMatch(/^rgba\(105, 90, 75, [\d.]+\)$/);
  });

  it('sets --kbd-dirt-start percentage', () => {
    const key = createMockKey();
    applyIvoryTexture(key, 60);
    expect(key._styles['--kbd-dirt-start']).toMatch(/^\d+%$/);
  });

  it('produces deterministic values for same MIDI note', () => {
    const key1 = createMockKey();
    const key2 = createMockKey();
    applyIvoryTexture(key1, 60);
    applyIvoryTexture(key2, 60);
    expect(key1._styles).toEqual(key2._styles);
  });

  it('produces different values for notes with different seeds', () => {
    const key1 = createMockKey();
    const key2 = createMockKey();
    applyIvoryTexture(key1, 60);
    applyIvoryTexture(key2, 61); // adjacent semitone, different seed
    // At least one property should differ
    const props = ['--kbd-ivory-base', '--kbd-ivory-top', '--kbd-ivory-bottom', '--kbd-dirt-color', '--kbd-dirt-start'];
    const someDifferent = props.some(p => key1._styles[p] !== key2._styles[p]);
    expect(someDifferent).toBe(true);
  });
});

// ══════════════════════════════════════════════════════════════
//  applyVintageWear
// ══════════════════════════════════════════════════════════════

describe('applyVintageWear', () => {
  function createMockKey() {
    const classes = new Set();
    return {
      classList: {
        add: (c) => classes.add(c),
        has: (c) => classes.has(c),
      },
      _classes: classes,
    };
  }

  it('adds kbd-stain-yellow to some white keys', () => {
    // Find a white key where h % 5 === 0
    let found = false;
    for (let note = 0; note < 128; note++) {
      const h = (note * 2654435761) % 100;
      if (h % 5 === 0) {
        const key = createMockKey();
        applyVintageWear(key, note, false);
        expect(key._classes.has('kbd-stain-yellow')).toBe(true);
        found = true;
        break;
      }
    }
    expect(found).toBe(true);
  });

  it('adds kbd-stain-scuff to some white keys', () => {
    let found = false;
    for (let note = 0; note < 128; note++) {
      const h = (note * 2654435761) % 100;
      if (h % 9 === 0) {
        const key = createMockKey();
        applyVintageWear(key, note, false);
        expect(key._classes.has('kbd-stain-scuff')).toBe(true);
        found = true;
        break;
      }
    }
    expect(found).toBe(true);
  });

  it('adds kbd-stain-ding to some white keys', () => {
    let found = false;
    for (let note = 0; note < 128; note++) {
      const h = (note * 2654435761) % 100;
      if (h % 13 === 0) {
        const key = createMockKey();
        applyVintageWear(key, note, false);
        expect(key._classes.has('kbd-stain-ding')).toBe(true);
        found = true;
        break;
      }
    }
    expect(found).toBe(true);
  });

  it('adds kbd-stain-worn to some black keys', () => {
    let found = false;
    for (let note = 0; note < 128; note++) {
      const h = (note * 2654435761) % 100;
      if (h % 7 === 0) {
        const key = createMockKey();
        applyVintageWear(key, note, true);
        expect(key._classes.has('kbd-stain-worn')).toBe(true);
        found = true;
        break;
      }
    }
    expect(found).toBe(true);
  });

  it('black keys never get yellow/scuff/ding stains', () => {
    // Test a range of black key notes
    for (let note = 0; note < 128; note++) {
      const key = createMockKey();
      applyVintageWear(key, note, true);
      expect(key._classes.has('kbd-stain-yellow')).toBe(false);
      expect(key._classes.has('kbd-stain-scuff')).toBe(false);
      expect(key._classes.has('kbd-stain-ding')).toBe(false);
    }
  });

  it('produces deterministic results', () => {
    const key1 = createMockKey();
    const key2 = createMockKey();
    applyVintageWear(key1, 60, false);
    applyVintageWear(key2, 60, false);
    expect(key1._classes).toEqual(key2._classes);
  });

  it('does nothing to a clean key when no stains apply', () => {
    // Find a note where h%5, h%9, h%13 are all non-zero
    for (let note = 0; note < 128; note++) {
      const h = (note * 2654435761) % 100;
      if (h % 5 !== 0 && h % 9 !== 0 && h % 13 !== 0) {
        const key = createMockKey();
        applyVintageWear(key, note, false);
        expect(key._classes.size).toBe(0);
        return;
      }
    }
  });
});
