# Prompt de continuación — Sesión desde D:\desarrollos\ABDSynths

> Pega este texto como primer mensaje en la nueva conversación, abierta con el directorio de trabajo en `D:\desarrollos\ABDSynths` (nivel padre, con acceso a TODOS los repos de la suite).

---

Estamos evaluando y ejecutando la consolidación del ecosistema ABDSynths. Trabaja en español conmigo. Arranca **leyendo primero** este documento de evaluación (lo escribió la sesión anterior y es la referencia de la decisión):

**`ABDScope/docs/EVALUATION_ABDSHAREDCODE_ASSETS.md`** — léelo completo antes de tocar nada.

## Contexto (resumen verificado por la sesión anterior)

Tres repositorios hermanos en este directorio, todos accesibles ahora:

1. **ABDScope** (`./ABDScope`) — visualizador/analizador de audio universal (v0.3.1): core C++20 header-only lock-free en `Source/Core/` (sin JUCE), pegamento JUCE en `Source/JUCE/`, WebUI ES-modules en `WebUI/src/` con 56/56 tests Vitest en verde y smoke test C++.
2. **ABDSharedCode** (`./ABDSharedCode`) — librería de código compartido con módulos CMake (`ABDShared::AutoUpdater`, `ABDShared::HardwareMidiDetect`) y consumo local-primero + FetchContent.
3. **ABDSharedAssets** (`./ABDSharedAssets`) — activos compartidos: tokens CSS, 6 temas, adaptadores de componentes (`styles/components/scope.css`, `keyboard.css`), iconos SVG (`icons/`), contratos, modelos. Empaquetado npm `@abdsynths/shared` en curso.

Consumidores conocidos de ABDScope: **ABDMS2000** (local `../ABDScope`, sin fallback FetchContent, WebUI copiado a `WebUI/abdscope` por sync script) y **ABDAudioLab** (local + fallback FetchContent a `github.com/ajabadia/ABDScope`).

## Decisión tomada (confírmala conmigo al empezar)

La sesión anterior **recomendó y confirmó la opción A1**: absorber ABDScope dentro de ABDSharedCode como módulo (`ABDSharedCode/Scope/`), conservando historia git, con targets `ABDShared::Scope*` + aliases de compatibilidad `ABDScope::*`, actualizando consumidores en el mismo cambio. Si yo te digo otra cosa (A2/A3), seguimos la opción que indique.

Quedan pendientes de mi decisión (pregúntamelos al inicio, en una sola tanda):
1. ¿A1, A2 o A3 definitivo?
2. Política de versionado de ABDSharedCode (tags semver por módulo vs `master`).
3. Destino de `ABDScope.git` tras la absorción (archivar vs mantener como repo canónico).
4. Política de servido del WebUI del scope (embed en binary data siempre vs disco/junction como modo oficial).

## Orden de trabajo (plan del documento de evaluación, §6)

- **Fase 0 — Fijar decisiones**: confirmar los 4 puntos anteriores conmigo y registrarlos en el doc de evaluación + HANDOFF. Sin código.
- **Fase 1 — Enriquecer assets (bajo riesgo)**: enriquecer `ABDSharedAssets/styles/components/scope.css` al set completo de tokens que usa el scope (faltan `--scope-accent`, `--scope-accent-hover`, `--scope-surface-elevated`; unificar `--scope-font` vs `--scope-font-lcd`); refactorizar `ABDScope/WebUI/src/scope.css` para quitar los bloques `[data-theme=...]` que duplican paletas de los temas compartidos (mantener solo fallbacks en `:root` para el modo standalone); generar módulo de iconos del scope desde `ABDSharedAssets/icons/` con test de paridad. Hecho cuando: 56/56 tests verdes y demo standalone pinta igual con y sin cascada compartida.
- **Fase 2 — Extraer andamiaje WebView2 común en ABDSharedCode**: el patrón resource-provider + componente WebBrowserComponent está duplicado entre `ABDScope/Source/JUCE/` y `ABDSharedCode/HardwareMidiDetect/`; extraer una base compartida y migrar HardwareMidiDetect a ella (ANTES de mover el scope).
- **Fase 3 — Absorber ABDScope en ABDSharedCode** (solo si A1): mover con historia (`git subtree` o merge) a `ABDSharedCode/Scope/`, registrar targets en el CMake orquestador (patrón HardwareMidiDetect), actualizar MS2000 + AudioLab en el mismo cambio, verificar tests (ambas toolchains Vitest + smoke C++) desde la nueva ubicación. Hecho cuando: los dos consumidores compilan enlazando `ABDShared::Scope*` y `ABDScope.git` queda marcado como obsoleto (nunca borrado).
- **Fase 4 — Cierre**: actualizar ROADMAP/HANDOFF/CHANGELOG de ABDScope y ABDSharedCode; eliminar aliases tras una release de margen; promover assets a ABDSharedAssets solo cuando tengan ≥2 consumidores.

## Reglas de trabajo obligatorias

- **No toques ni commitees nada sin preguntarme antes.** Hay trabajo en curso SIN commitear en los tres repos (p. ej. en ABDScope: split CMake `ABDScopeCoreHeaders`/`ABDScopeCore` ya hecho en el working tree; en ABDSharedCode: `MidiKeyboard/` sin trackear; en ABDSharedAssets: `package.json`, `demo/proto/`, junction `abdbank/`). Respeta ese estado; no hagas commits "de limpieza".
- Conserva las convenciones de los repos: ficheros JS ≤200 líneas, C++ ≤300, CSS solo con custom properties (`--scope-*`, `--color-*`), cero emojis en UI, inglés técnico en código, docs en español/inglés según cada repo. NO elimines BOM/CRLF de los docs que los llevan (`docs/INTEGRATION_GUIDE.md`, `docs/DATA_CONTRACT.md`, `CHANGELOG.md` de ABDScope).
- Tests: `npm test` en la raíz de ABDScope (56/56) y también la toolchain de `ABDScope/WebUI/`. Manténlos verdes antes y después de cada fase.
- Zero-copy: los proyectos nunca copian código de ABDSharedCode ni activos de ABDSharedAssets (junction/npm/CMake/sync-script documentado como última opción). ABDSharedAssets solo aloja activos con ≥2 consumidores.
- No ejecutes comandos destructivos ni `git push`/`git rebase` sin permiso explícito.

## Primera acción

1. Lee `ABDScope/docs/EVALUATION_ABDSHAREDCODE_ASSETS.md`.
2. Confirma conmigo los 4 puntos de decisión pendientes.
3. Propón el plan concreto de la Fase 1 (o la fase que decida) antes de escribir código.
