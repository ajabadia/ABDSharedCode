/**
 * Casio CZ-101 / CZ-1000 / CZ-5000 / CZ-1 Adapter
 *
 * Thin wrapper over the Casio CZ ModelContract. All byte-level SysEx
 * orchestration (nibble encoding, checksum, framing) is delegated to the
 * contract; this adapter only maps between the contract API and the
 * ImportAdapter / ExportAdapter / HardwareLinkContract interfaces.
 */

import { BaseImportAdapter, ImportResult, PatchData } from '../ImportAdapter';
import { BaseExportAdapter, ExportOptions } from '../ExportAdapter';
import { BaseHardwareLink, HardwareDevice, ImportResult as HLImportResult } from '../HardwareLinkContract';
import type { MidiOutputPortInfo } from '../Midi';
import type { ModelContract } from '../ModelContract';
import { getModelContract, getHardwareIds } from '../Models';

const ALL_MODEL_IDS = ['casio-cz101', 'casio-cz1000', 'casio-cz5000', 'casio-cz1'];
const CANONICAL_MODEL_ID = 'casio-cz101';

/** The subset of ModelContract that the Casio CZ contract implements. */
type CasioCzContract = ModelContract &
  Required<Pick<ModelContract,
    'parseFile' | 'serializeFile' | 'detectHardware' | 'buildPatchSysEx' |
    'buildDumpRequest' | 'parseDumpResponse' | 'verifyChecksum' | 'getProgramAddress'>>;

function getCanonicalContract(): CasioCzContract {
  const contract = getModelContract(CANONICAL_MODEL_ID);
  if (!contract) throw new Error(`ModelContract '${CANONICAL_MODEL_ID}' not found`);
  return contract as CasioCzContract;
}

// ─── Import Adapter ───

export class CasioCzImportAdapter extends BaseImportAdapter {
  adapterId = 'sysex-casio-cz';
  displayName = 'Casio CZ-101 / CZ-1000 / CZ-5000 / CZ-1';
  supportedExtensions = ['.syx'];
  targetModelIds = ALL_MODEL_IDS;

  canParse(data: Uint8Array, filename: string): boolean {
    if (!filename.toLowerCase().endsWith('.syx')) return false;
    return getCanonicalContract().parseFile(data, filename) !== null;
  }

  verifyChecksum(data: Uint8Array): boolean {
    return getCanonicalContract().verifyChecksum(data);
  }

  parse(data: Uint8Array, filename: string): ImportResult {
    const contract = getCanonicalContract();
    const parsed = contract.parseFile(data, filename);
    if (parsed === null) {
      return this.createResult({
        success: false,
        modelId: CANONICAL_MODEL_ID,
        error: 'No se encontraron mensajes SysEx Casio CZ válidos',
      });
    }

    const hardwareIds = getHardwareIds(parsed.modelId);
    const patches: PatchData[] = parsed.patches.map(p => this.createPatchData({
      name: p.name,
      category: p.category,
      author: p.author,
      tags: p.tags,
      notes: p.notes,
      originAddress: p.originAddress,
      rawData: p.rawData,
      hardwareIds,
      parameters: {},
      isFavorite: p.isFavorite,
      creationDate: p.creationDate,
    }));

    return this.createResult({
      modelId: parsed.modelId,
      bankName: parsed.bankName,
      patches,
      warnings: parsed.warnings,
    });
  }
}

// ─── Export Adapter ───

export class CasioCzExportAdapter extends BaseExportAdapter {
  adapterId = 'export-casio-cz';
  displayName = 'Casio CZ-101 / CZ-1000 / CZ-5000 / CZ-1';
  fileExtension = '.syx';
  targetModelIds = ALL_MODEL_IDS;

  serialize(patches: PatchData[], bankName: string, options?: ExportOptions): Uint8Array {
    const opts = { ...this.getDefaultOptions(), ...options };
    const contract = getCanonicalContract();
    const mapped = patches.map((p, i) => ({
      rawData: p.rawData,
      slot: i,
      name: p.name,
    }));
    return contract.serializeFile(mapped, {
      midiChannel: opts.midiChannel,
      deviceId: opts.deviceId,
      format: opts.format,
    });
  }
}

// ─── Hardware Link ───

export class CasioCzHardwareLink extends BaseHardwareLink {
  modelId = 'casio-cz101';
  supportsEditBuffer = false;
  interMessageDelayMs = 100;
  dumpTimeoutMs = 5000;

  protected getManufacturerId(): number[] {
    return getCanonicalContract().sysexManufacturerId;
  }
  protected getModelId(): number {
    return getCanonicalContract().legacySysEx?.modelIdByte ?? 0x12;
  }

  detectHardware(midiOutputs: MidiOutputPortInfo[]): HardwareDevice | null {
    return getCanonicalContract().detectHardware(midiOutputs);
  }

  buildPatchDump(patch: PatchData, slot: number, channel: number): Uint8Array[] {
    const contract = getCanonicalContract();
    return [contract.buildPatchSysEx(patch.rawData, slot, channel)];
  }

  buildBankDump(patches: PatchData[], channel: number): Uint8Array[] {
    return patches.flatMap((p, i) => this.buildPatchDump(p, i, channel));
  }

  buildDumpRequest(slot: number | 'all', channel: number): Uint8Array {
    return getCanonicalContract().buildDumpRequest(slot, channel);
  }

  parseDumpResponse(data: Uint8Array): HLImportResult {
    const contract = getCanonicalContract();
    const entries = contract.parseDumpResponse(data);
    const hardwareIds = getHardwareIds(contract.modelId);
    const patches = entries.map(e => this.createResultPatch(contract, e, hardwareIds));
    if (patches.length === 0) {
      return this.createImportResult({
        success: false,
        modelId: contract.modelId,
        error: 'No se encontraron mensajes SysEx Casio CZ válidos',
      });
    }
    return this.createImportResult({
      modelId: contract.modelId,
      bankName: `Casio ${contract.displayName}`,
      patches,
    });
  }

  private createResultPatch(contract: CasioCzContract, e: { rawData: Uint8Array; slot: number }, hardwareIds: string[]): PatchData {
    return {
      name: contract.getProgramAddress(e.slot),
      category: 'Other',
      author: 'Unknown',
      tags: [],
      notes: '',
      originAddress: contract.getProgramAddress(e.slot),
      rawData: new Uint8Array(e.rawData),
      hardwareIds,
      parameters: {},
      isFavorite: false,
      creationDate: new Date().toISOString(),
    };
  }

  private createImportResult(overrides: Partial<ImportResult>): ImportResult {
    return {
      success: true,
      modelId: '',
      bankName: 'Imported Bank',
      patches: [],
      warnings: [],
      ...overrides,
    };
  }
}