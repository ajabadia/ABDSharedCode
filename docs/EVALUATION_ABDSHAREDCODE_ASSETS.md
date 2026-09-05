# Evaluación: ABDScope → ABDSharedCode y relación con ABDSharedAssets

**Fecha:** 5 de septiembre de 2026
**Estado:** Documento de evaluación y decisión. **Fase 0 cerrada (2026-09-05):** decisión A1 + versionado + destino de repo + política de servido registradas en §7 y en `HANDOFF.md`. **Fase 1 (assets) ejecutada 2026-09-05, sin commitear** (ver §6).
**Alcance:** ABDScope (este repo), `ABDSharedCode`, `ABDSharedAssets` y los consumidores conocidos (`ABDMS2000`, `ABDAudioLab`)

---

## 1. Objetivo

Responder con orden a dos preguntas:

1. **¿Llevamos ABDScope a ABDSharedCode?** Es decir, ¿absorbemos este proyecto como un módulo más de la librería de código compartido, siguiendo el precedente que ya marca `MidiKeyboard/`?
2. **¿Usamos los activos de ABDSharedAssets en el WebUI del scope, o enriquecemos los que ya existen allí?**

Se revisa el estado actual de los tres repositorios con evidencia verificada y se propone un plan por fases con criterios de "hecho" y riesgo controlado.

---

## 2. Estado verificado (revisión del 2026-09-05)

### 2.1 ABDScope (este repo, v0.3.1) — sano

- **Tests:** 56/56 tests JS en verde (Vitest, `npm test` en raíz; 7 ficheros: trigger, frame, input, renderers, scope, lane, smoke).
- **Arquitectura:** capas limpias y dentro de los límites autoimpuestos (máx. 200 líneas JS / 300 C++):
  - `WebUI/src/` — motor (scope/frame/trigger), inputs (Analyser/Push), renderers, mounts, CSS de tokens.
  - `Source/Core/` — C++20 puro header-only lock-free (SPSC multi-tap, serializador, trigger).
  - `Source/JUCE/` — pegamento JUCE (`JuceScopeComponent`, `JuceWebScopeComponent`, `ScopeResourceProvider`).
  - `WebUI/tests/` + `Source/tests/StandaloneSmoke.cpp`.
- **CMake actual (working tree, sin commitear):** división ya hecha de
  `ABDScopeCoreHeaders` (INTERFACE, **sin JUCE**, incluye solo `Source/`+`Source/Core`) y
  `ABDScopeCore` (INTERFACE, enlaza `ABDScopeCoreHeaders` + fuentes de `Source/JUCE/`).
  El smoke test enlaza solo `ABDScopeCoreHeaders`. Alias públicos: `ABDScope::ABDScopeCore` y `ABDScope::ABDScopeWebAssets`.
  WebUI embebible vía `juce_add_binary_data` (cuando el llamador tiene JUCE en scope): embebe `WebUI/src/*` + `index.html`, excluyendo demo/tests/node_modules.
- **Consumidores conocidos hoy:**
  - `ABDMS2000`: `add_subdirectory ../ABDScope` **local obligatorio** (`FATAL_ERROR` si no existe; sin fallback FetchContent). WebUI consumido por **copiado** a `WebUI/abdscope` vía script `sync_scope.js` (patrón raw-ESM + Vite dev).
  - `ABDAudioLab`: `add_subdirectory ../ABDScope EXCLUDE_FROM_ALL` + **fallback FetchContent** a `github.com/ajabadia/ABDScope`.
  - `ABDCZ101`, `ABDEep`, `ABDJUNiO601`, `ABDPro008`: consumidores planificados según ROADMAP/HANDOFF (Fase 5), aún no integrados.

### 2.2 ABDSharedCode — en migración activa

