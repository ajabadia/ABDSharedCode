// roland-aira-demora.ts — Roland AIRA Demora contract (split from roland-aira.ts)
import { ModelContract } from '../ModelContract';
import { buildAiraContract } from './roland-aira-shared';

export const rolandAiraDemoraContract = buildAiraContract(
  'roland-aira-demora',
  'Roland AIRA Demora',
  0x17,
  'roland-demora.webp',
);
