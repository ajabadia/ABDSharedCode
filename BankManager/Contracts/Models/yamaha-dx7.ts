/**
 * Yamaha DX7 / DX7II ModelContract
 * Covers: DX7, DX7II (VCED single voice, VMEM 32-voice bulk)
 *
 * SysEx formats (standard DX7 — verified against real ROM1A.syx dumps):
 *   Single voice:  F0 43 gg 09 20 00 [128B VCED] sum&0x7F F7        (136 bytes)
 *   Bulk 32 voice: F0 43 gg 09 20 00 [32×128B VCED] sum&0x7F F7     (4104 bytes)
 *   Dump request:  F0 43 gg 09 20 00 F7                               (8 bytes)
 *
 * DX7II formats (extended, model byte = 0x01):
 *   Single voice:  F0 43 10|ch 01 09 20 00 [155B VCED] sum&0x7F F7   (165 bytes)
 *   Bulk 32 voice: F0 43 10|ch 01 09 20 00 [32×155B VCED] sum&0x7F F7
 *
 * Legacy format (7-byte header, some older tools):
 *   F0 43 gg 00 09 20 0x [data] sum&0x7F F7                          (4105 bytes)
 *
 * gg device byte: 0g (group, DX7) or 10|ch (channel, DX7II/legacy)
 * Checksum: (128 - (sum of bytes[3..N-2] % 128)) & 0x7F
 */

import {
  ModelContract, validateModelContract, type ContractFileParse
} from '../ModelContract';
import { yamahaChecksum, splitSysexMessages } from '../SysEx/codec';

const DX7_PATCH_DATA_SIZE = 128;
const DX7_PATCH_NAME_MAX_LENGTH = 10;
const DX7II_PATCH_DATA_SIZE = 155;
const DX7II_PATCH_NAME_MAX_LENGTH = 10;

const CATEGORIES = ['Bass', 'Lead', 'Pad', 'FX', 'Keys', 'Perc', 'Synth', 'Other'];
const DEFAULT_CATEGORY = 'Other';
const SYSEX_MANUFACTURER_ID = [0x43];
const FORMAT_VERSION = 1;

const CMD_BULK     = 0x09;
const SUB_SINGLE   = 0x20;
const SUB_BULK     = 0x20;

// ============================================================
// DX7 Parameter validation (from NeuralDX7 constants.py)
// ============================================================

const N_OSC = 6;
const GLOBAL_VALID_RANGES: Record<string, number[]> = {
  'PR1':  Array.from({length: 100}, (_, i) => i),
  'PR2':  Array.from({length: 100}, (_, i) => i),
  'PR3':  Array.from({length: 100}, (_, i) => i),
  'PR4':  Array.from({length: 100}, (_, i) => i),
  'PL1':  Array.from({length: 100}, (_, i) => i),
  'PL2':  Array.from({length: 100}, (_, i) => i),
  'PL3':  Array.from({length: 100}, (_, i) => i),
  'PL4':  Array.from({length: 100}, (_, i) => i),
  'ALG':  Array.from({length: 32}, (_, i) => i),
  'OKS':  Array.from({length: 2}, (_, i) => i),
  'FB':   Array.from({length: 8}, (_, i) => i),
  'LFS':  Array.from({length: 100}, (_, i) => i),
  'LFD':  Array.from({length: 100}, (_, i) => i),
  'LPMD': Array.from({length: 100}, (_, i) => i),
  'LAMD': Array.from({length: 100}, (_, i) => i),
  'LPMS': Array.from({length: 8}, (_, i) => i),
  'LFW':  Array.from({length: 6}, (_, i) => i),
  'LKS':  Array.from({length: 2}, (_, i) => i),
  'TRNSP': Array.from({length: 49}, (_, i) => i),
  'NAME CHAR 1':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 2':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 3':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 4':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 5':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 6':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 7':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 8':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 9':  Array.from({length: 128}, (_, i) => i),
  'NAME CHAR 10': Array.from({length: 128}, (_, i) => i),
};

