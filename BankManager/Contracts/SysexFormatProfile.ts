import type { ModelContract } from './ModelContract.ts';

export interface SysexFormatProfile {
  profileId: string;
  modelId: string;
  displayName: string;
  hardwareRevision?: string;
  firmwareRange?: { min?: string; max?: string };
  manufacturerId: number[];
  modelIdBytes?: number[];
  commands: Record<string, number | number[]>;
  rawDataSize: number;
  wireDataSize?: number;
  packing?: 'none' | 'nibble' | '7to8' | '8to7';
  checksum?: 'none' | 'sum7' | 'roland' | 'casio' | 'custom';
  addressing: string;
  notes?: string;
}

export interface NormativeContractRow extends SysexFormatProfile {
  bankCapacity: number;
  banksCount: number;
  programsPerBank: number;
  formatVersion: number;
}

export function validateSysexFormatProfile(profile: SysexFormatProfile): string[] {
  const errors: string[] = [];
  if (!profile.profileId) errors.push('profileId is required');
  if (!profile.modelId) errors.push('modelId is required');
  if (!Array.isArray(profile.manufacturerId) || profile.manufacturerId.length === 0) errors.push('manufacturerId is required');
  if (!Number.isInteger(profile.rawDataSize) || profile.rawDataSize <= 0) errors.push('rawDataSize must be positive');
  if (profile.wireDataSize !== undefined && (!Number.isInteger(profile.wireDataSize) || profile.wireDataSize <= 0)) errors.push('wireDataSize must be positive');
  if (!profile.addressing) errors.push('addressing is required');
  return errors;
}

export function createNormativeContractRow(contract: ModelContract, profile: SysexFormatProfile): NormativeContractRow {
  const errors = validateSysexFormatProfile(profile);
  if (errors.length > 0) throw new Error(`Invalid SysEx profile '${profile.profileId}': ${errors.join('; ')}`);
  if (profile.modelId !== contract.modelId) throw new Error(`Profile '${profile.profileId}' does not belong to '${contract.modelId}'`);
  return {
    ...profile,
    bankCapacity: contract.bankCapacity,
    banksCount: contract.banksCount,
    programsPerBank: contract.programsPerBank,
    formatVersion: contract.formatVersion
  };
}
