// Llama a `git` de una forma que funciona en una máquina concreta, y no solo
// en la de quien escribió el test.
//
// COPIA GESTIONADA. El mismo fichero, con el mismo nombre y el mismo
// comportamiento, existe en `ABDEep/WebUI/tests/helpers/gitSeguro.js`. Aquí
// cambia UNA sola cosa: `RAIZ` sube dos niveles en vez de tres, porque aquí las
// herramientas viven en `tools/`, no en `WebUI/tests/helpers/`. Si algo cambia
// en una de las dos copias, cambia en las dos.
//
// ─────────────────────────────────────────────────────────────────────────
// POR QUÉ ESTE FICHERO EXISTE
//
// En Windows, git se niega a trabajar en un repositorio cuyo propietario no es
// el usuario que lo invoca, y dice:
//
//   fatal: detected dubious ownership in repository at 'D:/.../ABDSharedCode'
//
// El aviso acierta en el propósito —esa protección existe por algo— y en la
// práctica estorba en el caso de este workspace, donde el repositorio lo creó
// otro usuario de la misma máquina. La salida de git es la que el guard
// necesita: `ls-files` para saber qué ficheros existen de verdad, `show` para
// leer el blob de un fichero y no lo que hay en disco, y `check-attr` para
// preguntarle a git qué eol ve en cada uno.
//
// ─────────────────────────────────────────────────────────────────────────
// LA TRAMPA, Y POR QUÉ NO SE RESUELVE CON `-c safe.directory` A SECO
//
// Cuando `git ls-files` falla y la lista queda vacía, el guard entero se pone
// rojo diciendo que no hay ni un artefacto declarado, que es un diagnóstico que
// manda a mirar el fichero equivocado: el guard está perfectamente, lo que
// falló fue leer la lista.
//
// Así que no basta con que git funcione: si vuelve a fallar, el fallo tiene que
// SER de git y decir por qué.
//
// ─────────────────────────────────────────────────────────────────────────
// POR QUÉ `safe.directory` SE CALCULA Y NO SE ESCRIBE
//
// Escribir la ruta absoluta a mano (`safe.directory=D:/desarrollos/...`)
// funciona en esta máquina y en ninguna otra: en un checkout limpio, o en una
// copia del repo en otro sitio, el preflight se pondría rojo por un path que no
// existe. Un guard que depende de una máquina no es un guard, es una costumbre.
//
// Por eso la ruta sale de `import.meta.url`: el fichero sabe dónde está, y por
// tanto sabe qué repositorio es.
//
// ─────────────────────────────────────────────────────────────────────────
// LO QUE NO HACE
//
// No toca la configuración global de git, ni escribe en `~/.gitconfig`. Eso
// sería cambiar el equipo entero de máquina para arreglar un problema de un
// test, y además el efecto no se ve cuando se lee el código. Aquí la excepción
// va en el comando, que es donde se puede ver, y solo dura lo que dura ese
// comando.

import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const aqui = dirname(fileURLToPath(import.meta.url));

// Este fichero vive en `tools/`, y el repo está UN nivel por encima: las
// herramientas cuelgan de la raíz, como `audit_unconsumed_sources.py`.
export const RAIZ = resolve(aqui, '..');

/**
 * Git acepta la ruta de varias formas, pero en Windows lo que entiende sin
 * sorpresas es la CON VIGILILLAS HACIA ADELANTE y sin barra final. Con `\` en
 * vez de `/` el valor no casa y git vuelve a quejarse de dubious ownership,
 * que es el fallo más difícil de ver de todos porque parece que la opción no
 * se está aplicando.
 */
const rutaParaGit = (ruta) => ruta.replace(/\\/g, '/').replace(/\/+$/, '');

/**
 * Los argumentos que abren TODA llamada a git: la excepción de
 * `safe.directory`, primero, antes que el subcomando.
 *
 * Se declara una vez y se concatena en cada llamada. Es lo contrario de
 * repetirlo cuatro veces en cuatro sitios, que es como una excepción se
 * desincroniza: se arregló una llamada y se olvidaron las otras tres.
 */
export const GIT = ['-c', `safe.directory=${rutaParaGit(RAIZ)}`];

/**
 * Corre un subcomando de git en este repositorio, con la excepción puesta.
 *
 * @param {string[]} args el subcomando y sus argumentos, SIN el `git` delante.
 * @param {object} [opciones] opciones de `execFileSync`.
 * @returns {string} la salida de git, como texto.
 */
export function gitSeguro (args, opciones = {}) {
  return execFileSync('git', [...GIT, ...args], {
    cwd: opciones.cwd || RAIZ,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
    ...opciones
  });
}

/**
 * Igual que `gitSeguro`, pero devuelve `null` en vez de lanzar.
 *
 * Para las preguntas que tienen respuesta buena Y mala, y las dos son un
 * resultado válido —«¿tiene historial?» puede que sí y puede que no—.
 * Devolver `null` obliga a decidir qué hacer, mientras que capturar el error
 * dentro del `catch` deja la excepción a medio camino.
 *
 * @param {string[]} args el subcomando y sus argumentos.
 * @param {object} [opciones] opciones de `execFileSync`.
 * @returns {string|null} la salida de git, o `null` si git falló.
 */
export function gitSeguroONull (args, opciones = {}) {
  try {
    return gitSeguro(args, opciones);
  } catch (e) {
    return null;
  }
}

/**
 * Los ficheros que git lleva, que es lo único que las reglas pueden cubrir.
 *
 * LANZA si git no cooperate, y no devuelve una lista vacía: una lista vacía
 * haría que "ninguna regla cubre nada" fuese cierto y el guard pasaría sin
 * haber mirado un solo fichero, que es el peor resultado posible porque es
 * verde.
 *
 * @returns {string[]} rutas con barras hacia delante, como las devuelve git.
 */
export function leerTrackeados () {
  try {
    return gitSeguro(['ls-files'], { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 })
      .split('\n')
      .filter((f) => f !== '')
      .map((f) => f.replace(/\\/g, '/'));
  } catch (e) {
    const stderr = (e.stderr || '').toString().trim();
    const porQue = stderr === '' ? e.message : stderr.split('\n')[0];

    throw new Error('`git ls-files` falló en ' + RAIZ + ', de modo que el guard no puede decir '
      + 'NADA sobre las reglas: ni a favor ni en contra.\n\n'
      + 'Lo que falló: ' + porQue + '\n\n'
      + 'Si el repositorio pertenece a otro usuario, esto es la protección de "dubious '
      + 'ownership" y no debería pasar: `gitSeguro` ya pone la excepción. Si la ves, el helper '
      + 'está mirando al repositorio equivocado, y su RAIZ sale de donde vive el fichero; lo '
      + 'que hay que revisar es donde está `tools/git_seguro.mjs`, no las reglas.');
  }
}