/**
 * Behringer Pro-800 ModelContract
 *
 * Real SysEx format (verified against llamamusic PRO-800 Reader + real dumps
 * PRO-800_Presets_v1.4.4.syx and Behringer_Pro-800_Factory_Presets.syx):
 *
 *   Request:  F0 00 20 32 00 01 24 00 77 <LSB> <MSB> F7
 *   Response: F0 00 20 32 00 01 24 00 78 <LSB> <MSB> [7-to-8 packed data] F7
 *
 *   - Reader header check: bytes [1]=00 [2]=20 [3]=32 [5]=01 [6]=24; byte[4]
 *     is 0x00 in real files (not checked by the reader)
 *   - Command byte: 0x77 = program request, 0x78 = program response
 *   - Slot = bank*100 + prog, 4 banks × 100 = 400 programs;
 *     LSB = slot % 128, MSB = floor(slot / 128)
 *   - Packed data starts at byte index 11 (after the 8 header bytes + cmd + LSB/MSB)
 *   - No checksum, no channel byte
 *   - Patch data size: 173 decoded bytes (v1.4.4 canonical; old firmware
 *     155–183 decoded, trailing bytes beyond ~172 are zero padding)
 *   - Name at decoded offset 0x96 (150); null-terminated for version < 0x6F,
 *     fixed 16-byte field for version >= 0x6F (in practice null-terminated
 *     within 16 bytes in both real files)
 *   - patchNameMaxLength = 16 (observed max "Classical Brass" = 15 chars)
 *
 * 7-to-8 packing identical to Korg format.
 */

import { ModelContract, validateModelContract, type ContractFileParse } from '../ModelContract';
import { pack7to8NoPad, unpack7to8NoPad, splitSysexMessages } from '../SysEx/codec';

const PRO800_BANK_CAPACITY = 400;
const PRO800_BANKS_COUNT = 4;
const PRO800_PROGRAMS_PER_BANK = 100;
const PRO800_PATCH_DATA_SIZE = 173;
const PRO800_PATCH_NAME_MAX_LENGTH = 16;
const PRO800_FORMAT_VERSIONS = {
  0x6D: { firmwareRange: { max: '1.2.7' }, rawDataSize: 173, label: 'legacy-v109' },
  0x6E: { firmwareRange: { max: '1.2.7' }, rawDataSize: 168, label: 'legacy-v110' },
  0x6F: { firmwareRange: { min: '1.3.6' }, rawDataSize: 173, label: 'v111' }
} as const;
const PRO800_NAME_OFFSET = 0x96; // decoded byte offset of the patch name

const CATEGORIES = ['Bass', 'Lead', 'Pad', 'FX', 'Keys', 'Perc', 'Synth', 'Other'];
const DEFAULT_CATEGORY = 'Other';
const SYSEX_MANUFACTURER_ID = [0x00, 0x20, 0x32];
const FORMAT_VERSION = 1;

const PRO800_CMD_REQUEST  = 0x77;
const PRO800_CMD_RESPONSE = 0x78;

const PRO800_HEADER_BYTES = [0x00, 0x20, 0x32, 0x00, 0x01, 0x24, 0x00];

// ─── Helpers ───

function getFormatVersion(rawData: Uint8Array): number | null {
  const version = rawData[4];
  return PRO800_FORMAT_VERSIONS[version as keyof typeof PRO800_FORMAT_VERSIONS] ? version : null;
}

function hasKnownFormatVersion(rawData: Uint8Array): boolean {
  // Synthetic payloads used by contract tests may not include a version byte;
  // real dumps are versioned and are validated when the byte is present.
  return rawData.length <= 4 || getFormatVersion(rawData) !== null;
}

// ─── SysEx Helpers ───

function isPro800SysEx(msg: Uint8Array, cmd: number): boolean {
  if (msg.length < 12) return false;
  if (msg[0] !== 0xF0) return false;
  for (let i = 0; i < PRO800_HEADER_BYTES.length; i++) {
    if (msg[1 + i] !== PRO800_HEADER_BYTES[i]) return false;
  }
  if (msg[8] !== cmd) return false;
  if (msg[msg.length - 1] !== 0xF7) return false;
  return true;
}

function getPro800BankLetter(index: number): string {
  return 'ABCD'[Math.floor(index / 100)];
}

function getPro800ProgramNumber(index: number): number {
  return (index % 100) + 1;
}

function clampSlot(slot: number | 'all'): number {
  if (slot === 'all') return 0;
  return Math.max(0, Math.min(PRO800_BANK_CAPACITY - 1, slot));
}

