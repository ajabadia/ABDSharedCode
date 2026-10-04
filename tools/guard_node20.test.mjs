// Tests del guard de Node 20: tools/guard_node20.py.
//
//   node --test tools/guard_node20.test.mjs
//
// POR QUE HAY UN FICHERO DEDICADO Y NO UNOS MAS EN guard_atributos.test.mjs
// ============================================================================
// Aquel fichero es el guard de `.gitattributes` y el de pipefail. Este es otro
// guard, con otro sujeto y otra tabla, y meterlo en un fichero cuyo nombre no
// lo dice seria el primer paso a que nadie sepa donde buscar.
//
// Y por que los negativos se escriben aqui y no se dejan para el CI: un guard
// que solo se ha visto pasar no se sabe si pasa. La mitad de estas pruebas son
// workflows que TIENEN que salir con 1. Si el guard se rompiera y saliera
// siempre con 0, estas se caerian, y con ellas el rojo.
//
// EL CASO QUE MAS INTERESA
// ========================
// `actions/upload-artifact@v5` y `actions/download-artifact@v6` siguen siendo
// node20. Una regla de la forma "v5 o menos es node20" los dejaria pasar. Estos
// dos tests son los que pagan la tabla del guard, y por eso estan aqui y no
// como nota en el codigo.
//
// SIN DEPENDENCIAS: `node:test` y `node:assert`, que vienen en el runtime. Este
// repo no tiene `package.json` de tooling y el guard no debe empezar a
// depender de un `pnpm install` que nadie recuerda hacer.

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

const AQUI = resolve(import.meta.dirname);
const GUION = resolve(AQUI, 'guard_node20.py');

const PY = (() => {
  for (const cand of (process.platform === 'win32' ? ['python', 'python3'] : ['python3', 'python'])) {
    if (spawnSync(cand, ['--version'], { encoding: 'utf8' }).status === 0) return cand;
  }
  return null;
})();
const SIN_PY = PY === null && 'no hay python en esta maquina';

/** Monta una suite de un solo uso y corre el guard. (codigo, salida) */
function medir (workflows, extra = []) {
  const d = mkdtempSync(join(tmpdir(), 'node20-'));
  try {
    const raiz = join(d, 'suite');
    for (const [repo, nombre, txt] of workflows) {
      const wf = join(raiz, repo, '.github', 'workflows');
      mkdirSync(wf, { recursive: true });
      writeFileSync(join(wf, nombre), txt, 'utf8');
    }
    const r = spawnSync(PY, [GUION, '--raiz', raiz, ...extra], { encoding: 'utf8' });
    return { code: r.status, out: (r.stdout || '') + (r.stderr || '') };
  } finally {
    rmSync(d, { recursive: true, force: true });
  }
}

/** Un workflow de checkout, con los repos que se le digan. */
function pinsWorkflow (...repos) {
  const l = ['name: pins', 'on: [push]', 'jobs:', '  guard:', '    steps:'];
  for (const repo of repos) {
    l.push(`      - uses: actions/checkout@v7`);
    l.push(`        with:`);
    l.push(`          repository: ajabadia/${repo}`);
    l.push(`          ref: ${'a'.repeat(40)}`);
    l.push(`          path: ${repo}`);
  }
  return l.join('\n') + '\n';
}

/** Corre el guard con un workflow de pines escrito a fichero. */
function medirConPins (workflows, repos) {
  const d = mkdtempSync(join(tmpdir(), 'node20-pins-'));
  try {
    const pins = join(d, 'pins.yml');
    writeFileSync(pins, pinsWorkflow(...repos), 'utf8');
    const raiz = join(d, 'suite');
    for (const [repo, nombre, txt] of workflows) {
      const wf = join(raiz, repo, '.github', 'workflows');
      mkdirSync(wf, { recursive: true });
      writeFileSync(join(wf, nombre), txt, 'utf8');
    }
    const r = spawnSync(PY, [GUION, '--raiz', raiz, '--pins-de', pins], { encoding: 'utf8' });
    return { code: r.status, out: (r.stdout || '') + (r.stderr || '') };
  } finally {
    rmSync(d, { recursive: true, force: true });
  }
}

