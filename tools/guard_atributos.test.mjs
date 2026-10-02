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