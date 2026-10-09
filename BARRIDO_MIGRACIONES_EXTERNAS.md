# Barrido de migraciones / sustituciones precomprobadas — fuera de ABDSharedCode

> Qué partes de otros proyectos parecen susceptibles de:
> - **sustitución** por lo que ya existe en ABDSharedCode, o
> - **extracción** hacia ABDSharedCode como nuevo módulo compartido.
>
> Criterio usado: evidencia concreta en el árbol montado (importaciones, includes, CMake, lockfiles, docs/roadmap de los repos). Donde no hay evidencia, aparece como "por confirmar", no como lista definitiva.

---

## A. Consumo ya existe (no es candidato a migración, es realidad)

Esto ya está siendo consumido por los demás proyectos.

### A1. `ABDSharedCode/MidiKeyboard` → `@abdsynths/midi-keyb`
**Qué ya consume:**
- `ABDMS2000`
- `ABDNeural/WebUI`
- (también ABDSharedCode mismo como workspace miembro)

Evidencia:
- `ABDMS2000/package-lock.json`, `ABDNeural/WebUI/pnpm-lock.yaml`, `ABDNeural/WebUI/pnpm-workspace.yaml`.

**Implicación:**
Si algún proyecto tiene su propio teclado MIDI en JS y la divergencia es mínima, el candidato más obvio es usar `@abdsynths/midi-keyb`. Si hay fork, hay que decidir si se reemplaza o se mantiene justificado.

---

### A2. `ABDShared::SynthCore` y `ABDShared::DspCore` / `ABDShared::DspEffects`
**Qué ya consume:**
- `ABDMS2000`
- `ABDNeural`

Evidencia:
- `ABDMS2000/CMakeLists.txt` enlaza `ABDShared::SynthCore`, `ABDShared::HardwareDrivers`, `ABDShared::ScopeCore`/`ScopeCoreHeaders`.
- `ABDMS2000/wasm/CMakeLists.txt` usa `target_link_libraries(ms2000_dsp PRIVATE ABDShared::SynthCore)`.
- `ABDNeural/CMakeLists.txt` enlaza `ABDShared::DspCore` y `ABDShared::DspEffects`.
- `ABDMS2000/Source/DSP/*` tiene varios "sync artifact, not hand-maintained — edit ABDSharedCode/SynthCore instead".

**Implicación:**
Esto ya no es "migrar" **si** `ABDShared::DspCore` y `ABDShared::DspEffects` están disponibles como targets reales en `ABDSharedCode` en la versión que cada proyecto integra. En el árbol montado en esta sesión no apareció `DspEffects/` como carpeta presente, por lo que lo más seguro es tratar este punto como “evidencia de consumo documentado/centrado en CMake y docs” y no como certificado de que la superficie de efectos compartida está resuelta y lista para ser usada tal cual.

Si se confirma que el target existe y se puede consumir, el siguiente paso es:
- evitar que surjan gemelos locales,
- alinear nombres de target,
- revisar si hay archivos locales que todavía compilan código que ahora vive en el compartido.

---

### A3. `ABDShared::HardwareDrivers` (SysExCodec / NRPNParser)
**Qué ya consume:**
- `ABDMS2000`

Evidencia:
- `ABDMS2000/Source/MIDI/*` incluye `../../ABDSharedCode/HardwareDrivers/SysExCodec.h` y `../../ABDSharedCode/HardwareDrivers/NRPNParser.h`.
- `ABDMS2000/Source/Tests/DSPCoreTests*.cpp` también los incluye.
- `ABDMS2000/ROADMAP.md` ya lo menciona como migrado/por migrar.

**Implicación:**
Este consumo parece activo. Lo que conviene revisar es si todavía hay copies locales de los mismos codecs que no deberían existir.

---

### A4. `ABDShared::Scope*` (ScopeCore / ScopeCoreHeaders / ScopeWebAssets)
**Qué ya consume:**
- `ABDMS2000`
- `ABDAudioLab` (parece referencia cruzada desde build-reference/.sln y tlog)

