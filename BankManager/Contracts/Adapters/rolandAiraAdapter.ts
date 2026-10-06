/**
 * Roland AIRA Modular (Bitrazer / Torcido / Demora / Scooper) Adapter
 *
 * SysEx format (Roland standard, from ABDSharedAssets roland_aira_patch_spec):
 *   Data Set 1    (DT1): F0 41 10 00 00 00 <model> 12 aa bb cc dd <data...> <checksum> F7
 *   Data Request 1 (RQ1): F0 41 10 00 00 00 <model> 11 aa bb cc dd ss tt uu vv <checksum> F7
 *
 * - Byte 2 (0x10) is the Roland device ID; AIRA modules do not use the MIDI
 *   channel byte in the header, so the channel-aware base helpers are not used.
 * - Checksum: Roland 7-bit — (0x80 - (sum(addr+data) & 0x7F)) & 0x7F, summed
 *   from the first address byte through the last data byte.
 *
 * Address map (from the patch spec contract):
 *   10 00 00 01..0A   main module params (10 per model)
 *   10 10 00 <5n>+0   slot n type (slots 1..6 -> offsets 00,05,0A,0F,14,19)
 *   10 10 00 <5n>+1-4 slot n params (4 per slot, contiguous with type byte)
 *   10 20 <src> <dst> cable connect op (data 0x01 = connected, 0x00 = removed)
 *   10 21 00 <src>    cable condition: 6-byte destination bitmask per source
 *
 * Model IDs: 0x15 Bitrazer, 0x16 Torcido, 0x17 Demora, 0x18 Scooper.
 * All four share the same internal state layout — only the factory algorithm
 * and panel differ — so one canonical state format serves all four.
 *
 * Canonical rawData layout (little-endian, fixed offsets):
 *   [0]      model ID byte (0x15..0x18)
 *   [1..10]  main params P1..P10 (10 bytes)
 *   [11..16] slot types, slots 1..6 (0x00 = empty slot)
 *   [17..40] slot params, 4 per slot x 6 slots (24 bytes)
 *   [41]     cable count N
 *   [42..]   N x (source, destination) byte pairs
 *   [..+132] cable conditions: 22 sources x 6 destination-bitmask bytes
 */

import { BaseImportAdapter, ImportResult, PatchData } from '../ImportAdapter';
import { BaseExportAdapter, ExportOptions } from '../ExportAdapter';
import { BaseHardwareLink, HardwareDevice, ImportResult as HLImportResult } from '../HardwareLinkContract';
import type { MidiOutputPortInfo } from '../Midi';
import { getModelContract } from '../Models';
import { splitSysexMessages } from '../SysEx/codec';

// ─── Constants ───

const DEVICE_ID = 0x10;
const FAMILY_BYTES = [0x00, 0x00, 0x00];
const CMD_RQ1 = 0x11;
const CMD_DT1 = 0x12;

const MODEL_IDS: Record<string, number> = {
  'roland-aira-bitrazer': 0x15,
  'roland-aira-torcido':  0x16,
  'roland-aira-demora':   0x17,
  'roland-aira-scooper':  0x18,
};
const ALL_MODEL_IDS = Object.keys(MODEL_IDS);
const MODEL_ID_BYTES = Object.values(MODEL_IDS);

const MAIN_PARAM_COUNT = 10;
const SLOT_COUNT = 6;
const PARAMS_PER_SLOT = 4;
const CONDITION_SOURCES = 22;
const CONDITION_BYTES_PER_SOURCE = 6;
const MAX_CABLES = 64;

// Canonical rawData offsets
const OFF_MODEL = 0;
const OFF_MAIN = 1;
const OFF_SLOT_TYPES = OFF_MAIN + MAIN_PARAM_COUNT;                       // 11
const OFF_SLOT_PARAMS = OFF_SLOT_TYPES + SLOT_COUNT;                      // 17
const OFF_CABLE_COUNT = OFF_SLOT_PARAMS + SLOT_COUNT * PARAMS_PER_SLOT;   // 41
const OFF_CABLES = OFF_CABLE_COUNT + 1;                                   // 42
const OFF_CONDITIONS = OFF_CABLES + MAX_CABLES * 2;                       // 170
const CANON_MIN_LEN = OFF_CONDITIONS + CONDITION_SOURCES * CONDITION_BYTES_PER_SOURCE; // 302

