# Changelog

All notable changes to the **ABDScope** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.4.0] - 2026-10-06
### Added
- **Absorption into ABDSharedCode**: ABDScope now lives as the `Scope/` subtree of
  `ABDSharedCode` (git subtree, history preserved). The canonical consumption path
  is `ABDShared::ScopeCore` / `ABDShared::ScopeCoreHeaders` / `ABDShared::ScopeWebAssets`
  (aliases registered in `ABDSharedCode/CMakeLists.txt` over the targets defined
  here). Themes and icons stay canonical in `ABDSharedAssets`; the theme parity
  test (`WebUI/tests/theme.test.js`) keeps guarding that boundary.
### Changed
- **JUCE probe and GuiDemo are standalone-only**: the `C:/JUCE` probe and the
  `ABDScope_GuiDemo` target now run only when ABDScope is the top-level project
  (`CMAKE_SOURCE_DIR == CMAKE_CURRENT_SOURCE_DIR`). Consumers that pull Scope via
  `add_subdirectory` bring their own JUCE (or none, as in the Emscripten builds)
  and must not get an unrelated JUCE tree or a demo app in their build.
- **Version alignment** across CMake, package.json files and docs set to 0.4.0.

---

## [0.3.2] - 2026-09-04
### Added
- Full first-class support for `audiolab-light` theme ("Técnica Refinada" Precision Lab / Sonarworks style: clean light `#f8f9fa` base, `#ffffff` surface, subtle reticle `rgba(15,23,42,0.08)`, and high-contrast `#00a86b` / `#0284c7` traces).
- Reactive `pageLoaded` event listener in `JuceWebScopeComponent` for reliable, race-free theme initialization on page load.
- Query string (`?theme=...`) and fragment sanitization in `ScopeResourceProvider`, allowing URL-driven configuration without breaking embedded binary asset resolution.
### Changed
- Refactored `OscilloscopeRenderer` and `SpectrumRenderer` to dynamically resolve canvas background and grid reticle colors via CSS custom properties (`--scope-bg`, `--scope-grid`, `--scope-grid-center`).
- Improved active state button text contrast in light mode with `#ffffff` text on accent green.

---

## [0.3.1] - 2026-09-04

### Fixed
- **C++ smoke test was inert in Release**: `StandaloneSmoke.cpp` relied on `assert()`, which `NDEBUG` strips from Release builds, and asserted stale JSON keys (`time`, `samplesL`) that `ScopeFrameSerializer` never emits. Rewritten with explicit runtime checks against the real wire contract (`timeDataL`, `numSamples`, `tapId`, ...); the build now fails when the contract breaks, in both Debug and Release.
- **`AnalyserInput.destroy()` cleanup** now releases the real references (`analyserL`/`analyserR`, `timeBufferL`/`timeBufferR`, `freqBuffer`) instead of properties that never existed.

### Changed
- **Deterministic tap wire ids**: `ScopeTap` / `ScopeDataCollector::registerTap` accept an optional stable `id` (slug). The serializer emits the explicit id, or a deterministic snake_case slug derived from the display name (`makeSlug`). `selectTap()` / `findTapIndex()` resolve explicit id -> display name / derived slug -> lenient substring. WebUI `availableTaps[].id` must match the C++ id (see `docs/INTEGRATION_GUIDE.md` §10.1).
- **`detectedNoteName` now includes the octave** (e.g. `"A4"`, `"B5"`), matching the JS trigger engine and `docs/DATA_CONTRACT.md`.
- **`TriggerDetector` defaults to peak-scaled adaptive hysteresis** (`AUTO_HYSTERESIS`), as documented for sub-bass tracking.
- **`JuceWebScopeComponent` on-demand tap activation**: lane subscriptions (`SET_ACTIVE_TAP { tapId, laneIdx }`) activate only the taps referenced by lanes; an all-active fallback is kept until the first subscription arrives (see class docs).
- **WebUI demo relocated** from `examples/browser_sandbox` to `WebUI/demo` so all guides and launch scripts agree; `npm run demo` serves it on port 8080.
- **Version alignment** across CMake, package.json and docs set to 0.3.1.

