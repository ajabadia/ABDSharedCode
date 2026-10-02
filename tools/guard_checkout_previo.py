"""Ningun workflow hace su propio checkout antes de llamar a la accion.

La accion `pnpm-workspace-bootstrap` hace tres checkouts y avisa en su bloque de
uso de que el workflow NO debe hacer el suyo antes: el layout puede quedar con
dos clones en el mismo sitio. Ese aviso es una frase en un YAML, y una frase no
falla. Este guard es lo que la hace cumplir.

POR QUE NO USA `yaml`, Y POR QUE ESO NO ES IRRELEVANTE. Todo lo demas de
`tools/` es stdlib puro, y anadir `pip install pyyaml` a un guard meteria la
primera dependencia de red de la suite en el sitio donde menos se nota: uno que
se ejecuta en el runner, donde si falta el paquete el paso sale con 2 y el
motivo (`falta pyyaml, no se puede medir`) no dice nada de lo que el guard
pretendia vigilar. Medido: asi fallo el primer push con este guard dentro.

El lector de workflows es de aqui, y por eso tiene la regla que no tiene `yaml`:
si el fichero menciona la accion y el lector NO encuentra los pasos, eso es un
error (2), no un "no hay hallazgos". Un parser propio que no entiende una cosa
se limita a no verla, y un guard que no ve no puede decir que esta bien.

LA REGLA, Y POR QUE ES ESTA Y NO UNA MAS FINA. En un job que llama a la accion,
NINGUN `actions/checkout` puede aparecer antes del paso que la llama. No hace
falta distinguir el llamante de los hermanos: la accion tambien los clona a
ellos, asi que cualquier checkout previo es redundante, y redundante aqui
significa "un clon mas en el workspace". MEDIDO en ABDEep (run 37058821215,
windows-2022): el checkout sin `path:` deja el clon del llamante en la RAIZ del
workspace, no en un subdirectorio con el nombre del repo, y al lado de
`pnpm-workspace.yaml` y `node_modules`.

Codigos de salida: 0 nada, 1 hay hallazgos, 2 no se ha podido medir.
"""
import argparse
import glob
import io
import os
import re
import sys

ACCION = 'pnpm-workspace-bootstrap'
CHECKOUT = re.compile(r'^actions/checkout@', re.I)


class Ilegible(Exception):
    """El fichero no se ha podido entender. No es un hallazgo: es un 2."""


def sin_comments(linea):
    """Quita el comentario de una linea, sin tocar lo que va dentro de comillas."""
    dentro = None
    for i, c in enumerate(linea):
        if dentro:
            if c == dentro:
                dentro = None
        elif c in ('"', "'"):
            dentro = c
        elif c == '#' and (i == 0 or linea[i - 1] in ' \t'):
            return linea[:i]
    return linea


def clave(linea):
    """(indentacion, clave, valor) de una linea `clave: valor`, o None."""
    m = re.match(r'^(\s*)([A-Za-z0-9_.\-]+):(.*)$', linea)
    if not m:
        return None
    return len(m.group(1)), m.group(2), m.group(3).strip()


def elemento(lista, i):
    """El bloque que empieza en la lista[i] (`- `) y su indice de fin."""
    sangria = len(lista[i]) - len(lista[i].lstrip())
    j = i + 1
    while j < len(lista):
        s = lista[j]
        if s.strip() and (len(s) - len(s.lstrip())) <= sangria:
            break
        j += 1
    return i + 1, j


