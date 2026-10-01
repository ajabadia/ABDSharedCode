// Lectura y evaluación de `.gitattributes`, en funciones PURAS: reciben texto y
// una lista de ficheros, y devuelven estructuras. Nada lee el disco ni llama a
// git, y por eso al guard se le pueden dar reglas inventadas y comprobar que
// las detecta. Un guard que solo sabe decir que el repo actual está bien no es
// un guard: es una fotografía.
//
// ─────────────────────────────────────────────────────────────────────────
// QUÉ GUARDA, Y POR QUÉ ESTE REPO NO TIENE EL DE SUS HERMANOS
//
// ABDEep y ABDSharedAssets tienen un guard casi idéntico para lo mismo: todo
// artefacto GENERADO tiene que estar declarado en `.gitattributes`. Aquí el
// caso mayoritario es el otro: este repo es C++ y lo que se cuela son
// BINARIOS, no `.gen`. Un `.obj` de 40 MB, un `.lib`, un `.exe` de un
// toolchain, una demo en `.gif` de 224 KB, y un `.pyc` que el propio
// `.gitignore` de este repo ya prohíbe desde la línea 53.
//
// O sea: el motor de patrones (que NO puede cambiar, o el guard miente) es el
// mismo que el de los hermanos, y las SEÑAS son de aquí. Está copiado, no
// importado, porque los tres repos son independientes y no comparten
// dependencias; por eso la parte copiada es la que tiene que ser idéntica
// línea a línea, y las pruebas de este fichero la comprueban contra casos
// inventados en vez de contra lo que hay en disco.
//
// ─────────────────────────────────────────────────────────────────────────
// EL DETALLE QUE HACE FALLO ESTE GUARD, Y QUE ESTÁ EN LA COPIA
//
// Un patrón de `.gitattributes` es un patrón tipo `.gitignore`, donde `*` NO
// cruza el `/`:
//
//     Certification/*.pyc   NO cubre Certification/__pycache__/x.pyc
//
// Eso no es un detalle de la especificación: es exactamente el fallo que
// dejó inerte la regla de los bancos en ABDEep durante años. La regla estaba
// escrita, se leía como una protección, y no cubría ni uno de los ocho
// bancos porque estaban un nivel más abajo. Por eso `cubreLa` pregunta "qué
// ficheros cubre ESTA regla" y no "¿qué dice ESTA regla": contra una regla
// inerte no hay nada que contrastar si no se mira a quién cubre.

/** Una regla: el patrón tal cual, sus atributos, y el texto original. */
function reglaDe (linea) {
  const texto = linea.trim();

  if (texto === '' || texto.startsWith('#')) return null;

  // La forma con comillas: "*.txt" text eol=lf
  const entrecomillado = /^\"([^\"]+)\"\s+(.*)$/.exec(texto);

  if (entrecomillado) {
    return { patron: entrecomillado[1], atributos: entrecomillado[2].split(/\s+/), cruda: texto };
  }

  const partes = texto.split(/\s+/);

  if (partes.length < 2) return null;

  return { patron: partes[0], atributos: partes.slice(1), cruda: texto };
}

/** Todas las reglas de un `.gitattributes`, sin comentarios ni macros. */
function reglasDe (texto) {
  return texto
    .split('\n')
    .map(reglaDe)
    .filter((r) => r !== null)
    // Un patrón entre corchetes es una macro de atributo, no una regla de
    // ficheros, y no se aplica a ningún path.
    .filter((r) => !r.patron.startsWith('['));
}

/**
 * Compila un patrón de `.gitattributes` a una expresión regular.
 *
 * TRES cosas que un `new RegExp(patron.replace('*', '.*'))` ingenuo haría mal:
 *
 *   - `*` no cruza `/`. Sin esto, `resources/banks/*.syx` casaría con un
 *     fichero de un subdirectorio, que es justo el fallo que hizo inerte la
 *     regla de los bancos.
 *   - `.` hay que escaparlo, para que un nombre con punto no case con
 *     cualquier cosa.
 *   - Un patrón SIN `/` NO va anclado a la raíz: `*.pyc` matchea en CUALQUIER
 *     directorio. Este es el caso contrario al de los bancos, y es el que hace
 *     falta aquí, porque los `.pyc` viven en `__pycache__/`.
 */
function patronARegex (patron) {
  const anclaAlFinal = !patron.includes('/');
  let salida = anclaAlFinal ? '(?:.*/)?' : '';
  let i = 0;

  while (i < patron.length) {
    const c = patron[i];

    if (c === '*') {
      if (patron[i + 1] === '*') {
        // `**` cruza directorios. Se come el `/` que lo separa si lo hay.
        if (patron[i + 2] === '/') {
          salida += '(?:.*/)?';
          i += 3;
          continue;
        }
        salida += '.*';
        i += 2;
        continue;
      }
      salida += '[^/]*';
      i += 1;
      continue;
    }

    if (c === '?') {
      salida += '[^/]';
      i += 1;
      continue;
    }

    if (c === '[') {
      const cierre = patron.indexOf(']', i + 1);
      if (cierre !== -1) {
        let clase = patron.slice(i + 1, cierre);
        if (clase.startsWith('!')) clase = '^' + clase.slice(1);
        salida += '[' + clase + ']';
        i = cierre + 1;
        continue;
      }
    }

    salida += c.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    i += 1;
  }

  return new RegExp('^' + salida + '$');
}

