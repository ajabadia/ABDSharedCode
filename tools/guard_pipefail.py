#!/usr/bin/env python3
"""
QUE NINGUN TRABAJO DE LA SUITE SE PONGA EN VERDE PORQUE UN PIPE SE COMIO EL CODIGO.

Quinto trabajo del CI, y el mas pequeno de todos: no compila nada y no mide nada.
Lee los `.github/workflows/*.yml` de la suite y falla si alguno mete un `tee` en una
tuberia sin que el shell de ese paso pueda notar que lo de la izquierda fallo.

EL FALLO QUE CIERRA, MEDIDO. En el primer push de este repo, el paso de tests de
MidiKeyboard salio VERDE con `pnpm test` fallando. Reproducido en local:

    sin pipefail: exit=0
    con pipefail: exit=1

y con el caso real, un `pnpm test` sin manifiesto (que es lo que paso):

    sin pipefail: exit=0     <- el job en verde
    con pipefail: exit=1

La razon: `cmd | tee log` devuelve el codigo de `tee`, que es 0 siempre. Ese dia lo
detectamos de casualidad, leyendo el log. Este guard es para que no dependa de que
alguien lea el log.

LO QUE AFIRMA, Y DE DONDE SALE. Afirma UNA cosa: "este paso no puede perder un codigo
de salida por un pipe". Se comprueba leyendo el YAML y las reglas del runner, y las
reglas del runner son CODIGO, no documentacion. De
`src/Runner.Worker/Handlers/ScriptHandlerHelpers.cs`:

    ["bash"] = "--noprofile --norc -e -o pipefail {0}",
    ["sh"]   = "-e {0}",
    ["pwsh"] = "-command \". '{0}'\"",

y para pwsh/powershell, `FixUpScriptContents` ANADE al final del script:

    if ((Test-Path -LiteralPath variable:\\LASTEXITCODE)) { exit $LASTEXITCODE }

De ahi salen las tres reglas del guard:

  1. `shell: bash` explicito YA LLEVA `-o pipefail`. Asi que un `| tee` debajo de un
     `shell: bash` NO es hallazgo. El commit `4f5e6a4` arreglo el paso de tests
     poniendo las dos cosas, y un guard que se pusiera rojo ahi estaria marcando como
     roto lo que acabamos de arreglar.
  2. Sin `shell:` en Linux el runner usa `sh`, y `sh` es `-e {0}`: SIN pipefail. Ese
     es el agujero de verdad, y el que produjo el verde falso.
  3. En pwsh el pipefail no existe como concepto, pero el runner anade
     `exit $LASTEXITCODE`, asi que el codigo de salida SALE. Por eso un `| tee` en
     pwsh se cuenta aparte y no es fallo. No por prudencia: porque el runner lo
     resuelve, y un guard que marca mas de lo que la CI exige es un guard que hay
     que apagar.

QUE SE LEE Y DE DONDE SALE LA LISTA DE REPOS.

`--raiz` mira los subdirectorios INMEDIATOS de un directorio y, de cada uno, su
`.github/workflows/`. El layout de la suite es justo eso: un directorio con un clon
por repo. El mismo script sirve para el arbol de trabajo local y para el job, sin
una lista de repos escrita a mano: la sale de `verificar_foto.py --listar`, que a su
vez la lee del workflow. Si alguien anade un repo a la suite, la lista lo sigue sin
que nadie actualice un script.

LIMITE CONOCIDO, ACEPTADO A PROPOSITO: no se comprueba que el `tee` de un `run:` sea de
bash. Un `echo "instala con | tee log"` dentro de un run se cuela como hallazgo. Se
acepta porque el arreglo esarreglar un comentario, y un guard que se apaga con un
comentario sigue siendo un guard.

Salidas:
  0  ningun trabajo de la suite puede perder un codigo de salida por un pipe
  1  hay al menos uno que puede. Es un HALLAZGO, no un fallo de ejecucion
  2  no se pudo comprobar (no hay workflows que mirar). Un 0 aqui seria el verde mas
     peligroso de todos: el guard no habria medido nada

  --check    no imprime el informe sano, solo el veredicto
  --listar   imprime los repos, los pasos mirados y los riesgos, y sale 0. Es lo que
             contesta "el guard estaba en verde porque no miraba nada"
  --raiz DIR la raiz de la suite (por defecto, el padre de este repo)

Uso:
  python tools/guard_pipefail.py --check
  python tools/guard_pipefail.py --listar
  python tools/guard_pipefail.py --raiz /ruta/a/ABDSynths
"""

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = os.path.abspath(os.path.join(HERE, '..'))

