#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Audita que todo lo que hay en ABDSharedCode este de verdad conectado a un
proyecto del suite, y saca a la luz lo que se queda colgando.

POR QUE EXISTE. El modulo compartido se ha ido llenando de extracciones a
medias, y una extraccion a medias es casi invisible: el fichero esta ahi, se
documenta, se prueba con sus propios tests, y no lo incluye NINGUN producto. Es
la forma mas comoda de perder trabajo sin enterarse. Esto lo mide.

QUE MIDE, Y POR QUE NO BASTA CON "grep el nombre". Un consumidor puede escribir
`#include "../hardware/AiraSysExController.h"` (relativa) y con eso apuntar a SU
copia privada mientras el modulo compartido sigue en el include path, sin que
nadie se entere: el nombre coincide, el fichero no. Por eso aqui no se busca el
NOMBRE, se resuelve cada #include y se pregunta a que path apunta de verdad.

Salida:
  0  solo hay huerfanos de la lista blanca (estado conocido)
  1  hay huerfanos NUEVOS (fuera de ALLOWLIST y, si se paso --baseline, fuera de
     la linea base), o una entrada de lista blanca que un producto ya consume,
     o una entrada de la linea base que YA NO es huerfana, o un TESTS_PROPIOS
     que el modulo dejo de compilar  -> para el CI
  2  error de uso, o excepcion no prevista (no es un hallazgo: no se llego a
     comprobar nada)
  3  NO SE HA PODIDO COMPROBAR: no hay ningun proyecto de la suite al lado, y
     este auditor mide una propiedad de TODA la suite, no de este repo. Sin los
     hermanos no sabe quien consume que, y su respuesta no es "nadie lo usa":
     es "no mire". El 3 existe para que un job no lo lea como un verde ni como
     un rojo.

EL TRINQUETE, Y POR QUE HACE FALTA. Este script compara este repo contra una FOTO
de la suite, y esa foto va siempre por detras: una fuente nueva que aun no ha
llegado a ningun HEAD de los hermanos se ve sin consumidor aunque un producto la
use de verdad. Medido contra los SHA fijados del workflow: 55. En el arbol de
trabajo, con lo que hay sin commitear: 0.

Con eso, hay dos salidas malas y una buena:

  - Dejar el job en rojo siempre: el primer dia de CI nace roto, y un CI rojo el
    primer dia es un CI que nadie mira.
  - Poner `continue-on-error` en el workflow: el job pasa en verde sin mirar, que
    es el peor resultado posible porque es verde.

La buena es un TRINQUETE, y `tools/audit-baseline.json` lo es. Una fuente sin
consumidor que ya esta en la linea base NO es un fallo nuevo, y el codigo sale
0. Una que no esta, sale 1. Y al reves tambien muerde: una entrada de la linea
base que YA NO es huerfana es un FALLO, porque si no, la linea base se convierte
en un escudo permanente y nadie la poda nunca.

O sea: esto no es una lista de excepciones, es una lista de deuda con el
inventario al dia.

Y el inventario va con FECHA. La linea base que escribe --write-baseline lleva
una clave `foto` con el SHA exacto de cada repo hermano en el disco donde se
midio, porque sin ella el trinquete solo sabe que "estas 55 no las consume
nadie" y no CONTRA QUE se midio eso. Medido: con un commit nuevo en ABDNeural que
no toca consumidores, el audit sale 0 (siguen siendo las mismas 55 huerfanas y
los mismos nombres) y no hay forma de saber que se ha movido el suelo. Eso lo
vigila `tools/verificar_foto.py`, que compara esa foto con los SHA que clona
el workflow.

--baseline <fichero>   activa el TRINQUETE contra esa linea base de huerfanas
                         conocidas. Sin este flag la linea base NO se lee, ni
                         aunque tools/audit-baseline.json este ahi al lado.
  --write-baseline       reescribe tools/audit-baseline.json con ESTA pasada
  --help

POR QUE --baseline ES UN FLAG Y NO UN DEFECTO. La linea base commiteada esta
medida contra una foto de la suite: los repos hermanos en los SHA que fija
.github/workflows/shared-code-ci.yml. En local, al lado, esos mismos repos tienen
trabajo SIN COMMITEAR (varios ficheros en M, ??), asi que las 55 huerfanas de CI
NO son huerfanas aqui: sus consumidores existen en el disco. Si la linea base se
leyera siempre, en local saldrian 55 "entradas que ya no son huerfanas" y el repo
se quedaria en rojo para siempre, con un arreglo (--write-baseline) que ademas
destruye la proteccion de CI. Un rojo permanente es un rojo que nadie mira.

Asi que: en local, sin --baseline, el trinquete no aplica y sale 1 solo si hay
huerfanas de verdad, que es el modo estricto. En CI, el workflow pasa
--baseline explicitamente y las dos reglas (nueva por encima, y entrada que hay
que podar) estan activas.

UN CLON DE ESTE REPO AL LADO NO ES UN PRODUCTO DE LA SUITE. El auditor mide que
fuentes consume cada proyecto HERMANO, y un segundo clon de ABDSharedCode se
parecia a uno: no se llama ABDSharedCode, asi que el filtro por nombre lo dejaba
pasar y el veredicto se invertia con un motivo FALSO (la linea base decia "17
entradas ya no son huerfanas" cuando lo unico que habia pasado es que habia una
carpeta de mas). Se reconoce porque trae ESTE MISMO SCRIPT, que ningun producto
de la suite tiene, y se ignora con su motivo impreso. Se probo con el remoto de
git como criterio y no vale: un clon de un clon (que es lo que se hace al montar
un sandbox) tiene el remoto apuntando al clon intermedio, no a origin. Medido:
con 0, 1 y 3 clones de mas al lado, el veredicto es identico.

QUE CUENTA COMO HUERFANA. Una fuente es huerfana si ningun producto la enlaza Y
el modulo tampoco la compila en sus propios tests. Las dos mitades hacen falta:
sin la primera, el modulo se llenaria de trabajo a medias que nadie integra;
sin la segunda, las cabeceras que solo usa el test standalone del modulo saldrian
como huerfanas nuevas en cada pasada, que es un rojo perpetuo que no significa
ningun problema y por lo tanto no obliga a arreglar nada.

Uso:
  python tools/audit_unconsumed_sources.py           # informe
  python tools/audit_unconsumed_sources.py --check   # sin informe, pero EN ROJO DICE POR QUE
  python tools/audit_unconsumed_sources.py --check --baseline tools/audit-baseline.json
                                                     # idem, con el TRINQUETE (lo que hace CI)
