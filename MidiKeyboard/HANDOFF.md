# ABDKeyboard — Handoff

## What Was Done

Extracted the unified keyboard component from ABDMS2000 into a standalone shared library (`@abdsynths/keyboard`). The component merges the best features from three projects:

- **ABDMS2000**: Responsive key count, wheel filmstrip, touch support
- **ABDEep**: Velocity curves, pressure display, pitch-bend displacement, ivory texture, LED color by mode
- **ABDCZ101**: Vintage wear stains, QWERTY keyboard

### New: Panic Button + Keyboard Shortcut

- **Auto-generated panic button** renders to the right of the keybed when no `panicBtnId` is provided
- **Tooltip**: "Panic: All Notes Off (Ctrl+Q)" on hover
- **Ctrl+Q / Cmd+Q** keyboard shortcut triggers panic from anywhere (works even when `enableQwerty` is false)
- Visual feedback: red LED flash on the button during panic strobe
- Full accessibility: `role="button"`, `tabindex="0"`, `aria-label`, Enter/Space support

### New: Sustain Pedal (CC#64)

- **Auto-generated sustain toggle button** renders next to the panic button (green LED when active)
- **Ctrl+Space / Cmd+Space** keyboard shortcut toggles sustain
- **`onSustainChange(on)`** callback notifies parent app (on/off)
- **`setSustain(bool)`** / **`getSustain()`** / **`toggleSustain()`** public API
- **Panic releases sustain** automatically
- Pass `sustainBtnId` to bind to an external DOM element

### New: Sostenuto Pedal (CC#66)

- **Auto-generated sostenuto button** renders next to sustain (orange LED when active)
- **Ctrl+Shift+Space** keyboard shortcut toggles sostenuto
- **`onSostenutoChange(on)`** callback notifies parent app (on/off)
- **`setSostenuto(bool)`** / **`getSostenuto()`** / **`toggleSostenuto()`** public API
- Captures all currently active notes when engaged — only those notes are sustained
- Notes pressed AFTER the pedal is engaged are NOT captured
- **Panic releases sostenuto** automatically
- Pass `sostenutoBtnId` to bind to an external DOM element

### New: Soft Pedal (CC#67)

- **Auto-generated soft pedal button** renders next to sostenuto (purple LED when active)
- **Ctrl+Alt+Space** keyboard shortcut toggles soft pedal
- **`onSoftPedalChange(on)`** callback notifies parent app (on/off)
- **`setSoftPedal(bool)`** / **`getSoftPedal()`** / **`toggleSoftPedal()`** public API
- Attenuates velocity when active: `velocity *= softPedalFactor` (default: 0.65)
- Affects both pointer and QWERTY input
- **Panic releases soft pedal** automatically
- Pass `softPedalBtnId` to bind to an external DOM element

### New: Velocity Control

- **`onVelocityChange(note, velocity)`** callback fires on every note-on with the computed velocity
- **`velocitySource: 'yPosition'`** — velocity calculated from vertical touch position
- **`velocitySource: 'fixed'`** — constant velocity (default: 0.85)
- **`velocityCurve: 'soft'|'hard'|'linear'|'normal'|'fixed'`** — applies response curve
- **`fixedVelocity: 0.85`** — override the fixed velocity value

### New: Aftertouch (Channel + Polyphonic)

- **`enableAftertouch: true`** (config) — enables aftertouch generation from pointer Y movement on held keys
- **`aftertouchMode: 'channel'|'polyphonic'`** — channel (shared) or per-note pressure
- **`aftertouchSensitivity: 0.5`** — how much pointer Y-movement maps to pressure (0..1)
- **`onAftertouch(note, pressure)`** callback — `note=-1` for channel, `note=60` for polyphonic
- **`setAftertouch(note, pressure)`** — software can inject aftertouch values
- **`releaseAftertouch(note)`** — release aftertouch (pressure=0)
- **`getAftertouch()`** — returns current channel aftertouch (0..1)
- Integrates with **`enablePressureDisplay`** for visual feedback on held keys

### New: Scale Filter