const OSCILLATOR_VALID_RANGES: Record<string, number[]> = {
  'R1':  Array.from({length: 100}, (_, i) => i),
  'R2':  Array.from({length: 100}, (_, i) => i),
  'R3':  Array.from({length: 100}, (_, i) => i),
  'R4':  Array.from({length: 100}, (_, i) => i),
  'L1':  Array.from({length: 100}, (_, i) => i),
  'L2':  Array.from({length: 100}, (_, i) => i),
  'L3':  Array.from({length: 100}, (_, i) => i),
  'L4':  Array.from({length: 100}, (_, i) => i),
  'BP':  Array.from({length: 100}, (_, i) => i),
  'LD':  Array.from({length: 100}, (_, i) => i),
  'RD':  Array.from({length: 100}, (_, i) => i),
  'RC':  Array.from({length: 4}, (_, i) => i),
  'LC':  Array.from({length: 4}, (_, i) => i),
  'DET': Array.from({length: 15}, (_, i) => i),
  'RS':  Array.from({length: 8}, (_, i) => i),
  'KVS': Array.from({length: 8}, (_, i) => i),
  'AMS': Array.from({length: 4}, (_, i) => i),
  'OL':  Array.from({length: 100}, (_, i) => i),
  'FC':  Array.from({length: 32}, (_, i) => i),
  'M':   Array.from({length: 2}, (_, i) => i),
  'FF':  Array.from({length: 100}, (_, i) => i),
};

function buildOscKeys(): string[] {
  const oscKeys = [
    'R1', 'R2', 'R3', 'R4',
    'L1', 'L2', 'L3', 'L4',
    'BP', 'LD', 'RD', 'RC', 'LC',
    'DET', 'RS', 'KVS', 'AMS', 'OL', 'FC', 'M', 'FF', 'DET', 'RS',
  ];
  const oscParams: string[] = [];
  for (let i = 0; i < 6; i++) {
    for (const key of [
      'R1', 'R2', 'R3', 'R4',
      'L1', 'L2', 'L3', 'L4',
      'BP', 'LD', 'RD', 'RC', 'LC',
      'DET', 'RS', 'KVS', 'AMS', 'OL', 'FC', 'M', 'FF'
    ]) {
      oscParams.push(`${i}_${key}`);
    }
  }
  return oscParams;
}

const OSC_KEYS = [
  'R1', 'R2', 'R3', 'R4',
  'L1', 'L2', 'L3', 'L4',
  'BP', 'LD', 'RD', 'RC', 'LC',
  'DET', 'RS', 'KVS', 'AMS', 'OL', 'FC', 'M', 'FF'
];

const GENERAL_KEYS = [
  'PR1', 'PR2', 'PR3', 'PR4',
  'PL1', 'PL2', 'PL3', 'PL4',
  'ALG', 'OKS', 'FB',
  'LFS', 'LFD', 'LPMD', 'LAMD', 'LPMS', 'LFW', 'LKS',
  'TRNSP',
  'NAME CHAR 1', 'NAME CHAR 2', 'NAME CHAR 3', 'NAME CHAR 4',
  'NAME CHAR 5', 'NAME CHAR 6', 'NAME CHAR 7', 'NAME CHAR 8',
  'NAME CHAR 9', 'NAME CHAR 10',
];

function buildVoiceKeys(): string[] {
  const oscKeys = [
    'R1', 'R2', 'R3', 'R4',
    'L1', 'L2', 'L3', 'L4',
    'BP', 'LD', 'RD', 'RC', 'LC',
    'DET', 'RS', 'KVS', 'AMS', 'OL', 'FC', 'M', 'FF'
  ];
  const oscParams: string[] = [];
  for (let i = 0; i < 6; i++) {
    for (const key of [
      'R1', 'R2', 'R3', 'R4',
      'L1', 'L2', 'L3', 'L4',
      'BP', 'LD', 'RD', 'RC', 'LC',
      'DET', 'RS', 'KVS', 'AMS', 'OL', 'FC', 'M', 'FF'
    ]) {
      oscParams.push(`${i}_${key}`);
    }
  }
  return oscParams.concat([
    'PR1', 'PR2', 'PR3', 'PR4',
    'PL1', 'PL2', 'PL3', 'PL4',
    'ALG', 'OKS', 'FB',
    'LFS', 'LFD', 'LPMD', 'LAMD', 'LPMS', 'LFW', 'LKS',
    'TRNSP',
    'NAME CHAR 1', 'NAME CHAR 2', 'NAME CHAR 3', 'NAME CHAR 4',
    'NAME CHAR 5', 'NAME CHAR 6', 'NAME CHAR 7', 'NAME CHAR 8',
    'NAME CHAR 9', 'NAME CHAR 10',
  ]);
}

