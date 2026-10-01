// Pruebas del guard de `.gitattributes`.
//
//   node --test tools/
//
// SIN DEPENDENCIAS, a propósito. Este repo no tiene `package.json`, ni vitest,
// ni runner: tiene cuatro tests de C++ y unas herramientas en node. Meter un
// runner para veinte tests sería meter una cadena de dependencias en un repo que
// no la tiene, y el guard que vigila los saltos de línea de los binarios
// terminaría depending de un `pnpm install` que nadie recuerda hacer.
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
import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

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