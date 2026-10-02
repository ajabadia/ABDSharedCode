#!/usr/bin/env python3
"""
QUE LA FOTO DE LA LINEA BASE SEA LA FOTO DEL WORKFLOW.

Este es el cuarto trabajo del CI, y es el que cierra el agujero por el que el
trinquete del audit empieza a MENTIR sin que se note.

El audit mide que fuentes de ABDSharedCode consume ALGUN producto de la suite, y
los productos los clona el workflow fijandolos por SHA. Ese conjunto de SHA es
la FOTO contra la que se mide. La linea base (`tools/audit-baseline.json`)
inventaria las huerfanas que esa foto produce, y por eso lleva una clave `foto`
con el SHA exacto de cada hermano.

Medido, no supuesto: subir el SHA de un hermano en el workflow SIN regenerar la
linea base deja el audit en verde, porque el conjunto de huerfanas no cambia
(sigue siendo 55, los consumidores siguen sin estar commiteados) y el
trinquete solo compara NOMBRES. La linea base sigue diciendo "estas 55" y nadie
puede saber que se movio el suelo. Eso no es que el audit mienta: es que su
veredicto ya no se refiere a lo que el workflow cree que midio.

Y hay un caso peor, que es el que hace este guard necesario de verdad: si el
SHA se sube DESPUES de regenerar la linea base en local. En local los
hermanos tienen el trabajo sin commitear en el disco, asi que `--write-baseline`
alli escribe una foto de huerfanas QUE NO SON LAS DE CI. Commiteada esa linea
base, el audit de CI pasa a medir 55 huerfanas nuevas y falla con un motivo
que no nombra la causa: "hay una fuente sin consumidor". El rojo es correcto y
el diagnostico equivocado, que es la forma mas cara de perder tiempo.

QUE COMPRUEBA, y por que cada cosa:

  1. La linea base tiene clave `foto`, con al menos un repo. Sin ella, el
     trinquete no tiene con que compararse y el rojo del audit no diria nada
     util. Exit 1.
  2. Todo repo de la foto tiene SHA, y ningun `null`. Un null es un repo al
     que no se le pudo preguntar el HEAD: una foto con huecos parece completa.
     Exit 1.
  3. La foto y los SHA del workflow se corresponden REPOSITORIO A REPOSITORIO:
     mismos repos, mismos SHA. Exit 1 en cuanto uno no cuadra.

Lo que NO comprueba, a proposito: que los SHA sean los de master, ni que
existan. Que un SHA apunte a un commit viejo es una decision, no un error; y un
SHA que no existe lo dice `actions/checkout` con un mensaje mucho mejor que uno
que este script pueda inventar. Este guard responde a una sola pregunta: la foto
que midi el audit, es la foto que el workflow va a medir.

Salidas:
  0  la foto de la linea base es la del workflow
  1  no lo es, o no se puede saber. Es un HALLAZGO, no un fallo de ejecucion:
     la respuesta es regenerar la linea base.
  2  mal escrito, o el workflow no se puede leer. No es un hallazgo: es que la
     comprobacion no llego a hacerse, y un 1 aqui seria mentir.

  --listar           imprime `repo sha` de la foto del workflow, una linea por
                    repo, y sale 0. Es lo que consume el paso del workflow que
                    comprueba que esos SHA sigan existiendo en el remoto.

Uso:
  python tools/verificar_foto.py
  python tools/verificar_foto.py --check
  python tools/verificar_foto.py --listar
  python tools/verificar_foto.py --baseline tools/audit-baseline.json
  python tools/verificar_foto.py --workflow .github/workflows/shared-code-ci.yml
"""

import io
import json
import os
import re
import sys
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = os.path.abspath(os.path.join(HERE, '..'))

WORKFLOW_POR_DEFECTO = os.path.join(SHARED, '.github', 'workflows', 'shared-code-ci.yml')
LINEA_BASE_POR_DEFECTO = os.path.join(HERE, 'audit-baseline.json')

# Los checkout del workflow se leen del YAML, no de una lista de aqui. Una lista
# en este fichero seria una SEGUNDA copia de los SHA: se pondria al dia cuando
# alguien se acuerde, que es justo lo que este guard existe para que no haga
# falta. El YAML es la unica fuente, y por eso el workflow no tiene que
# avisar de nada: si cambia un SHA, cambia lo que este script lee.
#
# Se emparejan `repository:` con el `ref:` siguiente porque en un checkout van
# juntos y en ese orden. Con `path:` en vez de `repository:` se emparejaria el
# SHA con la CARPETA, que para ABDSharedCode es su propio checkout (sin `ref:`)
# y para los hermanos coincide con el nombre; es el segundo caso el que
# importa, y `repository:` es el que dice el nombre de verdad.
RE_REPOSITORY = re.compile(r'^\s*repository:\s*[\'"]?([\w.-]+/[\w.-]+)[\'"]?\s*$')

