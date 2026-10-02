#!/usr/bin/env python3
"""
REGENERAR LA LINEA BASE CONTRA LA FOTO, Y NO CONTRA EL ARBOL DE TRABAJO.

La linea base (`tools/audit-baseline.json`) es una foto de la suite: dice que 55
fuentes de ABDSharedCode no las consume nadie. Es una verdad CON FECHA, y la fecha
son los SHA contra los que se midio (la clave `foto`).

El problema de regenerarla en LOCAL, y por que este comando existe. El auditor
calcula la suite como el DIRECTORIO PADRE de donde vive el script
(`SUITE = <padre de SHARED>`), asi que en el arbol de trabajo esta mirando los
hermanos CON LO QUE TIENEN EN DISCO. Y ahi hay trabajo SIN COMMITEAR de otra
persona: `git status` da 50 entradas en ABDEep, 96 en ABDMS2000 y 45 en ABDNeural.

Medido en este repo, con los mismos 6 repos y la misma linea base:

    en el laboratorio (clones en los SHA de la foto)  -> 0, las 55 huerfanas
    en el arbol de trabajo (con cambios sin commitear) -> 1, las 55 SON huerfanas

Lo que pasa en el arbol es que los consumidores de esas 55 fuentes EXISTEN, pero
en ficheros modificados o sin versionar que estan en el disco y en NINGUN HEAD
commiteado. Ejemplo: `DspEffects/CascadeShelfEq.h` solo lo incluyen
`ABDMS2000/Source/DSP/Effects/Equalizer.{h,cpp}` y `DSPCoreTests_EqualizerParity.cpp`,
los tres modificados o sin versionar.

Si se regenera la linea base ahi, lo que se commitea NO son las huerfanas de CI: es
el estado del disco de otra persona, que el job de audit no podria reproducir nunca.
El rojo que sale luego es correcto y el diagnostico equivocado, que es la forma mas
cara de perder tiempo.

QUE HACE ESTE. Monta un laboratorio: clona cada repo de la foto en su SHA, con
`fetch --depth 1` para no baixar la historia entera, y ejecuta EL AUDITOR DE DENTRO
DEL LABORATORIO. No el del arbol de trabajo: el auditor saca `SUITE` de donde esta
el propio script, asi que el que hay que ejecutar es el del clon. Es la unica
forma de que la suite que mide sea la suite que hay.

QUE SHA USA, Y POR QUE NO ES EL DE LA FOTO POR DEFECTO. Los SHA salen del WORKFLOW
(`--listar` de `tools/verificar_foto.py`), no de la linea base. La linea base es lo
que se quiere REGENERAR, asi que leer los SHA de ahi seria medirse a uno mismo: si
los SHA se equivocaron al subirlos en el workflow, este comando volveria a clonar
los mismos repos equivocados y escribiria una linea base que sigue mintiendo. Del
workflow, que es lo que CI va a clonar de verdad. Con `--de-la-linea-base` se
fuerza la otra fuente, y si las dos no coinciden el script lo dice antes de
clonar nada.

Y ABDSharedCode, QUE NO ESTA EN LA FOTO. Se clona en el HEAD LOCAL, no en
`origin/master`. En CI el job `audit` mide este repo en el commit que se esta
pusheando, asi que medir contra `origin/master` mediria un commit distinto del que
se va a publicar. Medido: cuando el arbol tiene commits sin subir, clonar en
`origin/master` produce una linea base que no corresponde a lo que se commitea.

Y DE DONDE SALE EL RESULTADO. Con `--escribir` la linea base nueva se copia al
arbol de trabajo, y ANTES se ensenia por pantalla el diff contra la que hay. El
arreglo de una linea base equivocada es volver a este comando, asi que el paso
anterior al que sustituye era el que decia cual era.

Salidas:
  0  la foto se midio y el veredicto es el que dice (0 en el trinquete)
  1  el trinquete muerde, o el laboratorio no se pudo montar. NO es lo mismo y el
     motivo sale en el sitio de cada uno
  2  el laboratorio no se pudo montar: no se ha medido nada, y eso no es un hallazgo

  --check           (por defecto) monta el laboratorio y MEDE, sin escribir nada
  --escribir        regenera la linea base en el arbol de trabajo. Es una decision,
                    asi que es un flag y no el comportamiento por defecto
  --de-la-linea-base  clona los SHA que dice la linea base, no los del workflow
  --raiz DIR        donde montar el laboratorio (por defecto, un temporal)
  --workflow FICHERO  de donde leer los SHA (por defecto, el de este repo)
  --reusar          no borra el laboratorio si ya existe (mas rapido al iterar)
  --montar          solo clona, no mide. Sirve para mirar el laboratorio

Uso:
  python tools/regenerar_linea_base.py
  python tools/regenerar_linea_base.py --escribir
  python tools/regenerar_linea_base.py --raiz D:/tmp/lab --montar
"""

