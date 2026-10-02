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
(`ubuntu-latest`, `actions/checkout@v4`) con un job por caso, y el resultado leido
del log de cada job:

| caso | `path:` | repo | resultado medido |
|---|---|---|---|
| A | `dup` | ABDSharedCode, luego ABDSharedAssets | **el segundo borra el primero** |
| B | `uno` / `dos` | distintos | conviven, los dos intactos |
| C | `dup` | ABDSharedAssets dos veces, mismo `ref` | idempotente, no borra |
| D | `dup` | ABDSharedAssets, `ref` distinto | cambia al `ref` nuevo, **no borra** |
| E | `ABDSharedCode` | el llamante y luego ABDSharedCode | **no borra**: el llamante ya era ABDSharedCode |

El caso E salio de medir la consecuencia practica (llamar a la accion con
`project: ABDSharedCode`) y resulto **degenerado**: el repo llamante de ese
workflow era el propio ABDSharedCode, asi que los dos remotos coincidian y no
habia nada que borrar. Se queda en la tabla porque es el que fija por que el caso
A lleva el nombre "repo DISTINTO" en vez de "path repetido", y porque una
medicion que no reproduce lo que se queria medir tambien es un dato.

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

La accion se protege de eso en `tools/bootstrap-workspace.mjs`, que es el unico
que sabe cual es el layout esperado: si `ABDSharedCode/` no esta donde debe,
falla ahi y no en el `pnpm install` de tres pasos mas tarde.

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
