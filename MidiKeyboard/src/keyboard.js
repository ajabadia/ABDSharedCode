/**
 * ABD Unified Keyboard Component
 * ==============================
 * Best-of-breed piano keybed assembled from ABDMS2000, ABDEep, and ABDCZ101.
 *
 * Features inherited per source:
 *   ABDEep:   velocity curves, pressure display (AT+MW+PB), pitch-bend displacement,
 *             LED color by arp/seq/chord state, per-key ivory texture
 *   ABDCZ101: vintage wear stains, QWERTY keyboard, 49-key CZ-101 parity
 *   ABDMS2000: responsive key count, wheel filmstrip, touch support
 */

// ── Imports from utils (pure functions, no DOM state) ──
import {
  QWERTY_MAP, NOTE_NAMES, OCTAVE_PATTERN, midiToName,
  applyVelocityCurve, SCALE_INTERVALS, isInScale, snapToScale,
  applyIvoryTexture, applyVintageWear,
} from './utils.js';

// Shared filmstrip wheel component (ABDSharedAssets) — single source of truth
import { createWheel } from '@abdsynths/shared/components/wheel.js';

// Re-export CZ101_PRESET so existing consumers don't break
export { CZ101_PRESET } from './utils.js';

/**
 * Create a fully functional keyboard component.
 * @param {Object} deps - Dependencies and configuration
 * @returns {Object} Keyboard instance
 */