### Verified
- **`JuceWebScopeComponent` compiled against real JUCE**: `JuceWebScopeComponent.h` and `ScopeResourceProvider.cpp` (with the generated `ABDScopeWebAssets.h`) compile clean with MSVC (`/W4 /std:c++20`) against JUCE 8.0.12 + the WebView2 SDK NuGet package. This closes the earlier caveat that the component had only been reviewed by hand.
- **WebView2 compile requirements documented**: consuming targets must define `JUCE_USE_WIN_WEBVIEW2=1` (defaults to 0 in `juce_gui_extra`, which hides `withResourceProvider`) and have the `Microsoft.Web.WebView2` NuGet package installed — see `docs/INTEGRATION_GUIDE.md` §9.2.
- **Reproducible installs**: both `package-lock.json` files (repo root + `WebUI/`) are committed; use `npm ci` instead of `npm install` for deterministic dependency trees.

---

## [0.3.0] - 2026-09-03

### Added
- **Multi-Tap Lane Isolation & Concurrency**:
  - `JuceWebScopeComponent` tracks per-lane tap subscriptions (`laneTaps`) via `{ type: 'SET_ACTIVE_TAP', tapId, laneIdx }`.
  - Concurrently serializes all active taps into a multi-tap JSON bundle (`{ "taps": { "tapId": {...} } }`), enabling independent lanes to run live simultaneously without cross-talk or stalling.
  - `EmbeddedMount` routes bundled frames to matching lanes according to `lane.activeTap === frame.tapId`.
- **Sub-Sample Phase Lock (`triggerFraction`)**:
  - `ScopeFrameSerializer` now extracts and serializes `triggerFraction` alongside `triggerIndex`.
  - `OscilloscopeRenderer` performs sub-sample interpolation for analog-grade jitter-free oscilloscope traces.

### Changed
- **Contiguous 1:1 Audio Frame Extraction**:
  - Removed variable fractional decimation (`step = readCount / targetSamples`) for audio signals in `ScopeFrameSerializer.h`.
  - Frames now take the most recent contiguous block of `targetSamples` at true audio rate (48 kHz), eliminating artificial waveform stretch, frequency warping (e.g. 1000 Hz appearing as 1406 Hz), and trigger hunting.

---

## [0.2.0-beta] - 2026-09-02

### Added
- **Multi-Lane Responsive 2-Column Grid Architecture**:
  - `LaneController` managing modular lanes with independent canvases, DPR scaling, and `ResizeObserver`.
  - Responsive 2-column grid layout with per-lane column span (`span 1` for compact modes like Lissajous and Phase; `span 2` for panoramic modes like Oscilloscope, FFT Spectrum, and Waterfall).
  - **Auto-Expansion for Solitary Lanes**: 1-column lanes that sit alone on a row without a partner automatically expand to 2 columns (100% full width), eliminating empty gaps.
  - **Intelligent Non-Duplicated Assignment**: Adding new lanes dynamically assigns the first unused visualization mode and input signal probe, with 100% state preservation of existing lanes.
  - Manual column span toggle button (`[ ½ ]` / `[ 1 ]`) in each lane header.
  - Automatic container query / narrow viewport fallback (`< 480px` forces full width).
  - Configurable `maxLanes` parameter (default: 1) with instant layout switch buttons (`[ 1 ]`, `[ 2 ]`, `[ 3 ]`, `[ 4 ]`).
  - Automatic vertical scrolling and guaranteed minimum lane height (`minLaneHeight: 130px`).
- **Per-Lane Tools & Telemetry**:
  - Independent per-lane **Freeze / Hold** button allowing reference signal A/B comparison against live audio.
  - Independent per-lane **Snapshot** PNG export button.
  - Dynamic probe selector dropdown populated from caller's `availableTaps`.
  - Independent real-time Note & Frequency badge (`A4 (440 Hz)` / `-12 dB`) displayed inside each lane header.
- **Sub-Bass Pitch Lock & Trigger Stabilization (< 140 Hz down to 20 Hz)**:
  - Peak-amplitude adaptive hysteresis in JavaScript (`trigger.js`) and C++ (`TriggerDetector.h`).
  - Extended 4096-sample analysis window eliminating waveform jitter and phase hopping on deep bass notes (30 Hz - 65 Hz).
- **Native C++ Standalone GUI Demo**:
  - `ABDScope Native GUI Demo.exe` compiled with MSVC C++20 and linked with JUCE graphics.
- **Unified Build & Test Pipeline (`build.bat`)**:
  - CMake C++20 MSVC release compilation + Standalone C++ smoke sanity verification + Vitest unit test suite (56 tests, 100% pass).

---

## [0.1.0-alpha] - 2026-09-02

### Added
- **Repository Setup**: Initial directory tree, `.gitignore`, `package.json`, `vitest.config.js`.
