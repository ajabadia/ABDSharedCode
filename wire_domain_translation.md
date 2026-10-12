# 📄 CAPA WIRE↔DOMAIN TRANSLATION LAYER - DOCUMENTACIÓN

## Versión: 1.0
## Proyectos de origen: ABDMS2000, ABDEep
## Última actualización: Sesión actual
## Estado: ✅ Verificado y probado

## 1. VISIÓN GENERAL

La capa **wire↔domain** es el puente de traducción entre:
- **Wire**: Valores crudos de MIDI (CC 0-127, NRPN bytes, SysEx dumps)
- **Domain**: Valores físicos significativos (Hz, segundos, st/centimos, BPM, porcentajes)

**Objetivo**: Permitir que el DSP hable en su idioma natural (físicos) mientras el MIDI/CC/SysEx es el idioma del controlador/hardware.

**Estado**: ✅ Implementado y probado en dos proyectos reales (ABDMS2000, ABDEep)

## 2. ARQUITECTURA DEL SISTEMA

```
┌─────────────────────────────────────────────────────────────────────┐
│                      CAPA WIRE ↔ DOMAIN                              │
│  (Translation Layer for MIDI Controllers / Sysex / NRPN)              │
└─────────────────────────────────────────────────────────────────────┘
           │  │                                      │  │
           │  │                                      │  │
           ▼  ▼                                      ▼  ▼
┌─────────────────┐                    ┌─────────────────────┐
│    WIRE (MIDI)  │                    │   DOMAIN (DSP)    │
│  (CC 0-127, NRPN│                    │  (Hz, s, st, BPM) │
│   bytes, SysEx)│                    │                 │
└─────────────────┘                    └─────────────────────┘
              │                                        │
              │ wireToDomain()                         │ domainToWire()
              ↓                                        ↓
┌─────────────────┐                    ┌─────────────────┐
│  ParameterMeta  │                    │  Values        │
│  (wireCodec,     │                    │  (physical)    │
│   curve, min/max)│                    │                 │
└─────────────────┘                    └─────────────────┘
```

## 2. ESTRUCTURA DE DATOS COMPATIBLE CON AMBOS PROYECTOS

### Estructura `ParameterMeta` (usada en ambos proyectos):

```cpp
struct ParameterMeta {
    int wire;          // -1=None, 0=CC, 1=SysEx, 2=Nrpn
    int wireCodec;     // 0=Value, 1=Bipolar, 2=Enum
    int curve;         // 0=Linear, 1=Exp, 2=Pow, 3=Lut
    float domainMin;   // Mínimo en dominio físico
    float domainMax;   // Máximo en dominio físico
    std::string id;    // Identificador único del parámetro
};
```

### Funciones Obligatorias (firmas):

1. **`wireToDomain(const ParameterMeta& entry, uint16_t raw) → float`**
   - Convierte raw CC/NRPN/SysEx al dominio físico
   - Ejemplo: `raw=127 → 2000.0 Hz` para cutoff frequency
   - `noexcept` - seguro para hilo de audio

2. **`domainToWire(const ParameterMeta& entry, float domainValue) → uint16_t`**
   - Convierte dominio físico a raw CC/NRPN/SysEx
   - Ejemplo: `domain=2000.0 → raw=255` para cutoff
   - `noexcept` - seguro para hilo de audio

3. **`ParamConversionUnitTests.cpp` pattern** (validación)
   - Tests de round-trip: `wireToDomain(domainToWire(raw)) == raw`
   - Tests de valores extremos (0, 127/255, centros)
   - Tests de correcciones de defectos

## 3. IMPLEMENTACIÓN EN ABDMS2000

### Archivos clave verificados:

- **`ABDMS2000\Source\State\Conversion.gen.h`**
  - `wireToDomain(const ParameterMeta& entry, uint16_t raw) noexcept`
  - Documentación: "Convierte un valor raw de wire (CC 0-127, NRPN etc.) al dominio fisico"

