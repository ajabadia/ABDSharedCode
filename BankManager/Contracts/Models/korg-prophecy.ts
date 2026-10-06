/**
 * ABD Universal Bank Manager — Korg Prophecy ModelContract
 *
 * The Prophecy uses a completely different SysEx format from the MS2000:
 *   - Manufacturer: 42
 *   - Model byte: 0x41
 *   - [7 data][1 ctrl] packing (control byte AFTER each 7 data bytes)
 *   - Single dump:   F0 42 3n 41 40 01 00 [611B packed] F7   (535 raw)
 *   - Bank dump:     F0 42 3n 41 4C <bank> 00 00 00 [stream] F7
 *     (64 patches = 34240 raw → 39131 wire)
 *
 * No separate checksum — integrity via packing structure.
 */

import {
  ModelContract, validateModelContract, type ContractFileParse
} from '../ModelContract';
import {
  packProphecy7to8, unpackProphecy8to7, splitSysexMessages
} from '../SysEx/codec';

const PROPHECY_BANK_CAPACITY = 128;
const PROPHECY_BANKS_COUNT = 2;
const PROPHECY_PROGRAMS_PER_BANK = 64;
const PROPHECY_RAW_PATCH_SIZE = 535;
const PROPHECY_NAME_MAX_LENGTH = 16;
const PROPHECY_CMD_SINGLE = 0x40;
const PROPHECY_CMD_BANK   = 0x4C;
const PROPHECY_CMD_REQ    = 0x10;
const PROPHECY_CMD_ALLREQ = 0x0E;
const PROPHECY_BANK_A     = 0x10;
const PROPHECY_BANK_B     = 0x11;
const PROPHECY_SINGLE_ADDR = [0x01, 0x00];

const CATEGORIES = ['Bass', 'Lead', 'Pad', 'FX', 'Keys', 'Perc', 'Synth', 'Other'];
const DEFAULT_CATEGORY = 'Other';
const SYSEX_MANUFACTURER_ID = [0x42];
const FORMAT_VERSION = 1;

function isProphecySysEx(msg: Uint8Array): boolean {
  return msg.length >= 6
    && msg[0] === 0xF0
    && msg[1] === 0x42
    && msg[3] === 0x41
    && (msg[4] === PROPHECY_CMD_SINGLE || msg[4] === PROPHECY_CMD_BANK);
}

function getBankFromSlot(slot: number): number {
  return slot < 64 ? 0 : 1;
}

function getBankAddrByte(bank: number): number {
  return bank === 0 ? PROPHECY_BANK_A : PROPHECY_BANK_B;
}

function unpackProphecyStream(packed: Uint8Array): Uint8Array {
  return unpackProphecy8to7(packed);
}

function extractPatchNameFromRaw(raw: Uint8Array): string {
  const nameBytes = raw.slice(0, PROPHECY_NAME_MAX_LENGTH);
  return new TextDecoder().decode(nameBytes).replace(/[\0\s]+$/, '');
}

