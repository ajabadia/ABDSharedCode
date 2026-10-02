# `pnpm-workspace-bootstrap`

Recrea el layout de hermanos del workspace pnpm de la suite y ejecuta
`pnpm install`. Los paquetes `@abdsynths/*` no estan publicados en ningun
registry: solo existen como miembros de este workspace, asi que sin este
bootstrap `pnpm install` no puede resolver las dependencias `workspace:*`.

Los inputs y el layout estan documentados en el `action.yml`. Este README solo
recoge lo que **no** se deduce leyendo el fichero: lo que se ha medido sobre
`actions/checkout` y por que los tres checkouts tienen que seguir siendo de esa
accion.

---

## Lo que hace `actions/checkout` cuando dos checkouts caen en el MISMO `path:`

**Medido, no deducido.** Un workflow de un solo uso en una rama descartable
(`ubuntu-latest`) con un job por caso, y el resultado leido
del log de cada job. Re-medido con los cinco casos por duplicado en la misma
corrida, con v4 y con v5 (run 37060586396): el veredicto de cada caso es
identico en las dos columnas. Duplicarlos en una sola corrida, en vez de
comparar contra la medicion antigua, es para que un resultado distinto no se
pueda atribuir al runner.

| caso | `path:` | repo | resultado medido |
|---|---|---|---|
| A | `dup` | ABDSharedCode, luego ABDSharedAssets | **el segundo borra el primero** |
| B | `uno` / `dos` | distintos | conviven, los dos intactos |
| C | `dup` | ABDSharedAssets dos veces, mismo `ref` | idempotente, no borra |
| D | `dup` | ABDSharedAssets, `ref` distinto | cambia al `ref` nuevo, **no borra** |
| E | `ABDSharedCode` | el llamante y luego ABDSharedCode | **no borra**: el llamante ya era ABDSharedCode |
| F | la raiz del workspace, y luego el del `project` | el llamante sin `path:`, y luego la accion con `project` | **no borra nada, pero deja dos clones del llamante** |

El caso E salio de medir la consecuencia practica (llamar a la accion con
`project: ABDSharedCode`) y resulto **degenerado**: el repo llamante de ese
workflow era el propio ABDSharedCode, asi que los dos remotos coincidian y no
habia nada que borrar. Se queda en la tabla porque es el que fija por que el caso
A lleva el nombre "repo DISTINTO" en vez de "path repetido", y porque una
medicion que no reproduce lo que se queria medir tambien es un dato.

Del caso F, que se midio aparte en ABDEep (`windows-2022`, run `37058821215`,
rama descartable) y es el que hace peligroso el aviso de la accion: un checkout
**sin `path:` deja el clon en la RAIZ del workspace**, no en un subdirectorio con
el nombre del repo. Con el caso E al lado, el `ls` del workspace daba:

```
ABDEep                      <- el clon de la accion
ABDSharedAssets
ABDSharedCode
CMakeLists.txt              <- del clon del checkout previo, en la raiz
Source  WebUI  scripts  build.bat  Presets  resources  ...
node_modules
pnpm-lock.yaml
pnpm-workspace.yaml         <- el manifiesto, tambien en la raiz
```

El llamante no se borra: son paths distintos, y lo unico que se pierde es el
`project` declarado en el paso de la accion. Con el checkout previo en
`path: <project>` el resultado es **identico al control**, porque ahi si es el
caso C.

Del caso A, en el log:

```
Deleting the contents of '/home/runner/work/ABDSharedCode/ABDSharedCode/dup'
```

y despues `CMakeLists.txt`, `DspCore/` y `tools/` - todo lo del primer repo -
ya no estaban. El `remote` final era el del segundo. O sea: **dos checkouts al
mismo `path:` no se acumulan, el segundo se come el primero**.

### Lo que decide es el REMOTO, no el `path:` repetido

De los cuatro casos: A borra (remotos distintos), B convive (paths distintos),
C y D **no borran** aunque repitan `path:` (mismo remoto). El `path:` repetido no
es lo peligroso por si mismo; lo peligroso es repetirlo con **otro repo**.

La regla que sale, y que es la que aplica a esta accion:

> Dos checkouts con el mismo `path:` se limpian entre si **si y solo si apuntan a
> repos distintos**. Con el mismo repo, el segundo es idempotente o cambia de
> `ref`, pero nunca borra.

### El guard que vigila esta tabla, y que hacer cuando el checkout cambie

Lo de arriba esta **medido**, no deducido, y lo medido se queda viejo: si
`actions/checkout` sube de version y cambia de comportamiento, este README
sigue diciendo exactamente lo mismo y con la misma seguridad. Un README no
falla, asi que la tabla tiene un guard al lado.

Los datos estan en `tools/checkout-medido.json` (la misma tabla, en JSON) y el
guard es el bloque `la tabla de casos medidos de actions/checkout` de
`tools/guard_atributos.test.mjs`. Seis comprobaciones:

- **`NINGUN checkout del repo esta sin medir`** es el que paga la tabla. Recorre
  los YAML de `.github/` y saca cada `actions/checkout@<version>`, y falla si
  alguna no esta en `versiones_medidas`. Sube el checkout a `v5` y sale con 1
  diciendo que hay que volver a medir los casos A-E.
