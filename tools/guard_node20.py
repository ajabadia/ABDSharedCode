#!/usr/bin/env python3
"""Falla si algun workflow de la suite declara una accion que corre en Node 20.

QUE ESTE GUARD VIGILA, Y POR QUE EXISTE
=======================================

Node 20 dejo de recibir parches de seguridad en abril de 2026. Una accion que
declara `runs.using: node20` en su `action.yml` sigue siendo correcta en el
sentido de que GitHub la ejecuta, y por eso nada se rompe de golpe: el rojo
aparece semanas despues, cuando GitHub apaga el runtime, y para entonces el
fichero lleva meses commiteado y el commit que lo subio ya no esta a la vista.

Un rojo de esa clase es el peor sitio para enterarse: no dice que repo, no dice
que linea, y el que lo arregla tiene que recorrer toda la suite adivinando. Este
guard es el que dice "tu workflow, tu linea, tu pin", en el commit que lo
introduce y no tres meses despues.

COMO SE SABE EL RUNTIME DE UNA ACCION
======================================

Del `action.yml` de la accion, en el tag exacto que el workflow declara:
`runs.using`. No de memoria, no de la major de la accion, y no suponiendo que
el corte es el mismo para todas. ESTO NO ES UNA SUPUESTICION Y SE NOTA:

    actions/checkout@v4          node20     actions/upload-artifact@v5   node20
    actions/checkout@v5          node24     actions/upload-artifact@v6   node24
    actions/setup-node@v5        node24     actions/download-artifact@v5 node20
    actions/cache@v5             node24     actions/download-artifact@v6 node20
    actions/setup-python@v6      node24     actions/download-artifact@v7 node24

`upload-artifact@v5` sigue en node20 y `download-artifact@v6` tambien. Si este
guard tuviera una regla de la forma "v5 o menos es node20", dejaria pasar dos
pins que hoy estan en rojo en GitHub. Por eso la tabla de abajo dice, de cada
accion, EL ULTIMO MAJOR QUE SIGUE SIENDO NODE20, y ese numero se leyo de su
`action.yml`. Quien anada una accion nueva tiene que hacer lo mismo; si no, el
guard falla diciendo que no la conoce. Falla cerrado a proposito: en este repo ya
esta escrito que "un guard que se puede dejar en warning es un guard que no
guarda", y una tabla de la que no se avisa cuando le falta una fila es una tabla
que se queda vieja sola.

LA EXENTA, Y POR QUE NO SE BORRA
================================

`ilammy/msvc-dev-cmd` esta en node20 y se queda. No por globales: porque no hay
v2. Su tag mas alto a dia de hoy es v1.13.0 y su rama por defecto sigue
declarando `using: node20`, o sea que aguas arriba no ha salido de ahi. Subirlo
no es posible; bajarlo no es el arreglo.

La exencion es POR PIN EXACTO, no por accion: si alguien sube el pin, el pin ya
no coincide con el exento y el guard vuelve a fallar. Una exencion que vale
para "esta accion" deja de valer en cuanto la accion cambia, y en ese momento es
justo cuando hay que volver a decidir. Aqui hay que mirar otra vez.

COMO SE USA
===========

    python tools/guard_node20.py --raiz D:/desarrollos/ABDSynths

Salidas: 0 si ningun workflow declara una accion en node20, 1 si hay alguno,
2 si el directorio no existe o no hay workflows que mirar. El 2 es aparte del 1
a proposito: "no he podido mirar" no es "esta todo limpio", y un guard que
confunde las dos cosas da un verde que no significa nada.
"""

import argparse
import io
import os
import re
import sys

# ACCION (en minusculas, como las da GitHub) -> (ultimo major que sigue siendo
# node20, donde empieza node24). El segundo valor es el major minimo admitido.
# Todo lo leido del action.yml del tag correspondiente; ver la cabecera.
CORTES = {
    'actions/checkout': (4, 5),
    'actions/setup-node': (4, 5),
    'actions/cache': (4, 5),
    'actions/setup-python': (5, 6),
    'actions/upload-artifact': (5, 6),
    'actions/download-artifact': (6, 7),
    'microsoft/setup-msbuild': (2, 3),
    'pnpm/action-setup': (4, 5),
    'softprops/action-gh-release': (2, 3),
    'jwlawson/actions-setup-cmake': (1, 2),
    'mymindstorm/setup-emsdk': (14, 15),
    'jamesives/github-pages-deploy-action': (0, 4),
}

# EXENTAS: pin exacto -> por que se acepta. Un pin distinto NO esta exento.
EXENTAS = {
    'ilammy/msvc-dev-cmd@v1': 'no hay v2; su rama por defecto sigue en node20',
}

