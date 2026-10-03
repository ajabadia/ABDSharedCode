"""Vuelve a medir los casos de `actions/checkout` y falla si el comportamiento
cambio respecto a la tabla de `tools/checkout-medido.json`.

POR QUE HAY QUE MEDIR Y NO LEER. `actions/checkout` puede cambiar de
comportamiento al subir de version sin que nada en este repo se entere: la
regla ("dos checkouts con el mismo `path:` se limpian entre si si y solo si
apuntan a repos distintos") esta en el `action.yml` de la accion y en un
README, no en codigo que se ejecute. El test de `guard_atributos.test.mjs`
falla cuando el repo usa una version que no esta en `versiones_medidas`, que
es el aviso de "vuelve a medir". Este script es la vuelta a medir.

COMO SE MEDE, Y POR QUE UN CENTINELA. El unico modo fiable de saber si el
segundo checkout borro el primero es comprobar si el primero sigue ahi
DESPUES del segundo. Antes no se puede: en ese momento los dos contents son
indistinguibles. Asi que el primer checkout deja dentro del directorio un
fichero centinela, y la pregunta es una sola y binaria: sigue existiendo.
`git log`, el tamano del arbol y el mtime son todosophores que dependen del
ref, del tamano del repo y de la cache; el centinela no depende de nada.

LA TABLA NO SE EDITA AQUI. Este script solo la lee. Cambiar la tabla es un acto
deliberado de alguien que ha medido a mano y sabe que el comportamiento nuevo
es el bueno; si esto lo hiciera solo, un cambio de comportamiento quedaria
aceptado por el mismo mecanismo que deberia detectarlo.

Codigos de salida: 0 todo igual, 1 el comportamiento cambio, 2 no se ha podido
medir. Igual que `guard_checkout_previo.py`, y por el mismo motivo: un guard
que no puede medir no puede decir que esta bien.
"""
import argparse
import io
import json
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
TABLA = os.path.join(AQUI, 'checkout-medido.json')
README = os.path.join(
    AQUI, '..', '.github', 'actions', 'pnpm-workspace-bootstrap', 'README.md')
ABRE = '<!-- GENERADO-DESDE-JSON: no editar a mano -->'
CIERRA = '<!-- /GENERADO-DESDE-JSON -->'

# DONDE QUEDA EL PRIMER CLON DE CADA CASO. Vive AQUI y no en el YAML porque el
# centinela tiene que escribirse ENTRE los dos checkouts, y el segundo checkout
# con el mismo `path:` borra el directorio. Si esta ruta se计算出 mal, el
# centinela acaba de escribir despues del segundo y la medicion dice siempre
# "no borra". MEDIDO: asi fallo la primera corrida, con los doce jobs en verde
# y la comparacion diciendo que A no borra cuando la tabla dice que si.
SEGUNDO_CLON = {
    'A': 'dup',   # mismo path, repos distintos: el segundo borra
    'B': 'uno',   # paths distintos: `dup` no se toca, el sentinel va en `uno`
    'C': 'dup',   # mismo path, mismo repo, mismo ref
    'D': 'dup',   # mismo path, mismo repo, ref distinto
    'E': 'dup',   # mismo path, el llamante y luego el propio llamante
    'F': '.',     # el llamante sin `path:` cae en la raiz del workspace
}
SENTINELA = '.medido-sentinel'


class NoMedible(Exception):
    """No se ha podido medir. No es un cambio de comportamiento: es un 2."""


def cargar_tabla():
    if not os.path.exists(TABLA):
        raise NoMedible('no esta %s' % TABLA)
    with io.open(TABLA, encoding='utf-8') as f:
        return json.load(f)


def plan():
    """Lo que hay que medir: los casos de la tabla y las versiones medidas."""
    tabla = cargar_tabla()
    casos = tabla.get('casos')
    versiones = tabla.get('versiones_medidas')
    if not casos or not versiones:
        raise NoMedible('la tabla no trae `casos` y `versiones_medidas`')
    return {
        'casos': [{'caso': c['caso'], 'borra': c['borra']} for c in casos],
        'versiones': versiones,
    }


def centinela(caso, workspace):
    """Deja el centinela donde quedo el PRIMER clon. Entre los dos checkouts."""
    if caso not in SEGUNDO_CLON:
        raise NoMedible('caso desconocido: %s' % caso)
    ruta = os.path.join(workspace, SEGUNDO_CLON[caso], SENTINELA)
    if not os.path.isdir(os.path.dirname(ruta)):
        raise NoMedible('el primer clon no esta donde se esperaba: %s' % os.path.dirname(ruta))
    with io.open(ruta, 'w', encoding='utf-8') as f:
        f.write('medido %s\n' % caso)
    return ruta


