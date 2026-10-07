# Auditoría: los 20 commits que llegaron a master en un push directo (2026-10-06)

## Qué pasó

El 2026-10-06 a las 10:24 (+0200 / 08:24 UTC) se hizo un **push directo a
`master`** (reflog de `origin/master`: `update by push`) con **exactamente 20
commits** y **155 archivos** (140 `.h`/`.cpp`, 6 `.md`, configs), saltándose el
flujo de PR:

```
d6c3025..a2eb333  (20 commits, autor "Buffy (local agent)", 01:54–08:33)
```

Cómo se construyó esa tanda (reflog de `master`):

| hora | evento |
|---|---|
| 08:13 | `a0f8ffb` — UN commit con todo: "formatear C++ + agregar modulo BankManager" |
| 08:30 | amend → `b4b2de6` |
| 08:31 | `reset` de vuelta a `e5e4c83` (los dos quedan colgados del reflog) |
| 08:33 | re-creación como **18 commits** (16 de formato por módulo + docs + BankManager) |
| 10:24 | push directo con los 20 (incluye `02ad1b0` y `e5e4c83`, hechos antes de madrugada) |

## Qué midió la auditoría

1. **Nada se perdió en el reset/amend.** `git diff e5e4c83 b4b2de6` y
   `git diff e5e4c83 a2eb333` son **idénticos byte a byte** (34 470 líneas
   cada uno): el split en 18 conserva exactamente el contenido del amend.
   La única diferencia entre `a0f8ffb` y `b4b2de6` toca 9 archivos de
   `BankManager/` (iteración propia del turno), no hay contenido ajeno perdido.
2. **Los 16 commits de "formatear con clang-format" están bien delimitados por
   módulo**: ningún commit toca archivos fuera de su módulo (salvo `docs/`,
   declarado en el mensaje).
3. **Pero no todos son formato puro.** Prueba: por cada archivo modificado se
   comparó el *multiset de identificadores* antes/después (el formateo no puede
   cambiar identificadores; unión de líneas tampoco). De 140 archivos C++:
   13 cambian tokens. Tras inspección manual:
   - **5 son legítimos de clang-format**: comentarios de cierre de namespace
     (`FixNamespaceComments`) y orden de includes (`DspCore.h`, `DspDebug.h`,
     `DspMath.h`, `VoiceAllocator.h`, `AudioMidiInterfaceDetector.h`).
   - **8 contienen contenido real** (WIP ajeno incluido, ver abajo).
   *Límite del método:* un cambio de prosa en comentarios que no añade ni quita
   identificadores pasaría el filtro; los 8 hallazgos se confirmaron a ojo.
4. **El push dejó la CI roja** (run `37435921486`, 08:24 UTC):
   - `Tools (node:test)` — `not ok 18` del Guard de `.gitattributes`.
   - `Audit de fuentes (trinquete)` — el módulo BankManager entró con fuentes
     sin consumidor fuera de la línea base.
   Los otros dos workflows salieron verdes. PR #1 (`d68da75`, 11:34) puso todo
   en verde 70 minutos después. **Master está verde hoy** (últimos runs 14:49:
   success).

## El WIP ajeno que quedó incluido

Contenido real que viajó DENTRO de commits que dicen ser solo formateo:

| commit | archivo(s) | qué es |
|---|---|---|
| `d922829` (DspEffects) | `DspSchroederReverb.h`, `DspEffectsTests.cpp` | **Cambio de comportamiento**: quita el clamp `decay_ = jmin(decay, 1.0f)` de `setDecay`; renombra el test `testSchroederKnobsAreClamped` → `testMandosDelBarridoSinRecorte` (y su llamada en el runner); reescribe la doc del método |
| `d5dabaf` (HardwareDrivers) | `SysExCodec.h`, `SysExCodec.cpp` | **Feature nueva**: orden collector Korg (`pack8to7Korg`/`unpack7to8Korg`/`pack8to7KorgPadded`), variantes `pack8to7Dm`, `pack8to7NoPad`, `pack8to7Padded`, `unpack7to8Tolerant`, enum de orden de bits + documentación completa |
| `bcaa296` (HardwareMidiDetect y docs) | `USAGE.md`, `ARCHITECTURE_SPEC.md` | Renombre de rutas `ABDSharedAssets/contracts/hardware/` → `ABDSharedAssets/contracts/` (8 sitios) |
| `2e6593d` (deprecated y docs) | `_Deprecados/LutDSP-JunoBBD.h`, `docs/homonimias-cabeceras.md` | Reescritura grande de la nota del módulo deprecado + edición de homonimias |
| `02ad1b0` (scope documental) | `tools/ds_effects_mutation_bank.py` | Poda de 3 mutaciones `schroeder-*` — el mismo WIP de quitar los clamps (sí lo anuncia el mensaje, pero es contenido funcional) |

## Reconciliación: qué se verificó y qué NO se tocó

Verificaciones de coherencia del WIP incluido (todas pasan):

- El test nuevo existe y está cableado (`DspEffectsTests.cpp:2719` y `:6061`);
  el test viejo solo se menciona en un comentario que explica el cambio de signo.
- El renombre `contracts/hardware` está **completo**: 0 referencias residuales
  en este repo ni en ABDBankManager.
- `tools/ds_effects_mutation_bank.py` compila (`py_compile` = 0) y las 3 claves
  podadas no quedan referenciadas.
- El nuevo `SysExCodec.cpp` **sí se compila**: `ABDSharedCode/CMakeLists.txt:317`
  lo mete en el target que consume ABDBankManager, cuya CI está verde
  (Build Verification y C++ Unit Tests en el run post-fusión de ABDBankManager).
- El job `Clang-format` está verde sobre estos archivos desde el propio push.
- Master: verde en los 3 workflows.

Decisiones:

1. **No se reescribe la historia.** Los 20 commits están publicados y encima se
   aplastaron 3 PRs (#1, #2, #3); un force-push rompería SHAs ya referenciados.
   El mezclado queda **documentado aquí** en vez de corregido en el pasado.
2. **El WIP ajeno sin commitear no se toca.** El worktree de `ABDSharedCode`
   tiene ~57 entradas sucias ajenas (TS de `BankManager/Contracts`, `Scope/`,
   `CMakeLists.txt`). Se comprobó que son **disjuntas** de los 20: el único
   archivo solapado es `CMakeLists.txt`, y en hunks distintos (el commit
   añadió el bloque BankManager en la línea 378; el sucio añade el bloque Scope
   en la 710).
3. **El push local sin subir tampoco se toca**: `master` local está 42 commits
   por delante de `origin/master` con la absorción de `Scope/` (`dd03379`),
   todavía en curso.

## Lección para la próxima

- Un `git add -A` + reset/split **arrastra todo lo que haya en el worktree**:
  el formato por módulo heredó contenido ajeno sin que se viera en los mensajes.
- El multiset de identificadores (comando abajo) detecta el contenido semántico
  en segundos antes de un push:

```bash
for c in <commits>; do for f in $(git diff --name-only $c^ $c); do
  diff <(git show $c^:$f | grep -oE '[A-Za-z_][A-Za-z_0-9]*' | sort | uniq -c) \
       <(git show $c:$f   | grep -oE '[A-Za-z_][A-Za-z_0-9]*' | sort | uniq -c) \
    >/dev/null || echo "$c CAMBIA TOKENS $f"
done; done
```

- Y como ya manda este repo: cambios por PR, nunca push directo a `master`.
