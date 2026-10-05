#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Banco de mutaciones de DspEffects: cada arreglo sigue teniendo un test que muerda?

QUE ES. Un catalogo de ARREGLOS —los defectos que la auditoria del modulo
corrigio— y, para cada uno, una o mas mutaciones que lo deshacen y comprueban
que el banco se pone ROJO. No prueba que el codigo sea correcto: prueba que el
BANCO MIERDA, que es otra cosa y que no se puede dar por buena de palabra.

POR QUE UN CATALOGO Y NO UNA LISTA. La version anterior eran siete mutaciones
soltas sobre tres ficheros, y eso tenia un agujero: nada decia que las siete
fueran TODOS los arreglos. Se podia deshacer un octavo arreglo, anadir su
comprobacion al banco, y el "7 de 7" seguiria en el INTEGRATION_GUIDE sin que
nadie se enterase. Aqui cada mutacion dice que arreglo deshace, y el banco se
niega a correr si hay un arreglo del catalogo sin mutacion. "Cada arreglo tiene
un test que muerda" pasa de ser una frase a ser una condicion que se comprueba.

Y HAY UN SEGUNDO AGUJERO, MAS TONTO. Contaba como deteccion cualquier rojo. Un
banco que se pone rojo porque la mutacion rompio OTRA comprobacion, o porque
algo ha reventado por el camino, se contaba como "mordida" y el arreglo
parecia vigilado sin estarlo. Por eso cada mutacion declara `muerde`: el texto
de la comprobacion que TIENE que ponerse roja. Si el banco se pone rojo de
verdad pero por otro sitio, sale "ROJA PERO NO POR AQUI" y es un fallo. Eso es
lo que separa un test que muerde de un test que tropieza.

LOS TRES NIVELES, Y CADA UNO COMPRUEBA AL ANTERIOR

    --rapido          La tabla esta bien: cada anclaje aparece exactamente una
                      vez en su fichero, cada `muerde` sigue existiendo en el
                      banco de pruebas, cada arreglo tiene mutacion. Sin
                      compilar, sin tocar el arbol. Cuesta un segundo.

    --autocomprobacion  Y ADEMAS, que el que comprueba sepa comprobar. Se
                      класса con entradas falsas a proposito: un arreglo sin
                      mutacion, un `muerde` que no existe, un anclaje que no
                      esta, un id repetido, y se exige que el revisor losSenale
                      TODOS. Si el revisor se calla, esto sale en rojo: una
                      herramienta de comprobacion que no se puede comprobar no
                      comprueba nada. Tambien se prueban las clasificaciones
                      de rojo con salidas de mentira.

    (por defecto)     La corrida de verdad: base verde, mutar, compilar, correr,
                      y exigir rojo POR EL MOTIVO CORRECTO. Y al final
                      reconstruir y devolver el arbol, comprobado por checksum.

QUE MIDE Y QUE NO. Mide SENSIBILIDAD: que cada arreglo tenga al menos una
comprobacion que se rompa al deshacerlo, y que se rompa la suya. No mide
correccion del arreglo: un banco rojo porque no compila no vale, y por eso la
mutacion que no compila se cuenta como fallo del banco, no como deteccion.

LAS TRAMPAS, QUE ESTAN TODAS EN EL CODIGO PORQUE ALGUIEN LAS PISO:

  1. UNA MUTACION QUE NO MUTA NADA NO ES UNA PRUEBA QUE PASA. La del vtable
     buscaba con `\\n` a pelo en un fichero que esta en CRLF, no cambiaba ni un
     byte, y salia "[OK]": verde por no haber ejecutado nada. Se compara el
     checksum antes y despues de mutar, y si el fichero no ha cambiado se dice
     que la mutacion esta ROTA.

  2. HAY QUE DEVOLVER EL FICHERO. Todo va con `try/finally` y copia de
     seguridad, y al final se comprueba que el checksum vuelve. Un script que
     muta en el sitio y se queda a medias deja el arbol con un defecto de
     verdad, que es peor que no tener la herramienta.

  3. LOS FINALES DE LINEA. Se normaliza a `\\n` para buscar, se devuelve el
     fichero con la convencion que tenia, y el checksum avisa si algo se cuela.

  4. EL BANCO TARDA. unos 70 s por ejecucion y unos 80 s de compilacion, asi que
     las doce por separado son casi media hora. Por eso hay `--only` y
     `--arreglo`, que es como se hizo en tandas la primera vez.

 USO

    python tools/ds_effects_mutation_bank.py --rapido
    python tools/ds_effects_mutation_bank.py --autocomprobacion
    python tools/ds_effects_mutation_bank.py --list
    python tools/ds_effects_mutation_bank.py                  # las doce
    python tools/ds_effects_mutation_bank.py --only 8,9,10     # una tanda
    python tools/ds_effects_mutation_bank.py --arreglo cascada-puerta-vuelve
    python tools/ds_effects_mutation_bank.py --build-dir ../ABDNeural/build-reference

 CODIGO DE SALIDA

    0  todo lo que se ha comprobado, ha dado bien
    1  algun arreglo sobrevive, o el banco se pone rojo por otro motivo, o una
       mutacion esta rota
    2  no se puede ni empezar (no hay ejecutable, el banco no esta verde, o la
       tabla esta mal) — que se distingue del 1 a proposito: el 1 dice "el
       banco es debil", el 2 dice "no tengo derecho a opinar"