// ─── Roland 7-bit Checksum ───

/** (0x80 - (sum & 0x7F)) & 0x7F over addr+data */
export function rolandChecksum(bytes: Uint8Array): number {
  let sum = 0;
  for (const b of bytes) sum += b;
  return (0x80 - (sum & 0x7F)) & 0x7F;
}

// ─── DT1 / RQ1 Framing ───

function frame(modelByte: number, cmd: number, body: number[]): Uint8Array {
  // Roland checksum scope: address + data bytes only (the body)
  const checksum = rolandChecksum(Uint8Array.from(body));
  return new Uint8Array([0xF0, 0x41, DEVICE_ID, ...FAMILY_BYTES, modelByte, cmd, ...body, checksum, 0xF7]);
}

/** Build a DT1 (Data Set 1) write message for a 4-byte address + data. */
export function buildDt1(modelByte: number, addr: number[], data: Uint8Array): Uint8Array {
  return frame(modelByte, CMD_DT1, [...addr, ...Array.from(data)]);
}

/** Build an RQ1 (Data Request 1) read message: 4-byte address + 4-byte size. */
export function buildRq1(modelByte: number, addr: number[], size: number[]): Uint8Array {
  return frame(modelByte, CMD_RQ1, [...addr, ...size]);
}

export interface Dt1 {
  modelByte: number;
  addr: [number, number, number, number];
  data: Uint8Array;
}

/**
 * Parse and checksum-verify a DT1 message.
 * Returns null when the message is not an AIRA DT1 or the checksum fails.
 */
export function parseDt1(msg: Uint8Array): Dt1 | null {
  // F0 41 10 00 00 00 <model> 12 <a1..a4> <data...> <checksum> F7
  if (msg.length < 14) return null;
  if (msg[0] !== 0xF0 || msg[1] !== 0x41 || msg[2] !== DEVICE_ID) return null;
  if (msg[3] !== 0x00 || msg[4] !== 0x00 || msg[5] !== 0x00) return null;
  if (!MODEL_ID_BYTES.includes(msg[6]) || msg[7] !== CMD_DT1) return null;
  const addr: [number, number, number, number] = [msg[8], msg[9], msg[10], msg[11]];
  const data = msg.slice(12, msg.length - 2);
  const expected = rolandChecksum(Uint8Array.from([...addr, ...data]));
  if (msg[msg.length - 2] !== expected) return null;
  return { modelByte: msg[6], addr, data };
}

// ─── Canonical State Codec ───

export interface AiraCable {
  source: number;
  destination: number;
}

export interface AiraState {
  modelId: string;
  mainParams: number[];
  slotTypes: number[];
  slotParams: number[][];
  cables: AiraCable[];
  /** 22 sources x 6 bytes of destination bitmask */
  conditions: Uint8Array;
}

/** Serialize structured AIRA state into the canonical rawData layout. */
export function encodeAiraState(state: AiraState): Uint8Array {
  const cables = (state.cables || []).slice(0, MAX_CABLES);
  const out = new Uint8Array(CANON_MIN_LEN + cables.length * 2);
  out[OFF_MODEL] = MODEL_IDS[state.modelId] || 0x15;
  for (let i = 0; i < MAIN_PARAM_COUNT; i++) {
    out[OFF_MAIN + i] = (state.mainParams[i] || 0) & 0x7F;
  }
  for (let s = 0; s < SLOT_COUNT; s++) {
    out[OFF_SLOT_TYPES + s] = (state.slotTypes[s] || 0) & 0x7F;
  }
  for (let s = 0; s < SLOT_COUNT; s++) {
    for (let p = 0; p < PARAMS_PER_SLOT; p++) {
      out[OFF_SLOT_PARAMS + s * PARAMS_PER_SLOT + p] =
        ((state.slotParams[s] && state.slotParams[s][p]) || 0) & 0x7F;
    }
  }
  out[OFF_CABLE_COUNT] = cables.length;
  cables.forEach((c, i) => {
    out[OFF_CABLES + i * 2] = c.source & 0x7F;
    out[OFF_CABLES + i * 2 + 1] = c.destination & 0x7F;
  });
  const condLen = CONDITION_SOURCES * CONDITION_BYTES_PER_SOURCE;
  if (state.conditions && state.conditions.length) {
    out.set(Uint8Array.from(state.conditions.slice(0, condLen)), CANON_MIN_LEN - condLen);
  }
  return out;
}

