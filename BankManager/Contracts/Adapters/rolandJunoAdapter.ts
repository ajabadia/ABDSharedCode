/**
 * Roland Juno-106 / Juno-60 / Juno-6 / HS-60 Adapter
 *
 * Thin wrapper that delegates all SysEx byte orchestration to the
 * corresponding ModelContract (roland-juno.ts), which owns:
 *   parseFile, serializeFile, buildPatchSysEx, buildBulkSysEx,
 *   buildDumpRequest, parseDumpResponse, detectHardware,
 *   verifyChecksum, computeChecksum, and all byte-level constants.
 *
 * SysEx formats (from ABDJUNiO601 reference, handled by the contract):
 *   Single patch:  F0 41 30 ch [18 bytes data] F7              (no checksum)
 *   Bulk dump:     F0 41 30 02 01 [64×18B data] <(-sum)&0x7F> F7
 *   Request:       F0 41 ch <modelId> 11 00 F7
 *   Param change:  F0 41 32 ch paramId value F7
 */

import { BaseImportAdapter, ImportResult, PatchData } from '../ImportAdapter';
import { BaseExportAdapter, ExportOptions } from '../ExportAdapter';
import { BaseHardwareLink, HardwareDevice, ImportResult as HLImportResult } from '../HardwareLinkContract';
import { getModelContract } from '../Models';
import type { ModelContract, ContractFileParse } from '../ModelContract';

// ─── Constants ───

const ALL_MODEL_IDS = ['roland-juno106', 'roland-juno60', 'roland-juno6', 'roland-hs60'] as const;
const ALL_MODEL_IDS_MUTABLE: string[] = [...ALL_MODEL_IDS];

function getJunoContracts(): ModelContract[] {
  return ALL_MODEL_IDS_MUTABLE
    .map(id => getModelContract(id))
    .filter((c): c is ModelContract => c != null);
}

// Los cuatro Juno comparten formato byte a byte: ni los singles (23B) ni el
// bulk llevan byte de modelo (ver parseFile en roland-juno.ts), asi que por
// bytes el primer contrato de la lista gana siempre. El unico discriminante
// real es el nombre del fichero — asi los distinguen los fixtures factory
// (Juno106_*, Juno60_*, Juno6_*, HS60_*). Sin pista de nombre se conserva el
// orden por defecto (juno106 primero), que es el que venia hasta ahora.
// El orden de los patrones importa: "Juno6" no debe capturar "Juno60".
const FILENAME_MODEL_HINTS: Array<{ pattern: RegExp; modelId: string }> = [
  { pattern: /hs[\s_-]*60/i, modelId: 'roland-hs60' },
  { pattern: /juno[\s_-]*106/i, modelId: 'roland-juno106' },
  { pattern: /juno[\s_-]*60/i, modelId: 'roland-juno60' },
  { pattern: /juno[\s_-]*6(?![0-9])/i, modelId: 'roland-juno6' },
];

/** Ordena los contratos poniendo primero el que nombre el fichero, si lo nombra. */
function orderContractsForFilename(contracts: ModelContract[], filename: string): ModelContract[] {
  const hint = FILENAME_MODEL_HINTS.find(h => h.pattern.test(filename))?.modelId;
  if (!hint) return contracts;
  return [
    ...contracts.filter(c => c.modelId === hint),
    ...contracts.filter(c => c.modelId !== hint),
  ];
}

// ─── Import Adapter ───

export class RolandJunoImportAdapter extends BaseImportAdapter {
  adapterId = 'sysex-roland-juno';
  displayName = 'Roland Juno-106 / 60 / 6 / HS-60';
  supportedExtensions = ['.syx'];
  targetModelIds = ALL_MODEL_IDS_MUTABLE;

  canParse(data: Uint8Array, filename: string): boolean {
    if (!filename.toLowerCase().endsWith('.syx')) return false;
    for (const contract of getJunoContracts()) {
      if (contract.parseFile && contract.parseFile(data, filename) != null) return true;
    }
    return false;
  }

  verifyChecksum(data: Uint8Array): boolean {
    for (const contract of getJunoContracts()) {
      if (contract.verifyChecksum && contract.verifyChecksum(data)) return true;
    }
    return false;
  }

