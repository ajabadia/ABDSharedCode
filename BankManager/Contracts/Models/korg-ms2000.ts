/**
 * Korg MS2000 / microKORG / Prophecy ModelContract
 * Covers: MS2000, microKORG, Prophecy
 *
 * SysEx format (ABDMS2000 reference):
 *   Single dump:      F0 42 3n 58 40 [128B packed] F7
 *   All data dump:    F0 42 3n 58 4C [128×128B packed] F7
 *   Param change:     F0 42 3n 58 41 [offsetL] [offsetH] [value] F7
 *   Dump request:     F0 42 3n 58 10 F7
 *   All dump request: F0 42 3n 58 0E F7
 *
 * Packing: 7-to-8 bit (every 7 data bytes → 1 control + 7 encoded bytes)
 * No separate checksum — integrity via packing structure.
 *
 * 128-byte raw layout: [0x00..0x0B] name (12B), [0x0C] voice byte,
 * [0x0D] reserved, [0x0E..0x4B] timbre params, [0x4C..0x7F] reserved.
 */

import {
  ModelContract, validateModelContract, type ContractFileParse
} from '../ModelContract';
import {
  pack8to7, unpack7to8, splitSysexMessages
} from '../SysEx/codec';

const BANK_CAPACITY = 128;
const BANKS_COUNT = 8;
const PROGRAMS_PER_BANK = 16;
const PATCH_DATA_SIZE = 288; // MS2000/microKORG raw patch data size (before 7-to-8 packing)
const PATCH_NAME_MAX_LENGTH = 12;

const CATEGORIES = ['Bass', 'Lead', 'Pad', 'FX', 'Keys', 'Perc', 'Synth', 'Other'];
const DEFAULT_CATEGORY = 'Other';
const SYSEX_MANUFACTURER_ID = [0x42];
const FORMAT_VERSION = 1;

const BANK_LETTERS = 'ABCDEFGH';

// ─── Command codes (ABDMS2000 SysExManager.h / MS2000_SysEx_Spec.md) ───
const CMD_DUMP       = 0x40; // Program Data Dump (single patch)
const CMD_ALL_DUMP   = 0x4C; // All Data Dump (128 patches)
const CMD_REQUEST    = 0x10; // Program Dump Request
const CMD_ALL_REQUEST = 0x0E; // All Data Dump Request
const CMD_PARAM      = 0x41; // Parameter Change
const CMD_ACK        = 0x23; // Write Completed
const CMD_NACK       = 0x24; // Write Error

// ─── Model IDs (per hardware) ───
// MS2000/microKORG use model byte 0x58. Prophecy uses 0x41 (different format).
const MODEL_IDS: Record<string, number> = {
  'korg-ms2000':    0x58,
  'korg-microkorg': 0x58, // identical SysEx format to MS2000
  'korg-prophecy':  0x41
};

function getBankLetter(index: number): string {
  return BANK_LETTERS[Math.floor(index / 16)];
}

function getProgramNumber(index: number): number {
  return (index % 16) + 1;
}

// ─── SysEx Helpers ───

function isKorgSysEx(msg: Uint8Array, modelIdByte: number): boolean {
  return msg.length >= 5
    && msg[0] === 0xF0
    && msg[1] === 0x42
    && msg[3] === modelIdByte
    && (msg[4] === CMD_DUMP || msg[4] === CMD_ALL_DUMP);
}