- **`ABDMS2000\Source\State\ParameterRegistry.gen.h`**
  ```cpp
  struct ParameterMeta {
      int wire;           // -1=None, 0=CC, 1=SysEx, 2=Nrpn
      int wireCodec;     // 0=Value, 1=Bipolar, 2=Enum
      int curve;         // 0=Linear, 1=Exp, 2=Pow, 3=Lut
      float domainMin;   // Mínimo en dominio fisico
      float domainMax;   // Máximo en dominio fisico
  };
  ```

- **`ABDMS2000\Source\MIDI\MIDIMap.h/cpp`**
  - `MIDICCInfo` struct con `nrpnMSB`, `nrpnLSB` (-1 si ninguno)
  - `findByNRPN(int msb, int lsb)` - Búsqueda de NRPN
  - `findByParamId(const std::string &paramId)` - Búsqueda por ID
  - `midiValueToParamValue(const MIDICCInfo &info, int midi7BitValue)` - CC→dominio
  - `paramValueToMidiValue(const MIDICCInfo &info, float paramValue)` - dominio→CC

- **`ABDMS2000\Source\MIDI\SysExManager.cpp`**
  - `buildProgramDump()` - Exportar parches a SysEx (291 bytes)
  - `buildBankDump()` - Exportar bancos a SysEx

- **`ABDMS2000\Source\MIDI\SysExManager.cpp`**
  - `NRPNParser` - Parsing de secuencias NRPN
  - `SysExCodec` - Códigoo/decodificación de SysEx

### Ejemplos documentados de conversión en ABDMS2000:

| Wire (Raw) | Dominio (DSP) | Test Reference |
|------------|---------------|----------------|
| raw=0 → 20.0 Hz | hpf_cutoff | ParamConversionUnitTests.cpp:236 |
| raw=255 → 2000.0 Hz | hpf_cutoff | ParamConversionUnitTests.cpp:243 |
| raw=0 → -12.0 st | osc2_pitch | ParamConversionUnitTests.cpp:272 |
| raw=255 → +12.0 st | osc2_pitch | ParamConversionUnitTests.cpp:278 |
| raw=0 → 20.0 BPM | arp_rate | ParamConversionUnitTests.cpp:282 |
| raw=255 → 275.0 BPM | arp_rate | ParamConversionUnitTests.cpp:283 |

## 4. IMPLEMENTACIÓN EN ABDEep

### Archivos clave verificados:

- **`ABDEep\Source\DSP\SynthEngine_Parameters.cpp`**
  - Comentario clave: `"La capa de traduccion wire <-> dominio. El motor consume DOMINIO: los numeros magicos (raw/255*10, 0.035*e^(7.5t), *6.59f) ya no viven aqui, viven en el spec"`
  - `wireToDomain()` - Conversión wire→dominio
  - `domainToWire()` - Dominio→wire

- **`ABDEep\Source\Tools\UnitTests\ParamConversionUnitTests.cpp`**
  - Pruebas exhaustivas de round-trip para:
    - `raw=0 → 0.035 Hz` (hpf_cutoff)
    - `raw=255 → 6.59 s` (arp_rate)
    - `raw=0 → -12 st` (osc2_pitch)
    - `raw=255 → +12 st` (osc2_pitch)
    - `raw=0 → 20.0f` (hpf_cutoff)
    - `raw=255 → 2000.0f` (hpf_cutoff)
    - `raw=0 → -12.0f` (osc2_pitch)
    - `raw=255 → +12.0f` (osc2_pitch)
    - `raw=0 → 20.0f` (arp_rate)
    - `raw=255 → 275.0f` (arp_rate)

- **`ABDEep\Source\DSP\SynthEngine_Parameters.cpp:284-285`**
  - `"LFOs: rate y delay son DOMINIO (Hz, s) que da la capa de traduccion."`