import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED = os.path.abspath(os.path.join(HERE, '..'))

LINEA_BASE = os.path.join(HERE, 'audit-baseline.json')
AUDITOR = 'audit_unconsumed_sources.py'

# Donde va el laboratorio. Un temporal del sistema por defecto: el arbol de trabajo
# no es un sitio para clonar seis repos, y `/d/tmp` es una ruta que solo existe en
# esta maquina.
LAB_POR_DEFECTO = os.path.join(tempfile.gettempdir(), 'abd-foto')

REPOSITORY = re.compile(r'^\s*repository:\s*[\'"]?([\w.-]+/[\w.-]+)[\'"]?\s*$')
REF = re.compile(r'^\s*ref:\s*[\'"]?([0-9a-f]{7,40})[\'"]?\s*$')


def git (destino, *args):
    """Un `git -C destino ...`. (stdout, codigo)."""
    r = subprocess.run(['git', '-C', destino] + list(args),
                       capture_output=True, text=True, errors='replace')
    return (r.stdout or '').strip(), r.returncode


def shas_del_workflow (ruta):
    """
    Los SHA que el workflow clona, como {repo: sha}. (diccionario, texto_de_error).

    Se leen del YAML con dos regex, sin PyYAML, por la misma razon que en
    `verificar_foto.py`: este comando tiene que correr sin instalar nada. Y se
    empareja `repository:` con su `ref:` siguiente porque en un checkout de actions
    esos dos campos van juntos y en ese orden.

    El nombre se recorta a la parte que sigue al `owner/`, porque el laboratorio los
    clona en directorios que se llaman como el repo corto.
    """
    if not os.path.isfile(ruta):
        return None, 'no existe %s' % ruta
    try:
        txt = io.open(ruta, 'r', encoding='utf-8').read()
    except OSError as exc:
        return None, 'no se pudo leer %s: %s' % (ruta, exc)

    shas = {}
    pendiente = None
    for linea in txt.split('\n'):
        m = REPOSITORY.match(linea)
        if m:
            pendiente = m.group(1).rsplit('/', 1)[-1]
            continue
        m = REF.match(linea)
        if m and pendiente:
            shas[pendiente] = m.group(1)
            pendiente = None

    if not shas:
        return None, ('%s no tiene ningun checkout con `repository:` y `ref:`. Si el '
                      'workflow cambio de forma, este comando se esta midiendo a si '
                      'mismo: no encuentra la foto y mediria una que nadie ha medido.'
                      % ruta)
    return shas, None


def shas_de_la_linea_base (ruta):
    """La clave `foto` de la linea base. (diccionario, texto_de_error)."""
    if not os.path.isfile(ruta):
        return {}, 'no existe %s' % ruta
    try:
        datos = json.load(io.open(ruta, 'r', encoding='utf-8'))
    except (ValueError, OSError) as exc:
        return {}, 'no se pudo leer %s: %s' % (ruta, exc)

    foto = datos.get('foto') if isinstance(datos, dict) else None
    if not isinstance(foto, dict) or not foto:
        return {}, ('%s no tiene la clave "foto", o esta vacia. Sin ella no se sabe '
                    'que repos hay que clonar.' % ruta)
    return foto, None


