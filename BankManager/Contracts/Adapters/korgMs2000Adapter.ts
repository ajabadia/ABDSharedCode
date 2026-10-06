/**
 * Korg MS2000 / microKORG / Prophecy Adapter
 *
 * Thin wrapper that delegates all SysEx byte orchestration to the
 * corresponding ModelContract (korg-ms2000.ts), which owns:
 *   parseFile, serializeFile, buildPatchSysEx, buildDumpRequest,
 *   parseDumpResponse, detectHardware, verifyChecksum, extractPatchName,
 *   getProgramAddress, and all byte-level constants.
 *
 * SysEx format (handled by the contract):
 *   Single dump:  F0 42 <3n> <modelId> 40 [7-to-8 packed data] F7
 *   Dump request: F0 42 <3n> <modelId> 10 F7
 *
 * 7-to-8 packing: every 7 input bytes → 1 control + 7 encoded bytes.
 * No checksum (integrity via packing structure).
 *
 * The adapter keeps only target-model orchestration the contract does not own:
 *  - Filename-based disambiguation between MS2000 and microKORG (they share
 *    model byte 0x58, so the wire format alone cannot tell them apart).
 *  - Per-index bank addressing (A.01..A.16…) assigned from patch position.
 *  - Legacy Prophecy export (model byte 0x5A) — the modern 0x41 format lives
 *    in its own contract (korg-prophecy.ts); this adapter keeps the legacy
 *    wire form for MS2000-family files.
 */

import { BaseImportAdapter, ImportResult, PatchData } from '../ImportAdapter';
import { BaseExportAdapter, ExportOptions } from '../ExportAdapter';
import { BaseHardwareLink, HardwareDevice, ImportResult as HLImportResult } from '../HardwareLinkContract';
import type { MidiOutputPortInfo } from '../Midi';
import { getModelContract } from '../Models';
import type { ContractFileParse } from '../ModelContract';

// ─── Constants ───

const ALL_MODEL_IDS = ['korg-ms2000', 'korg-microkorg', 'korg-prophecy'] as const;
const ALL_MODEL_IDS_MUTABLE: string[] = [...ALL_MODEL_IDS];

// ─── Import Adapter ───

export class KorgMs2000ImportAdapter extends BaseImportAdapter {
  adapterId = 'sysex-korg-ms2000';
  displayName = 'Korg MS2000 / microKORG / Prophecy';
  supportedExtensions = ['.syx'];
  targetModelIds = ALL_MODEL_IDS_MUTABLE;

  canParse(data: Uint8Array, filename: string): boolean {
    if (!filename.toLowerCase().endsWith('.syx')) return false;
    const contract = getModelContract('korg-ms2000');
    if (contract?.parseFile) {
      return contract.parseFile(data, filename) != null;
    }
    return false;
  }

  verifyChecksum(data: Uint8Array): boolean {
    const contract = getModelContract('korg-ms2000');
    return contract?.verifyChecksum ? contract.verifyChecksum(data) : false;
  }

  parse(data: Uint8Array, filename: string): ImportResult {
    const contract = getModelContract('korg-ms2000');
    if (!contract?.parseFile) {
      return this.createResult({
        success: false,
        modelId: 'korg-ms2000',
        error: 'No se encontraron mensajes SysEx Korg MS2000 válidos',
      });
    }

    const result: ContractFileParse | null = contract.parseFile(data, filename);
    if (result == null) {
      return this.createResult({
        success: false,
        modelId: 'korg-ms2000',
        error: 'No se encontraron mensajes SysEx Korg MS2000 válidos',
      });
    }

    // MS2000 and microKORG share model byte 0x58, so disambiguate by filename.
    let modelId = 'korg-ms2000';
    const fn = filename.toLowerCase();
    if (fn.includes('microkorg')) modelId = 'korg-microkorg';

    const patches: PatchData[] = result.patches.map((p, i) => ({
      name: p.name,
      category: p.category,
      author: p.author,
      tags: p.tags,
      notes: p.notes,
      originAddress: contract.getProgramAddress(i),
      rawData: new Uint8Array(p.rawData),
      hardwareIds: [modelId, ...ALL_MODEL_IDS_MUTABLE.filter(id => id !== modelId)],
      parameters: {},
      isFavorite: p.isFavorite,
      creationDate: p.creationDate,
    }));

    return this.createResult({
      modelId,
      bankName: `Korg ${modelId.replace('korg-', '').toUpperCase()}`,
      patches,
      warnings: result.warnings,
    });
  }
}