- El fichero medido tiene que decir **contra que** version se midio, traer la
  **linea de log** que lo prueba y exactamente **un** caso que borra. Sin eso, una
  lista vacia haria que lo de arriba pasara de verde sin mirar nada.
- El README y el JSON tienen que contar los mismos casos, y decir el mismo
  resultado. La tabla es para gente y el JSON es para el guard, pero no pueden
  separarse en silencio. Son dos tests: uno para los casos y otro para el
  resultado de cada uno.
- Los `path:` fijos que la accion coloca **tienen que ser** los que la medicion
  declara. Anade un cuarto checkout y falla si no lo anotas.
- La **guarda de colision** tiene que cubrir cada uno de esos `path:` en su
  `case`, y tiene que ir **antes** del primer checkout.

Que **no** hace, y conviene no creas: no vuelve a medir el comportamiento.
Medir necesita el runner, un workflow de un solo uso y leer su log; un test que
fabrica ese entorno estara probando su propia fabrica. Lo que si hace es
impedir que la decision se tome sin mirar.

### Como se vuelve a medir

1. Una rama descartable con un workflow por caso, `actions/checkout@<version>`
   y `ubuntu-latest`. Los casos son los de la tabla de arriba: mismo `path:` con
   repos distintos, paths distintos, mismo repo dos veces, mismo repo con otro
   `ref`, y el llamante contra si mismo.
2. Leer el log de cada job y copiar **la linea** que lo demuestra, no el
   resumen. Al borrar sale `Deleting the contents of '<path>'`.
3. Actualizar `tools/checkout-medido.json` (`versiones_medidas` y, si el
   comportamiento cambio, `casos` y `regla`) y este README, en el mismo commit.
4. `node --test tools/guard_atributos.test.mjs`: si algo no cuadra, el guard
   avisa antes que nadie.

### De donde sale esa regla (por si cambia el `checkout`)

De `src/git-directory-helper.ts`, `prepareExistingDirectory()`: si el `fetch-url`
del directorio no es el que pide el checkout, pone `remove = true`, y al final
borra el contenido del directorio. Con el mismo remoto, `remove` se queda en
`false` y lo unico que hace es limpiar refs y `reset --hard` en su sitio.

O sea que la regla no es una convencion de la suite, es el comportamiento
documentado del checkout. Si un dia cambia, esto hay que volver a medirlo.

### Y por que esta accion no puede sufrir la colision

La accion hace tres checkouts con `path:` `<project>`, `ABDSharedCode` y
`ABDSharedAssets`. Si alguien la llamara con `project: ABDSharedCode`, los dos
primeros caerian en el mismo directorio con repos distintos, y por el caso A el
segundo borraria el primero. El resultado seria un `workspace` con el repo
llamante donde deberia estar `ABDSharedCode`, y el bootstrap fallaria mas tarde,
con un motivo que no nombra el checkout que lo rompio.

La accion se protege de eso **en dos sitios**, y el primero es el que evita
que ocurra:

1. **Antes de clonar nada.** El primer paso de `runs.steps` en el `action.yml` es
   una guarda que rechaza `project: ABDSharedCode` y `project: ABDSharedAssets` y
   sale con codigo 1 sin haber hecho un solo checkout. El mensaje dice cual es la
   colision (los tres `path:` y el hecho de que el segundo se come el primero), no
   solo "input invalido". La comparacion es sin mayusculas porque los jobs que
   usan esta accion corren en `windows-latest`, donde `abdsharedcode/` y
   `ABDSharedCode/` son el mismo directorio y la colision ocurre igual.
2. **Despues de clonar**, en `tools/bootstrap-workspace.mjs`, que es el unico que
   sabe cual es el layout esperado: si `ABDSharedCode/` no esta donde debe, falla
   ahi y no en el `pnpm install` de tres pasos mas tarde.

Las dos hacen falta y no son la misma comprobacion. La guarda del `action.yml` ve
un input; el script ve el disco. Un `actions/checkout` puesto en el workflow
llamante, o un repo que alguien haya renombrado, se escapan de la guarda pero no
del script.

## Lo que esta accion hace deliberadamente

- El checkout del repo llamante va **sin `repository:`**, para heredar el
  comportamiento de un checkout normal (asi un PR desde un fork resuelve igual).
  Los otros dos si llevan `repository:`, porque son repos que el workflow
  llamante no puede clonar.
- Los tres checkouts los hace **la accion, no el workflow**. Si el workflow
  hace su propio checkout del repo llamante antes de esta accion, el layout
  puede quedar con dos clones en el mismo sitio. Por eso el `action.yml` avisa de
  eso en el bloque de uso.
- Los refs de los hermanos son configurables (`shared-code-ref`,
  `shared-assets-ref`) pero por defecto siguen la rama por defecto. Fijarlos a un
  SHA es lo que hace reproducible el `pnpm install`; si se usa un `ref` movil, el
  resultado depende de cuando corre.

## Medido en esta maquina

- `pnpm-version` por defecto `12.8.1`, la misma que declaran `packageManager`
  en los dos repos que fijan la version. Antes este input era `10`, asi que una
  prueba podia pasar en local y fallar en CI (o al reves) sin que nadie tocase
  nada.
- `skip-puppeteer-download` por defecto `true`: ningun job de la suite usa
  puppeteer (los e2e usan Playwright) y son ~150 MB por job que nadie consume.
