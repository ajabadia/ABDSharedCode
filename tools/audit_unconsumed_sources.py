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
  1  hay huerfanos NUEVOS  -> para el CI
  2  error de uso

Uso:
  python tools/audit_unconsumed_sources.py           # informe
  python tools/audit_unconsumed_sources.py --check   # solo codigo de salida
"""

import io
import os
import re
import sys
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = os.path.abspath(os.path.join(HERE, '..'))
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
    'DspCore/DspCoreTests.cpp':
        'el test standalone del propio modulo; lo compila su CMake, ningun producto',
    'DspEffects/DspEffectsTests.cpp':
        'el test standalone del propio modulo; lo compila su CMake, ningun producto',
    'SynthCore/SynthCoreTests.cpp':
        'el test standalone del propio modulo; lo compila su CMake, ningun producto',
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


def project_of(path):
    rel = os.path.relpath(path, SUITE).split(os.sep)
    return rel[0] if len(rel) > 1 else '(raiz)'


def main():
    check_only = '--check' in sys.argv

    shared = []
    for p in walk_sources(SHARED):
        shared.append((p, os.path.relpath(p, SHARED).replace('\\', '/')))
    shared_set = set(p for p, _ in shared)

    # --- 1. que .cpp del modulo compila cada proyecto ------------------------
    compiled_by = collections.defaultdict(set)   # ruta -> {proyecto}
    for proj in sorted(os.listdir(SUITE)):
        base = os.path.join(SUITE, proj)
        if not os.path.isdir(base) or proj in SKIP:
            continue
        for dp, dn, fn in os.walk(base):
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
    self_built = {p for p, who in compiled_by.items() if who == {'ABDSharedCode'}}

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

    for proj in sorted(os.listdir(SUITE)):
        base = os.path.join(SUITE, proj)
        if not os.path.isdir(base) or proj in SKIP or proj == 'ABDSharedCode':
            continue
        for dp, dn, fn in os.walk(base):
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
    for proj in sorted(os.listdir(SUITE)):
        base = os.path.join(SUITE, proj)
        if not os.path.isdir(base) or proj in SKIP:
            continue
        proj_bases = [base, norm(os.path.join(base, 'src')), norm(os.path.join(base, 'Source')),
                      norm(os.path.join(base, 'wasm')), SHARED, SUITE]
        for p in walk_sources(base):
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

    # --- 4. veredicto --------------------------------------------------------
    orphans = []
    for p, rel in sorted(shared, key=lambda t: t[1]):
        if p in reachable:
            continue
        # el .cpp se marca como "inerte" si ademas nadie lo compila
        inert = rel.endswith(('.cpp', '.c')) and p not in compiled_by and p not in self_built
        orphans.append((rel, inert))

    known = [o for o in orphans if o[0] in ALLOWLIST]
    new = [o for o in orphans if o[0] not in ALLOWLIST]
    seen = dict(orphans)
    listed = [r for r in ALLOWLIST if r not in seen]

    if not check_only:
        total = len(shared)
        print('=' * 96)
        print('FUENTES DE ABDSharedCode SIN NINGUN PROYECTO CONSUMIDOR')
        print('=' * 96)
        print('\n  %d fuentes en el modulo, %d alcanzables desde un producto, %d huerfanas.'
              % (total, len(shared_set & reachable), len(orphans)))
        print('\n  HUERFANAS CONOCIDAS (lista blanca):\n')
        for rel, inert in sorted(known):
            mark = '  [INERTE: no lo compila nadie, ni el propio modulo]' if inert else ''
            print('      %-44s%s' % (rel, mark))
            print('          %s' % ALLOWLIST[rel].replace('\n', ' ')[:100])
        if listed:
            print('\n  LISTA BLANCA DESACTUALIZADA (ya no son huerfanas, borrar la entrada):')
            for r in listed:
                print('      %s' % r)
        if new:
            print('\n  *** HUERFANAS NUEVAS (no estaban en la lista blanca) ***\n')
            for rel, inert in sorted(new):
                print('      %-44s%s' % (rel, '  [INERTE]' if inert else ''))

    return 1 if (new or listed) else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:                                    # noqa: BLE001
        print('error: %s' % exc, file=sys.stderr)
        sys.exit(2)
