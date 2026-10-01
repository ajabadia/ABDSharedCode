#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
  ==============================================================================

    run_dsp_math_harness.py  (arnés de paridad y coste de DSP, NO parte del módulo)

    Qué hace, en una línea: responde a la pregunta "¿las trascendentales de
    `DspMath.h` siguen siendo las MISMAS en WASM que en nativo, y cuestan lo que
    cuestan?", y se pone en rojo si aparece una función trigonométrica nueva sin
    medir.

    LAS CUATRO PUERTAS, y qué atrapa cada una:

      A. COBERTURA.  Lee `DspMath.h`, saca las declaraciones de nivel de namespace
                     y las compara con el manifiesto del arnés. Si alguien
                     declara un `tan` o un `exp` nuevo y no lo mete en
                     `kManifiesto`, esta puerta falla. Es LA puerta que
                     responde a "que ninguna trigonometrica nueva se cuele sin
                     medir", y es la unica que no necesita ni compilador.

      B. SIMBOLOS.    Compila el arnés a objeto y mira los simbolos INDEFINIDOS
                     con `nm`. El modulo tiene que salir con cero. Esto es lo que
                     hace dangerousa una fuga: en nativo, llamar a `expf` de libm
                     COMPILA Y FUNCIONA, y nadie se entera hasta que alguien lo
                     compila a WASM y descubre que alli no hay biblioteca. La
                     puerta B avisa en nativo, donde todavia se puede arreglar.

      C. INFORME.     Corre el arnés nativo, comprueba que salen las once
                     funciones con sus 16 casos, y enseña el coste. El coste
                     AVISA, no falla: depende de la maquina.

      D. PARIDAD.     Compila a `--target=wasm32 -nostdlib`, lo corre en node y
                     compara los BITS con los del nativo, funcion a funcion y
                     caso a caso. Ademas comprueba que el modulo WASM solo
                     importe el reloj: en freestanding, una llamada a libm no
                     puede enlazarse, asi que una fuga que ya se colara en B sale
                     aqui como un import de mas.

    CÓDIGOS DE SALIDA, que no son intercambiables:

      0  todo pasa, y la pata WASM se ha ejecutado de verdad
      1  alguna puerta falla
      3  la pata WASM NO se ha podido ejecutar (falta clang o wasm-ld)

    El 3 existe para que un CI no confunda "nadie ha comprobado la paridad" con
    "la paridad esta bien". Un 0 sin haber corrido WASM seria mentira.

    Uso:
        python tools/run_dsp_math_harness.py
        python tools/run_dsp_math_harness.py --rapido     # pocas repeticiones
        python tools/run_dsp_math_harness.py --sin-wasm   # solo nativo

    Herramientas: g++ para nativo, clang++ + wasm-ld para WASM, node para
    correrlo. Se pueden Override con las variables de entorno
    DSPMATH_NATIVO, DSPMATH_WASM_CC, DSPMATH_WASM_LD y DSPMATH_NODE.

    Solo g++, no MSVC: el arnes usa `__builtin_memcpy` para volcar los bits
    de un float, y MSVC no lo tiene. Es el precio de que el MISMO fuente
    compile en nativo y a wasm32 sin <cstring>, que en freestanding no esta.

  ==============================================================================