// ─── Export Adapter ───

export class KorgMs2000ExportAdapter extends BaseExportAdapter {
  adapterId = 'export-korg-ms2000';
  displayName = 'Korg MS2000 / microKORG / Prophecy';
  fileExtension = '.syx';
  targetModelIds = ALL_MODEL_IDS_MUTABLE;

  serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array {
    const opts = { ...this.getDefaultOptions(), ...options };
    const contract = getModelContract('korg-ms2000');
    if (!contract?.buildPatchSysEx) {
      throw new Error('Korg MS2000 contract not found or missing buildPatchSysEx');
    }

    const msgs: Uint8Array[] = [];
    for (let i = 0; i < patches.length; i++) {
      const p = patches[i];
      const patchModelId = p.hardwareIds?.find(id => ALL_MODEL_IDS_MUTABLE.includes(id)) || 'korg-ms2000';

      if (patchModelId === 'korg-prophecy') {
        // Legacy Prophecy export (model byte 0x5A). Delegate the byte-level
        // build to the contract, then retag the model byte for the legacy
        // MS2000-family wire form.
        const msg = contract.buildPatchSysEx(p.rawData, i, opts.midiChannel & 0x0F);
        const legacy = new Uint8Array(msg);
        legacy[3] = 0x5A;
        msgs.push(legacy);
      } else {
        // MS2000 / microKORG both serialize through the canonical 0x58 contract.
        msgs.push(contract.buildPatchSysEx(p.rawData, i, opts.midiChannel));
      }
    }

    const total = msgs.reduce((n, m) => n + m.length, 0);
    const out = new Uint8Array(total);
    let off = 0;
    for (const m of msgs) { out.set(m, off); off += m.length; }
    return out;
  }
}

// ─── Hardware Link ───

export class KorgMs2000HardwareLink extends BaseHardwareLink {
  modelId = 'korg-ms2000';
  supportsEditBuffer = true;
  interMessageDelayMs = 20;
  dumpTimeoutMs = 2000;

  protected getManufacturerId(): number[] { return [0x42]; }
  protected getModelId(): number {
    const contract = getModelContract(this.modelId);
    return contract?.legacySysEx?.modelIdByte ?? 0x58;
  }

  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null {
    const contract = getModelContract(this.modelId);
    if (contract?.detectHardware) {
      return contract.detectHardware(midiOutputs);
    }
    return null;
  }

  buildPatchDump(patch: PatchData, slot: number, channel: number): Uint8Array[] {
    const contract = getModelContract(this.modelId);
    if (contract?.buildPatchSysEx) {
      return [contract.buildPatchSysEx(patch.rawData, slot, channel)];
    }
    return [];
  }

  buildBankDump(patches: PatchData[], channel: number): Uint8Array[] {
    const contract = getModelContract(this.modelId);
    const { buildPatchSysEx } = contract ?? {};
    if (!buildPatchSysEx) return [];
    return patches.map((p, i) => buildPatchSysEx(p.rawData, i, channel));
  }

  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    const contract = getModelContract(this.modelId);
    if (contract?.buildDumpRequest) {
      return contract.buildDumpRequest(slot, channel);
    }
    return new Uint8Array();
  }

  parseDumpResponse(data: Uint8Array): HLImportResult {
    const contract = getModelContract(this.modelId);
    if (contract?.parseDumpResponse) {
      const parsed = contract.parseDumpResponse(data);
      const patches: PatchData[] = parsed.map((p, i) => ({
        name: contract.extractPatchName?.(p.rawData) || contract.getProgramAddress?.(i) || `Slot ${p.slot}`,
        category: contract.defaultCategory,
        author: 'Unknown',
        tags: [],
        notes: '',
        originAddress: contract.getProgramAddress?.(i) ?? `Slot ${p.slot}`,
        rawData: new Uint8Array(p.rawData),
        parameters: {},
        isFavorite: false,
        creationDate: new Date().toISOString(),
      }));
      return {
        success: patches.length > 0,
        modelId: this.modelId,
        bankName: 'Korg MS2000',
        patches,
        warnings: [],
      };
    }
    return { success: false, modelId: this.modelId, bankName: '', patches: [], warnings: [], error: 'Contract parseDumpResponse not available' };
  }
}
