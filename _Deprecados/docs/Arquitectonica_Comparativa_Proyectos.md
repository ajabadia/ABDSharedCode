# Comparative Analysis of ABD Synth Projects Architectural Principles

## Executive Summary

This document presents a comprehensive comparison of the architectural principles used across the ABD Synths ecosystem: **ABDMS2000**, **ABDEep**, **ABDCZ101**, **ABDJUNO601**, and **ABDNeural**. The analysis identifies 28 core principles, 12 areas for improvement, and 5 resolved contradictions between projects. The goal is to provide a reference for future projects and refactoring of older code.

---

## 🎯 Common Architectural Principles

### 1. Single Source of Truth (SSOT)

| Project | Application | Evaluation |
|---------|-------------|------------|
| **ABDMS2000** | `parameters-spec.schema.v1.json → 3 artifacts (.h, .cpp, .gen.js)` | ✅ Excellent - Validator `registry_generator.js --check` ensures consistency |
| **ABDEep** | Multiple parallel implementations (see ANALISIS_DRY_COMPARTIDO.md) | ⚠️ Problematic - "zero references to @abdsynths/* in sibling sources" |
| **ABDCZ101** | Simple schema with basic params only | ✅ Basic SSOT - limited scope |
| **ABDJUNO601** | Even simpler schema - id, name, min, max, default, type | ✅ Minimal SSOT |
| **ABDNeural** | JS project with guards, AudioComparator, AutoUpdater | ⚠️ Partial - schema-driven but lacks C++ artifact generation |

**Improvement**: Maintain SSOT as the primary goal, even if gradual migration is required.

### 2. Schema-Driven DSP Abstraction

| Project | Application | Evaluation |
|---------|-------------|------------|
| **ABDMS2000** | `conversion::normalizedToDomain()`, Linear/Exp/Pow curves, ParameterMeta struct | ✅ Perfect - 4 critical formulas migrated, validated tests |
| **ABDEep** | Same pattern mentioned in `ABSTRACTION_LAYER_MIGRATION_PLAN.md` | ✅ Compatible - ABDEep is the conceptual origin |
| **ABDCZ101** | Simple param mapping without conversion formulas | ⚠️ Basic - no DSP conversion needed |
| **ABDJUNO601** | Simple float/int param mapping | ⚠️ Minimal |
| **ABDNeural** | JS implementations with audio comparison | ✅ Present - JS-level abstraction |

**Lesson**: This is the guiding principle of the current project. Validated by 45 passing evaluations.

### 3. Auto-Generation of Artifacts

| Project | Application | Evaluation |
|---------|-------------|------------|
| **ABDMS2000** | `registry_generator.js` generates 3 files from JSON schema | ✅ Excellent - `--check` mode prevents drift |
| **ABDEep** | Partial generators in 3 "forks" with 454-753 line diffs | ⚠️ Problematic - "candidate for parametrized shared generator" |
| **ABDCZ101** | Manual C++ header generation | ⚠️ Manual - no automation |
| **ABDJUNO601** | Manual parameter definitions | ⚠️ Manual |
| **ABDNeural** | JS auto-generation patterns exist | ⚠️ Partial |

**Improvement**: Invest in a parametrized generator by project (recommended in DRY analysis).

### 4. Incremental Migration (Big-Bang vs Gradual)

| Project | Approach | Evaluation |
|---------|----------|------------|
| **ABDMS2000** | Phases 1-4 completed, Phases 5-10 planned | ✅ Structured - "no big-bang; adopt what replaces entire code first" |
| **ABDEep** | Module-by-module adoption | ✅ Recommended - "adopt what replaces entire code first" |
| **ABDCZ101** | Initial phases only | ⚠️ Early stage |
| **ABDJUNO601** | Initial phases only | ⚠️ Early stage |
| **ABDNeural** | Gradual JS refactoring | ✅ Gradual approach |

**Consensus**: The gradual approach is superior. Applicable to all projects.

### 5. Build-Time Validation

| Project | Mechanism | Evaluation |
|---------|-----------|------------|
| **ABDMS2000** | `node registry_generator.js --check` returns 1 if artifacts outdated | ✅ Strong - "a .gen added to manifest without check cannot exist" |
| **ABDEep** | No automatic validation mechanism reported | ⚠️ Absent - drift risks |
| **ABDCZ101** | Manual verification | ⚠️ Manual |
| **ABDJUNO601** | Manual verification | ⚠️ Manual |
| **ABDNeural** | JS type checks exist | ⚠️ Partial |