# El padre de este repo es la raiz de la suite en el arbol de trabajo. En CI el job
# pasa `--raiz` con el workspace, que tiene el mismo layout: un directorio por repo.
RAIZ_POR_DEFECTO = os.path.abspath(os.path.join(SHARED, '..'))

EXTENSIONES = ('.yml', '.yaml')

# Un `tee` SOLO cuenta si es un comando de una tuberia: al principio de la linea o
# justo detras de `|`, `;`, `&` o `(`. La palabra "puppeteer" CONTIENE "tee" y sale en
# los workflows de la suite (el Chromium, ABD_SKIP_PUPPETEER), asi que un `grep tee`
# sin mas marca como sospechoso un fichero sano. Medido: `grep -n tee
# ABDEep/.github/workflows/webui-ci.yml` casa en la linea 53, dentro de la palabra
# "Chromium". Con este patron esa linea no cuenta.
RE_TEE_EN_PIPE = re.compile(r'(?:\A|[|;&(]\s*)(?:[\w./-]+/)?tee(?=\s|\Z)')

# `set` con pipefail entre sus opciones: `set -o pipefail`, `set -euo pipefail`,
# `set -uo pipefail`, `set -o errexit -o pipefail`. Se busca la palabra dentro de una
# invocacion de `set`, no un `pipefail` cualquiera.
RE_SET_PIPEFAIL = re.compile(r'(?:\A|[;&]\s*|\|\|\s*|\n)\s*set\s+-[^\n]*\bpipefail\b')

# El `shell:` de un paso, de un `defaults: run:` de job o de workflow. Se captura el
# valor ENTERO porque `bash` y `bash -e {0}` no significan lo mismo: el primero hereda
# `-o pipefail` del runner, el segundo lo ha pisado.
RE_SHELL = re.compile(r'^\s*shell:\s*(.+?)\s*$')

# Una clave de bloque (`run:`, `shell:`) con su sangria, para saber de quien es cada
# clave: lo mas indentado es el cuerpo de un bloque, no otra clave.
RE_CLAVE = re.compile(r'^(\s*)([A-Za-z_][\w.-]*):(.*)$')


def _sin_comentarios (lineas):
    """
    Las lineas del cuerpo de un `run:` que NO son comentarios.

    Sin esto el propio workflow de este repo seria un hallazgo: el comentario que
    explica el verde falso dice `` `pnpm test | tee log` devuelve ``, que es justo lo
    que el guard busca. Un guard que se dispara con su propia documentacion se
    corrige borrando la documentacion, que es lo contrario de lo que se quiere.
    """
    return [l for l in lineas if not l.lstrip().startswith('#')]


def _cuerpo_de_bloque (lineas, i, sangria_clave):
    """
    El cuerpo del bloque escalar que empieza en `i`. (lineas, siguiente_indice).

    Un bloque YAML es todo lo que esta mas indentado que su clave. No se mira el `|`
    ni el `>`: se toma lo que venga, que es lo que GitHub ejecuta.
    """
    cuerpo = []
    j = i + 1
    while j < len(lineas):
        l = lineas[j]
        if not l.strip():
            cuerpo.append(l)
            j += 1
            continue
        if len(l) - len(l.lstrip()) <= sangria_clave:
            break
        cuerpo.append(l)
        j += 1

    while cuerpo and not cuerpo[-1].strip():
        cuerpo.pop()

    return cuerpo, j


