/**
 * ABDKeyboard — Host Strip Height Smoke Test
 *
 * HOST CONTRACT (src/keyboard.css, línea 10): la franja del host que envuelve
 * `#piano-keyboard` lleva altura DEFINIDA (px o calc() sobre variables
 * resolubles), nunca una cadena de estiramientos (`height:100%` / `auto` /
 * solo `min-height`). WebView2 no resuelve el 100% a través de estiramientos
 * dobles: las teclas colapsan a fracción de píxel (lección NEURONiK 8.3,
 * "todas negras").
 *
 * README §Important Layout & Container Contract documenta los hosts:
 *   ABDMS2000 155px (colapsado 42px) · ABDEep 185px · ABDCZ101 180px ·
 *   NEURONiK height: calc(var(--abd-keys-h) - var(--keys-note-h, 14px))
 *
 * Tres capas:
 *   1. Registro (HOSTS): cada host documentado tiene su regla de franja en
 *      CSS y esa regla declara SU altura de siempre (stripHeightPx), dentro del
 *      rango saneable.
 *   2. Descubrimiento: cualquier proyecto del workspace que monte
 *      (@abdsynths/midi-keyb en un import) o declare el paquete como
 *      dependencia DEBE estar en el registro — un synth nuevo no puede montar
 *      el teclado compartido sin franja con altura cumpliendo el contrato.
 *   3. Montaje REAL: cada host registrado MONTA el paquete (import del
 *      specifier + createKeyboard en el mismo fichero). Declarar la dependencia
 *      y renderizar el keybed propio ya no cuela: fue el caso de ABDEep y
 *      ABDCZ101 hasta que montaron el componente compartido.
 */