- **`enableScaleFilter: true`** (config) — locks keys to a musical scale
- **`scaleType`** — `'major'` | `'minor'` | `'pentatonic'` | `'blues'` | `'dorian'` | `'phrygian'` | `'lydian'` | `'mixolydian'` | `'minor_pentatonic'` | `'whole_tone'` | `'chromatic'`
- **`scaleRoot`** — root note (0=C, 2=D, 4=E, 5=F, 7=G, 9=A, 11=B)
- **`scaleSnapMode: 'block'|'snap'`** — `'block'` (skip notes outside scale) or `'snap'` (snap to nearest in-scale note)
- **`setScaleFilter(type, root?)`** — enable scale filter programmatically
- **`getScaleFilter()`** — returns `{ type, root, snapMode, enabled }`
- **`disableScaleFilter()`** — remove scale lock, restore all keys
- Keys outside the scale are visually dimmed (CSS class `.kbd-key-outside-scale`)
- Works with both pointer/touch and QWERTY input
- QWERTY notes that fall outside the scale are blocked in `'block'` mode, or snapped to nearest in-scale note in `'snap'` mode

### New: Chord Memory (from ABDEep)

- **`enableChordMemory: true`** (config) — enables chord memory feature
- **`maxChordSlots: 12`** — number of available chord slots (1..12)
- **`saveChord(slot, notes?)`** — save notes to a slot; if notes not provided, saves currently active notes
- **`playChord(slot)`** — replay saved notes from a slot (calls `onNoteOn` for each)
- **`releaseChord(slot)`** — release all notes in a slot (calls `onNoteOff` for each)
- **`getChords()`** — returns array of saved chord arrays (or null for empty slots)
- **`clearChord(slot)`** — clear a specific slot
- **`clearAllChords()`** — clear all slots
- Notes are saved sorted by MIDI number for consistency
- Visual feedback: keys show `.kbd-chord-saved` (amber) and `.kbd-chord-playing` (green) CSS classes
- Works with both pointer/touch and QWERTY input

## Architecture

```
ABDKeyboard/
├── src/
│   ├── keyboard.js    — createKeyboard() factory, stateful logic, ~1053 lines
│   ├── utils.js       — Pure functions (scales, velocity, textures), ~118 lines
│   └── keyboard.css   — Theme tokens + all button/animation styles, ~881 lines
├── tests/
│   ├── keyboard.test.js             — 182 tests (core, pedals, accessibility, collapse, resize)
│   ├── utils.test.js                — 75 tests (pure functions: scales, velocity, textures)
│   ├── chord-memory-bridge.test.js  — 34 tests (ABDEep bridge integration)
│   ├── cz101-bridge.test.js         — 14 tests (CZ-101 preset integration)
│   └── multitouch.test.js           — 13 tests (multi-touch edge cases)
├── demo/
│   ├── index.html     — Interactive demo page
│   ├── keyboard-demo.gif — Recording of demo
│   └── capture.mjs    — Frame capture script for GIF
├── package.json       — vitest config
└── README.md, CHANGELOG.md, HANDOFF.md, ROADMAP.md

Total: 318 tests, 2052 lines of source code
```

## Key Design Decisions