const VOICE_KEYS = (() => {
  const oscKeys = [
    'R1', 'R2', 'R3', 'R4',
    'L1', 'L2', 'L3', 'L4',
    'BP', 'LD', 'RD', 'RC', 'LC',
    'DET', 'RS', 'KVS', 'AMS', 'OL', 'FC', 'M', 'FF'
  ];
  const oscParams: string[] = [];
  for (let i = 0; i < 6; i++) {
    for (const key of [
      'R1', 'R2', 'R3', 'R4',
      'L1', 'L2', 'L3', 'L4',
      'BP', 'LD', 'RD', 'RC', 'LC',
      'DET', 'RS', 'KVS', 'AMS', 'OL', 'FC', 'M', 'FF'
    ]) {
      oscParams.push(`${i}_${key}`);
    }
  }
  return oscParams.concat([
    'PR1', 'PR2', 'PR3', 'PR4',
    'PL1', 'PL2', 'PL3', 'PL4',
    'ALG', 'OKS', 'FB',
    'LFS', 'LFD', 'LPMD', 'LAMD', 'LPMS', 'LFW', 'LKS',
    'TRNSP',
    'NAME CHAR 1', 'NAME CHAR 2', 'NAME CHAR 3', 'NAME CHAR 4',
    'NAME CHAR 5', 'NAME CHAR 6', 'NAME CHAR 7', 'NAME CHAR 8',
    'NAME CHAR 9', 'NAME CHAR 10',
  ]);
})();

const VOICE_PARAMETER_RANGES: Record<string, number[]> = {
  ...Object.fromEntries(
    Object.entries(OSCILLATOR_VALID_RANGES).flatMap(([key, range]) =>
      Array.from({ length: N_OSC }, (_, i) => [`${i}_${key}`, range] as [string, number[]])
    )
  ),
  ...GLOBAL_VALID_RANGES,
};

function verifyVoice(params: Record<string, number>): boolean {
  for (const [key, value] of Object.entries(params)) {
    const range = VOICE_PARAMETER_RANGES[key];
    if (!range || !range.includes(value)) {
      console.warn(`DX7 verify failed: ${key}=${value} not in range`);
      return false;
    }
  }
  return true;
}

// DX7 checksum is the standard Yamaha checksum (see SysEx/codec).
const dx7Checksum = yamahaChecksum;

/**
 * Unpack a DX7 VMEM (128 bytes, compressed) to VCED (155 bytes, uncompressed).
 * VMEM is the format used in 32-voice bulk dumps.
 * VCED is the format used for single voice SysEx messages.
 * Reference: Dexed unpackProgram() and Yamaha DX7 MIDI SysEx spec.
 */
function unpackProgram(ved: Uint8Array, vmem: Uint8Array): void {
  const bulk = vmem;
  for (let op = 0; op < 6; op++) {
    // EG rate and level, break point, depth, scaling (11 bytes)
    for (let i = 0; i < 11; i++) {
      ved[op * 21 + i] = bulk[op * 17 + i] & 0x7F;
    }
    // Left/right curves
    const curves = bulk[op * 17 + 11] & 0x0F;
    ved[op * 21 + 11] = curves & 3;
    ved[op * 21 + 12] = (curves >> 2) & 3;
    // Detune/RS
    const detuneRs = bulk[op * 17 + 12] & 0x7F;
    ved[op * 21 + 13] = detuneRs & 7;
    // KVS/AMS
    const kvsAms = bulk[op * 17 + 13] & 0x1F;
    ved[op * 21 + 14] = kvsAms & 3;
    ved[op * 21 + 15] = (kvsAms >> 2) & 7;
    // Output level
    ved[op * 21 + 16] = bulk[op * 17 + 14] & 0x7F;
    // FCoarse/Mode
    const fcoarseMode = bulk[op * 17 + 15] & 0x3F;
    ved[op * 21 + 17] = fcoarseMode & 1;
    ved[op * 21 + 18] = (fcoarseMode >> 1) & 0x1F;
    // Fine frequency
    ved[op * 21 + 19] = bulk[op * 17 + 16] & 0x7F;
    // Detune (upper bits)
    ved[op * 21 + 20] = (detuneRs >> 3) & 0x7F;
  }
  // Pitch EG rates and levels (8 bytes)
  for (let i = 0; i < 8; i++) {
    ved[126 + i] = bulk[102 + i] & 0x7F;
  }
  // Algorithm
  ved[134] = bulk[110] & 0x1F;
  // Feedback/OscSync
  const oksFb = bulk[111] & 0x0F;
  ved[135] = oksFb & 7;
  ved[136] = oksFb >> 3;
  // LFO Speed, Delay, PMD, AMD
  ved[137] = bulk[112] & 0x7F;
  ved[138] = bulk[113] & 0x7F;
  ved[139] = bulk[114] & 0x7F;
  ved[140] = bulk[115] & 0x7F;
  // LFO Waveform/PMS/LKS
  const lpmsLfwLks = bulk[116] & 0x7F;
  ved[141] = lpmsLfwLks & 1;
  ved[142] = (lpmsLfwLks >> 1) & 7;
  ved[143] = lpmsLfwLks >> 4;
  // Transpose
  ved[144] = bulk[117] & 0x7F;
  // Name (10 bytes)
  for (let i = 0; i < 10; i++) {
    ved[145 + i] = bulk[118 + i] & 0x7F;
  }
}