export const korgProphecyContract: ModelContract = {
  modelId: 'korg-prophecy',
  displayName: 'Korg Prophecy',
  manufacturer: 'Korg',
  icon: 'korg-logo.svg',
  thumbnail: 'korg-prophecy.webp',

  bankCapacity: PROPHECY_BANK_CAPACITY,
  banksCount: PROPHECY_BANKS_COUNT,
  programsPerBank: PROPHECY_PROGRAMS_PER_BANK,

  getProgramAddress(globalIndex: number): string {
    if (globalIndex < 0 || globalIndex >= PROPHECY_BANK_CAPACITY) return '';
    const bank = globalIndex < 64 ? 'A' : 'B';
    const num = (globalIndex % 64) + 1;
    return `${bank}${String(num).padStart(2, '0')}`;
  },

  parseProgramAddress(address: string): number | null {
    const match = address.match(/^([AB])(\d{1,2})$/i);
    if (!match) return null;
    const bank = match[1].toUpperCase();
    const num = parseInt(match[2], 10);
    if (num < 1 || num > 64) return null;
    return (bank === 'A' ? 0 : 64) + (num - 1);
  },

  patchDataSize: PROPHECY_RAW_PATCH_SIZE,
  patchNameMaxLength: PROPHECY_NAME_MAX_LENGTH,
  extractPatchName(data: Uint8Array): string {
    return extractPatchNameFromRaw(data);
  },

  categories: CATEGORIES,
  defaultCategory: DEFAULT_CATEGORY,

  compatibleModels: [],

  sysexManufacturerId: SYSEX_MANUFACTURER_ID,
  formatVersion: FORMAT_VERSION,
  sysexModelId: { offset: 3, values: [0x41] },
  midiDetection: { portPattern: /prophecy|proph/i, displayName: 'Korg Prophecy' },

  midi: {
    defaultChannel: 1,
    defaultDeviceId: 0x41
  },

  supportsEditBuffer: false,
  interMessageDelayMs: 50,
  dumpTimeoutMs: 5000,

  computeChecksum(): number { return 0; },

  verifyChecksum(sysex: Uint8Array): boolean {
    if (sysex.length < 8) return false;
    if (sysex[0] !== 0xF0 || sysex[1] !== 0x42 || sysex[3] !== 0x41) return false;
    if (sysex[sysex.length - 1] !== 0xF7) return false;
    if (sysex[4] !== PROPHECY_CMD_SINGLE && sysex[4] !== PROPHECY_CMD_BANK) return false;
    // Body starts at index 7 for single (after F0 42 3n 41 40 01 00), index 9 for bank (after F0 42 3n 41 4C addr4)
    const bodyStart = sysex[4] === PROPHECY_CMD_SINGLE ? 7 : 9;
    const body = sysex.slice(bodyStart, sysex.length - 1);
    // Single: 611 bytes (76*8+3) or 612; Bank: 39131 bytes (continuous stream 34240 raw + 4891 ctrl = 39131)
    if (sysex[4] === PROPHECY_CMD_SINGLE) {
      if (body.length !== 611 && body.length !== 612) return false;
    } else {
      if (body.length !== 39131) return false;
    }
    return body.length % 8 === 3;
  },

  buildPatchSysEx(rawData: Uint8Array, _slot: number, channel: number): Uint8Array {
    const size = this.patchDataSize;
    const data = rawData.slice(0, size);
    const padded = new Uint8Array(size);
    padded.set(data);

    const packed = packProphecy7to8(padded);
    const hdr = [0xF0, 0x42, 0x30 | (channel & 0x0F), 0x41, PROPHECY_CMD_SINGLE, ...PROPHECY_SINGLE_ADDR];
    return new Uint8Array([...hdr, ...packed, 0xF7]);
  },

  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    if (!isProphecySysEx(sysex)) return null;
    if (sysex[4] !== PROPHECY_CMD_SINGLE) return null;
    if (sysex[sysex.length - 1] !== 0xF7) return null;

    const body = sysex.slice(7, sysex.length - 1);
    const raw = unpackProphecyStream(body);
    const patch = raw.slice(0, PROPHECY_RAW_PATCH_SIZE);
    return { rawData: new Uint8Array(patch), slot: 0 };
  },

  buildBulkSysEx(patches: { rawData: Uint8Array; slot: number }[], channel: number): Uint8Array {
    const byBank = new Map<number, Uint8Array[]>();
    for (const p of patches) {
      const b = getBankFromSlot(p.slot);
      if (!byBank.has(b)) byBank.set(b, []);
      byBank.get(b)!.push(p.rawData.slice(0, PROPHECY_RAW_PATCH_SIZE));
    }

    const messages: Uint8Array[] = [];
    for (const [bank, list] of byBank) {
      const concat = new Uint8Array(list.reduce((sum, p) => sum + p.length, 0));
      let off = 0;
      for (const p of list) { concat.set(p, off); off += p.length; }
      const packed = packProphecy7to8(concat);
      const hdr = [0xF0, 0x42, 0x30 | (channel & 0x0F), 0x41, PROPHECY_CMD_BANK, getBankAddrByte(bank), 0x00, 0x00, 0x00];
      messages.push(new Uint8Array([...hdr, ...packed, 0xF7]));
    }
    if (messages.length === 1) return messages[0];
    const total = messages.reduce((sum, m) => sum + m.length, 0);
    const combined = new Uint8Array(total);
    let o = 0;
    for (const m of messages) { combined.set(m, o); o += m.length; }
    return combined;
  },

  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    const results: { rawData: Uint8Array; slot: number }[] = [];

    for (const msg of splitSysexMessages(sysex).filter(isProphecySysEx)) {
      if (msg[4] === PROPHECY_CMD_SINGLE) {
        const parsed = korgProphecyContract.parsePatchSysEx?.call(this, msg);
        if (parsed) results.push(parsed);
      } else if (msg[4] === PROPHECY_CMD_BANK) {
        const body = msg.slice(9, msg.length - 1);
        const bankAddr = msg[5];
        const baseSlot = bankAddr === PROPHECY_BANK_A ? 0 : 64;
        const raw = unpackProphecyStream(body);
        for (let i = 0; i + PROPHECY_RAW_PATCH_SIZE <= raw.length; i += PROPHECY_RAW_PATCH_SIZE) {
          const patch = raw.slice(i, i + PROPHECY_RAW_PATCH_SIZE);
          results.push({ rawData: new Uint8Array(patch), slot: baseSlot + Math.floor(i / PROPHECY_RAW_PATCH_SIZE) });
        }
      }
    }
    return results;
  },

  parseFile(data: Uint8Array, _filename: string): ContractFileParse | null {
    const parsed = splitSysexMessages(data)
      .filter(isProphecySysEx)
      .flatMap(msg => {
        if (msg[4] === PROPHECY_CMD_SINGLE) {
          const p = this.parsePatchSysEx?.(msg);
          return p ? [p] : [];
        }
        const body = msg.slice(9, msg.length - 1);
        const bankAddr = msg[5];
        const baseSlot = bankAddr === PROPHECY_BANK_A ? 0 : 64;
        const raw = unpackProphecyStream(body);
        const out: { rawData: Uint8Array; slot: number }[] = [];
        for (let i = 0; i + PROPHECY_RAW_PATCH_SIZE <= raw.length; i += PROPHECY_RAW_PATCH_SIZE) {
          out.push({ rawData: new Uint8Array(raw.slice(i, i + PROPHECY_RAW_PATCH_SIZE)), slot: baseSlot + Math.floor(i / PROPHECY_RAW_PATCH_SIZE) });
        }
        return out;
      });
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
    return this.buildBulkSysEx!(patches, options.midiChannel);
  },

  detectHardware(ports: Array<{ name?: string; id?: string }>): { name: string; inputId: string; outputId: string; manufacturer: string; modelId: string } | null {
    const port = ports.find(p => /prophecy|proph/i.test(p.name || ''));
    return port
      ? { name: port.name || 'Korg Prophecy', inputId: port.id || '', outputId: port.id || '', manufacturer: 'Korg', modelId: this.modelId }
      : null;
  },

  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    const cmd = slot === 'all' ? PROPHECY_CMD_ALLREQ : PROPHECY_CMD_REQ;
    return new Uint8Array([0xF0, 0x42, 0x30 | (channel & 0x0F), 0x41, cmd, 0xF7]);
  },

  legacySysEx: {
    modelIdByte: 0x41,
    buildDumpRequest: (ch) => new Uint8Array([0xF0, 0x42, 0x30 | (ch & 0x0F), 0x41, PROPHECY_CMD_REQ, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 6 && bytes[0] === 0xF0 && bytes[1] === 0x42 && bytes[3] === 0x41
  }
};

export const allKorgProphecyContracts = [korgProphecyContract];

allKorgProphecyContracts.forEach(c => {
  const result = validateModelContract(c);
  if (!result.valid) {
    console.error(`❌ ${c.modelId} validation failed:`, result.errors);
  }
});

export default korgProphecyContract;