def _shell_de_una_clave (lineas, i, sangria_clave):
    """El `shell:` que cuelga de la clave de `lineas[i]`, o None.

    Se buscan dos niveles porque la forma que usan los repos de la suite es
    `defaults:` -> `run:` -> `shell:`.
    """
    cuerpo, j = _cuerpo_de_bloque(lineas, i, sangria_clave)

    for k in range(i + 1, j):
        m = RE_SHELL.match(lineas[k])
        if m and len(lineas[k]) - len(lineas[k].lstrip()) > sangria_clave:
            return m.group(1).strip()

    return None


def _jobs (lineas):
    """
    Los jobs del workflow, cada uno con su sangria, su `hasta` y su shell por defecto.

    Se localiza `jobs:` y se toman las claves que cuelgan de el. No hace falta mas:
    lo que se busca es un `run:` con un `tee` y el shell que lo ejecuta.
    """
    inicio = None
    for i, l in enumerate(lineas):
        if re.match(r'^jobs:\s*$', l):
            inicio = i + 1
            break

    if inicio is None:
        return []

    jobs = []
    for i in range(inicio, len(lineas)):
        l = lineas[i]
        if not l.strip():
            continue
        m = RE_CLAVE.match(l)
        if m and len(m.group(1)) == 2:
            jobs.append({'nombre': m.group(2), 'desde': i, 'sangria': len(m.group(1))})

    for k, job in enumerate(jobs):
        job['hasta'] = jobs[k + 1]['desde'] if k + 1 < len(jobs) else len(lineas)
        # La sangria de las CLAVES del job es la de su primera clave, que no tiene por
        # que ser la del nombre del job mas dos. Medido en ABDEep/webui-bundle-ci.yml:
        # el job `bundle-in-binary` esta en la sangria 2 y su `runs-on:` en la 4, y
        # buscar por la sangria del nombre no encontraba ni el `runs-on:` ni el
        # `defaults:`. Con eso, un `| tee` de pwsh en un job de Windows se tomaba por
        # un `sh` sin pipefail, que es justo el falso positivo que este guard no puede
        # permitirse en su primer dia de vida.
        job['sangria_claves'] = _sangria_de_las_claves(lineas, job)
        job['shell_default'] = _shell_default_del_job(lineas, job)
        job['runner'] = _runner_del_job(lineas, job)

    return jobs


def _sangria_de_las_claves (lineas, job):
    """La sangria de las claves del job: la de su primera clave de nivel superior."""
    for i in range(job['desde'] + 1, job['hasta']):
        l = lineas[i]
        if not l.strip():
            continue
        sangria = len(l) - len(l.lstrip())
        if sangria <= job['sangria']:
            continue
        m = RE_CLAVE.match(l)
        if m:
            return sangria
        # Una linea sin clave a ese nivel es un comentario: no dice nada de la forma.
    return job['sangria'] + 2


def _runner_del_job (lineas, job):
    """El `runs-on:` de un job, en minusculas, o '' si no lo dice."""
    for i in range(job['desde'], job['hasta']):
        m = re.match(r'^\s*runs-on:\s*(.+?)\s*$', lineas[i])
        if m and len(lineas[i]) - len(lineas[i].lstrip()) == job['sangria_claves']:
            return m.group(1).strip().lower()
    return ''


def _shell_default_del_job (lineas, job):
    """El `defaults: run: shell:` de un job, si lo tiene."""
    for i in range(job['desde'], job['hasta']):
        m = re.match(r'^(\s*)defaults:\s*$', lineas[i])
        if m and len(m.group(1)) == job['sangria_claves']:
            shell = _shell_de_una_clave(lineas, i, job['sangria_claves'])
            if shell:
                return shell
    return None


def _shell_default_del_workflow (lineas):
    """El `defaults: run: shell:` de primer nivel, si existe."""
    for i, l in enumerate(lineas):
        if re.match(r'^defaults:\s*$', l):
            shell = _shell_de_una_clave(lineas, i, 0)
            if shell:
                return shell
    return None


