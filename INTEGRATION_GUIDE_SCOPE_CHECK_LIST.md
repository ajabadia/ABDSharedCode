# Checklist de integridad del bloque `## Módulo: Scope` del guía

**Archivo**: `REPLACE_ME/INTEGRATION_GUIDE.md`
**Sección**: línea REPLACE_ME del todo el guía.
**Versión revisada**: REPLACE_ME.

## Colas del bloque Scope (listado de verificación que queda pendiente)
- [ ] Encabezado `## Módulo: Scope` presente y único en su nivel (subsección, y luego `## Integración Ecosistema` no dentro).
- [ ] Párrafo de introducción donde se explica que Scope vino del hermano ABDScope y es subtree aquí desde v0.4.0.
- [ ] Título `### Integración en CMake`.
- [ ] Bloque cmake de ejemplo real (target_link_libraries(TuProyecto PRIVATE ABDShared::ScopeCore)).
- [ ] Explicación breve de por qué no se puede hacer `add_subdirectory(../ABDScope)` / FetchContent duplicando el subtree (doble registro -> error de CMake).
- [ ] Explicación del orden: JUCE antes de ABDSharedCode para que el binary data funcione en el consumer.
- [ ] Tabla de tres aliases con descripción:
  - `ABDShared::ScopeCore` (core + glue JUCE + WebUI embebida, el que enlaza un producto).
  - `ABDShared::ScopeCoreHeaders` (core header-only sin JUCE, tests/tools).
  - `ABDShared::ScopeWebAssets` (binarios de WebUI, solo si juce_add_binary_data existe; sin JUCE la WebUI se sirve por filesystem).
- [ ] Nota EMSCRIPTEN: el glue JUCE no se enlaza en WASM pero el core header-only sigue disponible.
- [ ] `### Componentes` con listado de las clases reales del módulo (ScopeDataCollector, ScopeTap/TapId/ScopeTapType, ScopeFrameSerializer, TriggerDetector/SpscRingBuffer, JuceWebScopeComponent, ScopeResourceProvider).
- [ ] `### Ejemplo de Uso` con bloque cmake real + bloque cpp real (JuceWebScopeComponent, setTheme, activación de taps).
- [ ] Break `---` después de la sección Scope.
- [ ] El `## Integración Ecosistema` no está dentro de la sección Scope (es siguiente módulo).
- [ ] El ejemplo de uso copia código real del consumidor (MS2000 o ABDAudioLab), no pseudo-código.
- [ ] Se puede verificar que la sintaxis del bloque cmake es correcta tipográficamente (no faltan paréntesis, backticks, etc).
- [ ] El lenguaje y tono siguen el estilo del resto del guía (spanish, con notas técnicas).
- [ ] No hay typo ni rotura de linea dentro de los bloques de ejemplo (sin ~ backtick aislado, sin código cortado).
- [ ] Comprobación de integridad: copiar el bloque completo del guía y pegarlo en un archivo temporal; revisar que no falta ningún bloque de la plantilla.

# Detalles adicionales del interfaz (más allá del estructural)
- [ ] La página entero tiene las secciones de módulos en el misma plantilla (AutoUpdater, DspCore+DspEffects, MidiKeyboard, HardwareMidiDetect, HardwareDrivers, AudioComparator, LutDSP, y ahora Scope).
- [ ] El bloque de Scope mantiene la coherencia con el bloque de DspCore como hermano ("familia de osciladores" vs "familia de filtros").
- [ ] La sección de Scope refleja el espacio del Scope submodule (Scope/ en el repositorio actual).
- [ ]La guía integra el Scope como un módulo compartido con una sola fuente de verdad.
- [ ] Se deja la nota sobre el repo hermano ABDScope arqueándose eventualmente (opcional pero útil).
- [ ] Se deja el párrafo de "si funciona funciona siempre igual para todos".
- [ ] El bloque se puede verificar en esté repositorio (no se necesita otro contexto).
