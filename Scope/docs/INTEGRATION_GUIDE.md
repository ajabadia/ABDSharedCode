# ABDScope Integration Guide (Zero-Copy)

> **Objective:** Connect `ABDScope` into any synthesizer plugin (`ABDMS2000`, `ABDCZ101`, `ABDEep`, `ABDJUNiO601`, `ABDAudioLab`) in under 5 minutes with zero code duplication, multi-lane support, zero-copy DSP taps, and live hot-reloading.

---

## 1. WebUI Linking

There are two supported web-linking strategies (choose one). **ABDMS2000 uses the copy/sync strategy (§1.1)** because the web build runs on plain native ES modules without a bundler (see §1.2 for the Vite path, deferred for now).

### 1.1. Copy/Sync Into Web Root — the `sync_scope.js` pattern (**ABDMS2000, chosen**)

When the web UI is served as **native ES modules from the web root** (no bundler — e.g. `sirv-cli WebUI --port 8384 --cors --single --dev`), files outside `WebUI/` produce 404s and native ESM cannot `import './x.css'`. ABDScope is therefore **copied** into the consumer's web root on every start, and the original in `ABDScope/WebUI/src` remains the **single source of truth**.

Create a sync script in `Scripts/sync_scope.js` (modeled on `sync_bankmanager.js`) that **deletes the destination first** (so deprecated files never linger) then copies `ABDScope/WebUI` → `<synth>/WebUI/abdscope/`, excluding anything not needed by the raw-ESM runtime. The script is ESM (the repo's `package.json` sets `"type": "module"`) and relies on `fs.cpSync` with a `filter` (the delete-first behaviour is achieved with an explicit `fs.rmSync` before the copy):

```js
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const rootDir   = path.resolve(__dirname, '..');
const sourceDir = path.resolve(rootDir, '..', 'ABDScope', 'WebUI');
const destRoot  = path.join(rootDir, 'WebUI', 'abdscope');

const toPosix = (p) => p.split(path.sep).join('/');
const EXCLUDE_FILES = new Set(['vite.config.js', 'vite.config.mjs', 'vitest.config.js',
  'package.json', 'package-lock.json', 'pnpm-lock.yaml', 'index.html']);
const EXCLUDE_DIRS = new Set(['node_modules', 'tests']);

function shouldCopy(src) {
  if (EXCLUDE_FILES.has(path.basename(src))) return false;
  const rel = toPosix(src);
  for (const dir of EXCLUDE_DIRS) {
    if (path.basename(src) === dir) return false;
    if (rel.split('/').includes(dir)) return false;
  }
  return true;
}

console.log('Sincronizando ABDScope WebUI -> WebUI/abdscope/');
if (!fs.existsSync(sourceDir)) { console.error('Origen no encontrado:', sourceDir); process.exit(1); }

fs.rmSync(destRoot, { recursive: true, force: true });           // delete destination first
fs.cpSync(sourceDir, destRoot, { recursive: true, dereference: true, filter: shouldCopy });

console.log('OK - ABDScope sincronizado.');
```

Wire it into your `start.bat` (before the static server starts) and `package.json`:

```bat
node Scripts/sync_scope.js
node Scripts/sync_bankmanager.js
:: ... then sirv WebUI --port 8384 --cors --single --dev
```

```json
{ "scripts": { "sync:scope": "node Scripts/sync_scope.js" } }
```

Then reference the **copied** files from your web root:

```html
<!-- index.html — CSS via <link> (native ESM cannot import CSS) -->
<link rel="stylesheet" href="abdscope/src/scope.css">
```

```javascript
// your component — import scope.js from the copied location
import { createScope } from '../../abdscope/src/scope.js';
```

> **Rule of thumb:** never `import './x.css'` in a raw-ESM build — every stylesheet must go through a `<link>` in `index.html`. Only `scope.js` is imported as a module.

### 1.2. Vite / Bundler Adoption — **adopted: Vite is the dev server (production content stays embedded)**

**Status:** Vite is now the **development server** for `WebUI/` (it replaced `sirv-cli` in `start.bat`, same port **8384**, September 2026). Plugin production content is untouched: WebView2 still loads `juce://backend` → `pluginResourceProvider` → embedded `WebUIAssets` binaries. A `vite build` producing a served `dist/` artifact remains a future option.

**What was required to adopt Vite dev:**
- `npm i -D vite@5 @abdsynths/shared dexie file-saver jszip` in ABDMS2000 (the 3 runtime deps of the synced `abdbank` subtree so Vite can serve `abdbank/index.html` as its own SPA entry without resolution errors).
- `start.bat` runs the existing module syncs (`keyboard`, `sync_bankmanager`, `sync_scope`, `sync_assets`) then `npx -y vite --config vite.config.js`.
- `vite.config.js`: `root: 'WebUI'`, `base: './'`, `port: 8384`, `strictPort`, `fs.allow: ['..']`, plus a `transformIndexHtml` plugin that injects `<link href="/src/styles/shared-cascade.css">` **before** `themes.css` so the 3-level shared cascade renders (Shared tokens → theme → host MS2000 overrides win).
- `WebUI/src/styles/shared-cascade.css` = `@import '@abdsynths/shared/styles/tokens.css'`.

**Verified:** `GET /` (200, cascade injected first), `/abdbank/index.html` (200, 47 KB), `/abdbank/src/store/persistence.js` (200), shared-cascade (200), empty Vite error log, `npm test` **65/65**. `start.bat`/`index.html` behavior preserved after the switch; `sirv-cli` is no longer required.

> **Known risk (dev-only):** `npm audit` reports 4 vulnerabilities in the local dev toolchain (`esbuild ≤0.24.2` + `vite ≤6.4.2` → the esbuild dev-server advisory GHSA-67mh-4wv8-2f99). These affect the **localhost dev server only**; they never ship in the plugin build. The non-breaking fix is to bump vite/vitest later; documented, not forced.

### 1.3. NTFS Directory Junction (legacy alternative, not used by ABDMS2000)

Previous approach — a junction live-links the source so edits hot-reload without copying:

```cmd
cd D:\desarrollos\ABDSynths\ABDMS2000\WebUI\src
mklink /J scope D:\desarrollos\ABDSynths\ABDScope\WebUI\src
```

`import { createScope } from './scope/scope.js'` and `<link rel="stylesheet" href="./scope/scope.css">` then resolve live. (Not used because the raw-ESM web root must contain everything physically, and a junction points outside `WebUI/`.)

## 2. CMake Build Configuration (C++ Core)

In your synthesizer's `CMakeLists.txt`:

```cmake
# Add ABDScope as a subproject
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ABDScope" ABDScope_Build)

# Link ABDScopeCore to your plugin target
target_link_libraries(ABDMS2000 PRIVATE
    ABDScope::ABDScopeCore
)
```

`ABDScopeCore` exports both `Source/Core` and `Source/JUCE` as include directories, so `#include <Core/ScopeTap.h>` and `#include <JUCE/JuceWebScopeComponent.h>` resolve directly.

---

## 3. Native JUCE Integration (`JuceWebScopeComponent`)

ABDScope provides a zero-boilerplate, plug-and-play component ready for immediate integration into any JUCE plugin GUI, desktop tool, or floating modal window:

```cpp
#include <JUCE/JuceWebScopeComponent.h>

class MyPluginEditor : public juce::AudioProcessorEditor
{
public:
    MyPluginEditor(MySynthAudioProcessor& p) : AudioProcessorEditor(p), processor(p)
    {
        // One-liner instantiation: handles WebView2, embedded resources, IPC, and 30 FPS multi-tap pump
        scopeView = std::make_unique<abd::scope::JuceWebScopeComponent>(
            processor.scopeCollector,
            processor.getSampleRate(),
            30 // Refresh rate Hz
        );
        addAndMakeVisible(*scopeView);
        setSize(1000, 650);
    }

    void resized() override
    {
        scopeView->setBounds(getLocalBounds());
    }

private:
    MySynthAudioProcessor& processor;
    std::unique_ptr<abd::scope::JuceWebScopeComponent> scopeView;
};
```

### Key Capabilities of `JuceWebScopeComponent`:
1. **On-Demand Multi-Tap Activation**: The WebUI announces per-lane probe subscriptions via `{ type: 'SET_ACTIVE_TAP', tapId, laneIdx }`. Once the first subscription arrives, the component activates **only the taps referenced by at least one lane**, so unobserved probes cost ~0 CPU in the audio thread. Before the first subscription the component keeps all registered taps active (all-active fallback) so lanes render out of the box.
2. **Multi-Tap Frame Bundling**: Automatically serializes every currently active probe into `{ "taps": { [tapId]: frame } }` so independent lanes can run live simultaneously without cross-talk or stalling.
3. **Embedded Binary Resource Provider**: Zero external web server or node process required; assets are served natively from memory using JUCE binary data.

---

## 3.1 C++ Audio Engine Tap Setup (`processBlock`)

### In `PluginProcessor.h`:
```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <Core/ScopeDataCollector.h>
#include <Core/ScopeFrameSerializer.h>

class MySynthAudioProcessor : public juce::AudioProcessor,
                              private juce::Timer
{
public:
    MySynthAudioProcessor();
    ~MySynthAudioProcessor() override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // Multi-tap telemetry collector
    abd::scope::ScopeDataCollector scopeCollector;
    abd::scope::ScopeTap* tapMaster { nullptr };
    abd::scope::ScopeTap* tapVoice1 { nullptr };
    abd::scope::ScopeTap* tapFilter { nullptr };
    abd::scope::ScopeTap* tapLfo1   { nullptr };

    // IPC tap switching handler called from WebView2
    void handleWebUiMessage(const juce::var& message);

private:
    void timerCallback() override; // 30 FPS message thread serializer pump
    abd::scope::ScopeFrameSerializer frameSerializer { 1024 };
};
```

### In `PluginProcessor.cpp` Constructor & Timer:
```cpp
MySynthAudioProcessor::MySynthAudioProcessor()
{
    // Register probes (inactive by default, 0 CPU cost until subscribed by a lane).
    // The 4th argument is the stable wire id (slug) the WebUI must use in `availableTaps`.
    tapMaster = scopeCollector.registerTap("Master Output",  abd::scope::ScopeTapType::StereoAudio, 4096, "master");
    tapVoice1 = scopeCollector.registerTap("Osc 1 (DWGS)",   abd::scope::ScopeTapType::StereoAudio, 4096, "osc1");
    tapFilter = scopeCollector.registerTap("Filter Out",     abd::scope::ScopeTapType::StereoAudio, 4096, "filter");
    tapLfo1   = scopeCollector.registerTap("LFO 1 (CV)",     abd::scope::ScopeTapType::ControlSignal, 4096, "lfo1");

    // Set default active tap (used by standalone single-tap serialization)
    scopeCollector.selectTap("master");

    // Start 30 FPS telemetry decimation pump on the message thread
    startTimerHz(30);
}

MySynthAudioProcessor::~MySynthAudioProcessor()
{
    stopTimer();
}

void MySynthAudioProcessor::timerCallback()
{
    // Decimate and serialize active tap to JSON wire format
    std::string jsonPacket = frameSerializer.serializeActiveFrame(
        scopeCollector.getActiveTap(),
        static_cast<float>(getSampleRate())
    );

    if (!jsonPacket.empty())
    {
        // Emit event to WebUI frontend over WebView2 IPC
        emitWebUiEvent("scopeFrame", jsonPacket);
    }
}

void MySynthAudioProcessor::handleWebUiMessage(const juce::var& message)
{
    // Single-tap hosts: resolve any id / display name directly.
    // JuceWebScopeComponent handles multi-lane per-lane subscriptions internally.
    if (message["type"].toString() == "SET_ACTIVE_TAP")
    {
        const juce::String tapId = message["tapId"].toString();
        if (tapId.isNotEmpty())
            scopeCollector.selectTap(tapId.toStdString());
    }
}
```

### In `processBlock()` (Audio Thread â€” < 1 ns overhead when probe inactive):
```cpp
void MySynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const size_t numSamples = static_cast<size_t>(buffer.getNumSamples());

    // 1. Process Voice / Oscillator DSP...
    // tapVoice1->writeStereo(voiceL, voiceR, numSamples);

    // 2. Process Filter DSP...
    // tapFilter->writeStereo(filterL, filterR, numSamples);

    // 3. Process Master FX & Output...
    tapMaster->writeStereo(
        buffer.getReadPointer(0),
        buffer.getReadPointer(1),
        numSamples
    );
}
```

> **Zero-overhead guarantee:** `ScopeTap::writeStereo()` performs a single relaxed atomic load when the tap is inactive and returns immediately — no copy, no allocation. Only taps subscribed by a visual lane are marked active.

---

## 4. WebUI Instantiation (JavaScript)

### Pattern A: Embedded Scope with Multi-Lane Support
```javascript
import { createScope } from './scope/scope.js';

export function initPanelScope(containerId = 'chassis-scope') {
  const scope = createScope({
    containerId,
    mountMode: 'embedded',
    title: 'MS2000 TELEMETRY',
    maxLanes: 2, // Allow switching between 1 and 2 lanes in chassis
    layout: '1',
    enabledModes: ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'],
    defaultMode: 'oscilloscope',
    showFreeze: true,
    showSnapshot: true,
    showVuMeters: true,
    availableTaps: [
      { id: 'master', name: 'Master Out' },
      { id: 'osc1',   name: 'Osc 1 (DWGS)' },
      { id: 'filter', name: 'Filter Out' },
      { id: 'lfo1',   name: 'LFO 1 (CV)' }
    ],
    defaultTap: 'master',
    onTapChange: (tapId, laneIdx) => {
      // Send tap switch message to C++ audio engine
      window.chrome?.webview?.postMessage({ type: 'SET_ACTIVE_TAP', tapId, laneIdx });
    }
  });

  // Listen for frames emitted from C++ timerCallback
  window.addEventListener('message', (e) => {
    if (e.data?.event === 'scopeFrame' && e.data?.payload) {
      scope.pushFrame(e.data.payload);
    }
  });

  return scope;
}
```

### Pattern B: Floating Modal Window
```javascript
import { createScope } from './scope/scope.js';

export function createScopeModal() {
  const modalScope = createScope({
    mountMode: 'floating',
    title: 'MS2000 TELEMETRY LAB',
    maxLanes: 4, // Allow up to 4 simultaneous diagnostic lanes
    layout: '2', // Start with Dual Split
    enabledModes: ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'],
    defaultMode: 'oscilloscope',
    showFreeze: true,
    showSnapshot: true,
    showVuMeters: true,
    availableTaps: [
      { id: 'master', name: 'Master Out' },
      { id: 'osc1',   name: 'Osc 1 (DWGS)' },
      { id: 'filter', name: 'Filter Out' },
      { id: 'lfo1',   name: 'LFO 1 (CV)' }
    ],
    defaultTap: 'master',
    onTapChange: (tapId, laneIdx) => {
      window.chrome?.webview?.postMessage({ type: 'SET_ACTIVE_TAP', tapId, laneIdx });
    }
  });

  return modalScope;
}
```

> **Tip:** The `onTapChange` callback also fires once per lane right after the scope mounts (and after layout changes), so the C++ side learns the initial lane subscriptions without any extra handshake code.

### Pattern C: Browser Web Audio `AnalyserNode` (`ABDMS2000` web path, no IPC)

When the scope runs in a **real browser** (synthesizer's own Web Audio engine over WebAudio, not WebView2 IPC), feed ABDScope from a `WebAudioAnalyserNode` tapped at the engine's master bus. This is the simplest robust wiring — no per-tap C++ frame pump required; the scope's `connectAnalyser()` drives its own 60 FPS drawing loop.

1. In the audio engine module, expose the live `AudioContext` and the master gain (`masterGain`) so the UI can tap the bus.
2. In the bridge layer (`BridgeWasm`), add pass-through getters so the UI can reach them (they are often not exposed from the engine directly):
   ```javascript
   get audioContext() { return this.engine?.audioCtx ?? null; }
   get masterGain()   { return this.engine?.masterGain ?? null; }
   ```
3. Lazy-init ABDScope on first `open()` (audio may not be ready at construction — do **not** build the analyser in the constructor):
   ```javascript
   import { createScope } from '../../abdscope/src/scope.js';

   async _ensureScope() {
     const bridge = this.bridge;
     const audioCtx = bridge.audioContext;   // AudioContext
     const master   = bridge.masterGain;     // GainNode (master bus)
     if (!audioCtx || !master) return;

     this.scope = createScope({
       mountMode: 'floating',
       title: 'MS2000 TELEMETRY LAB',
       maxLanes: 2,
       layout: 'single',
       enabledModes: ['oscilloscope', 'spectrum', 'lissajous', 'phase', 'spectrogram'],
       defaultMode: 'oscilloscope',
       showFreeze: true,
       showSnapshot: true,
       availableTaps: this.SCOPE_TAPS,       // [{ id, name }] labels; single analyser source
     });

     // Tap the master bus with a real AnalyserNode
     const analyser = audioCtx.createAnalyser();
     analyser.fftSize = 2048;
     master.connect(analyser);               // analyser is read-only tap; bus passes signal through
     this.scope.connectAnalyser(analyser, { sampleRate: audioCtx.sampleRate });
   }

   open()  { if (!this.scope) this._ensureScope(); this.scope?.open();  }
   close() { this.scope?.close(); }
   toggle(){ if (!this.scope) this._ensureScope(); this.scope?.toggle(); }
   destroy(){ this.scope?.destroy(); this.scope = null; }
   ```

> **Note:** a single `AnalyserNode` tapped at the master bus feeds all lane taps (the multi-tap `availableTaps` list is purely presentational in this path). For genuine per-tap isolation in the browser, create one `AnalyserNode` per tap id and call `connectAnalyser({ analyserL, analyserR })` per lane.

---

## 5. Theme Styling

Ensure your host HTML sets its corresponding theme attribute:

```html
<body data-theme="ms2000">
  <link rel="stylesheet" href="./scope/scope.css">
  <div id="chassis-scope" style="width: 380px; height: 160px;"></div>
</body>
```

Supported themes: `ms2000`, `cz101`, `deepmind`, `audiolab`.

---

## 6. Pure C++ / Native JUCE Integration (e.g. `ABDAudioLab`)

For standalone desktop applications or laboratory test benches that render strictly in native C++ using `juce::Graphics` (without WebView2 or HTML):

```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ABDScope/Source/JUCE/JuceScopeComponent.h"

class AudioLabComponent : public juce::Component
{
public:
    AudioLabComponent(abd::scope::ScopeTap* tapToMonitor)
        : scopeComponent(tapToMonitor, 44100.0f)
    {
        // 1. Configure visual appearance
        scopeComponent.setMode(abd::scope::NativeScopeMode::Spectrum);
        scopeComponent.setTraceColour(juce::Colour(0xff00e676)); // AudioLab Emerald
        scopeComponent.setBackgroundColour(juce::Colour(0xff06120a));

        // 2. Add as child component
        addAndMakeVisible(scopeComponent);
    }

    void resized() override
    {
        scopeComponent.setBounds(getLocalBounds().reduced(8));
    }

    void switchToOscilloscope()
    {
        scopeComponent.setMode(abd::scope::NativeScopeMode::Oscilloscope);
    }

    void switchToLissajous()
    {
        scopeComponent.setMode(abd::scope::NativeScopeMode::Lissajous);
    }

private:
    abd::scope::JuceScopeComponent scopeComponent;
};
```

---

## 7. WebView2 Binary Assets & Embedded Scope Serving Guidelines

When embedding `ABDScope` inside a plugin or application using WebView2 without spinning up an HTTP server, keep these architectural rules in mind:

### 7.1. Embedded Telemetry (`WebUI/index.html`)
- **`WebUI/index.html` (Production Embedded Scope)**: Contains only the multi-lane canvas visualizer (`EmbeddedMount`), listening to C++ IPC via `window.__pushScopeFrame` and message events. It fills 100% of the viewport and does NOT include any signal generators or local Web Audio controls. The page is silent by default: to surface IPC parsing errors in the WebView host console, evaluate `window.__ABDSCOPE_DEBUG__ = true;` before loading it (see `docs/USAGE_GUIDE.md` §6).

> The interactive browser sandbox (`WebUI/demo/`) was removed on 2026-09-05; the standalone WebUI is exercised via `npm run serve` (serves `WebUI/` with the canonical dark theme) and the Vitest suites.

### 7.2. CMake Binary Assets Packaging Pattern
In `CMakeLists.txt`, always exclude `/tests/` and `/node_modules/`:

```cmake
# Embedded WebUI Binary Assets for WebView2
if(COMMAND juce_add_binary_data)
  file(GLOB_RECURSE ABDSCOPE_WEB_ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/src/*"
  )
  list(APPEND ABDSCOPE_WEB_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/WebUI/index.html")
  list(FILTER ABDSCOPE_WEB_ASSETS EXCLUDE REGEX "/tests/")
  list(FILTER ABDSCOPE_WEB_ASSETS EXCLUDE REGEX "/node_modules/")

  juce_add_binary_data(ABDScopeWebAssets
    HEADER_NAME "ABDScopeWebAssets.h"
    NAMESPACE "ABDScopeWebAssets"
    SOURCES ${ABDSCOPE_WEB_ASSETS}
  )
  target_link_libraries(ABDScopeCore INTERFACE ABDScopeWebAssets)
  add_library(ABDScope::ABDScopeWebAssets ALIAS ABDScopeWebAssets)
endif()
```

### 7.3. Basename Resolution in `ScopeResourceProvider`
Because `juce_add_binary_data` strips subdirectory paths and stores only the base filename in `originalFilenames`, ES6 module requests (e.g. `/src/mount/LaneController.js` or `../renderers/OscilloscopeRenderer.js`) must be resolved by their base filename:

```cpp
// Extract bare filename (e.g. "/src/mount/LaneController.js" -> "LaneController.js")
juce::String filename = decodedPath.fromLastOccurrenceOf("/", false, false);
if (filename.isEmpty()) filename = decodedPath;

// Match against originalFilenames table
for (int i = 0; i < ABDScopeWebAssets::namedResourceListSize; ++i) {
    if (filename.equalsIgnoreCase(ABDScopeWebAssets::originalFilenames[i])) {
        binData = ABDScopeWebAssets::getNamedResource(ABDScopeWebAssets::namedResourceList[i], binSize);
        break;
    }
}
```
Always return strict MIME types: `text/html` (`.html`), `text/css` (`.css`), `application/javascript` (`.js`/`.mjs`).

---

## 8. Remote GitHub Dependency Management via `FetchContent` (Zero-Config Monorepo & CI/CD)

To allow consumer synthesizers to automatically pull and update `ABDScope` directly from GitHub while still allowing live local development, use CMake's **Local Override + `FetchContent`** pattern:

```cmake
# ------------------------------------------------------------------------------
# ABDScope: Local Monorepo Fallback with Remote GitHub FetchContent
# ------------------------------------------------------------------------------
set(ABDSCOPE_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ABDScope")

if(EXISTS "${ABDSCOPE_LOCAL_DIR}/CMakeLists.txt")
    message(STATUS "ABDScope: Using local workspace from ${ABDSCOPE_LOCAL_DIR}")
    add_subdirectory("${ABDSCOPE_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/ABDScope_Build" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    message(STATUS "ABDScope: Fetching from GitHub repository (master branch)...")
    FetchContent_Declare(
        ABDScope
        GIT_REPOSITORY https://github.com/ajabadia/ABDScope.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ABDScope)
endif()

# Link the core library to your audio target
target_link_libraries(MySynthesizer PRIVATE ABDScope::ABDScopeCore)
```

### Benefits of this Pattern:
1. **Local Developer Speed**: When working in your local monorepo (`D:\desarrollos\ABDSynths`), CMake detects the sibling folder and uses it instantly without network requests or git pushes.
2. **Deterministic CI/CD & Team Collaboration**: When building on other machines or in GitHub Actions, CMake automatically clones the exact repository tag and compiles it cleanly.


---

## 9. Plug-and-Play C++ GUI Component (`JuceWebScopeComponent`)

Starting in v0.1.0, `ABDScope` provides a ready-to-use, zero-boilerplate JUCE C++ Component: `abd::scope::JuceWebScopeComponent`.

Instead of manually creating a `juce::WebBrowserComponent`, hooking up `withResourceProvider`, writing an IPC message listener, and managing a 30 FPS serialization timer pump, consumers can integrate the entire embedded WebUI telemetry suite in **3 lines of code**:

### 9.1. Usage in Any Plugin Editor or Window
```cpp
#include <JUCE/JuceWebScopeComponent.h>

class MyPluginAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    MyPluginAudioProcessorEditor(MyPluginAudioProcessor& p)
        : AudioProcessorEditor(&p), processor(p)
    {
        // 1. Instantiate plug-and-play web scope component
        webScope = std::make_unique<abd::scope::JuceWebScopeComponent>(
            processor.getScopeCollector(),
            processor.getSampleRate(),
            30 // Refresh rate in Hz
        );

        // 2. Add as child component
        addAndMakeVisible(*webScope);

        setSize(800, 500);
    }

    void resized() override
    {
        webScope->setBounds(getLocalBounds());
    }

private:
    MyPluginAudioProcessor& processor;
    std::unique_ptr<abd::scope::JuceWebScopeComponent> webScope;
};
```

### 9.2. CMake Requirement
The target building `JuceWebScopeComponent` (your plugin or standalone app) must have WebView2 enabled in JUCE:
```cmake
juce_add_plugin(MyPlugin
    ...
    NEEDS_WEBVIEW2 TRUE
)
target_link_libraries(MyPlugin PRIVATE ABDScope::ABDScopeCore)
```

> **⚠ Compile-time requirement verified against JUCE 8.0.12:** the WebView2 SDK headers are loaded through JUCE's own `FindWebView2.cmake`, which searches for a local `Microsoft.Web.WebView2` NuGet package (default: `%USERPROFILE%\AppData\Local\PackageManagement\NuGet\Packages`). Install it with:
> ```powershell
> Install-Package Microsoft.Web.WebView2 -Scope CurrentUser -Source nugetRepository
> ```
>
> In addition, `juce_gui_extra` defaults `JUCE_USE_WIN_WEBVIEW2` to `0`, and `WebBrowserComponent::Options::withResourceProvider` (which `JuceWebScopeComponent` relies on) is only compiled when that macro is `1`. Define it on the consuming target **before** any JUCE module header is included:
> ```cmake
> target_compile_definitions(MyPlugin PRIVATE JUCE_USE_WIN_WEBVIEW2=1)
> ```
> Without both the NuGet package and this macro the component fails to compile (`withResourceProvider is not a member of juce::WebBrowserComponent::Options`).
> 
> Verified in this repository: `JuceWebScopeComponent.h` and `ScopeResourceProvider.cpp` compile clean with MSVC + JUCE 8.0.12 + WebView2 SDK when the macro is defined. The same two files also compile clean against **JUCE 8.0.4** (the version pinned by the ABDAudioLab consumer project), so the component is compatible with both 8.0.4 and 8.0.12.

---

## 10. Tap Routing Contracts & Wire Protocol Best Practices

### 10.1. Tap ID vs. Display Name Resolution
- **WebUI Contract**: In `availableTaps`, each probe defines a machine slug `id` and a human-readable `name`. The `id` **must match** the stable wire id the C++ side registered for that tap (the 4th argument of `registerTap`); if the C++ tap registered no explicit id, the derived snake_case slug of its display name is used instead (see `makeSlug`):
  ```javascript
  availableTaps: [
    { id: 'hardware_in', name: 'Hardware In (DUT)' },
    { id: 'stimulus',    name: 'Stimulus Generator' },
    { id: 'osc1',        name: 'Oscillator 1 (DWGS)' }
  ]
  ```
  ```cpp
  // Matching C++ registration
  tapHardwareIn = scopeCollector.registerTap("Hardware In (DUT)", abd::scope::ScopeTapType::StereoAudio, 4096, "hardware_in");
  ```
  When the user changes taps, the frontend posts `{ type: 'SET_ACTIVE_TAP', tapId, laneIdx }`.
- **C++ Resolution**: `ScopeDataCollector::selectTap()` / `findTapIndex()` resolve in this order: explicit registered id equality -> case-insensitive display name / derived slug equality -> lenient substring fallback. Whether the frontend sends `'hardware_in'` or `'Hardware In (DUT)'`, `selectTap()` resolves to the correct probe.

### 10.2. JSON Wire Protocol & Typed Array Normalization
- When C++ transmits frames over WebView2 IPC (`window.__pushScopeFrame`), the numeric sample buffers `timeDataL` and `timeDataR` arrive as native JavaScript `Array` instances from `JSON.parse`.
- `createDataFrame()` in `WebUI/src/frame.js` automatically converts native JS `Array` objects into high-performance `Float32Array` instances:
  ```javascript
  const timeDataL = (raw.timeDataL instanceof Float32Array)
    ? raw.timeDataL
    : (Array.isArray(raw.timeDataL) ? new Float32Array(raw.timeDataL) : new Float32Array(0));
  ```
  *Never assume incoming bridge data is already a `Float32Array` when parsing JSON wire packets.*

### 10.3. Recommended Diagnostic Reference Tap Pattern
Every audio engine should register a dedicated diagnostic tap (e.g. `Diagnostic 1kHz`) that synthesizes a pure reference sine wave (-6 dBfs) whenever it is marked active. This allows operators and QA engineers to immediately verify visual rendering, FFT frequency spikes, Lissajous figures, and phase correlation without requiring physical audio hardware or active MIDI notes.