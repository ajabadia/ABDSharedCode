/**
 * ABD Bank Manager — All Model Contracts
 * Re-exports all hardware ModelContracts
 */

import { allCasioContracts } from './casio-cz.ts';
import { allRolandJunoContracts } from './roland-juno.ts';
import { allKorgContracts } from './korg-ms2000.ts';
import { allKorgProphecyContracts } from './korg-prophecy.ts';
import { allBehringerDm12Contracts } from './behringer-dm12.ts';
import { allBehringerDm6Contracts } from './behringer-dm6.ts';
import { allBehringerDm12dContracts } from './behringer-dm12d.ts';
import { allBehringerPro800Contracts } from './behringer-pro800.ts';
import { allYamahaContracts } from './yamaha-dx7.ts';
import { allRolandAiraContracts } from './roland-aira.ts';
import { HARDWARE_QUEUE_CONFIGS } from '../HardwareQueueConfigs.ts';

// Re-export individual contracts
export * from './casio-cz.ts';
export * from './roland-juno.ts';
export * from './korg-ms2000.ts';
export * from './korg-prophecy.ts';
export * from './behringer-dm12.ts';
export * from './behringer-dm6.ts';
export * from './behringer-dm12d.ts';
export * from './behringer-pro800.ts';
export * from './yamaha-dx7.ts';
export * from './roland-aira.ts';

// All contracts combined
export const allModelContracts = [
  ...allCasioContracts,
  ...allRolandJunoContracts,
  ...allKorgContracts,
  ...allKorgProphecyContracts,
  ...allBehringerDm12Contracts,
  ...allBehringerDm6Contracts,
  ...allBehringerDm12dContracts,
  ...allBehringerPro800Contracts,
  ...allYamahaContracts,
  ...allRolandAiraContracts
];

// Lookup by modelId
export const modelContractMap = new Map(allModelContracts.map(c => [c.modelId, c]));

export function getModelContract(modelId: string) {
  return modelContractMap.get(modelId);
}

export function getCompatibleModels(modelId: string): string[] {
  const contract = modelContractMap.get(modelId);
  const compat = new Set(contract?.compatibleModels || []);
  // Reverse: models whose compatibleModels list this modelId
  for (const [id, c] of modelContractMap) {
    if (id !== modelId && c.compatibleModels?.includes(modelId)) compat.add(id);
  }
  return Array.from(compat);
}

/**
 * Asociación multi-hardware de un patch/banco: el blob es válido en el modelo
 * canónico y en todos sus compatibleModels. El gestor no interpreta el blob
 * (principio de asepsia) — solo sabe a qué hardwares asociarlo.
 */
export function getHardwareIds(modelId: string): string[] {
  const contract = modelContractMap.get(modelId);
  if (!contract) return [modelId];
  const ids = new Set<string>([modelId]);
  // Forward: models listed in this contract's compatibleModels
  if (contract.compatibleModels) {
    for (const id of contract.compatibleModels) ids.add(id);
  }
  // Reverse: models whose compatibleModels include this modelId
  for (const [id, c] of modelContractMap) {
    if (id !== modelId && c.compatibleModels?.includes(modelId)) ids.add(id);
  }
  return Array.from(ids);
}

export function getContractsForManufacturer(manufacturer: string) {
  return allModelContracts.filter(c => c.manufacturer === manufacturer);
}

// Manufacturer name → HARDWARE_QUEUE_CONFIGS key
const MANUFACTURER_TO_QUEUE_KEY: Record<string, keyof typeof HARDWARE_QUEUE_CONFIGS> = {
  Casio: 'casio-cz',
  Roland: 'roland-juno',
  Korg: 'korg-ms2000',
  Behringer: 'behringer-dm12',
  Yamaha: 'yamaha-dx7'
};

export interface MidiConfig {
  channel: number;
  deviceId: number;
  interMessageDelayMs: number;
  dumpTimeoutMs: number;
}

/**
 * Configuración MIDI derivada (no editable) para un modelo:
 * canal/device del ModelContract.midi + timing de HARDWARE_QUEUE_CONFIGS.
 * El Bank Manager no expone settings de MIDI; se deriva del contrato y el hardware.
 */
export function getMidiConfig(modelId: string): MidiConfig {
  const contract = modelContractMap.get(modelId);
  const queueKey = contract ? MANUFACTURER_TO_QUEUE_KEY[contract.manufacturer] : undefined;
  const queue = queueKey ? HARDWARE_QUEUE_CONFIGS[queueKey] : undefined;

  return {
    channel: contract?.midi?.defaultChannel ?? 1,
    deviceId: contract?.manufacturer === 'Korg'
    ? 0x10
    : contract?.midi?.defaultDeviceId ?? 0x10,
    interMessageDelayMs: queue?.interMessageDelayMs ?? 20,
    dumpTimeoutMs: queue?.dumpTimeoutMs ?? 3000
  };
}