/**
 * ABDSynths — Guard de dirección del canal nativo → JS de JUCE WebView2.
 *
 * ## El fallo que vigila
 *
 * En JUCE 8 hay dos canales y van en direcciones opuestas:
 *
 *   - **nativo → JS**: `juce::WebBrowserComponent::emitEventIfBrowserIsVisible(eventId, obj)`.
 *     Es el ÚNICO que dispara los `window.__JUCE__.backend.addEventListener(eventId, …)`
 *     del WebUI.
 *   - **JS → nativo**: `window.__JUCE__.backend.emitEvent(eventId, obj)`.
 *     Lo recoge el `withEventListener(eventId, …)` nativo.
 *
 * Escribir desde C++ un `evaluateJavascript("window.__JUCE__.backend.emitEvent(...)")`
 * es usar el segundo canal en la dirección equivocada: JUCE lo enruta al nativo,
 * nadie escucha en el WebUI y **el mensaje se descarta en silencio** (no lanza, no
 * loguea: el `evaluateJavascript` termina "bien"). Eso es exactamente lo que le pasó
 * a ABDMS2000: todo el canal `cppToWebui` (`hostModel`, `syncAllParams`, `state`…)
 * moría ahí y el Bank Manager embebido abría con el catálogo vacío, mientras el log
 * del plugin decía que sí había recibido el `requestState`.
 *
 * ## Qué NO vigila
 *
 * `evaluateJavascript` en sí **no** está prohibido: es legítimo para llamar a
 * funciones propias de la página (ABDEep usa `window._onStateRestored()`, y el
 * helper compartido `JuceWebView2Component` lo usa para aplicar el tema). El JS
 * también usa `backend.emitEvent` legítimamente: es *su* dirección. Lo prohibido es
 * únicamente **alcanzar los listeners de JUCE desde C++** con ese `emitEvent`.
 *
 * ## Uso
 *
 * ```js
 * import { checkBridgeDirection, formatFindings } from '<…>/webviewBridgeDirection.js';
 *
 * const result = checkBridgeDirection({ sourceRoot: 'Source', emitters: ['Plugin/PluginEditor.cpp'] });
 * if (!result.ok) throw new Error(formatFindings(result));
 * ```
 *
 * Los comentarios C/C++ se eliminan antes de buscar, para que la documentación que
 * explica el fallo (que necesariamente lo menciona) no se dispare a sí misma.
 *
 * Este módulo es ESM puro y sin dependencias: lo consumen los runners de cada host
 * (vitest en ABDMS2000 / ABDCZ101) y también scripts `node` sueltos (ABDJUNiO601,
 * que no tiene runner JS). Vive en `ABDSharedCode/WebView2Bridge/` porque es el
 * hogar compartido de todo lo relativo al puente WebView2 de la suite.
 */

import fs from 'node:fs';
import path from 'node:path';

/** Llamada prohibida *desde C++* (canal JS → nativo). */
export const FORBIDDEN_CALL = 'backend.emitEvent';

/** Llamada correcta *desde C++* (canal nativo → JS). */
export const REQUIRED_CALL = 'emitEventIfBrowserIsVisible';

/** Extensiones consideradas código C++ (incluye Objective-C++ de las variantes mac). */
export const CPP_FILE_PATTERN = /\.(h|hpp|cpp|cc|cxx|mm|ipp)$/i;

/** Directorios que nunca contienen código propio del host. */
const SKIPPED_DIRECTORIES = new Set([
  'node_modules', 'build', 'build-test', 'build_wasm', '_deps', '.git', 'dist', 'third_party', 'vendor'
]);

/**
 * Quita comentarios C/C++ (de bloque y de línea).
 *
 * El truco `(^|[^:])//` evita comerse los `//` de una URL
 * (`https://juce.backend`, `bankwebui://`), que si no truncarían la línea.
 *
 * @param {string} source
 * @returns {string} código sin comentarios (el espaciado se conserva)
 */
export function stripCppComments(source) {
  return source.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/(^|[^:])\/\/[^\n]*/g, '$1');
}

/**
 * Código sin comentarios y con el espaciado colapsado: comparaciones estables frente
 * a reindentaciones.
 * @param {string} source
 * @returns {string}
 */
export function compactCpp(source) {
  return stripCppComments(source).replace(/\s+/g, ' ');
}