"""

import io
import os
import re
import shutil
import subprocess
import sys
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FUENTE_MATH   = os.path.join(RAIZ, 'DspCore', 'DspMath.h')
FUENTE_ARNES  = os.path.join(RAIZ, 'DspCore', 'DspMathHarness.cpp')
MANIFIESTO    = os.path.join(RAIZ, 'DspCore', 'DspMathHarness.h')
SHIMS         = os.path.join(RAIZ, 'tools', 'wasm_freestanding')
LADO_WASM     = os.path.join(RAIZ, 'tools', 'dsp_math_wasm_parity.mjs')

# Los simbolos de libm que NADIE puede llamar desde el modulo. La lista es
# amplia a proposito: no son solo las que existen hoy en DspMath.h, son todas las
# que alguien podria usar por costumbre. `tanh` esta en la lista y no es paradoja:
# la del modulo es `abd::dsp::tanh`, y la de libm se llama `tanhf`.
SIMBOLOS_LIBM = {
    'sinf', 'cosf', 'tanf', 'asinf', 'acosf', 'atanf', 'atan2f',
    'sinhf', 'coshf', 'tanhf', 'asinhf', 'acoshf', 'atanhf',
    'expf', 'exp2f', 'expm1f', 'logf', 'log2f', 'log10f', 'log1pf',
    'powf', 'sqrtf', 'cbrtf', 'hypotf', 'hypotf', 'fmodf', 'remainderf',
    'floorf', 'ceilf', 'truncf', 'roundf', 'rintf', 'nearbyintf',
    'ldexpf', 'frexpf', 'modff', 'copysignf', 'fdimf', 'fmaxf', 'fminf',
    'fmaf', 'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'atan2',
    'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh',
    'exp', 'exp2', 'log', 'log2', 'log10', 'pow', 'sqrt', 'fmod', 'hypot',
}

# El reloj: lo unico que el modulo WASM tiene permitido importar.
IMPORTACION_PERMITIDA = 'env.dspMathHarnessHostMilis'

fallos = []
avisos = []


def fallar (puerta, mensaje):
    fallos.append((puerta, mensaje))
    print('  [FALLO] %s: %s' % (puerta, mensaje))


def avisar (mensaje):
    avisos.append(mensaje)
    print('  [aviso] %s' % mensaje)


def seccion (titulo):
    print('')
    print('=== %s' % titulo)


def leer (ruta):
    with io.open(ruta, encoding='utf-8') as f:
        return f.read()


# ==============================================================================
# PUERTA A: cobertura. Toda trascendental de DspMath.h esta en el manifiesto.
# ==============================================================================

def puerta_cobertura ():
    seccion ('A. COBERTURA: toda funcion de DspMath.h esta en el manifiesto?')

    fuente = leer (FUENTE_MATH)

    # El bloque `namespace dspmath_detail` NO es API: son `sinPoly`, `cosPoly`,
    # `reduce` y compañía, que son Auxiliares. Contarlas haria que el manifiesto
    # tuviera que anotar los, que es justo lo contrario de lo que se quiere. Se salta
    # desde la apertura hasta la llave que la cierra, que es la primera linea
    # que es solo una llave.
    lineas = fuente.split('\n')
    corte = 0
    for i, linea in enumerate (lineas):
        if linea.startswith ('namespace dspmath_detail'):
            for j in range (i + 1, len (lineas)):
                if lineas[j].rstrip () == '}':
                    corte = j + 1
                    break
            break

    patron = re.compile (r'^inline\s+(?:float|double|int)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(')
    declaradas = []
    for linea in lineas[corte:]:
        m = patron.match (linea)
        if m:
            declaradas.append (m.group (1))

    # Lo declarado en el manifiesto del arnés.
    manifiesto = leer (MANIFIESTO)
    # OJO el segundo campo: vale `&envoltura` para las funciones de un
    # argumento y `nullptr` para las de dos, que llevan la adaptacion en el
    # tercer campo. Buscar solo `&envoltura` hacia que `pow` y `wrapPhase` —las
    # dos de dos argumentos— parecieran no estar en el manifiesto, que es
    # justo el falso positivo que hace que una puerta que miente se cambie
    # por otra que miente.
    enManifiesto = re.findall (r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*(?:&envoltura|nullptr)', manifiesto)

    print('  DspMath.h declara   : %s' % ', '.join (declaradas))
    print('  el manifiesto tiene: %s' % ', '.join (enManifiesto))

    if not declaradas:
        fallar ('A', 'no he encontrado ninguna declaracion en DspMath.h: el patron ha cambiado '
                      'y esta puerta dejaria pasar cualquier cosa nueva sin avisar')
        return

    sinCubrir = [n for n in declaradas if n not in enManifiesto]
    if sinCubrir:
        fallar ('A', 'funciones de DspMath.h que NO estan en el manifiesto del arnes: %s. '
                      'O estan sin medir, o el manifiesto se ha quedado corto'
                 % ', '.join (sinCubrir))

    sobrantes = [n for n in enManifiesto if n not in declaradas]
    if sobrantes:
        fallar ('A', 'el manifiesto nombra funciones que DspMath.h ya no declara: %s'
                 % ', '.join (sobrantes))

    if not sinCubrir and not sobrantes and declaradas:
        print('  [ok] las %d funciones estan todas, en los dos sentidos' % len (declaradas))


# ==============================================================================
# PUERTA B: el objeto no puede tener un solo simbolo de libm sin definir.
# ==============================================================================

def puerta_simbolos (objeto):
    seccion ('B. SIMBOLOS: el objeto no tira de libm')

    nm = shutil.which ('nm')
    if nm is None:
        avisar ('no hay `nm` en el PATH: la puerta de simbolos NO se ha ejecutado. '
                'En Windows suele venir con mingw, en el mismo directorio que g++.')
        return

    salida = subprocess.run ([nm, '-u', objeto], capture_output=True, text=True)
    indefinidos = set()
    for linea in salida.stdout.split ('\n'):
        partes = linea.split()
        if partes:
            indefinidos.add (partes[-1].lstrip ('_'))

    fugas = indefinidos & SIMBOLOS_LIBM
    if fugas:
        fallar ('B', 'el modulo TIRA DE LIBM: %s. En nativo compila y funciona, y en '
                      'WASM no hay biblioteca, asi que esto rompe solo en el navegador'
                 % ', '.join (sorted (fugas)))
    else:
        print('  [ok] sin simbolos de libm: %d simbolos indefinidos, ninguno trigonometrico'
              % len (indefinidos))

    otros = indefinidos - SIMBOLOS_LIBM
    if otros:
        print('  (otros simbolos indefinidos, por si interesa: %s)'
              % ', '.join (sorted (otros)[:12]))


# ==============================================================================
# Parseo del informe
# ==============================================================================

def parsear_informe (texto):
    """Devuelve (lineas, pie). Cada linea es (nombre, [bits], [campos])."""
    filas = []
    pie = None
    for linea in texto.split ('\n'):
        linea = linea.strip ()
        if not linea:
            continue
        if linea.startswith ('#'):
            pie = linea
            continue
        if linea.startswith ('DspMathHarness'):
            continue
        partes = linea.split ()
        nombre = partes[0]
        bits = [p for p in partes[1:] if re.fullmatch (r'[0-9a-f]{8}', p)]
        campos = [p for p in partes[1:] if p not in bits]
        filas.append ((nombre, bits, campos))
    return filas, pie


# ==============================================================================
# Lector de la seccion de importacion de un .wasm
# ==============================================================================

def imports_que_pide (ruta_wasm):
    """Nombres `modulo.funcion` que el .wasm pide a su entorno.

    Se lee el binario a mano en vez de tirar de una herramienta: son treinta
    lineas y asi esta puerta no depende de que haya un `wasm-objdump` en la
    maquina, que es justo el caso de un contenedor de build."""
    with open (ruta_wasm, 'rb') as f:
        d = f.read ()

    if d[:4] != b'\x00asm':
        fallar ('D', 'el fichero no empieza por el magic de WebAssembly')
        return ['<no es un .wasm>']

    def uleb (p):
        r = 0
        s = 0
        while True:
            b = d[p]
            p += 1
            r |= (b & 0x7f) << s
            if not (b & 0x80):
                return r, p
            s += 7

    nombres = []
    pos = 8
    while pos < len (d):
        sid = d[pos]
        pos += 1
        tam, pos = uleb (pos)
        if sid == 2:                                   # seccion de importacion
            n, p = uleb (pos)
            for _ in range (n):
                ln, p = uleb (p)
                modulo = d[p:p + ln].decode ('utf-8')
                p += ln
                ln, p = uleb (p)
                funcion = d[p:p + ln].decode ('utf-8')
                p += ln
                tipo = d[p]
                p += 1
                if tipo == 0:                          # funcion
                    _, p = uleb (p)
                elif tipo == 1:                        # tabla
                    _, p = uleb (p)
                    _, p = uleb (p)
                elif tipo == 2:                        # memoria
                    flags = d[p]
                    p += 1
                    _, p = uleb (p)
                    if flags & 1:
                        _, p = uleb (p)
                elif tipo == 3:                        # global
                    _, p = uleb (p)
                    p += 1
                nombres.append ('%s.%s' % (modulo, funcion))
        pos += tam

    return [n for n in nombres if n != IMPORTACION_PERMITIDA]


# ==============================================================================
# PUERTA D: paridad bit a bit entre nativo y WASM.
# ==============================================================================

def puerta_paridad (informe_nativo):
    seccion ('D. PARIDAD: los bits de WASM son los mismos que los de nativo?')

    cc = os.environ.get ('DSPMATH_WASM_CC') or shutil.which ('clang++')
    ld = os.environ.get ('DSPMATH_WASM_LD') or shutil.which ('wasm-ld')
    node = os.environ.get ('DSPMATH_NODE') or shutil.which ('node')

    if not cc or not ld or not node:
        falta = [n for n, v in (('clang++', cc), ('wasm-ld', ld), ('node', node)) if not v]
        print('  [SIN EJECUTAR] falta %s. La paridad WASM NO se ha comprobado.' % ', '.join (falta))
        return 3

    tmp = tempfile.mkdtemp (prefix='dspmath')
    obj = os.path.join (tmp, 'harness.o')
    wasm = os.path.join (tmp, 'harness.wasm')

    def correr (cmd, etiqueta):
        r = subprocess.run (cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print('  [FALLO] %s fallo:' % etiqueta)
            for linea in (r.stderr or '').split ('\n')[:12]:
                if linea.strip ():
                    print('      ' + linea)
            return False
        return True

    # Pocas repeticiones en WASM: el coste no se usa de puerta y 1.000.000 de
    # vueltas por funcion en un motor de node son segundos que no aportan nada.
    if not correr ([cc, '--target=wasm32', '-std=c++17', '-O2', '-nostdlib', '-ffreestanding',
                   '-fno-exceptions', '-fno-rtti', '-DDspMathHarnessWasm',
                   '-DDspMathHarnessRepeticiones=20000',
                   '-I', RAIZ, '-I', SHIMS, '-c', FUENTE_ARNES, '-o', obj],
                  'la compilacion a wasm32'):
        return 1

    if not correr ([ld, '--no-entry', '--allow-undefined',
                    '--export=dspMathHarnessReport', '--export-memory',
                    '-o', wasm, obj], 'el enlazado a wasm'):
        return 1

    # Los simbolos que el modulo pide al mundo se comprueban AQUI, leyendo el
    # .wasm, y no es redundante con la puerta B: en freestanding `expf` no
    # existe, asi que una fuga o aparece como un import de mas o no enlaza. Y
    # esta comprobacion no depende de que haya `nm` en la maquina, que es lo que
    # la vuelve util en un contenedor de build.
    fugas = imports_que_pide (wasm)
    if fugas:
        fallar ('D', 'el modulo WASM pide al host %s. Lo unico que puede pedir es el reloj (%s): '
                      'en freestanding no hay biblioteca de la que salirse, asi que cualquier '
                      'otra peticion es una fuga'
                 % (', '.join (fugas), IMPORTACION_PERMITIDA))
    else:
        print('  [ok] el modulo WASM solo importa el reloj: %s' % IMPORTACION_PERMITIDA)

    r = subprocess.run ([node, LADO_WASM, wasm], capture_output=True, text=True)
    if r.returncode != 0:
        print('  [FALLO] node no pudo correr el modulo:')
        for linea in (r.stderr or '').split ('\n')[:12]:
            if linea.strip ():
                print('      ' + linea)
        return 1

    filas_wasm, pie_wasm = parsear_informe (r.stdout)
    filas_nat, _pie_nat = parsear_informe (informe_nativo)

    if len (filas_nat) != len (filas_wasm):
        fallar ('D', 'nativo reporta %d funciones y WASM %d' % (len (filas_nat), len (filas_wasm)))
        return 1

    for (nombre, bits_nat, campos_nat), (nombre_w, bits_w, campos_w) in zip (filas_nat, filas_wasm):
        if nombre != nombre_w:
            fallar ('D', 'el orden de las funciones no es el mismo: %s vs %s' % (nombre, nombre_w))
            continue
        if bits_nat == bits_w:
            print('  [ok] %-12s %d bits identicos' % (nombre, len (bits_nat)))
            continue

        # Difieren: WHICH case, que es la informacion que hace falta para mirar.
        if len (bits_nat) != len (bits_w):
            fallar ('D', '%s: distinto numero de casos (%d nativo, %d wasm)'
                    % (nombre, len (bits_nat), len (bits_w)))
            continue
        malos = [(i, a, b) for i, (a, b) in enumerate (zip (bits_nat, bits_w)) if a != b]
        detalle = ', '.join ('caso %d: %s -> %s' % (i, a, b) for i, a, b in malos[:4])
        fallar ('D', '%s: %d de %d casos difieren. %s'
                % (nombre, len (malos), len (bits_nat), detalle))

    if pie_wasm:
        print('  %s' % pie_wasm)
    return 0


# ==============================================================================
def main ():
    rapido = '--rapido' in sys.argv
    sin_wasm = '--sin-wasm' in sys.argv

    print ('Arnés de paridad y coste de DSP: DspMath.h nativo frente a WASM')
    print ('raiz: %s' % RAIZ)

    puerta_cobertura ()

    tmp = tempfile.mkdtemp (prefix='dspmath')
    exe = os.path.join (tmp, 'harness.exe')
    obj = os.path.join (tmp, 'harness.o')

    cc_nativo = os.environ.get ('DSPMATH_NATIVO') or shutil.which ('g++') or shutil.which ('clang++')
    if not cc_nativo:
        print('')
        print('  [FALLO] no hay compilador de C++ (ni g++ ni clang++ ni DSPMATH_NATIVO).')
        return 1

    reps = '20000' if rapido else '1000000'
    cmd = [cc_nativo, '-std=c++17', '-O2', '-ffreestanding', '-I', RAIZ,
           '-DDspMathHarnessRepeticiones=' + reps, FUENTE_ARNES, '-o', exe]
    r = subprocess.run (cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print('  [FALLO] el arnés no compila en nativo:')
        for linea in (r.stderr or '').split ('\n')[:15]:
            if linea.strip ():
                print ('      ' + linea)
        return 1

    # El mismo comando a objeto, para la puerta de simbolos.
    r = subprocess.run ([cc_nativo, '-std=c++17', '-O2', '-ffreestanding', '-I', RAIZ,
                        '-DDspMathHarnessRepeticiones=2000', '-c', FUENTE_ARNES, '-o', obj],
                       capture_output=True, text=True)
    if r.returncode != 0:
        avisar ('no se ha podido generar el objeto para la puerta de simbolos')
    else:
        puerta_simbolos (obj)

    seccion ('C. INFORME NATIVO: el coste de cada una')
    r = subprocess.run ([exe], capture_output=True, text=True)
    if r.returncode != 0:
        print ('  [FALLO] el arnés nativo no se ha podido ejecutar')
        return 1

    print (r.stdout.rstrip ())
    filas, _pie = parsear_informe (r.stdout)

    if len (filas) == 0:
        fallar ('C', 'el informe nativo no tiene ninguna linea de funcion')
    else:
        for nombre, bits, campos in filas:
            if len (bits) != 16:
                fallar ('C', '%s: %d casos, se esperaban 16' % (nombre, len (bits)))
            if campos and campos[-1] == 'SOBRE-PRESUPUESTO':
                avisar ('%s esta por encima de su presupuesto de coste (%s contra %s). '
                        'Puede ser la maquina; si se repite, es que ha cambiado algo.'
                        % (nombre, campos[0] if len (campos) > 0 else '?',
                           campos[1] if len (campos) > 1 else '?'))
        print ('  [ok] %d funciones con sus 16 casos' % len (filas))

    estado_wasm = 0
    if sin_wasm:
        print ('')
        print ('=== D. PARIDAD: omitida a proposito (--sin-wasm)')
    else:
        estado_wasm = puerta_paridad (r.stdout)

    print ('')
    print ('=== RESUMEN')
    if fallos:
        for puerta, mensaje in fallos:
            print ('  FALLO en %s: %s' % (puerta, mensaje))
    else:
        print ('  ninguna puerta en rojo')
    if avisos:
        print ('  %d aviso(s), que no hacen fallar la ejecucion' % len (avisos))

    if fallos:
        return 1
    if estado_wasm == 3:
        print ('')
        print ('  ATENCION: la paridad WASM NO se ha comprobado. Esto NO es un OK.')
        return 3
    return 0


if __name__ == '__main__':
    sys.exit (main ())