/** Decode canonical rawData back into structured state. Returns null if malformed. */
export function decodeAiraState(raw: Uint8Array): AiraState | null {
  if (!raw || raw.length < CANON_MIN_LEN) return null;
  const modelId = Object.entries(MODEL_IDS).find(([, b]) => b === raw[OFF_MODEL])?.[0];
  if (!modelId) return null;
  const cableCount = Math.min(raw[OFF_CABLE_COUNT], MAX_CABLES);
  const minLen = OFF_CABLES + cableCount * 2
    + CONDITION_SOURCES * CONDITION_BYTES_PER_SOURCE;
  if (raw.length < minLen) return null;
  const cables: AiraCable[] = [];
  for (let i = 0; i < cableCount; i++) {
    cables.push({
      source: raw[OFF_CABLES + i * 2],
      destination: raw[OFF_CABLES + i * 2 + 1],
    });
  }
  return {
    modelId,
    mainParams: Array.from(raw.slice(OFF_MAIN, OFF_MAIN + MAIN_PARAM_COUNT)),
    slotTypes: Array.from(raw.slice(OFF_SLOT_TYPES, OFF_SLOT_TYPES + SLOT_COUNT)),
    slotParams: Array.from({ length: SLOT_COUNT }, (_, s) =>
      Array.from(raw.slice(
        OFF_SLOT_PARAMS + s * PARAMS_PER_SLOT,
        OFF_SLOT_PARAMS + (s + 1) * PARAMS_PER_SLOT,
      ))),
    cables,
    conditions: new Uint8Array(raw.slice(
      CANON_MIN_LEN - CONDITION_SOURCES * CONDITION_BYTES_PER_SOURCE,
      CANON_MIN_LEN,
    )),
  };
}

// ─── DT1 Stream <-> State ───

/** Apply one DT1 to an in-progress state (mutates). */
function applyDt1(state: AiraState, dt1: Dt1): void {
  const [q, w, x, y] = dt1.addr;
  if (q === 0x10 && w === 0x00 && x === 0x00) {
    // Main params: 10 00 00 01..0A, possibly as one contiguous block
    for (let k = 0; k < dt1.data.length; k++) {
      const idx = y - 1 + k;
      if (idx >= 0 && idx < MAIN_PARAM_COUNT) state.mainParams[idx] = dt1.data[k] & 0x7F;
    }
  } else if (q === 0x10 && w === 0x10 && x === 0x00) {
    // Slot block: 10 10 00 <5n + off>, off 0 = type, 1..4 = params
    const slot = Math.floor(y / 5);
    const off = y % 5;
    if (slot >= 0 && slot < SLOT_COUNT) {
      for (let k = 0; k < dt1.data.length; k++) {
        const o = off + k;
        if (o === 0) state.slotTypes[slot] = dt1.data[k] & 0x7F;
        else if (o <= PARAMS_PER_SLOT) state.slotParams[slot][o - 1] = dt1.data[k] & 0x7F;
      }
    }
  } else if (q === 0x10 && w === 0x20 && dt1.data.length === 1) {
    // Cable connect op: 10 20 <src> <dst>, data 0x01 = connected
    if (dt1.data[0] === 0x01
      && !state.cables.some(c => c.source === x && c.destination === y)
      && state.cables.length < MAX_CABLES) {
      state.cables.push({ source: x, destination: y });
    }
  } else if (q === 0x10 && w === 0x21 && x === 0x00 && y < CONDITION_SOURCES) {
    // Condition bitmask: 10 21 00 <src>, 6 bytes of destination bits
    const base = CANON_MIN_LEN - CONDITION_SOURCES * CONDITION_BYTES_PER_SOURCE
      + y * CONDITION_BYTES_PER_SOURCE;
    for (let k = 0; k < CONDITION_BYTES_PER_SOURCE; k++) {
      state.conditions[base - OFF_CABLES - state.cables.length * 0 + k] = dt1.data[k] & 0xFF;
    }
  }
}

