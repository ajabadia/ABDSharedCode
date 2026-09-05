# ABDScope

> **Universal, lock-free, zero-copy audio visualizer and telemetry engine** for the ABDSynths plugin ecosystem and scientific diagnostic tools.

[![Architecture](https://img.shields.io/badge/Architecture-3--Tier%20Modular%20Pipeline-blue.svg)](ARCHITECTURE_SPEC.md)
[![Tests](https://img.shields.io/badge/Tests-56%20Passing%20(100%25)-brightgreen.svg)](WebUI/tests)
[![C++](https://img.shields.io/badge/C%2B%2B-20%20Lock--Free%20SPSC-purple.svg)](Source/Core)
[![License](https://img.shields.io/badge/License-Proprietary-red.svg)](#)

---

## 🚀 Key Capabilities

- **Responsive 2-Column Multi-Lane Grid & Intelligent Lane Packing**:
  - Configurable stacked lanes (`maxLanes: 1..N`) with dynamic layout switcher buttons (`[ 1 ]`, `[ 2 ]`, `[ 3 ]`, `[ 4 ]`).
  - **Auto-expansion for solitary lanes**: Compact modes (Lissajous, Phase) automatically pair 50%/50% side-by-side or expand to full 100% width when solitary, leaving zero empty space.
  - **Intelligent non-duplicated mode and tap selection**: Adding a new lane automatically assigns the first unused visualization mode and signal probe, with full state preservation of existing lanes.
  - **Manual column span toggle**: `[ ½ ]` (50% half-width) vs `[ 1 ]` (100% full-width) per lane.
  - **Per-lane Freeze & Snapshot**: Freeze any lane for live A/B reference comparisons against active audio signals, and export instant PNG snapshots.
  - **Independent per-lane probe selector**: Any lane can tap into any host telemetry point (`availableTaps`: Master, Osc 1, Filter, LFO, etc.).
  - Automatic vertical scrolling with minimum lane height guarantees (`130px`).
- **Studio-Grade Analysis & Sub-Bass Pitch Lock**:
  - Peak-scaled adaptive hysteresis and 4096-sample analysis window for jitter-free trigger stabilization on deep sub-bass (< 140 Hz down to 20 Hz).
  - Octave-adaptive cycle fitting (`1`, `2`, `4`, `8` cycles) with fundamental pitch detection and live note/Hz badges per lane.
  - Logarithmic FFT spectrum analyzer (20 Hz - 20 kHz) with decay ballistics and peak hold.
  - Stereophonic Lissajous Vectorscope ($M/S$ rotated $45^\circ$) with analog persistence simulation.
  - Stereo Phase Correlation meter with companion dual VU ballistics.
  - Real-time continuous Spectrogram Waterfall with plasma/inferno color palettes.
- **Dual-Input Engine**:
  - **Direct Web Audio API**: Pumps `AnalyserNode` streams for standalone web / WASM environments.
  - **Bridge / Push**: Decoupled JSON streaming from C++ JUCE engines via 30 Hz atomic IPC wire protocol.
- **Zero Audio-Thread Overhead**:
  - Lock-free C++20 SPSC ring buffers (`SpscRingBuffer.h`) with atomic on-demand measuring (`isTapActive`). Inactive measuring points consume 0 CPU cycles and 0 memory copies.
- **Flexible Mount Modes**:
  - **Embedded Panel**: Mounts seamlessly inside any host synthesizer DOM container with auto `ResizeObserver`.
  - **Floating Modal**: Draggable, resizable desktop overlay window with smooth backdrop blur.
- **Multi-Platform Native & Web**:
  - Pure ES Module WebUI.
  - Native JUCE 2D Component for C++ Standalone tools (`ABDScope Native GUI Demo.exe`).

---

## 📂 Repository Structure

```text
ABDScope/
├── Source/                       # C++ Engine & JUCE wrappers
│   ├── Core/                     # Pure C++20 lock-free SPSC multi-tap collectors & DSP
│   ├── JUCE/                     # Native JUCE graphics components
│   ├── StandaloneDemo/           # Native Standalone GUI Application Demo
│   └── tests/                    # C++ Standalone Sanity Smoke Test
├── WebUI/                        # Modern WebUI Frontend (ES Modules)
│   ├── index.html                # Production embedded scope host page
│   ├── src/                      # Core JS engine, inputs, mounts, and renderers
│   │   ├── scope.js              # Factory / orchestrator (createScope)
│   │   ├── frame.js              # ScopeDataFrame normalization & metrics
│   │   ├── trigger.js            # Sub-sample pitch lock & adaptive hysteresis
│   │   ├── scope.css             # Theme tokens (CSS custom properties only)
│   │   ├── mount/                # MountBase + Mounts, LaneController/LaneView, layout & DOM helpers
│   │   ├── renderers/            # Oscilloscope, Spectrum, Lissajous, Phase, Spectrogram, VU
│   │   ├── input/                # AnalyserInput (WebAudio) & PushInput (Bridge)
│   │   ├── icons.js             # Generated icon module (canonical: ABDSharedAssets/icons)
│   │   └── utils/                # exportImage (PNG clipboard/download)
│   └── tests/                    # Vitest unit test suite (59 tests)
├── docs/                         # Integration guides, data contracts, and usage manuals
├── ARCHITECTURE_SPEC.md          # Complete technical architectural specification
├── ROADMAP.md                    # Roadmap milestone tracking
└── build.bat                     # Unified C++ CMake & WebUI test automation script
```

---

## ⚡ Quick Start

### 0. Install JavaScript Dependencies (two committed lockfiles)
The repo keeps **two self-contained npm toolchains** — repo root (`Vitest` suite over `WebUI/tests`, happy-dom) and `WebUI/` (jsdom suite). Each has its own committed `package-lock.json`, so install deterministically with `npm ci`:

```cmd
npm ci
cd WebUI && npm ci
cd ..
```

### 1. Build and Run All Tests (C++ MSVC & Vitest)
```cmd
build.bat
```

### 2. Standalone Host Page (WebUI/index.html)
```cmd
cd WebUI
npm run serve
```
Open `http://localhost:8080/` in any browser. This is the embedded WebView2 host page: it renders the canonical dark theme and receives live frames via `pushFrame`/IPC. Per-theme palettes are **not** bundled with the module anymore — hosts adopting the ABDSynths design system supply them through the shared cascade (`@abdsynths/shared` themes + component adapter, see `docs/INTEGRATION_GUIDE.md`). The interactive `WebUI/demo/` harness was removed (2026-09-05).

> `start.bat` is an alternative launcher that serves the whole `WebUI/` folder (with the embedded host page `index.html`) on port 8391.

---

## 📖 Integration Example (Host Synthesizer)

```javascript
import { createScope } from './scope/scope.js';

const scope = createScope({
  containerId: 'scope-container',
  mountMode: 'embedded',
  maxLanes: 4,
  layout: '2',
  availableTaps: [
    { id: 'master', name: 'Master Out' },
    { id: 'osc1',   name: 'Oscillator 1' },
    { id: 'filter', name: 'Ladder Filter' }
  ],
  onTapChange: (tapId, laneIdx) => {
    bridge.setActiveScopeTap(tapId, laneIdx);
  }
});

// Receive streaming telemetry frames from C++ timer callback (30 FPS)
window.addEventListener('message', (e) => {
  if (e.data?.event === 'scopeFrame' && e.data?.payload) {
    scope.pushFrame(e.data.payload);
  }
});
```

---

## 📜 License

Proprietary © 2026 ABDSynths. All rights reserved.