def _inicio_del_paso (lineas, i, sangria_run):
    """
    La linea en la que empieza el paso al que pertenece el `run:` de `lineas[i]`.

    Un paso es un elemento de la lista `steps:`, asi que empieza en el `- ` que esta
    en la sangria de la lista, que es DOS MENOS que la de sus claves: `run:` cuelga de
    ahi, no al reves.

    Esto no es cosmetico, es lo que hace que el guard vea la realidad. Buscar el
    `shell:` hacia atras sin limite hacia que un paso SIN `shell:` herede el del paso
    ANTERIOR, que esta en la misma sangria. Medido: con esa busqueda, el workflow de
    este repo con el arreglo de `4f5e6a4` quitado seguia saliendo en verde, porque el
    paso de tests tomaba el `shell: bash` del paso de al lado. Un guard que se aprueba
    a si mismo con un bug asi no vigila nada.
    """
    sangria_elemento = sangria_run - 2

    for k in range(i - 1, -1, -1):
        l = lineas[k]
        if not l.strip():
            continue
        sangria = len(l) - len(l.lstrip())
        if sangria < sangria_elemento:
            break
        if sangria == sangria_elemento and l.lstrip().startswith('-'):
            return k

    return i


def _nombre_del_paso (lineas, inicio, sangria_run):
    """
    El `name:` del paso que empieza en `lineas[inicio]`.

    El `name:` va en la misma linea que el `- `, asi que se mira desde `inicio` hacia
    DELANTE. Antes salia `(sin nombre)` en todos los pasos: la busqueda comparaba la
    sangria del `name:` con la del `run:`, y el `name:` esta dos columnas a la
    izquierda, asi que nunca casaba.
    """
    m = re.match(r'^\s*-?\s*name:\s*(.+?)\s*$', lineas[inicio])
    if m:
        return m.group(1).strip().strip('\'"')

    sangria = sangria_run - 2
    for k in range(inicio + 1, len(lineas)):
        l = lineas[k]
        if not l.strip():
            continue
        sangria_k = len(l) - len(l.lstrip())
        if sangria_k < sangria:
            break
        nm = re.match(r'^\s*name:\s*(.+?)\s*$', l)
        if nm and sangria_k == sangria:
            return nm.group(1).strip().strip('\'"')

    return '(sin nombre)'


def pasos (lineas, job, shell_workflow):
    """
    Los pasos de un job que ejecutan codigo. (lista de diccionarios)

    El shell de un paso es, en este orden: el suyo, el `defaults: run: shell:` de su
    job, el del workflow, y None (que lo resuelve el runner). La precedencia es la de
    GitHub y no es una suposicion: si un paso dice `shell:` gana, y un `defaults:` solo
    habla de los pasos que NO digan nada.
    """
    salida = []

    for i in range(job['desde'], job['hasta']):
        m = re.match(r'^(\s*)run:\s*(.*?)\s*$', lineas[i])
        if not m:
            continue

        sangria = len(m.group(1))
        inicio = _inicio_del_paso(lineas, i, sangria)

        # El `shell:` se busca SOLO dentro de este paso, desde su `- ` hasta su `run:`.
        # Ver `_inicio_del_paso`: hacia atras sin limite el shell se contaminaba
        # del paso anterior.
        shell_paso = None
        for k in range(inicio, i):
            sm = RE_SHELL.match(lineas[k])
            if sm and len(lineas[k]) - len(lineas[k].lstrip()) == sangria:
                shell_paso = sm.group(1).strip()

        if m.group(2) and not m.group(2).startswith(('#', '|', '>')):
            cuerpo = [m.group(2)]          # `run: pnpm test | tee log`, en una linea
        else:
            cuerpo, _ = _cuerpo_de_bloque(lineas, i, sangria)

        salida.append({
            'nombre': _nombre_del_paso(lineas, inicio, sangria),
            'shell': shell_paso or job['shell_default'] or shell_workflow,
            'runner': job['runner'],
            'cuerpo': cuerpo,
            'linea': i + 1,
        })

    return salida


