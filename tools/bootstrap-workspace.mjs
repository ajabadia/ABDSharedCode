// Bootstrap del workspace pnpm de ABDSharedCode.
//
// QUE RESUELVE, Y POR QUE HACE FALTA. `MidiKeyboard/` es el paquete
// `@abdsynths/midi-keyb`, y declara `@abdsynths/shared` como `workspace:*`.
// Ese paquete no esta en este repo: es ABDSharedAssets, que vive al lado. Asi que
// un clon limpio de ABDSharedCode no puede instalar sus propias pruebas: el
// workspace que las resolveria esta en la raiz de la suite, que es un repo
// distinto de este.
//
// Medido, en ese clon:
//   - sin `pnpm-workspace.yaml` en este repo: "Cannot resolve package from
//     workspace because workspace packages were not loaded into the resolver",
//     que no nombra el paquete que falta ni dice que hacer;
//   - con el manifiesto pero sin el hermano: "no package named
//     @abdsynths/shared is present in the workspace", que ya si;
//   - con las dos cosas: 326 pruebas en verde, 5 de los 6 ficheros.
//
// QUE NO HACE. No toca el estado de los repos hermanos: si ABDSharedAssets ya esta
// al lado, no lo actualiza ni lo cambia de rama, solo comprueba que esta y que es
// el repo que se espera. Actualizar el hermano es una decision de quien lo
// checkout, no de un script que se ejecuta al instalar.
//
// USO EN LOCAL Y USO EN CI, Y POR QUE ES EL MISMO SCRIPT. En local el workspace
// es ESTE repo: el manifiesto esta versionado aqui y sus miembros son
// `MidiKeyboard` y `../ABDSharedAssets`. En CI el workspace es el DIRECTORIO que
// arma la accion `pnpm-workspace-bootstrap`, que hace tres checkouts hermano a
// hermano, y ahi el manifiesto hay que GENERARLO porque nadie lo ha versionado y
// ademas tiene que listar al repo llamante, que solo CI conoce. Son dos problemas
// que parecen dos y son el mismo: quien tiene que saber como es el workspace es
// este fichero, no un `run:` de bash que se puede quedar viejo sin que ninguna
// prueba lo note. Por eso la accion llama a este script en vez de reimplementarlo.
//
// Uso:
//   node tools/bootstrap-workspace.mjs           # deja el workspace instalable
//   node tools/bootstrap-workspace.mjs --check   # comprueba SIN instalar ni escribir
//   node tools/bootstrap-workspace.mjs --help
//
//   # lo que hace la accion de CI: el manifiesto se genera en <raiz>, no aqui
//   node tools/bootstrap-workspace.mjs --workspace-root "$GITHUB_WORKSPACE" --project ABDEep
//
// Codigos de salida, que no son intercambiables:
//   0  el workspace esta listo (o --check no encuentra nada que arreglar)
//   1  falta algo y este script no puede arreglarlo
//   2  error de uso

import { spawnSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const RAIZ = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SUITE = dirname(RAIZ);

// El hermano, con su remoto. La REF la elige quien ejecuta: en local interesa
// la rama de trabajo, que se mueve; en CI la fija el workflow, que es lo que ya
// hace la accion `pnpm-workspace-bootstrap`. Fijar aqui un SHA seria decidir por
// la gente que desarrolla en local que trabaja contra un `@abdsynths/shared`
// viejo, y ese SHA se quedaria viejo sin que nadie se entere.
const HERMANOS = [
  {
    nombre: 'ABDSharedAssets',
    paquete: '@abdsynths/shared',
    remoto: 'https://github.com/ajabadia/ABDSharedAssets.git',
    // Lo que hay que poder LEER de el, no "que exista": si el hermano esta a
    // medias, que es el caso que un clon a medias produce, la verdad sale de que
    // falte este fichero concreto.
    necesita: ['package.json', 'components/wheel.js']
  }
];

const AYUDA = `Bootstrap del workspace pnpm de ABDSharedCode.

  node tools/bootstrap-workspace.mjs           deja el workspace instalable
  node tools/bootstrap-workspace.mjs --check   comprueba SIN instalar ni escribir
  node tools/bootstrap-workspace.mjs --help    esto

  --workspace-root <dir>   el workspace no es este repo sino <dir> (lo que hace
                           la accion de CI). Genera ahi el manifiesto, instala
                           ahi y verifica el layout.
  --project <nombre>       el repo llamante, que CI solo conoce. Va OBLIGATORIO
                           con --workspace-root: los dos van juntos o ninguno.

Que hace: comprueba que el hermano ${HERMANOS[0].nombre} este al lado (y en local, si
no esta, lo clona), y lanza \`pnpm install\` en el workspace.

Que NO hace: no actualiza ni cambia de rama el hermano que ya esta, y no toca
ningun fichero de el. Comprobar que este no es lo mismo que decidir cual version.
`;

function fallo (mensaje) {
  process.stderr.write(`bootstrap-workspace: ${mensaje}\n`);
  return 1;
}

/**
 * Un fallo de CI, con el prefijo que lo hace clicable en el log del job.
 *
 * Los dos caminos de fallo se distinguen en el prefijo: el de local explica que
 * paso y como se arregla, y el de CI tiene que salir como anotacion del job
 * porque ahi no hay nadie leyendo para intentar descifrar un exit 1.
 */
function falloDeJob (mensaje) {
  process.stderr.write(`::error::${mensaje}\n`);
  return 1;
}

/** Ruta relativa al workspace, con barras hacia delante, como las pide pnpm. */
function miembro (desde, ruta) {
  return relative(desde, ruta).replace(/\\/g, '/');
}

/**
 * Los paquetes pnpm que ABDSharedCode expone, DESCUBIERTOS.
 *
 * Se descubren y no se hardcodean porque anadir un paquete a ABDSharedCode no
 * deberia obligar a tocar este script ni la accion de CI ni cada workflow que la
 * usa: hoy es solo `MidiKeyboard`, y con esta lista el dia que haya un segundo lo
 * hereda todo el mundo sin tocar nada.
 *
 * @param {string} raizRepo  donde esta ABDSharedCode.
 * @param {string} workspace  el workspace al que van a ser miembros.
 * @returns {string[]} rutas relativas al workspace, en orden.
 *
 * NOTA SOBRE RUTAS QUE SALEN LARGAS. `RAIZ` sale de `import.meta.url`, y Node
 * resuelve symlinks y junctions antes de dar esa URL. En un sandbox donde
 * ABDSharedCode es una union a otro sitio, el miembro sale como
 * `../../desarrollos/.../MidiKeyboard` en vez de `ABDSharedCode/MidiKeyboard`. En
 * CI no pasa: los tres checkouts son directorios de verdad. Y aunque pasara no
 * estaria roto, porque la ruta es relativa al workspace y pnpm la resuelve igual;
 * solo seria un manifiesto mas feo de leer en el log de un job.
 */
function miembros (raizRepo, workspace) {
  return readdirSync(raizRepo, { withFileTypes: true })
    .filter((e) => e.isDirectory() && existsSync(resolve(raizRepo, e.name, 'package.json')))
    .map((e) => miembro(workspace, resolve(raizRepo, e.name)))
    .sort();
}

/**
 * El manifiesto del workspace, como texto. Los miembros van en orden estable
 * (hermano, paquetes de ABDSharedCode, repo llamante) para que el fichero
 * generado sea siempre el mismo y su diff en el log de un job no parezca un
 * cambio de verdad.
 *
 * LLEVA `allowBuilds`, Y POR QUE. Sin esta tabla el install depende de la
 * version de pnpm del workflow: con pnpm 10 sale verde avisando, y con pnpm 11 o
 * posterior sale con 1 y ERR_PNPM_IGNORED_BUILDS. Medido en esta maquina con
 * 12.8.1, con el manifiesto sin esta tabla:
 *
 *   Error: ERR_PNPM_IGNORED_BUILDS
 *     Ignored build scripts: esbuild@0.21.5, puppeteer@25.12.0
 *
 * O sea: un job puede estar verde hoy y caerse manana por una subida de version
 * que no toca este repo. La tabla lo hace explicito y lo deja en un solo sitio,
 * que es justo lo que se ganaba al no duplicar el layout en bash.
 *
 * `puppeteer` NO es un valor fijo: sale del input `skip-puppeteer-download` del
 * workflow, el mismo que decide la variable de entorno del install. Fijarlo en
 * `false` aqui mientras el input dice "descargalo" seria una contradicjon que
 * solo se descubriria viendo que el gif de la demo no se puede grabar.
 *
 * @param {string} workspace  donde vive el manifiesto.
 * @param {string} suite      donde vive el hermano (el padre del workspace en
 *                            local, el propio workspace en CI).
 * @param {string[]} paquetes  miembros que aporta ABDSharedCode.
 * @param {string} proyecto  repo llamante.
 * @param {boolean} permitirPuppeteer  si su script de build puede correr.
 * @returns {string} el contenido de pnpm-workspace.yaml.
 */
function manifiesto (workspace, suite, paquetes, proyecto, permitirPuppeteer) {
  const entradas = [
    ...HERMANOS.map((h) => `  - ${miembro(workspace, resolve(suite, h.nombre))}`),
    ...paquetes.map((p) => `  - ${p}`),
    `  - ${miembro(workspace, resolve(workspace, proyecto))}`
  ];

  const aprobaciones = ['allowBuilds:', '  esbuild: true', `  puppeteer: ${permitirPuppeteer}`];

  return `packages:\n${entradas.join('\n')}\n\n${aprobaciones.join('\n')}\n`;
}

/**
 * La guardia del layout, que antes vivia en el `run:` de bash de la accion.
 *
 * Portada tal cual porque `pnpm install` puede terminar en verde y aun asi dejar
 * las dependencias "workspace:*" sin enlazar. Sin esto, eso no se descubre hasta
 * que Vite o Vitest revientan veinte pasos mas tarde con un error que no menciona
 * el workspace.
 *
 * @returns {string[]} problemas; vacio = todo bien.
 */
function problemasDeLayout (workspace, suite, proyecto, paquetes) {
  const problemas = [];
  const raizProyecto = resolve(workspace, proyecto);

  if (!existsSync(raizProyecto)) {
    problemas.push(`el layout de hermanos no se creo: falta el directorio '${proyecto}' en ${workspace}.`);
  }

  for (const p of paquetes) {
    if (!existsSync(resolve(workspace, p))) {
      problemas.push(`el manifiesto lista '${p}' pero ese directorio no existe.`);
    }
  }

  for (const h of HERMANOS) {
    for (const f of h.necesita) {
      if (!existsSync(resolve(suite, h.nombre, f))) {
        problemas.push(`falta ${h.nombre}/${f}: el clon esta a medias, o es de antes de ese fichero.`);
      }
    }
  }

  const dirEnlaces = resolve(raizProyecto, 'node_modules', '@abdsynths');
  const enlazados = existsSync(dirEnlaces) ? readdirSync(dirEnlaces) : [];

  // Solo se queixa de los enlaces si lo demas esta bien: si falta el directorio
  // del proyecto, decir "no hay enlaces" es un segundo sintomas del primero y
  // hace que quien lee tenga que decidir cual de los dos es la causa.
  if (enlazados.length === 0 && problemas.length === 0) {
    problemas.push(`pnpm no enlazo ningun paquete @abdsynths/* en ${proyecto}/node_modules: `
      + 'el manifiesto no los esta resolviendo.');
  }

  return problemas;
}

function hermano (nombre) {
  return HERMANOS.find((h) => h.nombre === nombre);
}

/**
 * Lo que hay que poder leer del hermano: una lista de PARES [motivo, porQue].
 *
 * Siempre pares, nunca una cadena suelta. La version anterior devolvia una
 * cadena cuando el hermano no estaba y pares cuando le faltaba un fichero, y
 * quien la consumia hacia `const [motivo, porQue] = problemas[0]`: sobre una
 * cadena eso no falla, destructura los dos primeros CARACTERES, y el mensaje
 * salia como "FALTA ABDSharedAssets -> n: o". Un tipo de retorno que cambia de
 * forma es un fallo que no se ve en el sitio donde se lee.
 */
function problemasDe (nombre, suite) {
  const base = resolve(suite, nombre);
  const h = hermano(nombre);

  if (!existsSync(base)) {
    return [[`no esta al lado de este repo (se esperaba en ${suite})`, '']];
  }
  if (!existsSync(resolve(base, '.git'))) {
    return [[`${nombre} existe pero no es un clon de git`, 'no se sabe de donde vino, y por tanto no se puede confiar en lo que tenga']];
  }
  return h.necesita
    .filter((f) => !existsSync(resolve(base, f)))
    .map((f) => [`falta ${f}`, 'clon a medias, o una version del hermano anterior a ese fichero']);
}

/** Clona el hermano que falte. Devuelve el codigo de salida del proceso. */
function clonar (h, ref, suite) {
  const destino = resolve(suite, h.nombre);
  const args = ['clone', '--depth', '1'];

  if (ref) args.push('--branch', ref);
  args.push(h.remoto, destino);

  process.stderr.write(`bootstrap-workspace: clonando ${h.remoto} -> ${destino}\n`);

  const r = spawnSync('git', args, { stdio: 'inherit' });
  return r.status === null ? 2 : r.status;
}

function pnpm (args, entorno, cwd) {
  return spawnSync('pnpm', args, {
    cwd,
    stdio: 'inherit',
    shell: process.platform === 'win32',
    env: { ...process.env, ...entorno }
  });
}

/**
 * Los argumentos, en vez de un `filter` que solo vale para banderas.
 *
 * `--workspace-root` y `--project` llevan valor, y hay dos formas de escribirlo
 * (`--x v` y `--x=v`) porque quien las escribe es un `run:` de un workflow
 * generado, y un `=` de mas no deberia ser la diferencia entre funcionar y no.
 *
 * @returns {{opciones: Map<string,string>, banderas: Set<string>, desconocidos: string[]}}
 */
function parsear (argv) {
  const opciones = new Map();
  const banderas = new Set();
  const desconocidos = [];
  const conValor = new Set(['--workspace-root', '--project']);

  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];

    if (!a.startsWith('--')) { desconocidos.push(a); continue; }

    if (a.includes('=')) {
      const [clave, ...resto] = a.split('=');
      if (conValor.has(clave)) opciones.set(clave, resto.join('='));
      else desconocidos.push(clave);
      continue;
    }

    if (conValor.has(a)) {
      const valor = argv[i + 1];
      if (valor === undefined || valor.startsWith('--')) {
        desconocidos.push(`${a} (sin valor)`);
        continue;
      }
      opciones.set(a, valor);
      i++;
      continue;
    }

    banderas.add(a);
  }

  return { opciones, banderas, desconocidos };
}