/** Qué ficheros de `ficheros` cubre una regla. */
function cubreLa (regla, ficheros) {
  const rx = patronARegex(regla.patron);
  return ficheros.filter((f) => rx.test(f));
}

/**
 * Si la regla declara el fichero como BINARIO, de las dos maneras que hay.
 *
 * Las dos existen y no son lo mismo: `binary` es la abreviatura que git
 * define como `-diff -merge -text`, y `-text` a secas dice lo mismo respecto a
 * los saltos de línea pero deja el diff activo. Para este guard basta con
 * cualquiera de las dos: lo que se busca es que git NO toque el contenido, y
 * las dos lo consiguen. Fijar solo `text` sin `eol` NO cuenta: convertirse a
 * texto es precisamente el daño.
 */
function declaraBinario (regla) {
  return regla.atributos.some((a) => a === 'binary' || a === '-text');
}

/**
 * Si la regla obliga a LF en checkout, que es lo que hace falta para un
 * artefacto que se COMPARA.
 *
 * Hace falta distinguir tres cosas que un `includes` a secas no distingue: que
 * no hay ninguna regla, que hay una que dice otra cosa, y que hay una regla
 * con glob que sí cubre el fichero. Solo la tercera cuenta.
 */
function fijaLf (regla) {
  if (regla === null) return false;
  return regla.atributos.some((a) => a === 'text' || a.startsWith('text=')) &&
    regla.atributos.includes('eol=lf');
}

/** Las reglas que de verdad declaran `ruta` como binario. */
function reglasQueDeclaranBinario (ruta, texto) {
  return reglasDe(texto).filter((r) => declaraBinario(r) && patronARegex(r.patron).test(ruta));
}

/** Las reglas que de verdad fijan `ruta` a LF. */
function reglasQueFijan (ruta, texto) {
  return reglasDe(texto).filter((r) => fijaLf(r) && patronARegex(r.patron).test(ruta));
}

/**
 * Por encima de esto un fichero es GRANDE, y a partir de aquí "cambió" ya no
 * significa "cambió una línea".
 *
 * 128 KB y no 1 MB porque el umbral tiene que estar donde el problema empieza a
 * ser visible, no donde todavía no molesta: un fichero de texto de 200 KB con
 * los saltos de línea cambiados da un diff del fichero entero, que es un
 * revisión que nadie lee y un `git blame` que no señala a nadie. El `.gif` de
 * la demo de teclado pesa 224 KB, y por eso está en este lado: no por ser un
 * `.gif`, sino por ser algo que ocupa más que una pantalla de diff.
 */
const UMBRAL_PESADO = 128 * 1024;

/**
 * Extensiones que son binarias por DEFINICIÓN, se detecting o no bytes NUL.
 *
 * Los NUL solos no bastan, y el motivo es concreto: un `.pyc` empieza con
 * bytes NUL pero un `.lib` de un toolchain puede no tenerlos en los primeros
 * 8 KB, y un fichero que parece texto cuando no lo es es justo el caso que
 * este guard tiene que cazar. Al revés también: un `.h` con un `0x00` pegado
 * dentro sigue siendo un `.h`, y no por eso hay que declararlo binario.
 */
const EXTENSIONES_BINARIAS =
  /\.(pyc|obj|lib|a|so|dll|dylib|exe|pdb|ilk|o|a|bin|rom|syx|wav|mp3|ogg|zip|png|jpg|jpeg|gif|webp|icns|woff2?|ttf|otf|pdf)$/i;

/**
 * LAS SEÑAS DE UN ARTEFACTO: lo que permite que un artefacto nazca protegido
 * sin que nadie tenga que acordarse de añadirlo a una lista.
 *
 * TRES SEÑAS, y ninguna bastante por sí sola:
 *
 *   - `binario`: tiene bytes NUL, o una extensión que no puede ser texto. Es
 *     la señal de este repo, y la que encuentra los `.pyc` y los `.gif`.
 *   - `voluminoso`: pasa de `UMBRAL_PESADO`. No lo hace sospechoso, lo hace
 *     caro de no declarar: si además es texto, sus saltos de línea pueden
 *     cambiar sin que nadie lo decida.
 *   - `generado`: `AUTO-GENERATED` en las cinco primeras líneas, una ruta de
 *     caché de Python (`__pycache__/`, `.pyc`), o un nombre de artefacto
 *     (`.gen.*`, `.data.json`). Los generadores de ABDEep ponen la cabecera, y
 *     es la misma señal que usan los hermanos, para que el descubrimiento se
 *     comporte igual en los tres.
 *
 * LO QUE ESTA SEÑAL NO RESUELVE, Y HAY QUE DECIRLO. No descubre un artefacto
 * nuevo y desconocido: descubre los que el generador ha decidido marcar. Un
 * guard que promete descubrirlo todo sin marcar nada es un guard que descubre
 * cero, que es el peor resultado posible porque es verde.
 */