/**
 * Números de línea (1-based) donde el código usa el canal equivocado.
 * @param {string} source contenido de un fichero C/C++
 * @returns {number[]}
 */
export function findForbiddenLines(source) {
  return stripCppComments(source)
    .split('\n')
    .map((line, index) => [index + 1, line])
    .filter(([, line]) => line.includes(FORBIDDEN_CALL))
    .map(([lineNumber]) => lineNumber);
}

/**
 * Lista recursiva de ficheros C/C++ bajo `rootDir` (orden estable).
 * @param {string} rootDir
 * @returns {string[]} rutas absolutas
 */
export function listCppSources(rootDir) {
  const found = [];
  for (const entry of fs.readdirSync(rootDir, { withFileTypes: true })) {
    const full = path.join(rootDir, entry.name);
    if (entry.isDirectory()) {
      if (SKIPPED_DIRECTORIES.has(entry.name)) continue;
      found.push(...listCppSources(full));
    } else if (CPP_FILE_PATTERN.test(entry.name)) {
      found.push(full);
    }
  }
  return found.sort();
}

/**
 * Busca la llamada prohibida en todo un árbol de código C++.
 * @param {string} sourceRoot raíz a escanear (p. ej. `<repo>/Source`)
 * @returns {{ file: string, lines: number[] }[]} rutas relativas a `sourceRoot`
 */
export function scanForForbiddenCalls(sourceRoot) {
  const absoluteRoot = path.resolve(sourceRoot);
  return listCppSources(absoluteRoot)
    .map(file => ({ file, lines: findForbiddenLines(fs.readFileSync(file, 'utf8')) }))
    .filter(({ lines }) => lines.length > 0)
    .map(({ file, lines }) => ({ file: path.relative(absoluteRoot, file), lines }));
}

/**
 * Comprueba la dirección del canal en un host.
 *
 * @param {object} options
 * @param {string} options.sourceRoot raíz del código C++ del host
 * @param {string[]} [options.emitters] ficheros (relativos a `sourceRoot`) que emiten
 *   hacia el WebUI: deben usar {@link REQUIRED_CALL}. Si falta el fichero o no la usa,
 *   se reporta (es la mitad positiva del guard: no basta con no equivocarse).
 * @returns {{
 *   ok: boolean,
 *   scanned: number,
 *   findings: { file: string, lines: number[] }[],
 *   missingEmitters: string[],
 *   absentEmitters: string[],
 *   problems: string[]
 * }}
 */
export function checkBridgeDirection({ sourceRoot, emitters = [] } = {}) {
  if (!sourceRoot) throw new Error('checkBridgeDirection: falta sourceRoot');

  const absoluteRoot = path.resolve(sourceRoot);
  const scanned = listCppSources(absoluteRoot).length;
  const findings = scanForForbiddenCalls(absoluteRoot);

  const missingEmitters = [];
  const absentEmitters = [];
  for (const emitter of emitters) {
    const file = path.join(absoluteRoot, emitter);
    if (!fs.existsSync(file)) {
      absentEmitters.push(emitter);
      continue;
    }
    if (!stripCppComments(fs.readFileSync(file, 'utf8')).includes(REQUIRED_CALL)) {
      missingEmitters.push(emitter);
    }
  }

  const problems = [];
  for (const { file, lines } of findings) {
    problems.push(
      `${file}:${lines.join(',')} usa ${FORBIDDEN_CALL} desde C++ ` +
        `(canal JS → nativo): el mensaje no llega a ningún listener del WebUI. Usa ${REQUIRED_CALL}.`
    );
  }
  for (const emitter of missingEmitters) {
    problems.push(`${emitter} no emite con ${REQUIRED_CALL}: no hay canal nativo → JS.`);
  }
  for (const emitter of absentEmitters) {
    problems.push(`${emitter} no existe: el guard quedó desactualizado.`);
  }

  return {
    ok: problems.length === 0,
    scanned,
    findings,
    missingEmitters,
    absentEmitters,
    problems
  };
}

/**
 * Texto listo para lanzar como error o imprimir por consola.
 * @param {ReturnType<typeof checkBridgeDirection>} result
 * @returns {string}
 */
export function formatFindings(result) {
  if (result.ok) return 'OK — canal nativo → JS correcto en todo el código C++.';
  return `Dirección del canal WebView2 incorrecta (${result.problems.length} problema(s)):\n` +
    result.problems.map(problem => `  - ${problem}`).join('\n');
}