- Módulos CMake actuales: `ABDShared::AutoUpdater` (librería estática, `juce_core`+`juce_events`) y `ABDShared::HardwareMidiDetect` (INTERFACE, `juce_audio_devices`+`juce_gui_extra`, con `juce_add_binary_data` condicional para su `WebUI/index.html`).
- **`HardwareMidiDetect` se construyó "estilo ABDScope"**: reimplementa el mismo andamiaje WebView2 (resource provider, componente WebBrowserComponent, WebUI embebido condicional). Es código duplicado de patrón respecto a `Source/JUCE/` de ABDScope.
- **`MidiKeyboard/` está sin commitear** dentro de ABDSharedCode (creado 2026-09-04): componente WebUI completo (README/CHANGELOG/HANDOFF/ROADMAP + `src/` + `demo/` + `tests/` + `vitest.config.js`) ensamblado a partir de ABDMS2000/ABDEep/ABDCZ101. **Precedente directo de "componente reutilizable absorbido en ABDSharedCode".**
- Además hay modificaciones sin commitear en `AutoUpdater/AutoUpdater.cpp`, `HardwareMidiDetect/*` y `HardwareMidiDetect/WebUI/index.html`.

### 2.3 ABDSharedAssets — en migración activa

- Contenido: `styles/` (tokens.css con 120+ variables, 6 temas: ms2000, cz101, deepmind, juno, audiolab, audiolab-light; `index.css` bundle; `components/`: panels, buttons, controls, navbar, lcd, scope, keyboard), `icons/`, `contracts/`, `models/`, `brands/`, `demo/`, `docs/`.
- **Sin commitear:** `package.json` (`@abdsynths/shared` npm), `demo/proto/` (prototipo Vite), junction `abdbank/`, `node_modules/`, README y `docs/INTEGRATION_GUIDE.md` modificados.
- Consumo actual por parte de los hosts: junction/symlink (`node_modules/@abdsynths/shared → ABDSharedAssets` en ABDMS2000) y cascada de import `tokens → tema → overrides host` vía Vite.

---

## 3. Duplicaciones y fricciones reales detectadas

| # | Duplicación / fricción | Evidencia | Dónde resolverla |
|---|---|---|---|
| 1 | **Patrón WebView2 duplicado**: ABDScope (`ScopeResourceProvider` + `JuceWebScopeComponent`) y HardwareMidiDetect (`HardwareMidiPickerResourceProvider` + `JuceHardwareMidiPicker`) implementan lo mismo por separado | Comparación de cabeceras de ambos resource providers | ABDSharedCode: extraer base `ABDShared::WebView2Bridge` |
| 2 | **Paletas de tema duplicadas**: `WebUI/src/scope.css` hardcodea bloques `[data-theme=ms2000/cz101/deepmind/audiolab(+dark)/audiolab-light(+light)]` con los mismos colores que `ABDSharedAssets/styles/themes/*.css` definen como `--color-*` | scope.css líneas 25–80 vs temas compartidos | scope.css consume tokens; paleta única en ABDSharedAssets temas |
| 3 | **Adaptador compartido desincronizado**: `ABDSharedAssets/styles/components/scope.css` (17 líneas) declara un subconjunto de tokens `--scope-*` que no coincide con el set real del scope: le faltan `--scope-accent`, `--scope-accent-hover`, `--scope-surface-elevated` y usa `--scope-font` (el scope usa `--scope-font-lcd`) | Comparación directa de ambos ficheros | Enriquecer el adaptador (ver §5) |
| 4 | **Iconos duplicados**: el scope incrusta SVGs inline en `mountDom.js`/`LaneView.js` (cámara, cerrar ×, freeze, play) y ABDSharedAssets ya tiene `icons/camera.svg, close.svg, freeze.svg, oscilloscope.svg, play.svg, spectrum.svg` (coincidencia 1:1 con los controles del scope) | grep de SVG en WebUI/src vs `ls icons/` | Fuente única en ABDSharedAssets/icons + módulo generado |
| 5 | **Recetas de integración distintas por consumidor**: MS2000 exige `../ABDScope` local sin fallback; AudioLab usa local + FetchContent. ABDSharedCode ya tiene el patrón local-primero + FetchContent estandarizado | CMakeLists de MS2000 (líneas 40–70) y de AudioLab | Unificar receta (ABDSharedCode) |
| 6 | **Copia de WebUI por sync script** (`sync_scope.js` en MS2000) en vez de junction/embed: mecanismo de servido del WebUI aún no único | docs/INTEGRATION_GUIDE §1.1 vs §1.3 | Formalizar política de servido en ABDSharedCode |

