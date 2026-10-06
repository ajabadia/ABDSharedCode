/**
 * ABD Universal Bank Manager — Behringer DeepMind 12 ModelContract
 */

import { ModelContract, validateModelContract, type ContractFileParse } from '../ModelContract';
import { pack8to7Dm, unpack7to8Dm, splitSysexMessages } from '../SysEx/codec';

const DM12_PATCH_DATA_SIZE = 242;
const DM12_PATCH_NAME_MAX_LENGTH = 16;
const MODEL_ID = 0x20;
const MANUFACTURER_ID = [0x00, 0x20, 0x32];
const DEVICE_ID = 0x00;
const PROTOCOL_VERSION = 0x07;
const CMD_DUMP = 0x02;
const CMD_REQUEST = 0x01;
const PACKED_SIZE = 278;
const PROGRAMS_PER_BANK = 128;
const CATEGORIES = ['Bass', 'Lead', 'Pad', 'FX', 'Keys', 'Perc', 'Synth', 'Other'];

function isDeepMindMessage(message: Uint8Array): boolean {
  return message.length >= 13 &&
    message[0] === 0xF0 && message[1] === MANUFACTURER_ID[0] && message[2] === MANUFACTURER_ID[1] && message[3] === MANUFACTURER_ID[2] &&
    message[4] === MODEL_ID && message[6] === CMD_DUMP &&
    message[message.length - 1] === 0xF7;
}

const behringerDm12Contract: ModelContract = {
  modelId: 'behringer-deepmind12',
  displayName: 'Behringer DeepMind 12',
  manufacturer: 'Behringer',
  icon: 'behringer-logo.svg',
  thumbnail: 'behringer-deepmind12.webp',
  bankCapacity: 1024,
  banksCount: 8,
  programsPerBank: 128,
  getProgramAddress(index: number): string {
    return `${'ABCDEFGH'[Math.floor(index / 128)]}${String((index % 128) + 1).padStart(3, '0')}`;
  },
  parseProgramAddress(address: string): number | null {
    const match = address.match(/^([A-H])(\d{3})$/i);
    if (!match) return null;
    const bank = 'ABCDEFGH'.indexOf(match[1].toUpperCase());
    const program = Number(match[2]);
    return bank >= 0 && program >= 1 && program <= 128 ? bank * 128 + program - 1 : null;
  },
  patchDataSize: DM12_PATCH_DATA_SIZE,
  patchNameMaxLength: DM12_PATCH_NAME_MAX_LENGTH,
  extractPatchName(data: Uint8Array): string {
    if (data.length < 239) return '';
    return new TextDecoder().decode(data.slice(223, 239)).replace(/\0/g, '').trim();
  },
  categories: CATEGORIES,
  defaultCategory: 'Other',
  compatibleModels: [],
  sysexManufacturerId: MANUFACTURER_ID,
  formatVersion: 1,
  sysexModelId: { offset: 4, values: [0x20] },
  midiDetection: { portPattern: /deep.?mind|dm.?12/i, displayName: 'DeepMind 12' },
  parameterSchemaKey: 'behringer-deepmind12',
  midi: { defaultChannel: 1, defaultDeviceId: DEVICE_ID },
  supportsEditBuffer: false,
  interMessageDelayMs: 50,
  dumpTimeoutMs: 5000,
  computeChecksum: () => 0,
  verifyChecksum(sysex: Uint8Array): boolean {
    return isDeepMindMessage(sysex) && sysex.length === 291;
  },
  buildPatchSysEx(rawData: Uint8Array, slot: number, _channel: number): Uint8Array {
    const data = new Uint8Array(DM12_PATCH_DATA_SIZE);
    data.set(rawData.slice(0, DM12_PATCH_DATA_SIZE));
    const packed = pack8to7Dm(data);
    const padded = new Uint8Array(PACKED_SIZE);
    padded.set(packed.slice(0, PACKED_SIZE));
    const bank = Math.max(0, Math.min(7, Math.floor(slot / PROGRAMS_PER_BANK)));
    const program = Math.max(0, Math.min(127, slot % PROGRAMS_PER_BANK));
    return new Uint8Array([0xF0, ...MANUFACTURER_ID, MODEL_ID, DEVICE_ID, CMD_DUMP, PROTOCOL_VERSION, bank, program, ...padded, 0x00, 0x00, 0xF7]);
  },
  parsePatchSysEx(sysex: Uint8Array): { rawData: Uint8Array; slot: number } | null {
    if (!isDeepMindMessage(sysex)) return null;
    const slot = (sysex[8] & 0x07) * PROGRAMS_PER_BANK + (sysex[9] & 0x7F);
    return { rawData: unpack7to8Dm(sysex.slice(10, 10 + PACKED_SIZE)).slice(0, DM12_PATCH_DATA_SIZE), slot };
  },
  buildDumpRequest(slot: number | 'all', _channel: number): Uint8Array {
    const bank = slot === 'all' ? 0 : Math.max(0, Math.min(7, Math.floor(slot / PROGRAMS_PER_BANK)));
    const program = slot === 'all' ? 0 : Math.max(0, Math.min(127, slot % PROGRAMS_PER_BANK));
    return new Uint8Array([0xF0, ...MANUFACTURER_ID, MODEL_ID, DEVICE_ID, CMD_REQUEST, bank, program, 0xF7]);
  },
  parseDumpResponse(sysex: Uint8Array): { rawData: Uint8Array; slot: number }[] {
    return splitSysexMessages(sysex).flatMap(message => {
      const parsed = this.parsePatchSysEx?.(message);
      return parsed ? [parsed] : [];
    });
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
    const port = ports.find(p => /deep.?mind|dm.?12/i.test(p.name || ''));
    return port
      ? { name: port.name || 'DeepMind 12', inputId: port.id || '', outputId: port.id || '', manufacturer: 'Behringer', modelId: this.modelId }
      : null;
  },

  legacySysEx: {
    modelIdByte: MODEL_ID,
    buildDumpRequest: channel => new Uint8Array([0xF0, ...MANUFACTURER_ID, MODEL_ID, DEVICE_ID, CMD_REQUEST, 0x00, 0x00, 0xF7]),
    validateSysEx: bytes => isDeepMindMessage(bytes)
  }
};

export const allBehringerDm12Contracts = [behringerDm12Contract];
allBehringerDm12Contracts.forEach(contract => {
  const result = validateModelContract(contract);
  if (!result.valid) console.error(`❌ ${contract.modelId} validation failed:`, result.errors);
});

export default behringerDm12Contract;
