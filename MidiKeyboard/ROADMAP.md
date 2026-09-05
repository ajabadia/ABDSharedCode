# ABDKeyboard — Roadmap

## Phase 1: Extraction ✅
- [x] Create shared project structure
- [x] Move keyboard.js and keyboard.css
- [x] Unit tests (16 tests, vitest + jsdom)
- [x] Documentation (README, CHANGELOG, HANDOFF, ROADMAP)
- [x] npm package.json with exports

## Phase 2: ABDMS2000 Integration ✅
- [x] Replace inline keyboard code with import from ABDKeyboard
- [x] Delete old keyboardAnimations.js, wheelFilmstrip.js
- [x] Remove dead CSS from main.css

## Phase 3: ABDEep Integration
- [x] Port velocity curves (soft/hard/linear/normal/fixed, yPosition source)
- [x] Port pressure display (aftertouch + modwheel + pitchbend visual feedback)
- [x] Port pitch-bend displacement (keys shift horizontally)
- [x] Port per-key ivory texture (deterministic HSL variations)
- [x] Port LED color by mode (getLedColor callback)
- [x] Implement aftertouch generation (channel + polyphonic modes)
- [x] Implement velocity callback (onVelocityChange)
- [x] Chord Memory — save/replay note groups with visual feedback (12 slots)
- [ ] Test with ABDEep bridge events
- [ ] Update HANDOFF.md

## Phase 4: ABDCZ101 Integration
- [x] Vintage wear stains (ported from ABDCZ101)
- [x] 49-key mode (CZ101_PRESET with numOctaves:4, startNote:48)
- [x] QWERTY CZ-101 range validation (C3–C7)
- [x] ABDCZ101 bridge tests (14 tests: preset, keys, range, sustain, panic, octave)
- [x] Update HANDOFF.md

## Phase 5: Polish
- [x] Panic button — auto-generated to right of keybed with red LED + tooltip
- [x] Panic keyboard shortcut — Ctrl+Q / Cmd+Q triggers All Notes Off
- [x] Panic accessibility — role, tabindex, aria-label, Enter/Space support
- [x] Sustain pedal (CC#64) — auto-generated toggle button with green LED
- [x] Sustain keyboard shortcut — Ctrl+Space / Cmd+Space toggles sustain
- [x] Sustain public API — setSustain, getSustain, toggleSustain, onSustainChange
- [x] Aftertouch — channel + polyphonic pressure generation from pointer movement
- [x] Aftertouch public API — setAftertouch, releaseAftertouch, getAftertouch, onAftertouch
- [x] Velocity callback — onVelocityChange fires on every note-on
- [x] Pressure display tests (11 tests: AT, MW, combined, skip-frame, release, panic)
- [x] Pitch-bend displacement tests (8 tests: positive/negative/half, combine, panic)
- [x] Ivory texture tests (7 tests: CSS vars, deterministic, black keys)
- [x] Vintage wear stain tests (3 tests: white/black keys, disabled)
- [x] LED color callback tests (4 tests: getLedColor, static, fallback)
- [x] Panic button tests (3 tests: auto-gen, click, external panicBtnId)
- [x] Sustain pedal tests (11 tests: toggle, LED, auto-gen, panic release)
- [x] Velocity tests (6 tests: callback, fixedVelocity, CSS, curves, per-note)
- [x] Aftertouch tests (13 tests: channel, polyphonic, release, clamping, pointer, panic)
- [x] Sostenuto pedal (CC#66) — captures active notes only, auto-generated button with orange LED
- [x] Sostenuto keyboard shortcut — Ctrl+Shift+Space toggles sostenuto
- [x] Sostenuto public API — setSostenuto, getSostenuto, toggleSostenuto, onSostenutoChange
- [x] Soft pedal (CC#67) — attenuates velocity, auto-generated button with purple LED
- [x] Soft pedal keyboard shortcut — Ctrl+Alt+Space toggles soft pedal
- [x] Soft pedal public API — setSoftPedal, getSoftPedal, toggleSoftPedal, onSoftPedalChange
- [x] Accessibility (ARIA labels, keyboard navigation for screen readers)
- [x] Collapse/Expand button — chevron to collapse keyboard downward, enableCollapse config
- [x] ResizeObserver — auto re-render keybed when container size changes
- [x] Sostenuto pedal tests (12 tests: toggle, capture, LED, auto-gen, panic, idempotent)
- [x] Soft pedal tests (12 tests: toggle, LED, velocity attenuation, QWERTY, panic)
- [x] Accessibility tests (19 tests: ARIA, live regions, keyboard nav, wheel labels)
- [x] Collapse tests (14 tests: auto-gen, expand/collapse, click/keyboard, chevron, aria)
- [x] ResizeObserver tests (5 tests: creates, disabled, disconnects, re-render, safe after destroy)
- [x] Scale filter — lock keys to a musical scale (major, minor, pentatonic, blues, dorian, etc.)
- [x] Scale filter config — enableScaleFilter, scaleType, scaleRoot, scaleSnapMode
- [x] Scale filter public API — setScaleFilter, getScaleFilter, disableScaleFilter
- [x] Scale filter visuals — dimming of keys outside the scale
- [x] Scale filter tests (19 tests: enable/disable, block/snap modes, scales, root, QWERTY, visuals)
- [x] Chord Memory — save/replay note groups with visual feedback
- [x] Chord Memory config — enableChordMemory, maxChordSlots
- [x] Chord Memory API — saveChord, playChord, releaseChord, getChords, clearChord, clearAllChords
- [x] Chord Memory tests (13 tests: save, play, release, clear, slots, API, disabled mode)
- [x] Animation performance audit (will-change, compositor layers, CSS optimization)
- [x] ABDCZ101 bridge tests (14 tests: preset, 49 keys, QWERTY range, sustain, panic, octave)
- [x] Multi-touch edge cases (pointerId tracking, same-key replacement, sustain + multi-touch, 13 tests)
- [x] README screenshots / GIF demos (feature showcase table, interactive demo docs)