def pasos_de(texto):
    """[(job, indice_del_paso, uses, with_)] del workflow, EN ORDEN.

    El recorrido es por SANGRIAS y no por un parser general, porque la forma de
    un workflow de GitHub es fija: `jobs:` en 0, cada job en 2, `steps:` en 4,
    cada paso en 6 con un `- `, y dentro del paso `uses:` y `with:` en 8 con sus
    sub-claves en 10. Escribirse de mas para ese caso es mas corto que un
    parser, y mas facil de verificar: cada numero esta justificado por el
    formato, no por una heuristica.
    """
    lineas = []
    for l in texto.splitlines():
        l = sin_comments(l).rstrip()
        if l.strip():
            lineas.append(l)

    salida = []
    job = None
    i = 0
    while i < len(lineas):
        s = lineas[i]
        sangria = len(s) - len(s.lstrip())

        if sangria == 2 and clave(s) and clave(s)[1] not in ('jobs',):
            # Nombre de job, a dos espacios y con algo despues de los dos puntos.
            k = clave(s)
            if k and k[2] == '':
                job = k[1]
        elif sangria == 4 and s.strip() == 'steps:':
            j = i + 1
            while j < len(lineas):
                s2 = lineas[j]
                s2_ind = len(s2) - len(s2.lstrip())
                if s2_ind <= 4:
                    break
                if s2_ind == 6 and s2.lstrip().startswith('- '):
                    ini, fin = elemento(lineas, j)
                    # La linea del `- ` PARTE del paso. Sin esto se pierde justo
                    # la forma mas corta de escribir un paso (`- uses: ...`), que
                    # no tiene `name:` y por tanto parece no tener contenido: el
                    # guard se quedaba sin ver la mitad de los pasos.
                    cuerpo = [re.sub(r'^(\s*)-\s*', r'\1', lineas[j])] + lineas[ini:fin]
                    uses, with_ = _del_paso(cuerpo)
                    if uses:
                        salida.append((job, len(salida), uses, with_))
                    j = fin
                    continue
                j += 1
            i = j
            continue
        i += 1

    return salida


def _del_paso(lineas):
    """(uses, with_) de las lineas de un paso."""
    uses = ''
    with_ = {}
    dentro = False
    for l in lineas:
        k = clave(l)
        if not k:
            continue
        sangria, nombre, valor = k
        if sangria <= 8 and nombre == 'uses':
            uses = valor.strip('\'"')
        if sangria <= 8 and nombre == 'with':
            dentro = True
            continue
        if dentro and sangria > 8:
            with_[nombre] = valor
        elif sangria <= 8:
            dentro = False
    return uses, with_


def menciona_en_codigo(texto):
    """True si la accion aparece en alguna linea que NO sea comentario.

    No es un detalle. ABDEep, ABDMS2000 y el propio ABDSharedCode la nombran en
    el texto de sus workflows (en comentarios que explican de donde viene el
    layout), y con la comprobacion ingenua `if ACCION not in texto` esos ficheros
    entran al parser y salen como "menciona la accion pero no hay ningun uses:
    que la traiga", que es un 2 y para el job entero. Lo que se busca es si la
    accion se USA, y usarla es que aparezca en un `uses:`.
    """
    for l in texto.splitlines():
        s = sin_comments(l)
        if ACCION in s:
            return True
    return False


def revisa(texto, fichero):
    """Los hallazgos de UN workflow."""
    if not menciona_en_codigo(texto):
        return []
    hay = pasos_de(texto)
    if not any(ACCION in u for _j, _i, u, _w in hay):
        raise Ilegible('%s: menciona la accion pero el lector no ha encontrado '
                       'ningun `uses:` que la traiga. O el YAML usa una forma que '
                       'este lector no entiende, o el guard se esta quedando '
                       'ciego, y en los dos casos no puede decir que este bien.'
                       % fichero)

    hallazgos = []
    for job, indice, uses, with_ in hay:
        if ACCION not in uses:
            continue
        project = (with_.get('project') or '(sin project)').strip('\'"')
        for otro, j, u, w in hay:
            if otro != job or j >= indice:
                continue
            if CHECKOUT.match(u.strip().strip('\'"')):
                # Sin el `uses:` del checkout previo: no lo imprime el informe y
                # sobraba un campo que habia que desempaquetar en tres sitios.
                hallazgos.append((job, j, w, project))
    return hallazgos


def workflows_de(repo):
    d = os.path.join(repo, '.github', 'workflows')
    if not os.path.isdir(d):
        return []
    return sorted(glob.glob(os.path.join(d, '*.yml')) + glob.glob(os.path.join(d, '*.yaml')))


def nombre_repo(repo):
    return os.path.basename(os.path.normpath(repo))