/**
 * Pack a DX7 VCED (155 bytes, uncompressed) to VMEM (128 bytes, compressed).
 * Reverse of unpackProgram().
 */
function packProgram(vmem: Uint8Array, ved: Uint8Array): void {
  for (let op = 0; op < 6; op++) {
    // EG rate and level, break point, depth, scaling (11 bytes)
    for (let i = 0; i < 11; i++) {
      vmem[op * 17 + i] = ved[op * 21 + i] & 0x7F;
    }
    // Left/right curves
    vmem[op * 17 + 11] = (ved[op * 21 + 11] & 0x03) | ((ved[op * 21 + 12] & 0x03) << 2);
    // Detune/RS
    vmem[op * 17 + 12] = (ved[op * 21 + 13] & 0x07) | ((ved[op * 21 + 20] & 0x7F) << 3);
    // KVS/AMS
    vmem[op * 17 + 13] = (ved[op * 21 + 14] & 0x03) | ((ved[op * 21 + 15] & 0x07) << 2);
    // Output level
    vmem[op * 17 + 14] = ved[op * 21 + 16] & 0x7F;
    // FCoarse/Mode
    vmem[op * 17 + 15] = (ved[op * 21 + 17] & 0x01) | ((ved[op * 21 + 18] & 0x1F) << 1);
    // Fine frequency
    vmem[op * 17 + 16] = ved[op * 21 + 19] & 0x7F;
  }
  // Pitch EG rates and levels (8 bytes)
  for (let i = 0; i < 8; i++) {
    vmem[102 + i] = ved[126 + i] & 0x7F;
  }
  // Algorithm
  vmem[110] = ved[134] & 0x1F;
  // Feedback/OscSync
  vmem[111] = (ved[135] & 0x07) | ((ved[136] & 0x01) << 3);
  // LFO Speed, Delay, PMD, AMD
  vmem[112] = ved[137] & 0x7F;
  vmem[113] = ved[138] & 0x7F;
  vmem[114] = ved[139] & 0x7F;
  vmem[115] = ved[140] & 0x7F;
  // LFO Waveform/PMS/LKS
  vmem[116] = (ved[141] & 0x01) | ((ved[142] & 0x07) << 1) | ((ved[143] & 0x07) << 4);
  // Transpose
  vmem[117] = ved[144] & 0x7F;
  // Name (10 bytes)
  for (let i = 0; i < 10; i++) {
    vmem[118 + i] = ved[145 + i] & 0x7F;
  }
}

/**
 * Build a standard DX7 single voice SysEx (VCED format, 163 bytes).
 * Header: F0 43 gg 00 01 1B  [155B VCED]  checksum  F7
 * Checksum covers: VCED data bytes only (standard DX7 spec).
 */
function buildDx7VoiceSysEx(ved: Uint8Array, channel: number): Uint8Array {
  // MIDI channel 1-16 → SysEx byte 0-15 (0-based)
  const ch = (channel - 1) & 0x0F;
  const checksum = dx7Checksum(ved.subarray(0, 155));
  const result = new Uint8Array(6 + 155 + 2); // 163 bytes
  result.set([0xF0, 0x43, ch, 0x00, 0x01, 0x1B], 0);
  result.set(ved.subarray(0, 155), 6);
  result[6 + 155] = checksum;
  result[6 + 155 + 1] = 0xF7;
  return result;
}