function senasDeArtefacto (fichero) {
  const { ruta, contenido, bytes } = fichero;
  const senas = [];
  const texto = contenido || '';
  const cabeza = typeof bytes === 'string' ? bytes.slice(0, 8000) : '';

  if (EXTENSIONES_BINARIAS.test(ruta) || cabeza.indexOf('\u0000') !== -1) senas.push('binario');

  if (typeof bytes === 'number' && bytes > UMBRAL_PESADO) senas.push('voluminoso');

  if (
    /(^|\/)__pycache__\//.test(ruta) ||
    /\.pyc$/.test(ruta) ||
    /\.gen\.[a-z]+$/.test(ruta) ||
    /\.data\.json$/.test(ruta) ||
    texto.split('\n').slice(0, 5).join('\n').includes('AUTO-GENERATED')
  ) senas.push('generado');

  return senas;
}

/**
 * El atributo que necesita, y que si no tiene lo deja desprotegido.
 *
 * La dirección importa: declararle `text eol=lf` a un `.gif` no lo protege,
 * lo da por bueno como texto, y declararle `binary` a un artefacto generado en
 * JSON tampoco, porque entonces el diff byte a byte de cada regeneración
 * sigue dependiendo del eol de cada máquina. Por eso la respuesta es una función
 * de las SEÑAS y no del nombre.
 */
function atributoEsperado (artefacto) {
  return artefacto.senas.indexOf('binario') !== -1 ? 'binary' : 'text eol=lf';
}

/** Un artefacto: un fichero con al menos una de las señas. */
function esArtefacto (fichero) {
  return senasDeArtefacto(fichero).length > 0;
}

/**
 * Los artefactos de una lista de ficheros, cada uno con la seña que lo delató.
 *
 * Devolver la seña es lo que permite que un fallo diga POR QUÉ se considera
 * artefacto: "binario y sin declarar" es un diagnóstico distinto de "generado
 * y sin fijar", y quien lo lee tiene que saber si busca un `.gitignore` o un
 * `.gitattributes`.
 */
function descubreArtefactos (ficheros) {
  return (ficheros || [])
    .map((f) => ({ ruta: f.ruta, senas: senasDeArtefacto(f), bytes: f.bytes }))
    .filter((f) => f.senas.length > 0);
}

/**
 * Los artefactos que NADIE declara, cada uno con el atributo que le falta.
 *
 * Un binario sin `binary` se juega en dos conversiones de fin de línea, y un
 * artefacto generado o grande sin `eol=lf` se juega en que dos personas
 *checkout en dos máquinas distintas produce un diff del fichero
 * entero. Ninguna de las dos frases la dice el repo: se ve cuando pasa.
 */
function artefactosSinDeclarar (artefactos, texto) {
  return (artefactos || [])
    .map((a) => {
      const falta = atributoEsperado(a);
      const hay = falta === 'binary'
        ? reglasQueDeclaranBinario(a.ruta, texto).length > 0
        : reglasQueFijan(a.ruta, texto).length > 0;

      return hay ? null : { ruta: a.ruta, senas: a.senas, falta };
    })
    .filter((a) => a !== null);
}

/**
 * Si una regla es PREVENTIVA: declara una intención sobre una extensión o una
 * familia de rutas, y no sobre un fichero concreto.
 *
 * POR QUÉ HACE FALTA. Este `.gitattributes` declara `*.a binary`, y aquí no hay
 * ni un `.a`: es una regla de futuro, y es correcta. Un guard que exige "toda
 * regla cubre un fichero" las marca a todas como error, y lo único que se
 * puede hacer con ese guard es apagarlo.
 *
 * Un patrón como `sources/plataformas/windows.cpp` NO empieza por `*.`, así que
 * cuenta como ruta concreta, que es lo que es: si ese fichero no existe, la
 * ruta está mal escrita o se ha movido, y las dos cosas son fallos silenciosos.
 */
function esPreventiva (regla) {
  return /^\*\./.test(regla.patron) || regla.patron.includes('**');
}

/**
 * Las reglas que no cubren NINGÚN fichero, separadas en error y declarativas.
 * Separarlas es el punto: mezclarlas convertía al guard en algo que hay que
 * apagar en cuanto un repo declara su futuro.
 */
function reglasInertes (reglas, ficheros) {
  const inertes = reglas.filter((r) => cubreLa(r, ficheros).length === 0);

  return {
    errores: inertes.filter((r) => !esPreventiva(r)),
    preventivas: inertes.filter(esPreventiva)
  };
}

export {
  UMBRAL_PESADO,
  EXTENSIONES_BINARIAS,
  reglaDe, reglasDe, patronARegex, cubreLa,
  declaraBinario, fijaLf,
  reglasQueDeclaranBinario, reglasQueFijan,
  senasDeArtefacto, atributoEsperado, esArtefacto,
  descubreArtefactos, artefactosSinDeclarar,
  esPreventiva, reglasInertes
};