function main () {
  const { opciones, banderas, desconocidos } = parsear(process.argv.slice(2));

  if (banderas.has('--help') || banderas.has('-h')) {
    process.stdout.write(AYUDA);
    return 0;
  }

  const malos = [
    ...desconocidos,
    ...[...banderas].filter((b) => b !== '--check').map((b) => `${b} (desconocida)`)
  ];

  if (malos.length > 0) {
    // 2 y no 1: el contrato de este fichero distingue "no se puede" de "mal
    // escrito", igual que hacen los otros scripts de la suite. Un exit 1 aqui
    // se lee como "el workspace esta roto" en un log.
    process.stderr.write(`bootstrap-workspace: argumento desconocido: ${malos[0]}\n\n${AYUDA}\n`);
    return 2;
  }

  // Los dos van juntos o ninguno: --project sin --workspace-root no dice donde va
  // el repo llamante, y --workspace-root sin --project genera un workspace al que
  // solo le faltan de miembros lo unico que CI sabe. Aceptarlos por separado daria
  // dos formas de pasar que no funcionan, y que nadie descubre hasta el job.
  const raizWorkspace = opciones.get('--workspace-root');
  const proyecto = opciones.get('--project');

  if (Boolean(raizWorkspace) !== Boolean(proyecto)) {
    process.stderr.write('bootstrap-workspace: --workspace-root y --project van juntos o ninguno.\n\n');
    process.stderr.write(AYUDA);
    return 2;
  }

  const enCi = Boolean(raizWorkspace);

  // En local el workspace es este repo y el hermano vive en su padre. En CI el
  // workspace es el directorio que arma la accion, y el hermano esta DENTRO de
  // el, al lado de ABDSharedCode. No se deduce de donde este el hermano: lo dice
  // quien llama, porque depender de que ABDSharedCode cuelgue justo del raiz del
  // workspace es un supuesto que un dia se rompe sin avisar.
  const workspace = enCi ? resolve(raizWorkspace) : RAIZ;
  const suite = enCi ? workspace : SUITE;

  const check = banderas.has('--check');
  const ref = process.env.ABDSHARED_ASSETS_REF || '';
  const paquetes = miembros(RAIZ, workspace);

  // --- 0. que ABDSharedCode exponga algun paquete --------------------------
  if (paquetes.length === 0) {
    const mensaje = 'ABDSharedCode no expone ningun miembro de workspace (ningun package.json '
      + 'en un subdirectorio): @abdsynths/midi-keyb no podria resolverse.';
    return enCi ? falloDeJob(mensaje) : fallo(mensaje);
  }

  // --- 1. el hermano, si falta ---------------------------------------------
  for (const h of HERMANOS) {
    const problemas = problemasDe(h.nombre, suite);

    if (problemas.length === 0) continue;

    const [motivo, porQue] = problemas[0];
    const explicacion = porQue ? `${motivo}: ${porQue}` : motivo;

    // En CI el hermano lo pone el checkout de la accion, y si no sirve no es cosa
    // de este script: clonar dentro de un job son credenciales y una ref que
    // eligio el workflow. Por eso aqui es un fallo, y no un clonado.
    if (enCi) {
      return falloDeJob(`${h.nombre} no sirve en el workspace: ${explicacion}. `
        + 'Lo pone el checkout de la accion, no este script.');
    }

    if (check) {
      process.stderr.write(`bootstrap-workspace: FALTA ${h.nombre} -> ${explicacion}\n`);
      process.stderr.write(
        `  ${h.paquete} esta declarado como "workspace:*" y ${h.nombre} es otro repo, ` +
        'asi que este clon no puede resolverlo solo.\n' +
        `  Arreglo: node tools/bootstrap-workspace.mjs   (o clonalo a mano de ${h.remoto})\n`
      );
      return 1;
    }

    if (motivo.startsWith('no esta al lado')) {
      const codigo = clonar(h, ref, suite);
      if (codigo !== 0) return fallo(`git clone de ${h.nombre} salio con ${codigo}`);

      const despues = problemasDe(h.nombre, suite);
      if (despues.length > 0) {
        return fallo(`tras clonar, ${h.nombre} sigue incompleto: ${despues[0][0]} (${despues[0][1]})`);
      }
      continue;
    }

    return fallo(`${h.nombre} esta pero no sirve: ${explicacion}`);
  }

  // --- 2. el manifiesto ------------------------------------------------------
  // En local esta VERSIONADO, y este script no lo toca: escribirlo seria pisar
  // un fichero del repo. En CI no hay manifiesto que versionar porque el
  // workspace entero se materializa en cada job, y el que hace falta todavia no
  // existe porque tiene que listar al repo llamante, que es lo unico que CI sabe.
  // Por eso aqui se generan, mas abajo, cuando ya se sabe si puppeteer puede
  // construir: sus filas de `allowBuilds` dependen de ese valor.
  const rutaManifiesto = resolve(workspace, 'pnpm-workspace.yaml');

  if (!enCi) {
    if (!existsSync(rutaManifiesto)) {
      return fallo('falta pnpm-workspace.yaml en la raiz de este repo, que es lo que hace que MidiKeyboard sea resoluble.');
    }

    const texto = readFileSync(rutaManifiesto, 'utf8');
    for (const h of HERMANOS) {
      if (!texto.includes(`../${h.nombre}`)) {
        return fallo(`pnpm-workspace.yaml no lista ../${h.nombre}: ${h.paquete} no se podria resolver.`);
      }
    }
  }

  // --- 3. Chromium, el manifiesto de CI, y la instalacion -------------------
  // Chromium (~150 MB) que solo necesita `MidiKeyboard/demo/capture.mjs`, que no es
  // un test. Lo decide el input `skip-puppeteer-download` del workflow, que llega
  // aqui como ABD_SKIP_PUPPETEER, y ese mismo valor decide las DOS cosas que lo
  // controlan: la variable de entorno del install y la fila de `allowBuilds` del
  // manifiesto. Que sean el mismo valor y no dos constantes en dos sitios es lo que
  // evita que uno se cambie y el otro no. Lo que se pone es "0" y no una cadena
  // vacia cuando hay que dejarlo pasar: PUPPETEER_SKIP_DOWNLOAD="" NO es lo mismo
  // que no estar, y con la vacia la descarga ocurre.
  const saltarPuppeteer = enCi ? (process.env.ABD_SKIP_PUPPETEER || 'true') !== 'false' : true;

  if (enCi) {
    const texto = manifiesto(workspace, suite, paquetes, proyecto, !saltarPuppeteer);

    // --check sigue sin escribir, tambien en CI. El contrato de este flag dice
    // "SIN instalar ni escribir", y un --check que escribe deja de ser el modo de
    // mirar y pasa a ser el modo de cambiar: en un job, una comprobacion que
    // modifica el arbol hace que el paso siguiente ya no compruebe lo que hay.
    if (check) {
      const actual = existsSync(rutaManifiesto) ? readFileSync(rutaManifiesto, 'utf8') : null;

      if (actual !== texto) {
        return falloDeJob('pnpm-workspace.yaml no coincide con el que este script genera:\n'
          + '  --- en disco ---\n'
          + (actual === null ? '  (no existe)\n' : actual.split('\n').map((l) => `  ${l}`).join('\n'))
          + '\n  --- esperado ---\n'
          + texto.split('\n').filter((l) => l).map((l) => `  ${l}`).join('\n') + '\n');
      }

      process.stderr.write(`bootstrap-workspace: ${rutaManifiesto} esta como toca.\n`);
    } else {
      writeFileSync(rutaManifiesto, texto, 'utf8');

      process.stderr.write(`bootstrap-workspace: ${rutaManifiesto}\n`);
      for (const linea of texto.split('\n')) {
        if (linea) process.stderr.write(`  ${linea}\n`);
      }
    }
  }

  if (check) {
    if (enCi) {
      const problemas = problemasDeLayout(workspace, suite, proyecto, paquetes);
      if (problemas.length > 0) {
        return falloDeJob(`el layout del workspace no esta bien:\n  - ${problemas.join('\n  - ')}`);
      }
    }

    process.stderr.write('bootstrap-workspace: el hermano esta, el manifiesto esta, y no hace falta instalar.\n');
    process.stderr.write('  Para instalar de verdad: node tools/bootstrap-workspace.mjs\n');
    return 0;
  }

  if (saltarPuppeteer) process.stderr.write('puppeteer: descarga del Chromium OMITIDA.\n');

  process.stderr.write(`bootstrap-workspace: instalando el workspace de ${workspace}\n`);

  const r = pnpm(['install', '--no-frozen-lockfile'],
    { PUPPETEER_SKIP_DOWNLOAD: saltarPuppeteer ? '1' : '0' },
    workspace);

  if (r.status !== 0) {
    const mensaje = `pnpm install salio con ${r.status}. El error de pnpm va arriba, sin filtrar.`;
    return enCi ? falloDeJob(mensaje) : fallo(mensaje);
  }

  // --- 4. la guardia del layout ---------------------------------------------
  // Solo en CI, que es donde `pnpm install` puede terminar en verde dejando las
  // dependencias "workspace:*" sin enlazar, y donde no hay nadie mirando.
  if (enCi) {
    const problemas = problemasDeLayout(workspace, suite, proyecto, paquetes);

    if (problemas.length > 0) {
      return falloDeJob(`el layout del workspace no esta bien:\n  - ${problemas.join('\n  - ')}`);
    }

    const dirEnlaces = resolve(workspace, proyecto, 'node_modules', '@abdsynths');
    const enlazados = readdirSync(dirEnlaces);

    for (const e of enlazados) {
      process.stderr.write(`  ${e.padEnd(16)} -> ${resolve(dirEnlaces, e)}\n`);
    }

    process.stderr.write(`Workspace listo: ${enlazados.length} paquete(s) @abdsynths/* `
      + `enlazados en ${proyecto}.\n`);
    return 0;
  }

  process.stderr.write('bootstrap-workspace: listo. Las pruebas de MidiKeyboard: pnpm test\n');
  return 0;
}

process.exit(main());