  parse(data: Uint8Array, filename: string): ImportResult {
    for (const contract of orderContractsForFilename(getJunoContracts(), filename)) {
      if (!contract.parseFile) continue;
      const result: ContractFileParse | null = contract.parseFile(data, filename);
      if (result == null) continue;

      const patches: PatchData[] = result.patches.map(p => ({
        name: p.name,
        category: p.category,
        author: p.author,
        tags: p.tags,
        notes: p.notes,
        originAddress: p.originAddress,
        rawData: new Uint8Array(p.rawData),
        hardwareIds: p.hardwareIds,
        parameters: {},
        isFavorite: p.isFavorite,
        creationDate: p.creationDate,
      }));

      return this.createResult({
        modelId: result.modelId,
        bankName: result.bankName,
        patches,
        warnings: result.warnings,
      });
    }

    return this.createResult({
      success: false,
      modelId: ALL_MODEL_IDS_MUTABLE[0],
      error: 'No se encontraron mensajes SysEx Roland Juno válidos',
    });
  }
}

// ─── Export Adapter ───

export class RolandJunoExportAdapter extends BaseExportAdapter {
  adapterId = 'export-roland-juno';
  displayName = 'Roland Juno-106 / 60 / 6 / HS-60';
  fileExtension = '.syx';
  targetModelIds = ALL_MODEL_IDS_MUTABLE;

  serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array {
    const opts = { ...this.getDefaultOptions(), ...options };
    const contract = getModelContract(ALL_MODEL_IDS_MUTABLE[0]);
    if (!contract?.serializeFile) {
      throw new Error('Roland Juno contract not found or missing serializeFile');
    }

    // Delegate the single-vs-bank orchestration to the contract: honor an
    // explicit format option, otherwise derive from patch count (single ≤ 1).
    const format: 'single' | 'bank' = options?.format ?? (patches.length <= 1 ? 'single' : 'bank');
    const contractPatches = patches.map((p, i) => ({
      rawData: p.rawData,
      slot: i,
      name: p.name,
    }));

    return contract.serializeFile(contractPatches, {
      midiChannel: opts.midiChannel ?? 0,
      deviceId: opts.deviceId ?? 0,
      format,
    });
  }
}

// ─── Hardware Link ───

export class RolandJunoHardwareLink extends BaseHardwareLink {
  modelId = 'roland-juno106';
  supportsEditBuffer = false;
  interMessageDelayMs = 50;
  dumpTimeoutMs = 3000;

  protected getManufacturerId(): number[] { return [0x41]; }
  protected getModelId(): number {
    const contract = getModelContract(this.modelId);
    return contract?.legacySysEx?.modelIdByte ?? 0x3E;
  }

  detectHardware(midiOutputs: Array<{ name?: string; id?: string }>): HardwareDevice | null {
    const contract = getModelContract(this.modelId);
    if (contract?.detectHardware) {
      return contract.detectHardware(midiOutputs);
    }
    for (const output of midiOutputs) {
      const name = output.name || '';
      if (/juno.?106/i.test(name) || /juno.?60/i.test(name) || /juno.?6[^0]/i.test(name) || /hs.?60/i.test(name)) {
        return {
          name,
          inputId: output.id || '',
          outputId: output.id || '',
          manufacturer: 'Roland',
          modelId: this.modelId,
        };
      }
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
    if (contract?.buildBulkSysEx) {
      return [contract.buildBulkSysEx(
        patches.map((p, i) => ({ rawData: p.rawData, slot: i })),
        channel,
      )];
    }
    return patches.map((p, i) => {
      if (contract?.buildPatchSysEx) {
        return contract.buildPatchSysEx(p.rawData, i, channel);
      }
      return new Uint8Array();
    });
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
      const patches: PatchData[] = parsed.map(p => ({
        name: contract.getProgramAddress?.(p.slot) ?? `Slot ${p.slot}`,
        category: contract.defaultCategory,
        author: 'Unknown',
        tags: [],
        notes: '',
        originAddress: contract.getProgramAddress?.(p.slot) ?? `Slot ${p.slot}`,
        rawData: new Uint8Array(p.rawData),
        parameters: {},
        isFavorite: false,
        creationDate: new Date().toISOString(),
      }));
      return {
        success: patches.length > 0,
        modelId: this.modelId,
        bankName: `Roland ${this.modelId.replace('roland-', '').toUpperCase()}`,
        patches,
        warnings: [],
      };
    }
    return { success: false, modelId: this.modelId, bankName: '', patches: [], warnings: [], error: 'Contract parseDumpResponse not available' };
  }
}
