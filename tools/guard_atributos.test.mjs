// Pruebas del guard de `.gitattributes`.
//
//   node --test tools/guard_atributos.test.mjs
//
// POR QUÉ EL NOMBRE DEL FICHERO Y NO EL DIRECTORIO. La orden de antes era
// `node --test tools/`, y funciona o no según la versión de Node en un punto que
// no se ve. Hasta la 20, `node --test` recibía un DIRECTORIO y lo buscaba
// recursivamente. Desde la 21 recibe PATRONES GLOB, y el glob `tools` casa con el
// propio directorio: Node lo carga como módulo y contesta
// `Error: Cannot find module '.../tools'` (MODULE_NOT_FOUND). No es que el
// directorio no tenga tests, es que el argumento dejó de ser un directorio.
//
// Medido en la 24.21, en este repo: `node --test tools/` sale 1 con
// MODULE_NOT_FOUND; `node --test tools/*.test.mjs` sale 0 con los 37 de 37. Y
// entrecomillado tampoco vale como arreglo general: `node --test
// "tools/**/*.test.mjs"` funciona en la 21+ y rompe en la 20, donde el argumento
// se toma literal. Ninguna forma con directorio o con glob vale en las dos, y el
// CI usa la 20 mientras que la máquina de desarrollo ya va por la 24: cualquier
// forma con globs pasa en local y falla en CI, o al revés. Un nombre de fichero
// explícito es la única que no depende de la versión, así que es la que se
// documenta.
//
// El precio de esa elección es que los ficheros hay que NOMBRARLOS, y un test que
// no se nombra no se ejecuta nunca: verde, sin hacer nada, que es el peor
// resultado posible porque parece una comprobación. Por eso el último bloque
// comprueba que no haya más `*.test.mjs` en `tools/` que los que nombra esta
// cabecera. Añadir uno sin actualizar la cabecera es un test que nadie lee nunca.
//
// SIN DEPENDENCIAS, a propósito. Estas herramientas NO están dentro del workspace
// pnpm y no declaran nada: `node:test` y `assert` vienen en el runtime, así que se
// ejecutan con `node --test` y sin `pnpm install`. La raíz del repo sí tiene ya un
// `package.json` (el del workspace, para MidiKeyboard) y MidiKeyboard sí usa vitest,
// pero esto es otra cosa: meter un runner para veinte tests sería meter una cadena
// de dependencias en un guard que vigila los binarios, y ese guard acabaría
// dependiendo de un `pnpm install` que nadie recuerda hacer.
//
// Lo que hay aquí es más pequeño y más duro: `node:test` viene en el runtime,
// `assert` viene en el runtime, y las funciones puras de `guard_atributos.mjs`
// se pueden dar reglas INVENTADAS y comprobar que las detecta. Eso es lo que
// distingue una prueba de una fotografía.
//
// Y EL ORDEN DE ESTE FICHERO. Primero el motor de patrones con reglas
// inventadas, luego las señas, luego la dirección de la declaración, y solo al
// final el repo real. Al revés, un fallo en el motor se manifestaría como «el
// repo tiene dos artefactos sin declarar», que es un diagnóstico que manda a
// mirar el fichero equivocado: el mismo fallo caro que hizo falta `gitSeguro`
// para arreglar en ABDEep.

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import {
  copyFileSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync,
  rmSync, writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

import {
  UMBRAL_PESADO, patronARegex, reglasDe, cubreLa, reglasInertes,
  senasDeArtefacto, atributoEsperado, descubreArtefactos, artefactosSinDeclarar
} from './guard_atributos.mjs';
import { GIT, gitSeguro, leerTrackeados, RAIZ } from './git_seguro.mjs';
import { informe } from './comprobar_atributos.mjs';

// ─────────────────────────────────────────────────────────────────────────
// EL MOTOR DE PATRONES, CON REGLAS QUE NO ESTAN EN NINGUN `.gitattributes`
// ─────────────────────────────────────────────────────────────────────────

describe('el motor de patrones, que es la parte que no puede cambiar', () => {
  it('`*` no cruza el `/`, que es el fallo que dejo inerte la regla de ABDEep', () => {
    // El caso histórico: la regla de los bancos se leía como una protección y
    // no cubría ni uno, porque estaban un nivel más abajo.
    assert.equal(patronARegex('resources/banks/*.syx').test('resources/banks/FABRICANTE/A.syx'), false);
    assert.equal(patronARegex('resources/banks/*.syx').test('resources/banks/A.syx'), true);
  });

  it('`**` sí cruza directorios, y se come el `/` que lo separa', () => {
    assert.equal(patronARegex('tools/**/*.pyc').test('tools/audit/__pycache__/x.pyc'), true);
    assert.equal(patronARegex('tools/**').test('tools/audit/__pycache__/x.pyc'), true);
  });

  it('un patrón SIN barra no va anclado a la raíz: `*.pyc` matchea en cualquier directorio', () => {
    // El caso contrario al de los bancos, y el que hace falta aquí, porque los
    // `.pyc` viven en `__pycache__/`.
    assert.equal(patronARegex('*.pyc').test('Certification/__pycache__/x.pyc'), true);
    assert.equal(patronARegex('/x.pyc').test('Certification/__pycache__/x.pyc'), false);
  });

  it('el punto se escapa, para que un nombre con punto no case con cualquier cosa', () => {
    assert.equal(patronARegex('*.gen.cpp').test('registry.genXcpp'), false);
    assert.equal(patronARegex('*.gen.cpp').test('registry.gen.cpp'), true);
  });

  it('`?` casa un carácter y solo uno, y nunca una barra', () => {
    assert.equal(patronARegex('a?c').test('abc'), true);
    assert.equal(patronARegex('a?c').test('ac'), false);
    assert.equal(patronARegex('a?c').test('a/c'), false);
  });

  it('cubreLa responde "qué ficheros cubre ESTA regla", que es la pregunta que git no sabe', () => {
    const regla = { patron: 'tools/*.pyc', atributos: ['binary'] };

    assert.deepEqual(
      cubreLa(regla, ['tools/x.pyc', 'tools/__pycache__/x.pyc', 'docs/x.pyc']),
      ['tools/x.pyc']
    );
  });
});

// ─────────────────────────────────────────────────────────────────────────
// LAS SEÑAS: qué se considera artefacto, y por qué
// ─────────────────────────────────────────────────────────────────────────

describe('las tres señas, y que ninguna basta por sí sola', () => {
  it('binario por extensión, que es lo que encuentra los `.pyc` y los `.gif`', () => {
    // Un `.pyc` acumula DOS senas: binario por lo que es, generado por lo que es.
    // Anotar solo una seria la senas mas pobre de las dos.
    assert.deepEqual(senasDeArtefacto({ ruta: 'a/b.pyc', bytes: 10 }), ['binario', 'generado']);
    assert.deepEqual(senasDeArtefacto({ ruta: 'demo.gif', bytes: 10 }), ['binario']);
    assert.deepEqual(senasDeArtefacto({ ruta: 'build/x.obj', bytes: 10 }), ['binario']);
  });

  it('binario por bytes NUL, aunque la extensión no lo diga', () => {
    // Un `.dat` de un toolchain no lo declara nadie y sí lleva NUL. Si el guard
    // solo mirase extensiones, ese fichero entraría por la puerta de atrás.
    // Los bytes NUL viajan en `bytes` y como STRING, que es como los lee el
    // runner: un binario no se decodifica a utf8, se miran los primeros 8 KB.
    const conNul = { ruta: 'build/firmware.dat', bytes: 'AB\u0000\u0000' };

    assert.deepEqual(senasDeArtefacto(conNul), ['binario']);
  });

  it('voluminoso a partir del umbral, y no antes', () => {
    assert.deepEqual(
      senasDeArtefacto({ ruta: 'x.json', bytes: UMBRAL_PESADO - 1 }),
      []
    );
    assert.deepEqual(
      senasDeArtefacto({ ruta: 'x.json', bytes: UMBRAL_PESADO + 1 }),
      ['voluminoso']
    );
  });

  it('generado por la cabecera AUTO-GENERATED, en las cinco primeras líneas', () => {
    const linea = '// AUTO-GENERATED. No editar a mano.';
    const caben = ['a\n'.repeat(4) + linea, 'b'];
    const noCaben = ['a\n'.repeat(6) + linea, 'b'];

    assert.deepEqual(senasDeArtefacto({ ruta: 'x.cpp', contenido: caben.join('\n') }), ['generado']);
    assert.deepEqual(senasDeArtefacto({ ruta: 'x.cpp', contenido: noCaben.join('\n') }), []);
  });

  it('generado por la caché de Python y por el nombre de artefacto', () => {
    assert.deepEqual(senasDeArtefacto({ ruta: 'tools/__pycache__/x.pyc', bytes: 1 }), ['binario', 'generado']);
    assert.deepEqual(senasDeArtefacto({ ruta: 'x.data.json', bytes: 1 }), ['generado']);
    assert.deepEqual(senasDeArtefacto({ ruta: 'Source/x.gen.cpp', bytes: 1 }), ['generado']);
  });

  it('un fuente normal no es artefacto, o el guard no encontraría nada y sería ruido', () => {
    assert.deepEqual(senasDeArtefacto({ ruta: 'DspCore/DspCore.h', bytes: 4096, contenido: '#pragma once' }), []);
    assert.deepEqual(senasDeArtefacto({ ruta: 'tools/audit_unconsumed_sources.py', bytes: 8000, contenido: '#!/usr/bin/env python3' }), []);
  });

  it('descubreArtefactos devuelve la seña que delató a cada uno, que es lo que permite diagnosticar', () => {
    const encontrados = descubreArtefactos([
      { ruta: 'demo.gif', bytes: 300 * 1024 },
      { ruta: 'DspCore/DspCore.h', bytes: 10, contenido: '#pragma once' }
    ]);

    assert.equal(encontrados.length, 1);
    assert.deepEqual(encontrados[0], { ruta: 'demo.gif', senas: ['binario', 'voluminoso'], bytes: 300 * 1024 });
  });
});

// ─────────────────────────────────────────────────────────────────────────
// LA DIRECCIÓN DE LA DECLARACIÓN, que es donde está el daño de verdad
// ─────────────────────────────────────────────────────────────────────────

describe('declarar el atributo equivocado no es declarar', () => {
  it('un binario con `text eol=lf` NO está protegido, y el guard tiene que decirlo', () => {
    // El error que hace un guard `includes()` silencioso: si solo se preguntara
    // "¿tiene ALGUNA regla?", este caso daría verde. Y es el caso que importa:
    // git convertiría el contenido del binario y el fichero quedaría irreconocible.
    const artefactos = [{ ruta: 'demo.gif', senas: ['binario', 'voluminoso'] }];
    const reglas = '*.gif text eol=lf\n';

    assert.equal(artefactosSinDeclarar(artefactos, reglas).length, 1);
    assert.equal(artefactosSinDeclarar(artefactos, reglas)[0].falta, 'binary');
  });

  it('`binary` y `-text` cuentan las dos: lo que importa es que git no toque el contenido', () => {
    const artefactos = [{ ruta: 'demo.gif', senas: ['binario'] }];

    assert.deepEqual(artefactosSinDeclarar(artefactos, '*.gif binary\n'), []);
    assert.deepEqual(artefactosSinDeclarar(artefactos, '*.gif -text\n'), []);
    assert.equal(artefactosSinDeclarar(artefactos, '*.gif text\n').length, 1);
  });

  it('un artefacto de texto necesita eol=lf, y `text` a secas no cuenta', () => {
    const artefactos = [{ ruta: 'x.data.json', senas: ['generado'] }];

    assert.deepEqual(artefactosSinDeclarar(artefactos, '*.data.json text eol=lf\n'), []);
    assert.equal(artefactosSinDeclarar(artefactos, '*.data.json text\n').length, 1);
    assert.equal(artefactosSinDeclarar(artefactos, '').length, 1);
  });

  it('atributoEsperado sale de las SEÑAS, no del nombre', () => {
    assert.equal(atributoEsperado({ senas: ['binario', 'generado'] }), 'binary');
    assert.equal(atributoEsperado({ senas: ['voluminoso'] }), 'text eol=lf');
    assert.equal(atributoEsperado({ senas: ['generado'] }), 'text eol=lf');
  });

  it('la defensa muerde: quitar la regla devuelve el artefacto a la lista de rojos', () => {
    const reglas = '*.gif binary\n*.pyc binary\n*.cpp text eol=lf\n';
    const artefactos = [{ ruta: 'demo.gif', senas: ['binario'] }];

    assert.deepEqual(artefactosSinDeclarar(artefactos, reglas), []);
    assert.equal(artefactosSinDeclarar(artefactos, reglas.replace('*.gif binary\n', '')).length, 1);
  });
});

// ─────────────────────────────────────────────────────────────────────────
// LAS REGLAS INERTES: la otra dirección del mismo problema
// ─────────────────────────────────────────────────────────────────────────

describe('una regla que no cubre nada se lee como una protección y no protege nada', () => {
  // Con un `.cpp` en la lista, `*.cpp` queda cubierta y `*.a` sigue sin
  // cubrir nada: que es exactamente el caso que hay que separar.
  const ficheros = [
    'CMakeLists.txt',
    'DspCore/DspCore.h',
    'DspCore/DspEffectsTests.cpp',
    'docs/README.md'
  ];

  it('una extensión sin ficheros hoy es PREVENTIVA, y no es un error', () => {
    // `*.a binary` no cubre ni un fichero en este repo, y está bien: declara que
    // SI aparece un `.a`, tiene que ser binario. Un guard que exigiera "toda
    // regla cubre algo" marcaría todas las reglas de futuro como error, y lo
    // único que se podría hacer con ese guard es apagarlo.
    const inertes = reglasInertes(reglasDe('*.a binary\n*.cpp text eol=lf\n'), ficheros);

    assert.deepEqual(inertes.errores, []);
    assert.equal(inertes.preventivas.length, 1);
    assert.equal(inertes.preventivas[0].patron, '*.a');
  });

  it('una ruta CONCRETA que no existe es un error, porque o está mal escrita o se movió', () => {
    const inertes = reglasInertes(
      reglasDe('fuentes/plataformas/windows.cpp text eol=lf\n*.a binary\n'),
      ficheros
    );

    assert.equal(inertes.errores.length, 1);
    assert.equal(inertes.errores[0].patron, 'fuentes/plataformas/windows.cpp');
  });

  it('`CMakeLists.txt` sí está en la lista, y por eso no se queja de ella', () => {
    const inertes = reglasInertes(reglasDe('CMakeLists.txt text eol=lf\n'), ficheros);

    assert.deepEqual(inertes.errores, []);
  });
});

// ─────────────────────────────────────────────────────────────────────────
// EL REPO REAL, que es donde el guard tiene que tener razón
// ─────────────────────────────────────────────────────────────────────────

// El informe se calcula UNA vez y a ambito de modulo, porque lo usan dos
// `describe` distintos. Leerlo dos veces haria que un fichero anadido entre
// medias diera dos respuestas distintas en la misma pasada, que es la forma
// mas discreta de tener un test que no vale.
const datos = informe();

describe('el repo de verdad, que es el que el guard tiene que proteger', () => {
  const RUTAS = datos.artefactos.map((a) => a.ruta);

  it('la costura: git lee este repo y hay ficheros con los que trabajar', () => {
    // Si `ls-files` devolviera una lista vacía, "ningún artefacto sin declarar"
    // sería cierto y el guard pasaría sin haber mirado nada.
    assert.ok(datos.rutas.length > 100, 'la lista de ficheros trackeados está sospechosamente vacía');
    assert.ok(datos.reglas.length > 0);
  });

  it('descubre los artefactos de aquí, que son un `.gif` y un fuente grande', () => {
    // Un MÍNIMO y no una lista cerrada, que es la promesa del descubrimiento:
    // un `.obj` nuevo tiene que entrar en el recuento sin que nadie edite este
    // test.
    assert.ok(RUTAS.length >= 2, 'se esperaban al menos 2 artefactos, hay ' + RUTAS.length);

    for (const ruta of [
      'MidiKeyboard/demo/keyboard-demo.gif',
      'DspEffects/DspEffectsTests.cpp'
    ]) {
      assert.ok(RUTAS.includes(ruta), 'el guard no descubrió ' + ruta);
    }
  });

  it('el `.gif` se delata por contenido y por tamaño, y el fuente grande solo por tamaño', () => {
    const gif = datos.artefactos.find((a) => a.ruta === 'MidiKeyboard/demo/keyboard-demo.gif');
    const grande = datos.artefactos.find((a) => a.ruta === 'DspEffects/DspEffectsTests.cpp');

    assert.deepEqual(gif.senas, ['binario', 'voluminoso']);
    assert.deepEqual(grande.senas, ['voluminoso']);
  });

  it('TODO artefacto está declarado en .gitattributes, con el atributo que le toca', () => {
    // Esta es la puerta. El día que alguien commitee un `.obj` o un banco sin
    // declararlo, el rojo sale aquí y no en una compilación que nadie entienda.
    assert.deepEqual(
      datos.sinDeclarar.map((a) => a.ruta + ' necesita ' + a.falta),
      []
    );
  });

  it('no hay ninguna caché de Python trackeada, que es lo que `.gitignore` ya prohibía', () => {
    // `.gitignore` no aplica a lo que ya está trackeado: cuando se escribieron
    // las reglas `__pycache__/` y `*.pyc`, el `.pyc` de `Certification/` ya
    // estaba dentro desde antes, y por eso las reglas no lo expulsaron. Este
    // test es el que se encarga de que no vuelva a pasar.
    const dePython = datos.artefactos.filter((a) => a.ruta.endsWith('.pyc'));

    assert.deepEqual(dePython.map((a) => a.ruta), []);
    assert.equal(
      datos.rutas.some((r) => r.includes('__pycache__/')),
      false,
      'hay ficheros de __pycache__/ trackeados, y .gitignore no puede expulsarlos'
    );
  });

  it('ninguna regla de ruta concreta está inerte', () => {
    assert.deepEqual(datos.inertes.map((r) => r.cruda), []);
  });

  it('y hay reglas preventivas, que es lo que hace que el guard sea tolerable', () => {
    // Si esto fuera 0, el guard solo estaría comprobando cosas que no existen
    // y valdría menos de lo que parece.
    assert.ok(datos.preventivas.length > 0);
  });

  it('el guard y git dicen lo mismo sobre lo que es binario', () => {
    assert.deepEqual(datos.desacuerdos, []);
  });
});

// ─────────────────────────────────────────────────────────────────────────
// EL CONTRAPESO: `git check-attr` es la autoridad, y hay que contrastar
// ─────────────────────────────────────────────────────────────────────────

describe('git check-attr, que es la autoridad y se usa de contrapeso', () => {
  it('el helper apunta a ESTE repositorio, y la excepción va antes del subcomando', () => {
    assert.ok(existsSync(resolve(RAIZ, '.git')), 'RAIZ no es la raíz de un repositorio');
    assert.equal(GIT[0], '-c');
    assert.equal(GIT[1], 'safe.directory=' + RAIZ.replace(/\\/g, '/').replace(/\/+$/, ''));
    assert.equal(gitSeguro(['rev-parse', '--is-inside-work-tree']).trim(), 'true');
  });

  it('git declara binario el `.gif`, que es lo que el guard le dice', () => {
    const salida = gitSeguro(['check-attr', 'binary', '--', 'MidiKeyboard/demo/keyboard-demo.gif']);

    assert.match(salida, /binary:\s*set/);
  });

  it('git declara texto y LF el fuente grande, que también es lo que el guard le dice', () => {
    const salida = gitSeguro(['check-attr', 'text', 'eol', '--', 'DspEffects/DspEffectsTests.cpp']);

    assert.match(salida, /text:\s*set/);
    assert.match(salida, /eol:\s*lf/);
  });

  it('la lista de trackeados es la de git, con barras hacia delante', () => {
    const rutas = leerTrackeados();

    assert.ok(rutas.length > 100);
    assert.equal(rutas.some((r) => r.includes('\\')), false);
  });

  it('el `.gitattributes` del disco es el que se está juzgando, y dice lo que dice', () => {
    const texto = readFileSync(resolve(RAIZ, '.gitattributes'), 'utf8');

    assert.equal(texto, datos.reglas);
    assert.match(texto, /^\*\.gif binary$/m);
    assert.match(texto, /^\*\.pyc binary$/m);
  });
});

// ─────────────────────────────────────────────────────────────────────────
// LA ORDEN DOCUMENTADA, QUE ESTA CABECERA DICE Y QUE NADIE COMPRUEBA

// Este guard es de los pocos que vigilan algo del PROPIO repo de test y no de
// `.gitattributes`, y existe por una razón concreta: al documentar el nombre del
// fichero en vez del directorio se ganó algo que no es gratis. Con un directorio,
// `node --test tools/` descubría lo que hubiera; con un nombre, lo que no esté
// nombrado no se ejecuta. Ese es el fallo que este bloque mide.
//
// El fallo es INDETECTABLE fuera de aquí: un `tools/nuevo.test.mjs` que nadie
// nombra no falla, no se queja y no aparece en ningún log. Se ejecuta con 0
// pruebas y con código de salida 0, que es la forma más limpia que hay de
// mentir.
describe('la orden que dice la cabecera de este fichero', () => {
  const AQUI = resolve(RAIZ, 'tools');

  /**
   * Las órdenes de la cabecera, y solo ellas.
   *
   * La línea que documenta la orden es un comentario entero: empieza por `//`,
   * sigue con espacios, y se acaba en `node --test <algo>`. El comentario de más
   * abajo cita órdenes que NO son la documentada (`node --test tools/`, que es
   * justo la que no funciona), y si el patrón fuera más ancho las contaría como
   * si fueran la orden buena y este guard se aprobaría a sí mismo.
   */
  const ordenesDocumentadas = (cabecera) =>
    [...cabecera.matchAll(/^\/\/\s+node --test (\S+)\s*$/gm)].map((m) => m[1]);

  const cabecera = readFileSync(resolve(AQUI, 'guard_atributos.test.mjs'), 'utf8');
  const ordenes = ordenesDocumentadas(cabecera);

  const ficherosDeTest = readdirSync(AQUI).filter((f) => f.endsWith('.test.mjs'));

  it('la cabecera documenta una orden que existe', () => {
    assert.ok(ordenes.length > 0,
      'la cabecera de guard_atributos.test.mjs no dice cómo ejecutar las pruebas');

    for (const orden of ordenes) {
      assert.ok(existsSync(resolve(RAIZ, orden)),
        `la cabecera documenta \`${orden}\` y ese fichero no existe`);
    }
  });

  it('la cabecera no documenta el directorio, que es la orden que ya no funciona', () => {
    // La forma con globs pasa en local (Node 24) y rompe en el CI (Node 20), y al
    // revés también. Nombrar el directorio o un glob es volver a abrir este
    // ticket justo cuando alguien suba la versión de Node en el workflow.
    const parsanas = ordenes.filter((o) => o === 'tools' || o === 'tools/' || o.includes('*'));

    assert.deepEqual(parsanas, [],
      'forma documentada que depende de la versión de Node: '
      + `${parsanas.join(', ')}. Usa el nombre del fichero.`);
  });

  it('cada fichero de test de tools/ aparece en la orden documentada', () => {
    const sinNombrar = ficherosDeTest.filter(
      (f) => !ordenes.some((o) => o.replace(/\\/g, '/').endsWith('/' + f) || o === f));

    assert.deepEqual(sinNombrar, [],
      `${sinNombrar.length} fichero(s) de test que la cabecera no nombra, y por tanto que `
      + 'nadie ejecuta: añade el nombre a la cabecera de este fichero.');
  });
});


// El verde falso: un paso que sale en verde porque `tee` se comio el codigo de
// salida. Va aqui, y no en un `guard_pipefail.test.mjs` propio, por el mismo motivo
// que el bloque de la foto: el guard de la cabecera de mas arriba obliga a que TODO
// `*.test.mjs` de tools/ este nombrado en esa cabecera, y la cabecera solo admite una
// orden con UN solo nombre. Anadir un segundo fichero de test obligaria a cambiar ese
// patron, que es justo el patron que impide que ese guard se apruebe a si mismo.
describe('el verde falso, que es un pipe que se come el codigo de salida', () => {
  const PY = (() => {
    for (const cand of (process.platform === 'win32' ? ['python', 'python3'] : ['python3', 'python'])) {
      const r = spawnSync(cand, ['--version'], { encoding: 'utf8' });
      if (r.status === 0) return cand;
    }
    return null;
  })();

  const GUION = resolve(RAIZ, 'tools', 'guard_pipefail.py');
  const WORKFLOW = resolve(RAIZ, '.github', 'workflows', 'shared-code-ci.yml');
  const SIN_PY = PY === null && 'no hay python en esta maquina';

  /** Un workflow con UN paso, de la forma real de GitHub. */
  function workflow (nombre, cuerpo, shell) {
    const l = [
      'name: prueba',
      'on: [push]',
      'jobs:',
      '  uno:',
      '    runs-on: ubuntu-latest',
      '    steps:',
      `      - name: ${nombre}`,
    ];
    if (shell) l.push(`        shell: ${shell}`);
    l.push('        run: |');
    for (const c of cuerpo) l.push(`          ${c}`);
    return l.join('\n') + '\n';
  }

  /** Monta una suite de un clon y corre el guard. (codigo, salida) */
  function medir (workflows) {
    const d = mkdtempSync(join(tmpdir(), 'pipefail-'));
    try {
      const raiz = join(d, 'suite');
      for (const [repo, nombre, txt] of workflows) {
        const wf = join(raiz, repo, '.github', 'workflows');
        mkdirSync(wf, { recursive: true });
        writeFileSync(join(wf, nombre), txt, 'utf8');
      }
      const r = spawnSync(PY, [GUION, '--check', '--raiz', raiz], { encoding: 'utf8' });
      return { code: r.status, out: (r.stdout || '') + (r.stderr || '') };
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  }

  it('EL CASO QUE CIERRA: `pnpm test | tee log` sin pipefail sale 1, no 0', {
    skip: SIN_PY,
  }, () => {
    // El fallo del primer push, reproducido. Medido en bash antes de escribir el
    // guard: `false | tee /dev/null` sin pipefail sale 0 y con pipefail sale 1, que
    // es la diferencia entre un job verde con `pnpm test` fallando y un job rojo.
    const r = medir([['ABDSharedCode', 'ci.yml',
      workflow('Tests', ['pnpm test 2>&1 | tee "$RUNNER_TEMP/log"'])]]);

    assert.equal(r.code, 1,
      `un \`| tee\` sin pipefail tiene que ser un hallazgo. Salida:\n${r.out}`);
    // Y el motivo tiene que NOMBRAR el paso y el shell, porque "algo fallo" obliga a
    // abrir el workflow a buscar.
    assert.match(r.out, /Tests/, `el informe no nombra el paso:\n${r.out}`);
    assert.match(r.out, /sh/, `el informe no dice que shell ejecuta el paso:\n${r.out}`);
  });

  it('`shell: bash` SOLO ya vale, porque el runner le pone -o pipefail', { skip: SIN_PY }, () => {
    // Este es el caso donde un guard demasiadoplice se pone ROJO sobre el paso que
    // el commit 4f5e6a4 acaba de arreglar. Sale de las reglas del runner
    // (`ScriptHandlerHelpers.cs`), no de una suposicion.
    const r = medir([['ABDSharedCode', 'ci.yml',
      workflow('Tests', ['pnpm test 2>&1 | tee log'], 'bash')]]);
    assert.equal(r.code, 0, `shell: bash ya trae pipefail del runner:\n${r.out}`);
  });

  it('pero `shell: bash -e {0}` NO vale: esos args pisan los del runner', { skip: SIN_PY }, () => {
    // El caso hermano del de arriba, y el que separa un guard medido de uno que
    // mira "la palabra bash" por el fichero. Escribir args propios REEMPLAZA el
    // `--noprofile --norc -e -o pipefail {0}` del runner, y el pipefail se va con el.
    const r = medir([['ABDSharedCode', 'ci.yml',
      workflow('Tests', ['pnpm test 2>&1 | tee log'], 'bash -e {0}')]]);
    assert.equal(r.code, 1, `args propios sin pipefail: tiene que ser hallazgo:\n${r.out}`);
  });

  it('`set -o pipefail` dentro del run tambien vale, con o sin shell:', { skip: SIN_PY }, () => {
    for (const shell of [null, 'bash', 'sh']) {
      const r = medir([['ABDSharedCode', 'ci.yml',
        workflow('Tests', ['set -o pipefail', 'pnpm test 2>&1 | tee log'], shell)]]);
      assert.equal(r.code, 0, `set -o pipefail dentro (shell=${shell}):\n${r.out}`);
    }
  });

  it('las tres formas de `set` que se usan de verdad', { skip: SIN_PY }, () => {
    for (const linea of ['set -o pipefail', 'set -euo pipefail', 'set -uo pipefail']) {
      const r = medir([['ABDSharedCode', 'ci.yml',
        workflow('Tests', [linea, 'pnpm test 2>&1 | tee log'])]]);
      assert.equal(r.code, 0, `${linea} tiene que valer:\n${r.out}`);
    }
  });
  

  it('la palabra "puppeteer" NO es un tee, y el guard no puede marcar por eso', {
    skip: SIN_PY,
  }, () => {
    // Falso POSITIVO real y medido: `grep -n tee ABDEep/.github/workflows/webui-ci.yml`
    // casa en la linea 53, dentro de la palabra "Chromium" de la linea que explica
    // que se omite la descarga de puppeteer. Un guard que nace marcando eso se apaga
    // el primer dia, y a partir de ahi no vigila nada.
    const r = medir([['ABDSharedCode', 'ci.yml',
      workflow('Bootstrap', ['echo se omite el Chromium de puppeteer'])]]);
    assert.equal(r.code, 0, `"puppeteer" no es una tuberia:\n${r.out}`);
  });

  it('un `| tee` que esta en un COMENTARIO no cuenta', { skip: SIN_PY }, () => {
    // El propio workflow de este repo tiene un comentario que dice
    // "`pnpm test | tee log` devuelve el codigo de tee". Si el guard no lo ignorara,
    // el fichero que documenta el fallo seria un hallazgo, y el arreglo seria borrar
    // la documentacion.
    const r = medir([['ABDSharedCode', 'ci.yml', workflow('Tests', [
      '# `pnpm test | tee log` devuelve el codigo de tee, que es 0 siempre',
      'pnpm test',
    ])]]);
    assert.equal(r.code, 0, `un comentario no ejecuta nada:\n${r.out}`);
  });

  it('en pwsh no es hallazgo: el runner le anade exit $LASTEXITCODE', { skip: SIN_PY }, () => {
    // El `FixUpScriptContents` del runner anade `if ((Test-Path variable:LASTEXITCODE))
    // { exit $LASTEXITCODE }` a los scripts de pwsh, asi que el pipe se nota. Y hay un
    // caso real de la suite: el paso de CMake de ABDEep/webui-bundle-ci.yml.
    const r = medir([['ABDSharedCode', 'ci.yml',
      workflow('Configure CMake', ['cmake -B build 2>&1 | tee cfg.log'], 'pwsh')]]);
    assert.equal(r.code, 0, `pwsh propaga el codigo por su cuenta:\n${r.out}`);
  });

  it('EL REPO REAL: su workflow esta limpio', { skip: SIN_PY }, () => {
    // El guard sobre el arbol de trabajo entero, no sobre un fixture.
    const r = spawnSync(PY, [GUION, '--check'], { cwd: RAIZ, encoding: 'utf8' });
    assert.equal(r.status, 0,
      `la suite real da ${r.status}. Si es 1, hay un \`| tee\` sin pipefail:\n`
      + `${r.stdout}${r.stderr}`);
  });

  it('y mira pasos de verdad, no sale en verde por no mirar nada', { skip: SIN_PY }, () => {
    // La contraprueba del `--listar`: un guard en verde que no ha medido nada es el
    // peor resultado posible, porque parece una comprobacion. Se mide que ha visto
    // pasos con codigo de verdad.
    const r = spawnSync(PY, [GUION, '--listar'], { cwd: RAIZ, encoding: 'utf8' });
    assert.equal(r.status, 0, `--listar no compara nada y sale 0:\n${r.stdout}${r.stderr}`);

    const pasos = Number((r.stdout.match(/pasos con codigo ejecutable: (\d+)/) || [])[1]);
    const riesgos = Number(
      (r.stdout.match(/que pueden perder el codigo de salida: (\d+)/) || [])[1]);
    const repos = Number((r.stdout.match(/repos con workflows: (\d+)/) || [])[1]);

    // EL UMBRAL NO ES 100, Y POR QUE NO. Este mismo script corre en dos sitios con
    // layouts distintos: en el arbol de trabajo local ve la suite entera (10 repos),
    // y en el job `tools` de CI ve SOLO este repo, porque ese job no clona a los
    // hermanos. Medido en el run 37046639690: en CI el recuento es 1 repo y 11
    // pasos, no 244. Un umbral escrito para el caso grande hace que este test falle
    // en CI POR MEDIR LO CORRECTO, que es la forma mas rapida de enseñar a ignorar
    // los rojos de un guard.
    //
    // Lo que si es invariante es que ha mirado algo y que no ha encontrado nada.
    assert.ok(repos >= 1, `el guard no ha visto ningun repo:\n${r.stdout}`);
    assert.ok(pasos >= 10,
      `ha mirado ${pasos} pasos, y un workflow entero tiene mas de eso:\n${r.stdout}`);
    assert.equal(riesgos, 0, `este repo tiene ${riesgos} riesgos:\n${r.stdout}`);
  });

  it('una raiz sin workflows: 2, no 0, porque no es un hallazgo', { skip: SIN_PY }, () => {
    const d = mkdtempSync(join(tmpdir(), 'vacio-'));
    try {
      mkdirSync(join(d, 'suite', 'repo'), { recursive: true });
      const r = spawnSync(PY, [GUION, '--check', '--raiz', join(d, 'suite')],
        { encoding: 'utf8' });
      assert.equal(r.status, 2,
        'sin workflows no se ha medido nada: un 0 aqui diria que la suite esta limpia');
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  });

  it('una raiz que no existe: 2, y lo dice', { skip: SIN_PY }, () => {
    const r = spawnSync(PY, [GUION, '--check', '--raiz', join(tmpdir(), 'no-existe-este-dir')],
      { encoding: 'utf8' });
    assert.equal(r.status, 2, 'una raiz inexistente no puede dar un veredicto');
    assert.match((r.stdout || '') + (r.stderr || ''), /no existe/,
      'el motivo tiene que decir que no existe, no solo devolver 2');
  });

  it('un flag desconocido: 2, y se escribe el que si vale', { skip: SIN_PY }, () => {
    const r = spawnSync(PY, [GUION, '--inventado'], { cwd: RAIZ, encoding: 'utf8' });
    assert.equal(r.status, 2);
    const salida = (r.stdout || '') + (r.stderr || '');
    for (const flag of ['--check', '--listar', '--raiz']) {
      assert.ok(salida.includes(flag), `el error no ofrece ${flag}:\n${salida}`);
    }
  });
  

  /** Un workflow con un `defaults: run:` de un tipo dado. (texto) */
  function conDefaults (clave, valor) {
    return [
      'name: prueba',
      'on: [push]',
      'jobs:',
      '  uno:',
      '    runs-on: ubuntu-latest',
      '    defaults:',
      '      run:',
      `        ${clave}: ${valor}`,
      '    steps:',
      '      - name: Tests',
      '        run: |',
      '          pnpm test 2>&1 | tee log',
      '',
    ].join('\n');
  }

  it('`defaults: run: shell: bash` protege a los pasos que no digan shell:', {
    skip: SIN_PY,
  }, () => {
    // La precedencia es la de GitHub y no una suposicion: si un paso dice `shell:`
    // gana, y un `defaults:` solo habla de los pasos que NO digan nada.
    const r = medir([['ABDSharedCode', 'ci.yml', conDefaults('shell', 'bash')]]);
    assert.equal(r.code, 0, `el defaults: run: shell: protege:\n${r.out}`);
  });

  it('y `defaults: run: shell: sh` NO protege, porque sh no trae pipefail', {
    skip: SIN_PY,
  }, () => {
    const r = medir([['ABDSharedCode', 'ci.yml', conDefaults('shell', 'sh')]]);
    assert.equal(r.code, 1, `sh es \`-e {0}\`: sin pipefail:\n${r.out}`);
  });

  it('`defaults: run: working-directory:` sin shell no protege (no es un shell)', {
    skip: SIN_PY,
  }, () => {
    // El caso real de ABDEep/webui-ci.yml: tiene un `defaults: run:
    // working-directory:` y nada de shell. Que el bloque exista no significa que
    // proteja: lo que protege es el `shell:`.
    const r = medir([['ABDSharedCode', 'ci.yml', conDefaults('working-directory', 'ABDEep')]]);
    assert.equal(r.code, 1, `working-directory no es un shell:\n${r.out}`);
  });

  it('un paso NO hereda el `shell:` del paso ANTERIOR', { skip: SIN_PY }, () => {
    // Este es el bug que casi hizo que este guard se aprobara a si mismo. Con la
    // busqueda del `shell:` "hacia atras sin limite", el paso sin `shell:` tomaba el
    // del paso de al lado, que esta en la misma sangria. Medido: con ese bug, el
    // workflow real de este repo con el arreglo de 4f5e6a4 quitado seguia en verde.
    const wf = [
      'name: prueba',
      'on: [push]',
      'jobs:',
      '  uno:',
      '    runs-on: ubuntu-latest',
      '    steps:',
      '      - name: Con bash',
      '        shell: bash',
      '        run: |',
      '          echo hola',
      '      - name: Sin shell, y con tee',
      '        run: |',
      '          pnpm test 2>&1 | tee log',
      '',
    ].join('\n');
    const r = medir([['ABDSharedCode', 'ci.yml', wf]]);
    assert.equal(r.code, 1,
      `el segundo paso no hereda nada: sin shell, el runner usa sh:\n${r.out}`);
    assert.match(r.out, /Sin shell/,
      `el informe tiene que senalar el paso SIN shell, no el de al lado:\n${r.out}`);
  });

  it('el repo real, con el arreglo de 4f5e6a4 quitado, es un hallazgo', { skip: SIN_PY }, () => {
    // La contraprueba del guard entero, con el fichero de verdad y no un fixture. Se
    // copia el workflow real, se le quita el `shell: bash` y el `set -o pipefail` del
    // paso de tests, y tiene que salir 1 nombrando ESE paso. Sin esta prueba, un
    // guard que no mirara nada tambien estaria en verde.
    const real = readFileSync(WORKFLOW, 'utf8');
    const roto = real.replace(
      '        shell: bash\n        run: |\n          set -o pipefail\n',
      '        run: |\n');
    assert.notEqual(roto, real,
      'no se ha podido quitar el arreglo del workflow real: el fixture no serviria');

    const d = mkdtempSync(join(tmpdir(), 'real-'));
    try {
      const wf = join(d, 'suite', 'ABDSharedCode', '.github', 'workflows');
      mkdirSync(wf, { recursive: true });
      writeFileSync(join(wf, 'shared-code-ci.yml'), roto, 'utf8');
      const r = spawnSync(PY, [GUION, '--check', '--raiz', join(d, 'suite')],
        { encoding: 'utf8' });
      const salida = (r.stdout || '') + (r.stderr || '');
      assert.equal(r.status, 1,
        `el workflow real sin la proteccion tiene que ser un hallazgo:\n${salida}`);
      assert.match(salida, /Tests de MidiKeyboard/,
        `tiene que nombrar el paso de tests, no otro:\n${salida}`);
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  });

  it('`runs-on: windows` con `| tee` y sin shell NO es hallazgo (el runner usa pwsh)', {
    skip: SIN_PY,
  }, () => {
    // El caso REAL de la suite, y el que obliga a leer `runs-on:`. El paso "Configure
    // CMake" de ABDEep/webui-bundle-ci.yml hace `cmake ... | tee log` sin declarar
    // shell, pero su job es `windows-2022`: el runner ejecuta pwsh y le anade
    // `exit $LASTEXITCODE`. Sin mirar el `runs-on:`, el guard lo marcaria como fallo
    // sobre un fichero sano, y un guard que nace marcando eso no sobrevive al primer
    // dia.
    const wf = [
      'name: prueba',
      'on: [push]',
      'jobs:',
      '  bundle-in-binary:',
      '    name: El binario lleva el bundle',
      '    runs-on: windows-2022',
      '    steps:',
      '      - name: Configure CMake',
      '        run: |',
      '          cmake -B build 2>&1 | tee build\configure.log',
      '',
    ].join('\n');
    const r = medir([['ABDEep', 'ci.yml', wf]]);
    assert.equal(r.code, 0, `en windows el runner usa pwsh, que propaga el codigo:\n${r.out}`);
  });

  it('y el MISMO paso en `runs-on: ubuntu` SI es hallazgo, porque ahi es sh', {
    skip: SIN_PY,
  }, () => {
    // El caso hermano: el `runs-on:` es lo unico que cambia, y por eso se mira. Sin
    // el `runs-on:` no se puede distinguir, y sin distinguirlos el guard o se come un
    // rojo o se come un falso positivo.
    const l = [
      'name: prueba',
      'on: [push]',
      'jobs:',
      '  uno:',
      '    name: Con nombre, que no es una clave del job',
      '__RUNS_ON__',
      '    steps:',
      '      - name: Configure CMake',
      '        run: |',
      '          cmake -B build 2>&1 | tee build\configure.log',
      '',
    ].join('\n');

    const rojo = medir([['ABDEep', 'ci.yml', l.replace('__RUNS_ON__', '    runs-on: ubuntu-latest')]]);
    assert.equal(rojo.code, 1, `en ubuntu el runner usa sh, que pierde el codigo:\n${rojo.out}`);

    const verde = medir([['ABDEep', 'ci.yml', l.replace('__RUNS_ON__', '    runs-on: windows-2022')]]);
    assert.equal(verde.code, 0, `en windows el runner usa pwsh:\n${verde.out}`);
  });

  it('el nombre del job no se confunde con su `runs-on:`', { skip: SIN_PY }, () => {
    // El `name:` de un job cuelga de el, igual que el `runs-on:`, y los dos estan en la
    // misma sangria. Un parser que se comiera el `name:` por el nombre del job
    // perderia el `runs-on:` de al lado. Este test mete un job con `name:` para que el
    // caso no dependa de que el fichero real lo tenga o no.
    const wf = [
      'name: prueba',
      'on: [push]',
      'jobs:',
      '  uno:',
      '    name: Este nombre es del job, no del workflow',
      '    runs-on: ubuntu-latest',
      '    steps:',
      '      - name: Tests',
      '        run: |',
      '          pnpm test 2>&1 | tee log',
      '',
    ].join('\n');
    const r = medir([['ABDSharedCode', 'ci.yml', wf]]);
    assert.equal(r.code, 1,
      `con runs-on: ubuntu el paso es un hallazgo, y el name: no puedepotentarlo:\n${r.out}`);
  });

  it('el quinto trabajo se vigila a si mismo (un `| tee` suyo, sin pipefail, es 1)', {
    skip: SIN_PY,
  }, () => {
    // ESTE es el test que hace que el trabajo sea una puerta y no un adorno. El paso
    // "El guard ha mirado de verdad" hace `... | tee "$RUNNER_TEMP/pipes.txt"` para no
    // perder el recuento, asi que el guard tiene que MIRARSE a si mismo. Si el paso
    // dejara de llevar `shell: bash` + `set -o pipefail`, este guard dejaria de
    // vigilar el fichero donde vive, que es la forma mas tonta de no vigilar nada.
    const roto = readFileSync(WORKFLOW, 'utf8').replace(
      '        shell: bash\n        run: |\n          set -o pipefail\n'
      + '          python tools/guard_pipefail.py --listar',
      '        run: |\n          python tools/guard_pipefail.py --listar');
    assert.notEqual(roto, readFileSync(WORKFLOW, 'utf8'),
      'no se ha podido quitar la proteccion del paso nuevo: el test no mide nada');

    const d = mkdtempSync(join(tmpdir(), 'self-'));
    try {
      const wf = join(d, 'suite', 'ABDSharedCode', '.github', 'workflows');
      mkdirSync(wf, { recursive: true });
      writeFileSync(join(wf, 'shared-code-ci.yml'), roto, 'utf8');
      const r = spawnSync(PY, [GUION, '--check', '--raiz', join(d, 'suite')],
        { encoding: 'utf8' });
      const salida = (r.stdout || '') + (r.stderr || '');
      assert.equal(r.status, 1,
        `el paso del quinto trabajo sin pipefail tiene que ser un hallazgo:\n${salida}`);
      assert.match(salida, /El guard ha mirado de verdad/,
        `tiene que senalar ese paso:\n${salida}`);
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  });

  it('y el quinto trabajo esta en el workflow, con sus repos clonados', { skip: SIN_PY }, () => {
    // El guard puede pasar en verde sobre un workflow donde el trabajo no existe: no
    // miraria el fichero del todo. Se comprueba que el trabajo esta, y que los
    // hermanos tambien, porque un guard sin hermanos solo vigila este repo.
    const wf = readFileSync(WORKFLOW, 'utf8');
    assert.match(wf, /^ {2}pipes:$/m, 'el quinto trabajo `pipes` no esta en el workflow');
    assert.match(wf, /guard_pipefail\.py --check --raiz "\$GITHUB_WORKSPACE"/,
      'el trabajo no llama al guard con la raiz del workspace');

    for (const repo of ['ABDEep', 'ABDMS2000', 'ABDNeural', 'ABDCZ101', 'ABDSharedAssets']) {
      assert.ok(wf.includes(`repository: ajabadia/${repo}`),
        `el quinto trabajo no clona ${repo}, asi que el guard no lo miraria`);
    }
  });
});


// El laboratorio: como se mide la linea base contra la FOTO y no contra el arbol de
// trabajo. Va aqui, y no en un `regenerar_linea_base.test.mjs` propio, por el mismo
// motivo que los otros dos bloques: el guard de la cabecera de mas arriba obliga a que
// TODO `*.test.mjs` de tools/ este nombrado en esa cabecera, y la cabecera solo
// admite una orden con UN solo nombre.
describe('el laboratorio, que mide la foto y no el arbol de trabajo', () => {
  const PY = (() => {
    for (const cand of (process.platform === 'win32' ? ['python', 'python3'] : ['python3', 'python'])) {
      const r = spawnSync(cand, ['--version'], { encoding: 'utf8' });
      if (r.status === 0) return cand;
    }
    return null;
  })();

  const GUION = resolve(RAIZ, 'tools', 'regenerar_linea_base.py');
  const WORKFLOW = resolve(RAIZ, '.github', 'workflows', 'shared-code-ci.yml');
  const SIN_PY = PY === null && 'no hay python en esta maquina';

  /** Corre el comando contra un workflow y una linea base escribidos a mano. */
  function correr (args) {
    const r = spawnSync(PY, [GUION].concat(args), { cwd: RAIZ, encoding: 'utf8' });
    return { code: r.status, out: (r.stdout || '') + (r.stderr || '') };
  }

  it('un workflow sin ningun checkout con ref: da 2, no 0', { skip: SIN_PY }, () => {
    // Sin repos que clonar no hay fotografia que medir, y un 0 aqui seria un verde
    // que no ha medido nada. Es el mismo codigo 2 que usa el resto de guards del repo.
    const d = mkdtempSync(join(tmpdir(), 'lab-wf-'));
    try {
      const wf = join(d, 'wf.yml');
      writeFileSync(wf, 'name: x\non: [push]\njobs:\n  uno:\n    steps: []\n', 'utf8');
      const r = spawnSync(PY, [GUION, '--raiz', join(d, 'lab'), '--workflow', wf],
        { encoding: 'utf8' });
      const salida = (r.stdout || '') + (r.stderr || '');
      assert.notEqual(r.status, 0,
        `sin SHA que clonar no se puede medir, y no puede salir en verde:\n${salida}`);
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  });

  it('la foto sale del WORKFLOW, y no de la linea base que se quiere regenerar', {
    skip: SIN_PY,
  }, () => {
    // Este es el argumento central del comando, y por eso hay un test que lo fija.
    // Leer los SHA de la linea base seria medirse a uno mismo: si los SHA se
    // equivocaron al subirlos en el workflow, este comando clonaria los mismos
    // repos equivocados y escribiria una linea base que sigue mintiendo, con la
    // ilusion de que se ha arreglado. La foto tiene que salir de lo que CI clona.
    const wf = readFileSync(WORKFLOW, 'utf8');
    const base = readFileSync(resolve(RAIZ, 'tools', 'audit-baseline.json'), 'utf8');

    const shasDelWorkflow = [...wf.matchAll(/repository: ajabadia\/(\w+)[\s\S]{0,80}?ref: ([0-9a-f]{7,40})/g)]
      .map((m) => m[1]);
    assert.ok(shasDelWorkflow.length >= 5,
      `el workflow deberia clonar los 5 hermanos, no ${shasDelWorkflow.length}`);

    // Los SHA del workflow tienen que estar en la linea base: si no, el job `foto`
    // esta en rojo y el comando no puede disfrazarlo.
    const foto = JSON.parse(base).foto || {};
    for (const repo of shasDelWorkflow) {
      assert.ok(foto[repo], `la linea base no declara ${repo}: el job foto lo diria`);
    }
  });

  it('el auditor se ejecuta DENTRO del laboratorio, no el del arbol', { skip: SIN_PY }, () => {
    // `SUITE = <padre del directorio del script>`, asi que ejecutar el script de aqui
    // mediria el ARBOL DE TRABAJO, que es exactamente el fallo que este comando evita.
    // El del arbol ve los cambios sin commitear de otra persona y daria 55 huerfanas
    // falsas; el del clon ve los SHA de la foto y da las 55 de verdad.
    const src = readFileSync(GUION, 'utf8');
    assert.match(src, /raiz, 'ABDSharedCode', 'tools', AUDITOR/,
      'el comando tiene que ejecutar el auditor de DENTRO del clon, no el de SHARED');
    assert.doesNotMatch(src, /subprocess\.run\(\[sys\.executable, os\.path\.join\(HERE/,
      'el comando no puede ejecutar el auditor de tools/ junto a el: mediria el arbol');
  });

  it('el repositorio se clona con fetch --depth 1, no con clone', { skip: SIN_PY }, () => {
    // Medido: un clon completo de ABDSharedAssets son 56 MB de .git y los seis repos
    // tardan minutos; `fetch --depth 1 <sha>` tarda menos de 2 s por repo. Y
    // `clone --depth 1` NO sirve: trae el HEAD del rama, no un SHA cualquiera.
    const src = readFileSync(GUION, 'utf8');
    assert.match(src, /'fetch', '-q', '--depth', '1', 'origin', sha/,
      'los repos tienen que venir por fetch --depth 1 con el SHA explicito');
    assert.doesNotMatch(src, /'clone'/,
      'no debe usar git clone: con --depth 1 no trae un SHA arbitrario');
  });

  it('un repo que no se puede clonar es 2, y el resto no se mide en su lugar', {
    skip: SIN_PY,
  }, () => {
    // Un laboratorio al que le falta un hermano mediria MENOS huerfanas que CI, porque
    // los consumidores que faltan harian parecer huerfanas fuentes que no lo son. Y esa
    // linea base se escribiria en el sitio que dice medir la verdad.
    const d = mkdtempSync(join(tmpdir(), 'lab-fallo-'));
    try {
      // Un workflow con un SHA que no existe en el remoto.
      const wf = join(d, 'wf.yml');
      writeFileSync(wf, [
        'name: x',
        'on: [push]',
        'jobs:',
        '  uno:',
        '    steps:',
        '      - uses: actions/checkout@v4',
        '        with:',
        '          repository: ajabadia/ABDSharedAssets',
        '          ref: 0000000000000000000000000000000000000000',
        '',
      ].join('\n'), 'utf8');

      const r = spawnSync(PY, [GUION, '--raiz', join(d, 'lab'), '--workflow', wf],
        { encoding: 'utf8' });
      const salida = (r.stdout || '') + (r.stderr || '');
      assert.equal(r.status, 2,
        `un SHA inexistente es que no se ha medido nada, no un hallazgo:\n${salida}`);
      assert.match(salida, /NO SE HA PODIDO MONTAR/,
        `el motivo tiene que decir que el laboratorio no se monto:\n${salida}`);
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  });

  it('un flag desconocido: 2, y se escribe el que si vale', { skip: SIN_PY }, () => {
    const r = correr(['--inventado']);
    assert.equal(r.code, 2);
    for (const flag of ['--escribir', '--raiz', '--montar']) {
      assert.ok(r.out.includes(flag), `el error no ofrece ${flag}:\n${r.out}`);
    }
  });

  it('--help sale 0 y documenta los flags que importan', { skip: SIN_PY }, () => {
    const r = correr(['--help']);
    assert.equal(r.code, 0);
    for (const flag of ['--escribir', '--de-la-linea-base', '--reusar', '--montar']) {
      assert.ok(r.out.includes(flag), `--help no menciona ${flag}`);
    }
  });
});
// La foto de la linea base. Va aqui, y no en un `verificar_foto.test.mjs` propio,
// porque el guard de la cabecera de mas arriba obliga a que TODO `*.test.mjs` de
// tools/ este nombrado en esa cabecera, y la cabecera solo admite una orden con
// UN solo nombre (el patron `node --test (\S+)`). Anadir un segundo fichero de
// test obligaria a cambiar ese patron, que es justo el patron que impide que
// este guard se apruebe a si mismo. Menos campos que cambiar, mejor.
describe('la foto de la linea base, que es lo que impide que el trinquete mienta', () => {
  // Windows necesita python y no python3; Unix al reves. Se prueban los dos y
  // se coge el primero que exista, porque un guard que solo funciona en la
  // plataforma de quien lo escribio no es un guard.
  const PY = (() => {
    for (const cand of (process.platform === 'win32' ? ['python', 'python3'] : ['python3', 'python'])) {
      const r = spawnSync(cand, ['--version'], { encoding: 'utf8' });
      if (r.status === 0) return cand;
    }
    return null;
  })();

  const GUION = resolve(RAIZ, 'tools', 'verificar_foto.py');
  const WORKFLOW = resolve(RAIZ, '.github', 'workflows', 'shared-code-ci.yml');
  const LINEA_BASE = resolve(RAIZ, 'tools', 'audit-baseline.json');

  /** Corre el guard contra un workflow y una linea base escritos a dedo. */
  function medir (workflowTxt, baseline) {
    const d = mkdtempSync(join(tmpdir(), 'foto-'));
    try {
      const wf = join(d, 'wf.yml');
      const bl = join(d, 'base.json');
      writeFileSync(wf, workflowTxt, 'utf8');
      writeFileSync(bl, JSON.stringify(baseline), 'utf8');

      const r = spawnSync(PY, [GUION, '--check', '--workflow', wf, '--baseline', bl],
        { encoding: 'utf8' });
      return { code: r.status, out: (r.stdout || '') + (r.stderr || '') };
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  }

  // Un workflow minimo pero de la FORMA REAL: `repository:` con owner y luego su
  // `ref:`. El owner es lo que hacia fallar la primera version de este guard
  // contra el workflow de verdad, asi que el fixture lo trae a proposito.
  const WORKFLOW_SANO = [
    'jobs:',
    '  audit:',
    '    steps:',
    '      - uses: actions/checkout@v4',
    '        with:',
    '          repository: ajabadia/ABDEep',
    '          ref: aaaaaa111111',
    '      - uses: actions/checkout@v4',
    '        with:',
    '          repository: ajabadia/ABDNeural',
    '          ref: bbbbbb222222',
    '',
  ].join('\n');

  const BASE_SANA = {
    nota: 'x',
    foto: { ABDEep: 'aaaaaa111111', ABDNeural: 'bbbbbb222222' },
    fuentes: ['DspCore/DspMath.h'],
  };

  it('el repo real: su linea base declara la foto que el workflow va a medir', {
    skip: PY === null && 'no hay python en esta maquina',
  }, () => {
    const r = spawnSync(PY, [GUION, '--check'], { cwd: RAIZ, encoding: 'utf8' });
    assert.equal(r.status, 0,
      `el repo de verdad da ${r.status}. Si es 1, la foto guardada no es la del `
      + `workflow, o la linea base se regenero en local (donde los hermanos tienen `
      + `trabajo sin commitear):\n${r.stdout}${r.stderr}`);
  });

  it('foto igual: 0', { skip: PY === null && 'no hay python' }, () => {
    assert.equal(medir(WORKFLOW_SANO, BASE_SANA).code, 0);
  });

  it('un SHA subido sin regenerar: 1, que es EL caso que este guard existe para',
    { skip: PY === null && 'no hay python' }, () => {
      // El audit se queda en VERDE en este caso: el conjunto de huerfanas no
      // cambia, siguen siendo las mismas 55, y el trinquete solo compara
      // nombres. Lo unico que se ha movido es el suelo.
      const r = medir(WORKFLOW_SANO.replace('bbbbbb222222', 'cccccc333333'), BASE_SANA);
      assert.equal(r.code, 1);
      assert.match(r.out, /ABDNeural/,
        'el rojo tiene que NOMBRAR el repo que no cuadra, no solo decir que no cuadra');
    });

  it('una linea base sin clave foto: 1, y el motivo es que no se puede saber',
    { skip: PY === null && 'no hay python' }, () => {
      const sinFoto = { nota: 'x', fuentes: ['DspCore/DspMath.h'] };
      const r = medir(WORKFLOW_SANO, sinFoto);
      assert.equal(r.code, 1);
      assert.match(r.out, /foto/);
    });

  it('una foto con un null: 1, porque una foto con huecos parece completa',
    { skip: PY === null && 'no hay python' }, () => {
      const r = medir(WORKFLOW_SANO, { ...BASE_SANA, foto: { ABDEep: null, ABDNeural: 'bbbbbb222222' } });
      assert.equal(r.code, 1);
      assert.match(r.out, /ABDEep/);
    });

  it('un repo en el workflow que no esta en la foto: 1 (su foto nadie la midio)',
    { skip: PY === null && 'no hay python' }, () => {
      const r = medir(WORKFLOW_SANO + '          repository: ajabadia/ABDCZ101\n          ref: dddddd444444\n',
        BASE_SANA);
      assert.equal(r.code, 1);
      assert.match(r.out, /ABDCZ101/);
    });

  it('un clon de mas al lado se ignora, y un producto de verdad no', {
    skip: PY === null && 'no hay python',
  }, () => {
    // El bug que esto vigila: un SEGUNDO clon de ABDSharedCode al lado se tomaba
    // por un producto de la suite, porque no se llama ABDSharedCode y el filtro
    // era por nombre. El veredicto se invertia con un motivo FALSO: la linea
    // base decia "17 entradas ya no son huerfanas" cuando lo unico que habia
    // pasado era que habia una carpeta de mas.
    //
    // Se monta una suite minima en un temporal y se mira lo que el script DICE
    // haber medido, que es la lista de `hermanos`. Un test que solo comprueba
    // que el script arranca no vigila nada: pasaria igual con el bug puesto.
    const d = mkdtempSync(join(tmpdir(), 'clon-'));
    try {
      const raiz = join(d, 'suite');
      const modulos = join(raiz, 'ABDSharedCode');
      mkdirSync(join(modulos, 'tools'), { recursive: true });
      // El modulo necesita ESTE SCRIPT, que es su propia firma.
      copyFileSync(join(RAIZ, 'tools', 'audit_unconsumed_sources.py'),
        join(modulos, 'tools', 'audit_unconsumed_sources.py'));

      // Un clon de mas: trae el script, asi que es "otro ABDSharedCode".
      const clon = join(raiz, 'ci-solo');
      mkdirSync(join(clon, 'tools'), { recursive: true });
      copyFileSync(join(RAIZ, 'tools', 'audit_unconsumed_sources.py'),
        join(clon, 'tools', 'audit_unconsumed_sources.py'));

      // Un producto de verdad: NO trae el script.
      const producto = join(raiz, 'ABDNeural');
      mkdirSync(join(producto, 'Source'), { recursive: true });
      writeFileSync(join(producto, 'Source', 'algo.cpp'), '// nada\n', 'utf8');

      for (const q of [modulos, clon, producto]) {
        spawnSync('git', ['init', '-q', q], { encoding: 'utf8' });
      }

      const r = spawnSync(PY, [join(modulos, 'tools', 'audit_unconsumed_sources.py')],
        { cwd: raiz, encoding: 'utf8' });
      const salida = (r.stdout || '') + (r.stderr || '');

      assert.match(salida, /Hermanos medidos: ABDNeural(,|$)/m,
        `un producto de la suite tiene que medirse. Sale:
${salida}`);
      assert.doesNotMatch(salida, /Hermanos medidos:[^\n]*ci-solo/,
        `un clon de este repo NO es un producto de la suite y no puede medirse. Sale:
${salida}`);
      assert.match(salida, /IGNORADO ci-solo/,
        `y no en silencio: tiene que decir que lo ha ignorado y por que. Sale:
${salida}`);
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  });

  it('--listar imprime los SHA del workflow y sale 0, sin comparar nada', {
    skip: PY === null && 'no hay python',
  }, () => {
    // Lo consume el paso del workflow que avisa si un SHA dejo de existir. Si
    // `--listar` tambien comprobara algo, ese paso heredaria el 1 de una
    // diferencia de foto que no es lo que quiere mirar.
    const d = mkdtempSync(join(tmpdir(), 'listar-'));
    try {
      const wf = join(d, 'wf.yml');
      writeFileSync(wf, WORKFLOW_SANO, 'utf8');
      const r = spawnSync(PY,
        [GUION, '--listar', '--workflow', wf], { encoding: 'utf8' });
      assert.equal(r.status, 0);
      // Se parte por Linea y se filtra la vacia: en Windows stdout llega con
      // CRLF, y comparar el texto entero contra un array hace fallar el test
      // solo en esta plataforma, que es la forma de que un test sea verdad a
      // medias.
      const lineas = r.stdout
        .split(String.fromCharCode(10))
        .map((l) => l.replace(String.fromCharCode(13), ''))
        .filter((l) => l.trim());
      assert.deepEqual(lineas.sort(), ['ABDEep aaaaaa111111', 'ABDNeural bbbbbb222222'],
        `el listado tiene que ser exactamente repo sha, con el nombre recortado: ${r.stdout}`);
    } finally {
      rmSync(d, { recursive: true, force: true });
    }
  });

  it('un workflow ilegible: 2, no 1, porque no es un hallazgo', { skip: PY === null && 'no hay python' }, () => {
    const r = spawnSync(PY, [GUION, '--check', '--workflow', join(tmpdir(), 'no-existe-esta.yml')],
      { cwd: RAIZ, encoding: 'utf8' });
    assert.equal(r.status, 2,
      'un 1 aqui diria "la foto no cuadra" cuando en realidad no se ha medido nada');
  });
});