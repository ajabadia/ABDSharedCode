// roland-aira-shared.ts — constantes y helper compartidos por los contratos AIRA (split)
// Factoría de ModelContract para módulos Roland AIRA Modular.
// SSOT de categorías y títulos: ABDSharedAssets/contracts/roland_aira_*.json.

import { ModelContract } from '../ModelContract';

export const AIRA_MAIN_PARAM_COUNT = 10;
export const AIRA_SLOT_COUNT = 6;
export const AIRA_PARAMS_PER_SLOT = 4;
export const AIRA_CONDITION_SOURCES = 22;
export const AIRA_CONDITION_BYTES_PER_SOURCE = 6;
export const AIRA_MAX_CABLES = 64;

/** Tamaño canónico del estado: 302 bytes (véase rolandAiraAdapter.ts cabecera). */
export const AIRA_PATCH_DATA_SIZE =
  1 + AIRA_MAIN_PARAM_COUNT
  + AIRA_SLOT_COUNT
  + AIRA_SLOT_COUNT * AIRA_PARAMS_PER_SLOT
  + 1 + AIRA_MAX_CABLES * 2
  + AIRA_CONDITION_SOURCES * AIRA_CONDITION_BYTES_PER_SOURCE;

/** Categorías fieles por módulo (SSOT: ABDSharedAssets/contracts/roland_aira_*.json). */
export const AIRA_CATEGORIES: Record<string, string[]> = {
  'roland-aira-bitrazer': ['Filter', 'Crusher', 'Delay', 'Custom'],
  'roland-aira-torcido': ['Distortion', 'Overdrive', 'Custom'],
  'roland-aira-demora': ['Delay', 'Buffer', 'Custom'],
  'roland-aira-scooper': ['Looper', 'Scatter', 'Custom'],
};
export const AIRA_DEFAULT_CATEGORY = 'Custom';

/**
 * Construye un ModelContract AIRA desde sus datos canónicos.
 * Los cuatro módulos (Bitrazer, Torcido, Demora, Scooper) comparten el
 * mismo layout de estado y solo difieren en el modelId byte, nombre y
 * miniatura.
 */
export function buildAiraContract(
  modelId: string,
  displayName: string,
  modelIdByte: number,
  thumbnail: string,
): ModelContract {
  return {
    modelId,
    displayName,
    manufacturer: 'Roland',
    icon: 'roland-logo.svg',
    thumbnail,

    bankCapacity: 1,
    banksCount: 1,
    programsPerBank: 1,

    getProgramAddress: () => '10 00 00 00',
    parseProgramAddress: (address) => (address === '10 00 00 00' ? 0 : null),

    patchDataSize: AIRA_PATCH_DATA_SIZE,
    patchNameMaxLength: 0,
    extractPatchName: () => '',

    categories: AIRA_CATEGORIES[modelId] ?? ['Custom'],
    defaultCategory: AIRA_DEFAULT_CATEGORY,

    compatibleModels: ['roland-aira-bitrazer', 'roland-aira-torcido', 'roland-aira-demora', 'roland-aira-scooper'],

    sysexManufacturerId: [0x41],
    formatVersion: 1,

    sysexModelId: {
      offset: 6,
      values: [modelIdByte],
      multiByte: [0x00, 0x00, 0x00],
    },

    midiDetection: {
      portPattern: new RegExp(
        modelId.replace('roland-aira-', '') + '|aira',
        'i',
      ),
      displayName: displayName.replace('Roland AIRA ', ''),
    },

    midi: {
      defaultChannel: 1,
      defaultDeviceId: 0x10,
    },

    computeChecksum(data: Uint8Array): number {
      let sum = 0;
      for (const b of data) sum += b;
      return (0x80 - (sum & 0x7F)) & 0x7F;
    },

    verifyChecksum(sysex: Uint8Array): boolean {
      if (sysex.length < 14) return false;
      if (sysex[0] !== 0xF0 || sysex[1] !== 0x41 || sysex[2] !== 0x10) return false;
      if (sysex[6] !== modelIdByte || sysex[7] !== 0x12) return false;
      const body = sysex.slice(8, sysex.length - 2);
      const expected = this.computeChecksum!(body);
      return sysex[sysex.length - 2] === expected;
    },

    detectHardware(ports: Array<{ name?: string; id?: string }>): { name: string; inputId: string; outputId: string; manufacturer: string; modelId: string } | null {
      const port = ports.find(p => new RegExp(modelId.replace('roland-aira-', '') + '|aira', 'i').test(p.name || ''));
      return port
        ? { name: port.name || displayName, inputId: port.id || '', outputId: port.id || '', manufacturer: 'Roland', modelId }
        : null;
    },
  };
}
