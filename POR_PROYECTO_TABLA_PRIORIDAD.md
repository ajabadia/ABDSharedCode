# Migraciones por proyecto — tabla de prioridad y estado

> Un solo vistazo por proyecto, con tres baldes operativos:
> - **Consumo real / ya migrado:** ya usa o ya integró ABDSharedCode.
> - **Duplicado / sustitución documentada:** hay evidencia de código/local duplicado o candidato claro de sustitución.
> - **Candidato extracción:** parece partible hacia ABDSharedCode como nuevo módulo o unificación.
> - **Pendiente:** no hay evidencia firme en este árbol montado.
>
> Fuente: barrido de CMake/includes/importJS/docs visibles en este monorepo + ficheros del propio ABDSharedCode.
> Donde no hay evidencia, figura como pendiente, no como afirmación.

---

## 0. Abreviaturas usadas en la tabla

- **CM** = CMakeLists.txt / target linkage visible
- **JS** = import de `@abdsynths/*` o consumo de MP del workspace
- **DOC** = afirmación basada en docs/roadmap/handoff del propio proyecto
- **SH** = afirmación basada en docs del propio ABDSharedCode

---

## 1. Tabla por proyecto

| Proyecto | Consumo real / ya migrado | Duplicado / sustitución documentada | Candidato extracción / unificación | Pendiente |
|---|---|---|---|---|
| **ABDAudioLab** | Integra `ABDSharedCode` por CM (local + FetchContent). Compila/enlaza: `HardwareDrivers/`, `AudioComparator/`, `HardwareMidiDetect/`, `WebView2Bridge/`, `StudioTopology/`, `MidiKeyboard` nat., tests del repo compartido. Usa `ABDShared::ScopeCore` desde `ABDSharedCode/Scope`. | Revisar si aún hay stubs/forwarders locales que dupliquen lo ya en compartido. | Revisar si algún visual nuevo (p.ej. tipo `Waterfall3DComponent`) ya está promovido o sigue con forwarder local. Revisar alineación de schemas generados con `sourceOfTruth` del compartido. | Inventario local exacto de duplicados restantes. |
| **ABDMS2000** | Integra `ABDSharedCode` por CM. Enlaza `ABDShared::SynthCore`, `ABDShared::HardwareDrivers`, `ABDShared::ScopeCore/ScopeCoreHeaders`. Consume `SysExCodec`/`NRPNParser` desde `Source/MIDI/`. Consume `MidiKeyboard` como workspace pnpm. | DOC: duplicados locales documentados vs compartido: `Source/MIDI/SysExCodec.{h,cpp}`, `Source/MIDI/NRPNParser.{h,cpp}`, `WebUI/src/components/utils.js` vs `ABDSharedCode/MidiKeyboard/src/utils.js`. Provider WebView2 propio vs `WebView2Bridge/WebView2ResourceProvider.*`. Detección manual de puertos vs `HardwareMidiDetect` + contrato `korg_ms2000.json`. AutoUpdater enlazado sin refs en Source (dependencia muerta probables). | Posible `ABDShared::Filters` o extensión LutDSP para filtros modelados/ZDF. Converger asignación de voz (`VoiceManager`/`VoiceAllocator`). Unificar patrón resource provider WebView2. | Detalles de implementación de VoiceManager y filtros. |
| **ABDNeural** | Consume `ABDShared::DspCore`, `ABDShared::DspEffects`. Consume `MidiKeyboard`/workspace. References a `WebView2Bridge` desde testing/contracto. Contratos generados apuntan a `ABDSharedCode/SynthCore/S950PatchFields.h` y `S950Calibration.h`. | Revisar si hay efectos/catálogo propios duplicados del catálogo compartido. Revisar si hay Scope/picker/resource provider propio alineable. | Unificar patrón resource provider/Scope si hay duplicación real. | Inventario exacto de duplicados de efectos/catálogo local. |
| **ABDScope** | Repo hermano histórico de Scope; absorción documentada hacia `ABDSharedCode/Scope`. | Duplicación de patrón resource provider WebView2: `ABDScope/Source/JUCE/ScopeResourceProvider.*` vs `ABDSharedCode/HardwareMidiDetect/HardwareMidiPickerResourceProvider.*`. | Absorción canónica de Scope en `ABDSharedCode/Scope`. Publicar `ABDShared::WebView2Bridge` como target claro si falta. | Confirmar si la absorción está completada o sigue pendiente. |
| **ABDJUNiO601** | Integra por CM. Enlaza `LutDSP`, `DspCore`, `HardwareDrivers`. Consume `DspJunoHPF.h` y `ArpeggiatorJuno.h` (shims zero-alloc validados, 100% tests OK), y BBD Chorus (`JunoBBD.h`). `DspJunoVCF.h` disponible en `DspCore` para adopción. | Generalización de decodificación de cinta / FSK si aplica a otros sintes. | Adopción de `DspJunoVCF.h` para reemplazar `JunoVCF` local con versión zero-JUCE. | FSK tape decoder generalizable. |
| **ABDEep** | Integra por CM (nativo + WASM). Enlaza `SynthCore`, `DspCore`, `DspEffects`. Shims y consumo activo de `DspJunoHPF`, `DspJunoVCF` (`VcfVoicing`), `DspVAOnePole`, `DspMoogLadder`, `DspKorgMS20`, `PolyBLEP`, `VoiceAllocator`, `DriftEngine`, `EnvelopeAnalog`, `LfoAnalog`, `Arpeggiator`, `ControlSequencer`. 5.084 tests WebUI y 1.1M aserciones C++ pasando. | Shims completados con 100% paridad. | Ningún DSP reutilizable pendiente en `Source/DSP` (sólo quedan osciladores y orquestadores propios del DeepMind). | Ninguno. |
| **ABDCZ101** | Consume `@abdsynths/midi-keyb` y `@abdsynths/shared` desde workspace. | Revisar si tiene algo local duplicable del compartido. | Revisar si hay algo compartible documentado (p.ej. curvas DCW/envolventes mencionadas cruzadamente). | Inventario de CMake/includes/JS real. |
| **ABDOmegaUnified** | Sin consumo directo detectado en este árbol: `host/CMakeLists.txt` no referencia `ABDSharedCode`; `package.json` no declara `@abdsynths/*`. | Sin evidencia de duplicado vs compartido en este árbol. | Sin candidato confirmado sin ver Source/ más profundo. | Ver `host/Source/` y submódulos; ver si editor web usa contratos de `ABDSharedAssets/contracts` o es propio. |

---

## 2. Lectura rápida de prioridad

- **Completado recientemente:** `ABDJUNiO601` y `ABDEep` (JunoHPF, ArpeggiatorJuno, PolyBLEP, build WASM, 100% tests pasando).
- **Próximo en DSP compartido:** Extracción de filtros analógicos de `ABDEep` (`MoogLadderVCF`, `KorgMS20VCF`) hacia `ABDSharedCode/DspCore`.
- **Próximo en desduplicación de repositorio:** `ABDMS2000`. Limpieza de duplicados de `SysExCodec`/`NRPNParser` y `AutoUpdater` muerto.
- **Más estructural a medio plazo:** `ABDScope` (absorción en `ABDSharedCode/Scope`) y convergencia de providers WebView2.

---

## 3. Regla de limte que restringe lo que se puede migrar

Si algo usa JUCE o libm en el lazo de audio, la migración no es directa hacia módulos que la guía del repo dice JUCE-free / libm-free. Eso puede significar:
- reescritura,
- shim local,
- o no moverlo tampoco.

Así que "susceptible" no siempre significa "se mueve tal cual".

---

Si quieres, puedo:
- añadir una columna de "primer paso concreto" por proyecto,
- o pasar ya a la hoja de ruta mínima para `ABDMS2000`.