def clonar_en (destino, repo, sha, destino_log):
    """
    Clona `repo` en `sha` dentro de `destino`. (ok, motivo)

    NO es `git clone` y son tres razones, todas medidas:
      - `git clone --depth 1` trae el HEAD del rama por defecto, no un SHA cualquiera:
        para dejar el repo en un commit que no sea la punta hay que pedirlo
        explicitamente.
      - un clon completo baja toda la historia: ABDSharedAssets son 56 MB solo de
        `.git`, y los seis repos juntos tardan minutos. `fetch --depth 1 <sha>` tarda
        menos de 2 s por repo.
      - el repo se clona en un lab que se borra entero, asi que `--no-checkout` y un
        `checkout` aparte solo anaden un paso.

    Un SHA que el remoto no tiene NO se avisa: se cae. Por eso el error dice "no se
    pudo dejar en", que es lo que de verdad paso, y no un fallo de clon que no fue.
    """
    os.makedirs(destino, exist_ok=True)

    _, code = git(destino, 'init', '-q', '.')
    if code != 0:
        return False, 'no se pudo inicializar un repo en %s' % destino

    # `remote add` y no `remote set-url`: con `--reusar` el repo ya esta inicializado
    # y TIENE el remoto, y `add` falla con "remote origin already exists". Medido: sin
    # este `set-url`, la segunda pasada con --reusar daba 6 fallos de "no se pudo
    # apuntar al remoto" y se comia el laboratorio entero sin medir nada.
    remoto = 'https://github.com/ajabadia/%s.git' % repo
    tiene, _ = git(destino, 'remote')
    if 'origin' in tiene.split():
        _, code = git(destino, 'remote', 'set-url', 'origin', remoto)
        if code != 0:
            return False, 'no se pudo reapuntar el remoto de %s' % repo
    else:
        _, code = git(destino, 'remote', 'add', 'origin', remoto)
        if code != 0:
            return False, 'no se pudo apuntar al remoto de %s' % repo

    salida, code = git(destino, 'fetch', '-q', '--depth', '1', 'origin', sha)
    if code != 0:
        destino_log.append(salida)
        return False, ('no se pudo traer el %s de %s. O el SHA no esta en el remoto '
                       '(un force-push lo borro), o el repo se renombro.' % (sha[:12], repo))

    _, code = git(destino, 'checkout', '-q', 'FETCH_HEAD')
    if code != 0:
        return False, 'no se pudo hacer checkout en %s' % repo

    head, code = git(destino, 'rev-parse', 'HEAD')
    if code != 0 or not head:
        return False, 'no se pudo preguntar el HEAD de %s' % repo

    if not head.startswith(sha[:12]):
        return False, 'quedo en %s y se pedia %s' % (head[:12], sha[:12])

    return True, None


def montar (raiz, shas, reusar, log):
    """
    Clona todos los repos de la foto mas este. (ok, lista_de_fallos)

    Los hermanos van en el SHA que se les pidio. ABDSharedCode va en el HEAD LOCAL,
    por una razon que esta en el docstring: es lo que el job `audit` mide en CI.

    Un repo que falla NO se salta en silencio y NO se cuela como medido: se anota en
    la lista y el comando sale con 2. Un laboratorio al que le falta un hermano
    mediria MENOS huerfanas que CI (los consumidores que faltan harian pasar por
    huerfanas fuentes que no lo son), y escribir esa linea base seria meter una
   medida falsa en el sitio que dice medir la verdad.
    """
    fallos = []

    if os.path.isdir(raiz):
        if reusar:
            print('reutilizando el laboratorio de %s (--reusar)' % raiz)
        else:
            shutil.rmtree(raiz)
    os.makedirs(raiz, exist_ok=True)

    for repo in sorted(shas):
        destino = os.path.join(raiz, repo)
        ok, motivo = clonar_en(destino, repo, shas[repo], log)
        if ok:
            print('  ok       %-16s %s' % (repo, shas[repo][:12]))
        else:
            print('  FALLO    %-16s %s' % (repo, motivo))
            for linea in log:
                print('           git: %s' % linea)
            log[:] = []
            fallos.append((repo, motivo))

    # Este repo, en el commit que se esta vas a subir. Sin SHA de la foto porque no
    # lo tiene: en el workflow, su checkout no lleva `ref:`, es el propio commit.
    head, code = git(SHARED, 'rev-parse', 'HEAD')
    if code != 0 or not head:
        print('  FALLO    %-16s no se pudo preguntar el HEAD de este repo' % 'ABDSharedCode')
        fallos.append(('ABDSharedCode', 'no se pudo preguntar el HEAD de este repo'))
        return False, fallos

    ok, motivo = clonar_en(os.path.join(raiz, 'ABDSharedCode'), 'ABDSharedCode', head, log)
    if ok:
        print('  ok       %-16s %s (HEAD local)' % ('ABDSharedCode', head[:12]))
    else:
        print('  FALLO    %-16s %s' % ('ABDSharedCode', motivo))
        fallos.append(('ABDSharedCode', motivo))

    return not fallos, fallos