# Una accion local de este repo no declara `runs.using: node`: es composite, y
# el runtime lo pone el runner. Contarlas como node20 seria un falso positivo.
RE_LOCAL = re.compile(r'(\.github/actions/|^\./)')
RE_USES = re.compile(r'^\s*(?:-\s*)?uses:\s*([^\s#]+)\s*(#.*)?$')
RE_VERSION = re.compile(r'^v?(\d+)')
RE_REPO = re.compile(r'repository:\s*[\'"]?([\w.-]+/[\w.-]+)')


def workflows_de(raiz):
    """Los ficheros de workflow de todos los repos de la suite, con su repo."""
    vistos = []
    for repo in sorted(os.listdir(raiz)):
        base = os.path.join(raiz, repo, '.github')
        if not os.path.isdir(base):
            continue
        carpetas = [os.path.join(base, 'workflows')]
        acciones = os.path.join(base, 'actions')
        if os.path.isdir(acciones):
            for sub in sorted(os.listdir(acciones)):
                acciones_sub = os.path.join(acciones, sub, 'action.yml')
                if os.path.isfile(acciones_sub):
                    carpetas = carpetas + [acciones_sub]
        for carpeta in carpetas:
            if os.path.isfile(carpeta):
                vistos.append((repo, carpeta))
                continue
            if not os.path.isdir(carpeta):
                continue
            for nombre in sorted(os.listdir(carpeta)):
                if nombre.endswith(('.yml', '.yaml')):
                    vistos.append((repo, os.path.join(carpeta, nombre)))
    return vistos


def leer_uses(ruta):
    """(numero de linea, pin declarado) de cada `uses:` de un fichero."""
    with io.open(ruta, 'r', encoding='utf-8') as fh:
        for numero, linea in enumerate(fh, 1):
            if linea.lstrip().startswith('#'):
                continue
            m = RE_USES.match(linea.rstrip('\n'))
            if m is None:
                continue
            yield (numero, m.group(1).strip().strip('\'"'))


