import { describe, it, expect } from 'vitest';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { SCOPE_ICONS } from '../src/icons.js';

const here = dirname(fileURLToPath(import.meta.url));
// ABDSynths workspace layout: ABDScope/WebUI/tests -> ../../.. + ABDSharedAssets.
const canonicalDir = resolve(here, '../../../ABDSharedAssets/icons');
const hasCanonical = existsSync(canonicalDir);

const normalize = (svg) => svg.replace(/^\uFEFF/, '').replace(/\r/g, '').trim();

describe('scope icons module', () => {
  it('exposes exactly the icons the module renders', () => {
    expect(Object.keys(SCOPE_ICONS).sort()).toEqual(['camera', 'close', 'freeze']);
  });

  it('contains well-formed full svg markup per icon', () => {
    for (const [name, svg] of Object.entries(SCOPE_ICONS)) {
      expect(svg.startsWith('<svg '), `${name} must start with <svg`).toBe(true);
      expect(svg.trimEnd().endsWith('</svg>'), `${name} must end with </svg>`).toBe(true);
      expect(svg).toContain('currentColor');
    }
  });
});

const paritySuite = hasCanonical
  ? describe
  : describe.skip;

paritySuite('canonical parity with ABDSharedAssets/icons', () => {
  it('module content matches the canonical svg files (BOM/CRLF normalized)', () => {
    for (const [name, svg] of Object.entries(SCOPE_ICONS)) {
      const file = join(canonicalDir, `${name}.svg`);
      expect(existsSync(file), `${name}.svg missing in ABDSharedAssets/icons`).toBe(true);
      expect(normalize(svg), `${name} drifted from canonical ${file}`)
        .toBe(normalize(readFileSync(file, 'utf8')));
    }
  });
});