import { describe, it, expect } from 'vitest';
import { readFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import { join, extname, sep, relative, resolve } from 'node:path';

const PKG = '@abdsynths/midi-keyb';
const MIN_STRIP_PX = 40;  // por debajo de esto las teclas son inservibles
const MAX_STRIP_PX = 400; // una franja más alta que esto es un error de layout

/**
 * Registro de hosts: proyecto del workspace → selector de su franja y ficheros
 * CSS que hay que leer (rutas relativas a la raíz del workspace). Si añades un
 * synth nuevo que monta el teclado compartido, añádelo aquí.
 */
const HOSTS = [
  {
    project: 'ABDMS2000',
    stripSelector: '.keyboard-container',
    cssFiles: ['ABDMS2000/WebUI/src/styles/main.css'],
    stripHeightPx: 155,
  },
  {
    project: 'ABDEep',
    stripSelector: '.keyboard-container',
    cssFiles: ['ABDEep/WebUI/css/keyboard.css'],
    stripHeightPx: 185,
  },
  {
    project: 'ABDCZ101',
    stripSelector: '.keyboard',
    cssFiles: ['ABDCZ101/WebUI/src/styles/keyboard.css'],
    stripHeightPx: 180,
  },
  {
    // NEURONiK: la franja la crea src/ui/keyboard.js (div.keys-strip) y la
    // altura vive en main.css como calc sobre --abd-keys-h (120 - 14).
    project: 'ABDNeural',
    stripSelector: '.keys-strip',
    cssFiles: ['ABDNeural/WebUI/src/styles/main.css'],
    stripHeightPx: 106,
  },
];

/** Specimen de montaje real: import del specifier + llamada al factory. */
const MOUNT_CALL_RE = /createKeyboard\s*\(/;

// ── utilidades de workspace ──────────────────────────────────────────────────

/**
 * Sube desde cwd (vitest corre con cwd = raíz del paquete; en jsdom
 * import.meta.url no es file://) hasta la raíz del workspace ABDSynths
 * (puerta: pnpm-workspace.yaml + ABDSharedCode/MidiKeyboard).
 */
function findWorkspaceRoot() {
  let dir = process.cwd();
  for (let i = 0; i < 6; i++) {
    if (existsSync(join(dir, 'pnpm-workspace.yaml'))
      && existsSync(join(dir, 'ABDSharedCode', 'MidiKeyboard'))) {
      return dir;
    }
    const parent = resolve(dir, '..');
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}

// QUE ESTE FICHERO NO ES UN TEST DE ESTE PAQUETE, Y POR QUE SALTA EN VEZ DE
// REVENTAR. Las tres capas de arriba necesitan el CSS y el codigo de los cuatro
// hosts registrados (ABDMS2000, ABDEep, ABDCZ101, ABDNeural): comprueba que
// declaren altura definida en su franja, que esten registrados, y que monten el
// paquete de verdad. Eso es un CONTRATO DEL WORKSPACE, y un workspace son cinco
// repos, no uno. En un clon limpio de ABDSharedCode —que es donde se corre esto
// para probar el paquete— no hay ni la raiz del workspace ni los hosts.
//
// Antes esto fallaba al CARGAR el modulo, y asi se llevaba por delante los otros
// cinco ficheros del paquete en el recuento del runner: 326 pruebas en verde
// acababan marcadas como un fallo. Peor: el mensaje ("No se encontro la raiz
// del workspace") no decia que hacer, y el arreglo no es un bug, es un clon.
//
// Saltar es correcto aqui porque el salto se ve. Un `describe.skip` silencioso
// seria un verde que parece una comprobacion; este imprime el motivo y el
// runner lo cuenta como omitido. Lo que NO haria es fingir que el contrato se
// cumple: por eso el motivo nombra los repos que faltan.
const workspaceRoot = findWorkspaceRoot();
const SIN_WORKSPACE = workspaceRoot === null;

if (SIN_WORKSPACE) {
  process.stderr.write(
    '\n'
    + '  [host-strip-height] OMITIDO: este fichero no se puede ejecutar aqui.\n'
    + '  No es un test de @abdsynths/midi-keyb: es el contrato de franja de los\n'
    + '  cuatro hosts que montan el teclado (ABDMS2000, ABDEep, ABDCZ101,\n'
    + '  ABDNeural), y necesita sus repos al lado. Se ejecuta desde la raiz de la\n'
    + '  suite ABDSynths, donde esta pnpm-workspace.yaml y estan los hosts.\n'
    + '  Los otros cinco ficheros de este paquete si se ejecutan.\n\n'
  );
}

// Cuando no hay workspace, `root` se usa solo dentro de las capas que ya estan
// saltadas; se deja apuntando al paquete para que un fallo futuro sea legible en
// vez de ser un TypeError sobre null.
const root = workspaceRoot ?? process.cwd();

// ── utilidades de CSS (parseo mínimo, suficiente para reglas planas) ─────────

function stripComments(css) {
  return css.replace(/\/\*[\s\S]*?\*\//g, '');
}

function splitSelectorList(sel) {
  return sel.split(',').map((s) => s.trim()).filter(Boolean);
}

/**
 * Devuelve el cuerpo `{...}` de todas las reglas cuya lista de selectores
 * contiene `selector` exactamente (`.keyboard` no matchea `.keyboard::after`).
 * Maneja anidamiento de @media mediante pila.
 */
function extractRuleBodies(cssText, selector) {
  const css = stripComments(cssText);
  const bodies = [];
  const stack = [];
  let token = '';
  for (const ch of css) {
    if (ch === '{') {
      stack.push(token.trim());
      token = '';
    } else if (ch === '}') {
      const sel = stack.pop();
      if (sel !== undefined && splitSelectorList(sel).includes(selector)) {
        bodies.push(token);
      }
      token = '';
    } else {
      token += ch;
    }
  }
  return bodies;
}

/** Todos los `height:` declarados en el body (excluye min-/max-height: el previo no es `[\s;{]`). */
function extractHeightDecls(body) {
  const out = [];
  const re = /(?:^|[\s;{])height\s*:\s*([^;}]+)/gi;
  let m;
  while ((m = re.exec(body)) !== null) out.push(m[1].trim());
  return out;
}

/** Recolecta declaraciones `--var: valor` del CSS (la última de cada nombre gana). */
function collectVars(text, into = new Map()) {
  const re = /(--[A-Za-z0-9_-]+)\s*:\s*([^;}]+)/g;
  let m;
  while ((m = re.exec(text)) !== null) into.set(m[1], m[2].trim());
  return into;
}

/**
 * Resuelve un valor de height a px, o null si NO es altura definida
 * (100%, auto, var sin resolver, calc con %, etc. → cadena de estiramientos).
 */
function resolveHeight(raw, vars) {
  let v = raw.replace(/\s*!important\s*$/i, '').trim();

  // Sustituye var(--x[, fallback]) por su valor, iterativamente (vars anidadas).
  for (let i = 0; i < 10 && v.includes('var('); i++) {
    let changed = false;
    v = v.replace(/var\(\s*(--[\w-]+)\s*(?:,\s*([^()]*)\s*)?\)/g, (all, name, fallback) => {
      if (vars.has(name)) {
        changed = true;
        return vars.get(name);
      }
      if (fallback !== undefined) {
        changed = true;
        return fallback.trim();
      }
      return all; // var sin valor ni fallback → indefinida
    });
    if (!changed) break;
  }
  if (v.includes('var(')) return null;

  const plain = v.match(/^([+-]?\d*\.?\d+)px$/);
  if (plain) return parseFloat(plain[1]);

  const calc = v.match(/^calc\((.+)\)$/s);
  if (calc) {
    // Solo aritmética px pura: si queda un '%' (p. ej. calc(100% - 10px)) no es altura definida.
    const expr = calc[1].replace(/([+-]?\d*\.?\d+)px/g, '$1');
    if (!/^[-+*/().\d\s]+$/.test(expr)) return null;
    try {
      const n = Function(`"use strict"; return (${expr});`)();
      return Number.isFinite(n) ? n : null;
    } catch {
      return null;
    }
  }
  return null;
}

// ── descubrimiento de hosts en el workspace ──────────────────────────────────

const CODE_EXTS = new Set(['.js', '.mjs', '.cjs', '.ts', '.tsx', '.jsx', '.html']);
// Cualquier especificador entre comillas del paquete: from '…', import '…', export … from '…', require('…').
const SPECIFIER_RE = /['"]@abdsynths\/midi-keyb(\/[A-Za-z0-9._-]+)*['"]/;
const SKIP_DIRS = new Set([
  'node_modules', 'dist', 'build', 'Build', 'Release', 'GITS', 'tmp', 'bin',
  'obj', 'legacy', 'wasm', 'coverage', 'JUCE', 'JUNO106',
]);

/**
 * Recorre el workspace y devuelve:
 *  - mounters:   proyectos con un import de @abdsynths/midi-keyb en su código
 *  - dependents: proyectos que lo declaran en package.json (dependencies/peer/dev)
 * Ignora el directorio de este propio paquete (su src/tests citan el
 * specifier pero no son un host), los directorios ocultos/`_prefixed`
 * (basura: _backups, _Deprecados…) y los derivatives (dist/build).
 * A partir de 60k ficheros leídos corta (válvula de seguridad; el chequeo
 * inverso del test delata el corte).
 */
function discoverHosts(wsRoot) {
  const packageDir = join(wsRoot, 'ABDSharedCode', 'MidiKeyboard');
  const mounters = new Set();
  const dependents = new Set();
  let visited = 0;

  const projectOf = (absPath) => {
    const first = relative(wsRoot, absPath).split(sep)[0];
    return !first || first === '.' ? '(workspace root)' : first;
  };

  const walk = (dir, depth) => {
    if (depth > 8 || visited > 60000) return;
    let entries;
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      const abs = join(dir, e.name);
      if (e.isDirectory()) {
        if (abs === packageDir) continue;
        if (e.name.startsWith('.') || e.name.startsWith('_') || SKIP_DIRS.has(e.name)) continue;
        walk(abs, depth + 1);
        continue;
      }
      if (!e.isFile()) continue;
      if (e.name === 'package.json') {
        if (depth > 4) continue;
        try {
          const pkg = JSON.parse(readFileSync(abs, 'utf8'));
          const has = pkg.dependencies?.[PKG] || pkg.peerDependencies?.[PKG] || pkg.devDependencies?.[PKG];
          if (pkg.name !== PKG && has) dependents.add(projectOf(abs));
        } catch {
          // package.json roto: lo cubrirá el montaje real si llega a existir.
        }
        continue;
      }
      if (!CODE_EXTS.has(extname(e.name))) continue;
      let size = 0;
      try {
        size = statSync(abs).size;
      } catch {
        continue;
      }
      if (size === 0 || size > 5_000_000) continue;
      visited += 1;
      let text = '';
      try {
        text = readFileSync(abs, 'utf8');
      } catch {
        continue;
      }
      if (SPECIFIER_RE.test(text)) mounters.add(projectOf(abs));
    }
  };

  walk(wsRoot, 0);
  return { mounters, dependents, visited };
}

// ── tests ────────────────────────────────────────────────────────────────────

describe('host strip height (smoke)', () => {
  // Estas TRES capas leen ficheros de la suite. La cuarta no: es el resolutor de
  // CSS contra reglas inventadas, y se ejecuta siempre, porque es lo unico de
  // este fichero que no depende de que haya cuatro repos al lado.
  describe.skipIf(SIN_WORKSPACE)('registro: cada host documentado declara altura definida en su franja', () => {
    for (const host of HOSTS) {
      it(`${host.project} → ${host.stripSelector} resuelve a px`, () => {
        expect(host.stripSelector, `${host.project}: stripSelector vacío en HOSTS`).toBeTruthy();
        expect(host.cssFiles.length, `${host.project}: cssFiles vacío en HOSTS`).toBeGreaterThan(0);

        const vars = new Map();
        const bodies = [];
        for (const rel of host.cssFiles) {
          const abs = join(root, rel);
          expect(
            existsSync(abs),
            `${host.project}: no existe ${rel} — ¿se movió el CSS de la franja? Actualiza HOSTS.`,
          ).toBe(true);
          const css = readFileSync(abs, 'utf8');
          collectVars(css, vars);
          bodies.push(...extractRuleBodies(css, host.stripSelector));
        }

        expect(
          bodies.length,
          `${host.project}: ninguna regla '${host.stripSelector}' en ${host.cssFiles.join(', ')}`
          + ' — la franja cambió de nombre o de fichero; actualiza HOSTS.',
        ).toBeGreaterThan(0);

        const decls = bodies.flatMap((body) => extractHeightDecls(body));
        expect(
          decls.length,
          `${host.project}: '${host.stripSelector}' no declara height — la franja necesita altura`
          + ' DEFINIDA (no min-height ni cadena de estiramientos: WebView2 colapsa las teclas, lección 8.3).',
        ).toBeGreaterThan(0);

        for (const raw of decls) {
          const px = resolveHeight(raw, vars);
          expect(
            px,
            `${host.project}: height '${raw}' en '${host.stripSelector}' no resuelve a px definido`
            + ' (100% / auto / var sin resolver = franja sin altura → teclas a fracción de píxel en WebView2).',
          ).toBeTypeOf('number');
          expect(
            px,
            `${host.project}: height ${px}px fuera del rango saneable [${MIN_STRIP_PX}, ${MAX_STRIP_PX}]px`,
          ).toBeGreaterThanOrEqual(MIN_STRIP_PX);
          expect(
            px,
            `${host.project}: height ${px}px fuera del rango saneable [${MIN_STRIP_PX}, ${MAX_STRIP_PX}]px`,
          ).toBeLessThanOrEqual(MAX_STRIP_PX);
          if (host.stripHeightPx !== undefined) {
            expect(
              px,
              `${host.project}: la franja pasa de ${host.stripHeightPx}px a ${px}px — `
              + 'cambiar la altura del keybed en un host es un cambio de layout deliberado '
              + '(contrato WebView2), no un efecto colateral de la migracion.',
            ).toBe(host.stripHeightPx);
          }
        }
      });
    }
  });

  describe.skipIf(SIN_WORKSPACE)('descubrimiento: ningún host del workspace queda sin registrar', () => {
    it(`todo proyecto que monta o declara ${PKG} está en HOSTS`, () => {
      const { mounters, dependents } = discoverHosts(root);
      const discovered = new Set([...mounters, ...dependents]);
      const registered = new Set(HOSTS.map((h) => h.project));

      const unregistered = [...discovered].filter((p) => !registered.has(p)).sort();
      const stale = [...registered].filter((p) => !discovered.has(p)).sort();

      expect(
        unregistered,
        `Hosts sin registrar: ${unregistered.join(', ')} — añádelos a HOSTS con stripSelector y`
        + ` cssFiles; todo synth que monta ${PKG} debe declarar altura en su franja.`,
      ).toEqual([]);
      expect(
        stale,
        `HOSTS incluye proyectos que ya no montan ni declaran ${PKG}: ${stale.join(', ')}`
        + ' — retíralos del registro (o el descubrimiento no los ve: revisa SKIP_DIRS).',
      ).toEqual([]);
    }, 30000);
  });

  describe.skipIf(SIN_WORKSPACE)('montaje real: cada host monta el paquete, no solo lo declara', () => {
    /** Ficheros de codigo del proyecto (mismos ignores que el descubrimiento). */
    function projectSources(project) {
      const files = [];
      const walk = (dir, depth) => {
        if (depth > 8) return;
        let entries;
        try {
          entries = readdirSync(dir, { withFileTypes: true });
        } catch {
          return;
        }
        for (const e of entries) {
          const abs = join(dir, e.name);
          if (e.isDirectory()) {
            if (e.name.startsWith('.') || e.name.startsWith('_') || SKIP_DIRS.has(e.name)) continue;
            walk(abs, depth + 1);
            continue;
          }
          if (e.isFile() && CODE_EXTS.has(extname(e.name))) files.push(abs);
        }
      };
      walk(join(root, project), 0);
      return files;
    }

    for (const host of HOSTS) {
      it(`${host.project} importa el paquete y llamada a createKeyboard en el mismo fichero`, () => {
        const importers = projectSources(host.project).filter((abs) => {
          const text = readFileSync(abs, 'utf8');
          return SPECIFIER_RE.test(text);
        });

        expect(
          importers.length,
          `${host.project}: ningun fichero importa ${PKG} — el host declara la dependencia `
          + 'pero renderiza su propio keybed (el caso que este contrato prohibe).',
        ).toBeGreaterThan(0);

        const mounts = importers.filter((abs) => MOUNT_CALL_RE.test(readFileSync(abs, 'utf8')));
        expect(
          mounts.length,
          `${host.project}: importa ${PKG} pero no llama a createKeyboard() en el mismo fichero — `
          + 'la dependencia esta declarada y el keybed es propio.',
        ).toBeGreaterThan(0);
      }, 30000);
    }
  });

  describe('contrato: la semantica del resolver no da falsos verdes', () => {
    const resolveFrom = (body, varsText = '') => {
      const vars = collectVars(varsText ? `:root { ${varsText} }` : '');
      const bodies = extractRuleBodies(`.strip { ${body} }`, '.strip');
      const decls = bodies.flatMap((b) => extractHeightDecls(b));
      return { decls, px: decls.length ? resolveHeight(decls[0], vars) : null };
    };

    it('px literal es altura definida', () => {
      expect(resolveFrom('height: 155px').px).toBe(155);
    });

    it('porcentaje, auto y var sin resolver NO son altura definida', () => {
      expect(resolveFrom('height: 100%').px).toBeNull();
      expect(resolveFrom('height: auto').px).toBeNull();
      expect(resolveFrom('height: var(--missing)').px).toBeNull();
      expect(resolveFrom('height: calc(100% - 10px)').px).toBeNull();
    });

    it('calc sobre variables (con fallback) es altura definida', () => {
      const { px } = resolveFrom(
        'height: calc(var(--abd-keys-h) - var(--keys-note-h, 14px))',
        '--abd-keys-h: 120px',
      );
      expect(px).toBe(106);
    });

    it('solo min-height no declara height', () => {
      expect(resolveFrom('min-height: 200px').decls).toEqual([]);
    });

    it('un selector no matchea sus pseudo-elementos', () => {
      const bodies = extractRuleBodies(
        '.strip::after { height: 10px } .strip { height: 155px }',
        '.strip',
      );
      expect(bodies).toHaveLength(1);
      expect(extractHeightDecls(bodies[0])).toEqual(['155px']);
    });
  });
});
