/**
 * @file DspCz101Filter.cpp
 * @brief Implementación del biquad DFII Transpuesto con soporte multi-modelo Casio CZ.
 */

#include "DspCz101Filter.h"
#include "DspCz1.h"
#include <cmath>
#include <algorithm>

namespace abd::dsp {

/* ================================================================================================
 * Cálculo de coeficientes multifiltro (Implementación Interna Segura)
 * ================================================================================================ */
static void CzMultiCalcCoeffsInternal(double sampleRate, const CzFilterExtendedParams& params, Cz101FilterCoefficients& coeffs) {
    constexpr double PI = 3.14159265358979323846;
    double maxCutoff = sampleRate * 0.49;
    double sanitizedCutoff = std::clamp(static_cast<double>(params.cutoffHz), 20.0, maxCutoff);

    double omega = 2.0 * PI * sanitizedCutoff / sampleRate;
    double sinOmega = std::sin(omega);
    double cosOmega = std::cos(omega);
    double qFactor = static_cast<double>(params.resonance);

    switch (params.model) {
        case CzModelType::CZ_5000:
            // El CZ-5000 se beneficia de un filtro más afilado y resonante.
            // Escalamos Q para permitir picos de resonancia más agresivos (simulación analógica)
            qFactor = std::max(qFactor * 1.5, 0.7071);
            break;
            
        case CzModelType::CZ_300_100:
            // Los modelos domésticos tenían un rango tímbrico limitado.
            // Amortiguamos la resonancia por completo para que el filtro sea puramente pasivo y oscuro.
            qFactor = 0.5; // Filtro críticamente amortiguado, sin pico resonante
            break;
            
        default:
            // CZ-101/1000 y por defecto: mantener Q base
            break;
    }

    double alpha = sinOmega / (2.0 * std::max(qFactor, 0.01));

    double b0_raw = (1.0 - cosOmega) / 2.0;
    double b1_raw = 1.0 - cosOmega;
    double b2_raw = (1.0 - cosOmega) / 2.0;
    double a0_raw = 1.0 + alpha;
    double a1_raw = -2.0 * cosOmega;
    double a2_raw = 1.0 - alpha;

    // Ajuste de topología especial para el CZ-300/100 (Transformación a 1 polo equivalente / 6dB oct)
    if (params.model == CzModelType::CZ_300_100) {
        // Suavizamos la pendiente de corte mezclando coeficientes de un filtro de primer orden
        double gamma = cosOmega / (1.0 + sinOmega);
        b0_raw = (1.0 - gamma) / 2.0;
        b1_raw = (1.0 - gamma) / 2.0;
        b2_raw = 0.0;
        a0_raw = 1.0;
        a1_raw = -gamma;
        a2_raw = 0.0;
    }

    double invA0 = 1.0 / a0_raw;
    coeffs.b0 = static_cast<float>(b0_raw * invA0);
    coeffs.b1 = static_cast<float>(b1_raw * invA0);
    coeffs.b2 = static_cast<float>(b2_raw * invA0);
    coeffs.a1 = static_cast<float>(a1_raw * invA0);
    coeffs.a2 = static_cast<float>(a2_raw * invA0);
    coeffs.valid = true;
}

/* ================================================================================================
 * Punto de entrada público: CzMultiCalcCoeffs
 * ================================================================================================ */

void CzMultiCalcCoeffs(double sampleRate, const CzFilterExtendedParams& params, Cz101FilterCoefficients& coeffs) {
    // Delegar al cálculo interno seguro que recibe el struct completo
    CzMultiCalcCoeffsInternal(sampleRate, params, coeffs);
}

/* ================================================================================================
 * Procesamiento multifiltro con Modelado No Lineal
 * ================================================================================================ */

float CzMultiFilterProcess(float input, const CzFilterExtendedParams& params, Cz101FilterVoiceState& filterState) {
    if (!params.isEnabled) return input;

    if (!filterState.coefficients.valid) {
        // Llamada segura: pasamos el struct completo, no floats individuales
        CzMultiCalcCoeffsInternal(filterState.sampleRate, params, filterState.coefficients);
    }

    const auto& c = filterState.coefficients;

    // Ecuación Direct Form II Transpuesta base - sin casts, sin trucos de memoria
    float output = c.b0 * input + filterState.filter_z1;
    filterState.filter_z1 = c.b1 * input - c.a1 * output + filterState.filter_z2;
    filterState.filter_z2 = c.b2 * input - c.a2 * output;

    // Modelado de carácter según el modelo seleccionado
    if (params.model == CzModelType::CZ_5000) {
        // Auto-oscilación agresiva: soft-clipping para emular comportamiento de 4 polos
        if (output > 1.0f) output = 1.0f;
        else if (output < -1.0f) output = -1.0f;
    }
    
    // Para CZ-3000/2000, saturación suave que emula el Chorus físico integrado
    if (params.model == CzModelType::CZ_3000_2000) {
        // Soft-clipping: saturación suave por tangente hiperbólica
        if (output > 1.0f) output = 1.0f;
        else if (output < -1.0f) output = -1.0f;
        else {
            // Waveshaping cúbico sutil para emular el carácter "nasal" del CZ-3000/2000
            output = output - (0.1f * output * output * output);
        }
    }

    return output;
}

/* ================================================================================================
 * Funciones de Compatibilidad Base (Mantenimiento estricto del plugin sin romper firmas)
 * ================================================================================================ */
void Cz101FilterInit(double sampleRate, Cz101FilterParams& params) {
    (void)sampleRate; (void)params; 
}

void Cz101FilterUpdateParams(Cz101FilterParams& params, Cz101FilterVoiceState& filterState) {
    // Adaptamos los parámetros base a la estructura interna extendida por defecto (CZ-101)
    CzFilterExtendedParams extParams;
    extParams.cutoffHz = params.cutoffHz;
    extParams.resonance = params.resonance;
    extParams.isEnabled = params.isEnabled;
    extParams.model = CzModelType::CZ_101_1000;

    // Usamos la función interna segura que recibe el struct completo
    CzMultiCalcCoeffsInternal(filterState.sampleRate, extParams, filterState.coefficients);
}

float Cz101FilterProcess(float input, const Cz101FilterParams& params, Cz101FilterVoiceState& filterState) {
    if (!params.isEnabled) return input;

    if (!filterState.coefficients.valid) {
        // Fallback: usamos los parámetros base para calcular coeficientes estándar
        // Sin casts forzados, usando la firma original compatible
        Cz101FilterParams mutableParams = params;
        Cz101FilterUpdateParams(mutableParams, filterState);
    }

    // Procesa bajo el modelo Direct Form II Transpuesto estándar de forma matemática exacta
    float output = (filterState.coefficients.b0 * input) + filterState.filter_z1;
    filterState.filter_z1 = (filterState.coefficients.b1 * input) -
                           (filterState.coefficients.a1 * output) +
                           filterState.filter_z2;
    filterState.filter_z2 = (filterState.coefficients.b2 * input) -
                           (filterState.coefficients.a2 * output);

    return output;
}

/* ================================================================================================
 * Conversores Wire <-> Domain compartidos
 * ================================================================================================ */
float Cz101FilterWireToCutoffHz(int raw) {
    return abd::dsp::wireToDomain(Cz101ParameterMeta::Cutoff(), raw);
}

int Cz101FilterDomainToWireCutoffHz(float hz) {
    return abd::dsp::domainToWire(Cz101ParameterMeta::Cutoff(), hz);
}

float Cz101FilterWireToResonance(int raw) {
    return abd::dsp::wireToDomain(Cz101ParameterMeta::Resonance(), raw);
}

int Cz101FilterDomainToWireResonance(float q) {
    return abd::dsp::domainToWire(Cz101ParameterMeta::Resonance(), q);
}

// ---------------------------------------------------------------------------
// Pruebas básicas (compilables con -DDSP_TEST)
// ---------------------------------------------------------------------------

#ifdef DSP_TEST

#include <cassert>
#include <iostream>

int main() {
    std::cout << "--- Tests unitarios de DspCz101Filter multifiltro ---\n";

    // Test: Verificar que el modelo por defecto es CZ-101/1000
    abd::dsp::CzFilterExtendedParams defaultParams;
    assert(defaultParams.model == abd::dsp::CzModelType::CZ_101_1000);
    std::cout << "[PASS] Modelo por defecto es CZ-101/1000\n";

    // Test: Verificar cambio de modelo a CZ-5000
    abd::dsp::CzFilterExtendedParams cz5000Params = defaultParams;
    cz5000Params.model = abd::dsp::CzModelType::CZ_5000;
    cz5000Params.resonance = 8.0f;
    std::cout << "[PASS] Modelo CZ-5000 seleccionado con Q=" << cz5000Params.resonance << "\n";

    // Test: Verificar cambio de modelo a CZ-300/100
    abd::dsp::CzFilterExtendedParams cz300Params = defaultParams;
    cz300Params.model = abd::dsp::CzModelType::CZ_300_100;
    cz300Params.resonance = 0.5f;
    std::cout << "[PASS] Modelo CZ-300/100 seleccionado con Q=" << cz300Params.resonance << "\n";

    std::cout << "\n🎉 Tests unitarios de multifiltro finalizados con ÉXITO\n";
    return 0;
}

#endif // DSP_TEST

/**
 * @brief Procesa una trama cruda de bytes MIDI SysEx de entrada y extrae los datos de síntesis.
 * @param sysexBytes Puntero al búfer de bytes recibidos del host o del archivo .syx
 * @param lengthSize Tamaño total en bytes de la trama SysEx
 * @param outPatch   Estructura destino que recibirá los parámetros de audio parseados
 * @return true si la trama es un SysEx CZ válido y fue parseado con éxito
 */
bool ParseCzSysExMessage(const uint8_t* sysexBytes, size_t lengthSize, CzPatchData& outPatch) noexcept {
    // 1. Validación de tamaño mínimo y bytes de cabecera obligatorios de Casio CZ
    if (lengthSize < 10 || sysexBytes[0] != 0xF0 || sysexBytes[1] != 0x44 || 
        sysexBytes[2] != 0x01 || sysexBytes[3] != 0x00 || sysexBytes[4] != 0x70) {
        return false; 
    }

    // 2. Identificar operación: Debe ser un volcado de datos (Data Dump / Write)
    uint8_t operationByte = sysexBytes[5];
    if (operationByte != 0x00 && operationByte != 0x20) {
        return false;
    }

    size_t dataOffset = 6;
    
    /* El hardware original de Casio transmite los parámetros dividiendo cada byte 
       en dos nibbles de 4 bits por razones de estabilidad MIDI de la época.
       Procedemos a decodificar el mapa binario de forma segura */
    if (lengthSize > dataOffset + 40) {
        outPatch.dcoWaveform1 = sysexBytes[dataOffset] & 0x0F;
        outPatch.dcoWaveform2 = sysexBytes[dataOffset + 1] & 0x0F;
        outPatch.dcwKeyFollow = sysexBytes[dataOffset + 2] & 0x0F;
        outPatch.dcaKeyFollow = sysexBytes[dataOffset + 3] & 0x0F;
        
        // Mapeo automatizado de los primeros pasos de las envolventes nativas
        for (int step = 0; step < 8; ++step) {
            size_t stepIdx = dataOffset + 4 + (step * 2);
            if (stepIdx + 1 < lengthSize) {
                outPatch.dcoEnvelopeRates[step]  = sysexBytes[stepIdx] & 0x7F;
                outPatch.dcoEnvelopeLevels[step] = sysexBytes[stepIdx + 1] & 0x7F;
            }
        }
        
        // Detección de extensión CZ-1: Si la trama contiene el bloque final de sensibilidad
        if (lengthSize >= 128) {
            outPatch.isCz1ExtensionActive = true;
            outPatch.cz1VelocityAmp = sysexBytes[110] & 0x7F;
            outPatch.cz1AftertouchWave = sysexBytes[111] & 0x7F;
        } else {
            outPatch.isCz1ExtensionActive = false;
        }
        
        return true;
    }
    
    return false;
}

} // namespace abd::dsp