def medir (raiz, escribir, linea_base_entrada, linea_base_salida=None):
    """
    Corre el auditor DE DENTRO del laboratorio. (codigo, texto_de_error)

    El auditor es el del clon, no el del arbol: `SUITE` se calcula como el padre del
    directorio donde vive el script, asi que ejecutar el de aqui mediria el arbol de
    trabajo, que es justo lo que este comando evita.

    Con `--escribir` se ejecuta tambien el `--write-baseline`, que escribe DENTRO del
    clon, y el resultado se copia al arbol de trabajo. El trinquete y la regeneracion
    leen la misma linea base de entrada, que vive dentro del laboratorio: asi el
    laboratorio es autonomo y se puede tirar sin haber tocado nada de aqui.
    """
    guion = os.path.join(raiz, 'ABDSharedCode', 'tools', AUDITOR)

    if not os.path.isfile(guion):
        return 2, 'el clon de ABDSharedCode no trae %s. El commit que se ha clonado no lo tiene.' % AUDITOR

    generado = os.path.join(raiz, 'ABDSharedCode', 'tools', 'audit-baseline.json')

    # SIN `--write-baseline` NO HAY QUE COPIAR NADA. El auditor no escribe y el
    # `audit-baseline.json` del clon es el que traia el repo, o sea una linea base
    # vieja. Copiarlo encima de la del arbol seria escribir una linea base sin haber
    # medido nada, y el `--escribir` seemingly habria hecho su trabajo. Por eso la
    # copia va dentro del `if escribir`, y por eso `escribir` es lo unico que la
    # dispara: medido, este era el bug.
    if not escribir:
        r = subprocess.run([sys.executable, guion, '--check', '--baseline', linea_base_entrada],
                           capture_output=True, text=True, errors='replace')
        print(((r.stdout or '') + (r.stderr or '')).rstrip())
        return r.returncode, None

    # Con `--escribir` se hace lo que hace el auditor cuando alguien lo ejecuta a
    # mano: primero el trinquete, para ver el estado, y luego la regeneracion. Los dos
    # leen la MISMA linea base de entrada, que esta dentro del laboratorio.
    r = subprocess.run([sys.executable, guion, '--check', '--baseline', linea_base_entrada],
                       capture_output=True, text=True, errors='replace')
    print(((r.stdout or '') + (r.stderr or '')).rstrip())

    r = subprocess.run([sys.executable, guion, '--write-baseline'],
                       capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        return 2, ('el auditor no pudo regenerar la linea base (codigo %d). Su salida va '
                   'encima.' % r.returncode)

    if not os.path.isfile(generado):
        return 2, 'el auditor dijo que si pero no dejo ninguna linea base en el laboratorio'

    shutil.copyfile(generado, linea_base_salida)
    print('escrita la linea base del laboratorio en %s' % linea_base_salida)

    # El codigo del TRINQUETE es el que se devuelve, no el de la regeneracion. El
    # `--write-baseline` sale 0 siempre que pudo escribir, asi que devolver el suyo
    # haria que un rojo real del audit se perdiera al hacer `--escribir`.
    return r.returncode, None


def diff_de_la_linea_base (antes, despues):
    """
    Lo que cambia entre la linea base de antes y la de despues. (texto)

    Se ensenia ANTES de escribir. Es la unica defensa contra el fallo que el comando
    existe para evitar: una linea base regenerada en el sitio equivocado no esta
    "un poco mal", esta describiendo una foto que CI jamas va a medir, y el rojo que
    vendria luego no diria por que.
    """
    try:
        a = json.load(io.open(antes, 'r', encoding='utf-8'))
        b = json.load(io.open(despues, 'r', encoding='utf-8'))
    except (ValueError, OSError) as exc:
        return '  no se pudo comparar (%s). La de antes esta donde estaba.' % exc

    fa, fb = set(a.get('fuentes') or []), set(b.get('fuentes') or [])
    lineas = []
    for x in sorted(fb - fa):
        lineas.append('  + %s  (nueva huerfana)' % x)
    for x in sorted(fa - fb):
        lineas.append('  - %s  (ya la consume alguien)' % x)
    if not lineas:
        return '  sin cambios: las huerfanas son las mismas.'
    return '\n'.join(lineas)


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
        if a == '--workflow':
            if i + 1 >= len(argv):
                return {'--workflow': [True]}, ['--workflow sin valor']
            acc['--workflow'] = [argv[i + 1]]
            i += 2
            continue
        if a in ('--check', '--escribir', '--de-la-linea-base', '--reusar', '--montar',
                 '--help', '-h'):
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
        print('regenerar_linea_base: argumento desconocido: %s' % sueltos[0], file=sys.stderr)
        print('  --check | --escribir | --de-la-linea-base | --raiz <dir> | --workflow <f> | '
              '--reusar | --montar | --help', file=sys.stderr)
        return 2

    raiz = acc.get('--raiz', [LAB_POR_DEFECTO])[-1]
    if raiz is True:
        print('regenerar_linea_base: --raiz necesita el directorio que sigue', file=sys.stderr)
        return 2

    # El workflow del repo, o el que se le pase. El flag existe para poder MEDIR un
    # workflow escrito a mano, que es como se comprueba que el comando falla cuando no
    # encuentra la foto en vez de clonar una foto inventada.
    if '--workflow' in acc:
        valor = acc['--workflow'][-1]
        if valor is True:
            print('regenerar_linea_base: --workflow necesita el fichero que sigue',
                  file=sys.stderr)
            return 2
        workflow = valor
    else:
        workflow = os.path.join(SHARED, '.github', 'workflows', 'shared-code-ci.yml')
    del_wf, error_wf = shas_del_workflow(workflow)
    if error_wf:
        print('regenerar_linea_base: no se pudo leer la foto del workflow.', file=sys.stderr)
        print('  %s' % error_wf, file=sys.stderr)
        print('  Esto NO es un hallazgo: es que no se ha medido nada.', file=sys.stderr)
        return 2

    del_base, error_base = shas_de_la_linea_base(LINEA_BASE)

    # Las dos fuentes se comparan ANTES de clonar nada. Si no coinciden, el trinquete
    # del audit ya esta diciendo que el suelo se movio, y clonar de una u otra da
    # lineas base distintas. Se dice cual se va a usar y por que.
    if '--de-la-linea-base' in acc:
        if error_base:
            print('regenerar_linea_base: %s' % error_base, file=sys.stderr)
            return 2
        shas = dict(del_base)
        origen = 'la linea base (--de-la-linea-base)'
    else:
        shas = dict(del_wf)
        origen = 'el workflow'

    print('regenerar_linea_base: %d repos, SHA de %s' % (len(shas), origen))
    print('  %s' % os.path.normpath(raiz))

    if not error_base:
        distintos = [r for r in sorted(set(del_wf) | set(del_base))
                     if (del_wf.get(r) or '')[:12] != (del_base.get(r) or '')[:12]]
        if distintos and '--de-la-linea-base' not in acc:
            print('', file=sys.stderr)
            print('  AVISO: el workflow y la linea base NO coinciden en %d repo(s):' % len(distintos),
                  file=sys.stderr)
            for r in distintos:
                print('    %-16s workflow=%s  linea base=%s'
                      % (r, (del_wf.get(r) or '(nada)')[:12], (del_base.get(r) or '(nada)')[:12]),
                      file=sys.stderr)
            print('', file=sys.stderr)
            print('  Se clona con los del WORKFLOW, que es lo que CI va a medir.', file=sys.stderr)
            print('  Si lo que querias era regenerar contra la linea base, el audit de', file=sys.stderr)
            print('  este repo no va a estar de acuerdo: eso lo dice el job `foto`.', file=sys.stderr)
            print('', file=sys.stderr)

    log = []
    ok, fallos = montar(raiz, shas, '--reusar' in acc, log)

    if not ok:
        print('', file=sys.stderr)
        print('regenerar_linea_base: NO SE HA PODIDO MONTAR EL LABORATORIO.', file=sys.stderr)
        print('  Fallaron %d repo(s): %s' % (len(fallos), ', '.join(r for r, _ in fallos)),
              file=sys.stderr)
        print('', file=sys.stderr)
        print('  Esto NO es un hallazgo del audit: es que no se ha medido nada, y un', file=sys.stderr)
        print('  laboratorio al que le falta un hermano mediria MENOS huerfanas que CI.', file=sys.stderr)
        return 2

    if '--montar' in acc:
        print('laboratorio montado en %s (--montar: no se ha medido nada)' % raiz)
        return 0

    # La entrada del trinquete va dentro del laboratorio: el auditor lee de ahi, y de
    # este modo el laboratorio es autonomous y se puede medir y luego tirar sin tocar
    # el arbol de trabajo. Sin `--escribir` no hay nada que copiar porque lo que se
    # lee es la linea base de aqui, tal cual.
    entrada = LINEA_BASE
    antes = None
    if '--escribir' in acc:
        antes = LINEA_BASE + '.antes'
        entrada = os.path.join(raiz, 'linea-base-entrada.json')
        shutil.copyfile(LINEA_BASE, antes)
        shutil.copyfile(LINEA_BASE, entrada)

    codigo, error = medir(raiz, '--escribir' in acc, entrada, LINEA_BASE)
    if error:
        print('', file=sys.stderr)
        print('regenerar_linea_base: %s' % error, file=sys.stderr)
        return 2

    if '--escribir' in acc:
        print('')
        print('cambio en la linea base:')
        print(diff_de_la_linea_base(antes, LINEA_BASE))
        print('')
        print('  la anterior se ha guardado en %s' % os.path.normpath(antes))
        print('  si no era esto lo que querias, esa es la vuelta atras.')

    return codigo


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as exc:                                    # noqa: BLE001
        print('error: %s' % exc, file=sys.stderr)
        sys.exit(2)