# El workflow dice `repository: ajabadia/ABDEep` y la foto dice `ABDEep`, que es
# como se llama el repo en el disco. Sin normalizar, los diez repos se
# emparejarian en falso y el guard fallaria SIEMPRE, que es como nace un guard
# que nadie lee: la primera contraprueba (estado sano) lo ensezo. Se compara
# por el NOMBRE del repo, no por el `owner/repo` completo, porque la foto se
# midio en un directorio cuyo nombre es el nombre corto.
def _nombre_corto (repo):
    return repo.rsplit('/', 1)[-1] if '/' in repo else repo
RE_REF = re.compile(r'^\s*ref:\s*[\'"]?([0-9a-f]{7,40})[\'"]?\s*$')
RE_SHA = re.compile(r'^[0-9a-f]{7,40}$')


def _parsear (argv):
    """Como el del auditor: sin dependencias, y devolviendo los desconocidos."""
    acc = collections.OrderedDict()
    sueltos = []
    i = 0

    while i < len(argv):
        a = argv[i]

        if a.startswith('--') and '=' not in a and i + 1 < len(argv) \
                and not argv[i + 1].startswith('--'):
            acc.setdefault(a, []).append(argv[i + 1])
            i += 2
            continue

        acc.setdefault(a, []).append(True)
        i += 1

    for a in acc:
        if a not in ('--check', '--listar', '--baseline', '--workflow', '--help', '-h'):
            sueltos.append(a)

    return acc, sueltos


def shas_del_workflow (ruta):
    """
    Los SHA que el workflow clona, como {repo: sha}. Lee el YAML con dos
    regex en vez de con PyYAML a proposito: las herramientas de este repo no
    dependen de nada, y el CI no instala PyYAML en el job de tools. El patron
    que se busca es `repository:` seguido de su `ref:`, y en un checkout de
    actions esos dos campos van juntos y en ese orden.

    Un checkout SIN `ref:` (el de este repo, que usa el commit del propio
    workflow) no cuenta: no es un SHA de la foto, es el sitio desde el que se
    mide. Y el nombre se recorta a la parte que sigue al `owner/`, porque la
    foto se midio sobre directorios que se llaman como el repo corto.
    """
    if not os.path.isfile(ruta):
        return None, 'no existe %s' % ruta

    try:
        txt = io.open(ruta, 'r', encoding='utf-8').read()
    except OSError as exc:
        return None, 'no se pudo leer %s: %s' % (ruta, exc)

    shas = collections.OrderedDict()
    pendiente = None

    for linea in txt.split('\n'):
        m = RE_REPOSITORY.match(linea)
        if m:
            pendiente = _nombre_corto(m.group(1))
            continue

        m = RE_REF.match(linea)
        if m and pendiente:
            shas[pendiente] = m.group(1)
            pendiente = None

    if not shas:
        return None, ('%s no tiene ningun checkout con `repository:` y `ref:`. '
                      'Si el workflow cambio de forma, este guard se esta midiendo '
                      'a si mismo: no encuentra la foto y no puede compararla.' % ruta)

    return shas, None


def foto_de_la_linea_base (ruta):
    """La clave `foto` de la linea base. (diccionario, texto_de_error)."""
    if not os.path.isfile(ruta):
        return None, 'no existe %s' % ruta

    try:
        datos = json.load(io.open(ruta, 'r', encoding='utf-8'))
    except (ValueError, OSError) as exc:
        return None, 'no se pudo leer %s: %s' % (ruta, exc)

    if not isinstance(datos, dict):
        return None, '%s no es un objeto JSON' % ruta

    foto = datos.get('foto')
    if foto is None:
        return None, ('%s no tiene la clave "foto". Sin ella el trinquete del audit '
                      'no sabe contra que SHA se midio, y subir un SHA en el '
                      'workflow no le obliga a regenerar nada.' % ruta)

    if not isinstance(foto, dict) or not foto:
        return None, '%s: la clave "foto" tiene que ser un objeto {proyecto: sha} no vacio' % ruta

    return foto, None


def comparar (foto, shas):
    """
    Las diferencias entre las dos fotos. Devuelve una lista de lineas, vacia si
    son la misma.

    Se comparan en las DOS direcciones y no solo "todo lo del workflow esta en
    la linea base". Un repo que se anade al workflow sin regenerar la linea base
    es un repo cuya foto nadie ha medido, y un repo que se quita del workflow
    dejando su entrada en la linea base es una foto que ya no existe. Los dos
    son el mismo fallo y por eso van en la misma lista.
    """
    diferencias = []

    for repo in sorted(set(foto) | set(shas)):
        en_base = foto.get(repo)
        en_wf = shas.get(repo)

        if repo not in foto:
            diferencias.append('  %-16s el workflow lo clona en %s pero NO esta en la foto'
                               % (repo, en_wf))
        elif repo not in shas:
            diferencias.append('  %-16s esta en la foto (SHA %s) pero el workflow NO lo clona'
                               % (repo, en_base))
        elif not en_base:
            diferencias.append('  %-16s en la foto NO tiene SHA. No se pudo preguntar a git '
                               'por el HEAD de ese repo.' % repo)
        elif not RE_SHA.match(str(en_base)):
            diferencias.append('  %-16s la foto tiene "%s" donde deberia haber un SHA'
                               % (repo, en_base))
        elif en_wf.startswith(en_base) or en_base.startswith(en_wf):
            continue                      # el workflow abrevio el SHA, es el mismo
        else:
            diferencias.append('  %-16s la foto dice %s y el workflow clona %s'
                               % (repo, en_base[:12], en_wf[:12]))

    return diferencias