/**
 * Header length helpers: detect whether a SysEx message uses
 * the standard 6-byte DX7 header or the legacy 7-byte header.
 */
function dx7HeaderLen(msg: Uint8Array): number {
  // Standard DX7:  F0 43 gg 09 20 00  [data...]  chk F7   (6-byte header)
  // Legacy format: F0 43 gg 00 09 20 xx  [data...] chk F7 (7-byte header)
  // Check if bytes[3..5] match the standard address 09 20 00
  if (msg.length >= 8 && msg[3] === CMD_BULK && msg[4] === SUB_SINGLE && msg[5] === 0x00) return 6;
  // Legacy: bytes[4..6] match 09 20 0x
  if (msg.length >= 9 && msg[4] === CMD_BULK && msg[5] === SUB_SINGLE) return 7;
  return 0; // unknown
}

function isDx7Voice(msg: Uint8Array, modelByte: number): boolean {
  if (msg[0] !== 0xF0 || msg[1] !== 0x43 || msg[msg.length - 1] !== 0xF7) return false;
  // VCED single voice: F0 43 gg 00 01 1B [155B] chk F7 = 163 bytes
  if (msg.length === 163 && msg[3] === 0x00 && msg[4] === 0x01 && msg[5] === 0x1B) return true;
  const hdr = dx7HeaderLen(msg);
  if (hdr === 6) {
    // Standard 6-byte: F0 43 gg 09 20 00 [128B] chk F7 = 6+128+2 = 136
    return msg.length === hdr + DX7_PATCH_DATA_SIZE + 2;
  }
  if (hdr === 7) {
    // Legacy 7-byte: F0 43 gg 00 09 20 00 [128B] chk F7 = 7+128+2 = 137
    return msg.length === hdr + DX7_PATCH_DATA_SIZE + 2 && msg[3] === modelByte;
  }
  return false;
}

function isDx7Bulk(msg: Uint8Array, modelByte: number): boolean {
  if (msg[0] !== 0xF0 || msg[1] !== 0x43 || msg[msg.length - 1] !== 0xF7) return false;
  const hdr = dx7HeaderLen(msg);
  if (hdr === 6) {
    // Standard 6-byte bulk: F0 43 gg 09 20 00 [4096B] chk F7 = 6+4096+2 = 4104
    return msg.length === hdr + 32 * DX7_PATCH_DATA_SIZE + 2;
  }
  if (hdr === 7) {
    // Legacy 7-byte bulk: F0 43 gg 00 09 20 01 [4096B] chk F7 = 7+4096+2 = 4105
    return msg.length === hdr + 32 * DX7_PATCH_DATA_SIZE + 2 && msg[3] === modelByte;
  }
  return false;
}

function getDx7ProgramNumber(index: number): number {
  return (index % 32) + 1;
}

function getDx7iiProgramNumber(index: number): number {
  return (index % 64) + 1;
}

