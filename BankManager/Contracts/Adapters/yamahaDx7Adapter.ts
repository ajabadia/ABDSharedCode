/**
 * Yamaha DX7 / DX7II Adapter (thin wrapper)
 *
 * All byte-level SysEx orchestration is delegated to the ModelContract
 * (yamaha-dx7.ts). This adapter only maps between contract results and
 * the adapter/HWLink interfaces.
 */

import { BaseImportAdapter, ImportResult, PatchData } from '../ImportAdapter';
import { BaseExportAdapter, ExportOptions } from '../ExportAdapter';
import { BaseHardwareLink, HardwareDevice, ImportResult as HLImportResult } from '../HardwareLinkContract';
import type { MidiOutputPortInfo } from '../Midi';
import type { ModelContract, ContractFileParse } from '../ModelContract';
import { getModelContract } from '../Models';

// ─── Constants ───

const ALL_MODEL_IDS = ['yamaha-dx7', 'yamaha-dx7ii'];

// ─── Import Adapter ───

export class YamahaDx7ImportAdapter extends BaseImportAdapter {
  adapterId = 'sysex-yamaha-dx7';
  displayName = 'Yamaha DX7 / DX7II';
  supportedExtensions = ['.syx'];
  targetModelIds = ALL_MODEL_IDS;

  private getContract(modelId?: string): ModelContract {
    return getModelContract(modelId || 'yamaha-dx7')!;
  }

  canParse(data: Uint8Array, filename: string): boolean {
    if (!filename.toLowerCase().endsWith('.syx')) return false;
    for (const id of ALL_MODEL_IDS) {
      const contract = getModelContract(id);
      if (contract?.parseFile?.(data, filename)) return true;
    }
    return false;
  }

  verifyChecksum(data: Uint8Array): boolean {
    for (const id of ALL_MODEL_IDS) {
      const contract = getModelContract(id);
      if (!contract?.verifyChecksum) continue;
      // Try each contract — if all messages verify, return true
      const chunks = splitByF7(data);
      if (chunks.length === 0) continue;
      const allValid = chunks.every(msg => contract.verifyChecksum!(msg));
      if (allValid) return true;
    }
    return false;
  }

  parse(data: Uint8Array, filename: string): ImportResult {
    for (const id of ALL_MODEL_IDS) {
      const contract = getModelContract(id);
      if (!contract?.parseFile) continue;
      const parsed = contract.parseFile(data, filename);
      if (!parsed) continue;

      const patches: PatchData[] = parsed.patches.map(p => ({
        name: p.name,
        category: p.category,
        author: p.author,
        tags: p.tags,
        notes: p.notes,
        originAddress: p.originAddress,
        rawData: p.rawData,
        hardwareIds: p.hardwareIds ?? [parsed.modelId, ...ALL_MODEL_IDS.filter(m => m !== parsed.modelId)],
        parameters: {},
        isFavorite: p.isFavorite,
        creationDate: p.creationDate,
      }));

      return this.createResult({
        success: true,
        modelId: parsed.modelId,
        bankName: parsed.bankName,
        patches,
        warnings: parsed.warnings,
      });
    }

    return this.createResult({
      success: false,
      modelId: 'yamaha-dx7',
      error: 'No se encontraron mensajes SysEx Yamaha DX7 válidos',
    });
  }
}

// ─── Export Adapter ───

export class YamahaDx7ExportAdapter extends BaseExportAdapter {
  adapterId = 'export-yamaha-dx7';
  displayName = 'Yamaha DX7 / DX7II';
  fileExtension = '.syx';
  targetModelIds = ALL_MODEL_IDS;

  serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array {
    const opts = { ...this.getDefaultOptions(), ...options };
    const contract = getModelContract('yamaha-dx7')!;

    // Single patch → single-voice SysEx; multiple patches → bank bulk dump
    const format = patches.length > 1 ? 'bank' : 'single';

    return contract.serializeFile!(
      patches.map((p, i) => ({
        rawData: p.rawData,
        slot: i,
        name: p.name,
      })),
      {
        midiChannel: (opts.midiChannel + 1) & 0x0F,  // adapter 0-based → contract 1-based
        deviceId: opts.deviceId,
        format,
      },
    );
  }
}

// ─── Hardware Link ───

export class YamahaDx7HardwareLink extends BaseHardwareLink {
  modelId = 'yamaha-dx7';
  supportsEditBuffer = false;
  interMessageDelayMs = 20;
  dumpTimeoutMs = 2000;

  protected getManufacturerId(): number[] { return [0x43]; }
  protected getModelId(): number { return 0x00; }

  private get contract(): ModelContract {
    return getModelContract(this.modelId)!;
  }

  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null {
    return this.contract.detectHardware?.(midiOutputs) ?? null;
  }

  buildPatchDump(patch: PatchData, slot: number, channel: number): Uint8Array[] {
    const sysex = this.contract.buildPatchSysEx!(patch.rawData, slot, channel + 1);
    return [sysex];
  }

  buildBankDump(patches: PatchData[], channel: number): Uint8Array[] {
    const sysex = this.contract.buildBulkSysEx!(
      patches.map((p, i) => ({ rawData: p.rawData, slot: i })),
      channel + 1,
    );
    return [sysex];
  }

  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    return this.contract.buildDumpRequest!(slot, channel + 1);
  }

  parseDumpResponse(data: Uint8Array): HLImportResult {
    const results = this.contract.parseDumpResponse!(data);
    if (results.length === 0) {
      return { success: false, modelId: this.modelId, bankName: '', patches: [], warnings: [], error: 'No valid DX7 data found' };
    }
    const patches: PatchData[] = results.map((r, i) => ({
      name: this.contract.extractPatchName?.(r.rawData) || this.contract.getProgramAddress(i),
      category: this.contract.defaultCategory,
      author: 'Unknown',
      tags: [],
      notes: '',
      originAddress: this.contract.getProgramAddress(r.slot),
      rawData: r.rawData,
      hardwareIds: [this.modelId, ...ALL_MODEL_IDS.filter(m => m !== this.modelId)],
      parameters: {},
      isFavorite: false,
      creationDate: new Date().toISOString(),
    }));
    return {
      success: true,
      modelId: this.modelId,
      bankName: this.contract.displayName,
      patches,
      warnings: [],
    };
  }
}

// ─── Utilities ───

/** Split concatenated SysEx messages on 0xF7 boundaries. */
function splitByF7(data: Uint8Array): Uint8Array[] {
  const msgs: Uint8Array[] = [];
  let start = -1;
  for (let i = 0; i < data.length; i++) {
    if (data[i] === 0xF0) start = i;
    if (data[i] === 0xF7 && start >= 0) {
      msgs.push(data.slice(start, i + 1));
      start = -1;
    }
  }
  return msgs;
}
