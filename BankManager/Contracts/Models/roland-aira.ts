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
 *
 * **Estructura:** este barrel reexporta los cuatro contratos desde sus
 * módulos individuales (`roland-aira-bitrazer`, `roland-aira-torcido`,
 * `roland-aira-demora`, `roland-aira-scooper`). Los consumidores que
 * importan `allRolandAiraContracts` o los contratos individuales no notan
 * el cambio. El adaptador `rolandAiraAdapter.ts` delega en
 * `getModelContract(modelId)` y no necesita saber que los contratos ahora
 * viven en archivos separados.
 */

import { rolandAiraBitrazerContract } from './roland-aira-bitrazer';
import { rolandAiraTorcidoContract } from './roland-aira-torcido';
import { rolandAiraDemoraContract } from './roland-aira-demora';
import { rolandAiraScooperContract } from './roland-aira-scooper';

export { rolandAiraBitrazerContract } from './roland-aira-bitrazer';
export { rolandAiraTorcidoContract } from './roland-aira-torcido';
export { rolandAiraDemoraContract } from './roland-aira-demora';
export { rolandAiraScooperContract } from './roland-aira-scooper';

export const allRolandAiraContracts = [
  rolandAiraBitrazerContract,
  rolandAiraTorcidoContract,
  rolandAiraDemoraContract,
  rolandAiraScooperContract,
];