/** Build the full DT1 write stream for a state (main -> slots -> cables -> conditions). */
export function buildStateDt1s(modelByte: number, state: AiraState): Uint8Array[] {
  const msgs: Uint8Array[] = [];
  // Main params are contiguous: one DT1 for all 10
  msgs.push(buildDt1(modelByte, [0x10, 0x00, 0x00, 0x01],
    Uint8Array.from(state.mainParams.slice(0, MAIN_PARAM_COUNT))));
  // Each slot: type + 4 params are contiguous (5 bytes at 10 10 00 <5n>)
  for (let s = 0; s < SLOT_COUNT; s++) {
    const block = [state.slotTypes[s] || 0,
      ...(state.slotParams[s] || [0, 0, 0, 0]).slice(0, PARAMS_PER_SLOT)];
    msgs.push(buildDt1(modelByte, [0x10, 0x10, 0x00, s * 5], Uint8Array.from(block)));
  }
  // Cable connect ops
  for (const c of state.cables) {
    msgs.push(buildDt1(modelByte, [0x10, 0x20, c.source & 0x7F, c.destination & 0x7F],
      new Uint8Array([0x01])));
  }
  // Condition bitmasks — only emit sources with at least one bit set
  for (let src = 0; src < CONDITION_SOURCES; src++) {
    const mask = state.conditions.slice(
      src * CONDITION_BYTES_PER_SOURCE, (src + 1) * CONDITION_BYTES_PER_SOURCE);
    if (mask.some(b => b !== 0)) {
      msgs.push(buildDt1(modelByte, [0x10, 0x21, 0x00, src], mask));
    }
  }
  return msgs;
}

/**
 * Accept either a flat byte array or an array of already-split messages
 * (e.g. the Uint8Array[] returned by buildStateDt1s / buildPatchDump).
 */
function flattenInput(data: Uint8Array | Uint8Array[]): Uint8Array {
  if (!Array.isArray(data)) return data as Uint8Array;
  const total = (data as Uint8Array[]).reduce((n, m) => n + m.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const m of data as Uint8Array[]) { out.set(m, off); off += m.length; }
  return out;
}

// ─── Import Adapter ───

export class RolandAiraImportAdapter extends BaseImportAdapter {
  adapterId = 'sysex-roland-aira';
  displayName = 'Roland AIRA Modular (Bitrazer / Torcido / Demora / Scooper)';
  supportedExtensions = ['.syx'];
  targetModelIds = ALL_MODEL_IDS;

  canParse(data: Uint8Array | Uint8Array[], filename: string): boolean {
    if (!filename.toLowerCase().endsWith('.syx')) return false;
    return splitSysexMessages(flattenInput(data)).some(m => parseDt1(m) !== null);
  }

  verifyChecksum(data: Uint8Array | Uint8Array[]): boolean {
    const msgs = splitSysexMessages(flattenInput(data)).filter(m =>
      m.length > 8 && m[0] === 0xF0 && m[1] === 0x41 && m[2] === DEVICE_ID
      && MODEL_ID_BYTES.includes(m[6]));
    if (msgs.length === 0) return false;
    // Delegates the actual checksum verification to the AIRA ModelContract.
    return msgs.every(m => {
      const modelId = Object.entries(MODEL_IDS).find(([, b]) => b === m[6])?.[0];
      const contract = modelId ? getModelContract(modelId) : undefined;
      return contract?.verifyChecksum ? contract.verifyChecksum(m) : false;
    });
  }