def _clase_del_shell (shell, runner=''):
    """
    Que clase de shell ejecuta el paso: 'bash', 'sh', 'pwsh' o 'cmd'.

    SIN `shell:` declarado, lo que decide es el runner, y por eso hace falta el
    `runs-on:` del job. No es un detalle: el runner usa `pwsh` en Windows y `sh` en
    Linux y macOS, y del `runs-on:` depende si el codigo de salida se pierde o no.

    Medido en la suite, y por eso no se puede asumir `sh` a secas: el paso "Configure
    CMake" de ABDEep/webui-bundle-ci.yml hace `cmake ... | tee log` sin declarar shell,
    pero su job es `runs-on: windows-2022`, donde el runner ejecuta pwsh y le anade
    `exit $LASTEXITCODE`. Sin leer el `runs-on:`, el guard lo marcaria como fallo
    cuando el runner lo resuelve solo, que es un falso positivo sobre un fichero
    sano.

    Y el caso de verdad, que es el contrario: si el `runs-on:` no dice nada (o dice
    algo que no se reconoce), se asume `sh`, que es el caso conservador: `sh` es el
    que pierde el codigo de salida, asi que se marca y se explica.
    """
    if not shell:
        return 'pwsh' if 'windows' in (runner or '') else 'sh'

    primero = shell.split()[0].lower()
    return primero if primero in ('bash', 'sh', 'pwsh', 'powershell', 'cmd') else None


def _shell_ya_trae_pipefail (shell):
    """
    Si el `shell:` del paso ya lleva `-o pipefail` el solo. (bool)

    El runner pone `--noprofile --norc -e -o pipefail {0}` cuando el `shell:` es `bash`
    a secas. Si alguien escribe `bash -e {0}`, ha REEMPLAZADO esos argumentos y se ha
    llevado el pipefail, y eso si es un hallazgo.
    """
    if not shell:
        return False

    partes = shell.split()
    if not partes or partes[0].lower() != 'bash':
        return False

    if len(partes) == 1:
        return True

    return bool(re.search(r'(?<!\w)pipefail(?!\w)', shell))


def riesgo_de_un_paso (paso):
    """
    El veredicto de UN paso. (codigo, lista_de_hallazgos)

      0  este paso no puede perder un codigo de salida por un pipe
      1  este paso PUEDE perderlo: hay un `tee` y el shell no mira el pipe
      2  hay un `tee` pero el shell lo resuelve por su cuenta (pwsh): no es fallo
    """
    cuerpo = _sin_comentarios(paso['cuerpo'])
    if not cuerpo:
        return 0, []

    texto = '\n'.join(cuerpo)
    if not RE_TEE_EN_PIPE.search(texto):
        return 0, []

    shell = paso['shell']

    # El runner anade `exit $LASTEXITCODE` a los scripts de pwsh, asi que el pipe se
    # nota igual. Se cuenta aparte para que el informe diga POR QUE no es fallo.
    if _clase_del_shell(shell, paso.get('runner', '')) in ('pwsh', 'powershell'):
        return 2, []

    if RE_SET_PIPEFAIL.search(texto) or _shell_ya_trae_pipefail(shell):
        return 0, []

    return 1, [{
        'paso': paso['nombre'],
        'linea': paso['linea'],
        'shell': (shell or '(sin shell: el runner usa %s en %s)'
                   % (_clase_del_shell(shell, paso.get('runner', '')),
                      paso.get('runner') or 'un runner linux')),
        'motivo': ('`X | tee log` devuelve el codigo de tee, que es 0 siempre, y este '
                   'shell no tiene pipefail'),
    }]


def workflows_de (ruta_repo):
    """Los YAML de `.github/workflows/`, ordenados. (lista de rutas)"""
    d = os.path.join(ruta_repo, '.github', 'workflows')
    if not os.path.isdir(d):
        return []
    try:
        nombres = sorted(os.listdir(d))
    except OSError:
        return []
    return [os.path.join(d, n) for n in nombres if n.endswith(EXTENSIONES)]


