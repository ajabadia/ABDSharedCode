# Changelog

All notable changes to ABDKeyboard will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [0.1.5] - 2025-08-26

### Added
- Chord Memory — save and replay note groups (from ABDEep roadmap)
  - `enableChordMemory: true` config option
  - `maxChordSlots: 12` — configurable number of slots (1..12)
  - `saveChord(slot, notes?)` — save notes to a slot (or currently active notes)
  - `playChord(slot)` — replay saved chord (calls `onNoteOn` for each note)
  - `releaseChord(slot)` — release chord notes (calls `onNoteOff` for each)
  - `getChords()` — returns array of saved chords
  - `clearChord(slot)` / `clearAllChords()` — clear slots
  - Visual feedback: `.kbd-chord-saved` (amber) and `.kbd-chord-playing` (green) CSS classes
  - Works with both pointer/touch and QWERTY input
  - 13 new unit tests (182 total, up from 169)

## [0.1.4] - 2025-08-26

### Added
- Scale filter — lock keys to a musical scale (major, minor, pentatonic, blues, dorian, etc.)
  - `enableScaleFilter: true` config option
  - `scaleType: 'major'|'minor'|'pentatonic'|'blues'|'dorian'|'phrygian'|'lydian'|'mixolydian'|'minor_pentatonic'|'whole_tone'|'chromatic'`
  - `scaleRoot: 0` — root note (0=C, 2=D, 4=E, 5=F, 7=G, 9=A, 11=B)
  - `scaleSnapMode: 'block'|'snap'` — skip notes outside scale or snap to nearest
  - `setScaleFilter(type, root?)` / `getScaleFilter()` / `disableScaleFilter()` public API
  - Visual dimming of keys outside the scale (CSS class `.kbd-key-outside-scale`)
  - Works with both pointer/touch and QWERTY input
  - 19 new unit tests (169 total, up from 150)

## [0.1.3] - 2025-08-26

### Added
- Sostenuto pedal (CC#66 emulation) — captures active notes only
  - Auto-generated button with orange LED
  - Ctrl+Shift+Space keyboard shortcut
  - `onSostenutoChange(on)` callback for parent app integration
  - `setSostenuto(bool)` / `getSostenuto()` / `toggleSostenuto()` public API
  - Only notes physically held at engagement time are captured
  - Panic automatically releases sostenuto
- Soft pedal (CC#67 emulation) — attenuates velocity
  - Auto-generated button with purple LED
  - Ctrl+Alt+Space keyboard shortcut
  - `onSoftPedalChange(on)` callback for parent app integration
  - `setSoftPedal(bool)` / `getSoftPedal()` / `toggleSoftPedal()` public API
  - `softPedalFactor: 0.65` config for velocity multiplier
  - Affects both pointer and QWERTY input
  - Panic automatically releases soft pedal
- 24 new unit tests (150 total, up from 126)
  - Sostenuto pedal: toggle, capture, LED, auto-gen, panic, idempotent
  - Soft pedal: toggle, LED, velocity attenuation, QWERTY, panic

## [0.1.2] - 2025-08-25

### Added
- Sustain pedal (CC#64 emulation) — auto-generated toggle button with green LED
  - Ctrl+Space / Cmd+Space keyboard shortcut
  - `onSustainChange(on)` callback for parent app integration
  - `setSustain(bool)` / `getSustain()` / `toggleSustain()` public API
  - Panic automatically releases sustain
  - Pass `sustainBtnId` to bind to external DOM element
- Velocity control — `onVelocityChange(note, velocity)` callback fires on every note-on
  - `velocitySource: 'yPosition'` calculates from touch Y position
  - `fixedVelocity: 0.85` config for constant velocity
  - `velocityCurve: 'soft'|'hard'|'linear'|'normal'|'fixed'` response curves
- Aftertouch — channel and polyphonic pressure generation from pointer movement
  - `enableAftertouch: true` enables Y-movement pressure on held keys
  - `aftertouchMode: 'channel'|'polyphonic'` — shared or per-note pressure
  - `aftertouchSensitivity: 0.5` — pointer-to-pressure mapping (0..1)
  - `onAftertouch(note, pressure)` callback — note=-1 for channel, note=N for polyphonic
  - `setAftertouch(note, pressure)` / `releaseAftertouch(note)` / `getAftertouch()` API
  - Integrates with `enablePressureDisplay` for visual feedback on held keys
- 30 new unit tests (88 total, up from 58)
  - Sustain pedal: toggle, LED, auto-gen, panic release, idempotent
  - Velocity: callback, fixedVelocity, CSS variable, curves, per-note
  - Aftertouch: channel/polyphonic API, release, clamping, pointer generation, modes, panic

## [0.1.1] - 2025-08-25

### Added
- Auto-generated panic button (All Notes Off) to the right of the keybed
  - Synthesizer-style aesthetic with red LED indicator
  - Tooltip: "Panic: All Notes Off (Ctrl+Q)"
  - Full accessibility: role, tabindex, aria-label, Enter/Space
  - LED flashes red during panic strobe animation
- Keyboard shortcut: Ctrl+Q / Cmd+Q triggers panic (All Notes Off)
  - Works even when QWERTY mode is disabled
  - `preventDefault()` blocks browser close-tab behavior
- 37 new unit tests (58 total, up from 21)
  - Pressure display: aftertouch, modwheel, combined, skip-frame, release, panic
  - Pitch-bend displacement: positive/negative/half bend, combine, panic
  - Ivory texture: CSS variables, deterministic, black keys
  - Vintage wear stains: white/black keys, disabled mode
  - LED color callback: getLedColor, static, fallback
  - Panic button: auto-gen, click handler, external panicBtnId
  - QWERTY: Ctrl+Q and Cmd+Q panic shortcuts

### Fixed
- Test bug: `sweep applies led-sweep class asynchronously` used deprecated `done()` callback — converted to async/await
- Test bug: `enablePitchBendDisplace` and `enablePressureDisplay` tests missing `getPressureState` in config — pressure loop never started
- Test bug: "skips frame" test assertion incorrect — DOM skip optimization verified via tamper check instead of call count

## [0.1.0] - 2025-08-25

### Added
- Unified keyboard component extracted from ABDMS2000, ABDEep, and ABDCZ101
- Piano keybed with responsive key count (2–5 octaves)
- Velocity curves: soft, hard, linear, fixed, normal (Y-position source)
- Pressure display: aftertouch + modwheel + pitchbend visual feedback
- Pitch-bend displacement: keys shift horizontally
- Per-key ivory texture with deterministic HSL variations
- Vintage wear stains: yellowing, scuffs, dings
- QWERTY keyboard mapping (Ableton/JUCE standard)
- Arrow key octave shift (Up/Down)
- Pitch/mod wheel filmstrip (101 frames, sprite-based)
- Spring return for pitch wheel (auto-reset on release)
- Octave buttons with LED indicators (solid + blink states)
- LED sweep cascade animation (configurable direction + speed)
- Panic flash: triple strobe on All Notes Off
- Touch support for mobile/tablet
- Full public API: panic, sweep, setOctave, highlightNote, releaseNote, etc.
- CSS theme tokens for full customization
- Unit tests with vitest + jsdom