def clasificar(repo, fichero, linea, pin):
    """Que hacer con un pin.

    'falta'  -> es un hallazgo, el guard sale con 1
    'sha'    -> no se puede mirar sin red, se imprime y no se falla
    None     -> esta bien

    Un SHA se aparta a proposito. El runtime de una accion lo dice su
    `action.yml` EN ESE SHA, y un guard que corre sin red no lo puede leer. Habia
    que elegir entre fallar (y bloquear un pin inmutable, que es la forma
    correcta de fijar una dependencia) o pasar en silencio. Se imprime, que es lo
    unico que no es ciego: quien lo lea sabe que ahi no se ha comprobado nada,
    en vez de creer que si.
    """
    if RE_LOCAL.search(pin):
        return None
    if '@' not in pin:
        return None  # una accion local sin ref: no es de esta suite
    accion, ref = pin.rsplit('@', 1)
    clave = accion.lower()
    if RE_VERSION.match(ref) is None:
        return 'sha'
    exencion = EXENTAS.get('%s@%s' % (clave, ref))
    if exencion is not None:
        return None
    por_prefijo = [k for k in EXENTAS if k.split('@')[0] == clave]
    if por_prefijo:
        return ('falta', '%s esta exenta solo en %s' % (accion, por_prefijo[0].split('@')[1]),
                'vuelve al pin exento, o revisa si hay major nuevo')
    if clave not in CORTES:
        return ('falta', '%s no esta en la tabla del guard' % accion,
                'mira su action.yml en el tag que declara y anadela a CORTES '
                'con el ultimo major que sigue en node20')
    ultimo_node20, minimo = CORTES[clave]
    major = int(RE_VERSION.match(ref).group(1))
    if major >= minimo:
        return None
    if major == ultimo_node20:
        return ('falta', '%s@%s corre en node20' % (accion, ref),
                'sube a %s@%s o mas' % (accion, 'v%d' % minimo))
    return ('falta', '%s@%s es anterior al ultimo node20 conocido (%s@v%d), '
            'asi que tambien lo es' % (accion, ref, accion, ultimo_node20),
            'sube a %s@%s o mas' % (accion, 'v%d' % minimo))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument('--raiz', default=os.path.dirname(here),
                    help='directorio que contiene los repos de la suite')
    ap.add_argument('--pins-de', default=None,
                    help='workflow que declara los repos a clonar: se comprueba '
                         'que clona todos los que tienen workflows')
    args = ap.parse_args()

    raiz = os.path.abspath(args.raiz)
    if not os.path.isdir(raiz):
        sys.stderr.write('la raiz %s no existe\n' % raiz)
        return 2

    ficheros = workflows_de(raiz)
    if not ficheros:
        sys.stderr.write(
            'bajo %s no hay ningun .github/workflows: no se ha medido nada. '
            'Un 0 aqui diria que la suite esta limpia, y no se sabe.\n' % raiz)
        return 2

    hallazgos = []
    sin_verificar = {}
    pines = {}
    for repo, fichero in ficheros:
        for linea, pin in leer_uses(fichero):
            pines[pin] = pines.get(pin, 0) + 1
            veredicto = clasificar(repo, fichero, linea, pin)
            if veredicto == 'sha':
                sin_verificar[pin] = sin_verificar.get(pin, 0) + 1
            elif isinstance(veredicto, tuple) and veredicto[0] == 'falta':
                relativa = os.path.relpath(fichero, raiz).replace(os.sep, '/')
                hallazgos.append((repo, relativa, linea, pin,
                                  veredicto[1], veredicto[2]))

    if args.pins_de:
        # LO QUE ESTE PASO COMPRUEBA, Y LO QUE NO
        # ------------------------------------------
        # Que todo repo con workflows este en la lista de checkout del workflow.
        # El fallo que tapa esto es el de siempre: alguien anade un repo a la
        # suite, el guard lo ve en local porque esta al lado, y en CI no lo ve
        # porque nadie lo anadio a la lista. El job sale en verde midiendo una
        # foto vieja, que es un verde que no dice nada.
        #
        # Lo que NO comprueba es que los SHA sean los de hoy. Eso haria falta
        # red, y un guard que depende de la red es un guard que un dia no corre.
        # La consecuencia es concreta y hay que decirla: un workflow nuevo con un
        # pin en node20 se vera cuando se suba el SHA de su repo aqui, no el dia
        # que se commitee. Eso es assumible. Lo que no lo es creer que este guard
        # vigila el commit de hoy.
        ruta = os.path.abspath(args.pins_de)
        if not os.path.isfile(ruta):
            sys.stderr.write('--pins-de no existe: %s\n' % ruta)
            return 2
        with io.open(ruta, 'r', encoding='utf-8') as fh:
            texto = fh.read()
        clonados = set(re.sub(r'^.*/', '', m.group(1))
                       for m in RE_REPO.finditer(texto))
        # El repo del propio guard se clona SIN linea `repository:`: es el
        # checkout por defecto, el de este mismo repositorio. Sin esto, el guard
        # se denunciaria a si mismo como un repo que no mira.
        clonados.add(os.path.basename(os.path.dirname(here)))
        sin_clonar = sorted(set(r for r, _ in ficheros) - clonados)
        if sin_clonar:
            print('')
            print('REPOS QUE TIENEN WORKFLOWS Y NO ESTAN EN %s (%d):'
                  % (os.path.basename(ruta), len(sin_clonar)))
            for repo in sin_clonar:
                print('  %s' % repo)
            print('')
            print('  El guard los mediria en local, porque estan al lado, y en CI')
            print('  no los mediria: no estan clonados. Ese es un verde que no mira')
            print('  nada. Anadelos al workflow y sube su SHA.')
            return 1
        print('')
        print('pines: los %d repos que tienen workflows estan todos en %s'
              % (len(set(r for r, _ in ficheros)), os.path.basename(ruta)))

    repos = len(set(r for r, _ in ficheros))
    print('Node 20: %d repos, %d ficheros de workflow, %d pines distintos'
          % (repos, len(ficheros), len(pines)))
    for pin in sorted(pines, key=lambda p: p.lower()):
        marca = '  '
        if hallazgos and any(h[3] == pin for h in hallazgos):
            marca = '!!'
        print('  %s %-58s x%d' % (marca, pin, pines[pin]))

    if hallazgos:
        print('')
        print('ACCIONES EN NODE 20 (%d):' % len(hallazgos))
        for repo, fichero, linea, pin, motivo, arreglo in hallazgos:
            print('')
            # `fichero` ya lleva el nombre del repo delante: imprimirlo aqui otra
            # vez salia como ABDEep/ABDEep/.github/..., que es ruido.
            print('  %s:%d' % (fichero, linea))
            print('    %s' % pin)
            print('    %s' % motivo)
            print('    arreglo: %s' % arreglo)
        print('')
        return 1

    print('')
    print('node20 OK: ningun workflow de la suite declara una accion en node20')
    for pin, motivo in sorted(EXENTAS.items()):
        print('  exenta: %s  (%s)' % (pin, motivo))
    if sin_verificar:
        print('')
        print('SIN COMPROBAR (%d), porque el runtime lo dice el action.yml de ESE'
              ' commit y este guard corre sin red:' % len(sin_verificar))
        for pin in sorted(sin_verificar):
            print('  %s x%d' % (pin, sin_verificar[pin]))
    return 0


if __name__ == '__main__':
    sys.exit(main())