def repos (raiz):
    """
    Los repos de la suite que tienen workflows. (lista de (nombre, ruta, workflows))

    Se recorren los subdirectorios INMEDIATOS de la raiz, y la raiz misma. El filtro
    es "tiene `.github/workflows/` con algun YAML": un directorio sin workflows no
    puede romper un pipe, y contarlo haria que el recuento de repos pareciera mayor
    de lo que se ha mirado de verdad.
    """
    candidatos = []
    if workflows_de(raiz):
        candidatos.append((os.path.basename(raiz.rstrip(os.sep)) or raiz, raiz))
    try:
        for nombre in sorted(os.listdir(raiz)):
            sub = os.path.join(raiz, nombre)
            if os.path.isdir(sub):
                candidatos.append((nombre, sub))
    except OSError:
        return []

    return [(n, r, wfs) for n, r, wfs in ((n, r, workflows_de(r)) for n, r in candidatos)
            if wfs]


def medir_workflow (ruta):
    """
    Todo lo que el guard ve en un workflow. (riesgos, pasos_mirados, pwsh, error)

    No se usa PyYAML a proposito: las herramientas de este repo no dependen de nada, y
    este guard tiene que correr en un job que no instala nada.
    """
    try:
        txt = io.open(ruta, 'r', encoding='utf-8').read()
    except (OSError, UnicodeDecodeError) as exc:
        return [], 0, 0, 'no se pudo leer %s: %s' % (ruta, exc)

    lineas = txt.replace('\r\n', '\n').replace('\r', '\n').split('\n')
    shell_workflow = _shell_default_del_workflow(lineas)

    riesgos = []
    mirados = 0
    pwsh = 0

    for job in _jobs(lineas):
        for paso in pasos(lineas, job, shell_workflow):
            if not paso['cuerpo']:
                continue
            mirados += 1
            codigo, hallados = riesgo_de_un_paso(paso)
            if codigo == 1:
                for h in hallados:
                    h['job'] = job['nombre']
                riesgos.extend(hallados)
            elif codigo == 2:
                pwsh += 1

    return riesgos, mirados, pwsh, None


def relative (ruta, raiz):
    try:
        return os.path.relpath(ruta, raiz).replace(os.sep, '/')
    except ValueError:
        return ruta


def _parsear (argv):
    """Los flags sin dependencias, y devolviendo los desconocidos."""
    acc = {}
    sueltos = []
    i = 0

    while i < len(argv):
        a = argv[i]
        if a == '--raiz':
            if i + 1 >= len(argv):
                return {'--raiz': [True]}, ['--raiz sin valor']
            acc['--raiz'] = [argv[i + 1]]
            i += 2
            continue
        if a in ('--check', '--listar', '--help', '-h'):
            acc[a] = [True]
            i += 1
            continue
        if a.startswith('--'):
            sueltos.append(a)
            i += 1
            continue
        sueltos.append(a)
        i += 1

    return acc, sueltos