---

## 4. Pregunta A — ¿Llevar ABDScope a ABDSharedCode?

### 4.1 Argumentos a favor

1. **Ya es una librería compartida de facto**: se consume con `add_subdirectory`/FetchContent, igual que los módulos de ABDSharedCode. El GUI demo es un harness de desarrollo, no un producto.
2. **Precedente y momento**: `MidiKeyboard/` se está absorbiendo en ABDSharedCode ahora mismo; HardwareMidiDetect ya vive allí y copió el patrón del scope. Centralizar permite extraer el andamiaje WebView2 común en lugar de una tercera reimplementación.
3. **Una sola receta de integración**: el consumidor tendría un único bloque `ABDSHARED_CODE_DIR` local-primero + FetchContent, ganando AutoUpdater + HardwareMidiDetect + Scope sin recetas paralelas (elimina la fricción #5).
4. **Frontera de módulo ya lista**: el working tree ya separa core sin JUCE (`ABDScopeCoreHeaders`) del pegamento JUCE — encaja con la estructura de `AutoUpdater`/`HardwareMidiDetect`.

### 4.2 Costes y riesgos

1. **Historia git**: ABDScope tiene 37 commits y repo propio (`github.com/ajabadia/ABDScope`) con versionado semver (0.3.1). Una copia sin historia (como la de MidiKeyboard) perdería trazabilidad. Si se mueve, debe ser con historia (`git subtree`/merge) o manteniendo `ABDScope.git` como repo canónico y ABDSharedCode consumiéndolo como peer.
2. **Renombrado de targets**: `ABDScope::ABDScopeCore*` → `ABDShared::Scope*`. Hay que actualizar MS2000 y AudioLab **en el mismo cambio** y mantener aliases de compatibilidad durante la transición.
3. **Cadencia y versionado**: ABDSharedCode versiona grueso (`master` + tags ocasionales); ABDScope tiene semver propio, changelog y dos toolchains Vitest. Hay que fijar política de tags y dónde corren los tests del WebUI del scope tras el traslado.
4. **Política de servido del WebUI**: hoy conviven copia/sync (MS2000), embed binario (JUCE vía `juce_add_binary_data`) y junction. ABDSharedCode debe formalizar cuándo se embebe y cuándo se sirve desde disco/ABDSharedAssets (fricción #6).
5. **Crecimiento del repo compartido**: mezcla módulos C++ con componentes WebUI (MidiKeyboard + Scope) en un mismo árbol CMake; exige disciplina de módulos (una carpeta = un módulo = sus docs/tests propios).

### 4.3 Opciones

| Opción | Descripción | Cuándo elegirla |
|---|---|---|
| **A1 — Absorber con historia** | Mover `Source/` + `WebUI/` a `ABDScope/` dentro de ABDSharedCode (subtree para conservar historia), targets `ABDShared::Scope*`, aliases de compatibilidad `ABDScope::*` durante la transición, actualizar consumidores en el mismo cambio | Cuando se confirme que ABDScope no necesita cadencia ni repo propios (recomendada si el objetivo es "un solo sitio para todo el código compartido") |
| **A2 — Consumir como peer (estatus quo mejorado)** | ABDScope sigue en su repo, pero se documenta e integra con la misma receta local-primero + FetchContent que ABDSharedCode, y ambos comparten el andamiaje WebView2 extraído en ABDSharedCode | Cuando se valore más la independencia de versionado del scope (WebUI grande con toolchains propias) |
| **A3 — Híbrido** | El core C++ puro (header-only, sin JUCE) pasa a ABDSharedCode; el WebUI + pegamento JUCE se queda en ABDScope | Solo si el WebUI se quiere versionar/pubicar por separado como paquete npm |

**Recomendación:** A1, ejecutada por fases (ver §6), con estas condiciones innegociables:
historia conservada, consumidores actualizados en el mismo cambio, tests del WebUI verdes antes y después, y aliases de compatibilidad durante al menos una release.

---

## 5. Pregunta B — ¿Usar los activos de ABDSharedAssets o enriquecerlos?

**Respuesta corta: enriquecer el adaptador y los iconos existentes, sin mover el CSS de componentes ni forzar dependencia del sistema compartido en el WebUI del scope.**

### 5.1 Reparto de propiedad objetivo (patrón ya implícito en MidiKeyboard ↔ `keyboard.css`)

- **El módulo (ABDScope/ABDSharedCode) es dueño** del código y del CSS completo de su componente:
  `WebUI/src/scope.css` sigue siendo el CSS canónico (layout, lanes, botones, renderers).
- **ABDSharedAssets es dueño** de los *adaptadores* de tema y de la *fuente* de activos gráficos:
  - `styles/components/scope.css` = adaptador fino que mapea los tokens `--scope-*` del módulo a los tokens del host (`--color-accent`, `--color-panel-*`, etc.) con fallbacks. Nunca contiene estilos de layout del componente.
  - `icons/*.svg` = fuente única de iconografía vectorial monocromática.
- **Regla de oro:** ABDSharedAssets solo aloja activos con **≥2 consumidores**; los activos de un solo consumidor viven con el consumidor (p. ej. `exported_luts/`, vacío y solo del scope, se queda en el scope).

### 5.2 Acciones concretas

1. **Enriquecer `ABDSharedAssets/styles/components/scope.css`** hasta cubrir el set real de tokens del scope:
   - Añadir `--scope-accent`, `--scope-accent-hover`, `--scope-surface-elevated`.
   - Unificar el nombre de fuente (`--scope-font` compartido → el scope debe consumirlo; o declarar alias `--scope-font-lcd: var(--scope-font, ...)`).
   - Mantener fallbacks oscuros propios (estilo actual) para que el adaptador funcione sin tema de host.
2. **Refactorizar `WebUI/src/scope.css`**:
   - Eliminar los bloques `[data-theme=...]` que duplican las paletas de `ABDSharedAssets/styles/themes/*.css` (fricción #2). El color llega por la cascada del host (temas compartidos + adaptador); el scope mantiene **solo** los fallbacks por defecto en `:root` para funcionar standalone (demo/sin sistema compartido).
   - Verificar que los hosts actuales (MS2000 ya importa la cascada `@abdsynths/shared`) siguen pintando igual; ajustar `demo/` con un fallback oscuro explícito.
3. **Iconos**: los SVG canónicos viven en `ABDSharedAssets/icons/`. El scope renderiza desde un módulo de iconos generado a partir de esos SVG, con un **test de paridad** (los paths del módulo coinciden con los ficheros canónicos) para que no vuelvan a divergir. El modo embed (binary data) sigue autocontenido: el módulo de iconos se genera en tiempo de desarrollo, no se lee de disco en runtime.
4. **Paletas de espectrograma/LUTs**: hoy son de un solo consumidor (scope) → permanecen en el scope. Promover a ABDSharedAssets solo cuando aparezca un segundo consumidor.
5. **Documentación**: actualizar `docs/ICONS_GUIDE.md` y `docs/STYLES_GUIDE.md` de ABDSharedAssets con el patrón módulo↔adaptador y la regla de ≥2 consumidores.

> **Nota de no-regresión:** el WebUI del scope debe seguir funcionando **sin** ABDSharedAssets (standalone web/demo, WASM y consumidores que no adopten el sistema de diseño). Por eso el CSS del módulo conserva fallbacks y la adopción del adaptador compartido es *aditiva* por parte del host.

---

## 6. Plan de ejecución ordenado (fases con criterio de "hecho")

Cada fase es reversible o contiene su propio rollback; ninguna fase posterior empieza sin cerrar la anterior.

### Fase 0 — Fijar decisiones (sin código) ✅
- [x] Decidir A1/A2/A3 (§4.3) y política de versionado de ABDSharedCode (tags vs master).
- [x] Aprobar este documento como referencia.
- **Hecho cuando:** decisión registrada en este doc + HANDOFF. — ✅ Registrado 2026-09-05 (ver §7) en este doc y en `ABDScope/HANDOFF.md` §6.

### Fase 1 — Enriquecer assets (bajo riesgo, sin tocar consumidores) — ejecutada 2026-09-05
- [x] Enriquecer `ABDSharedAssets/styles/components/scope.css` al set completo de tokens (5.2.1). **Extensión decidida por el dueño:** mismo tratamiento para el adaptador del teclado `keyboard.css` (21 tokens `--kbd-*`; los de estado runtime tipo `--kbd-pressure`/`--kbd-velocity` se documentan como no themables) por la migración paralela de ABDKeyb a ABDSharedCode + ABDSharedAssets.
- [x] Refactorizar `WebUI/src/scope.css`: bloques `[data-theme=…]` eliminados; `:root` solo con fallbacks canónicos oscuros (16 tokens, incl. `--scope-header-bg` y `--scope-grid-center`) (5.2.2).
- [x] Generar módulo de iconos del scope desde `ABDSharedAssets/icons/` + test de paridad (5.2.3): `WebUI/src/icons.js` (`camera`, `close`, `freeze`) + `WebUI/tests/icons.test.js` (paridad normalizando BOM/CRLF; auto-skip fuera del workspace ABDSynths).
- **Decisiones asociadas (2026-09-05):** demo standalone `WebUI/demo/` **eliminado** (carpeta + referencias CMake/vitest/start.bat/docs); `index.html` (host page embed) con default oscuro `ms2000`; política de iconos: **todos** los iconos viven en `ABDSharedAssets/icons/` y cada consumidor incrusta solo los que usa (módulo generado + paridad, cero SVG duplicados en código de módulos).
- **Hecho cuando (parcial):** tests JS **59/59** verdes en ambas toolchains (eran 56/56; +3 del test de paridad de iconos). El criterio "demo standalone con/sin cascada" queda sustituido —el demo ya no existe— por **host parity**: MS2000/AudioLab adoptando la cascada compartida + revisión visual, pendiente en Fases 2–3.
- **Riesgo:** bajo (ejecutado). **Rollback:** revertir estos cambios (en ABDScope toca solo la retirada del demo en CMake/vitest/start.bat y el refactor CSS/iconos; consumidores intactos).

### Fase 2 — Extraer andamiaje WebView2 común en ABDSharedCode
- [ ] Crear módulo base (resource provider + componente WebBrowserComponent + utilidades de embed condicional).
- [ ] Migrar HardwareMidiDetect a la base.
- **Hecho cuando:** HardwareMidiDetect compila y funciona sin duplicar el andamiaje.
- **Riesgo:** medio (JUCE/WebView2). Se hace ANTES de mover el scope para que el traslado del scope no arrastre duplicación.

### Fase 3 — Absorber ABDScope en ABDSharedCode (solo si A1)
- [ ] Mover con historia (`git subtree` o merge) a `ABDSharedCode/Scope/` (Source/ + WebUI/ + docs del módulo).
- [ ] Registrar targets `ABDShared::ScopeCoreHeaders`, `ABDShared::ScopeCore`, `ABDShared::ScopeWebAssets` en el CMake orquestador (patrón HardwareMidiDetect) + aliases `ABDScope::*` de compatibilidad.
- [ ] Actualizar MS2000 y AudioLab al nuevo target en el **mismo** cambio; eliminar la ruta `FATAL_ERROR`-sin-fallback.
- [ ] Verificar tests del WebUI (ambas toolchains) y smoke C++ en el nuevo emplazamiento.
- **Hecho cuando:** MS2000 y AudioLab compilan enlazando `ABDShared::Scope*`; tests verdes; `ABDScope.git` queda marcado como obsoleto/archivo (no borrado).
- **Riesgo:** alto si se hace de golpe → por eso va al final y con aliases.

### Fase 4 — Cierre
- [ ] Actualizar ROADMAP/HANDOFF/CHANGELOG de ABDScope y de ABDSharedCode.
- [ ] Eliminar aliases de compatibilidad tras una release de margen.
- [ ] Promover assets a ABDSharedAssets solo cuando tengan ≥2 consumidores.

---

## 7. Decisiones resueltas (2026-09-05, dueño del ecosistema ABDSynths)

1. **A1 — Absorber completo con historia.** ABDScope completo (`Source/` + `WebUI/`) pasa a `ABDSharedCode/Scope/` conservando historia git (`git subtree`/merge). ABDScope no mantiene repo GitHub propio: la historia viaja dentro de ABDSharedCode, que se publicará como repo. El repo local de ABDScope se conserva **intacto** hasta que la migración esté completa (por seguridad); después se archiva, **nunca se borra**.
2. **Versionado de ABDSharedCode: tags semver por módulo** (p. ej. `scope-v0.4.0`, `midikeyboard-v1.0.0`); no un tag único global.
3. **Destino de `ABDScope.git`: archivar como obsoleto** tras el cierre (Fase 4). No canónico, no borrado.
4. **Política de servido del WebUI: contrato único, mecanismo por host.** La fuente canónica del WebUI vive en el módulo (un commit = un resultado idéntico para cualquier host). El mecanismo de servido es decisión de cada host según su toolchain: **MS2000 → Vite/disco** (en curso); **AudioLab → embed en binary data** vía `juce_add_binary_data` (JUCE puro, sin Vite). El embed sigue disponible como modo autocontenido para distribuciones. La paridad de resultado se garantiza sirviendo el mismo source-set del módulo; el módulo documenta el mapeo host → mecanismo.

---

## 8. Apéndice — Evidencia verificada

- `npm test` en ABDScope → 56/56 tests, 7 ficheros, Vitest 2.1.9 (2026-09-05).
- `git status` ABDScope → 4 ficheros modificados sin commitear: `CMakeLists.txt`, `Source/CMakeLists.txt` (split CoreHeaders/Core), `docs/INTEGRATION_GUIDE.md`, `docs/USAGE_GUIDE.md`.
- `git status` ABDSharedCode → modificados: `AutoUpdater/AutoUpdater.cpp`, `HardwareMidiDetect/*` (5 ficheros + WebUI/index.html); **sin trackear: `MidiKeyboard/`**.
- `git status` ABDSharedAssets → modificados: README, docs/INTEGRATION_GUIDE; **sin trackear: `package.json`, `demo/proto/`, `abdbank/` (junction), `node_modules/`**.
- Consumidores con referencia a ABDScope: `ABDMS2000/CMakeLists.txt` (local `../ABDScope`, sin fallback) y `ABDAudioLab/CMakeLists.txt` (local + FetchContent `github.com/ajabadia/ABDScope`).
- `ABDSharedAssets/icons/` contiene: `camera.svg, close.svg, freeze.svg, oscilloscope.svg, play.svg, spectrum.svg`.
- `WebUI/src/scope.css`: 505 líneas; bloques `[data-theme=]` para ms2000, cz101, deepmind, audiolab(+dark), audiolab-light(+light). `ABDSharedAssets/styles/themes/` contiene 6 temas (incluye juno, ausente en scope.css).
- Andamiaje duplicado: `ABDScope/Source/JUCE/ScopeResourceProvider.{h,cpp}` (namespace `abd::scope`) vs `ABDSharedCode/HardwareMidiDetect/HardwareMidiPickerResourceProvider.{h,cpp}` (namespace `abd::hwid`).
