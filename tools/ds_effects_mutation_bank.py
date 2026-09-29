#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Banco de mutaciones de DspEffects: el banco ROJO se pone de verdad?

QUE ES. Siete mutaciones que deshacen una a una los arreglos que la auditoria
del modulo aplico, y que comprueban que el banco de pruebas se pone rojo con
cada una. No prueba que el codigo sea correcto: prueba que el BANCO MIERDA, que
es otra cosa distinta y que no se puede dar por buena de palabra.

POR QUE ESTA EN tools/ Y NO EN UN SCRIPT SUELTO. Porque "7 de 7" es una
afirmacion que envejece. Los arreglos se pueden deshacer al tocar el fichero, o
el banco puede crecer y las comprobaciones pueden moverse a otro sitio, y el
numero sigue escrito en el INTEGRATION_GUIDE como si nada. Con esto, el 7 de 7
se vuelve a comprobar cuando quieras, y sale en rojo si deja de ser cierto.

QUE MIDE Y QUE NO. Mide SENSIBILIDAD del banco: que cada arreglo tenga al menos
una comprobacion que se rompa al deshacerlo. No mide correccion del arreglo: un
banco que se pone rojo porque da error de compilacion no vale, y por eso la
mutacion que no compila se cuenta como fallo del banco, no como mutacion
detectada.

LAS TRAMPAS, QUE ESTAN TODAS EN EL CODIGO PORQUE ALGUIEN LAS PISO:

  1. UNA MUTACION QUE NO MUTA NADA NO ES UNA PRUEBA QUE PASA. La del vtable
     buscaba con `\\n` a pelo en un fichero que esta en CRLF, no cambiaba ni un
     byte, y salia "[OK]": verde por no haber ejecutado nada. Aqui se compara el
     checksum antes y despues de mutar, y si el fichero no ha cambiado se dice
     que la mutacion esta ROTA y el script termina en rojo. Fijate en que el
     verde sale de "el banco se puso rojo" y el rojo sale de "no mutamos nada" o
     de "el banco ni se inmuto": son cosas distintas y se distinguen.

  2. HAY QUE DEVOLVER EL FICHERIO. Todo va con `try/finally` y copia de
     seguridad, y alfinal se comprueba que el checksum vuelve al de antes. Un
     script que muta en el sitio y se queda a medias deja el arbol con un
     defecto de verdad, que es peor que no tener la herramienta.

  3. LOS FINALES DE LINEA. Se normaliza a `\\n` para buscar, se devuelve el
     fichero con la convencion que tenia, y el checksum del punto 1 avisa si
     algo se ha colado.

  4. EL BANCO TARDA. unos 70 s por ejecucion y unos 80 s de compilacion, asi que
     las siete por separado son unos quince minutos. Por eso hay `--only`, que
     es como se hizo en dos tandas la primera vez.

 USO

    python tools/ds_effects_mutation_bank.py --list
    python tools/ds_effects_mutation_bank.py                  # las siete
    python tools/ds_effects_mutation_bank.py --only 1,2,7     # una tanda
    python tools/ds_effects_mutation_bank.py --build-dir ../ABDNeural/build-reference

 CODIGO DE SALIDA

    0  las siete mutaciones se detectaron
    1  alguna sobrevive (el banco se quedo verde deshaciendo un arreglo), o una
       mutacion no se pudo aplicar, o el banco no estaba verde de partida
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TARGET = 'ABDShared_DspEffects_Tests'

# build por defecto: el ejecutable del modulo vive en el arbol de ABDNeural,
# que es quien lo enlaza (ABDSharedCode es header-only y no tiene target propio)
BUILD_POR_DEFECTO = os.path.join(os.path.dirname(RAIZ), 'ABDNeural', 'build-reference')


# ----------------------------------------------------------------------------
# LAS SIETE MUTACIONES
#
# Cada una es una lista de (de, a) sobre un fichero. `de` tiene que aparecer
# EXACTAMENTE una vez: si aparece mas, el script para, porque una mutacion
# ambigua es una mutacion que no sabes que estas midiendo.
#
# Los cuatro primeros son los recortes de mando; el quinto es el extremo de la
# amortiguacion; el sexto es el umbral del diodo; el septimo y el primero son la
# etapa de caracter. El `decay` va dos veces a proposito: una quita el recorte
# (el defecto) y la otra pone el recorte que PARECIA el bueno. Esa segunda es la
# que mas ha merecido la pena, porque `jlimit (0, 1)` sobre el `decay` convierte
# el valor negativo en 0, el 0 no es el otro caso del ternario que hay dentro,
# y la realimentacion se cae de 0.3 a 0: desaparece la cola de una variante que
# ABDEep consume con sus presets de fabrica. Un arreglo razonable que rompe la
# paridad es justo el fallo que un banco de mutaciones esta para cazar.
# ----------------------------------------------------------------------------

