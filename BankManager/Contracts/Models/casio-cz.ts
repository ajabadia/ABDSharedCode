/**
 * Casio CZ Series ModelContract
 * Covers: CZ-101, CZ-1000, CZ-5000, CZ-1
 *
 * SysEx formats:
 *   Single dump:  F0 44 00 00 <modelId> 10 ch [nibbles] sum&0x7F F7
 *   Dump request: F0 44 00 00 <modelId> 30 ch F7
 *
 * Nibble encoding: each byte → 2 nibbles (high, low), doubles data size.
 * Checksum: sum of all nibble bytes & 0x7F.
 */

import {
  ModelContract, validateModelContract, type ContractFileParse
} from '../ModelContract';
import {
  casioChecksum, encodeNibble, decodeNibble, splitSysexMessages
} from '../SysEx/codec';

const BANK_CAPACITY = 16;
const BANKS_COUNT = 1;
const PROGRAMS_PER_BANK = 16;
const PATCH_DATA_SIZE = 128;
const PATCH_NAME_MAX_LENGTH = 0;

const CATEGORIES = ['Bass', 'Lead', 'Pad', 'FX', 'Keys', 'Perc', 'Synth', 'Other'];
const DEFAULT_CATEGORY = 'Other';
const SYSEX_MANUFACTURER_ID = [0x44];
const FORMAT_VERSION = 1;

const CMD_DUMP    = 0x10;
const CMD_REQUEST = 0x30;

const MODEL_IDS: Record<string, number> = {
  'casio-cz101':  0x12,
  'casio-cz1000': 0x13,
  'casio-cz5000': 0x14,
  'casio-cz1':    0x15
};

// Model byte -> modelId / patch size (evidencia: fixtures factory reales).
const MODEL_BY_BYTE: Record<number, { modelId: string; dataSize: number }> = {
  0x12: { modelId: 'casio-cz101',  dataSize: 128 },
  0x13: { modelId: 'casio-cz1000', dataSize: 128 },
  0x14: { modelId: 'casio-cz5000', dataSize: 128 },
  0x15: { modelId: 'casio-cz1',    dataSize: 288 }
};

function isCasioSysEx(msg: Uint8Array, modelId?: number): boolean {
  return msg.length >= 9
    && msg[0] === 0xF0 && msg[1] === 0x44
    && msg[2] === 0x00 && msg[3] === 0x00
    && (modelId === undefined || (msg[4] === modelId && MODEL_BY_BYTE[msg[4]] !== undefined))
    && (modelId !== undefined || MODEL_BY_BYTE[msg[4]] !== undefined)
    && msg[5] === CMD_DUMP
    && msg[msg.length - 1] === 0xF7;
}

/**
 * Checksum CZ (verificado contra los 128 mensajes de los fixtures factory):
 * suma & 0x7F desde el byte 6 (canal) hasta el ultimo nibble, excluyendo ck y F7.
 */
function czChecksumRegion(msg: Uint8Array): number {
  return casioChecksum(msg.slice(6, msg.length - 2));
}

function getBankLetter(index: number): string {
  return String.fromCharCode(0x41 + Math.floor(index / 16));
}

function getProgramNumber(index: number): number {
  return (index % 16) + 1;
}

