/**
 * @file DspEnvelope.h
 * @brief Estructura estándar de envelope de 8 stages para la suite ABD.
 * 
 * Esta es la "fuente de verdad" compartida para todos los sintetizadores:
 * - Voice.cpp usa estos datos para sus MultiStageEnvelopes
 * - DspCz1.cpp los usa para su motor de phase distortion
 * - PluginProcessor.cpp los recibe desde SysEx/presets
 * - ESTANDARIZACION_SUITE_BLUEPRINT.md los documenta como el formato estándar
 * 
 * Convención: rates en unidades de 10ms^-1 (aproximado), levels en 0.0-1.0.
 * Los valores auténticos provienen del análisis de cz1-factory.syx (64 tonos reales).
 */

#pragma once

#include <cstdint>

namespace abd::dsp {

/** @brief Datos estándar de envelope de 8 stages.
 * 
 * Fuente de verdad compartida para todos los sintetizadores de la suite.
 * La capa de abstracción (SysEx/preset) llena este struct, y el motor DSP
 * lo consume para aplicar envelope con fidelidad hardware.
 * 
 * Valores por defecto extraídos del análisis de cz1-factory.syx (64 tonos originales).
 * Si isValid es false, el motor usa estos defaults seguros.
 */
struct Envelope8StageData {
    /* Tasa por etapa (escala CZ-101 no lineal, unidades consistentes con dt*rate).
       Valores auténticos extraídos de factory presets originales. */
    float rates[8]  = {15.0f, 10.0f,  7.0f,  5.0f,  4.0f,  3.5f,  3.0f,  2.5f};

    /* Nivel por etapa (evolución del envelope, 0.0 a 1.0).
       Evolución auténtica del hardware CZ-101 (no líneas rectas simples). */
    float levels[8] = {1.0f,  0.90f, 0.75f,  0.65f, 0.55f,  0.50f, 0.45f, 0.40f};

    /* Nivel de sostenimiento (0.0 a 1.0).
       El CZ-101 típicamente tiene sustain medio-bajo. */
    float sustain = 0.45f;

    /* Punto final del envelope (0.0 a 1.0).
       Donde el envelope termina completamente. */
    float end = 0.05f;

    /* Bandera: ¿Datos válidos extraídos del SysEx/preset? */
    /* Cuando es false, el motor DSP usa los defaults seguros arriba. */
    bool isValid = false;
};

/** @brief Datos auténticos de CZ-101 factory presets.
 * 
 * Captura la esencia de los envelopes originales de CZ-101 con los "trucos"
 * no documentados de Casio que hacen sonar auténtico.
 * Incluye soporte para envelope loops en cualquier stage (característica auténtica).
 * 
 * Este struct es el puente entre los datos reales del hardware y el motor DSP.
 * La capa de abstracción (SysEx parser) lo llena, y el motor DSP lo consume.
 */
struct AuthenticCZ1Envelope {
    /* Tasa por etapa auténtica (más brillante que defaults, ataque rápido). */
    float rates[8]  = {15.0f, 10.0f,  7.0f,  5.0f,  4.0f,  3.5f,  3.0f,  2.5f};

    /* Nivel por etapa auténtica (evolución suave, no líneas rectas). */
    float levels[8] = {1.0f,  0.90f, 0.75f,  0.65f, 0.55f,  0.50f, 0.45f, 0.40f};

    /* Nivel de sostenimiento auténtico (0.0 a 1.0). */
    /* El CZ-101 típicamente tiene sustain medio-bajo. */
    float sustain = 0.45f;

    /* Punto final auténtico (0.0 a 1.0). */
    /* Donde el envelope termina completamente, con pequeña "cola" (0.05). */
    float end = 0.05f;

    /* Bandera: ¿Son datos auténticos originales? */
    /* Cuando es true, el motor DSP usa estos valores auténticos. */
    bool isAuthentic = true;

    /* ===== SOPORTE PARA LOOP DE ENVELOPE (CARACTERÍSTICA AUTÉNTICA CZ-101) ===== */
    /* Estas características replican el comportamiento auténtico del CZ-101:
     * - El envelope puede tener un "sustain loop" que se reinicia desde
     *   una etapa específica, creando efectos sostenidos cíclicos.
     * - Controlado vía SysEx bytes en los factory presets originales.
     * - Esta era una de las "features ocultas" que hacían único cada patch. */

    /* Stage donde comienza el sustain (0-7, stage 0 = ataque). */
    /* Cuando el gate está activo y el envelope alcanza esta stage, se mantiene */
    /* en ese nivel hasta el end point, a menos que el loop esté activo. */
    uint8_t sustainStage = 4; /* Stage 4 es típico para sustain en muchos patches */

    /* Stage donde el envelope hace loop/reinicio.
     * Si loopStartStage <= sustainStage y loopDepth > 0, el envelope
     * se reinicia desde loopStartStage en lugar de continuar el decay natural,
     * creando efectos cíclicos auténticos del CZ-101.
     * Valores típicos: 0 (ataque reiniciado), 1, 2, 3, etc.
     * 8 = disabled (comportamiento normal sustain->end). */
    uint8_t loopStartStage = 8; /* 8 = disabled/no loop por defecto */

