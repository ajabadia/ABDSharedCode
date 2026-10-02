"""Ningun workflow hace su propio checkout antes de llamar a la accion.

La accion `pnpm-workspace-bootstrap` hace tres checkouts y avisa en su bloque de
uso de que el workflow NO debe hacer el suyo antes: el layout puede quedar con
dos clones en el mismo sitio. Ese aviso es una frase en un YAML, y una frase no
falla. Este guard es lo que la hace cumplir.

POR QUE ESTA EN PYTHON Y NO EN EL FICHERO DE TESTS. El fichero de tests es node
y no tiene parser de YAML: meter `js-yaml` seria meter una cadena de dependencias
en un guard que justamente no tiene ninguna, y el guard acabaria dependiendo de
un `pnpm install` que nadie recuerda hacer. Aqui se usa el `yaml` de python, que
ya estaba ahi, y los tests de este guard se lanzan desde el fichero de node con
`spawnSync`, que es como ya se prueban `guard_pipefail.py` y
`audit_unconsumed_sources.py`.

LA REGLA, Y POR QUE ES ESTA Y NO UNA MAS FINA. En un job que llama a la accion,
NINGUN `actions/checkout` puede aparecer antes del paso que la llama. No hace
falta distinguir el llamante de los hermanos: la accion tambien los clona a
ellos, asi que cualquier checkout previo es redundante, y redundante aqui
significa "un clon mas en el workspace", que es exactamente el fallo que se
busca. Una regla mas fina ("solo si el remoto coincide con el del workflow")
dejaria pasar un checkout del llamante con `path:` distinto, que es el caso que
mas dano hace: MEDIDO, el checkout sin `path:` deja el clon del llamante en la
RAIZ del workspace, no en un subdirectorio con el nombre del repo, y al lado de
`pnpm-workspace.yaml` y `node_modules`.

Códigos de salida: 0 nada, 1 hay hallazgos, 2 no se ha podido medir.
"""
import argparse
import glob
import io
import os
import re
import sys

try:
    import yaml
except ImportError:
    print('guard_checkout_previo: falta pyyaml, no se puede medir', file=sys.stderr)
    sys.exit(2)

ACCION = 'pnpm-workspace-bootstrap'
CHECKOUT = re.compile(r'^actions/checkout@', re.I)
SALIDA_AYUDA = __doc__


def pasos(wf):
    """(job, indice, step) de todos los pasos, incluidos los de nivel workflow."""
    filas = []
    jobs = wf.get('jobs') or {}
    if isinstance(jobs, dict):
        for job, cuerpo in jobs.items():
            if not isinstance(cuerpo, dict):
                continue
            for i, st in enumerate(cuerpo.get('steps') or []):
                if isinstance(st, dict):
                    filas.append((job, i, st))
    for i, st in enumerate(wf.get('steps') or []):
        if isinstance(st, dict):
            filas.append(('<workflow>', i, st))
    return filas


def revisa_texto(fichero, texto):
    """Los hallazgos de UN workflow. Devuelve [(job, indice_previo, step, previo)]."""
    if ACCION not in texto:
        return []
    try:
        wf = yaml.safe_load(texto)
    except Exception as e:
        raise ValueError('%s: YAML ilegible (%s)' % (fichero, e))
    if not isinstance(wf, dict):
        return []

    hallazgos = []
    for job, i, st in pasos(wf):
        if ACCION not in str(st.get('uses') or ''):
            continue
        project = ((st.get('with') or {}).get('project') or '(sin project)')
        for otro_job, j, previo in pasos(wf):
            if otro_job != job or j >= i:
                continue
            if CHECKOUT.match(str(previo.get('uses') or '').strip()):
                hallazgos.append((job, j, previo, project))
    return hallazgos


def workflows_de(repo):
    d = os.path.join(repo, '.github', 'workflows')
    if not os.path.isdir(d):
        return []
    return sorted(glob.glob(os.path.join(d, '*.yml')) + glob.glob(os.path.join(d, '*.yaml')))


def nombre_repo(repo):
    return os.path.basename(os.path.normpath(repo))


def revisa_suite(raiz):
    """(repos_encontrados, hallazgos) de toda la suite.

    Los repos encontrados se devuelven aparte porque hay una confusion que este
    guard tiene que poder distinguir y que no puede distinguir callado: cero
    hallazgos y CERO repos mirados. La segunda es lo que pasa cuando el `--raiz`
    esta mal calculado y el guard mira el repo propio en vez de la suite: sale 0,
    dice que todo bien, y no ha visto ni un hermano.
    """
    salida = []
    repos = sorted(d for d in glob.glob(os.path.join(raiz, '*'))
                   if os.path.isdir(os.path.join(d, '.git')))
    for repo in repos:
        for wf in workflows_de(repo):
            texto = io.open(wf, encoding='utf-8').read()
            for job, j, st, project in revisa_texto(wf, texto):
                salida.append((nombre_repo(repo), os.path.basename(wf), job, j, st, project))
    return [nombre_repo(r) for r in repos], salida


def describe(st, project):
    with_ = st.get('with') or {}
    partes = ['project: %s' % project]
    if 'path' in with_:
        partes.append('path: %s' % with_['path'])
    if 'repository' in with_:
        partes.append('repository: %s' % with_['repository'])
    else:
        partes.append('repository: (ninguno, el llamante)')
    if 'ref' in with_:
        partes.append('ref: %s' % with_['ref'])
    if 'submodules' in with_:
        partes.append('submodules: %s' % with_['submodules'])
    return ', '.join(partes)


def informa(hallazgos, salida):
    for repo, wf, job, j, st, project in hallazgos:
        salida.write('%s/%s job %s: paso %d hace su propio checkout ANTES de la accion\n' % (repo, wf, job, j))
        salida.write('    %s\n' % describe(st, project))
        salida.write('    La accion ya clona el llamante y los dos hermanos, y con el\n')
        salida.write('    `project` que se le pasa. Un checkout antes deja un clon mas en el\n')
        salida.write('    workspace; medido en ABDEep, el que va SIN `path:` lo deja en la RAIZ\n')
        salida.write('    del workspace, junto a pnpm-workspace.yaml y node_modules.\n')
        salida.write('    Borra ese paso, o baja el checkout del llamante despues de la accion\n')
        salida.write('    si de verdad hace falta.\n')


def main(argv):
    p = argparse.ArgumentParser(description=SALIDA_AYUDA,
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
                hallazgos = revisa_texto(args.workflow, texto)
            except ValueError as e:
                print('guard_checkout_previo: %s' % e, file=sys.stderr)
                return 2
            filas = [(nombre_repo(os.path.dirname(os.path.dirname(os.path.dirname(
                os.path.abspath(args.workflow))))), os.path.basename(args.workflow)) + h
                for h in hallazgos]
        else:
            # Tres dirname: este guion -> tools -> ABDSharedCode -> la RAIZ de la
            # suite, que es donde estan los hermanos. Con dos se queda en
            # ABDSharedCode, se pasa el repo en vez de la suite y sale 0 sin
            # haber mirado nada de los hermanos: un verde que no ha mirado.
            raiz = args.raiz or os.path.dirname(os.path.dirname(
                os.path.dirname(os.path.abspath(__file__))))
            mirados, filas = revisa_suite(raiz)
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
        for repo, wf, job, j, st, project in filas:
            print('%s/%s %s %d %s' % (repo, wf, job, j, describe(st, project)))
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