const korgMs2000Contract: ModelContract = {
  modelId: 'korg-ms2000',
  displayName: 'Korg MS2000',
  manufacturer: 'Korg',
  icon: 'korg-logo.svg',
  thumbnail: 'korg-ms2000.webp',

  bankCapacity: BANK_CAPACITY,
  banksCount: BANKS_COUNT,
  programsPerBank: PROGRAMS_PER_BANK,

  getProgramAddress(globalIndex: number): string {
    return `${getBankLetter(globalIndex)}.${String(getProgramNumber(globalIndex)).padStart(2, '0')}`;
  },

  parseProgramAddress(address: string): number | null {
    const match = address.match(/^([A-H])\.(\d{2})$/i);
    if (!match) return null;
    const bank = match[1].toUpperCase();
    const prog = parseInt(match[2], 10);
    const bankIdx = BANK_LETTERS.indexOf(bank);
    if (bankIdx === -1 || prog < 1 || prog > 16) return null;
    return bankIdx * 16 + (prog - 1);
  },

  patchDataSize: PATCH_DATA_SIZE,
  patchNameMaxLength: PATCH_NAME_MAX_LENGTH,
  extractPatchName(data: Uint8Array): string {
    const nameOffset = 0x1C;
    if (data.length < nameOffset + PATCH_NAME_MAX_LENGTH) return '';
    const nameBytes = data.slice(nameOffset, nameOffset + PATCH_NAME_MAX_LENGTH);
    return new TextDecoder().decode(nameBytes).replace(/\0/g, '').trim();
  },

  categories: CATEGORIES,
  defaultCategory: DEFAULT_CATEGORY,

  compatibleModels: ['korg-microkorg'],

  sysexManufacturerId: SYSEX_MANUFACTURER_ID,
  formatVersion: FORMAT_VERSION,
  sysexModelId: { offset: 3, values: [0x58] },
  midiDetection: { portPattern: /ms.?2000|microkorg/i, displayName: 'Korg MS2000' },

  midi: {
    defaultChannel: 1,
    defaultDeviceId: 0x58
  },

  supportsEditBuffer: false,
  interMessageDelayMs: 50,
  dumpTimeoutMs: 3000,

  computeChecksum(): number { return 0; }, // Korg uses 7-to-8 packing, no separate checksum

  verifyChecksum(sysex: Uint8Array): boolean {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    if (sysex.length < 6) return false;
    if (sysex[0] !== 0xF0 || sysex[1] !== 0x42 || sysex[3] !== modelId) return false;
    if (sysex[sysex.length - 1] !== 0xF7) return false;
    // Check that unpacked data length is valid
    const packed = sysex.slice(5, sysex.length - 1);
    if (packed.length % 8 !== 0) return false;
    return true;
  },

  buildPatchSysEx(rawData: Uint8Array, slot: number, channel: number): Uint8Array {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    const size = this.patchDataSize;
    const data = rawData.slice(0, size);
    const padded = new Uint8Array(size);
    padded.set(data);

    const packed = pack8to7(padded);
    return new Uint8Array([0xF0, 0x42, 0x30 | (channel & 0x0F), modelId, CMD_DUMP, ...packed, 0xF7]);
  },

  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    if (!isKorgSysEx(sysex, modelId)) return null;
    if (sysex[sysex.length - 1] !== 0xF7) return null;

    const packed = sysex.slice(5, sysex.length - 1);
    const unpacked = unpack7to8(packed);

    const rawData = unpacked.slice(0, this.patchDataSize);
    const slot = 0; // Single patch dump, no slot info
    return { rawData: new Uint8Array(rawData), slot };
  },

  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    const cmd = slot === 'all' ? CMD_ALL_REQUEST : CMD_REQUEST;
    return new Uint8Array([0xF0, 0x42, 0x30 | (channel & 0x0F), modelId, cmd, 0xF7]);
  },

  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    const msgs = splitSysexMessages(sysex).filter(m => isKorgSysEx(m, modelId));
    const results: { rawData: Uint8Array; slot: number }[] = [];
    for (const msg of msgs) {
      const parsed = korgMs2000Contract.parsePatchSysEx?.call(this, msg);
      if (parsed) results.push(parsed);
    }
    return results;
  },

  parseFile(data: Uint8Array, _filename: string): ContractFileParse | null {
    const modelId = MODEL_IDS[this.modelId] || 0x58;
    const parsed = splitSysexMessages(data)
      .filter(m => isKorgSysEx(m, modelId))
      .map(m => this.parsePatchSysEx?.(m))
      .filter((p): p is { rawData: Uint8Array; slot: number } => p !== null);
    if (parsed.length === 0) return null;
    const patches = parsed.map(p => ({
      name: this.extractPatchName?.(p.rawData) || this.getProgramAddress(p.slot),
      category: this.defaultCategory,
      author: 'Unknown',
      tags: [] as string[],
      notes: '',
      originAddress: this.getProgramAddress(p.slot),
      rawData: new Uint8Array(p.rawData),
      isFavorite: false,
      creationDate: new Date().toISOString(),
    }));
    return {
      modelId: this.modelId,
      bankName: `Korg ${this.displayName}`,
      patches,
      warnings: [],
    };
  },

  serializeFile(patches: { rawData: Uint8Array; slot: number; name?: string }[], options: { midiChannel: number; deviceId: number; format: 'single' | 'bank' }): Uint8Array {
    const msgs = patches.map(p => this.buildPatchSysEx?.(p.rawData, p.slot, options.midiChannel) ?? new Uint8Array());
    if (msgs.length === 1) return msgs[0];
    const total = msgs.reduce((n, m) => n + m.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const m of msgs) { out.set(m, off); off += m.length; }
    return out;
  },

  detectHardware(ports: Array<{ name?: string; id?: string }>): { name: string; inputId: string; outputId: string; manufacturer: string; modelId: string } | null {
    const port = ports.find(p => /ms.?2000|microkorg/i.test(p.name || ''));
    return port
      ? { name: port.name || 'Korg MS2000', inputId: port.id || '', outputId: port.id || '', manufacturer: 'Korg', modelId: this.modelId }
      : null;
  },

  legacySysEx: {
    modelIdByte: 0x58,
    buildDumpRequest: (ch) => new Uint8Array([0xF0, 0x42, 0x30 | (ch & 0x0F), 0x58, CMD_REQUEST, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 5 && bytes[0] === 0xF0 && bytes[1] === 0x42 && bytes[3] === 0x58
  }
};

// microKORG (identical SysEx format)
export const korgMicrokorgContract: ModelContract = {
  ...korgMs2000Contract,
  modelId: 'korg-microkorg',
  displayName: 'Korg microKORG',
  thumbnail: 'korg-microkorg.webp'
};

// ABD MS2000 (SM002) — software synth of the ABDMS2000 host.
// Reuses the MS2000 patch format (rawData is an MS2000-style SysEx frame) so it
// can open hardware MS2000/microKORG banks, but is a local edit target: no
// physical MIDI port, so MIDI auto-detection/hardware routing is disabled.
export const korgAbdSm002Contract: ModelContract = {
  ...korgMicrokorgContract,
  modelId: 'abd-sm002',
  displayName: 'ABD MS2000 (SM002)',
  manufacturer: 'ABDSynths',
  isSoftsynth: true,
  compatibleModels: ['korg-ms2000', 'korg-microkorg'],
  midiDetection: undefined,
};

export const allKorgContracts = [
  korgMs2000Contract,
  korgMicrokorgContract,
  korgAbdSm002Contract
];

allKorgContracts.forEach(c => {
  const result = validateModelContract(c);
  if (!result.valid) {
    console.error(`❌ ${c.modelId} validation failed:`, result.errors);
  }
});

export default korgMs2000Contract;