MUTACIONES = [
    {
        'id': 1,
        'fichero': 'DspEffects/EffectPolicy.h',
        'titulo': 'La etapa de caracter vuelve a llevar vtable',
        'por_que': 'Se le devuelve el ancla virtual `prepareRateImpl` que la '
                   'auditoria quito porque nadie la sobrescribia. Ocho bytes y '
                   'una tabla de funciones por etapa a cambio de nada.',
        'cambios': [
            (
                "    void prepareSampleRate (double sampleRate) noexcept\n"
                "    {\n"
                "        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;\n"
                "    }",

                "    void prepareSampleRate (double sampleRate) noexcept\n"
                "    {\n"
                "        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;\n"
                "        prepareRateImpl (sampleRate_);\n"
                "    }"
            ),
            (
                "private:\n"
                "    double sampleRate_ = 44100.0;",

                "protected:\n"
                "    virtual void prepareRateImpl (double) noexcept {}\n"
                "\n"
                "private:\n"
                "    double sampleRate_ = 44100.0;"
            ),
        ],
    },
    {
        'id': 2,
        'fichero': 'DspEffects/DspSchroederReverb.h',
        'titulo': 'El decay se queda sin recorte',
        'por_que': 'Con `decay` por encima de 1.111 la realimentacion pasa de '
                   '1 y la cola crece sin limite. Es el defecto original.',
        'cambios': [
            (
                "        decay_ = jmin (decay, 1.0f);",
                "        decay_ = decay;"
            ),
        ],
    },
    {
        'id': 3,
        'fichero': 'DspEffects/DspSchroederReverb.h',
        'titulo': 'El decay se recorta por los dos extremos (el arreglo que parecia bueno)',
        'por_que': '`jlimit (0, 1)` en vez del `jmin (..., 1.0f)`. Con knobs en '
                   'rango no cambia ni un bit: por eso cuela. Pero el negativo '
                   'deja de ser la variante con significado propio, la '
                   'realimentacion se cae de 0.3 a 0 y la cola desaparece.',
        'cambios': [
            (
                "        decay_ = jmin (decay, 1.0f);",
                "        decay_ = jlimit (0.0f, 1.0f, decay);"
            ),
        ],
    },
    {
        'id': 4,
        'fichero': 'DspEffects/DspSchroederReverb.h',
        'titulo': 'La amortiguacion se queda sin recorte',
        'por_que': 'Con `damping = 2.0` el pasabajos `damp1 = 2, damp2 = -1` '
                   'tiene el polo en -1 y crece: pico de 1.0e+30 en un segundo.',
        'cambios': [
            (
                "        damping_ = jlimit (0.0f, 1.0f, damping);",
                "        damping_ = damping;"
            ),
        ],
    },
    {
        'id': 5,
        'fichero': 'DspEffects/DspSchroederReverb.h',
        'titulo': 'La difusion se queda sin recorte',
        'por_que': 'La misma clase de fallo que la amortiguacion, en el otro '
                   'extremo: con `diffusion = 2.0` la cola llega a 3.0e+28.',
        'cambios': [
            (
                "        diffusion_ = jlimit (0.0f, 1.0f, diffusion);",
                "        diffusion_ = diffusion;"
            ),
        ],
    },
    {
        'id': 6,
        'fichero': 'DspEffects/DspSchroederReverb.h',
        'titulo': 'La amortiguacion a cero vuelve a congelar el estado',
        'por_que': 'Con `damp1 = damping, damp2 = 1 - damping` y `damping` a 0 '
                   'exacto el estado es `estado = estado`: la cola no decae y se '
                   'queda en un offset de DC. Ojo al valor 0 EXACTO, que no es '
                   'NaN ni negativo y se colaba por el hueco del otro extremo.',
        'cambios': [
            (
                "        const float damp1 = damping_ > 0.0f ? damping_ : 1.0f;\n"
                "        const float damp2 = damping_ > 0.0f ? 1.0f - damping_ : 0.0f;",

                "        const float damp1 = damping_;\n"
                "        const float damp2 = 1.0f - damping_;"
            ),
        ],
    },
    {
        'id': 7,
        'fichero': 'DspEffects/characters/DiodeBridge.h',
        'titulo': 'El umbral del puente de diodos se sale de rango',
        'por_que': 'El `jlimit` que ya estaba en las otras dos lecturas del '
                   'mismo fichero, al hueco que se le escapaba: la del propio '
                   '`threshold`. Con 1.5 el puente se volvia transparente y '
                   'AMPLIFICABA el tramo.',
        'cambios': [
            (
                "        const float lim    = jlimit (0.0f, 1.0f, threshold);",
                "        const float lim    = threshold;"
            ),
        ],
    },
]


