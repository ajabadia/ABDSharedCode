// La puerta del guard de `.gitattributes`: lo ejecuta el CI (o quien quiera) y
// sale con 1 si algún artefacto generado o binario no está declarado.
//
//   node tools/comprobar_atributos.mjs
//
// Salida:
//   0  todo artefacto está declarado y ninguna regla está inerte por error
//   1  hay artefactos sin declarar, o reglas de ruta concreta que no cubren nada
//
// ─────────────────────────────────────────────────────────────────────────
// POR QUÉ UN FICHERO CORTO SEPARADO DEL GUARD
//
// `guard_atributos.mjs` no lee el disco ni llama a git: son funciones puras, y
// por eso el test puede darle reglas inventadas y comprobar que las detecta.
// Este fichero es el otro extremo, el que se ejecuta de verdad, y su trabajo es
// único: juntar las tres fuentes que el test usa como datos y devolver un
// código de salida. Si el cálculo viviera aquí, la prueba del motor de
// patrones necesitaría un repo de mentira entero para poder fuzzear una regla.
//
// LA DIRECCIÓN QUE ESTE FICHERO NO COMPRUEBA, Y OTRO SÍ. Aquí se pregunta
// «¿está declarado todo lo que hay?», que es la dirección que encuentra los
// `.pyc` y los `.gif`. La contraria —«¿está declarado algo que ya no existe?»—
// la contesta `reglasInertes`, también aquí, porque una ruta concreta
// equivocada es un fallo igual de silencioso: se lee como una protección y no
// protege nada. Lo que NO se comprueba es que un artefacto esté *en la
// revisión correcta*, porque eso ya lo dice `git diff --exit-code` en el CI, y
// repetirlo aquí sería tener dos puertas para lo mismo.
//
// Y LO QUE NO HACE, POR QUE NO SE PUEDE. No comprueba que los saltos de línea
// del checkout estén bien. Se podría (`git check-attr eol` sobre cada
// fichero), y en este repo daría rojo hoy: 73 de los 195 ficheros trackeados
// tienen CRLF en disco aunque su regla diga `eol=lf`, porque añadir la regla no
// reescribe el working tree y el filtro de limpieza de git esconde la
// diferencia. Arreglarlos es `git add --renormalize .`, que toca 73 ficheros, y
// eso no lo decide un guard: lo decide quien pueda revisar el diff.