def ruta_sentinela(caso, workspace):
    if caso not in SEGUNDO_CLON:
        raise NoMedible('caso desconocido: %s' % caso)
    return os.path.join(workspace, SEGUNDO_CLON[caso], SENTINELA)


def observa(sentinela):
    """El centinela sigue o no sigue. De ahi sale el `borra` del caso."""
    if not os.path.isabs(sentinela):
        raise NoMedible('la ruta del centinela tiene que ser absoluta: %s' % sentinela)
    if not os.path.isdir(os.path.dirname(sentinela)):
        raise NoMedible('el directorio del centinela no existe: %s' % os.path.dirname(sentinela))
    return {'borra': not os.path.exists(sentinela)}


def compara(observaciones, esperado):
    """`observaciones` es lo medido; `esperado` es la tabla. Devuelve una lista
    de diferencias legibles. Vacia significa que no cambio nada."""
    difs = []
    # La tabla se llama a si misma `versiones_medidas`; `versiones` es el nombre
    # que le pone `plan()`. Aki se acepta cualquiera de los dos, y si no esta
    # ninguno se avisa en vez de comparar contra una lista vacia que haria pasar
    # cualquier version sin medir.
    versiones = esperado.get('versiones_medidas') or esperado.get('versiones')
    if not versiones:
        raise NoMedible('la tabla no trae `versiones_medidas`')
    casos = esperado.get('casos')
    if not casos:
        raise NoMedible('la tabla no trae `casos`')
    por_caso = {o['caso']: o for o in observaciones if o.get('caso')}
    for caso in casos:
        visto = por_caso.get(caso['caso'])
        if visto is None:
            difs.append('%s: no se midio' % caso['caso'])
            continue
        if 'borra' not in visto:
            difs.append('%s: la medicion no trae `borra`' % caso['caso'])
            continue
        if visto['borra'] != caso['borra']:
            difs.append('%s: la tabla dice borra=%s y se midio borra=%s'
                        % (caso['caso'], str(caso['borra']).lower(), str(visto['borra']).lower()))
        if visto.get('version') and visto['version'] not in versiones:
            difs.append('%s: se midio con %s, que no esta en `versiones_medidas`'
                        % (caso['caso'], visto['version']))
    medidos = set(v.get('version') for v in observaciones if v.get('version'))
    sin_medir = sorted(m for m in medidos if m not in versiones)
    if sin_medir:
        difs.append('versiones medidas que no estan en la tabla: %s' % ', '.join(sin_medir))
    return difs


def tabla_readme(tabla):
    """La tabla del README, desde el JSON. Es la UNICA copia."""
    filas = ['| caso | `path:` | repo | resultado medido |',
             '|---|---|---|---|']
    for c in tabla.get('casos') or []:
        # Senfueza el resultado SOLO cuando borra, que es el caso unico con
        # consecuencias. El enfasis sale del dato, no de una decision de redaccion.
        res = c['resultado']
        if c.get('borra'):
            res = '**%s**' % res
        filas.append('| %s | %s | %s | %s |' % (
            c['caso'], c.get('path_wf', ''), c['repos'], res))
    return '\n'.join(filas)