# ----------------------------------------------------------------------------
# Utilidades de fichero. Todo pasa por aqui, y todo comprueba.
# ----------------------------------------------------------------------------

def checksum (ruta):
    with open(ruta, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def leer (ruta):
    """Devuelve (texto normalizado a \\n, es_crlf)."""
    with open(ruta, 'rb') as f:
        crudo = f.read()

    es_crlf = b'\r\n' in crudo
    texto = crudo.decode('utf-8').replace('\r\n', '\n')
    return texto, es_crlf


def escribir (ruta, texto, es_crlf):
    if es_crlf:
        texto = texto.replace('\n', '\r\n')
    with open(ruta, 'wb') as f:
        f.write(texto.encode('utf-8'))


def aplicar (ruta, cambios):
    """Aplica los cambios. Devuelve un texto de error, o None si todo bien."""
    texto, es_crlf = leer (ruta)
    antes = checksum (ruta)

    for de, a in cambios:
        cuantos = texto.count (de)
        if cuantos != 1:
            return ('ancla ambigua o ausente: aparece %d veces (se pedia 1)\n'
                    '    %s' % (cuantos, de.strip().splitlines()[0] if de.strip() else '(vacio)'))

        texto = texto.replace (de, a, 1)

    escribir (ruta, texto, es_crlf)

    # LA TRAMPA 1. Sin esto, una mutacion que no ha tocado nada sale verde.
    if checksum (ruta) == antes:
        return 'el fichero NO ha cambiado: la mutacion no se ha aplicado'

    return None


# ----------------------------------------------------------------------------
# Compilar y correr.
# ----------------------------------------------------------------------------

def ruta_exe (build_dir):
    return os.path.join (build_dir, 'ABDSharedCode', 'Debug', TARGET + '.exe')


def compilar (build_dir):
    proc = subprocess.run (
        ['cmake', '--build', build_dir, '--config', 'Debug', '--target', TARGET],
        capture_output=True, text=True
    )
    return proc.returncode, (proc.stdout or '') + (proc.stderr or '')


def correr (exe):
    proc = subprocess.run ([exe], capture_output=True, text=True)
    return proc.returncode, (proc.stdout or '') + (proc.stderr or '')


def esta_verde (rc, salida):
    return rc == 0 and '[OK]' in salida


def motivo_rojo (rc, salida):
    """Por que esta rojo, para el informe. Distingue compilar de fallar."""
    if 'error C' in salida or 'error LNK' in salida or 'fatal error' in salida:
        return 'el banco NO COMPILA con la mutacion'

    for linea in salida.splitlines():
        if linea.startswith ('[FAIL]'):
            return 'el banco se pone rojo: %s' % linea[7:].strip()[:88]

    for linea in salida.splitlines():
        if linea.startswith ('[FALLO]'):
            return linea.strip()

    if rc != 0:
        return 'el banco sale con codigo %d' % rc

    return 'el banco se quedo VERDE: nadie caza esta mutacion'


# ----------------------------------------------------------------------------

def listar ():
    print ('%d mutaciones. Cada una deshace un arreglo y espera que el banco la caze.\n'
           % len (MUTACIONES))

    for m in MUTACIONES:
        print ('  %d. %s' % (m['id'], m['titulo']))
        print ('     %s' % m['fichero'])
        for linea in m['por_que'].split ('. '):
            linea = linea.strip ()
            print ('       %s%s' % (linea, '' if linea.endswith ('.') else '.'))
        print ()


def main ():
    ap = argparse.ArgumentParser (
        description='Banco de mutaciones de DspEffects',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument ('--list', action='store_true', help='solo listar las mutaciones')
    ap.add_argument ('--only', default='',
                     help='ids separados por comas, para hacer una tanda (1,2,7)')
    ap.add_argument ('--build-dir', default=BUILD_POR_DEFECTO,
                     help='donde esta la build de ABDNeural (por defecto: %s)' % BUILD_POR_DEFECTO)
    args = ap.parse_args ()

    if args.list:
        listar ()
        return 0

    elegidas = MUTACIONES
    if args.only:
        pedidos = set ()
        for trozo in args.only.split (','):
            trozo = trozo.strip()
            if trozo.isdigit():
                pedidos.add (int (trozo))

        elegidas = [m for m in MUTACIONES if m['id'] in pedidos]
        if not elegidas:
            print ('--only no coincide con ninguna mutacion', file=sys.stderr)
            return 2

    exe = ruta_exe (args.build_dir)
    if not os.path.isfile (exe):
        print ('no encuentro el ejecutable del banco:\n  %s\n'
               'Indica la build con --build-dir.' % exe, file=sys.stderr)
        return 2

    # --- LA BASE. Sin esto "el banco se puso rojo" no quiere decir nada: igual
    # --- estaba rojo de antes, y entonces cualquier mutacion "pasa".
    print ('0. Comprobando que el banco esta VERDE antes de tocar nada...')
    rc, salida = correr (exe)
    if not esta_verde (rc, salida):
        print ('\nEl banco NO esta verde de partida, asi que este script no puede\n'
               'decir nada: si ya falla, todo mutado tambien "fallaria" y las siete\n'
               'mutaciones darian verde sin querer.\n\n%s' % motivo_rojo (rc, salida),
               file=sys.stderr)
        return 2
    print ('   [OK] base limpia\n')

    # --- Copia de seguridad de todo lo que se va a tocar, ANTES de empezar.
    respaldos = {}
    for m in elegidas:
        ruta = os.path.join (RAIZ, m['fichero'])
        if m['fichero'] not in respaldos:
            respaldos[m['fichero']] = (ruta, open (ruta, 'rb').read ())

    detectadas = 0
    rotas = []
    try:
        for m in elegidas:
            ruta = os.path.join (RAIZ, m['fichero'])
            inicio = time.time ()

            print ('%d. %s' % (m['id'], m['titulo']))

            error = aplicar (ruta, m['cambios'])
            if error is not None:
                print ('   ROTA -- %s' % error, file=sys.stderr)
                rotas.append ((m['id'], 'no se pudo aplicar'))
                _restaurar (respaldos, [m['fichero']])
                continue

            rc, salida = compilar (args.build_dir)
            if rc != 0:
                print ('   ROTA -- el banco no compila con la mutacion:\n%s'
                       % _cola (salida), file=sys.stderr)
                rotas.append ((m['id'], 'no compila'))
                _restaurar (respaldos, [m['fichero']])
                continue

            rc, salida = correr (exe)

            if esta_verde (rc, salida):
                print ('   NO DETECTADA -- el banco se quedo verde deshaciendo esto.\n'
                       '   El arreglo %d no tiene ninguna comprobacion que lo vigile.\n'
                       '   Eso NO es bueno: hay que anadirla al banco.'
                       % m['id'], file=sys.stderr)
                rotas.append ((m['id'], 'sobrevive'))
            else:
                detectadas += 1
                print ('   detectada -- %s' % motivo_rojo (rc, salida))

            _restaurar (respaldos, [m['fichero']])
            print ('   (%.0f s)' % (time.time () - inicio))

        # --- Y RECONSTRUIR. Devolver los ficheros no basta: el ejecutable se
        # --- queda compilado con la ultima mutacion puesta, y la siguiente
        # --- invocacion se encontraria un banco que NO esta verde de partida.
        # --- La guarda de arriba lo detecta y se niega a continuar, que es lo
        # --- correcto, pero es mucho mejor no dejar el banco asado.
        print ('\nReconstruyendo el banco con las fuentes devueltas...')
        rc, salida = compilar (args.build_dir)
        if rc != 0:
            print ('   ROTA -- al volver a construir sin mutaciones el banco no\n'
                   '   compila, osea que el arbol no quedo bien:\n%s' % _cola (salida),
                   file=sys.stderr)
            rotas.append ((0, 'la reconstruccion final no compila'))
        else:
            rc, salida = correr (exe)
            if not esta_verde (rc, salida):
                print ('   ROTA -- y ademas el banco devuelto no esta verde:\n'
                       '   %s' % motivo_rojo (rc, salida), file=sys.stderr)
                rotas.append ((0, 'el banco devuelto no esta verde'))
            else:
                print ('   [OK] banco devuelto y en verde')
    finally:
        # --- LA TRAMPA 2. Pase lo que pase, el arbol vuelve como estaba.
        _restaurar (respaldos, list (respaldos.keys ()))

    total = len (elegidas)
    print ('\n%s de %d mutaciones detectadas' % (detectadas, total))

    if rotas:
        print ('\nMutaciones que NO se han cazado:', file=sys.stderr)
        for ident, motivo in rotas:
            print ('  %d: %s' % (ident, motivo), file=sys.stderr)
        return 1

    return 0


def _cola (salida, lineas=12):
    return '\n'.join (salida.splitlines ()[-lineas:])


def _restaurar (respaldos, ficheros):
    for nombre in ficheros:
        ruta, contenido = respaldos[nombre]
        with open (ruta, 'wb') as f:
            f.write (contenido)

        if checksum (ruta) != hashlib.sha256 (contenido).hexdigest ():
            raise RuntimeError ('no se ha podido devolver %s a su estado' % nombre)


if __name__ == '__main__':
    sys.exit (main ())
