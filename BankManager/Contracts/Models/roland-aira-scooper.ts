// roland-aira-scooper.ts — Roland AIRA Scooper contract (split from roland-aira.ts)
import { ModelContract } from '../ModelContract';
import { buildAiraContract } from './roland-aira-shared';

export const rolandAiraScooperContract = buildAiraContract(
  'roland-aira-scooper',
  'Roland AIRA Scooper',
  0x18,
  'roland-scooper.webp',
);