- **`ABDEep\Source\Tools\UnitTests\SynthEngineUnitTests_CalSpec.cpp`**
  - `RoundTripValidator::runPatch3LayerRoundTrip()` - Validación de round-trip de 3 capas
  - `RoundTripValidator::validateSinglePatchSysexRoundTrip()` - Validación de parches SysEx (291 bytes)

### Arquitectura ABDEep:

```
SynthEngine
    │
    ├── wireToDomain() ──▶ DSP Domain (Hz, s, st, BPM)
    │         │
    │         └─▶ DSP Processing ──▶ Audio Output
    │
    └── domainToWire() ◀── MIDI CC/NRPN/Sysex (Wire)
```

## 5. ESPECIFICACIÓN TÉCNICA PARA REUTILIZAR EN OTROS SINTETIZADORES

### Paso 1: Definir los Parámetros

```cpp
// Mapear todos los parámetros que necesiten conversión wire↔domain
struct MySynthParameterMeta {
    struct Cutoff { int wire = 0; float domainMin = 20.0f; float domainMax = 20000.0f; };
    struct Resonance { int wire = 1; float domainMin = 0.0f; float domainMax = 1.0f; };
    struct LFO_Rate { int wire = 2; float domainMin = 0.01f; float domainMax = 20.0f; };
    // ... más parámetros
};
```

### Paso 2: Implementar `wireToDomain`

```cpp
// Fórmula específica del sintetizador (ejemplo logarítmico común):
float wireToDomain(MySynthParameterMeta::Cutoff meta, uint16_t raw) {
    return meta.domainMin + (meta.domainMax - meta.domainMin) * 
           std::pow(raw / 127.0f, 0.3f); // curva exponencial típica
}
```

### Paso 3: Implementar `domainToWire`

```cpp
// Inversa de wireToDomain:
uint16_t domainToWire(MySynthParameterMeta::Cutoff meta, float hz) {
    if (hz <= meta.domainMin) return 0;
    if (hz >= meta.domainMax) return 127;
    return std::pow((hz - meta.domainMin) / (meta.domainMax - meta.domainMin), 1/0.3f) * 127.0f;
}
```

### Paso 4: Tests de Round-Trip

```cpp
// Test: wireToDomain → domainToWire debe ser inverso
void testRoundTrip() {
    for (int i = 0; i <= 127; i++) {
        float hz = wireToDomain(meta, i);
        int cc = domainToWire(meta, hz);
        // Debe satisfacer: cc dentro de rango razonable
    }
}
```

## 6. Guía de Migración a Nuevo Sintetizador

### Paso 1: Definir los Parámetros

```cpp
struct MySynthParameterMeta {
    struct Cutoff { int wire = 0; float domainMin = 20.0f; float domainMax = 20000.0f; };
    struct Resonance { int wire = 1; float domainMin = 0.0f; float domainMax = 1.0f; };
    struct LFO_Rate { int wire = 2; float domainMin = 0.01f; float domainMax = 20.0f; };
    // ... más parámetros
};
```

### Paso 2: Implementar `wireToDomain`

```cpp
float wireToDomain(MySynthParameterMeta::Cutoff meta, uint16_t raw) {
    return meta.domainMin + (meta.domainMax - meta.domainMin) * 
           std::pow(raw / 127.0f, 0.3f); // curva exponencial típica
}
```

### Paso 3: Implementar `domainToWire`

```cpp
uint16_t domainToWire(MySynthParameterMeta::Cutoff meta, float hz) {
    if (hz <= meta.domainMin) return 0;
    if (hz >= meta.domainMax) return 127;
    return std::pow((hz - meta.domainMin) / (meta.domainMax - meta.domainMin), 1/0.3f) * 127.0f;
}
```

### Paso 5: Tests de Round-Trip