import { readFileSync, statSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

import { gitSeguroONull, leerTrackeados, RAIZ } from './git_seguro.mjs';
import {
  reglasDe, reglasInertes, descubreArtefactos, artefactosSinDeclarar, reglasQueDeclaranBinario
} from './guard_atributos.mjs';

/**
 * Lee los ficheros trackeados como el guard los necesita: ruta, tamaño, y el
 * contenido SOLO si puede ser texto.
 *
 * Los binarios no se decodifican: se leen los primeros 8 KB, que es lo único
 * que hace falta para buscar un byte NUL, y se pasa como `contenido` con los
 * bytes que no son imprimibles sustituidos. Un `.pyc` leído entero y pasado a
 * `toString('utf8')` devuelve una ristra de reemplazos que no dice nada, y
 * además tira la memoria de un `.obj` de 40 MB por el suelo.
 */
function leerFicheros (rutas) {
  const salida = [];

  for (const ruta of rutas) {
    const bytes = statSync(resolve(RAIZ, ruta)).size;
    let contenido = '';

    try {
      if (bytes > 8 * 1024 * 1024) {
        // Demasiado grande para mirarlo. No es un artefacto de código, y para
        // las señas de aquí solo importan el nombre y el tamaño.
        contenido = '';
      } else {
        const buffer = readFileSync(resolve(RAIZ, ruta));
        const esBinario = buffer.subarray(0, 8000).includes(0);

        contenido = esBinario ? '' : buffer.toString('utf8');
      }
    } catch (e) {
      // Un fichero que no se puede leer no se puede proteger: se avisa y se
      // sigue, porque reventar aquí dejaría el resto del guard sin comprobar
      // por un único banco corrupto.
      console.warn('[guard] no se pudo leer ' + ruta + ': ' + e.message);
    }

    salida.push({ ruta, contenido, bytes });
  }

  return salida;
}

/** El atributo que git está aplicando de verdad a un fichero, para contrastar. */
function atributoDeGit (ruta) {
  const salida = gitSeguroONull(['check-attr', 'binary', 'text', '--', ruta]);

  if (salida === null) return null;

  const atributos = {};

  for (const linea of salida.split('\n')) {
    const m = /^.*?:\s*(\S+):\s*(\S+)$/.exec(linea);

    if (m) atributos[m[1]] = m[2];
  }

  return atributos;
}

/**
 * El contraste con `git check-attr`, que es la autoridad y se usa de contrapeso.
 *
 * El guard no pregunta a git porque git no sabe responder "¿qué ficheros cubre
 * esta regla?", que es la pregunta de una regla inerte. Pero para lo que sí
 * sabe —qué atributo se aplica a ESTE fichero— mandar es de git, y que los dos
 * digan lo mismo es lo que convierte al guard en una comprobación y no en una
 * segunda opinión.
 */
function desacuerdos (artefactos, texto) {
  const malos = [];

  for (const artefacto of artefactos) {
    const declaradas = reglasQueDeclaranBinario(artefacto.ruta, texto).length > 0;
    const deGit = atributoDeGit(artefacto.ruta);

    if (!deGit || !declaradas) continue;
    if (deGit.binary !== 'set') {
      malos.push({ ruta: artefacto.ruta, motivo: 'el guard lo ve binario y git no lo declara' });
    }
  }

  return malos;
}

/** El informe completo, como datos. Lo usan el runner y el test. */
export function informe () {
  const texto = readFileSync(resolve(RAIZ, '.gitattributes'), 'utf8');
  const rutas = leerTrackeados();
  const ficheros = leerFicheros(rutas);
  const artefactos = descubreArtefactos(ficheros);
  const inertes = reglasInertes(reglasDe(texto), rutas);

  return {
    rutas,
    reglas: texto,
    artefactos,
    sinDeclarar: artefactosSinDeclarar(artefactos, texto),
    inertes: inertes.errores,
    preventivas: inertes.preventivas,
    desacuerdos: desacuerdos(artefactos, texto)
  };
}

/** El informe como texto para una persona, que es lo que se lee en el CI. */
export function formatea (datos) {
  const lineas = [];

  lineas.push('artefactos descubiertos: ' + datos.artefactos.length);
  for (const a of datos.artefactos) {
    lineas.push('  ' + a.ruta + '  [' + a.senas.join(', ') + ']');
  }

  if (datos.sinDeclarar.length > 0) {
    lineas.push('');
    lineas.push('SIN DECLARAR en .gitattributes:');
    for (const a of datos.sinDeclarar) {
      lineas.push('  ' + a.ruta + '  necesita ' + a.falta + '  [' + a.senas.join(', ') + ']');
    }
  }

  if (datos.inertes.length > 0) {
    lineas.push('');
    lineas.push('REGLAS INERTES, que se leen como proteccion y no cubren nada:');
    for (const r of datos.inertes) lineas.push('  ' + r.cruda);
  }

  if (datos.desacuerdos.length > 0) {
    lineas.push('');
    lineas.push('DISCREPAN del guard y de git:');
    for (const d of datos.desacuerdos) lineas.push('  ' + d.ruta + '  ' + d.motivo);
  }

  if (datos.preventivas.length > 0) {
    lineas.push('');
    lineas.push('preventivas, que hoy no cubren nada y está bien que sea así: ' +
      datos.preventivas.length);
    for (const r of datos.preventivas) lineas.push('  ' + r.cruda);
  }

  return lineas.join('\n');
}

// El `main` va debajo del `if` para que importar este fichero en el test no
// ejecute el guard entero por el casual de que el test quiera sus datos.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const datos = informe();

    console.log(formatea(datos));

    const rojos = datos.sinDeclarar.length + datos.inertes.length + datos.desacuerdos.length;

    if (rojos > 0) {
      console.error('');
      console.error('guard de .gitattributes: ' + rojos + ' problema(s).');
      process.exit(1);
    }

    process.exit(0);
  } catch (e) {
    // Un fallo al LEER el repo no es un fallo del guard, y decir la diferencia
    // es la mitad del trabajo: si se mezclan, el primero que lee el rojo busca
    // reglas que están bien.
    console.error('guard de .gitattributes: no se pudo ni siquiera leer el repo.');
    console.error(e.message);
    process.exit(2);
  }
}