def readme():
    """Reescribe el bloque generado del README desde el JSON.

    POR QUE GENERAR Y NO COMPARAR. El README y el JSON contaban la misma tabla
    dos veces, y un test comparaba las dos copias. Eso convierte cada cambio de
    la tabla en dos cambios manuales que hay que recordar hacer juntos, y el
    test solo avisa cuando alguien ya ha cometido el forget: para entonces el
    rojo es information, no prevencion. Generando, el README no se desincroniza
    nunca; lo unico que puede desincronizarse es el JSON, que es la fuente.
    """
    if not os.path.exists(README):
        raise NoMedible('no esta el README de la accion: %s' % README)
    with io.open(README, encoding='utf-8', newline='') as f:
        texto = f.read()
    if ABRE not in texto or CIERRA not in texto:
        raise NoMedible('el README no tiene los marcadores %s / %s' % (ABRE, CIERRA))
    cuerpo = '%s\n%s\n%s' % (ABRE, tabla_readme(cargar_tabla()), CIERRA)
    nuevo = re.sub(re.escape(ABRE) + '.*?' + re.escape(CIERRA), lambda _m: cuerpo,
                   texto, flags=re.S)
    return texto, nuevo


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest='cmd')
    pl = sub.add_parser('plan')
    pl.add_argument('--github-output', action='store_true',
                    help='imprime `clave=[...]` en el formato de $GITHUB_OUTPUT')
    o = sub.add_parser('observa')
    o.add_argument('--sentinela')
    o.add_argument('--workspace', help='raiz del workspace; el path sale del caso')
    o.add_argument('--caso', default='')
    o.add_argument('--version', default='')
    cen = sub.add_parser('centinela')
    cen.add_argument('--caso', required=True)
    cen.add_argument('--workspace', required=True)
    c = sub.add_parser('compara')
    c.add_argument('--observaciones', required=True)
    r = sub.add_parser('readme')
    r.add_argument('--check', action='store_true',
                   help='no escribe: sale con 1 si el README no es lo que se generaria')
    args = p.parse_args(argv)

    try:
        if args.cmd == 'plan':
            datos = plan()
            if args.github_output:
                # El formato de $GITHUB_OUTPUT es `clave=valor` y el valor puede
                # ser JSON, que es lo que `fromJSON` se come. Se imprimen dos
                # claves porque el YAML no sabe convertir un mapa en una
                # matriz por si solo.
                print('casos=%s' % json.dumps([c['caso'] for c in datos['casos']]))
                print('versiones=%s' % json.dumps(datos['versiones']))
            else:
                print(json.dumps(datos, ensure_ascii=False))
            return 0

        if args.cmd == 'centinela':
            print(centinela(args.caso, args.workspace))
            return 0

        if args.cmd == 'observa':
            centinela_ = args.sentinela
            if not centinela_:
                if not args.workspace:
                    raise NoMedible('observa necesita --sentinela o --workspace')
                centinela_ = ruta_sentinela(args.caso, args.workspace)
            r = observa(centinela_)
            r['caso'] = args.caso
            r['version'] = args.version
            print(json.dumps(r, ensure_ascii=False))
            return 0

        if args.cmd == 'readme':
            texto, nuevo = readme()
            if args.check:
                if texto != nuevo:
                    sys.stderr.write(
                        'el README de la accion NO es lo que se genera desde '
                        'checkout-medido.json\n')
                    sys.stderr.write('corrige con: python3 tools/medir_checkout.py readme\n')
                    return 1
                print('el README esta al dia con checkout-medido.json')
                return 0
            if texto == nuevo:
                print('el README ya estaba al dia')
                return 0
            with io.open(README, 'w', encoding='utf-8', newline='') as f:
                f.write(nuevo)
            print('README regenerado desde checkout-medido.json')
            return 0

        if args.cmd == 'compara':
            # Acepta un fichero o un DIRECTORIO. El directorio es lo que usa el
            # job que junta los artefactos de la matriz: cada job deja un JSON y
            # este los lee todos, en vez de que el workflow tenga que montar el
            # array a mano con un one-liner que nadie va a leer dentro de tres
            # meses.
            try:
                if os.path.isdir(args.observaciones):
                    # `os.walk` y no `listdir`: `actions/download-artifact` deja
                    # un subdirectorio por artefacto, y un listdir de una
                    # sola profundidad no encontraria ningun JSON.
                    observaciones = []
                    for raiz, _dirs, ficheros in os.walk(args.observaciones):
                        for nombre in sorted(ficheros):
                            if not nombre.endswith('.json'):
                                continue
                            with io.open(os.path.join(raiz, nombre), encoding='utf-8') as f:
                                pegado = json.load(f)
                            if isinstance(pegado, list):
                                observaciones.extend(pegado)
                            else:
                                observaciones.append(pegado)
                else:
                    with io.open(args.observaciones, encoding='utf-8') as f:
                        observaciones = json.load(f)
            except (IOError, OSError, ValueError) as e:
                # Un fichero que no se puede leer NO es un cambio de
                # comportamiento. Si aqui saliera con 1, un artefacto que no
                # llego se leeria como "el checkout cambio", que es justo la
                # mentira que este repo no quiere en un guard.
                raise NoMedible('no se pudo leer %s: %s' % (args.observaciones, e))
            if isinstance(observaciones, dict):
                observaciones = [observaciones]
            if not isinstance(observaciones, list) or not observaciones:
                raise NoMedible('no hay observaciones que comparar')
            difs = compara(observaciones, cargar_tabla())
            if difs:
                sys.stderr.write(
                    'EL COMPORTAMIENTO DE actions/checkout CAMBIO\n\n')
                for d in difs:
                    sys.stderr.write('  %s\n' % d)
                sys.stderr.write(
                    '\nEsto NO se arregla cambiando la tabla: o el cambio es\n'
                    'del bueno y hay que volver a medir a mano y escribir por que,\n'
                    'o es un cambio de comportamiento al que hay que adaptarse.\n')
                return 1
            print('sin cambios: %d mediciones coinciden con la tabla' % len(observaciones))
            return 0

        p.print_help()
        return 2
    except NoMedible as e:
        sys.stderr.write('no se ha podido medir: %s\n' % e)
        return 2


if __name__ == '__main__':
    sys.exit(main())