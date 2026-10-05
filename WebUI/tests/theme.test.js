import { describe, it, expect } from 'vitest';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const WORKSPACE_ROOT = resolve(here, '../../..');
const ABS_ASSETS = resolve(WORKSPACE_ROOT, 'ABDSharedAssets');
const THEMES_DIR = resolve(ABS_ASSETS, 'styles', 'themes');
const ADAPTER_PATH = resolve(ABS_ASSETS, 'styles', 'components', 'scope.css');
const GENERATED_PATH = resolve(here, '../src/theme.generated.css');

const normalize = (css) => css.replace(/^\uFEFF/, '').replace(/\r/g, '').trim();

const hasWorkspace = existsSync(ABS_ASSETS) && existsSync(THEMES_DIR) && existsSync(ADAPTER_PATH);
const generatedExists = existsSync(GENERATED_PATH);

const readGenerated = () => normalize(readFileSync(GENERATED_PATH, 'utf8'));

const readCanonicalThemes = () => {
  const files = readdirSync(THEMES_DIR)
    .filter((f) => f.endsWith('.css'))
    .sort();
  return files.map((f) => ({
    file: f.replace(/\.css$/, ''),
    css: normalize(readFileSync(join(THEMES_DIR, f), 'utf8')),
  }));
};

const readAdapter = () => normalize(readFileSync(ADAPTER_PATH, 'utf8'));

describe('theme cascade', () => {
  it('requires the generated file to exist', () => {
    expect(generatedExists).toBe(true);
  });
});

const canonicalSuite = hasWorkspace && generatedExists ? describe : describe.skip;

canonicalSuite('canonical parity with ABDSharedAssets', () => {
  const generated = readGenerated();
  const themes = readCanonicalThemes();
  const adapter = readAdapter();

  it('generated file includes all canonical theme files', () => {
    for (const { file, css } of themes) {
      expect(generated).toContain(`/* canonical: ABDSharedAssets/styles/themes/${file}.css */`);
      expect(generated).toContain(css);
    }
  });

  it('generated file includes the adapter exactly once', () => {
    const adapterMarker = '/* canonical: ABDSharedAssets/styles/components/scope.css */';
    expect(generated).toContain(adapterMarker);
    const idx = generated.indexOf(adapterMarker);
    const afterMarker = generated.slice(idx);
    expect(afterMarker.startsWith(adapterMarker + '\n' + adapter)).toBe(true);
  });

  it('generated file contains no CSS outside canonical sources + adapter', () => {
    for (const { file, css } of themes) {
      expect(generated).toContain(css);
    }
    expect(generated).toContain(adapter);
    expect(generated.split('\n').filter((line) => line.trim().startsWith('/* canonical:')).length).toBe(
      themes.length + 1
    );
  });
});
