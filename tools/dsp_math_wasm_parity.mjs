// Lado de WASM del arnés de DspMath. Se ejecuta con node, sin dependencias.
//
//   node tools/dsp_math_wasm_parity.mjs <ruta/al/harness.wasm>
//
// Qué hace y por qué no hace más:
//
//   - Instancia el módulo que compila `tools/run_dsp_math_harness.py` a
//     `--target=wasm32 -nostdlib`. No hay Emscripten aquí: es clang + wasm-ld, y
//     el módulo no tiene libc ni gestor de memoria porque el arnés no pide
//     ninguno. Solo pide una cosa al host, el reloj (abajo).
//   - Llama a `dspMathHarnessReport()`, que devuelve un PUNTERO dentro de la
//     memoria lineal del módulo, y lo lee como cadena UTF-8 hasta el NUL.
//   - Escribe ese informe tal cual en stdout. Quien COMPARA no es este
//     fichero sino el runner: aquí no se decide nada, se mide una vez y se
//     imprime, para que el lado nativo y el lado WASMбайan por el mismo
//     formato y la comparación sea entre iguales.
//
// POR QUÉ EL RELOJ ES UNA IMPORTACIÓN Y NO UN SHIM. El módulo declara
// `dspMathHarnessHostMilis` y no la tiene: en freestanding no existe `time.h`,
// ni `clock()`, ni reloj. Se lo pone el host. Si este fichero le pasara otro
// reloj, o un shim, el número que saldría sería el de un reloj inventado
// medido contra sí mismo, que no significa nada. `performance.now()` es el reloj
// de pared REAL del proceso de node, que es lo mismo que mide el navegador y lo
// que de verdad le importa a quien escucha.
//
// Y POR QUÉ EL COSTE EN WASM SOLO SE INFORMA. El reloj de node tiene una
// granularidad de submicrosegundo pero su resolución real depende de la
// plataforma y de la carga, y además el mismo bucle pasa por un JIT que puede
// optimizarlo distinto en cada llamada. El informe se imprime entero para que se
// pueda mirar, pero el runner NO falla por el coste de la pata WASM: falla por
// los bits, que sí son comparables bit a bit.

import { readFile } from 'node:fs/promises';
import { performance } from 'node:perf_hooks';

const ruta = process.argv[2];

if (!ruta) {
  console.error('uso: node tools/dsp_math_wasm_parity.mjs <harness.wasm>');
  process.exit(2);
}

const bytes = await readFile(ruta);

// El reloj del host, en milisegundos, que es lo que el arnés espera. Se
// declara aqui y no dentro del modulo porque no hay forma de que un modulo
// freestanding se ponga a tomar la hora a si mismo.
const imports = {
  env: {
    dspMathHarnessHostMilis: () => performance.now(),
  },
};

const { instance } = await WebAssembly.instantiate(bytes, imports);
const exports = instance.exports;

if (typeof exports.dspMathHarnessReport !== 'function') {
  console.error('el modulo no exporta dspMathHarnessReport');
  process.exit(1);
}
if (!exports.memory) {
  console.error('el modulo no exporta la memoria: no se puede leer el informe');
  process.exit(1);
}

const puntero = exports.dspMathHarnessReport();

// OJO: el puntero vale HASTA LA SIGUIENTE llamada, porque el buffer se
// reutiliza. Se lee una vez y no se vuelve a llamar. Esto no es una cautela
// theoretica: si se llamara dos veces y se guardaran los dos punteros, los dos
// informes serian el mismo buffer y la comparacion compararia consigo misma.
const memoria = new Uint8Array(exports.memory.buffer);
let fin = puntero;
while (fin < memoria.length && memoria[fin] !== 0) fin++;

const informe = new TextDecoder('utf-8').decode(memoria.subarray(puntero, fin));

if (informe.length === 0) {
  console.error('el modulo ha devuelto un informe vacio');
  process.exit(1);
}

process.stdout.write(informe);