const casioCzContract: ModelContract = {
  modelId: 'casio-cz101',
  displayName: 'Casio CZ-101',
  manufacturer: 'Casio',
  icon: 'casio-logo.svg',
  thumbnail: 'casio-cz101.webp',

  bankCapacity: BANK_CAPACITY,
  banksCount: BANKS_COUNT,
  programsPerBank: PROGRAMS_PER_BANK,

  getProgramAddress(globalIndex: number): string {
    return `${getBankLetter(globalIndex)}${getProgramNumber(globalIndex)}`;
  },

  parseProgramAddress(address: string): number | null {
    const match = address.match(/^([A-P])(\d+)$/i);
    if (!match) return null;
    const bankIdx = match[1].toUpperCase().charCodeAt(0) - 0x41;
    const progNum = parseInt(match[2], 10);
    if (bankIdx < 0 || bankIdx >= BANKS_COUNT) return null;
    if (progNum < 1 || progNum > PROGRAMS_PER_BANK) return null;
    return bankIdx * PROGRAMS_PER_BANK + (progNum - 1);
  },

  patchDataSize: PATCH_DATA_SIZE,
  patchNameMaxLength: PATCH_NAME_MAX_LENGTH,
  extractPatchName: () => '',

  categories: CATEGORIES,
  defaultCategory: DEFAULT_CATEGORY,

  compatibleModels: ['casio-cz1000', 'casio-cz5000', 'casio-cz1'],

  sysexManufacturerId: SYSEX_MANUFACTURER_ID,
  formatVersion: FORMAT_VERSION,
  sysexModelId: { offset: 4, values: [0x12] },
  midiDetection: { portPattern: /casio|cz.?101/i, displayName: 'Casio CZ-101' },

  midi: { defaultChannel: 1, defaultDeviceId: 0x10 },

  supportsEditBuffer: false,
  interMessageDelayMs: 30,
  dumpTimeoutMs: 3000,

  computeChecksum(data: Uint8Array): number {
    return casioChecksum(data);
  },

  verifyChecksum(sysex: Uint8Array): boolean {
    const msgs = splitSysexMessages(sysex).filter(m => isCasioSysEx(m));
    if (msgs.length === 0) return false;
    return msgs.every(m => m[m.length - 2] === czChecksumRegion(m));
  },

  buildPatchSysEx(rawData: Uint8Array, _slot: number, channel: number): Uint8Array {
    const modelId = MODEL_IDS[this.modelId] || 0x12;
    const dataSize = this.patchDataSize || PATCH_DATA_SIZE;
    const data = rawData.slice(0, dataSize);
    const padded = new Uint8Array(dataSize);
    padded.set(data);
    const nibbles = encodeNibble(padded);
    const result = new Uint8Array(7 + nibbles.length + 2);
    result[0] = 0xF0;
    result[1] = 0x44;
    result[2] = 0x00;
    result[3] = 0x00;
    result[4] = modelId;
    result[5] = CMD_DUMP;
    result[6] = channel & 0x0F;
    result.set(nibbles, 7);
    // El checksum cubre desde el byte de canal (6) hasta el último nibble.
    result[7 + nibbles.length] = casioChecksum(result.slice(6, 7 + nibbles.length));
    result[7 + nibbles.length + 1] = 0xF7;
    return result;
  },

  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    if (!isCasioSysEx(sysex)) return null;
    const info = MODEL_BY_BYTE[sysex[4]];
    const dataSize = info?.dataSize ?? PATCH_DATA_SIZE;
    const nibbles = sysex.slice(7, sysex.length - 2);
    const decoded = decodeNibble(nibbles);
    return { rawData: new Uint8Array(decoded.slice(0, dataSize)), slot: 0 };
  },

  buildDumpRequest(_slot: number | 'all', channel: number): Uint8Array {
    const modelId = MODEL_IDS[this.modelId] || 0x12;
    return new Uint8Array([0xF0, 0x44, 0x00, 0x00, modelId, CMD_REQUEST, channel & 0x0F, 0xF7]);
  },

  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    const msgs = splitSysexMessages(sysex);
    const results: { rawData: Uint8Array; slot: number }[] = [];
    for (const msg of msgs) {
      if (isCasioSysEx(msg)) {
        const info = MODEL_BY_BYTE[msg[4]];
        const dataSize = info?.dataSize ?? PATCH_DATA_SIZE;
        const nibbles = msg.slice(7, msg.length - 2);
        const decoded = decodeNibble(nibbles);
        results.push({ rawData: new Uint8Array(decoded.slice(0, dataSize)), slot: results.length });
      }
    }
    return results;
  },

  parseFile(data: Uint8Array, _filename: string): ContractFileParse | null {
    const msgs = splitSysexMessages(data).filter(m => isCasioSysEx(m));
    if (msgs.length === 0) return null;
    const firstModel = MODEL_BY_BYTE[msgs[0][4]]?.modelId;
    const parsed = msgs
      .map(m => this.parsePatchSysEx?.(m))
      .filter((p): p is { rawData: Uint8Array; slot: number } => p !== null);
    const patches = parsed.map((p, i) => ({
      name: this.extractPatchName?.(p.rawData) || this.getProgramAddress(i),
      category: this.defaultCategory,
      author: 'Unknown',
      tags: [] as string[],
      notes: '',
      originAddress: this.getProgramAddress(i),
      rawData: new Uint8Array(p.rawData),
      isFavorite: false,
      creationDate: new Date().toISOString(),
    }));
    return {
      modelId: firstModel || this.modelId,
      bankName: `Casio ${this.displayName}`,
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
    const port = ports.find(p => /casio|cz/i.test(p.name || ''));
    return port
      ? { name: port.name || 'Casio CZ', inputId: port.id || '', outputId: port.id || '', manufacturer: 'Casio', modelId: this.modelId }
      : null;
  },

  legacySysEx: {
    modelIdByte: 0x12,
    buildDumpRequest: (ch) => new Uint8Array([0xF0, 0x44, 0x00, 0x00, 0x12, 0x10, ch & 0x0F, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 8 && bytes[0] === 0xF0 && bytes[1] === 0x44 && bytes[2] === 0x00 && bytes[3] === 0x00
  }
};

export const casioCz1000Contract: ModelContract = {
  ...casioCzContract,
  modelId: 'casio-cz1000',
  compatibleModels: ['casio-cz5000', 'casio-cz1'],
  displayName: 'Casio CZ-1000',
  thumbnail: 'casio-cz1000.webp',
  legacySysEx: { ...casioCzContract.legacySysEx!, modelIdByte: 0x13 }
};

export const casioCz5000Contract: ModelContract = {
  ...casioCzContract,
  modelId: 'casio-cz5000',
  compatibleModels: ['casio-cz101', 'casio-cz1000', 'casio-cz1'],
  displayName: 'Casio CZ-5000',
  thumbnail: 'casio-cz5000.webp',
  bankCapacity: 32,
  banksCount: 2,
  legacySysEx: { ...casioCzContract.legacySysEx!, modelIdByte: 0x14 }
};

export const casioCz1Contract: ModelContract = {
  ...casioCzContract,
  modelId: 'casio-cz1',
  compatibleModels: ['casio-cz101', 'casio-cz1000', 'casio-cz5000'],
  displayName: 'Casio CZ-1',
  thumbnail: 'casio-cz1.webp',
  bankCapacity: 64,
  banksCount: 4,
  patchDataSize: 288,
  legacySysEx: { ...casioCzContract.legacySysEx!, modelIdByte: 0x15 }
};

export const allCasioContracts = [
  casioCzContract,
  casioCz1000Contract,
  casioCz5000Contract,
  casioCz1Contract
];

// Validate all
allCasioContracts.forEach(c => {
  const result = validateModelContract(c);
  if (!result.valid) {
    console.error(`❌ ${c.modelId} validation failed:`, result.errors);
  }
});

export default casioCzContract;