1. **Factory pattern** — `createKeyboard(deps)` returns a public API object. No classes, no singletons.
2. **Bridge-agnostic** — Callbacks (`onNoteOn`, `onNoteOff`, etc.) are injected. No direct bridge imports.
3. **CSS theme tokens** — All colors/fonts via `--kbd-*` CSS variables. Each project overrides via `:root`.
4. **Feature flags** — Each advanced feature is opt-in via config (pressure, ivory, vintage, QWERTY).
5. **Per-key `data-note`** — DOM keys carry `data-note` for external highlight/release.
6. **Auto-generated panic button** — When no `panicBtnId` is provided, a styled button is rendered automatically inside the keybed container. Pass `panicBtnId` to bind to an external element instead.
7. **Ctrl+Q shortcut** — Checked before the `ctrlKey` guard in `handleKeydown`, so it works regardless of `enableQwerty`.
8. **Sustain pedal toggle** — Auto-generated next to panic button. `Ctrl+Space` shortcut. `onSustainChange` callback for parent app integration.
9. **Sostenuto pedal (CC#66)** — Captures active notes when engaged. `Ctrl+Shift+Space` shortcut. Only notes physically held at engagement time are sustained.
10. **Soft pedal (CC#67)** — Attenuates velocity via `softPedalFactor`. `Ctrl+Alt+Space` shortcut. Affects both pointer and QWERTY input.
11. **Velocity as data** — `onVelocityChange` fires per note-on. `yPosition` source calculates from touch. Curves applied to raw value.
12. **Aftertouch generation** — `enableAftertouch` + `aftertouchMode` config. Pointer Y-movement on held keys generates pressure. `onAftertouch` callback sends channel (note=-1) or polyphonic (note=N) values. Software can also inject via `setAftertouch()`.

## Integration Status

| Project | Status | Import Path |
|---|---|---|
| ABDMS2000 | ✅ Active | `WebUI/src/components/keyboard.js` (copy) |
| ABDEep | ✅ Tested | Bridge integration tests (34 tests) |
| ABDCZ101 | ✅ Tested | CZ-101 preset integration tests (14 tests) |

## How to Integrate into a New Project

### Step 1: Copy the source files

```bash
cp -r ABDKeyboard/src/ ./src/components/keyboard/
# Files: keyboard.js, utils.js, keyboard.css
```

### Step 2: Create the required DOM

```html
<!-- Minimum required: just a container div -->
<div id="piano-keyboard"></div>

<!-- Optional: external buttons (auto-generated if omitted) -->
<button id="oct-up">Oct+</button>
<button id="oct-down">Oct-</button>
<div id="led-up"></div>
<div id="led-down"></div>

<!-- Optional: pitch/mod wheels (auto-generated if omitted) -->
<div id="pitch-wheel"></div>
<div id="mod-wheel"></div>
```

### Step 3: Include the CSS

```html
<link rel="stylesheet" href="src/components/keyboard/keyboard.css">
```

### Step 4: Initialize the keyboard

```js
import { createKeyboard } from './src/components/keyboard/keyboard.js';

const kbd = createKeyboard({
  containerId: 'piano-keyboard',  // REQUIRED — the container div ID
  onNoteOn: (note, vel) => bridge.noteOn(note, vel),
  onNoteOff: (note) => bridge.noteOff(note),
});
// That's it — panic + sustain buttons auto-generated!
```

### Step 5 (optional): Use pure utilities directly

```js
import { midiToName, SCALE_INTERVALS, isInScale, CZ101_PRESET } from './src/components/keyboard/utils.js';

midiToName(60);                    // 'C4'
isInScale(61, 60, SCALE_INTERVALS.major);  // false (C# not in C major)
```

### Minimal HTML (hello world)

```html
<!DOCTYPE html>
<html><head>
  <link rel="stylesheet" href="src/keyboard.css">
</head><body>
  <div id="piano-keyboard"></div>
  <script type="module">
    import { createKeyboard } from './src/keyboard.js';
    createKeyboard({
      containerId: 'piano-keyboard',
      onNoteOn: (n, v) => console.log('Note ON:', n, v),
      onNoteOff: (n) => console.log('Note OFF:', n),
    });
  </script>
</body></html>
```

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+Q / Cmd+Q | Panic — All Notes Off + flash |
| Ctrl+Space / Cmd+Space | Toggle Sustain Pedal (CC#64) |
| Ctrl+Shift+Space | Toggle Sostenuto Pedal (CC#66) |
| Ctrl+Alt+Space | Toggle Soft Pedal (CC#67) |
| Arrow Up | Shift octave up |
| Arrow Down | Shift octave down |
| Z–M (lower row) | Play notes (when QWERTY enabled) |
| Q–] (upper row) | Play notes (when QWERTY enabled) |

### New: Sostenuto Pedal (CC#66)

- **`enableSostenuto: true`** (config) — enables sostenuto pedal
- Captures all currently active notes when engaged; notes pressed AFTER are NOT captured
- **`setSostenuto(bool)`** / **`getSostenuto()`** / **`toggleSostenuto()`** public API
- **`onSostenutoChange(on)`** callback for parent app integration
- **Ctrl+Shift+Space** keyboard shortcut
- Auto-generated button with orange LED
- **Panic releases sostenuto** automatically

### New: Soft Pedal (CC#67)

- **`enableSoftPedal: true`** (config) — enables soft pedal
- Attenuates velocity: `velocity *= softPedalFactor` (default: 0.65)
- **`setSoftPedal(bool)`** / **`getSoftPedal()`** / **`toggleSoftPedal()`** public API
- **`onSoftPedalChange(on)`** callback for parent app integration
- **Ctrl+Alt+Space** keyboard shortcut
- Auto-generated button with purple LED
- **Panic releases soft pedal** automatically

### New: ABDCZ101 Preset

- **`CZ101_PRESET`** exported constant with CZ-101-specific config
- 49 keys (4 octaves, C3–C7), vintage wear enabled, no wheels
- **`import { CZ101_PRESET } from '@abdsynths/keyboard'`**
- Usage: `createKeyboard({ ...CZ101_PRESET, onNoteOn: ... })`

## Test Coverage (230 tests)

| Section | Tests | Coverage |
|---|---|---|
| Core (createKeyboard) | 16 | Render, destroy, octave, panic, API |
| Velocity Curves | 1 | Fixed velocity |
| QWERTY Keyboard | 7 | Note on/off, repeat, arrows, Ctrl+Q |
| LED Animations | 2 | Sweep, empty keybed |
| Pressure Display | 11 | AT, MW, combined, skip-frame, release, panic |
| Pitch-Bend Displacement | 8 | Positive/negative/half, combine, panic |
| Ivory Texture | 7 | CSS vars, deterministic, black keys |
| Vintage Wear Stains | 3 | White/black keys, disabled |
| LED Color Callback | 4 | getLedColor, static, fallback |
| Panic Button | 3 | Auto-gen, click, external panicBtnId |
| Sustain Pedal | 11 | Toggle, LED, auto-gen, panic release |
| Sostenuto Pedal | 12 | Toggle, capture, LED, auto-gen, panic, idempotent |
| Soft Pedal | 12 | Toggle, LED, velocity attenuation, QWERTY, panic |
| Velocity | 6 | Callback, fixedVelocity, CSS, curves, per-note |
| Aftertouch | 13 | Channel, polyphonic, release, clamping, pointer, panic |
| Scale Filter | 19 | Enable/disable, block/snap, scales, root, QWERTY, visuals |
| Chord Memory | 13 | Save, play, release, clear, slots, API, disabled mode |
| ResizeObserver | 5 | Creates, disabled, disconnects, re-render, safe after destroy |
| Accessibility | 19 | ARIA, live regions, keyboard nav, wheel labels |
| Collapse | 14 | Auto-gen, expand/collapse, click/keyboard, chevron, aria |
| Chord Memory Bridge | 34 | Save, play, release, workflow, edge cases |
| ABDCZ101 Bridge | 14 | Preset, 49 keys, QWERTY range, sustain, panic, octave |

## Known Issues

- None at extraction time.

## Files to Touch for Modifications

- `src/keyboard.js` — Core logic, key rendering, event handlers, all pedals, aftertouch, scale filter, chord memory, CZ-101 preset
- `src/keyboard.css` — Visual styling, theme tokens, all button styles, scale filter dimming, chord memory, sostenuto/soft pedal
- `tests/keyboard.test.js` — Unit tests (182 tests)

## Architecture Fixes (v1.1.0)

### Keybed Horizontal Flex Row (.kbd-keys-wrapper)
- Fixed root issue where keys collapsed into an ultra-narrow vertical strip (zipper artifact) when .kbd-keys-wrapper was missing explicit display: flex; flex-direction: row; flex: 1.
- Added contract into src/keyboard.css and ABDSharedAssets/styles/components/keyboard.css.

### Unified Dynamic Theming (AudioLab Dark & Light)
- Added query param support ?theme=audiolab / ?theme=audiolab-light on load.
- Added window.setTheme() hook responsive to data-theme changes from JuceWebView2Component.
- Cleaned up right-side controls layout: autoPanic and autoSustain end-cap buttons align cleanly at the right edge of #piano-keyboard.
