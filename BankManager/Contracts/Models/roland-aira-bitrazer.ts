// roland-aira-bitrazer.ts — Roland AIRA Bitrazer contract (split from roland-aira.ts)
import { ModelContract } from '../ModelContract';
import { buildAiraContract } from './roland-aira-shared';

export const rolandAiraBitrazerContract = buildAiraContract(
  'roland-aira-bitrazer',
  'Roland AIRA Bitrazer',
  0x15,
  'roland-bitrazer.webp',
);