Evidencia:
- `ABDMS2000/CMakeLists.txt` enlaza `ABDShared::ScopeCore` y `ABDShared::ScopeCoreHeaders`.
- build-reference de ABDNeural y ABDMS2000 empaquetan `ABDSharedCode/StudioTopologyAssets` y `HardwareMidiPickerAssets`.

**Implicación:**
Scope parece estar en proceso de absorción como módulo compartido. La señal fuerte aquí es que hay documentación abierta sobre cómo absorberlo y qué problemas de duplicación existen.

---

### A5. `DspJunoHPF`, `ArpeggiatorJuno` y `PolyBLEP`
**Qué ya consume:**
- `ABDJUNiO601` (consume `DspJunoHPF.h` y `ArpeggiatorJuno.h` con shims zero-alloc; 100% test suite pasando)
- `ABDEep` (consume `DspJunoHPF.h`, `PolyBLEP.h` y las unidades C++20 de `SynthCore` en nativo y WASM)

Evidencia:
- `ABDSharedCode/DspCore/DspJunoHPF.h` (unificación del filtro paso alto modelado de 4 etapas).
- `ABDSharedCode/SynthCore/ArpeggiatorJuno.h` (motor de arpegiador C++20 sin JUCE).
- `ABDJUNiO601/build_tests.bat` ejecutando toda la suite de tests en verde.
- `ABDEep/wasm/CMakeLists.txt` enlazando las unidades de SynthCore para WebAssembly.

**Implicación:**
Esta extracción y unificación ya está completada, validada matemáticamente y en producción local sin regresiones.

---

## B. Sustitución fuertemente señalada por los propios docs de los proyectos

Esto es lo más explícito: los proyectos ya han detectado duplicación y han anotado qué se puede sustituir.

### B1. `ABDMS2000` — duplicados exactos del compartido
Según `ABDMS2000/ANALISIS_DRY_COMPARTIDO.md` y `ABDMS2000/ROADMAP.md`, hay varios casos de código local idéntico al compartido:

1. **`Source/MIDI/SysExCodec.{h,cpp}`** duplica `ABDSharedCode/HardwareDrivers/SysExCodec.{h,cpp}` (salvo namespace).
2. **`Source/MIDI/NRPNParser.{h,cpp}`** duplica `ABDSharedCode/HardwareDrivers/NRPNParser.{h,cpp}` (salvo namespace).
3. **`WebUI/src/components/utils.js`** duplica `ABDSharedCode/MidiKeyboard/src/utils.js` (byte a byte según el informe).

**Qué se sugiere:**
- sustituir los dos duplicados de hardware codecs por el compartido,
- decidir si `utils.js` del WebUI se sustituye por el de `MidiKeyboard` o se justifica el fork.

### B2. `ABDMS2000` — provider WebView2 propio vs compartido
Según `ABDMS2000/ANALISIS_DRY_COMPARTIDO.md`, MS2000 usa `Source/PluginEditor_ResourceProvider.*`, mientras existe `ABDSharedCode/WebView2Bridge/WebView2ResourceProvider.*` justamente para sustituir ese patrón.

**Qué se sugiere:**
- si el provider de MS2000 hace lo mismo que el puente compartido, es candidato de sustitución directa;
- si tiene diferencias reales, hay que ver si encajan como extensión del puente compartido.

### B3. `ABDMS2000` — detección manual de puertos vs `HardwareMidiDetect`
Según el mismo informe, MS2000 tiene selección manual de puertos, mientras que `ABDShared::HardwareMidiDetect` + contratos de `ABDSharedAssets/contracts` (p.ej. `korg_ms2000.json`) permitirían detección + picker WebView2.

**Qué se sugiere:**
- si MS2000 necesita detectar hardware MS2000 físico, hay un contrato (`korg_ms2000.json`) y un módulo del que usar.
- si no lo necesita, puede que no haya migración real, solo posible.

### B4. `ABDMS2000` — AutoUpdater enlazado pero sin referencias en Source
Según `ABDMS2000/ANALISIS_DRY_COMPARTIDO.md`, MS2000 tiene `ABDShared::AutoUpdater` enlazado en CMake, pero cero referencias en `Source/`.

**Qué se sugiere:**
Esto parece candidato a **limpieza** más que a migración: o se consume con config/gancho, o se quita el enlace para no mantener dependencia muerta.