export function createKeyboard(deps = {}) {
  const {
    containerId = 'piano-keyboard',
    wheelPitchId = 'pitch-wheel-container',
    wheelModId = 'mod-wheel-container',
    octUpId = 'oct-up',
    octDownId = 'oct-down',
    ledUpId = 'led-up',
    ledDownId = 'led-down',
    panicBtnId = null,
    onNoteOn = () => {},
    onNoteOff = () => {},
    onPitchBend = () => {},
    onModWheel = () => {},
    onPanic = null,
    onOctaveChange = null,
    onSustainChange = null,
    onSostenutoChange = null,
    onSoftPedalChange = null,
    onAftertouch = null,
    onVelocityChange = null,
    onCollapseChange = null,
    sustainBtnId = null,
    sostenutoBtnId = null,
    softPedalBtnId = null,
    config = {}
  } = deps;

  const cfg = {
    numOctaves: config.numOctaves ?? 4,
    startNote: config.startNote ?? 36,
    maxOctaveShift: config.maxOctaveShift ?? 3,
    velocitySource: config.velocitySource ?? 'fixed',
    velocityCurve: config.velocityCurve ?? 'normal',
    fixedVelocity: config.fixedVelocity ?? 0.85,
    ledColor: config.ledColor ?? null,
    enablePressureDisplay: config.enablePressureDisplay ?? false,
    enablePitchBendDisplace: config.enablePitchBendDisplace ?? false,
    enableIvoryTexture: config.enableIvoryTexture ?? false,
    enableVintageWear: config.enableVintageWear ?? false,
    enableQwerty: config.enableQwerty ?? true,
    enableTouch: config.enableTouch ?? true,
    enableAftertouch: config.enableAftertouch ?? false,
    aftertouchMode: config.aftertouchMode ?? 'channel',
    aftertouchSensitivity: config.aftertouchSensitivity ?? 0.5,
    enableAccessibility: config.enableAccessibility ?? false,
    enableCollapse: config.enableCollapse ?? false,
    enableResizeObserver: config.enableResizeObserver ?? true,
    enableSostenuto: config.enableSostenuto ?? false,
    enableSoftPedal: config.enableSoftPedal ?? false,
    softPedalFactor: config.softPedalFactor ?? 0.65,
    enableScaleFilter: config.enableScaleFilter ?? false,
    scaleType: config.scaleType ?? 'major',
    scaleRoot: config.scaleRoot ?? 60,
    scaleSnapMode: config.scaleSnapMode ?? 'block',
    enableChordMemory: config.enableChordMemory ?? false,
    maxChordSlots: config.maxChordSlots ?? 12,
    getLedColor: config.getLedColor ?? null,
    getPressureState: config.getPressureState ?? null,
  };

  // ── State ──
  let octaveShift = 0;
  let sustainOn = false;
  let _channelAftertouch = 0;
  const activeKeys = new Map();
  const qwertyActive = new Set();
  let destroyed = false;
  let _pressureRafId = null;
  let _focusedKeyIndex = -1;
  let _liveRegion = null;
  let _collapsed = false;
  let _resizeObserver = null;
  let sostenutoOn = false;
  const _sostenutoCaptured = new Set();
  let softPedalOn = false;
  const _chords = new Array(cfg.maxChordSlots).fill(null);

  const container = document.getElementById(containerId);
  if (!container) {
    console.warn(`[Keyboard] Container #${containerId} not found`);
    return { destroy() {}, panic() {}, sweep() {}, getOctave: () => 0 };
  }

  let _keysWrapper = null;

  // ══════════════════════════════════════════════════════════════
  //  KEYBED RENDERING
  // ══════════════════════════════════════════════════════════════

  function renderKeybed() {
    container.classList.add('kbd-piano-keys');
    if (!_keysWrapper) {
      _keysWrapper = container.querySelector('.kbd-keys-wrapper');
      if (!_keysWrapper) {
        _keysWrapper = document.createElement('div');
        _keysWrapper.className = 'kbd-keys-wrapper';
        container.prepend(_keysWrapper);
      }
    }
    _keysWrapper.innerHTML = '';
    for (let oct = 0; oct < cfg.numOctaves; oct++) {
      const base = cfg.startNote + oct * 12;
      for (const p of OCTAVE_PATTERN) {
        const whiteNote = base + p.offset;
        const whiteKey = createKeyElement(whiteNote, false);
        if (p.sharp !== null) {
          const blackKey = createKeyElement(base + p.sharp, true);
          whiteKey.appendChild(blackKey);
        }
        _keysWrapper.appendChild(whiteKey);
      }
    }
    _keysWrapper.appendChild(createKeyElement(cfg.startNote + cfg.numOctaves * 12, false));
  }

  function createKeyElement(midiNote, isBlack) {
    const key = document.createElement('div');
    key.className = isBlack ? 'kbd-black-key' : 'kbd-white-key';
    key.dataset.note = midiNote;
    if (cfg.enableIvoryTexture && !isBlack) applyIvoryTexture(key, midiNote);
    if (cfg.enableVintageWear) applyVintageWear(key, midiNote, isBlack);
    if (cfg.enableAccessibility) applyKeyAria(key, midiNote);

    const playNote = (e) => {
      if (e) { e.preventDefault(); e.stopPropagation(); }
      const actualNote = Math.max(0, Math.min(127, midiNote + octaveShift));
      if (activeKeys.has(midiNote)) {
        // Multi-touch: different pointer on same key — transfer ownership
        const existing = activeKeys.get(midiNote);
        const newPid = e && e.pointerId != null ? e.pointerId : null;
        if (newPid != null && existing.pointerId !== newPid) {
          existing.pointerId = newPid;
        }
        return;
      }

      // Scale filter: check if note is in scale
      let noteToPlay = actualNote;
      if (_scaleEnabled) {
        const intervals = SCALE_INTERVALS[_scaleType] || SCALE_INTERVALS.major;
        if (!isInScale(actualNote, _scaleRoot, intervals)) {
          if (_scaleSnapMode === 'block') return;
          if (_scaleSnapMode === 'snap') noteToPlay = snapToScale(actualNote, _scaleRoot, intervals);
        }
      }

      let velocity = cfg.fixedVelocity;
      if (cfg.velocitySource === 'yPosition' && e && e.clientY != null) {
        const rect = key.getBoundingClientRect();
        if (rect.height > 0) {
          const relY = (e.clientY - rect.top) / rect.height;
          const raw = Math.max(0.15, Math.min(1.0, 0.15 + relY * 0.85));
          velocity = Math.max(0.01, Math.min(1.0, applyVelocityCurve(raw, cfg.velocityCurve)));
        }
      }

      // Soft pedal attenuates velocity
      if (softPedalOn) velocity *= cfg.softPedalFactor;

      activeKeys.set(midiNote, { actualNote: noteToPlay, element: key, velocity, startPointerY: e.clientY ?? null, pointerId: e && e.pointerId != null ? e.pointerId : null });
      key.classList.add('active');
      key.style.setProperty('--kbd-velocity', velocity.toFixed(3));

      if (cfg.enableAccessibility) {
        updateKeyAriaPressed(midiNote, true);
        announce(`${midiToName(noteToPlay)} on, velocity ${Math.round(velocity * 100)}%`);
      }

      const color = (cfg.getLedColor) ? cfg.getLedColor() : (cfg.ledColor || 'var(--color-accent)');
      key.style.setProperty('--kbd-led-color', color);

      onNoteOn(noteToPlay, velocity);
      if (onVelocityChange) onVelocityChange(noteToPlay, velocity);
    };

    const stopNote = (e) => {
      if (e) { e.preventDefault(); e.stopPropagation(); }
      if (!activeKeys.has(midiNote)) return;
      const { actualNote, element, pointerId: heldPointerId } = activeKeys.get(midiNote);

      // Multi-touch: only the pointer that started the note can stop it
      if (e && e.pointerId != null && e.pointerId !== heldPointerId) return;

      // Sostenuto: if this note was captured, don't release until sostenuto is off
      if (sostenutoOn && _sostenutoCaptured.has(midiNote)) return;

      // Sustain: hold notes in activeKeys until sustain is turned off
      if (sustainOn && e && (e.type === 'pointerup' || e.type === 'pointerleave')) {
        // Mark as sustained so panic can release it, but don't fire noteOff
        element.classList.remove('active');
        return;
      }

      if (cfg.enableAftertouch && onAftertouch) {
        if (cfg.aftertouchMode === 'polyphonic') {
          onAftertouch(actualNote, 0);
        } else if (_channelAftertouch > 0) {
          _channelAftertouch = 0;
          onAftertouch(-1, 0);
        }
      }

      activeKeys.delete(midiNote);
      if (element) {
        element.classList.remove('active');
        element.style.removeProperty('--kbd-velocity');
      }
      if (cfg.enableAccessibility) updateKeyAriaPressed(midiNote, false);
      onNoteOff(actualNote);

      if (cfg.enablePressureDisplay && element &&
          (element.classList.contains('kbd-pressured') || element.classList.contains('kbd-pitch-bent'))) {
        element.style.setProperty('--kbd-pressure', '0');
        element.style.setProperty('--kbd-mw-pressure', '0');
        element.classList.add('kbd-pressure-release');
        const onEnd = () => {
          element.classList.remove('kbd-pressure-release', 'kbd-pressured', 'kbd-pitch-bent');
          element.style.removeProperty('--kbd-pressure');
          element.style.removeProperty('--kbd-mw-pressure');
          element.style.removeProperty('--kbd-pb-offset');
        };
        element.addEventListener('transitionend', onEnd, { once: true });
        setTimeout(() => { if (element.classList.contains('kbd-pressure-release')) onEnd(); }, 500);
      }
    };

    key.addEventListener('pointerdown', playNote);
    key.addEventListener('pointerup', stopNote);
    // Note: pointerleave NOT used as stop trigger — it fires during multi-touch
    // (jsdom synthetic events don't carry pointerId) and causes false noteOff.
    // pointerup is reliable for all input types (touch, mouse, pen).

    if (cfg.enableAftertouch) {
      key.addEventListener('pointermove', (e) => {
        if (!activeKeys.has(midiNote)) return;
        const state = activeKeys.get(midiNote);
        if (state.startPointerY == null) return;
        const deltaY = state.startPointerY - (e.clientY ?? state.startPointerY);
        const maxDelta = key.getBoundingClientRect().height * 0.8;
        const rawPressure = Math.max(0, Math.min(1, (deltaY / maxDelta) * cfg.aftertouchSensitivity * 2));
        const pressure = Math.round(rawPressure * 127) / 127;
        if (cfg.aftertouchMode === 'polyphonic') {
          if (onAftertouch) onAftertouch(state.actualNote, pressure);
        } else if (pressure !== _channelAftertouch) {
          _channelAftertouch = pressure;
          if (onAftertouch) onAftertouch(-1, pressure);
        }
      });
    }

    if (cfg.enableTouch) {
      key.addEventListener('touchstart', playNote, { passive: false });
      key.addEventListener('touchend', stopNote, { passive: false });
      key.addEventListener('touchcancel', stopNote, { passive: false });
    }
    return key;
  }

  // ══════════════════════════════════════════════════════════════
  //  ACCESSIBILITY
  // ══════════════════════════════════════════════════════════════

  function announce(message) {
    if (!cfg.enableAccessibility || !_liveRegion) return;
    _liveRegion.textContent = message;
  }

  function getAllKeyElements() {
    return Array.from((_keysWrapper || container).querySelectorAll('.kbd-white-key, .kbd-black-key'));
  }

  function applyKeyAria(keyEl, midiNote) {
    keyEl.setAttribute('role', 'button');
    keyEl.setAttribute('tabindex', '-1');
    keyEl.setAttribute('aria-label', `Note ${midiToName(midiNote)} (MIDI ${midiNote})`);
    keyEl.setAttribute('aria-pressed', 'false');
  }

  function updateKeyAriaPressed(midiNote, pressed) {
    const keyEl = (_keysWrapper || container).querySelector(`[data-note="${midiNote}"]`);
    if (keyEl) keyEl.setAttribute('aria-pressed', pressed ? 'true' : 'false');
  }

  function focusKey(index) {
    const keys = getAllKeyElements();
    if (keys.length === 0) return;
    if (_focusedKeyIndex >= 0 && _focusedKeyIndex < keys.length) {
      keys[_focusedKeyIndex].setAttribute('tabindex', '-1');
    }
    _focusedKeyIndex = Math.max(0, Math.min(keys.length - 1, index));
    keys[_focusedKeyIndex].setAttribute('tabindex', '0');
    keys[_focusedKeyIndex].focus();
  }

  function setupAccessibility() {
    if (!cfg.enableAccessibility) return;
    _liveRegion = document.createElement('div');
    _liveRegion.setAttribute('role', 'status');
    _liveRegion.setAttribute('aria-live', 'polite');
    _liveRegion.setAttribute('aria-atomic', 'true');
    _liveRegion.className = 'kbd-sr-only';
    container.appendChild(_liveRegion);
    container.setAttribute('role', 'group');
    container.setAttribute('aria-label', 'Virtual piano keyboard');
    getAllKeyElements().forEach((keyEl) => {
      applyKeyAria(keyEl, parseInt(keyEl.dataset.note, 10));
    });
    const keys = getAllKeyElements();
    if (keys.length > 0) { keys[0].setAttribute('tabindex', '0'); _focusedKeyIndex = 0; }
    container.addEventListener('keydown', handleA11yKeyNav);
  }

  function handleA11yKeyNav(e) {
    if (!cfg.enableAccessibility) return;
    const keys = getAllKeyElements();
    if (keys.length === 0) return;
    switch (e.key) {
      case 'ArrowRight': e.preventDefault(); focusKey(_focusedKeyIndex + 1); announceNote(keys[_focusedKeyIndex]); break;
      case 'ArrowLeft': e.preventDefault(); focusKey(_focusedKeyIndex - 1); announceNote(keys[_focusedKeyIndex]); break;
      case 'ArrowUp': e.preventDefault(); focusKey(Math.max(0, _focusedKeyIndex - 7)); announceNote(keys[_focusedKeyIndex]); break;
      case 'ArrowDown': e.preventDefault(); focusKey(Math.min(keys.length - 1, _focusedKeyIndex + 7)); announceNote(keys[_focusedKeyIndex]); break;
      case 'Home': e.preventDefault(); focusKey(0); announceNote(keys[0]); break;
      case 'End': e.preventDefault(); focusKey(keys.length - 1); announceNote(keys[keys.length - 1]); break;
    }
  }

  function announceNote(keyEl) {
    if (!keyEl) return;
    const midiNote = parseInt(keyEl.dataset.note, 10);
    announce(`${midiToName(midiNote)}, MIDI ${midiNote}${activeKeys.has(midiNote) ? ', playing' : ''}`);
  }

  // ══════════════════════════════════════════════════════════════
  //  OCTAVE SHIFT + LEDS
  // ══════════════════════════════════════════════════════════════

  function updateOctaveLEDs() {
    const ledUp = document.getElementById(ledUpId);
    const ledDown = document.getElementById(ledDownId);
    if (!ledUp || !ledDown) return;
    const octVal = octaveShift / 12;
    ledUp.className = 'kbd-octave-led';
    ledDown.className = 'kbd-octave-led';
    if (octVal === 1) ledUp.classList.add('blink');
    else if (octVal >= 2) ledUp.classList.add('on');
    else if (octVal === -1) ledDown.classList.add('blink');
    else if (octVal <= -2) ledDown.classList.add('on');
  }

  function shiftOctave(delta) {
    const maxSemitones = cfg.maxOctaveShift * 12;
    octaveShift = Math.max(-maxSemitones, Math.min(maxSemitones, octaveShift + delta * 12));
    updateOctaveLEDs();
    const oct = octaveShift / 12;
    if (cfg.enableAccessibility) announce(`Octave shift ${oct > 0 ? '+' : ''}${oct}`);
    if (onOctaveChange) onOctaveChange(oct);
  }

  // ══════════════════════════════════════════════════════════════
  //  QWERTY KEYBOARD
  // ══════════════════════════════════════════════════════════════

  function handleKeydown(e) {
    if (destroyed) return;
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA' || e.target.tagName === 'SELECT') return;

    // Ctrl+Q → Panic
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'q') {
      e.preventDefault(); panic(); return;
    }
    // Ctrl+Space → Toggle Sustain
    if ((e.ctrlKey || e.metaKey) && e.key === ' ') {
      e.preventDefault(); toggleSustain(); return;
    }
    // Ctrl+Shift+Space → Toggle Sostenuto
    if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key === ' ') {
      e.preventDefault(); toggleSostenuto(); return;
    }
    // Ctrl+Alt+Space → Toggle Soft Pedal
    if ((e.ctrlKey || e.metaKey) && e.altKey && e.key === ' ') {
      e.preventDefault(); toggleSoftPedal(); return;
    }

    if (e.ctrlKey || e.metaKey || e.altKey || e.repeat) return;
    if (e.key === 'ArrowUp') { e.preventDefault(); shiftOctave(1); return; }
    if (e.key === 'ArrowDown') { e.preventDefault(); shiftOctave(-1); return; }
    if (!cfg.enableQwerty) return;

    const key = e.key.toLowerCase();
    if (QWERTY_MAP[key] !== undefined && !qwertyActive.has(key)) {
      qwertyActive.add(key);
      const midiNote = cfg.startNote + QWERTY_MAP[key];

      // Scale filter: check if note is in scale
      let noteToPlay = Math.max(0, Math.min(127, midiNote + octaveShift));
      if (_scaleEnabled) {
        const intervals = SCALE_INTERVALS[_scaleType] || SCALE_INTERVALS.major;
        if (!isInScale(noteToPlay, _scaleRoot, intervals)) {
          if (_scaleSnapMode === 'block') return;
          if (_scaleSnapMode === 'snap') noteToPlay = snapToScale(noteToPlay, _scaleRoot, intervals);
        }
      }

      if (!activeKeys.has(midiNote)) {
        let velocity = cfg.fixedVelocity;
        if (softPedalOn) velocity *= cfg.softPedalFactor;

        const keyEl = (_keysWrapper || container).querySelector(`[data-note="${midiNote}"]`);
        if (keyEl) {
          keyEl.classList.add('active');
          keyEl.style.setProperty('--kbd-velocity', velocity.toFixed(3));
          const color = (cfg.getLedColor) ? cfg.getLedColor() : (cfg.ledColor || 'var(--color-accent)');
          keyEl.style.setProperty('--kbd-led-color', color);
        }
        activeKeys.set(midiNote, { actualNote: noteToPlay, element: keyEl || null, velocity });
        onNoteOn(noteToPlay, velocity);
        if (onVelocityChange) onVelocityChange(noteToPlay, velocity);
        if (cfg.enableAccessibility) {
          updateKeyAriaPressed(midiNote, true);
          announce(`${midiToName(noteToPlay)} on, velocity ${Math.round(velocity * 100)}%`);
        }
      }
    }
  }

  function handleKeyup(e) {
    if (destroyed) return;
    if (!cfg.enableQwerty) return;
    const key = e.key.toLowerCase();
    if (QWERTY_MAP[key] !== undefined && qwertyActive.has(key)) {
      qwertyActive.delete(key);
      const midiNote = cfg.startNote + QWERTY_MAP[key];
      if (activeKeys.has(midiNote)) {
        const { actualNote, element } = activeKeys.get(midiNote);
        if (sostenutoOn && _sostenutoCaptured.has(midiNote)) return;
        if (element) { element.classList.remove('active'); element.style.removeProperty('--kbd-velocity'); }
        activeKeys.delete(midiNote);
        if (cfg.enableAccessibility) updateKeyAriaPressed(midiNote, false);
        onNoteOff(actualNote);
      }
    }
  }

  // ══════════════════════════════════════════════════════════════
  //  PITCH / MOD WHEELS
  //  Delegates markup, visuals, spring-reset and value readout to the
  //  shared filmstrip Wheel from @abdsynths/shared (single source).
  // ══════════════════════════════════════════════════════════════

  const wheels = [];

  function setupWheel(elementId, isPitch) {
    const wheel = createWheel(elementId, {
      type: isPitch ? 'pitch' : 'mod',
      onChange: (val) => {
        if (isPitch) onPitchBend(val / 8192.0);
        else onModWheel(val / 127.0);
      },
    });
    if (wheel) wheels.push(wheel);
    return wheel;
  }

  // ══════════════════════════════════════════════════════════════
  //  PRESSURE DISPLAY (from ABDEep)
  // ══════════════════════════════════════════════════════════════

  let _prevAT = 0, _prevMW = 0, _prevPB = 0;

  function updatePressureDisplay() {
    _pressureRafId = null;
    let at = 0, mw = 0, pb = 0;
    if (cfg.getPressureState) {
      const state = cfg.getPressureState();
      at = Math.max(0, Math.min(1, state.aftertouch || 0));
      mw = Math.max(0, Math.min(1, state.modWheel || 0));
      pb = Math.max(-1, Math.min(1, state.pitchBend || 0));
    } else if (cfg.enableAftertouch) {
      at = _channelAftertouch;
    }
    if (Math.abs(at - _prevAT) < 0.01 && Math.abs(mw - _prevMW) < 0.01 && Math.abs(pb - _prevPB) < 0.01) {
      _pressureRafId = requestAnimationFrame(updatePressureDisplay);
      return;
    }
    _prevAT = at; _prevMW = mw; _prevPB = pb;
    const combined = Math.max(at, mw);
    const pbPx = Math.round(pb * 6);
    for (const [, { element }] of activeKeys) {
      if (!element) continue;
      if (combined > 0.01) {
        element.style.setProperty('--kbd-pressure', combined.toFixed(3));
        element.style.setProperty('--kbd-mw-pressure', mw > 0.01 ? mw.toFixed(3) : '0');
        element.classList.add('kbd-pressured');
      } else {
        element.style.removeProperty('--kbd-pressure');
        element.style.removeProperty('--kbd-mw-pressure');
        element.classList.remove('kbd-pressured');
      }
      if (cfg.enablePitchBendDisplace && Math.abs(pb) > 0.01) {
        element.style.setProperty('--kbd-pb-offset', pbPx + 'px');
        element.classList.add('kbd-pitch-bent');
      } else {
        element.style.removeProperty('--kbd-pb-offset');
        element.classList.remove('kbd-pitch-bent');
      }
    }
    _pressureRafId = requestAnimationFrame(updatePressureDisplay);
  }

  // ══════════════════════════════════════════════════════════════
  //  LED ANIMATIONS
  // ══════════════════════════════════════════════════════════════

  function sweep(direction = 'right', speedMs = 12) {
    const keys = Array.from((_keysWrapper || container).querySelectorAll('.kbd-white-key, .kbd-black-key'));
    if (!keys.length) return;
    const ordered = direction === 'left' ? [...keys].reverse() : keys;
    ordered.forEach((key, i) => {
      setTimeout(() => {
        if (destroyed) return;
        key.classList.add('kbd-led-sweep');
        setTimeout(() => key.classList.remove('kbd-led-sweep'), 180);
      }, i * speedMs);
    });
  }

  function panicFlash() {
    const keys = Array.from((_keysWrapper || container).querySelectorAll('.kbd-white-key, .kbd-black-key'));
    const panicLed = container.querySelector('.kbd-panic-led');
    if (!keys.length && !panicLed) return;
    let flashes = 0;
    const interval = setInterval(() => {
      keys.forEach(k => k.classList.toggle('kbd-led-panic'));
      if (panicLed) {
        panicLed.style.background = flashes % 2 === 0 ? '#ff2233' : '#3a1010';
        panicLed.style.boxShadow = flashes % 2 === 0 ? '0 0 8px #ff2233, 0 0 16px rgba(255,34,51,0.5)' : 'inset 0 1px 2px rgba(0,0,0,0.6)';
      }
      flashes++;
      if (flashes >= 6) {
        clearInterval(interval);
        keys.forEach(k => k.classList.remove('kbd-led-panic'));
        if (panicLed) { panicLed.style.background = ''; panicLed.style.boxShadow = ''; }
      }
    }, 90);
  }

  // ══════════════════════════════════════════════════════════════
  //  SUSTAIN PEDAL (CC#64)
  // ══════════════════════════════════════════════════════════════

  function updateSustainVisuals() {
    const led = container.querySelector('.kbd-sustain-led');
    if (!led) return;
    sustainOn ? led.classList.add('on') : led.classList.remove('on');
  }

  function setSustain(on) {
    const val = !!on;
    if (val === sustainOn) return;
    sustainOn = val;
    updateSustainVisuals();
    if (cfg.enableAccessibility) announce(`Sustain ${sustainOn ? 'on' : 'off'}`);
    if (onSustainChange) onSustainChange(sustainOn);
  }

  function toggleSustain() { setSustain(!sustainOn); }

  // ══════════════════════════════════════════════════════════════
  //  SOSTENUTO PEDAL (CC#66)
  //  Captures all currently active notes when engaged.
  // ══════════════════════════════════════════════════════════════

  function updateSostenutoVisuals() {
    const led = container.querySelector('.kbd-sostenuto-led');
    if (!led) return;
    sostenutoOn ? led.classList.add('on') : led.classList.remove('on');
  }

  function setSostenuto(on) {
    const val = !!on;
    if (val === sostenutoOn) return;
    sostenutoOn = val;
    if (sostenutoOn) {
      _sostenutoCaptured.clear();
      for (const [baseNote] of activeKeys) _sostenutoCaptured.add(baseNote);
    } else {
      _sostenutoCaptured.clear();
    }
    updateSostenutoVisuals();
    if (cfg.enableAccessibility) announce(`Sostenuto ${sostenutoOn ? 'on' : 'off'}`);
    if (onSostenutoChange) onSostenutoChange(sostenutoOn);
  }

  function toggleSostenuto() { setSostenuto(!sostenutoOn); }

  // ══════════════════════════════════════════════════════════════
  //  SOFT PEDAL (CC#67)
  //  Attenuates velocity values when active.
  // ══════════════════════════════════════════════════════════════

  function updateSoftPedalVisuals() {
    const led = container.querySelector('.kbd-soft-led');
    if (!led) return;
    softPedalOn ? led.classList.add('on') : led.classList.remove('on');
  }

  function setSoftPedal(on) {
    const val = !!on;
    if (val === softPedalOn) return;
    softPedalOn = val;
    updateSoftPedalVisuals();
    if (cfg.enableAccessibility) announce(`Soft pedal ${softPedalOn ? 'on' : 'off'}`);
    if (onSoftPedalChange) onSoftPedalChange(softPedalOn);
  }

  function toggleSoftPedal() { setSoftPedal(!softPedalOn); }

  // ══════════════════════════════════════════════════════════════
  //  SCALE FILTER — lock keys to a musical scale
  // ══════════════════════════════════════════════════════════════

  let _scaleType = cfg.scaleType;
  let _scaleRoot = cfg.scaleRoot;
  let _scaleSnapMode = cfg.scaleSnapMode;
  let _scaleEnabled = cfg.enableScaleFilter;

  function getScaleIntervals() {
    return SCALE_INTERVALS[_scaleType] || SCALE_INTERVALS.major;
  }

  function applyScaleVisuals() {
    if (!_scaleEnabled) {
      getAllKeyElements().forEach((keyEl) => {
        keyEl.classList.remove('kbd-scale-in', 'kbd-scale-out');
      });
      return;
    }
    const intervals = getScaleIntervals();
    getAllKeyElements().forEach((keyEl) => {
      const midi = parseInt(keyEl.dataset.note, 10);
      const actualNote = Math.max(0, Math.min(127, midi + octaveShift));
      if (isInScale(actualNote, _scaleRoot, intervals)) {
        keyEl.classList.remove('kbd-scale-out');
        keyEl.classList.add('kbd-scale-in');
      } else {
        keyEl.classList.remove('kbd-scale-in');
        keyEl.classList.add('kbd-scale-out');
      }
    });
  }

  function setScaleFilter(type, root) {
    if (type === null || type === undefined) {
      _scaleEnabled = false;
      applyScaleVisuals();
      return;
    }
    _scaleEnabled = true;
    _scaleType = type;
    if (root !== undefined) _scaleRoot = root;
    applyScaleVisuals();
  }

  // ══════════════════════════════════════════════════════════════
  //  COLLAPSE / EXPAND
  // ══════════════════════════════════════════════════════════════

  function setCollapsed(collapsed) {
    if (!cfg.enableCollapse) return;
    const val = !!collapsed;
    if (val === _collapsed) return;
    _collapsed = val;
    const chevron = container.querySelector('.kbd-collapse-btn');
    if (chevron) {
      const label = chevron.querySelector('.kbd-collapse-chevron');
      if (label) label.textContent = _collapsed ? '▴' : '▾';
      chevron.setAttribute('aria-label', _collapsed ? 'Expand keyboard' : 'Collapse keyboard');
    }
    if (_collapsed) {
      container.classList.add('kbd-collapsed');
      if (_keysWrapper) _keysWrapper.classList.add('kbd-collapsed');
    } else {
      container.classList.remove('kbd-collapsed');
      if (_keysWrapper) _keysWrapper.classList.remove('kbd-collapsed');
    }
    if (cfg.enableAccessibility) announce(_collapsed ? 'Keyboard collapsed' : 'Keyboard expanded');
    if (onCollapseChange) onCollapseChange(_collapsed);
  }

  function toggleCollapse() { setCollapsed(!_collapsed); }

  // ══════════════════════════════════════════════════════════════
  //  CHORD MEMORY — save and replay note groups
  // ══════════════════════════════════════════════════════════════

  function _highlightKey(midiNote, velocity) {
    const keyEl = (_keysWrapper || container).querySelector(`[data-note="${midiNote}"]`);
    if (keyEl) {
      keyEl.classList.add('active');
      keyEl.style.setProperty('--kbd-led-color', cfg.ledColor || 'var(--color-accent)');
      if (velocity != null) keyEl.style.setProperty('--kbd-velocity', velocity.toFixed(3));
    }
  }

  function _releaseKey(midiNote) {
    for (const [baseNote, { actualNote, element }] of activeKeys) {
      if (actualNote === midiNote) {
        if (element) element.classList.remove('active');
        activeKeys.delete(baseNote);
        return;
      }
    }
    const keyEl = (_keysWrapper || container).querySelector(`[data-note="${midiNote}"]`);
    if (keyEl) keyEl.classList.remove('active');
  }

  function saveChord(slot, notes) {
    if (!cfg.enableChordMemory) return;
    const s = Math.max(0, Math.min(_chords.length - 1, slot));
    if (notes && Array.isArray(notes)) {
      _chords[s] = notes.slice().sort((a, b) => a - b);
    } else {
      _chords[s] = Array.from(activeKeys.values()).map(n => n.actualNote).sort((a, b) => a - b);
    }
    if (cfg.enableAccessibility) announce(`Chord ${s + 1} saved: ${_chords[s].length} notes`);
  }

  function playChord(slot) {
    if (!cfg.enableChordMemory) return;
    const s = Math.max(0, Math.min(_chords.length - 1, slot));
    const notes = _chords[s];
    if (!notes || notes.length === 0) return;
    for (const note of notes) {
      const vel = cfg.fixedVelocity;
      _highlightKey(note, vel);
      onNoteOn(note, vel);
      if (onVelocityChange) onVelocityChange(note, vel);
    }
    if (cfg.enableAccessibility) announce(`Chord ${s + 1} played: ${notes.length} notes`);
  }

  function releaseChord(slot) {
    if (!cfg.enableChordMemory) return;
    const s = Math.max(0, Math.min(_chords.length - 1, slot));
    const notes = _chords[s];
    if (!notes) return;
    for (const note of notes) { _releaseKey(note); onNoteOff(note); }
    if (cfg.enableAccessibility) announce(`Chord ${s + 1} released`);
  }

  function getChords() { return _chords.map(chord => chord ? [...chord] : null); }

  function clearChord(slot) {
    if (!cfg.enableChordMemory) return;
    const s = Math.max(0, Math.min(_chords.length - 1, slot));
    _chords[s] = null;
  }

  function clearAllChords() {
    if (!cfg.enableChordMemory) return;
    for (let i = 0; i < _chords.length; i++) _chords[i] = null;
  }

  // ══════════════════════════════════════════════════════════════
  //  PANIC
  // ══════════════════════════════════════════════════════════════

  function panic() {
    if (sustainOn) setSustain(false);
    if (sostenutoOn) setSostenuto(false);
    if (softPedalOn) setSoftPedal(false);
    if (_channelAftertouch > 0) { _channelAftertouch = 0; if (onAftertouch) onAftertouch(-1, 0); }
    for (const [baseNote, { actualNote, element }] of activeKeys) {
      if (element) {
        element.classList.remove('active', 'kbd-pressured', 'kbd-pitch-bent');
        element.style.removeProperty('--kbd-velocity');
        element.style.removeProperty('--kbd-pressure');
        element.style.removeProperty('--kbd-mw-pressure');
        element.style.removeProperty('--kbd-pb-offset');
      }
      onNoteOff(actualNote);
    }
    activeKeys.clear();
    qwertyActive.clear();
    if (cfg.enableAccessibility) announce('Panic: All notes off');
    panicFlash();
    if (onPanic) onPanic();
  }

  // ══════════════════════════════════════════════════════════════
  //  INIT
  // ══════════════════════════════════════════════════════════════

  function init() {
    renderKeybed();
    setupWheel(wheelPitchId, true);
    setupWheel(wheelModId, false);

    const octUp = document.getElementById(octUpId);
    const octDown = document.getElementById(octDownId);
    if (octUp) octUp.addEventListener('click', () => shiftOctave(1));
    if (octDown) octDown.addEventListener('click', () => shiftOctave(-1));

    // Panic button
    if (panicBtnId) {
      const panicBtn = document.getElementById(panicBtnId);
      if (panicBtn) panicBtn.addEventListener('click', panic);
    } else {
      const autoPanic = document.createElement('div');
      autoPanic.className = 'kbd-panic-btn';
      autoPanic.setAttribute('role', 'button');
      autoPanic.setAttribute('tabindex', '0');
      autoPanic.setAttribute('aria-label', 'All Notes Off — Panic (Ctrl+Q)');
      autoPanic.setAttribute('title', 'Panic: All Notes Off (Ctrl+Q)');
      autoPanic.innerHTML = `<span class="kbd-panic-led"></span><span class="kbd-panic-label">ALL<br>OFF</span>`;
      autoPanic.addEventListener('click', panic);
      autoPanic.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); panic(); } });
      container.appendChild(autoPanic);
    }

    // Sustain button
    if (sustainBtnId) {
      const sustainBtn = document.getElementById(sustainBtnId);
      if (sustainBtn) sustainBtn.addEventListener('click', toggleSustain);
    } else {
      const autoSustain = document.createElement('div');
      autoSustain.className = 'kbd-sustain-btn';
      autoSustain.setAttribute('role', 'button');
      autoSustain.setAttribute('tabindex', '0');
      autoSustain.setAttribute('aria-label', 'Sustain Pedal On/Off (Ctrl+Space)');
      autoSustain.setAttribute('title', 'Sustain Pedal: Toggle Hold (Ctrl+Space)');
      autoSustain.innerHTML = `<span class="kbd-sustain-led"></span><span class="kbd-sustain-label">SUST</span>`;
      autoSustain.addEventListener('click', toggleSustain);
      autoSustain.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleSustain(); } });
      container.appendChild(autoSustain);
    }

    // Sostenuto button (CC#66)
    if (sostenutoBtnId) {
      const sostBtn = document.getElementById(sostenutoBtnId);
      if (sostBtn) sostBtn.addEventListener('click', toggleSostenuto);
    } else if (cfg.enableSostenuto) {
      const autoSost = document.createElement('div');
      autoSost.className = 'kbd-sostenuto-btn';
      autoSost.setAttribute('role', 'button');
      autoSost.setAttribute('tabindex', '0');
      autoSost.setAttribute('aria-label', 'Sostenuto Pedal On/Off (CC#66)');
      autoSost.setAttribute('title', 'Sostenuto Pedal: Hold Active Notes (CC#66)');
      autoSost.innerHTML = `<span class="kbd-sostenuto-led"></span><span class="kbd-sostenuto-label">SOST</span>`;
      autoSost.addEventListener('click', toggleSostenuto);
      autoSost.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleSostenuto(); } });
      container.appendChild(autoSost);
    }

    // Soft pedal button (CC#67)
    if (softPedalBtnId) {
      const softBtn = document.getElementById(softPedalBtnId);
      if (softBtn) softBtn.addEventListener('click', toggleSoftPedal);
    } else if (cfg.enableSoftPedal) {
      const autoSoft = document.createElement('div');
      autoSoft.className = 'kbd-soft-btn';
      autoSoft.setAttribute('role', 'button');
      autoSoft.setAttribute('tabindex', '0');
      autoSoft.setAttribute('aria-label', 'Soft Pedal On/Off (CC#67)');
      autoSoft.setAttribute('title', 'Soft Pedal: Attenuate Velocity (CC#67)');
      autoSoft.innerHTML = `<span class="kbd-soft-led"></span><span class="kbd-soft-label">SOFT</span>`;
      autoSoft.addEventListener('click', toggleSoftPedal);
      autoSoft.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleSoftPedal(); } });
      container.appendChild(autoSoft);
    }

    // Collapse button
    if (cfg.enableCollapse) {
      const chevron = document.createElement('div');
      chevron.className = 'kbd-collapse-btn';
      chevron.setAttribute('role', 'button');
      chevron.setAttribute('tabindex', '0');
      chevron.setAttribute('aria-label', 'Collapse keyboard');
      chevron.setAttribute('title', 'Collapse/Expand keyboard');
      chevron.innerHTML = `<span class="kbd-collapse-chevron">▾</span>`;
      chevron.addEventListener('click', toggleCollapse);
      chevron.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleCollapse(); } });
      container.appendChild(chevron);
    }

    if (cfg.enableQwerty) {
      window.addEventListener('keydown', handleKeydown);
      window.addEventListener('keyup', handleKeyup);
    }

    updateOctaveLEDs();

    if (cfg.enablePressureDisplay && (cfg.getPressureState || cfg.enableAftertouch)) {
      _pressureRafId = requestAnimationFrame(updatePressureDisplay);
    }

    setupAccessibility();
    applyScaleVisuals();

    // ResizeObserver
    let _lastWidth = 0, _lastHeight = 0;
    if (cfg.enableResizeObserver && typeof ResizeObserver !== 'undefined') {
      _resizeObserver = new ResizeObserver((entries) => {
        if (destroyed || _collapsed) return;
        for (const entry of entries) {
          const { width, height } = entry.contentRect;
          if (Math.abs(width - _lastWidth) < 3 && Math.abs(height - _lastHeight) < 3) continue;
          _lastWidth = width; _lastHeight = height;
          renderKeybed();
          if (cfg.enableAccessibility) setupAccessibility();
          applyScaleVisuals();
        }
      });
      _resizeObserver.observe(container);
    }
  }

  init();

  return {
    panic,
    sweep,
    getOctave: () => octaveShift / 12,
    setOctave: (oct) => {
      octaveShift = Math.max(-cfg.maxOctaveShift * 12, Math.min(cfg.maxOctaveShift * 12, oct * 12));
      updateOctaveLEDs();
      if (cfg.enableAccessibility) announce(`Octave shift ${oct > 0 ? '+' : ''}${oct}`);
      if (onOctaveChange) onOctaveChange(oct);
    },
    releaseNote: (midiNote) => { _releaseKey(midiNote); onNoteOff(midiNote); },
    highlightNote: (midiNote, velocity) => { _highlightKey(midiNote, velocity); },
    setSustain,
    getSustain: () => sustainOn,
    toggleSustain,
    // Sostenuto API (CC#66)
    setSostenuto,
    getSostenuto: () => sostenutoOn,
    toggleSostenuto,
    // Soft Pedal API (CC#67)
    setSoftPedal,
    getSoftPedal: () => softPedalOn,
    toggleSoftPedal,
    // Scale Filter API
    setScaleFilter,
    getScaleFilter: () => _scaleEnabled ? { type: _scaleType, root: _scaleRoot, snapMode: _scaleSnapMode } : null,
    setScaleSnapMode: (mode) => { _scaleSnapMode = mode; cfg.scaleSnapMode = mode; applyScaleVisuals(); },
    disableScaleFilter: () => setScaleFilter(null),
    // Collapse API
    collapse: () => setCollapsed(true),
    expand: () => setCollapsed(false),
    toggleCollapse,
    isCollapsed: () => _collapsed,
    // Chord Memory API
    saveChord, playChord, releaseChord, getChords, clearChord, clearAllChords,
    // Aftertouch API
    setAftertouch: (note, pressure) => {
      const p = Math.max(0, Math.min(1, pressure));
      if (note === -1 || note === null) {
        if (p === _channelAftertouch) return;
        _channelAftertouch = p;
        if (onAftertouch) onAftertouch(-1, p);
      } else {
        if (onAftertouch) onAftertouch(note, p);
      }
    },
    releaseAftertouch: (note) => {
      if (note === -1 || note === null) { _channelAftertouch = 0; if (onAftertouch) onAftertouch(-1, 0); }
      else { if (onAftertouch) onAftertouch(note, 0); }
    },
    getAftertouch: () => _channelAftertouch,
    // Visual / config API
    setLedColor: (color) => { cfg.ledColor = color; },
    setOctaveCount: (numOctaves, startNote) => {
      cfg.numOctaves = numOctaves;
      if (startNote !== undefined) cfg.startNote = startNote;
      renderKeybed();
      applyScaleVisuals();
    },
    getActiveNotes: () => Array.from(activeKeys.values()).map(n => n.actualNote),
    destroy: () => {
      destroyed = true;
      if (_pressureRafId) cancelAnimationFrame(_pressureRafId);
      if (_resizeObserver) { _resizeObserver.disconnect(); _resizeObserver = null; }
      if (cfg.enableQwerty) { window.removeEventListener('keydown', handleKeydown); window.removeEventListener('keyup', handleKeyup); }
      if (cfg.enableAccessibility) container.removeEventListener('keydown', handleA11yKeyNav);
      for (const wheel of wheels) wheel.destroy();
      container.innerHTML = '';
      activeKeys.clear();
      qwertyActive.clear();
    }
  };
}