def revisa_suite(raiz):
    """(repos_vistos, hallazgos) de toda la suite.

    Los repos vistos se devuelven aparte porque hay una confusion que este guard
    tiene que poder distinguir y que no puede distinguir callado: cero hallazgos
    y CERO repos mirados. Lo segundo es lo que pasa cuando el `--raiz` esta mal
    calculado y el guard mira el repo propio en vez de la suite: sale 0, dice que
    todo bien, y no ha visto ni un hermano.
    """
    salida = []
    repos = sorted(d for d in glob.glob(os.path.join(raiz, '*'))
                   if os.path.isdir(os.path.join(d, '.git')))
    for repo in repos:
        for wf in workflows_de(repo):
            texto = io.open(wf, encoding='utf-8').read()
            for job, j, uses, w, project in revisa(texto, wf):
                salida.append((nombre_repo(repo), os.path.basename(wf), job, j, w, project))
    return [nombre_repo(r) for r in repos], salida


def describe(w, project):
    partes = ['project: %s' % project]
    if 'path' in w:
        partes.append('path: %s' % w['path'])
    if 'repository' in w:
        partes.append('repository: %s' % w['repository'])
    else:
        partes.append('repository: (ninguno, el llamante)')
    if 'ref' in w:
        partes.append('ref: %s' % w['ref'])
    if 'submodules' in w:
        partes.append('submodules: %s' % w['submodules'])
    return ', '.join(partes)


def informa(hallazgos, salida):
    for repo, wf, job, j, w, project in hallazgos:
        salida.write('%s/%s job %s: paso %d hace su propio checkout ANTES de la accion\n'
                     % (repo, wf, job, j))
        salida.write('    %s\n' % describe(w, project))
        salida.write('    La accion ya clona el llamante y los dos hermanos, y con el\n')
        salida.write('    `project` que se le pasa. Un checkout antes deja un clon mas en el\n')
        salida.write('    workspace; medido en ABDEep, el que va SIN `path:` lo deja en la RAIZ\n')
        salida.write('    del workspace, junto a pnpm-workspace.yaml y node_modules.\n')
        salida.write('    Borra ese paso, o baja el checkout del llamante despues de la accion\n')
        salida.write('    si de verdad hace falta.\n')


def main(argv):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--check', action='store_true',
                   help='busca el fallo y sale 1 si lo hay')
    p.add_argument('--raiz', default=None,
                   help='raiz de la suite. Por defecto, el padre de donde vive este guion')
    p.add_argument('--workflow', default=None,
                   help='un solo workflow, en vez de toda la suite')
    p.add_argument('--listar', action='store_true',
                   help='imprime los hallazgos y sale 0, sin comprobar nada')
    args = p.parse_args(argv)

    try:
        if args.workflow:
            texto = io.open(args.workflow, encoding='utf-8').read()
            try:
                hallazgos = revisa(texto, args.workflow)
            except Ilegible as e:
                print('guard_checkout_previo: %s' % e, file=sys.stderr)
                return 2
            filas = [(nombre_repo(os.path.dirname(os.path.dirname(os.path.dirname(
                os.path.abspath(args.workflow))))), os.path.basename(args.workflow)) + h
                for h in hallazgos]
        else:
            raiz = args.raiz or os.path.dirname(os.path.dirname(
                os.path.dirname(os.path.abspath(__file__))))
            try:
                mirados, filas = revisa_suite(raiz)
            except Ilegible as e:
                print('guard_checkout_previo: %s' % e, file=sys.stderr)
                return 2
            if not mirados:
                print('guard_checkout_previo: en %s no hay ni un repo (nada con '
                      '.git). No se ha medido nada: el --raiz esta mal, y sale 2 '
                      'para que no se confunda con un todo bien.' % raiz,
                      file=sys.stderr)
                return 2
    except OSError as e:
        print('guard_checkout_previo: no se ha podido leer (%s)' % e, file=sys.stderr)
        return 2

    if args.listar:
        for repo, wf, job, j, w, project in filas:
            print('%s/%s %s %d %s' % (repo, wf, job, j, describe(w, project)))
        return 0

    if not args.check:
        p.print_help()
        return 2

    if filas:
        sys.stderr.write('Checkout del llamante antes de la accion:\n\n')
        informa(filas, sys.stderr)
        sys.stderr.write('\n%d hallazgo(s). Exit 1.\n' % len(filas))
        return 1

    print('guard_checkout_previo: ningun job hace su propio checkout antes de llamar a la accion.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