---

## C. Doble escrito / gemelos que conviene unificar

### C1. Dos `VoiceAllocator` / asignadores de voz
Según `ABDMS2000/ANALISIS_DRY_COMPARTIDO.md` y `ABDMS2000/ANALISIS_DRY_BANKMANAGER_CZ101_ABDEEP.md`, hay:
- `ABDSharedCode/LutDSP/VoiceAllocator.h`
- `Source/Core/VoiceManager.{h,cpp}` en MS2000 (o semántica similar)

Y ambos documentos hablan de **converger** en un único asignador compartido.

**Qué se sugiere:**
Si `VoiceManager` de MS2000 y `VoiceAllocator` del compartido hacen lo mismo, convenga unificar antes de crecer. Esto es una extracción/unificación, no una sustitución pura.

### C2. Sinatra WebUI de teclado / utils.js
El informe de MS2000 pone `utils.js` del WebUI como idéntico al de `MidiKeyboard`.

**Qué se sugiere:**
Si es idéntico, el proyecto consumidor debería consumir la versión compartida o al menos dejar claro por qué diverge.

---

## D. Candidatos a extracción hacia ABDSharedCode (nuevo módulo compartido)

Aquí entramos en "este código está en otro proyecto y parece partible".

### D1. Filtros/modelado de filtro (ZDF, Moog, Korg MS20, etc.)
Según `ABDMS2000/ANALISIS_DRY_BANKMANAGER_CZ101_ABDEEP.md`, hay propuestas como:
- `JunoHPF` (✅ **Completado**: unificado en `ABDSharedCode/DspCore/DspJunoHPF.h`)
- `MoogLadderVCF` (Próximo candidato activo)
- `KorgMS20VCF` (Próximo candidato activo)
- `JunoVCF_ZDF`
- `VAOnePoleFilter`
- `VcfVoicing`

Y se menciona un módulo propuesto **`ABDShared::Filters`** (o extensión de `LutDSP` / `DspCore`) para alojarlos, coexistir con modelos LUT y proveer "camino exacto" por aparato.

**Qué se sugiere si se quiere avanzar:**
- `DspJunoHPF` ya demostró el patrón: header-only en `DspCore`, C++20 puro, cero dependencias de JUCE.
- `MoogLadderVCF` y `KorgMS20VCF` deben seguir exactamente este mismo patrón de paridad y shims.

### D2. Tablas / voces / snapshot helpers
El mismo informe menciona:
- `AudioThreadSnapshot`
- `VcfVoicing`
- políticas de robo de voces / prioridad / unisono / re-trigger sin clics

**Qué se sugiere:**
Esto entra en la bucket "así parece utilizable por varios sintetizadores", pero hay que verificar si ya existe algo parecido en `SynthCore` o si es nueva superficie.

### D3. Comparador A/B de paridad hardware vs emulación
Según el informe, `ABDShared::AudioComparator` puede dar "dictamen formal pass/warn/fail" para validar paridad CZ101 emulado vs hardware real.

**Qué se sugiere:**
Si más proyectos tienen grabaciones reales y necesitan dictamen, este módulo es candidato de consumo adicional, no necesariamente de nuevo desarrollo.

---

## E. Cosas que los docs del árbol marcan como pendientes / no resueltas

### E1. `ABDShared::WebView2Bridge` como target público vs archivo
Según `ABDScope/HANDOFF.md` y `ABDScope/docs/EVALUATION_ABDSHAREDCODE_ASSETS.md`, hay:
- archivos de puente en `ABDSharedCode/WebView2Bridge/`,
- pero no está claro si ya existe target publicado y documentado en el orquestador.

**Qué se sugiere:**
Si varios proyectos quieren reemplazar sus resource providers, conviene que el puente exista como target publicado, no como "archivos que referencian por path".

### E2. Absorción de `ABDScope` dentro de `ABDSharedCode/Scope`
Según `ABDScope/HANDOFF.md` y `ABDScope/docs/EVALUATION_ABDSHAREDCODE_ASSETS.md`, hay un plan documentado:
- absorber `ABDScope/Source/` + `ABDScope/WebUI/` en `ABDSharedCode/Scope/` con historia,
- registrar targets `ABDShared::Scope*`,
- mantener aliases de compatibilidad `ABDScope::*` durante transición.