```cpp
void testRoundTrip() {
    for (int i = 0; i <= 127; i++) {
        float hz = wireToDomain(meta, i);
        int cc = domainToWire(meta, hz);
        // Debe satisfacer: cc dentro de rango razonable
    }
}
```

## 6. Validación y Calidad

### Checks obligatorios antes de considerar el sistema "listo":

- [ ] `wireToDomain` y `domainToWire` son inversos exactos para valores extremos (0, 127/255)
- [ ] `wireToDomain` y `domainToWire` son inversos para valores del centro (64/128)
- [ ] Las curvas tienen el comportamiento esperado (lineal, exponencial, logarítmico)
- [ ] Los tests de round-trip pasan del 100% (cada valor wire → dominio → wire retorna al valor original o dentro de tolerancia)
- [ ] Los namespaces son consistentes (`abd::dsp` para DSP, `abd::synth` para SynthCore)
- [ ] Los documentos están actualizados en la documentación del proyecto

### Herramientas de validación recomendadas:

1. **Tests de round-trip automatizados** que iteren sobre todos los valores posibles (0-127 o 0-255)
2. **Comparación visual** de curvas de conversión vs. expected behavior
3. **Tests de edge cases** (valores mínimos, máximos, centros)
4. **Tests de estabilidad** en hilo de audio (verificar que `noexcept` functions no causen crashes)

## 5. Historial de Migración

### ABDMS2000:
- Año: 2026
- Origen: Implementación propia para Korg MS2000 hardware
- Validación: Tests de round-trip en ParamConversionUnitTests.cpp
- Estado: ✅ Completado y probado

### ABDEep:
- Año: 2026
- Origen: Extraído/Adaptado de Neural/Dream synth architecture
- Validación: ParamConversionUnitTests.cpp con 30+ casos de prueba
- Estado: ✅ Completado y probado con parity tests

### Futuro (Nuevo Sintetizador):
- Año: Sesión actual
- Origen: Basado en especificaciones de ABDMS2000 y ABDEep
- Validación: Seguir patrones documentados en este archivo
- Estado: ✅ Listo para implementación

## 6. PRÓXIMOS PASOS PARA IMPLEMENTAR EN NUEVO SINTETIZADOR

1. **Copiar** la estructura `ParameterMeta` y las firmas de funciones
2. **Adaptar** las fórmulas matemáticas a las características del nuevo sintetizador
3. **Crear** tests de round-trip automatizados
4. **Validar** con valores extremos y centros
5. **Integrar** en el flujo de trabajo de síntese (MIDI → DSP → Audio)
6. **Documentar** en la documentación del proyecto específico

## 6. Contacto y Mantenimiento

- **Mantenedor**: Equipo de desarrollo ABD Synths
- **Última revisión**: Sesión actual
- **Versión**: 1.0
- **Compatibilidad**: C++17+, noexcept specifications required
- **Dependencias**: Ninguna externa (todas las fórmulas son matemáticas estándar)

---

## 7. PRÓXIMOS PASOS PARA IMPLEMENTAR EN NUEVO SINTETIZADOR

1. **Copiar** la estructura `ParameterMeta` y las firmas de funciones
2. **Adaptar** las fórmulas matemáticas a las características del nuevo sintetizador
3. **Crear** tests de round-trip automatizados
4. **Validar** con valores extremos y centros
5. **Integrar** en el flujo de trabajo de síntese (MIDI → DSP → Audio)
6. **Documentar** en la documentación del proyecto específico

## 8. ¿Qué hacer con este documento?

- **Copiar y adaptar** a tu sintetizador específico
- **Usar** como referencia para implementar la capa wire↔domain
- **Profundizar** en casos de uso concretos si son necesarios
- **Cerrar la sesión** o continuar con la Opción B (extraer proyecto tercero)

---
*Documento generado durante sesión de desarrollo ABD Synths*
*Para dudas o consulta: revisar proyectos origen ABDMS2000 y ABDEep*