const yamahaDx7Contract: ModelContract = {
  modelId: 'yamaha-dx7',
  displayName: 'Yamaha DX7',
  manufacturer: 'Yamaha',
  icon: 'yamaha-logo.svg',
  thumbnail: 'yamaha-dx7.webp',

  bankCapacity: 32,
  banksCount: 1,
  programsPerBank: 32,

  getProgramAddress(globalIndex: number): string {
    return `V${String(getDx7ProgramNumber(globalIndex)).padStart(2, '0')}`;
  },

  parseProgramAddress(address: string): number | null {
    const match = address.match(/^V(\d{2})$/i);
    if (!match) return null;
    const prog = parseInt(match[1], 10);
    if (prog < 1 || prog > 32) return null;
    return prog - 1;
  },

  patchDataSize: DX7_PATCH_DATA_SIZE,
  patchNameMaxLength: DX7_PATCH_NAME_MAX_LENGTH,
  extractPatchName(data: Uint8Array): string {
    // DX7 voice name is 10 bytes at offset 118 in the VCED data (ASCII)
    if (data.length < 128) return '';
    const nameBytes = data.slice(118, 128);
    let name = '';
    for (const b of nameBytes) {
      if (b === 0x00) break;
      if (b >= 0x20 && b <= 0x7E) name += String.fromCharCode(b);
    }
    return name.trimEnd();
  },

  categories: CATEGORIES,
  defaultCategory: DEFAULT_CATEGORY,

  compatibleModels: ['yamaha-dx7ii'],

  sysexManufacturerId: SYSEX_MANUFACTURER_ID,
  formatVersion: FORMAT_VERSION,

  // DX7 uses byte[3] = device byte (0x00=DX7, 0x01=DX7II) for disambiguation
  sysexModelId: { offset: 3, values: [0x00] },
  midiDetection: { portPattern: /dx.?7|fm.?1|m.?wave|cuvave/i, displayName: 'DX7' },
  parameterSchemaKey: 'yamaha-dx7',

  midi: { defaultChannel: 1, defaultDeviceId: 0x10 },

  supportsEditBuffer: false,
  interMessageDelayMs: 50,
  dumpTimeoutMs: 5000,
  maxSysExMessageSize: 0, // DX7 bulk dumps must NOT be split — FM-1 expects single 4104-byte message

  computeChecksum(data: Uint8Array): number {
    return dx7Checksum(data);
  },

  verifyChecksum(sysex: Uint8Array): boolean {
    if (sysex.length < 8) return false;
    if (sysex[0] !== 0xF0 || sysex[1] !== 0x43) return false;
    if (sysex[sysex.length - 1] !== 0xF7) return false;
    // VCED single voice: 163 bytes, header F0 43 gg 00 01 1B
    if (sysex.length === 163 && sysex[3] === 0x00 && sysex[4] === 0x01 && sysex[5] === 0x1B) {
      const payload = sysex.slice(6, sysex.length - 2);
      return sysex[sysex.length - 2] === dx7Checksum(payload);
    }
    // VMEM format: checksum covers bytes after the header until checksum byte
    const hdr = dx7HeaderLen(sysex);
    if (hdr === 0) return false;
    const payload = sysex.slice(hdr, sysex.length - 2);
    return sysex[sysex.length - 2] === dx7Checksum(payload);
  },

  buildPatchSysEx(rawData: Uint8Array, _slot: number, channel: number): Uint8Array {
    // rawData is VMEM (128 bytes). Unpack to VCED (155 bytes) for single voice SysEx.
    const ved = new Uint8Array(155);
    const paddedVmem = new Uint8Array(DX7_PATCH_DATA_SIZE);
    paddedVmem.set(rawData.slice(0, DX7_PATCH_DATA_SIZE));
    unpackProgram(ved, paddedVmem);
    return buildDx7VoiceSysEx(ved, channel);
  },

  buildBulkSysEx(patches: { rawData: Uint8Array; slot: number }[], channel: number): Uint8Array {
    // Standard DX7 bulk dump: F0 43 gg 09 20 00 [32×128B VMEM] checksum F7 = 4104 bytes
    // Patches must be in VMEM format (128 bytes each)
    // Checksum covers: VMEM data bytes only (standard DX7 spec)
    const ch = (channel - 1) & 0x0F;
    const header = new Uint8Array([0xF0, 0x43, ch, CMD_BULK, SUB_SINGLE, 0x00]);
    const bankSize = 32 * DX7_PATCH_DATA_SIZE; // 4096 bytes
    const result = new Uint8Array(header.length + bankSize + 2); // 4104 bytes
    result.set(header, 0);
    for (const p of patches) {
      const offset = header.length + (p.slot * DX7_PATCH_DATA_SIZE);
      const data = p.rawData.slice(0, DX7_PATCH_DATA_SIZE);
      result.set(data, offset);
    }
    // Checksum covers data after the 6-byte header
    const checksum = dx7Checksum(result.slice(header.length, header.length + bankSize));
    result[header.length + bankSize] = checksum;
    result[result.length - 1] = 0xF7;
    return result;
  },

  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    if (!isDx7Voice(sysex, 0x00)) return null;
    // VCED single voice (163 bytes): F0 43 gg 00 01 1B [155B] chk F7
    if (sysex.length === 163 && sysex[3] === 0x00 && sysex[4] === 0x01 && sysex[5] === 0x1B) {
      const ved = new Uint8Array(sysex.slice(6, 6 + 155));
      const vmem = new Uint8Array(DX7_PATCH_DATA_SIZE);
      packProgram(vmem, ved);
      return { rawData: vmem, slot: 0 };
    }
    // VMEM single voice (136 bytes): F0 43 gg 09 20 00 [128B] chk F7
    const hdr = dx7HeaderLen(sysex);
    return { rawData: new Uint8Array(sysex.slice(hdr, hdr + DX7_PATCH_DATA_SIZE)), slot: 0 };
  },

  buildDumpRequest(_slot: number | 'all', channel: number): Uint8Array {
    // Standard DX7 dump request: F0 43 gg 09 20 00 F7 (8 bytes)
    const ch = (channel - 1) & 0x0F;
    return new Uint8Array([0xF0, 0x43, ch, CMD_BULK, SUB_SINGLE, 0x00, 0xF7]);
  },

  // ─── Validation (from NeuralDX7 constants.py) ───
  verifyVoice(params: Record<string, number>): boolean {
    return verifyVoice(params);
  },

  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    const msgs = splitSysexMessages(sysex);
    const results: { rawData: Uint8Array; slot: number }[] = [];

    for (const msg of msgs) {
      const hdr = dx7HeaderLen(msg);
      if (hdr === 0) continue;

      if (isDx7Voice(msg, 0x00)) {
        results.push({ rawData: new Uint8Array(msg.slice(hdr, hdr + DX7_PATCH_DATA_SIZE)), slot: results.length });
      } else if (isDx7Bulk(msg, 0x00)) {
        const patchData = msg.slice(hdr, hdr + 32 * DX7_PATCH_DATA_SIZE);
        for (let i = 0; i < 32; i++) {
          const s = i * DX7_PATCH_DATA_SIZE;
          results.push({ rawData: new Uint8Array(patchData.slice(s, s + DX7_PATCH_DATA_SIZE)), slot: i });
        }
      }
    }
    return results;
  },

  parseFile(data: Uint8Array, _filename: string): ContractFileParse | null {
    const parsed = splitSysexMessages(data).flatMap(msg => {
      const hdr = dx7HeaderLen(msg);
      if (hdr === 0) return [];
      if (isDx7Voice(msg, 0x00)) {
        return [{ rawData: msg.slice(hdr, hdr + DX7_PATCH_DATA_SIZE), slot: 0 }];
      }
      if (isDx7Bulk(msg, 0x00)) {
        const patchData = msg.slice(hdr, hdr + 32 * DX7_PATCH_DATA_SIZE);
        const out: { rawData: Uint8Array; slot: number }[] = [];
        for (let i = 0; i < 32; i++) {
          const s = i * DX7_PATCH_DATA_SIZE;
          out.push({ rawData: new Uint8Array(patchData.slice(s, s + DX7_PATCH_DATA_SIZE)), slot: i });
        }
        return out;
      }
      return [];
    });
    if (parsed.length === 0) return null;
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
      modelId: this.modelId,
      bankName: `Yamaha ${this.displayName}`,
      patches,
      warnings: [],
    };
  },

  serializeFile(patches: { rawData: Uint8Array; slot: number; name?: string }[], options: { midiChannel: number; deviceId: number; format: 'single' | 'bank' }): Uint8Array {
    if (options.format === 'bank' && patches.length > 0) {
      return this.buildBulkSysEx!(patches, options.midiChannel);
    }
    const msgs = patches.map(p => this.buildPatchSysEx?.(p.rawData, p.slot, options.midiChannel) ?? new Uint8Array());
    if (msgs.length === 1) return msgs[0];
    const total = msgs.reduce((n, m) => n + m.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const m of msgs) { out.set(m, off); off += m.length; }
    return out;
  },

  detectHardware(ports: Array<{ name?: string; id?: string }>): { name: string; inputId: string; outputId: string; manufacturer: string; modelId: string } | null {
    const port = ports.find(p => /dx.?7|fm.?1|m.?wave|cuvave/i.test(p.name || ''));
    return port
      ? { name: port.name || 'Yamaha DX7', inputId: port.id || '', outputId: port.id || '', manufacturer: 'Yamaha', modelId: this.modelId }
      : null;
  },

  legacySysEx: {
    modelIdByte: 0x00,
    buildDumpRequest: (ch) => new Uint8Array([0xF0, 0x43, (ch - 1) & 0x0F, 0x00, CMD_BULK, SUB_SINGLE, 0x00, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 6 && bytes[0] === 0xF0 && bytes[1] === 0x43 && bytes[3] === 0x00
  }
};

export const yamahaDx7iiContract: ModelContract = {
  ...yamahaDx7Contract,
  modelId: 'yamaha-dx7ii',
  displayName: 'Yamaha DX7II',
  thumbnail: 'yamaha-dx7.webp',
  sysexModelId: { offset: 3, values: [0x01] },
  midiDetection: { portPattern: /dx.?7ii|dx7.?ii/i, displayName: 'DX7II' },
  parameterSchemaKey: 'yamaha-dx7ii',
  bankCapacity: 64,
  programsPerBank: 64,
  patchDataSize: DX7II_PATCH_DATA_SIZE,
  patchNameMaxLength: DX7II_PATCH_NAME_MAX_LENGTH,

  getProgramAddress(globalIndex: number): string {
    return `V${String(getDx7iiProgramNumber(globalIndex)).padStart(2, '0')}`;
  },

  parseProgramAddress(address: string): number | null {
    const match = address.match(/^V(\d{2})$/i);
    if (!match) return null;
    const prog = parseInt(match[1], 10);
    if (prog < 1 || prog > 64) return null;
    return prog - 1;
  },

  extractPatchName(data: Uint8Array): string {
    // DX7II voice name is 10 bytes at offset 118 in the VCED data (ASCII)
    if (data.length < 128) return '';
    const nameBytes = data.slice(118, 128);
    let name = '';
    for (const b of nameBytes) {
      if (b === 0x00) break;
      if (b >= 0x20 && b <= 0x7E) name += String.fromCharCode(b);
    }
    return name.trimEnd();
  },

  buildPatchSysEx(rawData: Uint8Array, _slot: number, channel: number): Uint8Array {
    const data = rawData.slice(0, DX7II_PATCH_DATA_SIZE);
    const padded = new Uint8Array(DX7II_PATCH_DATA_SIZE);
    padded.set(data);
    const ch = (channel - 1) & 0x0F;
    const header = new Uint8Array([0xF0, 0x43, ch, 0x01, CMD_BULK, SUB_SINGLE, 0x00]);
    const payload = new Uint8Array(header.length + DX7II_PATCH_DATA_SIZE);
    payload.set(header, 0);
    payload.set(padded, header.length);
    // Checksum covers data after the 7-byte header (DX7II format)
    const checksum = dx7Checksum(payload.slice(7));
    const result = new Uint8Array(payload.length + 2);
    result.set(payload, 0);
    result[payload.length] = checksum;
    result[result.length - 1] = 0xF7;
    return result;
  },

  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    if (sysex.length !== 7 + DX7II_PATCH_DATA_SIZE + 2) return null;
    if (sysex[0] !== 0xF0 || sysex[1] !== 0x43) return null;
    if (sysex[3] !== 0x01) return null;
    if (sysex[4] !== CMD_BULK || sysex[5] !== SUB_SINGLE || sysex[6] !== 0x00) return null;
    if (sysex[sysex.length - 1] !== 0xF7) return null;
    // Checksum covers data after the 7-byte header
    const payload = sysex.slice(7, sysex.length - 2);
    if (sysex[sysex.length - 2] !== dx7Checksum(payload)) return null;
    return { rawData: new Uint8Array(sysex.slice(7, 7 + DX7II_PATCH_DATA_SIZE)), slot: 0 };
  },

  legacySysEx: {
    modelIdByte: 0x01,
    buildDumpRequest: (ch) => new Uint8Array([0xF0, 0x43, (ch - 1) & 0x0F, 0x01, CMD_BULK, SUB_SINGLE, 0x00, 0xF7]),
    validateSysEx: (bytes) => bytes.length >= 6 && bytes[0] === 0xF0 && bytes[1] === 0x43 && bytes[3] === 0x01
  }
};

export const allYamahaContracts = [
  yamahaDx7Contract,
  yamahaDx7iiContract
];

allYamahaContracts.forEach(c => {
  const result = validateModelContract(c);
  if (!result.valid) {
    console.error(`❌ ${c.modelId} validation failed:`, result.errors);
  }
});

// Export validation utilities for debugging
export {
  VOICE_KEYS,
  VOICE_PARAMETER_RANGES,
  verifyVoice,
  dx7Checksum,
};

export default yamahaDx7Contract;