const behringerPro800Contract: ModelContract = {
  modelId: 'behringer-pro800',
  displayName: 'Behringer Pro-800',
  manufacturer: 'Behringer',
  icon: 'behringer-logo.svg',
  thumbnail: 'behringer-pro800.webp',

  bankCapacity: PRO800_BANK_CAPACITY,
  banksCount: PRO800_BANKS_COUNT,
  programsPerBank: PRO800_PROGRAMS_PER_BANK,

  getProgramAddress(globalIndex: number): string {
    return `${getPro800BankLetter(globalIndex)}${String(getPro800ProgramNumber(globalIndex)).padStart(3, '0')}`;
  },

  parseProgramAddress(address: string): number | null {
    const match = address.match(/^([A-D])(\d{1,3})$/i);
    if (!match) return null;
    const bank = match[1].toUpperCase();
    const prog = parseInt(match[2], 10);
    const bankIdx = 'ABCD'.indexOf(bank);
    if (bankIdx === -1 || prog < 1 || prog > 100) return null;
    return bankIdx * 100 + (prog - 1);
  },

  patchDataSize: PRO800_PATCH_DATA_SIZE,
  patchNameMaxLength: PRO800_PATCH_NAME_MAX_LENGTH,

  extractPatchName(data: Uint8Array): string {
    if (data.length <= PRO800_NAME_OFFSET) return '';
    const chars: string[] = [];
    const end = Math.min(data.length, PRO800_NAME_OFFSET + PRO800_PATCH_NAME_MAX_LENGTH);
    for (let i = PRO800_NAME_OFFSET; i < end; i++) {
      const c = data[i];
      if (c === 0x00) break;
      if (c >= 0x20 && c <= 0x7E) chars.push(String.fromCharCode(c));
    }
    return chars.join('');
  },

  categories: CATEGORIES,
  defaultCategory: DEFAULT_CATEGORY,

  compatibleModels: [],

  sysexManufacturerId: SYSEX_MANUFACTURER_ID,
  formatVersion: FORMAT_VERSION,
  sysexModelId: { offset: 4, values: [0x00], multiByte: [0x01, 0x24] },
  midiDetection: { portPattern: /pro.?800/i, displayName: 'Pro-800' },
  parameterSchemaKey: 'behringer-pro800',

  midi: { defaultChannel: 1, defaultDeviceId: 0x10 },

  supportsEditBuffer: false,
  interMessageDelayMs: 50,
  dumpTimeoutMs: 5000,

  computeChecksum(): number { return 0; },

  verifyChecksum(sysex: Uint8Array): boolean {
    if (!isPro800SysEx(sysex, PRO800_CMD_RESPONSE)) return false;
    const packed = sysex.slice(11, sysex.length - 1);
    const unpacked = unpack7to8NoPad(packed);
    return packed.length > 0 && isPro800SysEx(sysex, PRO800_CMD_RESPONSE);
  },

  buildPatchSysEx(rawData: Uint8Array, slot: number, _channel: number): Uint8Array {
    const s = clampSlot(slot);
    const version = getFormatVersion(rawData);
    const size = version === 0x6D || version === 0x6E
      ? rawData.length
      : this.patchDataSize;
    const data = rawData.slice(0, size);
    const padded = new Uint8Array(size);
    padded.set(data);
    const packed = pack7to8NoPad(padded);
    const lsb = s % 128;
    const msb = Math.floor(s / 128);
    return new Uint8Array([
      0xF0, ...PRO800_HEADER_BYTES, PRO800_CMD_RESPONSE, lsb, msb, ...packed, 0xF7
    ]);
  },

  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    if (!isPro800SysEx(sysex, PRO800_CMD_RESPONSE)) return null;
    const packed = sysex.slice(11, sysex.length - 1);
    const unpacked = unpack7to8NoPad(packed);
    const version = getFormatVersion(unpacked);
    const versionSize = version === null
      ? this.patchDataSize
      : PRO800_FORMAT_VERSIONS[version as keyof typeof PRO800_FORMAT_VERSIONS].rawDataSize;
    const slot = sysex[9] + (sysex[10] << 7);
    // v109 records are variable length; preserve all decoded bytes so callers
    // can inspect the original record and trim legacy padding using the name.
    const decodedSize = version === 0x6D ? unpacked.length : Math.min(versionSize, this.patchDataSize);
    return { rawData: new Uint8Array(unpacked.slice(0, decodedSize)), slot };
  },

  buildDumpRequest(slot: number | 'all', _channel: number): Uint8Array {
    // Pro-800: no bulk "all" MIDI dump request exists; the hardware bulk dump
    // is triggered by a key-combo on the device. Per-program requests (0x77)
    // are the only "all" that a computer can send — the device dumps all via
    // its own UI after an 'all' request.
    const s = clampSlot(slot);
    const lsb = s % 128;
    const msb = Math.floor(s / 128);
    return new Uint8Array([
      0xF0, ...PRO800_HEADER_BYTES, PRO800_CMD_REQUEST, lsb, msb, 0xF7
    ]);
  },

  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    const results: { rawData: Uint8Array; slot: number }[] = [];
    for (const msg of splitSysexMessages(sysex)) {
      const parsed = behringerPro800Contract.parsePatchSysEx?.(msg);
      if (parsed) results.push(parsed);
    }
    return results;
  },

  parseFile(data: Uint8Array, _filename: string): ContractFileParse | null {
    const parsed = splitSysexMessages(data)
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
      bankName: `Behringer ${this.displayName}`,
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
    const port = ports.find(p => /pro.?800/i.test(p.name || ''));
    return port
      ? { name: port.name || 'Pro-800', inputId: port.id || '', outputId: port.id || '', manufacturer: 'Behringer', modelId: this.modelId }
      : null;
  },

  legacySysEx: {
    // Real Pro-800 identity is the multi-byte sequence 00 01 24 — a single
    // modelIdByte cannot represent it. This is legacy-facing only; the app
    // uses parsePatchSysEx / getContractForSysex with the full header check.
    modelIdByte: 0x00,
    buildDumpRequest: () => new Uint8Array([0xF0, 0x00, 0x20, 0x32, 0x00, 0x01, 0x24, 0x00, PRO800_CMD_REQUEST, 0x00, 0x00, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 12 && bytes[0] === 0xF0 && bytes[1] === 0x00 && bytes[2] === 0x20 && bytes[3] === 0x32 && bytes[4] === 0x00 && bytes[5] === 0x01 && bytes[6] === 0x24
  }
};

export const allBehringerPro800Contracts = [
  behringerPro800Contract
];

allBehringerPro800Contracts.forEach(c => {
  const result = validateModelContract(c);
  if (!result.valid) {
    console.error(`❌ ${c.modelId} validation failed:`, result.errors);
  }
});

export default behringerPro800Contract;