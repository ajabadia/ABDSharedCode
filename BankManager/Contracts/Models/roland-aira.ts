/**
 * ABD Bank Manager — Roland AIRA Modular Contracts
 * Bitrazer / Torcido / Demora / Scooper
 *
 * The four AIRA modules share one identical internal engine: 6 virtual
 * sub-module slots, a virtual cable matrix and 10 main-module parameters.
 * Only the factory algorithm (distortion / bitcrusher / delay / scoop) and
 * the panel serigraphy differ. Model IDs (0x15..0x18) come from the SysEx
 * address map documented by mugenkidou (AIRA_Modular_Effects) and mirrored
 * in ABDSharedAssets/contracts/roland_aira_patch_spec.json.
 *
 * Each module holds exactly ONE state (no patch memory) — the "bank" is
 * therefore 1 slot of one patch whose rawData is the canonical state layout
 * produced by the AIRA SysEx adapter (302 bytes, see rolandAiraAdapter.ts).
 */

import { ModelContract } from '../ModelContract';

const AIRA_MAIN_PARAM_COUNT = 10;
const AIRA_SLOT_COUNT = 6;
const AIRA_PARAMS_PER_SLOT = 4;
const AIRA_CONDITION_SOURCES = 22;
const AIRA_CONDITION_BYTES_PER_SOURCE = 6;
const AIRA_MAX_CABLES = 64;

/** Canonical state size: 302 bytes (see rolandAiraAdapter.ts header). */
const AIRA_PATCH_DATA_SIZE =
  1 + AIRA_MAIN_PARAM_COUNT
  + AIRA_SLOT_COUNT
  + AIRA_SLOT_COUNT * AIRA_PARAMS_PER_SLOT
  + 1 + AIRA_MAX_CABLES * 2
  + AIRA_CONDITION_SOURCES * AIRA_CONDITION_BYTES_PER_SOURCE;

const CATEGORIES = ['Patch', 'Other'];
const DEFAULT_CATEGORY = 'Patch';

function buildAiraContract(
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

    categories: CATEGORIES,
    defaultCategory: DEFAULT_CATEGORY,

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

export const rolandAiraBitrazerContract = buildAiraContract(
  'roland-aira-bitrazer', 'Roland AIRA Bitrazer', 0x15, 'roland-bitrazer.webp',
);
export const rolandAiraTorcidoContract = buildAiraContract(
  'roland-aira-torcido', 'Roland AIRA Torcido', 0x16, 'roland-torcido.webp',
);
export const rolandAiraDemoraContract = buildAiraContract(
  'roland-aira-demora', 'Roland AIRA Demora', 0x17, 'roland-demora.webp',
);
export const rolandAiraScooperContract = buildAiraContract(
  'roland-aira-scooper', 'Roland AIRA Scooper', 0x18, 'roland-scooper.webp',
);

export const allRolandAiraContracts: ModelContract[] = [
  rolandAiraBitrazerContract,
  rolandAiraTorcidoContract,
  rolandAiraDemoraContract,
  rolandAiraScooperContract,
];