"""

import argparse
import hashlib
import os
import subprocess
import sys
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TARGET = 'ABDShared_DspEffects_Tests'

# El banco de pruebas es donde viven los textos de `muerde`. Es el segundo
# fichero del cruce, y por eso el cruce se puede romper: si alguien renombra
# una comprobacion sin tocar la tabla, `muerde` deja de existir y el nivel
# rapido lo dice antes de que nadie pague veinte minutos de compilacion.
BANCO_DE_PRUEBAS = 'DspEffects/DspEffectsTests.cpp'

# build por defecto: el ejecutable del modulo vive en el arbol de ABDNeural,
# que es quien lo enlaza (ABDSharedCode es header-only y no tiene target propio)
BUILD_POR_DEFECTO = os.path.join(os.path.dirname(RAIZ), 'ABDNeural', 'build-reference')


# ----------------------------------------------------------------------------
# LOS ARREGLOS
#
# El catalogo, no la lista. Cada clave es un arreglo que la auditoria corrigio,
# y la condicion de que este aqui es: TIENE QUE TENER AL MENOS UNA MUTACION.
# Un arreglo que se agrega al catalogo y se queda sin mutacion es exactamente
# el agujero que esta herramienta existe para tapar, asi que se niega a arrancar.
#
# `de_donde` es de quien es el arreglo, para que la lista no crezca sin context.
# ----------------------------------------------------------------------------

ARREGLOS = {
    'etapa-sin-vtable': {
        'titulo': 'La etapa de caracter no lleva vtable',
        'donde': 'DspEffects/EffectPolicy.h',
        'de_donde': 'auditoria: cinco de los nueve ruteos perdian la seca',
    },
    'schroeder-damping-cero': {
        'titulo': 'La amortiguacion a 0 no congela el estado',
        'donde': 'DspEffects/DspSchroederReverb.h',
        'de_donde': 'auditoria: la cola se queda en un offset de DC',
    },
    'diodo-umbral-recorte': {
        'titulo': 'El umbral del puente de diodos se recorta a 0..1',
        'donde': 'DspEffects/characters/DiodeBridge.h',
        'de_donde': 'auditoria: al hueco que se le escapaba del jlimit',
    },
    'repisa-techo-045fs': {
        'titulo': 'La repisa se recorta a 0,45 veces el sample rate, no a 0,2',
        'donde': 'DspEffects/ShelfFilter.h',
        'de_donde': 'migracion del ecualizador: 0,2 movia las frecuencias del MS2000 a 32 kHz',
    },
    'repisa-banda-muerta': {
        'titulo': 'La ganancia tiene una banda muerta de 0,05 dB',
        'donde': 'DspEffects/ShelfFilter.h',
        'de_donde': 'migracion del ecualizador: viene del original del MS2000',
    },
    'cascada-indice-recortado': {
        'titulo': 'Un indice fuera de rango se recorta al perfil',
        'donde': 'DspEffects/CascadeShelfEq.h',
        'de_donde': 'migracion del ecualizador: el recorte es una regla, no del MS2000',
    },
    'cascada-ganancia-independiente': {
        'titulo': 'Mover la ganancia no reescribe la frecuencia',
        'donde': 'DspEffects/CascadeShelfEq.h',
        'de_donde': 'migracion del ecualizador: son dos mandos y van por dos caminos',
    },
    'cascada-puerta-vuelve': {
        'titulo': 'La puerta de hercios continuos se puede volver a cerrar con la tabla',
        'donde': 'DspEffects/CascadeShelfEq.h',
        'de_donde': 'migracion del ecualizador: lo caza el banco, no el comentario',
    },
    'cascada-puerta-independiente': {
        'titulo': 'Cada repisa tiene su propia puerta de hercios continuos',
        'donde': 'DspEffects/CascadeShelfEq.h',
        'de_donde': 'una sola bandera para las dos repisas: tocar el selector de '
                    'la ALTA reescribia tambien la frecuencia continua de la '
                    'BAJA y se la perdia sin avisar, y solo cuando el indice de '
                    'la alta casualmente no era el que ya estaba',
    },
}


# ----------------------------------------------------------------------------
# LAS MUTACIONES
#
# Cada una es una lista de (de, a) sobre un fichero. `de` tiene que aparecer
# EXACTAMENTE una vez: si aparece mas, el script para, porque una mutacion
# ambigua es una mutacion que no sabes que estas midiendo.
#
# `muerde` es el texto de la comprobacion que TIENE que ponerse roja. No es
# decoracion: es lo que distingue "el arreglo esta vigilado" de "el banco se
# puso rojo por otra cosa, y eso no es una mordida. Varias opciones separadas por
# '|' cuando la misma comprobacion se puede leer de mas de una forma.
#
# Las mutaciones 2 y 3 son del MISMO arreglo a proposito: una quita el recorte
# (el defecto original) y la otra pone el recorte que PARECIA el bueno. Esa
# segunda es la que mas ha merecido la pena, porque `jlimit (0, 1)` sobre el
# `decay` convierte el valor negativo en 0, el 0 no es el otro caso del ternario
# que hay dentro, y la realimentacion se cae de 0.3 a 0: desaparece la cola de
# una variante que ABDEep consume con sus presets de fabrica. Un arreglo
# razonable que rompe la paridad es justo el fallo que un banco de mutaciones
# esta para cazar.
# ----------------------------------------------------------------------------

MUTACIONES = [
    {
        'id': 1,
        'arreglo': 'etapa-sin-vtable',
        'fichero': 'DspEffects/EffectPolicy.h',
        'titulo': 'La etapa de caracter vuelve a llevar vtable',
        'por_que': 'Se le devuelve el ancla virtual `prepareRateImpl` que la '
                   'auditoria quito porque nadie la sobrescribia. Ocho bytes y '
                   'una tabla de funciones por etapa a cambio de nada.',
        # El texto sale de la linea 2649 del banco. Lo que hay en el banco no es
        # "sin vtable" sino "no lleva vtable", que es lo que hace que este
        # `muerde` lo pille la primera corrida: la herramienta se puso AJENA
        # con el banco en rojo, y tenia razon. Un `muerde` escrito de memoria es
        # una afirmacion, y una afirmacion no vigila nada.
        'muerde': 'no lleva vtable|polimorfica',
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
        'id': 6,
        'arreglo': 'schroeder-damping-cero',
        'fichero': 'DspEffects/DspSchroederReverb.h',
        'titulo': 'La amortiguacion a cero vuelve a congelar el estado',
        'por_que': 'Con `damp1 = damping, damp2 = 1 - damping` y `damping` a 0 '
                   'exacto el estado es `estado = estado`: la cola no decae y se '
                   'queda en un offset de DC. Ojo al valor 0 EXACTO, que no es '
                   'NaN ni negativo y se colaba por el hueco del otro extremo.',
        'muerde': 'amortiguacion|congel|offset',
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
        'arreglo': 'diodo-umbral-recorte',
        'fichero': 'DspEffects/characters/DiodeBridge.h',
        'titulo': 'El umbral del puente de diodos se sale de rango',
        'por_que': 'El `jlimit` que ya estaba en las otras dos lecturas del '
                   'mismo fichero, al hueco que se le escapaba: la del propio '
                   '`threshold`. Con 1.5 el puente se volvia transparente y '
                   'AMPLIFICABA el tramo.',
        'muerde': 'umbral|threshold',
        'cambios': [
            (
                "        const float lim    = jlimit (0.0f, 1.0f, threshold);",
                "        const float lim    = threshold;"
            ),
        ],
    },
    {
        'id': 8,
        'arreglo': 'repisa-techo-045fs',
        'fichero': 'DspEffects/ShelfFilter.h',
        'titulo': 'La repisa vuelve al techo de 0,2 veces el sample rate',
        'por_que': 'El limite "prudente" que se le ocurrio a alguien y que '
                   'MOVIA las frecuencias del propio MS2000 a 32 kHz: las '
                   'repisas de 8 y de 12 kHz se recortaban a 6,4 kHz y sonaban '
                   'en otro sitio. El MS2000 usa 12 kHz y 12000/32000 = 0,375, '
                   'asi que cualquier limite por debajo cambia el sonido de un '
                   'producto ya publicado.',
        'muerde': 'la repisa no recorta las frecuencias que el MS2000 usa de verdad',
        'cambios': [
            (
                "        const float limite = sampleRate_ * 0.45f;",
                "        const float limite = sampleRate_ * 0.2f;"
            ),
        ],
    },
    {
        'id': 9,
        'arreglo': 'repisa-banda-muerta',
        'fichero': 'DspEffects/ShelfFilter.h',
        'titulo': 'La ganancia pierde la banda muerta de 0,05 dB',
        'por_que': 'Sin ella, una automatizacion que se mueve en pasos de '
                   'milisegundo recalcula cinco coeficientes por paso. Es la '
                   'parte del original que mas trabajo ahorra, y la unica cuya '
                   'prueba tiene que observar que NO PASA: quitar el recorte no '
                   'muele, por eso esta mutacion deshace el arreglo al reves.',
        'muerde': 'un movimiento de ganancia dentro de la banda muerta no recalcula',
        'cambios': [
            (
                "        if (jmax (diferencia, -diferencia) > 0.05f)",
                "        if (jmax (diferencia, -diferencia) > 0.0f)"
            ),
        ],
    },
    {
        'id': 10,
        'arreglo': 'cascada-indice-recortado',
        'fichero': 'DspEffects/CascadeShelfEq.h',
        'titulo': 'La cascada se come el recorte de los indices',
        'por_que': 'Sin recorte, un 99 de un perfil de cuatro lee fuera de la '
                   'tabla. Que el recorte sea de la maquina y no del MS2000 es '
                   'justo lo que hace que la maquina sirva para otro producto: '
                   'un perfil de ocho no se protege solo.',
        'muerde': 'un indice por encima se recorta a la ultima posicion',
        'cambios': [
            (
                "        return index < 0 ? 0\n"
                "             : (index > Profile::numPositions - 1) ? Profile::numPositions - 1\n"
                "             : index;",

                "        return index;"
            ),
        ],
    },
    {
        'id': 11,
        'arreglo': 'cascada-ganancia-independiente',
        'fichero': 'DspEffects/CascadeShelfEq.h',
        'titulo': 'El mando de ganancia se lleva por delante la frecuencia',
        'por_que': 'El fallo mastipico de un puerto de este tipo: escribir '
                   '`setFrequencyHz` donde iba `setGainDB` en un cuerpo donde '
                   'las dos llamadas estan cerca. Con knobs en 0 dB no se oye '
                   'nada, que es por eso que cuela hasta que alguien mueve un '
                   'mando y la repisa se va de sitio.',
        'muerde': 'mover la ganancia deja la frecuencia donde estaba',
        'cambios': [
            (
                "    void setLowGainDB (float db) noexcept  { bajo_.setGainDB (db); }",
                "    void setLowGainDB (float db) noexcept\n"
                "    {\n"
                "        bajo_.setGainDB (db);\n"
                "        bajo_.setFrequencyHz (db);   // <-- la confusion\n"
                "    }"
            ),
        ],
    },
    {
        'id': 12,
        'arreglo': 'cascada-puerta-vuelve',
        'fichero': 'DspEffects/CascadeShelfEq.h',
        'titulo': 'La puerta de hercios continuos se queda abierta para siempre',
        'por_que': 'La primera version hacia la puerta de ida: en cuanto un '
                   'producto escribia un hercio a mano, su tabla de selector '
                   'quedaba muerta para siempre, y el propio comentario del '
                   'codigo decia lo contrario. Esta mutacion es la que lo cazó '
                   'en la primera corrida, y por eso existe: un defecto '
                   'escrito en la cabecera y contradicho por el codigo solo se '
                   've mirando el codigo.',
        'muerde': 'volver a elegir por indice recupera la tabla',
        'cambios': [
            (
                "        lowIndex_ = recortado;\n"
                "        lowUsaTabla_ = true;\n"
                "        aplicaFrecuencias();",

                "        lowIndex_ = recortado;\n"
                "        aplicaFrecuencias();"
            ),
        ],
    },
    {
        'id': 13,
        'arreglo': 'cascada-puerta-independiente',
        'fichero': 'DspEffects/CascadeShelfEq.h',
        'titulo': 'El selector de la alta se lleva por delante el hercio de la baja',
        'por_que': 'La version con UNA bandera para las dos repisas. El defecto '
                   'era intermitente, que es lo que lo hacia peligroso: solo se '
                   'manifestaba cuando el indice de la alta casualmente no era el '
                   'que ya estaba, porque la guarda de "no ha cambiado" cortaba '
                   'antes. Un fallo que depende de en que posicion tapaba el '
                   'mando del usuario es el que mas se tarda en ver.',
        'muerde': 'y NO se lleva por delante el hercio continuo de la baja',
        'cambios': [
            (
                "        if (lowUsaTabla_)\n"
                "            bajo_.setFrequencyHz (Profile::lowFreqs [lowIndex_]);",

                "        bajo_.setFrequencyHz (Profile::lowFreqs [lowIndex_]);"
            ),
        ],
    },
]


# ----------------------------------------------------------------------------
# Utilidades de fichero. Todo pasa por aqui, y todo comprueba.
# ----------------------------------------------------------------------------

def checksum (ruta):
    with open (ruta, 'rb') as f:
        return hashlib.sha256 (f.read()).hexdigest ()


def leer (ruta):
    """Devuelve (texto normalizado a \\n, es_crlf)."""
    with open (ruta, 'rb') as f:
        crudo = f.read()

    es_crlf = b'\r\n' in crudo
    texto = crudo.decode('utf-8').replace('\r\n', '\n')
    return texto, es_crlf


def escribir (ruta, texto, es_crlf):
    if es_crlf:
        texto = texto.replace('\n', '\r\n')
    with open (ruta, 'wb') as f:
        f.write (texto.encode ('utf-8'))


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
# NIVEL 0. LA TABLA.
#
# Lo unico que se puede comprobar sin pagar compilacion, y por eso es lo que
# se puede ejecutar en cada commit. Devuelve una lista de (codigo, mensaje).
# Los codigos son los que la autocomprobacion exige ver, asi que son parte del
# contrato y no se tocan sin cambiar tambien la autocomprobacion.
# ----------------------------------------------------------------------------

def revisar_tabla (mutaciones=None, arreglos=None, contenidos=None):
    """Revisa la tabla sin tocar el arbol.

    `contenidos` permite pasar los textos a mirar en vez de leerlos del disco:
    es lo que hace la autocomprobacion para meter una tabla rota a proposito.
    """
    if mutaciones is None:
        mutaciones = MUTACIONES
    if arreglos is None:
        arreglos = ARREGLOS
    if contenidos is None:
        contenidos = {}
        for nombre in set ([m['fichero'] for m in mutaciones] + [BANCO_DE_PRUEBAS]):
            ruta = os.path.join (RAIZ, nombre)
            if not os.path.isfile (ruta):
                continue
            with open (ruta, 'rb') as f:
                contenidos[nombre] = f.read ().decode ('utf-8')

    fallos = []

    def anotar (codigo, mensaje):
        fallos.append ((codigo, mensaje))

    # --- 1. El banco de pruebas tiene que estar a mano. Sin el no se puede
    # ---    comprobar de donde salen los textos de `muerde`.
    banco = contenidos.get (BANCO_DE_PRUEBAS)
    if banco is None:
        anotar ('FICHERO_AUSENTE', 'no se encuentra %s' % BANCO_DE_PRUEBAS)
        banco = ''

    # --- 2. Ids sin repetir. Dos mutaciones con el mismo id y `--only 4` solo
    # ---    correrian una de las dos, en silencio.
    vistos = {}
    for m in mutaciones:
        ident = m.get ('id')
        if ident in vistos:
            anotar ('ID_DUPLICADO', 'el id %s esta en dos mutaciones (%s y %s)'
                    % (ident, vistos[ident], m.get ('titulo', '?')))
        else:
            vistos[ident] = m.get ('titulo', '?')

    # --- 3. Cada mutacion deshace un arreglo que existe en el catalogo.
    for m in mutaciones:
        if m.get ('arreglo') not in arreglos:
            anotar ('ARREGLO_DESCONOCIDO',
                    'la mutacion %s dice que deshace "%s" y ese arreglo no esta '
                    'en el catalogo' % (m.get ('id'), m.get ('arreglo')))

    # --- 4. Y CADA ARREGLO TIENE AL MENOS UNA MUTACION. Esta es la condicion
    # ---    que responde a "cada arreglo sigue teniendo un test que muerda":
    # ---    un arreglo sin mutacion es un arreglo sin vigilancia, y lo mas
    # ---    probable es que nadie se haya dado cuenta.
    con_mutacion = set (m.get ('arreglo') for m in mutaciones)
    for clave, a in arreglos.items ():
        if clave not in con_mutacion:
            anotar ('ARREGLO_SIN_MUTACION',
                    'el arreglo "%s" (%s) no tiene ninguna mutacion que lo '
                    'deshaga, osea que nadie lo esta vigilando'
                    % (clave, a.get ('titulo', '')))

    # --- 5. Los anclajes. Una mutacion cuyo ancla ya no esta no mide nada.
    for m in mutaciones:
        nombre = m.get ('fichero')
        if nombre not in contenidos:
            anotar ('FICHERO_AUSENTE', 'la mutacion %s no encuentra %s'
                    % (m.get ('id'), nombre))
            continue

        texto = contenidos[nombre].replace ('\r\n', '\n')
        for de, _a in m.get ('cambios', []):
            cuantos = texto.count (de)
            if cuantos == 0:
                anotar ('ANCLA_AUSENTE',
                        'la mutacion %s ya no encuentra su ancla en %s:\n      %s'
                        % (m.get ('id'), nombre, de.strip ().splitlines ()[0][:70]))
            elif cuantos > 1:
                anotar ('ANCLA_AMBIGUA',
                        'la mutacion %s tiene un ancla %d veces en %s, y no se '
                        'sabe que esta midiendo'
                        % (m.get ('id'), cuantos, nombre))

    # --- 6. `muerde` TIENE que existir en el banco. Este es el cruce entre los
    # ---    dos ficheros y el que convierte "el arreglo esta vigilado" en algo
    # ---    comprobable: si la comprobacion se renombra o se borra, la tabla se
    # ---    queda mirando al vacio y el nivel rapido lo dice.
    for m in mutaciones:
        morde = m.get ('muerde', '')
        if not morde:
            anotar ('SIN_MUERDE',
                    'la mutacion %s no declara `muerde`, osea que contaria como '
                    'deteccion cualquier rojo, incluido el equivocado'
                    % m.get ('id'))
            continue

        for opcion in morde.split ('|'):
            if opcion and opcion in banco:
                break
        else:
            anotar ('MORDIDA_AUSENTE',
                    'la mutacion %s dice que debe morder "%s" y ese texto no esta '
                    'en %s: la comprobacion se renombro o se borro'
                    % (m.get ('id'), morde, BANCO_DE_PRUEBAS))

    return fallos


def nivel_rapido ():
    fallos = revisar_tabla ()
    mutaciones = MUTACIONES
    print ('%d arreglos, %d mutaciones, %d de ellas con una comprobacion que '
           'debe morder' % (len (ARREGLOS), len (mutaciones), len (mutaciones)))
    print ('   (todo estatico: ni compila ni toca el arbol)\n')

    if not fallos:
        print ('   [OK] cada arreglo tiene su mutacion, cada anclaje esta donde '
               'tiene que estar\n'
               '   [OK] y cada `muerde` sigue siendo una comprobacion de verdad')
        return 0

    print ('La tabla NO esta bien:\n', file=sys.stderr)
    for codigo, mensaje in fallos:
        print ('  %-20s %s' % (codigo, mensaje), file=sys.stderr)
    return 1


# ----------------------------------------------------------------------------
# NIVEL 1. LA AUTOCOMPROBACION.
#
# Un revisor que nunca ha visto un fallo no es un revisor. Se le pasan entradas
# rotas A PROPOSITO y se exige que senale cada una. Si se calla, esto sale en
# rojo: es la unica forma de que "la tabla esta bien" signifique algo.
# ----------------------------------------------------------------------------

def clasificar (rc, salida, morde):
    """Clasifica una corrida. Devuelve (estado, motivo).

    Los estados, y la razon por la que estan en este orden:

        ROTA        el banco no ha compilado, o no compila el fichero. Un banco
                    que no compila no muerde: muerde el compilador.
        SOBREVIVE   el banco sigue VERDE. El arreglo no tiene ninguna
                    comprobacion que lo vigile.
        AJENA       el banco se ha puesto rojo, pero no dice `muerde`. Se ha
                    puesto rojo por otra comprobacion, y contar eso como
                    deteccion es como se cuela un arreglo sin vigilar.
        DETECTADA   rojo, y el rojo es el que toca: la comprobacion que muerde
                    es la que se ha puesto falsa.
    """
    if 'error C' in salida or 'error LNK' in salida or 'fatal error' in salida:
        return 'ROTA', 'el banco NO COMPILA con la mutacion'

    if esta_verde (rc, salida):
        return 'SOBREVIVE', 'el banco se quedo VERDE: nadie caza esta mutacion'

    rojas = [l for l in salida.splitlines () if l.startswith ('[FAIL]')]
    if not rojas:
        for l in salida.splitlines ():
            if l.startswith ('[FALLO]'):
                return 'AJENA', 'el banco se para con: %s' % l.strip ()[:88]
        return 'AJENA', 'el banco sale con codigo %d y sin decir porque' % rc

    # OJO al nombre del bucle: `for morde in morde.split (...)` hace que `morde`
    # sea local de la generacion y revienta con UnboundLocalError al evaluarse el
    # iterable. Es el fallo mas tonto que se puede escribir en una comprehension
    # y lo ha escrito esta herramienta, que luego dice de las suyas.
    #
    # Y se devuelve la linea que HA ENCONTRADO, no la primera. Importa: una
    # mutacion puede poner en rojo tres comprobaciones, y el informe tiene que
    # ensenar la que declara `muerde`, o el informe miente sobre por que se ha
    # contado. La primera en Rojo puede ser una consecuencia de milagro.
    for linea in rojas:
        if any (opcion in linea for opcion in morde.split ('|') if opcion):
            return 'DETECTADA', linea[7:].strip ()[:88]

    return ('AJENA',
            'el banco se pone rojo pero no por aqui: entro "%s" y la '
            'comprobacion que muerde es "%s"' % (rojas[0][7:].strip ()[:60], morde))


def _contenidos_reales ():
    contenido = {}
    for nombre in set ([m['fichero'] for m in MUTACIONES] + [BANCO_DE_PRUEBAS]):
        ruta = os.path.join (RAIZ, nombre)
        if os.path.isfile (ruta):
            with open (ruta, 'rb') as f:
                contenido[nombre] = f.read ().decode ('utf-8')
    return contenido


def _autocomprobacion ():
    fallos = []
    contenido = _contenidos_reales ()

    print ('1. La tabla de verdad, sin arreglar nada:')
    reales = revisar_tabla (MUTACIONES, ARREGLOS, contenido)
    if reales:
        for codigo, mensaje in reales:
            print ('   ROTA -- %s: %s' % (codigo, mensaje), file=sys.stderr)
        fallos.append ('la tabla real ya esta rota antes de empezar')
    else:
        print ('   [OK] los %d arreglos con sus %d mutaciones pasan el cruce'
               % (len (ARREGLOS), len (MUTACIONES)))

    # --- La tabla rota a proposito. Cada trozo se mete uno a uno para saber
    # --- CUAL de los fallos se deja pasar, y no solo que hay alguno.
    print ('\n2. La tabla rota a proposito, que tiene que ser señalada:')

    def romper (cambios, indice=1):
        """La tabla entera, con UNA mutacion cambiada y las otras once igual.

        Se sustituye en su sitio y no se quita: si el caso se armara quitando
        mutaciones, el revisor veria tambien `ARREGLO_SIN_MUTACION` y no se
        sabria si ha encontrado lo que se le pedia o el fallo de al lado. Que
        cada caso se note su fallo y solo el suyo es lo que lo hace una prueba.
        """
        return [dict (m, cambios=cambios) if m is MUTACIONES[indice] else m
                for m in MUTACIONES]

    casos = [
        ('ARREGLO_SIN_MUTACION',
         dict (mutaciones=[m for m in MUTACIONES if m['arreglo'] != 'diodo-umbral-recorte']),
         'un arreglo del catalogo sin ninguna mutacion que lo deshaga'),

        ('ARREGLO_DESCONOCIDO',
         dict (mutaciones=MUTACIONES + [dict (MUTACIONES[0], id=900, arreglo='no-existe')]),
         'una mutacion que dice deshacer un arreglo que no esta en el catalogo'),

        ('MORDIDA_AUSENTE',
         dict (mutaciones=[dict (m, muerde='una comprobacion que no existe en el banco')
                           for m in MUTACIONES]),
         'una mutacion cuyo `muerde` no esta en el banco de pruebas'),

        ('SIN_MUERDE',
         dict (mutaciones=[dict ((k, v) for k, v in m.items () if k != 'muerde')
                           for m in MUTACIONES]),
         'una mutacion sin `muerde`, que contaria como deteccion cualquier rojo'),

        ('ANCLA_AUSENTE',
         dict (mutaciones=romper ([('ESTA ANCLA NO EXISTE EN NINGUN SITIO', 'x')])),
         'una mutacion cuyo ancla ya no esta en el fichero'),

        ('ANCLA_AMBIGUA',
         dict (mutaciones=romper ([('decay_', 'x')])),
         'una mutacion con un ancla que aparece varias veces'),

        ('ID_DUPLICADO',
         dict (mutaciones=MUTACIONES + [dict (MUTACIONES[0])]),
         'dos mutaciones con el mismo id'),
    ]

    for esperado, kwargs, explicacion in casos:
        lo_que_sale = {c for c, _m in revisar_tabla (contenidos=contenido, **kwargs)}
        if esperado in lo_que_sale:
            print ('   [OK] senala %-20s (%s)' % (esperado, explicacion))
        else:
            print ('   ROTA -- NO senala %s (%s); senalo: %s'
                   % (esperado, explicacion, sorted (lo_que_sale) or 'nada'),
                   file=sys.stderr)
            fallos.append ('el revisor se ha callado ante %s' % esperado)

    # --- Las clasificaciones, con salidas de mentira. Que `clasificar` sepa
    # --- decir "roja pero no por aqui" es media herramienta.
    print ('\n3. Las clasificaciones, con salidas de mentira:')
    casos_clase = [
        ((0, '[OK] DspEffects: 933 comprobaciones\n'), 'SOBREVIVE',
         'el banco verde con la mutacion puesta'),
        ((1, 'error C2065: algo sin declarar\n'), 'ROTA',
         'el banco que no compila'),
        ((1, '[FAIL] la repisa no recorta las frecuencias que el MS2000 usa de verdad\n'), 'DETECTADA',
         'el banco rojo justo por la comprobacion que debe morder'),
        ((1, '[FAIL] una comprobacion de otra cosa que no tiene nada que ver\n'), 'AJENA',
         'el banco rojo, pero por OTRA comprobacion'),
        ((1, '[FALLO] el banco se ha caido antes de terminar\n'), 'AJENA',
         'el banco que se para sin decir cual'),
    ]
    for (rc, salida), esperado, explicacion in casos_clase:
        morde = 'la repisa no recorta las frecuencias que el MS2000 usa de verdad'
        estado, _motivo = clasificar (rc, salida, morde)
        if estado == esperado:
            print ('   [OK] %-10s %s' % (estado, explicacion))
        else:
            print ('   ROTA -- esperaba %s y salio %s (%s)'
                   % (esperado, estado, explicacion), file=sys.stderr)
            fallos.append ('clasificar dice %s donde deberia decir %s' % (estado, esperado))

    # --- Y el mas tonto de todos: un banco que dice [OK] y luego revienta.
    print ('\n4. El banco que dice [OK] y despues se cae:')
    estado, _m = clasificar (1, '[OK] DspEffects: 933 comprobaciones\n', 'lo que sea')
    if estado == 'SOBREVIVE':
        print ('   ROTA -- lo toma por verde solo porque trae [OK] y el codigo es 1',
               file=sys.stderr)
        fallos.append ('esta_verde se traga un codigo de salida distinto de cero')
    else:
        print ('   [OK] el codigo de salida pesa mas que el [OK] impreso')

    if fallos:
        print ('\nLa autocomprobacion NO PASA. O el revisor no mira, o el\n'
               'clasificador no clasifica. En los dos casos, "%s de %d" no es\n'
               'una afirmacion que este script pueda hacer.'
               % (len (MUTACIONES) - len (fallos), len (MUTACIONES)), file=sys.stderr)
        for f in fallos:
            print ('  - %s' % f, file=sys.stderr)
        return 1

    def num (n):
        """Un numero en el texto del resumen, que no se escribe a mano.

        Los dos numeros del resumen estaban fijos ('los cinco fallos', 'los
        cuatro estados') y caducaron en cuanto la tabla grew de cinco casos a
        siete: el script seguia diciendo cinco mientras senalaba siete. Un
        numero que se cuenta a mano es un numero que miente en silencio.
        """
        return 'uno' if n == 1 else str (n)

    print ('\n[OK] autocomprobacion: el revisor senala los %s fallos que se le han '
           'puesto,\n     y el clasificador distingue los %s estados. La corrida '
           'real ya se puede fiar.'
           % (num (len (casos)), num (len (casos_clase))))
    return 0


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
    # El codigo de salida pesa mas que lo que imprime. Un banco que suelta el
    # [OK] y luego revienta no esta verde, por muy verde que se vea.
    return rc == 0 and '[OK]' in salida


def motivo_rojo (rc, salida):
    """Por que esta rojo, para el informe. Distingue compilar de fallar."""
    for linea in salida.splitlines ():
        if linea.startswith ('[FAIL]'):
            return 'el banco se pone rojo: %s' % linea[7:].strip ()[:88]

    for linea in salida.splitlines ():
        if linea.startswith ('[FALLO]'):
            return linea.strip ()

    if rc != 0:
        return 'el banco sale con codigo %d' % rc

    return 'el banco se quedo VERDE: nadie caza esta mutacion'


# ----------------------------------------------------------------------------

def listar ():
    por_arreglo = {}
    for m in MUTACIONES:
        por_arreglo.setdefault (m['arreglo'], []).append (m)

    print ('%d arreglos vigilados por %d mutaciones. Cada arreglo necesita al '
           'menos una.\n' % (len (ARREGLOS), len (MUTACIONES)))

    for clave, a in ARREGLOS.items ():
        print ('  %s' % clave)
        print ('     %s' % a['titulo'])
        print ('     en %s -- %s' % (a['donde'], a['de_donde']))

        muts = por_arreglo.get (clave, [])
        if not muts:
            print ('     SIN MUTACION -- nadie lo esta vigilando\n')
            continue

        for m in muts:
            print ('     %d. %s' % (m['id'], m['titulo']))
            for linea in m['por_que'].split ('. '):
                linea = linea.strip ()
                print ('        %s%s' % (linea, '' if linea.endswith ('.') else '.'))
            print ('        debe morder: "%s"' % m['muerde'])
        print ()


def elegir (args):
    elegidas = MUTACIONES

    if args.arreglo:
        clave = args.arreglo.strip ()
        if clave not in ARREGLOS:
            print ('--arreglo no coincide con ningun arreglo del catalogo (mira con --list)',
                   file=sys.stderr)
            return None
        elegidas = [m for m in MUTACIONES if m['arreglo'] == clave]

    if args.only:
        pedidos = set ()
        for trozo in args.only.split (','):
            trozo = trozo.strip ()
            if trozo.isdigit ():
                pedidos.add (int (trozo))
            else:
                print ('--only solo admite numeros, "%s" no lo es' % trozo, file=sys.stderr)
                return None

        elegidas = [m for m in elegidas if m['id'] in pedidos]
        if not elegidas:
            print ('--only no coincide con ninguna mutacion', file=sys.stderr)
            return None

    return elegidas


def main ():
    ap = argparse.ArgumentParser (
        description='Banco de mutaciones de DspEffects, anclado a los arreglos',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument ('--list', action='store_true', help='solo listar los arreglos y sus mutaciones')
    ap.add_argument ('--rapido', action='store_true',
                     help='solo el cruce estatico: sin compilar y sin tocar el arbol')
    ap.add_argument ('--autocomprobacion', action='store_true',
                     help='comprobar que el revisor senala una tabla rota a proposito')
    ap.add_argument ('--only', default='',
                     help='ids separados por comas, para hacer una tanda (1,2,7)')
    ap.add_argument ('--arreglo', default='',
                     help='una sola clave de arreglo (--list para verlas)')
    ap.add_argument ('--build-dir', default=BUILD_POR_DEFECTO,
                     help='donde esta la build de ABDNeural (por defecto: %s)' % BUILD_POR_DEFECTO)
    args = ap.parse_args ()

    if args.list:
        listar ()
        return 0

    if args.rapido:
        return nivel_rapido ()

    if args.autocomprobacion:
        return _autocomprobacion ()

    # --- La tabla, antes de nada. Si `muerde` ya no existe, esta corrida no
    # --- puede decir nada, y es una perdida de tiempo pagarla entera.
    fallos = revisar_tabla ()
    if fallos:
        print ('La tabla NO esta bien, asi que la corrida no puede medirse:\n',
               file=sys.stderr)
        for codigo, mensaje in fallos:
            print ('  %-20s %s' % (codigo, mensaje), file=sys.stderr)
        return 2

    elegidas = elegir (args)
    if elegidas is None:
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
               'decir nada: si ya falla, todo mutado tambien "fallaria" y todas las\n'
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

            print ('%d. [%s] %s' % (m['id'], m['arreglo'], m['titulo']))

            error = aplicar (ruta, m['cambios'])
            if error is not None:
                print ('   ROTA -- %s' % error, file=sys.stderr)
                rotas.append ((m['id'], m['arreglo'], 'no se pudo aplicar'))
                _restaurar (respaldos, [m['fichero']])
                continue

            rc, salida = compilar (args.build_dir)
            if rc != 0:
                print ('   ROTA -- el banco no compila con la mutacion:\n%s'
                       % _cola (salida), file=sys.stderr)
                rotas.append ((m['id'], m['arreglo'], 'no compila'))
                _restaurar (respaldos, [m['fichero']])
                continue

            rc, salida = correr (exe)
            estado, motivo = clasificar (rc, salida, m['muerde'])

            if estado == 'DETECTADA':
                detectadas += 1
                print ('   detectada -- %s' % motivo)
            elif estado == 'SOBREVIVE':
                print ('   NO DETECTADA -- el banco se quedo verde deshaciendo esto.\n'
                       '   El arreglo "%s" no tiene ninguna comprobacion que lo\n'
                       '   vigile. Eso NO es bueno: hay que anadirla al banco.'
                       % m['arreglo'], file=sys.stderr)
                rotas.append ((m['id'], m['arreglo'], 'sobrevive'))
            elif estado == 'AJENA':
                print ('   ROJA PERO NO POR AQUI -- %s\n'
                       '   El arreglo "%s" puede que este vigilado, o no: este\n'
                       '   script no lo puede decir, y por eso cuenta como fallo.'
                       % (motivo, m['arreglo']), file=sys.stderr)
                rotas.append ((m['id'], m['arreglo'], 'roja por otro sitio'))
            else:
                print ('   ROTA -- %s' % motivo, file=sys.stderr)
                rotas.append ((m['id'], m['arreglo'], 'rota'))

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
            rotas.append ((0, '(reconstruccion)', 'la reconstruccion final no compila'))
        else:
            rc, salida = correr (exe)
            if not esta_verde (rc, salida):
                print ('   ROTA -- y ademas el banco devuelto no esta verde:\n'
                       '   %s' % motivo_rojo (rc, salida), file=sys.stderr)
                rotas.append ((0, '(reconstruccion)', 'el banco devuelto no esta verde'))
            else:
                print ('   [OK] banco devuelto y en verde')
    finally:
        # --- LA TRAMPA 2. Pase lo que pase, el arbol vuelve como estaba.
        _restaurar (respaldos, list (respaldos.keys ()))

    total = len (elegidas)
    print ('\n%s de %d mutaciones detectadas, por el motivo que toca'
           % (detectadas, total))

    if rotas:
        print ('\nArreglos sin mordida verificada:', file=sys.stderr)
        for ident, clave, motivo in rotas:
            print ('  %s (%s): %s' % (clave, ident, motivo), file=sys.stderr)
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