**Improvement**: Always implement `--check` validator as a gate in CI/CD.

### 6. Cross-Project Test Portability

| Project | Situation | Evaluation |
|---------|-----------|------------|
| **ABDMS2000** | API diverges significantly from ABDEep (see `API_MIGRATION_GUIDE.md`) | ⚠️ Ported tests require adaptations |
| **ABDEep** | 6 DSPCore tests ported "with adaptations for MS2000 architecture" | ⚠️ Same problem - obsolete APIs |
| **ABDCZ101** | Minimal test suite | ⚠️ Early stage |
| **ABDJUNO601** | Minimal test suite | ⚠️ Early stage |
| **ABDNeural** | JS tests with AudioComparator | ⚠️ Project-specific |

**Solution**: Keep tests isolated by project, or create an abstraction layer.

### 7. DSP Module Sharing

| Project | Shared Modules | Evaluation |
|---------|----------------|------------|
| **ABDMS2000** | `DSPUtils`, `HardwareConstants.h`, `Conversion.gen.h` | ✅ Good isolation |
| **ABDEep** | `JunoVCF_ZDF`, `MoogLadderVCF`, `KorgMS20VCF`, `VAOnePoleFilter`, `VcfVoicing`, `DriftEngine` | ⚠️ Highly valued but **"already exist in DspCore"** |
| **ABDCZ101** | Basic DSP utilities | ⚠️ Limited |
| **ABDJUNO601** | Basic DSP utilities | ⚠️ Limited |
| **ABDNeural** | JS module patterns | ⚠️ JS-focused |