"""

import io
import json
import os
import re
import subprocess
import sys
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = os.path.abspath(os.path.join(HERE, '..'))

# El nombre de este mismo fichero, para reconocer a otro clon de este repo al
# lado (ver `hermanos` en main). Se calcula en vez de escribirlo a mano porque
# un nombre escrito a mano se queda viejo el dia que el script se renombre, y
# entonces el marcador dejaria de funcionar en silencio.
ESTE_SCRIPT = os.path.basename(__file__)
SUITE = os.path.abspath(os.path.join(SHARED, '..'))

EXTS = ('.h', '.hpp', '.hh', '.cpp', '.c', '.cxx', '.ixx', '.mm')

SKIP = ('node_modules', '.git', '_backups', '_Deprecados', '_RESOURCES', 'dist',
        '.next', 'JuceLibraryCode', 'CMakeFiles', '.pnpm-store', 'build',
        'build-release', 'build-debug', 'build-reference', 'exports', 'DOCS',
        'dist-newstyle', '_deps')

# `_Deprecados` esta en SKIP a proposito: es el ATICO del modulo, no fuente. Un
# fichero que se movio alli esta retirado de la circulacion por decision, y
# contarlo como "fuente sin consumidor" seria ruido: ya se documento por que se
# fue. Si algo de alli vuelve a hacer falta, sale de alli con su historia.

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)

# Rutas de fuente en CMake, con o sin prefijo de variable:
#   Segmented/SegmentedProbe.cpp
#   ${ABDSHAREDCODE_DIR}/HardwareDrivers/NRPNParser.cpp
#   ${CMAKE_CURRENT_SOURCE_DIR}/../ABDSharedCode/Visualizers/X.cpp
SRC_RE = re.compile(r'([^\s"\';()\[\],]*\.(?:cpp|c|cxx|mm))')
VAR_RE = re.compile(r'\$\{[^}]*\}/')


def strip_vars(path):
    """Deja solo la cola de la ruta, sin ${VAR}/ ni barras de entrada."""
    return VAR_RE.sub('', path).lstrip('/')

# ---------------------------------------------------------------------------
# Lista blanca: huerfanos CONOCIDOS, con el motivo. Anadir aqui es una decision
# consciente; lo que no este y aparezca, sale como NUEVO.
# ---------------------------------------------------------------------------
# Las nueve entradas _BANCO del modulo BankManager se borraron el 2026-10-06,
# el mismo dia en que ABDBankManager entro en la foto de productos del workflow
# (el corte, en ABDBankManager/DOCS/bank-manager-module-cut.md). Eran exactamente
# lo que su propio comentario prometia: "cuando ABDBankManager entre en la foto,
# estas entradas sobraran y el trinquete obligara a borrarlas". Con el consumidor
# en la foto, el script las marcaba como entrada de lista blanca ya consumida y
# salia con 1 hasta que se quitaran. El modulo sigue en BankManager/, ahora
# consumido y sin figurar en esta lista.
# ---------------------------------------------------------------------------
ALLOWLIST = {
    'DspEffects/MultiHeadEcho.h':
        'motor de maquina sin todavia consumidor; sus 3 defectos se corrigieron y '
        'prueban aqui, y un 4º candidato (releer la linea de cinta) se midio y se '
        'descarto: el compilador ya lo hace y el tiempo no separa (guia, seccion '
        '"Auditoria de DspEffects")',
    'DspEffects/RingMod.h':
        'motor de maquina sin todavia consumidor; gemelo de MultiHeadEcho',
    'DspEffects/characters/TapeColour.h':
        'etapa de caracter de RE-201; solo la usa MultiHeadEcho',
    'DspEffects/characters/DiodeBridge.h':
        'etapa de caracter de RE-201; solo la usa MultiHeadEcho',
    'DspEffects/profiles/Re201Profile.h':
        'perfil de datos de RE-201; solo lo usa MultiHeadEcho',
    'LcdDisplay/LcdDisplay.h':
        'MODULO INERTE: la migracion al WebUI termino (lo dice el comentario de '
        'NEURONiKEditor.h) pero los ficheros se quedaron. Propuesta: borrar',
    'LcdDisplay/LcdMenuManager.h':
        'MODULO INERTE: idem LcdDisplay.h. Propuesta: borrar',
    'LcdDisplay/LcdDisplayProbe.cpp':
        'MODULO INERTE: no esta en ningun target de compilacion, ni propio',
    'Segmented/Segmented.h':
        'puerto nativo del Segmented de ABDSharedAssets; tiene target y probe '
        'propios pero ningun proyecto lo incluye todavia',
    'Segmented/SegmentedProbe.cpp':
        'el probe de Segmented; lo compila el CMake del modulo, ningun proyecto',
    'DspCore/DspMathAudit.cpp':
        'arnas de medicion de DspMath; a proposito fuera de CMake, se compila a mano',
    'DspCore/DspMathBitIdent.cpp':
        'arnas de identidad bit a bit de DspMath; a proposito fuera de CMake',
    'DspCore/DspMathHarness.h':
        'arnas de paridad WASM<->nativo y de coste de DspMath; se compila dos veces '
        'con el mismo fuente, a nativo y a wasm32, y a proposito fuera de CMake',
    'DspCore/DspMathHarness.cpp':
        'el arnes anterior en su unico fuente; lo corre tools/run_dsp_math_harness.py',
    'DspCore/DspCoreTests.cpp':
        'el test standalone del propio modulo; lo compila su CMake, ningun producto',
    'DspEffects/DspEffectsTests.cpp':
        'el test standalone del propio modulo; lo compila su CMake, ningun producto',
    'SynthCore/SynthCoreTests.cpp':
        'el test standalone del propio modulo; lo compila su CMake, ningun producto',
    'Scope/Source/tests/StandaloneSmoke.cpp':
        'el smoke standalone del propio modulo (target ABDScope_CppSmoke); lo '
        'compila su CMake sin condiciones, ningun producto lo enlaza',
    'Scope/Source/tests/TestScopeTap.cpp':
        'test GTest del propio modulo (BUILD_TESTING + GTest en su CMake); lo '
        'compila su CMake, ningun producto',
    'Scope/Source/tests/TestSpscRingBuffer.cpp':
        'idem TestScopeTap.cpp: test GTest del modulo Scope, sin producto que lo '
        'enlace',
    'Scope/Source/tests/TestTriggerDetector.cpp':
        'idem TestScopeTap.cpp: test GTest del modulo Scope, sin producto que lo '
        'enlace',
    'Scope/Source/StandaloneDemo/Main.cpp':
        'demo nativa standalone de Scope; su compuerta CMAKE_SOURCE_DIR == '
        'CURRENT_SOURCE_DIR la excluye de todo build de producto (y de este)',
    'Scope/Source/JUCE/JuceScopeComponent.h':
        'componente JUCE nativo NO WebView del modulo; hoy solo lo incluye el demo '
        'standalone, los productos enlazan JuceWebScopeComponent',
    'Scope/Source/JUCE/ScopeResourceProvider.cpp':
        'no lo incluye nadie (es .cpp): lo propaga ABDShared::ScopeCore via '
        'target_sources INTERFACE a cada consumidor, y el mapa target->cpp de este '
        'script solo resuelve targets llamados ABDShared_ del CMakeLists raiz',
}


# Las unidades de traduccion que el modulo compila en SU PROPIO CMake para
# probarse a si mismo. No las consume ningun producto, asi que no son raices de
# "alcanzable desde un producto": y sin esta lista, lo que ellas incluyen sale
# como huerfano NUEVO en cada pasada.
#
# El caso que obliga a esto: `SynthCoreTests.cpp` incluye los cinco
# `SynthCore/S950*.h`, que son el trabajo mas reciente y mas comentado del
# modulo, y el auditor los declaraba "HUERFANAS NUEVAS (no estaban en la lista
# blanca)" con codigo de salida 1. No era verdad: se compilan y se ejecutan en
# cada `ctest` del modulo. Lo que el veredicto no tenia forma de decir es "no lo
# usa ningun producto, pero si su propio test", que es una situacion distinta de
# "no lo usa nadie" y no puede compararse con ella.
#
# NO vale con anadir CUALQUIER entrada de la lista blanca como raiz. Si
# `MultiHeadEcho.h` fuera raiz, arrastraria a `TapeColour.h`, `DiodeBridge.h` y
# `Re201Profile.h`, que solo se usan desde el y no los compila nadie: pasarian de
# huerfanos a "cubiertos" y la lista blanca se quedaria obsoleta al reves. Aqui
# solo entran las TU que el modulo COMPILA de verdad, que se comprueba abajo.
TESTS_PROPIOS = {
    'DspCore/DspCoreTests.cpp',
    'DspEffects/DspEffectsTests.cpp',
    'SynthCore/SynthCoreTests.cpp',
    'Segmented/SegmentedProbe.cpp',
    'Scope/Source/tests/StandaloneSmoke.cpp',
}


def norm(p):
    return os.path.normpath(os.path.abspath(p)).replace('\\', '/')


def walk_sources(base):
    for dp, dn, fn in os.walk(base):
        dn[:] = [d for d in dn if d not in SKIP and not d.startswith('.')]
        for f in fn:
            if f.endswith(EXTS):
                yield norm(os.path.join(dp, f))


def read(path):
    try:
        return io.open(path, 'r', encoding='utf-8', errors='ignore').read()
    except OSError:
        return ''


def read_own_cmake():
    """El CMakeLists del propio modulo compartido."""
    return read(os.path.join(SHARED, 'CMakeLists.txt'))


def remoto_de (directorio):
    """
    La URL del remoto `origin` de un repo, o None si no se puede saber (no es un
    repo, no hay remoto, o no hay git en la maquina).

    None y "" se distinguen a proposito: None es "no se pudo mirar", que obliga a
    seguir con el criterio antiguo por nombre; "" es "es un repo sin remoto", que
    es informacion de sobra. La funcion que la usa solo compara contra el remoto
    propio, y ahi None y "" se comportan igual de forma segura: no hay nada que
    comparar y se cuenta como lo que sea por su nombre.

    Se mira `git remote get-url origin` en vez de leer `.git/config` a mano: es la
    misma respuesta que daria un humano, y no tiene los casos raros del
    `.git/config` (includes, url en vez de url, repos sin .git).
    """
    try:
        r = subprocess.run(['git', '-C', directorio, 'remote', 'get-url', 'origin'],
                           capture_output=True, text=True)
    except (OSError, ValueError):
        return None

    if r.returncode != 0:
        return None
    return r.stdout.strip()


def project_of(path):
    rel = os.path.relpath(path, SUITE).split(os.sep)
    return rel[0] if len(rel) > 1 else '(raiz)'


LINEA_BASE_POR_DEFECTO = os.path.join(HERE, 'audit-baseline.json')


def leer_linea_base (ruta):
    """
    Las huerfanas que ya se sabian. Un JSON con `fuentes` (lista de rutas
    relativas), `nota` (por que estan ahi) y `foto` (los SHA contra los que se
    midio). Devuelve (conjunto, foto, texto_de_error).

    Un fichero que no existe NO es un error: sin linea base este script sigue
    funcionando, solo que sin red. Eso si, el error se distingue del "no hay
    linea base" por el texto, porque "no hay linea base" y "la linea base esta
    rota" no son lo mismo y uno es transitorio y el otro no.

    La `foto` es lo que separa "esta linea base describe estos 55 nombres" de
    "esta linea base describe estos 55 nombres MEDIDOS CONTRA ESTOS SHA". Sin
    ella, subir un SHA en el workflow deja el audit en verde y la linea base
    sigue anunciando una foto que ya nadie ha medido. Se devuelve como
    diccionario vacio cuando no esta, y el guard de tools/verificar_foto.py es
    el que dice que eso no vale.
    """
    if not os.path.isfile(ruta):
        return set(), {}, None

    try:
        datos = json.load(io.open(ruta, 'r', encoding='utf-8'))
    except (ValueError, OSError) as exc:
        return set(), {}, 'no se pudo leer %s: %s' % (ruta, exc)

    if not isinstance(datos, dict) or not isinstance(datos.get('fuentes'), list):
        return set(), {}, '%s no tiene la forma {"fuentes": [...], "nota": "..."}' % ruta

    foto = datos.get('foto')
    if foto is not None and not isinstance(foto, dict):
        return set(), {}, '%s: la clave "foto" tiene que ser un objeto {proyecto: sha}' % ruta

    return set(datos['fuentes']), (foto or {}), None


def escribir_linea_base (ruta, nombres, nota, foto):
    """
    Reescribe la linea base. Solo con --write-baseline: es una decision.

    La `foto` va dentro a proposito. Una linea base sin el SHA contra el que se
    midio dice "estas 55 fuentes no las consume nadie", y eso es una verdad sin
    fecha: sigue siendo verdad mañana aunque los hermanos hayan cambiado, y por
    eso subir un SHA en el workflow no la obliga a nada. Con la foto escrita, el
    SHA es un dato y no un comentario, y tools/verificar_foto.py puede
    compararlo con el del workflow.
    """
    cuerpo = collections.OrderedDict()
    cuerpo['nota'] = nota
    cuerpo['generado'] = 'python tools/audit_unconsumed_sources.py --write-baseline'
    cuerpo['foto'] = collections.OrderedDict(sorted(foto.items()))
    cuerpo['fuentes'] = sorted(nombres)

    with io.open(ruta, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(cuerpo, indent=2, ensure_ascii=False))
        f.write('\n')


def foto_de_la_suite (hermanos):
    """
    El SHA exacto contra el que se midio esta pasada. De cada repo hermano
    `git rev-parse HEAD`, en el disco donde el script esta mirando.

    Un repo al que no se le puede preguntar el HEAD NO se salta en silencio: se
    anota como null. La razon es que una foto con un hueco es visible (el guard
    la rechaza) mientras que una foto a la que le falta un repo del todo parece
    completa, y esa es la forma de que el guard deje de servir para algo.
    """
    foto = collections.OrderedDict()
    for nombre in hermanos:
        try:
            r = subprocess.run(['git', '-C', os.path.join(SUITE, nombre), 'rev-parse', 'HEAD'],
                               capture_output=True, text=True)
        except (OSError, ValueError):
            r = None
        sha = r.stdout.strip() if r is not None and r.returncode == 0 else None
        foto[nombre] = sha if sha else None
    return foto


def main ():
    check_only = '--check' in sys.argv
    escribir = '--write-baseline' in sys.argv

    (args, cola) = _parsear(sys.argv[1:])
    desconocidos = [a for a in cola if a not in ('--baseline',)]
    if args.get('--baseline') and args['--baseline'][1:]:
        desconocidos += args['--baseline'][1:]

    if desconocidos:
        print('audit_unconsumed_sources: argumento desconocido: %s' % desconocidos[0], file=sys.stderr)
        print('  --check | --baseline <fichero> | --write-baseline | --help', file=sys.stderr)
        return 2

    if '--help' in args or '-h' in args:
        print(__doc__.strip())
        return 0

    # `--baseline <fichero>`. Antes estaba escrito como
    # `args.get('--baseline', [None, POR_DEFECTO])[1]`, y el `[1]` se leia sobre
    # lo que devuelve el get, no sobre el valor por defecto: con
    # `--baseline ruta.json` la lista tiene un solo elemento y salia un
    # IndexError envuelto en "error: list index out of range", con codigo 2. Un
    # error de invocacion no es un error del script, y un 2 en un job se lee
    # como "se ha roto Python".
    #
    # Y sin `--baseline` la linea base NO se lee aunque este al lado. Ver el
    # docstring: esta es medida contra los SHA de CI, y en local no describe la
    # realidad del disco.
    ruta_base = LINEA_BASE_POR_DEFECTO
    base_conocida, foto_base, error_base = set(), {}, None

    if '--baseline' in args:
        valor = args['--baseline'][-1]
        if valor is True:
            print('audit_unconsumed_sources: --baseline necesita el fichero que sigue',
                  file=sys.stderr)
            return 2
        ruta_base = valor
        base_conocida, foto_base, error_base = leer_linea_base(ruta_base)
    if error_base:
        print('audit_unconsumed_sources: la linea base esta rota: %s' % error_base, file=sys.stderr)
        print('  Una linea base ilegible no se puede ignorar: pasaria todo y el job en', file=sys.stderr)
        print('  verde sin comprobar nada. Arreglala o borrala a proposito.', file=sys.stderr)
        return 1

    # --- 0. esta la suite al lado? --------------------------------------------
    # Se comprueba ANTES de mirar una sola fuente, porque sin hermanos este
    # script no puede distinguir "nadie consume esto" de "no he mirado quien
    # consume esto". Medido: en un clon solo de ABDSharedCode salen 76 huerfanas
    # nuevas, y todas son mentira: son fuentes que ABDEep, ABDMS2000 y ABDNeural
    # consumen de verdad, desde repos que este checkout no tiene.
    #
    # Por eso el codigo es 3 y no 1: un job que recibe 3 sabe que el paso no
    # comprobo nada, y un job que recibe 1 sabe que hay algo que arreglar. Con un
    # solo codigo, un checkout a medias se lee como un hallazgo.
    #
    # QUE ES UN HERMANO, Y POR QUE NO BASTA EL NOMBRE. La condicion de abajo es
    # "hay un .git y el nombre no es el mio", y hay un caso que se le escapa: un
    # SEGUNDO CLON de este mismo repo al lado. No se llama `ABDSharedCode`, asi
    # que no se parece a si mismo, y el auditor lo toma por un producto de la
    # suite que compila las fuentes de ABDSharedCode. Medido: con un clon
    # llamado `ci-solo` al lado, todo archivo del modulo aparece "consumido por
    # {ABDSharedCode, ci-solo}" y el veredicto se invierte: lo que el propio
    # modulo se compila en sus tests pasa aorphan, y el rojo dice que hay que
    # arreglar el codigo cuando lo unico que hay que arreglar es la carpeta de
    # al lado.
    #
    # Se distingue por un MARCADOR, no por el remoto de git. El remoto parece
    # la solucion y no lo es: un clon de un clon (que es justo lo que se hace al
    # montar un sandbox) tiene el remoto apuntando al clon intermedio, no a
    # origin, asi que los dos remotos no coinciden y el clon se cuela. Medido.
    #
    # El marcador es que el directorio contenga ESTE SCRIPT. Un clon de
    # ABDSharedCode lo trae (es el propio repo clonado); ningun producto de la
    # suite lo tiene, comprobado sobre los nueve. Es una pregunta que no depende
    # de git, de la red ni de como se llamo la carpeta, que es justo lo que
    # cambia cuando uno clona para trabajar en otra parte.
    #
    # Si los dos criterios dieran distinto, el remoto gana: el marcador es
    # heuristico y el remoto es un hecho. Pero como el remoto no cubre el caso
    # que importa, el que decide es el marcador.
    mi_remoto = remoto_de(SHARED)

    hermanos = []
    for d in sorted(os.listdir(SUITE)):
        if d in SKIP or not os.path.isdir(os.path.join(SUITE, d, '.git')):
            continue
        if d == os.path.basename(SHARED):
            continue

        dir_proy = os.path.join(SUITE, d)
        clon_de_este = os.path.isfile(os.path.join(dir_proy, 'tools', ESTE_SCRIPT))

        if not clon_de_este and mi_remoto:
            r = remoto_de(dir_proy)
            clon_de_este = bool(r) and r == mi_remoto

        if clon_de_este:
            print('audit_unconsumed_sources: IGNORADO %s: es otro clon de ESTE repo, '
                  'no un producto de la suite.' % d, file=sys.stderr)
            print('  Un clon de mas al lado hace que este script mida lo que compila ese '
                  'clon, y no lo que compila la suite. Lo que se ha medido de mas, y lo '
                  'que se ha medido de menos.', file=sys.stderr)
            continue

        hermanos.append(d)

    if not hermanos:
        print('audit_unconsumed_sources: NO SE HA PODIDO COMPROBAR.', file=sys.stderr)
        print('  Este script mide que fuentes de ABDSharedCode consume ALGUN proyecto de', file=sys.stderr)
        print('  la suite, y los proyectos tienen que estar clonados AL LADO (%s).' % SUITE,
              file=sys.stderr)
        print('  Ahora mismo no hay ninguno, y sin ellos todo parece huerfano.', file=sys.stderr)
        print('  En CI hay que clonarlos antes de llamar a este script; es lo que hace',
              file=sys.stderr)
        print('  .github/workflows/shared-code-ci.yml.', file=sys.stderr)
        print('', file=sys.stderr)
        return 3

    shared = []
    for p in walk_sources(SHARED):
        shared.append((p, os.path.relpath(p, SHARED).replace('\\', '/')))
    shared_set = set(p for p, _ in shared)

    # --- 1. que .cpp del modulo compila cada proyecto ------------------------
    compiled_by = collections.defaultdict(set)   # ruta -> {proyecto}
    # Se recorre `hermanos` y NO el directorio. Los cuatro bucles que miden la
    # suite (este, el de los targets, el de los includes y el del principio) lo
    # hacian cada uno por su cuenta con `sorted(os.listdir(SUITE))` y su propio
    # filtro, y por eso el filtro de "esto es un hermano de verdad" solo se
    # aplicaba a uno de los cuatro: un clon de mas al lado se ignoraba al contar
    # los repos, pero se colaba en los otros tres. Medido: con el clon al lado,
    # ignorarlo aqui no basta, seguian salir 46 huerfanas falsas.
    #
    # Que sea una lista ya filtrada y en un solo sitio es lo que hace que el
    # filtro se pueda auditar. Cuatro copias del mismo criterio son cuatro
    # sitio donde se puede equivocar, y ya se equivoco una.
    #
    # `hermanos` + el modulo, y no solo `hermanos`: antes este bucle recorria el
    # directorio entero, y de ahi que las fuentes del PROPIO modulo se contaran
    # como compiladas por el proyecto `ABDSharedCode`. De ahi sale `self_built`
    # (abajo), que es lo que exime a los tests standalone del modulo de salir
    # como huerfanas. Quitar el modulo de la lista sin querer hace que `who`
    # nunca valga {'ABDSharedCode'}, `self_built` queda vacio y todos esos tests
    # salen como huerfanas nuevas. Medido: 80 en vez de 0, sin ningun clon al
    # lado. El modulo se anade aqui a proposito y no por descuido.
    for proj in [os.path.basename(SHARED)] + hermanos:
        dir_proy = os.path.join(SUITE, proj)
        for dp, dn, fn in os.walk(dir_proy):
            dn[:] = [d for d in dn if d not in SKIP and not d.startswith('.')]
            manifests = [f for f in fn if f == 'CMakeLists.txt' or f.endswith('.cmake')]
            if not manifests:
                continue
            files = set(fn)
            for f in files:                                  # fuentes del directorio
                if f.endswith(('.cpp', '.c', '.cxx', '.mm')):
                    compiled_by[norm(os.path.join(dp, f))].add(proj)
            for mf in manifests:
                txt = read(os.path.join(dp, mf))
                for m in SRC_RE.finditer(txt):
                    tail = strip_vars(m.group(1))
                    if not tail:
                        continue
                    for cand in (norm(os.path.join(dp, tail)),
                                 norm(os.path.join(SHARED, tail)),
                                 norm(os.path.join(SUITE, tail))):
                        if os.path.isfile(cand):
                            compiled_by[cand].add(proj)
                            break

    # modulo compilandose a si mismo (tests y probes propios)
    self_built = {p for p, who in compiled_by.items()
                if who == {os.path.basename(SHARED)}}
    # El nombre del modulo va por la VARIABLE y no escrito a mano en el
    # conjunto de arriba: `os.path.basename(SHARED)` es el mismo criterio que
    # usa el filtro de hermanos, y si un dia el modulo viviera en una carpeta con
    # otro nombre, estas dos piezas no se podrian desincronizar.

    # Un target del modulo que un proyecto ENLAZA (ABDShared::SynthCore,
    # ABDShared::HardwareMidiDetect...) consume sus .cpp aunque el consumidor no
    # los escriba de su puño. Hay dos formas y las dos cuentan:
    #   - add_library(X STATIC  <fuentes>)      -> la compila el modulo
    #   - target_sources(X INTERFACE <fuentes>) -> las propaga al que enlaza
    # Se resuelve el mapa target -> .cpp leyendo el CMakeLists del modulo, en vez
    # de suponerlo por directorio: un INTERFACE sin .cpp (DspCore, DspEffects,
    # LutDSP, LcdDisplay) no puede consumir ninguno, y un target enlazado nunca
    # significa que se use todas las cabeceras de su carpeta.
    own = read_own_cmake()
    target_cpp = collections.defaultdict(set)      # ABDShared_X -> {ruta}

    for m in re.finditer(r'add_library\s*\(\s*ABDShared_([A-Za-z0-9_]+)', own):
        name = m.group(1)
        block = own[m.end():own.find(')', m.end()) + 1]
        for s in SRC_RE.finditer(block):
            for cand in (norm(os.path.join(SHARED, strip_vars(s.group(1)))),
                         norm(os.path.join(SUITE, strip_vars(s.group(1))))):
                if os.path.isfile(cand) and cand.endswith(('.cpp', '.c', '.cxx', '.mm')):
                    target_cpp[name].add(cand)
                    break

    for m in re.finditer(r'target_sources\s*\(\s*ABDShared_([A-Za-z0-9_]+)', own):
        name = m.group(1)
        block = own[m.end():own.find(')', m.end()) + 1]
        for s in SRC_RE.finditer(block):
            for cand in (norm(os.path.join(SHARED, strip_vars(s.group(1)))),
                         norm(os.path.join(SUITE, strip_vars(s.group(1))))):
                if os.path.isfile(cand) and cand.endswith(('.cpp', '.c', '.cxx', '.mm')):
                    target_cpp[name].add(cand)
                    break

    for proj in hermanos:
        dir_proy = os.path.join(SUITE, proj)
        for dp, dn, fn in os.walk(dir_proy):
            dn[:] = [d for d in dn if d not in SKIP and not d.startswith('.')]
            for mf in [f for f in fn if f == 'CMakeLists.txt' or f.endswith('.cmake')]:
                for m in re.finditer(r'ABDShared::([A-Za-z0-9_]+)', read(os.path.join(dp, mf))):
                    for src in target_cpp.get(m.group(1), ()):
                        compiled_by[src].add(proj)

    # --- 2. grafo de includes, resuelto a paths reales -----------------------
    # Un include entrecomillado se resuelve por el include path del proyecto, no
    # solo por el directorio del fichero que incluye: ABDAudioLab compila con
    # `src` en el path, asi que `#include "dsp/JunoBBD.h"` desde `src/tests/`
    # significa `src/dsp/JunoBBD.h`. Sin estas bases, medio suite parece huerfano.
    includes = {}          # fichero -> {ruta absoluta a la que resuelve}
    # Incluye el modulo. Antes este bucle recorria el directorio entero y por eso
    # resolvia los includes de las cabeceras propias del modulo; si se queda solo
    # con los hermanos, esas cabeceras dejan de resolver sus propias rutas
    # relativas y aparecen como huerfanas nuevas. Medido: 25 huerfanas falsas
    # (DspCore/DspMath.h, DspEffects/DspChorus.h, ...) que la version de HEAD no
    # veia. El bucle de targets (arriba) si excluye el modulo a proposito, y los
    # dos criterios son distintos: este resuelve includes, el otro busca quien
    # ENLAZA un target.
    for proj in [os.path.basename(SHARED)] + hermanos:
        dir_proy = os.path.join(SUITE, proj)
        # Las dos ultimas bases NO son del proyecto: son los include dirs que
        # ABDScopeCoreHeaders/ABDScopeCore propagan a CUALQUIER consumidor
        # (Scope/Source y Scope/Source/Core, ver INTEGRATION_GUIDE, "Modulo:
        # Scope"). El consumidor compila `<ScopeDataCollector.h>` y
        # `<JUCE/JuceWebScopeComponent.h>` a pelo porque el target pone esos
        # directorios en su path, y sin ellos aqui el alcance no podria ver que
        # los productos consumen esas cabeceras. Van AL FINAL: la resolucion se
        # queda con la PRIMERA base que acierte, asi que solo los includes que
        # antes no resolvian a ningun sitio ganan arista nueva, y ninguna
        # resolucion existente se mueve.
        proj_bases = [dir_proy, norm(os.path.join(dir_proy, 'src')),
                      norm(os.path.join(dir_proy, 'Source')),
                      norm(os.path.join(dir_proy, 'wasm')), SHARED, SUITE,
                      norm(os.path.join(SHARED, 'Scope', 'Source')),
                      norm(os.path.join(SHARED, 'Scope', 'Source', 'Core'))]
        for p in walk_sources(dir_proy):
            sdir = os.path.dirname(p)
            bases = [sdir] + proj_bases
            outs = set()
            for lit in INCLUDE_RE.findall(read(p)):
                lit = strip_vars(lit.replace('\\', '/'))
                if not lit:
                    continue
                for b in bases:
                    cand = norm(os.path.join(b, lit))
                    if os.path.isfile(cand):
                        outs.add(cand)
                        break
            includes[p] = outs

    # --- 3. alcanzable desde un proyecto PRODUCTO ---------------------------
    reachable = set()
    stack = [p for p, who in compiled_by.items() if who - {'ABDSharedCode'}]
    # solo entra al grafo lo que compila un proyecto de producto
    for p in list(stack):
        includes.setdefault(p, set())
    while stack:
        cur = stack.pop()
        if cur in reachable:
            continue
        reachable.add(cur)
        stack.extend(includes.get(cur, ()))

    #includes que nacen en el propio modulo y siguen hacia dentro
    for p in list(shared_set):
        if p in reachable:
            stack = list(includes.get(p, ()))
            while stack:
                cur = stack.pop()
                if cur in reachable:
                    continue
                reachable.add(cur)
                stack.extend(includes.get(cur, ()))

    # --- 3bis. alcanzable desde los TESTS DEL PROPIO MODULO ------------------
    # Raices distintas y veredicto distinto: aqui se entra por una TU que el
    # modulo compila en su CMake, no por un producto. Lo que se alcanza asi no es
    # "huerfano": se compila y se ejecuta, pero ningun producto lo enlaza, y eso
    # es informacion que el informe debe dar en su propia linea.
    tests_raices, tests_rotos = [], []
    for rel in sorted(TESTS_PROPIOS):
        p = norm(os.path.join(SHARED, rel))
        if not os.path.isfile(p):
            tests_rotos.append((rel, 'el fichero ya no existe'))
        elif p not in self_built:
            tests_rotos.append((rel, 'el CMake del modulo ya no lo compila'))
        else:
            tests_raices.append(p)

    probadas = set(tests_raices)
    stack = list(tests_raices)
    while stack:
        cur = stack.pop()
        for nxt in includes.get(cur, ()):
            if nxt not in probadas:
                probadas.add(nxt)
                stack.append(nxt)

    probadas &= shared_set

    # Las raices no se listan como "cubiertas por los tests": ESAS SON los tests.
    probadas_mostrables = probadas - set(tests_raices)

    # --- 4. veredicto --------------------------------------------------------
    orphans = []
    solo_probadas = []
    for p, rel in sorted(shared, key=lambda t: t[1]):
        if p in reachable:
            continue
        # el .cpp se marca como "inerte" si ademas nadie lo compila
        inert = rel.endswith(('.cpp', '.c')) and p not in compiled_by and p not in self_built
        if p in probadas_mostrables:
            solo_probadas.append((rel, inert))
        else:
            orphans.append((rel, inert))

    known = [o for o in orphans if o[0] in ALLOWLIST]
    new = [o for o in orphans if o[0] not in ALLOWLIST]

    # Una entrada de la lista blanca se declara OBSOLETA cuando la consume un
    # PRODUCTO, que es lo que invalida su motivo ("nadie lo enlaza"). NO cuando la
    # cubre el test del propio modulo: el motivo de `DspEffects/MultiHeadEcho.h`
    # ("motor de maquina sin todavia consumidor") sigue siendo cierto y se sigue
    # pudiendo comprobar, asi que marcarlo obsoleto por el hecho de que ahora hay
    # un test que lo ejercita seria tirar una decision consciente, no detectarla.
    # Medido: con este criterio, cero entradas salen obsoletas.
    listed = [rel for rel in ALLOWLIST
              if norm(os.path.join(SHARED, rel)) in reachable]

    # --- 5. el TRINQUETE ------------------------------------------------------
    # El nombre `base_conocida` y no `base` a proposito: los bucles que recorren
    # la suite usaban `base` para el DIRECTORIO del proyecto, y al acabar el
    # ultimo bucle `base` valia una ruta. Con `base = leer_linea_base(...)` la
    # linea base entera se perdia sin que nada fallara: `set(ruta)` es un set de
    # CARACTERES, la comparacion de nombres no encontraba nunca nada y el
    # trinquete pasaba siempre. Un nombre que dos cosas distintas comparten es un
    # bug esperando a que las dos cosas coincidan en la misma linea.
    #
    # Va ANTES de cualquier impresion porque decide que es un hallazgo y que es
    # una foto conocida, y los dos bloques de abajo (el corto de --check y el
    # informe largo) tienen que distinguir lo mismo que decide esto.
    #
    # Las huerfanas que ya estaban en la linea base no son un hallazgo nuevo, y
    # las entradas de la linea base que ya NO son huerfanas SI lo son: son deuda
    # cobrada que nadie ha podado, y sin esa regla la linea base se vuelve un
    # escudo que solo crece. Con las dos reglas el audit es puerta de verdad
    # hoy -- las 55 huerfanas conocidas en CI no rompen -- y aun asi muerde.
    nuevos_reales = [o for o in new if o[0] not in base_conocida]
    base_podada = sorted(r for r in base_conocida if r not in {o[0] for o in new})
    en_base = [o for o in new if o[0] in base_conocida]

    # `--check` NO es silencioso. Antes salia con 1 sin imprimir nada, y en el
    # log de un job eso se lee como "el script se rompio" y no como "ha
    # aparecido una fuente huerfana": el fallo que hay que arreglar no se
    # distinguia del fallo que no hay. Aqui imprime lo mismo que el informe, en
    # corto, y sale con 1.
    #
    # TODO lo que sale en modo --check va a stderr, sin excepcion. Antes el
    # parrafo de remedio iba dividido: las tres primeras lineas se imprimian sin
    # `file=sys.stderr` y solo la cuarta lo llevaba, asi que en --check, donde
    # stdout esta vacio por definicion, el log de un job mostraba "es un resto
    # DELIBERADO y no un olvido." sin sujeto, tres lineas antes de donde decia de
    # que. Un diagnostico partido en dos flujos se lee como dos diagnosticos.

    if check_only and (nuevos_reales or base_podada or listed or tests_rotos):
        if tests_rotos:
            n = len(tests_rotos)
            print('audit_unconsumed_sources: %d %s propio%s declarado%s que ya no %s:'
                  % (n,
                     'test' if n == 1 else 'tests',
                     '' if n == 1 else 's',
                     '' if n == 1 else 's',
                     'existe' if n == 1 else 'existen'),
                  file=sys.stderr)
            for rel, motivo in tests_rotos:
                print('  %s  -> %s' % (rel, motivo), file=sys.stderr)
            print('  TESTS_PROPIOS (este fichero) describe raices que ya no compila nadie: '
                  'el alcance "probado por el modulo" se ha perdido sin que se note. '
                  'O se compilan otra vez, o se borran de la lista.', file=sys.stderr)
            print('', file=sys.stderr)

        if nuevos_reales:
            n = len(nuevos_reales)
            # El sufijo dice contra QUE se ha comparado. Sin --baseline no se ha
            # comparado contra ninguna linea base, asi que decir "ni en la linea
            # base" seria hablar de un fichero que este script ni ha abierto.
            contra = ('ni en la lista blanca ni en la linea base (%s)'
                      % os.path.basename(ruta_base)) if base_conocida \
                else 'ni en la lista blanca (trinquete no activo)'
            print('audit_unconsumed_sources: %d %s SIN NINGUN PRODUCTO que %s, y que no %s '
                  '%s:' % (n,
                           'fuente huerfana' if n == 1 else 'fuentes huerfanas',
                           'la consume' if n == 1 else 'las consumen',
                           'esta' if n == 1 else 'estan',
                           contra),
                  file=sys.stderr)
            for rel, inert in sorted(nuevos_reales):
                print('  %s%s' % (rel, '  [INERTE: no lo compila nadie, ni el propio modulo]'
                                  if inert else ''), file=sys.stderr)
            # Una linea por idea: el texto se parte por la MISMA razon que antes se
            # partia entre stdout y stderr (un diagnostico largo repartido en
            # trozos se lee como varios diagnosticos), pero aqui cada trozo lleva
            # explicitamente su `file=sys.stderr`, que es lo que faltaba.
            print('  Trabajo a medias: el fichero esta, se documenta y se prueba, y ningun',
                  file=sys.stderr)
            print('  producto lo enlaza. O se le anade un consumidor, o se documenta el motivo',
                  file=sys.stderr)
            print('  en ALLOWLIST (tools/audit_unconsumed_sources.py), que es lo que dice que',
                  file=sys.stderr)
            print('  es un resto DELIBERADO y no un olvido.', file=sys.stderr)

        if listed:
            n = len(listed)
            print('audit_unconsumed_sources: %d %s de la lista blanca ya NO %s huerfana%s '
                  '(borrar la entrada%s):'
                  % (n,
                     'entrada' if n == 1 else 'entradas',
                     'es' if n == 1 else 'son',
                     '' if n == 1 else 's',
                     '' if n == 1 else 's'),
                  file=sys.stderr)
            for r in listed:
                print('  %s' % r, file=sys.stderr)

        print('  El informe completo, con el porque de cada huerfana conocida: '
              'python tools/audit_unconsumed_sources.py', file=sys.stderr)
        print('', file=sys.stderr)
    elif check_only:
        # Todo lo de mas arriba esta en verde y aun asi esto imprime. El motivo
        # es que un log de job que sale vacio no se distingue de un script que
        # no llego a mirar nada, y este script acaba de mirar 130 ficheros y 5
        # repos hermanos: callarse seria mentir.
        if base_conocida:
            print('audit_unconsumed_sources: sin hallazgos nuevos (%d huerfanas cubiertas por la '
                  'linea base %s).' % (len(en_base), os.path.basename(ruta_base)),
                  file=sys.stderr)
        else:
            print('audit_unconsumed_sources: sin hallazgos (%d huerfanas en lista blanca, '
                  '0 nuevas). Trinquete NO activo: no se ha pasado --baseline.'
                  % len(known), file=sys.stderr)
        print('', file=sys.stderr)

    if not check_only:
        total = len(shared)
        print('  Hermanos medidos: %s' % ', '.join(hermanos))
        print('=' * 96)
        print('FUENTES DE ABDSharedCode SIN NINGUN PROYECTO CONSUMIDOR')
        print('=' * 96)
        print('\n  %d fuentes en el modulo, %d alcanzables desde un producto, %d sin consumidor,'
              '\n  %d huerfanas, %d solo usadas por los tests del propio modulo.'
              % (total, len(shared_set & reachable), len(shared_set) - len(shared_set & reachable),
                 len(orphans), len(solo_probadas)))
        print('\n  HUERFANAS CONOCIDAS (lista blanca):\n')
        for rel, inert in sorted(known):
            mark = '  [INERTE: no lo compila nadie, ni el propio modulo]' if inert else ''
            print('      %-44s%s' % (rel, mark))
            motivo = ' '.join(ALLOWLIST[rel].split())
            print('          %s' % (motivo if len(motivo) <= 100 else motivo[:97] + '...'))
        if listed:
            print('\n  LISTA BLANCA DESACTUALIZADA (ya no son huerfanas, borrar la entrada):')
            for r in listed:
                print('      %s' % r)
        if nuevos_reales:
            print('\n  *** HUERFANAS NUEVAS (%s) ***\n'
                  % ('ni en la lista blanca ni en la linea base' if base_conocida
                     else 'ni en la lista blanca; trinquete no activo'))
            for rel, inert in sorted(nuevos_reales):
                print('      %-44s%s' % (rel, '  [INERTE]' if inert else ''))
        elif en_base:
            print('\n  CUBIERTAS POR LA LINEA BASE (%s): %d huerfanas conocidas, aceptadas a\n'
                  '  proposito porque sus consumidores estan escritos pero sin commitear en los\n'
                  '  repos hermanos. En cuanto se commiteen, sube los SHA, quita su entrada con\n'
                  '  --write-baseline y este script vuelve a exigirla.\n'
                  % (os.path.basename(ruta_base), len(en_base)))

        if solo_probadas:
            print('\n  SIN PRODUCTO QUE LAS USE, PERO SI LAS USA EL TEST DEL PROPIO MODULO\n')
            print('  No son huerfanas: se compilan y se ejecutan en cada ctest del modulo.')
            print('  Se listan aparte porque "nadie lo enlaza" y "el modulo lo prueba" son\n'
                  '  dos hechos distintos, y confundirlos es lo que hacia que las cinco\n'
                  '  cabeceras SynthCore/S950*.h salieran como huerfanas nuevas.\n')
            for rel, inert in sorted(solo_probadas):
                print('      %-44s%s' % (rel, '  [INERTE]' if inert else ''))
                # Si tiene entrada en la lista blanca, su motivo sigue valiendo y
                # no debe perderse por haber cambiado de seccion: "nadie lo
                # enlaza" sigue siendo cierto, y quien lo lea necesita saber que
                # es una decision y no un olvido.
                if rel in ALLOWLIST:
                    print('          %s' % ' '.join(ALLOWLIST[rel].split()))
            print('\n  Raices: %s' % ', '.join(sorted(os.path.relpath(p, SHARED).replace('\\', '/')
                                                    for p in tests_raices)))
            if tests_rotos:
                print('  DECLARADAS PERO NO COMPILADAS (cuenta como error):')
                for rel, motivo in tests_rotos:
                    print('      %-44s%s' % (rel, motivo))

    if escribir:
        nota = ('Huerfanas conocidas en la foto de la suite fijada por los SHA del '
                'workflow .github/workflows/shared-code-ci.yml, y medidas contra los '
                'SHA de la clave "foto" de este mismo fichero. Se aceptan a proposito: '
                'sus consumidores estan escritos pero sin commitear en los repos '
                'hermanos. Cuando se commiteen, sube los SHA del workflow, borra su '
                'entrada con --write-baseline y este script vuelve a exigirla. Si '
                'subes un SHA sin regenerar esto, tools/verificar_foto.py lo dice.')
        escribir_linea_base(ruta_base, [o[0] for o in new], nota, foto_de_la_suite(hermanos))
        print('audit_unconsumed_sources: linea base escrita con %d fuentes en %s'
              % (len(new), ruta_base), file=sys.stderr)
        # Escribir la linea base ES aceptar la foto de huerfanas de este momento,
        # asi que el trinquete no puede quejarse de lo que acaba de escribir. Lo
        # que si es un problema y no se arregla escribiendo son `listed`
        # (lista blanca obsoleta) y `tests_rotos` (raices que ya no compila nadie).
        return 1 if (listed or tests_rotos) else 0

    # `nuevos_reales` NO se reimprime aqui: en --check los lista el bloque corto
    # de arriba, y en el informe largo los lista la seccion "HUERFANAS NUEVAS" de
    # stdout. Imprimirlos una tercera vez no anade informacion, solo hace que el
    # mismo hallazgo aparezca dos veces en el log de un job y obligue a contar
    # para saber si son 1 o 2. Lo que solo se dice aqui es `base_podada`, que
    # ningun otro bloque dice.
    if base_podada:
        n = len(base_podada)
        print('audit_unconsumed_sources: %d %s de la linea base ya NO %s huerfana%s. '
              'La linea base se poda sola; dejarla sin podar la convierte en un escudo '
              'que ya no protege de nada:'
              % (n,
                 'entrada' if n == 1 else 'entradas',
                 'es' if n == 1 else 'son',
                 '' if n == 1 else 's'),
              file=sys.stderr)
        for r in base_podada:
            print('  %s' % r, file=sys.stderr)
        print('  Arreglo: python tools/audit_unconsumed_sources.py --write-baseline',
              file=sys.stderr)
        print('', file=sys.stderr)

    if not check_only and base_conocida:
        print('\n  TRINQUETE: %d huerfanas en la linea base, %d nuevas por encima de ella.'
              % (len(base_conocida), len(nuevos_reales)))
    elif not check_only:
        print('\n  TRINQUETE: NO ACTIVO (no se ha pasado --baseline). %d huerfanas sin '
              'consumidor, %d en lista blanca, %d nuevas.'
              % (len(orphans), len(known), len(new)))

    return 1 if (nuevos_reales or base_podada or listed or tests_rotos) else 0



def _parsear (argv):
    """
    Separador de argumentos mas pequeno que argparse, porque el resto de
    herramientas de este repo no dependen de nada y este tampoco va a empezar.
    Devuelve (diccionario opcion->[valor, ...], argumentos sueltos).
    """
    acc = collections.OrderedDict()
    sueltos = []
    i = 0

    while i < len(argv):
        a = argv[i]

        if a.startswith('--') and '=' not in a and i + 1 < len(argv) and not argv[i + 1].startswith('--'):
            acc.setdefault(a, []).append(argv[i + 1])
            i += 2
            continue

        acc.setdefault(a, []).append(True)
        i += 1

    for a in acc:
        if a not in ('--check', '--write-baseline', '--help', '-h'):
            sueltos.append(a)

    return acc, sueltos





if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:                                    # noqa: BLE001
        print('error: %s' % exc, file=sys.stderr)
        sys.exit(2)
