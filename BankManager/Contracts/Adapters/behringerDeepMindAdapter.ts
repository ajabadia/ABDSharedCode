import { BaseHardwareLink, HardwareDevice, PatchData, ImportResult } from '../HardwareLinkContract';
import type { MidiOutputPortInfo } from '../Midi';
import { pack8to7, unpack7to8, splitSysexMessages } from '../SysEx/codec';

const MANUFACTURER = [0x00, 0x20, 0x32];
const MODEL = 0x20;
const DEVICE = 0x00;
const PROGRAM_DUMP = 0x02;
const PROGRAM_REQUEST = 0x01;
const PROTOCOL = 0x07;
const SIZE = 242;
const PACKED_SIZE = 278;

function messageMatches(message: Uint8Array, command: number): boolean {
  return message.length >= 10 && message[0] === 0xF0 && message[1] === MANUFACTURER[0] && message[2] === MANUFACTURER[1] && message[3] === MANUFACTURER[2] && message[4] === MODEL && message[6] === command && message[message.length - 1] === 0xF7;
}

export class BehringerDeepMind12HardwareLink extends BaseHardwareLink {
  modelId = 'behringer-deepmind12';
  supportsEditBuffer = false;
  interMessageDelayMs = 50;
  dumpTimeoutMs = 5000;

  protected getManufacturerId(): number[] { return MANUFACTURER; }
  protected getModelId(): number { return MODEL; }

  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null {
    const output = midiOutputs.find(port => /deep.?mind|dm.?12/i.test(port.name || ''));
    return output ? { name: output.name || 'DeepMind 12', inputId: output.id || '', outputId: output.id || '', manufacturer: 'Behringer', modelId: this.modelId } : null;
  }

  buildPatchDump(patch: PatchData, slot: number, _channel: number): Uint8Array[] {
    const bank = Math.max(0, Math.min(7, Math.floor(slot / 128)));
    const program = Math.max(0, Math.min(127, slot % 128));
    const data = new Uint8Array(SIZE);
    data.set(patch.rawData.slice(0, SIZE));
    const packed = pack8to7(data);
    const padded = new Uint8Array(PACKED_SIZE);
    padded.set(packed.slice(0, PACKED_SIZE));
    return [new Uint8Array([0xF0, ...MANUFACTURER, MODEL, DEVICE, PROGRAM_DUMP, PROTOCOL, bank, program, ...padded, 0x00, 0x00, 0xF7])];
  }

  buildBankDump(patches: PatchData[], channel: number): Uint8Array[] {
    return patches.map((patch, index) => this.buildPatchDump(patch, index, channel)[0]);
  }

  buildDumpRequest(slot: number | 'all', _channel: number): Uint8Array {
    const value = slot === 'all' ? 0 : Math.max(0, Math.min(127, slot));
    const bank = slot === 'all' ? 0 : Math.floor(slot / 128);
    return new Uint8Array([0xF0, ...MANUFACTURER, MODEL, DEVICE, PROGRAM_REQUEST, bank, value, 0xF7]);
  }

  parseDumpResponse(data: Uint8Array): ImportResult {
    const patches: PatchData[] = [];
    for (const message of splitSysexMessages(data)) {
      if (!messageMatches(message, PROGRAM_DUMP) || message.length < 10 + PACKED_SIZE) continue;
      const rawData = unpack7to8(message.slice(10, 10 + PACKED_SIZE)).slice(0, SIZE);
      const slot = (message[8] & 0x07) * 128 + (message[9] & 0x7F);
      patches.push({ name: '', category: 'Other', author: '', tags: [], notes: '', originAddress: '', rawData, hardwareIds: [this.modelId], isFavorite: false, creationDate: new Date().toISOString(), slot } as PatchData & { slot: number });
    }
    return { success: patches.length > 0, modelId: this.modelId, bankName: 'DeepMind 12', patches, warnings: [] };
  }
}