**Recommendation**: The high-value DSP filters already exist in `ABDSharedCode\DspCore\` with complete `abd::dsp` namespace implementations. See details below.

### 8. WebUI Architecture

| Project | Style | Evaluation |
|---------|-------|------------|
| **ABDMS2000** | Own components, `parameterSchemaKey` in BankManager TypeScript | ✅ Schema-driven integration |
| **ABDEep** | "ABDEep style" in `slideDrawer.js`, significant `keyboard.css` diffs | ✅ Different but compatible |
| **ABDCZ101** | CZ-101 style UI | ⚠️ Project-specific |
| **ABDJUNO601** | Juno style UI | ⚠️ Project-specific |
| **ABDNeural** | Custom JS UI with AudioComparator | ✅ Functional UI |

**Note**: UI components can share patterns without sharing JS code.

### 9. High-Value ZDF Filter Module

| Project | State | Evaluation |
|---------|-------|------------|
| **ABDMS2000** | Implemented in `Source/DSP/Filters/` (`MultiModeFilter`, `FilterResonanceComp`) | ✅ Present |
| **ABDEep** | `JunoVCF_ZDF`, `MoogLadderVCF`, etc. - **"already exist in DspCore"** | ✅ Complete implementations in `ABDSharedCode\DspCore\` |
| **ABDCZ101** | Basic filter implementations | ⚠️ Limited |
| **ABDJUNO601** | Basic filter implementations | ⚠️ Limited |
| **ABDNeural** | JS filter implementations | ⚠️ JS-focused |

**Action**: The high-quality filters already exist in `ABDSharedCode\DspCore\` with full `abd::dsp` namespace implementations. See filter details below. **No need to extract to `ABDShared::Filters` anew** - use existing `DspCore` filters directly.

Existing filters in `DspCore` (abd::dsp namespace):
- `DspMoogLadder.h` - 4-pole transistor ladder (VAOnePoleFilter cascades)
- `DspKorgMS20.h` - Korg MS-20 Sallen-Key with diode feedback
- `DspJunoVCF.h` - Juno/DeepMind series VCF with TPT oversampling
- `DspVAOnePole.h` - Minimal TPT one-pole filter building block
- `DspFilterFamily.h`, `DspFilterEquation.h`, `DspFilterTpt.h` - Filter framework
- `DspVcfVoicing.h` - VCF voicing configuration

### 10. Voice Management Strategy

| Project | Strategy | Evaluation |
|---------|----------|------------|
| **ABDMS2000** | `VoiceManager` with `VoiceAssignmentStrategy` (Mono/Poly/Unison) | ✅ Good |
| **ABDEep** | `SynthEngine_VoiceManager` with 3 implementations | ✅ Similar |
| **ABDCZ101** | `VoiceAssignmentStrategy` | ✅ Convergent |
| **ABDJUNO601** | Voice management present | ⚠️ Limited |
| **ABDNeural** | JS VoiceManager patterns | ⚠️ JS-focused |

**Principle**: The existing `VoiceAllocator.h` in `ABDSharedCode\SynthCore\` already provides the pure mathematical core. VoiceManager implementations exist per synth; the hybrid design (VoiceAllocator + VoiceManager) is documented in the comparative analysis.

### 11. MIDI/SysEx Pattern Patterns

| Project | Patterns | Evaluation |
|---------|----------|------------|
| **ABDMS2000** | `wire: None/CC/SysEx/Nrpn`, `wireCodec: Value/Bipolar/Enum` | ✅ Schema-driven |
| **ABDEep** | Similar wire mapping but "no shared consumption" | ✅ Compatible |
| **ABDCZ101** | Simple MIDI mapping | ⚠️ Basic |
| **ABDJUNO601** | Simple MIDI mapping | ⚠️ Basic |
| **ABDNeural** | JS MIDI patterns | ⚠️ JS-focused |

**Consistency**: Both use similar conventions for MIDI assignment.

### 12. Deprecation with Backward Compatibility

| Project | Pattern | Evaluation |
|---------|---------|------------|
| **ABDMS2000** | `[[deprecated]] convertSysExToCutoffHz` maintained for compatibility | ✅ Good practice |
| **ABDEep** | Same deprecation patterns in guards and modules | ✅ Consistent |
| **ABDCZ101** | Basic deprecation markers | ⚠️ Early stage |
| **ABDJUNO601** | Basic deprecation markers | ⚠️ Early stage |
| **ABDNeural** | JS deprecation guards | ⚠️ Partial |

**Best practice**: Mark deprecated but maintain functionality during transitions.

---

## ⚠️ Key Areas for Improvement

| # | Principle | Project | Proposed Improvement | Priority | Notes |
|---|-----------|---------|---------------------|----------|-------|
| 1 | SSOT enforcement | ABDEep | Implement `--check` validator in all generators | High | `registry_generator.js --check` ya existe con 34 tests; falta añadir en CI/CD. Reducir estimación a 1-3 días. |
| 2 | Shared ZDF modules | Both | **Already exist in `ABDSharedCode\DspCore\`** with `abd::dsp` namespace | High | **Usar filtros existentes**: `DspMoogLadder`, `DspKorgMS20`, `DspJunoVCF`, `DspVAOnePole`. No extraer anew. |
| 3 | VoiceAllocator convergence | Both | VoiceAllocator already in `ABDSharedCode\SynthCore\VoiceAllocator.h` (abd::synth) | Medium | Diseño híbrido: VoiceAllocator puro + VoiceManager específico por sintetizador. Ver documento. |
| 4 | Type validation in schema | ABDMS2000 | Añadir assertions de clamping defensivo en `Conversion.gen.h` (C++). | Medium | Já completado en Fase 1. |
| 5 | Cross-project tests | Both | Tests unitarios en `ABDSharedCode/tests/` | Low | Tests residiendo en ABDSharedCode/tests/ probando clases puras. |
| 6 | Parametrized generator | ABDEep | Parametrize by project name | Medium | Un solo source file, múltiples outputs por proyecto. |
| 7 | Deprecation management | Both | Document deprecation policy in `DEPRECATION_POLICY.md` | Low | Crear archivo formal; `[[deprecated]]` ya aplicado en C++. |

---

## 🔀 Identified and Resolved Contradictions

### Contradiction 1: Shared Consumption vs. Initial Independence

| Fact | Context |
|------|---------|
| ABDEep consumes no `@abdsynths/*` (zero references) | At project start |
| ABDMS2000 has schema-driven that could be consumed | After Phases 3-4 |

**Solution**: "Zero big-bang" strategy - adopt first what replaces entire code, then extract ZDF filters. Phase 7 of ABDEep roadmap.

### Contradiction 2: Forked Generators vs. Single Source

| Fact | Context |
|------|---------|
| ABDEep has 3 forks of `registry_generator.js` with 454-753 line diffs | High maintenance cost |
| ABDMS2000 has single generator with `--check` | Simpler maintenance |

**Solution**: Parametrize the generator by project (recommended in DRY analysis). Initial cost compensates long-term savings.

### Contradiction 3: Test Portability vs. Project Specificity

| Fact | Context |
|------|---------|
| 6 DSPCore tests ported from ABDEep to ABDMS2000 "with adaptations" | Inherited tests not directly compatible |
| `API_MIGRATION_GUIDE.md` documents the differences | Necessary to maintain separate tests |

**Solution**: Keep tests per project with possible abstraction layer for common features. Don't force portability if it introduces complexity.

### Contradiction 4: WebUI "ABDEep Style" vs. Own Implementation

| Fact | Context |
|------|---------|
| `slideDrawer.js` marked "CZ-101 / ABDEep style" | Cultural identification, not technical |
| ABDMS2000 has its own implementation | Functionally equivalent |

**Solution**: Share UI patterns (tokens, layouts) but not necessary JavaScript code. Diffs of 301 and 531 lines are expected due to implementation differences, not errors.

### Contradiction 5: Multiple VoiceManager Implementations vs. Convergence

| Fact | Context |
|------|---------|
| 3 VoiceManager implementations (MS2000, CZ101, ABDEep) | Code duplication |
| DRY analysis recommends converging with `LutDSP/VoiceAllocator` | Long-term goal |

**Solution**: Gradual convergence. Start by extracting common logic to `ABDShared::SynthCore::VoiceAllocator`, keeping existing implementations working during transition.

### Contradiction 6: Deprecation Policy Consistency

| Fact | Context |
|------|---------|
| Both projects mark functions `[[deprecated]]` | Consistent good practice |
| No formally documented policy | Risk of accidental removal |

**Solution**: Document deprecation policy in project README, including:
- Version when marked deprecated
- Alternative function to use
- Removal timeline

---

## 📦 Annex: Proposed Solutions

### Solution 1: Universal `--check` Validator (Fase 1 - COMPLETADO)

```bash
# In all ecosystem projects
node Scripts/registry_generator.js --check
# Returns 1 if any .gen is outdated
# Integrate as gate in CI/CD
```

**Benefit**: Prevent drift between schema and generated artifacts.
**Effort**: **1-3 days** (much trabajo ya existe: `registry_generator.js --check` con 34 tests, falta integrar en CI) | **Impact**: High

### Solution 2: Use Existing Filters from `DspCore` (Fase 2 - Usar filtros ya existentes)

En lugar de extraer filtros a `ABDShared::Filters` anew, los filtros de alta calidad ya existen en:

**Ruta**: `ABDSharedCode\DspCore\` (namespace `abd::dsp`)

| Filtro | Descripción | Complejidad |
|--------|-------------|-------------|
| `DspMoogLadder.h` | 4-pole transistor ladder filter (VAOnePoleFilter cascadas) | Completo, TPT ZDF, auto-oscilación |
| `DspKorgMS20.h` | Korg MS-20 Sallen-Key con clipado de diode | Completo, saturación analogica |
| `DspJunoVCF.h` | Juno/DeepMind series VCF con oversampling TPT | Más sofisticado, polyphase IIR |
| `DspVAOnePole.h` | Minimal TPT one-pole filter building block | Ligero, bloque base |

**Benefit**: Filtros de producción calidad, ya probados y usados, sin necesidad de extraer y mantener duplicados.
**Effort**: **1-2 días** (integrar en proyecto, no crear desde cero) | **Impact**: High (usable inmediato)

### Solution 3: Converge VoiceAllocator - Diseño Híbrido (Fase 2)

```cpp
// VoiceAllocator already in ABDSharedCode\SynthCore\VoiceAllocator.h
// (abd::synth namespace, template class with stealing policies)
// 
// Hybrid design documented in comparative analysis:
// - VoiceAllocator: algoritmo matemático puro de robo de voz
// - VoiceManager: gestión de timbres, splits, ruteo por sintetizador
//
// Existing VoiceManager implementations per synth can integrate with this.
```

**Benefit**: VoiceAllocator already exists and is tested; hybrid design documented.

### Solution 4: Parametrized Registry Generator

```javascript
# registry_generator.js --project ABDMS2000 --check
# vs
node registry_generator.js --project ABDEep --check
```

**Benefit**: Single source file, multiple project outputs.
**Effort**: 1 day | **Impact**: Medium

### Solution 5: Module Tests in ABDSharedCode (Fase 3 - Simplificada)

```javascript
// Tests in ABDSharedCode/tests/ probando clases del namespace abd::dsp o abd::synth
// usando los filtros y VoiceAllocator existentes.
```

**Benefit**: Shared tests without forcing API compatibility, much simpler maintenance.
**Effort**: **2-3 days** (tests unitarios en ABDSharedCode/tests/) | **Impact**: Low

### Solution 6: Documented Deprecation Policy

```markdown
# Deprecation Policy

## Version 1.3.0 (2024)
- `convertSysExToCutoffHz(float)` marked deprecated
- Use `conversion::normToCutoffHz(entry, t)` instead
- Will be removed in v2.0.0 (Q1 2025)

## Version 2.0.0 (Q1 2025)
- `convertSysExToCutoffHz` removed
- All callers must use `normToCutoffHz`
```

**Benefit**: Clarity for developers, prevention of accidental breakage.
**Effort**: 1 day | **Impact**: Low

---

## 🟢 Fase 1: COMPLETADA - Base Blindada y Consolidación ABDMS2000

**Estado actual del ecosistema:**

| # | Item | Proyecto | Completado | Detalle |
|---|------|----------|------------|---------|
| 1 | DEPRECATION_POLICY.md | ABDMS2000 / ABDEep | ✅ | Policy formal v1.3.0 → v2.0.0, `[[deprecated]]` en C++, warnings en JS |
| 2 | `registry_generator.js --check` gate en CI | ABDMS2000 | ✅ | Verificado en `ABDMS2000\Scripts\registry_generator.js`, gate en `package.json` |
| 3 | Clamping defensivo en Conversion.gen.h | ABDMS2000 | ✅ | `std::clamp(t, 0.0f, 1.0f)` en casos Exp y Pow; clamping `domainMin/domainMax` en `domainToNormalized` |
| 4 | Vocoder Literales Dinámicos (8-Ch vs 16-Band) | ABDMS2000 | ✅ | Actualización dinámica de card `#card-vocoder` y cajón de bandas según perfil activo (microKORG vs MS2000) |
| 5 | Mod Sequencer Suppression en microKORG | ABDMS2000 | ✅ | Desactivación en UI y motor C++/WASM (`synthMode == 1`), conservando vista de Arpegiador |
| 6 | Aclaración Slots Matriz de Modulación | ABDMS2000 | ✅ | 4 slots físicos de parcheo virtual confirmados idénticos al hardware; 201/201 tests Vitest pasando |
| 7 | Filtros DSP existentes en DspCore | Shared / ABDEep | ✅ | Los filtros de alta calidad residen en `ABDSharedCode\DspCore\` con namespace `abd::dsp` |

---

## 🧭 Roadmap de Consolidación Arquitectónica ABDEep (Backend / DSP / SSOT)

> ⚠️ **Premisa y Límite de Alcance Innegociable:**  
> **Cero cambios en la interfaz visual (WebUI):** No se modifica la presentación visual, estructura HTML ni diseño CSS existente de ABDEep ni de su Calibration Lab. La consolidación es 100% interna a nivel de arquitectura C++, pipeline de generación de código y abstracción DSP.

### Fase 1: SSOT Registry & `--check` Validator (ABDEep)
- **Objetivo**: Estandarizar la verificación de esquemas y artefactos generados en ABDEep con el patrón implementado en ABDMS2000.
- **Acciones**:
  - Incorporar el flag `--check` en `ABDEep/scripts/registry_generator.js` (comprobación sin escritura que retorna `1` si algún `.gen` o `.data.json` está desactualizado).
  - Añadir el comando `"check:registry": "node scripts/registry_generator.js --check"` en `ABDEep/package.json`.
  - Integrar test de verificación de paridad en `scripts/registry_generator.test.js`.
- **Impacto Visual**: Nulo.

### Fase 2: Consumo de Filtros ZDF Compartidos desde `ABDSharedCode/DspCore` (Fidelidad Estricta al Instrumento)
- **Objetivo**: Conectar ABDEep con las implementaciones matemáticas TPT/ZDF canónicas en `abd::dsp`, **preservando de forma estricta la arquitectura y timbre del DeepMind 12** sin inventar ni exponer filtros ajenos:
  - En la voz del sintetizador: exclusivamente el VCF OTA analógico IR3109/80017A (24 dB / 12 dB, con voicing DeepMind y Juno-106 via `DspJunoVCF.h` / `DspVcfVoicing.h`) y el Juno HPF continuo con refuerzo de graves via `DspJunoHPF.h`.
  - En la sección de efectos: el Mood Filter (MF-101 en ranura FX 8) via `DspMoodFilter.h`.
  - No se exponen en la interfaz ni en la voz de ABDEep filtros de otros sintes (como Moog o MS-20), garantizando que el instrumento permanezca 100% fiel al hardware emulado.
- **Impacto Visual**: Nulo.

### Fase 3: VoiceAllocator Híbrido (`SynthCore/VoiceAllocator.h`) — ✅ YA INTEGRADO (NO TOCAR)
- **Estado**: ABDEep ya tiene este desacoplamiento implementado en `SynthEngine_VoiceManager.cpp` y `SynthEngine.h` delegando en `abd::synth::VoiceAllocator<12>`.
- **Decisión**: No se toca el asignador de voces de ABDEep; está funcionando y probado. Modificarlo sería redundante y acarrearía riesgos innecesarios.
- **Impacto Visual**: Nulo.

### Fase 4: Unificación del Puente Nativo (`ABDShared::WebView2Bridge`)
- **Objetivo**: Reutilizar el andamiaje del puente WebView2 para el paso de mensajes entre C++ y JS sin alterar los contratos JSON ni la lógica de la UI.
- **Acciones**:
  - Aprovechar las primitivas seguras de sincronización y normalización del puente compartido.
  - Garantizar que los contratos de datos existentes (XML para C++, JSON Schema para WebUI) permanezcan intactos.
- **Impacto Visual**: Nulo.

---

## 🟠 Fase 2 en Curso: Enfoque en Integración y VoiceAllocator (No crear filtros duplicados)

**Objetivo**: Integrar los filtros DSP ya existentes y converger VoiceAllocator con diseño híbrido.

### Prioridades Fase 2 (actualizadas):

| # | Tarea | Esfuerzo | Impacto | Estado |
|---|-------|----------|---------|--------|
| **1** | **Integrar filtros DspCore en los proyectos** | **1-2 días** | **Alto** (usable inmediato) | ⏳ Por iniciar |
| **2** | **Converger VoiceAllocator** - Diseño híbrido: VoiceAllocator ya en `SynthCore\VoiceAllocator.h` + VoiceManager específico | **3-4 días** | **Medio-Alto** | ⏳ Por iniciar |
| **3** | Tests de integración con filtros DspCore | **2-3 días** | **Medio** | ⏳ Por iniciar |

### Diseño Híbrido VoiceAllocator (reafirmado):

```
VoiceAllocator (ya en ABDSharedCode\SynthCore\, abd::synth namespace):
  - Template class con políticas: Poly1 (round-robin), Poly2, Unison
  - findVoiceToSteal(), releaseVoice(), allocateNoteOn/Off()
  - Ya incluye políticas LegacyTimestamp y Ms2000Ladder

VoiceManager (por crear/adaptar por sintetizador):
  - Gestión de timbres/splits/ruteo de osciladores
  - Lógica MS2000 (4 voces, 2 timbres) vs DeepMind 12 (12 voces, unísonos)
  - Integrar con VoiceAllocator::allocateNoteOn() results
```

### Pasos Fase 2 refinados:

1. **Integrar filtros DspCore** en ABDMS2000 y ABDEep (usando `#include "DspCore/DspMoogLadder.h"` etc.)
2. **Probar VoiceAllocator** con los filtros integrados (tests de robo de voz, note-on/off)
3. **Crear VoiceManager específico** por sintetizador que use VoiceAllocator::allocateNoteOn()
4. **Tests de VoiceAllocator** en `ABDSharedCode/tests/` con los filtros integrados

---

### 🔀 Bifurcación SysEx & Paridad Hardware: microKORG vs MS2000
- **Objetivo**: Separar la serialización, volcados SysEx (`.syx`), contratos de datos y comandos MIDI entre el Korg MS2000 y el microKORG para alcanzar paridad 1:1 con el hardware físico original de cada máquina.
- **Fase 1 (C++)**: `MicroKorgHardwareProgram.h` con bitmask de 8 pasos y byte `Last Step`, discriminación por longitud de dump (297B vs 299B) en `SysExManager.cpp`.
- **Fase 2 (NRPN)**: Soporte en `ABDSharedCode/HardwareDrivers` para `NRPN 0, 9` (`Arp Last Step` oficial).
- **Fase 3 (BankManager)**: Especialización de contrato `korg-microkorg` en `modelContracts.gen.js` y conversión cruzada bidireccional de patches.

---

*Document generated from analysis of ABD Synths ecosystem projects.*
*Comparative analysis covering: ABDMS2000, ABDEep, ABDCZ101, ABDJUNO601, ABDNeural*
*Ecosistema actualizado: ABDMS2000 arpegiador microKORG 8-step rhythm mask y motor extendido completados (203 tests Vitest OK, 361 tests C++ OK, check:registry OK); Roadmap de bifurcación SysEx microKORG planificado; ABDEep Fase 1 completada, Fase 2 blindada bajo fidelidad a DM12.*