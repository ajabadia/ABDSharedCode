# ABDScope Data Contract & Wire Protocol

> **Version:** 1.2.0  
> **Target:** C++ DSP Engine ↔ WebUI IPC Bridge / WASM  
> **v1.2.0 changes:** deterministic `tapId` slugs (explicit id or derived snake_case) and octave-qualified `detectedNoteName` from the C++ side (parity with the JS trigger engine).

---

## 1. ScopeDataFrame Specification

The canonical data structure delivered to renderers on every visual frame:

```typescript
interface ScopeDataFrame {
  /** Signal domain type ('audio' for bipolar PCM, 'control' for unipolar/bipolar CV) */
  signalType: 'audio' | 'control';

  /** Stable wire slug of the telemetry tap emitting this frame: the explicit id passed to registerTap(..., id), or the snake_case slug derived from its display name (e.g. 'hardware_in', 'diag_tone', 'master_out'). */
  tapId?: string;

  /** Left / Mono time domain PCM samples (-1.0 to +1.0) */
  timeDataL: Float32Array;

  /** Right time domain PCM samples (-1.0 to +1.0). Null for mono signals. */
  timeDataR: Float32Array | null;

  /** Number of valid samples in time buffers (typically 512, 1024 or 2048) */
  numSamples: number;

  /** Audio sample rate in Hz (e.g. 44100, 48000, 96000) */
  sampleRate: number;

  /** Integer sample index where positive zero-crossing occurred (0 if no trigger or control signal) */
  triggerIndex: number;

  /** Sub-sample fractional interpolation offset [0.0 to 1.0] for jitter-free analog oscilloscope locking */
  triggerFraction?: number;

  /** Fundamental frequency detected in Hz (0 if undetected or control signal) */
  estimatedFrequencyHz: number;

  /** Nearest musical note name with octave, e.g. 'A4', 'C#3', 'B5'. Empty string when undetected or control signal. */
  detectedNoteName: string;

  /** Logarithmic or linear FFT magnitudes in dB (-96.0 to 0.0 dB). Null if not computed. */
  spectrumDb: Float32Array | null;

  /** Number of FFT frequency bins */
  spectrumBins: number;

  /** Root-Mean-Square (RMS) amplitude [0.0 to 1.0] */
  rmsL: number;
  rmsR: number;

  /** Absolute peak amplitude [0.0 to 1.0+] */
  peakL: number;
  peakR: number;

  /** Stereo phase correlation coefficient [-1.0 to +1.0] */
  phaseCorrelation: number;
}
```

> **Tap id resolution:** `tapId` is the explicit slug registered on the C++ tap (`ScopeDataCollector::registerTap(name, type, capacity, id)`). If the C++ tap registers no explicit id, the serializer derives a deterministic snake_case slug from the display name via `makeSlug()` (e.g. `"Master Out"` → `"master_out"`). `ScopeDataCollector::selectTap()` / `findTapIndex()` accept the id slug, the case-insensitive display name, or a lenient substring, so hosts never need exact-case lookups. WebUI `availableTaps[].id` must match the C++ id.

---

## 2. C++ JUCE IPC Wire Protocol (JSON over Bridge)

### 2.1 Single Tap Frame
When a single tap is active, the C++ message thread serializes frames at a fixed 30 Hz rate:

```json
{
  "signalType": "audio",
  "tapId": "hardware_in",
  "sampleRate": 48000,
  "triggerIndex": 48,
  "triggerFraction": 0.2415,
  "estimatedFrequencyHz": 1000.0,
  "detectedNoteName": "B5",
  "rmsL": 0.3535,
  "rmsR": 0.3535,
  "peakL": 0.5000,
  "peakR": 0.5000,
  "timeDataL": [0.0, 0.0654, 0.1305, ...],
  "timeDataR": [0.3535, 0.3980, ...],
  "numSamples": 1024
}
```

### 2.2 Multi-Tap Bundle Protocol
When multiple visualization lanes in `ABDScope` monitor distinct taps simultaneously, `JuceWebScopeComponent` sends a bundled packet containing frames for all active taps in parallel:

```json
{
  "taps": {
    "hardware_in": {
      "signalType": "audio",
      "tapId": "hardware_in",
      "sampleRate": 48000,
      "triggerIndex": 0,
      "timeDataL": [...],
      "timeDataR": [...]
    },
    "diag_tone": {
      "signalType": "audio",
      "tapId": "diag_tone",
      "sampleRate": 48000,
      "triggerIndex": 48,
      "triggerFraction": 0.182,
      "estimatedFrequencyHz": 1000.0,
      "detectedNoteName": "B5",
      "timeDataL": [...],
      "timeDataR": [...]
    }
  }
}
```

### Consumption in WebUI:
`EmbeddedMount` automatically routes each sub-frame to its respective lane according to `lane.activeTap`:
```javascript
window.__pushScopeFrame = function(frameJson) {
  const frameData = typeof frameJson === 'string' ? JSON.parse(frameJson) : frameJson;
  scope.pushFrame(frameData);
};
```

---

## 3. Web Audio API Direct Ingestion (`connectAnalyser`)

When running in browser / standalone WASM mode:
```javascript
const analyser = audioContext.createAnalyser();
analyser.fftSize = 2048;
scope.connectAnalyser(analyser);
```
`AnalyserInput` automatically samples the node on `requestAnimationFrame` at 60 FPS without garbage collector churn.