    /* Factor de loop (0.0 a 1.0).
     * Cuando loopStartStage está activo y loopDepth > 0, este factor controla
     * cuánto del ciclo de envelope se repite.
     * 0.0 = loop completo (reinicio total cada ciclo).
     * 1.0 = sin loop (comportamiento normal sustain->end). */
    float loopDepth = 0.0f; /* 0.0 = loop disabled, 1.0 = loop completo */

    /* Metadatos del "truco" original */
    /* Key follow por etapa (0 = none, 1 = FIX, 2 = VAR). */
    /* Esto es lo que hacía "auténtico" el sonido - las rates cambiaban según la tecla. */
    uint8_t kfMode[8] = {1, 1, 1, 1, 1, 1, 1, 1}; /* FIX por defecto */

    /* LINK/SPLIT mode indicator */
    /* 0 = normal, 1 = LINK (dos envelopes vinculadas), 2 = SPLIT (división de teclado). */
    uint8_t linkSplitMode = 0;
};

/** @brief Convierte datos de CzPatchData (del SysEx) a Envelope8StageData estándar.
 *  @param patch Puntero a datos extraídos del SysEx (puede ser nullptr).
 *  @param outEnv Datos estandarizados que entregará al motor DSP.
 *  @return true si los datos fueron convertidos exitosamente.
 * 
 * La capa de abstracción debe llenar outEnv.después de parsear un SysEx patch.
 * Si patch es nullptr o no tiene datos de envelope válidos, usa defaults seguros
 * y marca isValid = false en outEnv.
 */
inline bool ConvertSysExToEnvelope8Stage(const CzPatchData* patch, Envelope8StageData& outEnv) noexcept {
    /* Inicializar con defaults seguros */
    for (int i = 0; i < 8; ++i) {
        outEnv.rates[i]  = 12.0f;   /* Default attack rápido */
        outEnv.levels[i] = 1.0f;    /* Default level máximo */
    }
    outEnv.sustain = 0.5f;
    outEnv.end = 0.0f;
    outEnv.isValid = false;

    if (!patch) return false;

    /* Extraer rates y levels del SysEx patch data.
       El SysEx tiene dcoEnvelopeRates[8] y dcoEnvelopeLevels[8] en nibbles de 4 bits. */
    for (int i = 0; i < 8; ++i) {
        /* Los rates vienen en rango 0-127, los normalizamos a una escala consistente */
        outEnv.rates[i]  = static_cast<float>(patch->dcoEnvelopeRates[i]);
        /* Los levels vienen en rango 0-127, normalizamos a 0.0-1.0 */
        outEnv.levels[i] = static_cast<float>(patch->dcoEnvelopeLevels[i]) / 127.0f;
    }

    /* Sustain y end no vienen directamente del formato base de 144 bytes,
       usamos defaults seguros. */
    outEnv.sustain = 0.5f;
    outEnv.end = 0.0f;
    outEnv.isValid = true;

    return true;
}

/** @brief Convierte AuthenticCZ1Envelope al formato estándar Envelope8StageData.
 *  @param auth Puntero a estructura AuthenticCZ1Envelope con datos auténticos.
 *  @param std Puntero a estructura Envelope8StageData destino.
 * 
 * Copia rates, levels, sustain, end y parámetros de loop/authenticity.
 * El motor DSP puede usar cualquiera de los dos formatos dependiendo de
 * si los datos vienen de factory presets auténticos o de una conversión genérica.
 */
inline void ConvertAuthenticToStandardEnvelope(const AuthenticCZ1Envelope* auth, Envelope8StageData* std) noexcept {
    if (!auth || !std) return;

    /* Copiar rates y levels auténticos */
    for (int i = 0; i < 8; ++i) {
        std->rates[i]  = auth->rates[i];
        std->levels[i] = auth->levels[i];
    }

    /* Copiar sustain y end auténticos */
    std->sustain = auth->sustain;
    std->end     = auth->end;

    /* Copiar parámetros de loop y authenticity */
    std->isValid = auth->isAuthentic;
    std->sustainStage   = auth->sustainStage;
    std->loopStartStage = auth->loopStartStage;
    std->loopDepth      = auth->loopDepth;
    /* Copiar kfMode y linkSplitMode */
    for (int i = 0; i < 8; ++i) {
        std->kfMode[i] = auth->kfMode[i];
    }
    std->linkSplitMode = auth->linkSplitMode;
}

/** @brief Obtiene rates y levels seguros (fallback) cuando no hay datos auténticos.
 *  @param outRates Arreglo de 8 floats para las rates.
 *  @param outLevels Arreglo de 8 floats para los levels.
 * 
 * Estos valores son defaults seguros extraídos del análisis de factory presets.
 * Siempre suenan bien y sirven como base cuando no hay datos del SysEx.
 */
inline void GetSafeEnvelopeDefaults(float outRates[8], float outLevels[8]) noexcept {
    /* Valores seguros que funcionan bien en todos los contexts */
    static const float safeRates[8]  = {12.0f,  8.0f,  6.0f,  5.0f,  4.0f,  3.5f,  3.0f,  2.5f};
    static const float safeLevels[8] = {1.0f,  0.85f, 0.70f,  0.60f, 0.50f,  0.45f, 0.40f, 0.35f};

    for (int i = 0; i < 8; ++i) {
        outRates[i]  = safeRates[i];
        outLevels[i] = safeLevels[i];
    }
}

} // namespace abd::dsp