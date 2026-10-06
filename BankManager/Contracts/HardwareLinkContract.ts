/**
 * ABD Bank Manager — HardwareLinkContract Interface
 * Bidirectional MIDI SysEx communication with hardware synthesizers
 */

import type { PatchData } from './PatchData.ts';
import type { MidiOutputPortInfo } from './Midi.ts';

// Re-export so existing `import { PatchData } from '../HardwareLinkContract'` keeps working
export type { PatchData } from './PatchData.ts';

export interface HardwareDevice {
  name: string;
  inputId: string;
  outputId: string;
  manufacturer: string;
  modelId: string;
}

export interface HardwareLinkContract {
  modelId: string;

  // ─── Discovery ───
  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null;

  // ─── Dump TO Synth ───
  buildPatchDump(patch: PatchData, slot: number, channel: number): Uint8Array[];
  buildBankDump(patches: PatchData[], channel: number): Uint8Array[];

  // ─── Fetch FROM Synth ───
  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array;
  parseDumpResponse(data: Uint8Array): ImportResult;

  // ─── Edit Buffer (audition without overwrite) ───
  supportsEditBuffer: boolean;
  buildEditBufferDump?(patch: PatchData, channel: number): Uint8Array[];

  // ─── Timing ───
  interMessageDelayMs: number;
  dumpTimeoutMs: number;
}

export interface ImportResult {
  success: boolean;
  modelId: string;
  bankName: string;
  patches: PatchData[];
  warnings: string[];
  error?: string;
}

/**
 * Base class with common MIDI utilities
 */
export abstract class BaseHardwareLink implements HardwareLinkContract {
  abstract modelId: string;
  abstract buildPatchDump(patch: PatchData, slot: number, channel: number): Uint8Array[];
  abstract buildBankDump(patches: PatchData[], channel: number): Uint8Array[];
  abstract buildDumpRequest(slot: number | 'all', channel: number): Uint8Array;
  abstract parseDumpResponse(data: Uint8Array): ImportResult;
  abstract supportsEditBuffer: boolean;
  abstract interMessageDelayMs: number;
  abstract dumpTimeoutMs: number;

  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null {
    // Override in subclasses for specific detection logic
    return null;
  }

  buildEditBufferDump?(patch: PatchData, channel: number): Uint8Array[] {
    return this.buildPatchDump(patch, 0x7F, channel); // 0x7F = edit buffer slot
  }

  protected createSysexHeader(command: number, channel: number): number[] {
    return [0xF0, ...this.getManufacturerId(), channel & 0x0F, this.getModelId(), command];
  }

  protected abstract getManufacturerId(): number[];
  protected abstract getModelId(): number;

  protected addChecksum(bytes: number[]): number[] {
    // Default: no checksum (Korg style). Override for Roland XOR, Yamaha sum & 0x7F
    return bytes;
  }

  protected finalizeSysex(bytes: number[]): Uint8Array {
    return new Uint8Array([...bytes, 0xF7]);
  }
}