def main ():
    acc, sueltos = _parsear(sys.argv[1:])

    if '--help' in acc or '-h' in acc:
        print(__doc__.strip())
        return 0

    if sueltos:
        print('guard_pipefail: argumento desconocido: %s' % sueltos[0], file=sys.stderr)
        print('  --check | --listar | --raiz <dir> | --help', file=sys.stderr)
        return 2

    raiz = acc.get('--raiz', [RAIZ_POR_DEFECTO])[-1]
    if raiz is True:
        print('guard_pipefail: --raiz necesita el directorio que sigue', file=sys.stderr)
        return 2

    if not os.path.isdir(raiz):
        print('guard_pipefail: no existe el directorio %s' % raiz, file=sys.stderr)
        print('  Esto NO es un hallazgo: es que no se ha mirado nada.', file=sys.stderr)
        return 2

    encontrados = repos(raiz)

    if not encontrados:
        print('guard_pipefail: NO HAY NINGUN WORKFLOW QUE MIRAR bajo %s' % raiz,
              file=sys.stderr)
        print('', file=sys.stderr)
        print('  Un 0 aqui seria el verde mas peligroso de todos: el guard no habria', file=sys.stderr)
        print('  medido nada y su veredicto no hablaria de nada de la suite. Pasa', file=sys.stderr)
        print('  `--raiz` con un directorio que tenga un clon por repo.', file=sys.stderr)
        return 2

    riesgos = []
    total_mirados = 0
    total_pwsh = 0

    for nombre, ruta_repo, wfs in encontrados:
        for wf in wfs:
            hallados, mirados, pwsh, error = medir_workflow(wf)
            if error:
                print('guard_pipefail: %s' % error, file=sys.stderr)
                print('  Esto NO es un hallazgo: es que la comprobacion no llego a hacerse.',
                      file=sys.stderr)
                return 2

            total_mirados += mirados
            total_pwsh += pwsh
            for h in hallados:
                h['repo'] = nombre
                h['fichero'] = relative(wf, raiz)
                riesgos.append(h)

    if '--listar' in acc:
        print('repos con workflows: %d' % len(encontrados))
        for nombre, ruta_repo, wfs in encontrados:
            print('  %-18s %d workflow(s)' % (nombre, len(wfs)))
        print('pasos con codigo ejecutable: %d' % total_mirados)
        print('con `| tee` en pwsh (el runner les anade exit $LASTEXITCODE): %d' % total_pwsh)
        print('que pueden perder el codigo de salida: %d' % len(riesgos))
        return 0

    if not riesgos:
        if '--check' not in acc:
            print('guard_pipefail: ningun trabajo puede perder un codigo de salida por un pipe.')
            print('  repos con workflows : %d' % len(encontrados))
            print('  pasos ejecutables   : %d' % total_mirados)
            print('  con tee en pwsh     : %d (el runner les anade exit $LASTEXITCODE)' % total_pwsh)
        return 0

    print('guard_pipefail: UN TRABAJO DE LA SUITE PUEDE PONERSE EN VERDE SIN HABER PASADO.',
          file=sys.stderr)
    print('', file=sys.stderr)
    for h in riesgos:
        print('  %s :: %s' % (h['repo'], h['fichero']), file=sys.stderr)
        print('    job   : %s' % h['job'], file=sys.stderr)
        print('    paso  : %s (linea %d)' % (h['paso'], h['linea']), file=sys.stderr)
        print('    shell : %s' % h['shell'], file=sys.stderr)
        print('    porque: %s' % h['motivo'], file=sys.stderr)
        print('', file=sys.stderr)

    print('  Que significa: ese paso sale VERDE aunque lo de la izquierda del pipe', file=sys.stderr)
    print('  falle. `cmd | tee log` devuelve el codigo de `tee`, que es 0 siempre.', file=sys.stderr)
    print('  Medido en el primer push de este repo: el paso de tests de MidiKeyboard', file=sys.stderr)
    print('  salio en verde con `pnpm test` fallando, con ERR_PNPM_NO_IMPORTER_MANIFEST_FOUND', file=sys.stderr)
    print('  debajo de un tick.', file=sys.stderr)
    print('', file=sys.stderr)
    print('  Arreglo:', file=sys.stderr)
    print('    pon `shell: bash` en el paso', file=sys.stderr)
    print('    y anade `set -o pipefail` al principio del `run:`', file=sys.stderr)
    print('', file=sys.stderr)
    print('  Las dos cosas. `shell: bash` solo ya vale, porque el runner le pone', file=sys.stderr)
    print('  `-o pipefail`; el `set -o pipefail` de dentro es la red por si alguien', file=sys.stderr)
    print('  cambia el shell del paso. Y sin `shell:`, el runner usa `sh`, que es', file=sys.stderr)
    print('  `-e {0}`: sin pipefail.', file=sys.stderr)
    print('', file=sys.stderr)
    return 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:                                    # noqa: BLE001
        print('error: %s' % exc, file=sys.stderr)
        sys.exit(2)