**Qué se sugiere:**
Esto es una migración estructural más que una migración de un fichero. Si se hace, impacta a todos los que consumen Scope hoy.

### E3. Duplicación de patrón WebView2 resource provider
Según `ABDScope/docs/EVALUATION_ABDSHAREDCODE_ASSETS.md` y `ABDScope/HANDOFF.md`, la duplicación entre:
- `ABDScope/Source/JUCE/ScopeResourceProvider.*`
- `ABDSharedCode/HardwareMidiDetect/HardwareMidiPickerResourceProvider.*`

está escrita como problema conocido.

**Qué se sugiere:**
Si varias apps tienen su propio "servidor de assets WebView2", el préstamo más valioso puede ser unificando el patrón base, no copiando provider a provider.

---

## F. Proyectos donde no tengo evidencia suficiente para afirmar migración

Con lo que hay montado ahora, **no** puedo afirmar candidatos concretos con la misma solidez en:

- `ABDOmegaUnified` (host/web): no heLocalizado referencias claras a módulos de ABDSharedCode en la evidencia que he recopilado aquí.
- carpetas mientras no se revise su `Source/` real.

Para esos, la respuesta correcta es: "por confirmar con barrido de su CMake/includes/importJS real".

---

## G. Hoja de ruta recomendada si se quiere operativa

Si se quiere convertir esto en acción, yo lo ordenaría así:

1. **Limpiar duplicados ya identificados:**
   - `ABDMS2000` SysExCodec / NRPNParser locales,
   - `ABDMS2000` utils.js del WebUI si es idéntico al compartido,
   - `ABDMS2000` AutoUpdater si está enlazado sin referencia.

2. **Unificar before expanding:**
   - `VoiceAllocator` / asignación de voz,
   - patrón resource provider WebView2,
   - scope standalone vs `ABDSharedCode/Scope`.

3. **Aceptar nuevos módulos con reglas:**
   - `ABDShared::Filters` o similar solo si cumple reglas del repo y trae tests/paridad,
   - nuevo código de filtro modelado solo si no rompe reglas de JUCE-free/libm-free.

4. **Por confirmar por proyecto:**
   - `ABDOmegaUnified`,
   - cualquier otro synth con tabla de efectos propia,
   - cualquier synth con su propio picker de hardware o scope.

---

## H. Evidencia base usada aquí

Fuentes que he consultado para este barrido:
- `ABDMS2000/ANALISIS_DRY_COMPARTIDO.md`
- `ABDMS2000/ANALISIS_DRY_BANKMANAGER_CZ101_ABDEEP.md`
- `ABDMS2000/ROADMAP.md`
- `ABDMS2000/CMakeLists.txt`
- `ABDMS2000/wasm/CMakeLists.txt`
- `ABDMS2000/Source/MIDI/*.h`
- `ABDMS2000/Source/DSP/*`
- `ABDMS2000/Source/Tests/DSPCoreTests*.cpp`
- `ABDNeural/CMakeLists.txt`
- `ABDNeural/ROADMAP.md`
- `ABDNeural/HANDOFF.md`
- `ABDNeural/Source/DSP/*`, `ABDNeural/Source/State/*`
- `ABDScope/HANDOFF.md`
- `ABDScope/docs/EVALUATION_ABDSHAREDCODE_ASSETS.md`
- `ABDScope/docs/SESSION_CONTINUATION_PROMPT.md`
- `ABDScope/CMakeLists.txt`, `ABDScope/Source/CMakeLists.txt`
- lockfiles y workspace yaml de `ABDMS2000` y `ABDNeural/WebUI`
- referencias cruzadas en build-reference y .sln de ABDMS2000 y ABDNeural

---

Si quieres, puedo:
- entrar en `ABDOmegaUnified` y buscar allí referencias reales a ABDSharedCode,
- escribir un inventario por proyecto: "duplicado exacto / candidato compartido / pendiente",
- o proponer los nombres de los targets/modulos nuevos que saldrían de D1-D3 si se decide extraerlos.
