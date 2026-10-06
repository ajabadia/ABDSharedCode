/**
 * Behringer Pro-800 Adapter
 *
 * Thin wrapper that delegates byte-level SysEx orchestration to the
 * behringer-pro800 ModelContract. Handles ONLY Pro-800; DeepMind 12
 * lives in behringerDm12Adapter.ts.
 */

import { BaseImportAdapter, type ImportResult, type PatchData } from '../ImportAdapter';
import { BaseExportAdapter, type ExportOptions } from '../ExportAdapter';
import { BaseHardwareLink, type HardwareDevice, type ImportResult as HLImportResult } from '../HardwareLinkContract';
import type { MidiOutputPortInfo } from '../Midi';
import { getModelContract } from '../Models';

// ─── Contract (single source of truth) ───

const PRO800_MODEL_ID = 'behringer-pro800';
const contract = getModelContract(PRO800_MODEL_ID)!;

// ─── Helpers ───

function mapContractPatches(
  contractPatches: { name: string; category: string; author: string; tags: string[];
    notes: string; originAddress: string; rawData: Uint8Array;
    hardwareIds?: string[]; isFavorite: boolean; creationDate: string }[],
  modelId: string,
): PatchData[] {
  return contractPatches.map(p => ({
    name: p.name,
    category: p.category,
    author: p.author,
    tags: p.tags,
    notes: p.notes,
    originAddress: p.originAddress,
    rawData: p.rawData,
    parameters: {},
    hardwareIds: p.hardwareIds ?? [modelId],
    isFavorite: p.isFavorite,
    creationDate: p.creationDate,
  }));
}

// ─── Import Adapter ───

export class BehringerPro800ImportAdapter extends BaseImportAdapter {
  adapterId = 'sysex-behringer-pro800';
  displayName = 'Behringer Pro-800';
  supportedExtensions = ['.syx'];
  targetModelIds = [PRO800_MODEL_ID];

  canParse(data: Uint8Array, filename: string): boolean {
    if (!filename.toLowerCase().endsWith('.syx')) return false;
    return contract.parseFile?.(data, filename) !== null;
  }

  parse(data: Uint8Array, filename: string): ImportResult {
    const parsed = contract.parseFile?.(data, filename);
    if (!parsed) {
      return this.createResult({
        success: false,
        modelId: PRO800_MODEL_ID,
        error: 'No se encontraron mensajes SysEx Pro-800 válidos',
      });
    }
    return this.createResult({
      modelId: parsed.modelId,
      bankName: parsed.bankName,
      patches: mapContractPatches(parsed.patches, PRO800_MODEL_ID),
      warnings: parsed.warnings,
    });
  }
}

// ─── Export Adapter ───

export class BehringerPro800ExportAdapter extends BaseExportAdapter {
  adapterId = 'export-behringer-pro800';
  displayName = 'Behringer Pro-800';
  fileExtension = '.syx';
  targetModelIds = [PRO800_MODEL_ID];

  serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array {
    const patchEntries = patches.map((p, i) => ({
      rawData: p.rawData,
      slot: i,
      name: p.name,
    }));
    return contract.serializeFile!(patchEntries, {
      midiChannel: options?.midiChannel ?? 0,
      deviceId: options?.deviceId ?? 0,
      format: options?.format ?? 'bank',
    });
  }
}

// ─── Hardware Link ───

export class BehringerPro800HardwareLink extends BaseHardwareLink {
  modelId = PRO800_MODEL_ID;
  supportsEditBuffer = contract.supportsEditBuffer ?? false;
  interMessageDelayMs = contract.interMessageDelayMs ?? 50;
  dumpTimeoutMs = contract.dumpTimeoutMs ?? 5000;

  protected getManufacturerId(): number[] { return contract.sysexManufacturerId; }
  protected getModelId(): number {
    return contract.sysexModelId?.values[0] ?? 0x00;
  }

  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null {
    return contract.detectHardware?.(midiOutputs) ?? null;
  }

  buildPatchDump(patch: PatchData, slot: number, channel: number): Uint8Array[] {
    return [contract.buildPatchSysEx!(patch.rawData, slot, channel)];
  }

  buildBankDump(patches: PatchData[], channel: number): Uint8Array[] {
    return patches.flatMap((p, i) => this.buildPatchDump(p, i, channel));
  }

  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    return contract.buildDumpRequest!(slot, channel);
  }

  parseDumpResponse(data: Uint8Array): HLImportResult {
    const parsed = contract.parseDumpResponse?.(data) ?? [];
    return {
      success: parsed.length > 0,
      modelId: this.modelId,
      bankName: contract.displayName,
      patches: parsed.map(p => ({
        name: contract.extractPatchName?.(p.rawData) || '',
        category: contract.defaultCategory,
        author: 'Unknown',
        tags: [],
        notes: '',
        originAddress: contract.getProgramAddress(p.slot),
        rawData: p.rawData,
        parameters: {},
        hardwareIds: [this.modelId],
        isFavorite: false,
        creationDate: new Date().toISOString(),
      })),
      warnings: [],
    };
  }
}