  parse(data: Uint8Array | Uint8Array[], filename: string): ImportResult {
    const dt1s = splitSysexMessages(flattenInput(data))
      .map(m => parseDt1(m))
      .filter((d): d is Dt1 => d !== null);
    if (dt1s.length === 0) {
      return this.createResult({
        success: false,
        modelId: 'roland-aira-bitrazer',
        error: 'No se encontraron mensajes SysEx Roland AIRA válidos',
      });
    }

    const modelId = Object.entries(MODEL_IDS)
      .find(([, b]) => b === dt1s[0].modelByte)?.[0] || 'roland-aira-bitrazer';
    const state: AiraState = {
      modelId,
      mainParams: new Array(MAIN_PARAM_COUNT).fill(0),
      slotTypes: new Array(SLOT_COUNT).fill(0),
      slotParams: Array.from({ length: SLOT_COUNT }, () => new Array(PARAMS_PER_SLOT).fill(0)),
      cables: [],
      conditions: new Uint8Array(CONDITION_SOURCES * CONDITION_BYTES_PER_SOURCE),
    };
    for (const dt1 of dt1s) applyDt1(state, dt1);

    const patches: PatchData[] = [this.createPatchData({
      name: `${modelId.replace('roland-aira-', '')} state`,
      originAddress: 'AIRA',
      rawData: encodeAiraState(state),
      hardwareIds: [modelId, ...ALL_MODEL_IDS.filter(id => id !== modelId)],
    })];

    return this.createResult({
      modelId,
      bankName: `AIRA ${modelId.replace('roland-aira-', '')}`,
      patches,
    });
  }
}

// ─── Export Adapter ───

export class RolandAiraExportAdapter extends BaseExportAdapter {
  adapterId = 'export-roland-aira';
  displayName = 'Roland AIRA Modular (Bitrazer / Torcido / Demora / Scooper)';
  fileExtension = '.syx';
  targetModelIds = ALL_MODEL_IDS;

  /**
   * Serializes the AIRA module state (stored in patch[0].rawData in the
   * canonical layout) as a concatenated DT1 stream.
   */
  serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array {
    void bankName;
    void options;
    const patch = patches[0];
    if (!patch) return new Uint8Array(0);
    const state = decodeAiraState(patch.rawData);
    if (!state) return new Uint8Array(0);
    const modelByte = MODEL_IDS[state.modelId] || 0x15;

    const msgs = buildStateDt1s(modelByte, state);
    const total = msgs.reduce((n, m) => n + m.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const m of msgs) { out.set(m, off); off += m.length; }
    return out;
  }
}

// ─── Hardware Link ───

export class RolandAiraHardwareLink extends BaseHardwareLink {
  modelId = 'roland-aira-bitrazer';
  supportsEditBuffer = false;
  interMessageDelayMs = 30;
  dumpTimeoutMs = 5000;

  protected getManufacturerId(): number[] { return [0x41, DEVICE_ID, ...FAMILY_BYTES]; }
  protected getModelId(): number { return MODEL_IDS[this.modelId] || 0x15; }

  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null {
    // Delegates port-name matching to each AIRA ModelContract (one per model).
    for (const modelId of ALL_MODEL_IDS) {
      const contract = getModelContract(modelId);
      const device = contract?.detectHardware?.(midiOutputs);
      if (device) return { ...device, modelId: this.modelId };
    }
    return null;
  }

  /**
   * AIRA modules hold one state per device (no patch slots), so `slot` is
   * ignored and the whole state is written as a DT1 stream.
   */
  buildPatchDump(patch: PatchData, slot: number, channel: number): Uint8Array[] {
    void slot; void channel;
    const state = decodeAiraState(patch.rawData);
    if (!state) return [];
    return buildStateDt1s(MODEL_IDS[state.modelId] || this.getModelId(), state);
  }

  buildBankDump(patches: PatchData[], channel: number): Uint8Array[] {
    return this.buildPatchDump(patches[0], 0, channel);
  }

  /**
   * RQ1 read request. 'all' reads the main-param block; the slot and cable
   * blocks live in non-contiguous address ranges and need separate RQ1s
   * (limitation of the single-message contract return type).
   */
  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    void channel;
    const modelByte = this.getModelId();
    if (slot === 'all' || slot === 0) {
      return buildRq1(modelByte, [0x10, 0x00, 0x00, 0x00], [0x00, 0x00, 0x00, 0x10]);
    }
    const s = Math.min(Math.max(slot, 1), SLOT_COUNT) - 1;
    return buildRq1(modelByte, [0x10, 0x10, 0x00, s * 5], [0x00, 0x00, 0x00, 0x05]);
  }

  parseDumpResponse(data: Uint8Array | Uint8Array[]): HLImportResult {
    const adapter = new RolandAiraImportAdapter();
    return adapter.parse(data, 'dump.syx');
  }
}