/** Un workflow de la forma real de GitHub, con N pasos `uses:`. */
function workflow (...pins) {
  const l = ['name: prueba', 'on: [push]', 'jobs:', '  uno:', '    runs-on: ubuntu-latest', '    steps:'];
  pins.forEach((pin, i) => {
    l.push(`      - name: paso ${i + 1}`);
    l.push(`        uses: ${pin}`);
  });
  return l.join('\n') + '\n';
}

describe('lo que TIENE que salir con 1', () => {
  it('un checkout@v4 es node20 y se nombra el repo, el fichero y la linea',
    { skip: SIN_PY }, () => {
      const { code, out } = medir([['ABDEep', 'webui-ci.yml', workflow('actions/checkout@v4')]]);
      assert.equal(code, 1);
      assert.match(out, /ABDEep\/\.github\/workflows\/webui-ci\.yml:8/);
      assert.match(out, /actions\/checkout@v4 corre en node20/);
      assert.match(out, /arreglo: sube a actions\/checkout@v5 o mas/);
    });

  it('upload-artifact@v5: node20 aunque el numero semble moderno', { skip: SIN_PY }, () => {
    const { code, out } = medir([['ABDAudioLab', 'w.yml', workflow('actions/upload-artifact@v5')]]);
    assert.equal(code, 1);
    assert.match(out, /upload-artifact@v5 corre en node20/);
    assert.match(out, /v6 o mas/);
  });

  it('download-artifact@v6: el corte de esta accion es mas alto que el de las demas',
    { skip: SIN_PY }, () => {
      const { code, out } = medir([['ABDAudioLab', 'w.yml', workflow('actions/download-artifact@v6')]]);
      assert.equal(code, 1);
      assert.match(out, /download-artifact@v6 corre en node20/);
    });

  it('download-artifact@v7 NO se toca: es el primer major que ya es node24',
    { skip: SIN_PY }, () => {
      const { code } = medir([['ABDAudioLab', 'w.yml', workflow('actions/download-artifact@v7')]]);
      assert.equal(code, 0);
    });

  it('un pin mas viejo que el ultimo node20 conocido tambien se cae, y lo dice',
    { skip: SIN_PY }, () => {
      const { code, out } = medir([['ABDEep', 'w.yml', workflow('actions/checkout@v2')]]);
      assert.equal(code, 1);
      assert.match(out, /anterior al ultimo node20 conocido/);
    });

  it('una accion que la tabla no conoce sale con 1, no con un 0 deenbargo',
    { skip: SIN_PY }, () => {
      const { code, out } = medir([['ABDEep', 'w.yml', workflow('gente/nunca-oida@v1')]]);
      assert.equal(code, 1);
      assert.match(out, /no esta en la tabla del guard/);
    });

  it('cambiar el pin de la exenta la deja de estar exenta', { skip: SIN_PY }, () => {
    const ok = medir([['ABDBankManager', 'sign-release.yml', workflow('ilammy/msvc-dev-cmd@v1')]]);
    assert.equal(ok.code, 0);
    const ko = medir([['ABDBankManager', 'sign-release.yml', workflow('ilammy/msvc-dev-cmd@v2')]]);
    assert.equal(ko.code, 1);
    assert.match(ko.out, /exenta solo en v1/);
  });
});

