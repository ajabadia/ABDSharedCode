// roland-aira-torcido.ts — Roland AIRA Torcido contract (split from roland-aira.ts)
import { ModelContract } from '../ModelContract';
import { buildAiraContract } from './roland-aira-shared';

export const rolandAiraTorcidoContract = buildAiraContract(
  'roland-aira-torcido',
  'Roland AIRA Torcido',
  0x16,
  'roland-torcido.webp',
);