def main ():
    (args, sueltos) = _parsear(sys.argv[1:])

    desconocidos = list(sueltos)
    if args.get('--baseline') and args['--baseline'][1:]:
        desconocidos += args['--baseline'][1:]
    if args.get('--workflow') and args['--workflow'][1:]:
        desconocidos += args['--workflow'][1:]

    if '--help' in args or '-h' in args:
        print(__doc__.strip())
        return 0

    if desconocidos:
        print('verificar_foto: argumento desconocido: %s' % desconocidos[0], file=sys.stderr)
        print('  --check | --listar | --baseline <fichero> | --workflow <fichero> | --help',
              file=sys.stderr)
        return 2

    # `--baseline` sin valor daba IndexError antes de mirar nada. Aqui el
    # sintoma seria peor: `[1]` sobre `True` y el script se cae con un error de
    # Python que en un job se lee como "se ha roto Python", no como "falta el
    # argumento".
    if '--baseline' in args:
        valor = args['--baseline'][-1]
        if valor is True:
            print('verificar_foto: --baseline necesita el fichero que sigue', file=sys.stderr)
            return 2
        ruta_base = valor
    else:
        ruta_base = LINEA_BASE_POR_DEFECTO

    if '--workflow' in args:
        valor = args['--workflow'][-1]
        if valor is True:
            print('verificar_foto: --workflow necesita el fichero que sigue', file=sys.stderr)
            return 2
        ruta_wf = valor
    else:
        ruta_wf = WORKFLOW_POR_DEFECTO

    shas, error_wf = shas_del_workflow(ruta_wf)
    if error_wf:
        print('verificar_foto: no se pudo leer la foto del workflow.', file=sys.stderr)
        print('  %s' % error_wf, file=sys.stderr)
        print('  Esto NO es un hallazgo: es que la comprobacion no llego a hacerse.', file=sys.stderr)
        return 2

    # `--listar` imprime en stdout y sale 0 sin comparar nada. Lo consume el paso
    # del workflow que avisa si un SHA dejo de existir en el remoto, que es una
    # comprobacion distinta de esta: aqui la foto es "la que el workflow clona",
    # alli es "ese commit sigue existiendo". Que el listado salga de ESTE
    # script y no de un parser propio en el `run:` es lo que evita que las dos
    # vistas de "los SHA del workflow" se separen en silencio.
    if '--listar' in args:
        for repo in sorted(shas):
            print('%s %s' % (repo, shas[repo]))
        return 0

    foto, error_base = foto_de_la_linea_base(ruta_base)
    if error_base:
        print('verificar_foto: la linea base no declara contra que foto se midio.',
              file=sys.stderr)
        print('  %s' % error_base, file=sys.stderr)
        print('  Arreglo: en un clon con los hermanos en los SHA del workflow,', file=sys.stderr)
        print('          python tools/audit_unconsumed_sources.py --write-baseline',
              file=sys.stderr)
        return 1

    diferencias = comparar(foto, shas)

    if not diferencias:
        if '--check' not in args:
            print('verificar_foto: la foto de la linea base es la del workflow (%d repos).'
                  % len(shas))
            for repo in sorted(shas):
                print('  %-16s %s' % (repo, shas[repo][:12]))
        return 0

    print('verificar_foto: LA FOTO DE LA LINEA BASE NO ES LA DEL WORKFLOW.', file=sys.stderr)
    print('', file=sys.stderr)
    for d in diferencias:
        print(d, file=sys.stderr)
    print('', file=sys.stderr)
    print('  Que significa: la linea base se midio contra una foto que ya no es la que', file=sys.stderr)
    print('  el workflow clona. El audit puede estar en verde y aun asi no estar', file=sys.stderr)
    print('  diciendo nada sobre lo que CI va a medir.', file=sys.stderr)
    print('', file=sys.stderr)
    print('  O lo que ha pasado es al reves, y es peor: la linea base se regenero en', file=sys.stderr)
    print('  LOCAL, donde los hermanos tienen el trabajo sin commitear, y lo que se', file=sys.stderr)
    print('  commiteo no son las huerfanas de CI.', file=sys.stderr)
    print('', file=sys.stderr)
    print('  Arreglo, y el orden importa:', file=sys.stderr)
    print('    1. deja cada hermano en el SHA que pone el workflow', file=sys.stderr)
    print('       (en local: git -C <repo> checkout <sha>)', file=sys.stderr)
    print('    2. python tools/audit_unconsumed_sources.py --write-baseline', file=sys.stderr)
    print('    3. commitea tools/audit-baseline.json CON EL WORKFLOW YA SUBIDO', file=sys.stderr)
    print('', file=sys.stderr)
    return 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:                                    # noqa: BLE001
        print('error: %s' % exc, file=sys.stderr)
        sys.exit(2)