describe('lo que tiene que salir con 0', () => {
  it('la exenta se acepta y se IMPRIME, que es lo que la hace vigilada',
    { skip: SIN_PY }, () => {
      const { code, out } = medir([['ABDBankManager', 'w.yml', workflow('ilammy/msvc-dev-cmd@v1')]]);
      assert.equal(code, 0);
      assert.match(out, /exenta: ilammy\/msvc-dev-cmd@v1/);
    });

  it('un `uses:` de comentario no cuenta: un workflow comentado no ejecuta nada',
    { skip: SIN_PY }, () => {
      const txt = workflow('actions/checkout@v7').replace(
        '        uses: actions/checkout@v7',
        '        # uses: actions/checkout@v4');
      assert.equal(medir([['ABDEep', 'w.yml', txt]]).code, 0);
    });

  it('los majors que ya son node24 se aceptan todos juntos', { skip: SIN_PY }, () => {
    const { code } = medir([['ABDAudioLab', 'w.yml', workflow(
      'actions/checkout@v7', 'actions/setup-node@v7', 'actions/cache@v6',
      'actions/upload-artifact@v7', 'actions/download-artifact@v8',
      'actions/setup-python@v7', 'pnpm/action-setup@v6.1.0',
      'microsoft/setup-msbuild@v3', 'jwlawson/actions-setup-cmake@v2')]]);
    assert.equal(code, 0);
  });

  it('una accion local es composite y no declara node: contarla seria un falso positivo',
    { skip: SIN_PY }, () => {
      const { code } = medir([['ABDEep', 'w.yml',
        workflow('ajabadia/ABDSharedCode/.github/actions/pnpm-workspace-bootstrap@master')]]);
      assert.equal(code, 0);
    });

  it('un pin por SHA no se puede comparar con una major, y se imprime sin fallar',
    { skip: SIN_PY }, () => {
      const { code, out } = medir([['ABDEep', 'w.yml', workflow('gente/nunca-oida@abc123def')]]);
      assert.equal(code, 0);
      assert.match(out, /SIN COMPROBAR/);
      assert.match(out, /gente\/nunca-oida@abc123def/);
    });
});

describe('cuando no se puede mirar, que es distinto de que este limpio', () => {
  it('una raiz que no existe sale con 2, no con 0', { skip: SIN_PY }, () => {
    const r = spawnSync(PY, [GUION, '--raiz', join(tmpdir(), 'no-existe-esta-cosa')], { encoding: 'utf8' });
    assert.equal(r.status, 2);
  });

  it('una raiz sin workflows sale con 2, porque un 0 aqui diria que la suite esta limpia',
    { skip: SIN_PY }, () => {
      const d = mkdtempSync(join(tmpdir(), 'node20-vacio-'));
      try {
        const raiz = join(d, 'suite');
        mkdirSync(join(raiz, 'ABDEep'), { recursive: true });
        const r = spawnSync(PY, [GUION, '--raiz', raiz], { encoding: 'utf8' });
        assert.equal(r.status, 2);
        assert.match((r.stdout || '') + (r.stderr || ''), /no se ha medido nada/);
      } finally {
        rmSync(d, { recursive: true, force: true });
      }
    });

    it('un --pins-de que no existe sale con 2', { skip: SIN_PY }, () => {
      const { code } = medir([['ABDEep', 'w.yml', workflow('actions/checkout@v7')]],
        ['--pins-de', join(tmpdir(), 'no-existe-este-workflow.yml')]);
      assert.equal(code, 2);
    });
});

describe('la lista de checkout del workflow, que es una copia y se queda vieja sola', () => {
    it('si el workflow clona todos los repos con workflows, sale con 0', { skip: SIN_PY }, () => {
      const { code, out } = medirConPins(
        [['ABDEep', 'w.yml', workflow('actions/checkout@v7')],
          ['ABDNeural', 'w.yml', workflow('actions/checkout@v7')]],
        ['ABDEep', 'ABDNeural']);
      assert.equal(code, 0);
      assert.match(out, /los 2 repos que tienen workflows estan todos en pins\.yml/);
    });

    it('un repo con workflows que NO esta en la lista se nombra: ese es el verde que no mira',
      { skip: SIN_PY }, () => {
        const { code, out } = medirConPins(
          [['ABDEep', 'w.yml', workflow('actions/checkout@v7')],
            ['ABDNeural', 'w.yml', workflow('actions/checkout@v7')]],
          ['ABDEep']);
        assert.equal(code, 1);
        assert.match(out, /REPOS QUE TIENEN WORKFLOWS Y NO ESTAN EN/);
        assert.match(out, /^\s+ABDNeural$/m);
        assert.match(out, /no los mediria/);
      });
});