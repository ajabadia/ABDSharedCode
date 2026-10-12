/**
 * @file DspCz101Filter.h
 * @brief Filtro biquad específico para la familia Casio CZ (Estado, Coeficientes, Multi-Modelo, SysEx y DAC 12-bit).
 */

#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace abd::dsp {

struct Cz101FilterParams {
    float cutoffHz = 2000.0f;      
    float resonance = 0.7f;        
    bool  isEnabled = true; 
};

struct Cz101FilterCoefficients {
    float b0 = 1.0f; 
    float b1 = 0.0f; 
    float b2 = 0.0f; 
    float a1 = 0.0f; 
    float a2 = 0.0f; 
    bool  valid = false; 
};

/**
 * @struct Cz101FilterVoiceState
 * @brief Almacenado dentro de DspCz1VoiceState para mantener el aislamiento por voz.
 */
struct Cz101FilterVoiceState {
    float filter_z1 = 0.0f;            
    float filter_z2 = 0.0f;            
    Cz101FilterCoefficients coefficients; 
    double sampleRate = 44100.0;      
};

// Modelo de sintetizador CZ identificado
enum class CzModelType {
    CZ_101_1000 = 0,   // Filtro biquad 2 polos estándar, lineal y equilibrado
    CZ_5000,           // Filtro de 4 polos simulado, auto-oscilación agresiva
    CZ_3000_2000,      // Lowpass con saturación, carácter nasal (soft-clipping)
    CZ_300_100         // Lo-Fi lowpass/bandpass, 1 polo equivalente, oscuro/vintage
};

/**
 * @struct CzFilterExtendedParams
 * @brief Parámetros extendidos del filtro CZ que incluyen la selección de modelo.
 */
struct CzFilterExtendedParams {
    float cutoffHz = 2000.0f;      
    float resonance = 0.7f;        
    bool  isEnabled = true; 
    CzModelType model = CzModelType::CZ_101_1000; 
};

// Declaraciones de funciones del filtro (Compatibilidad Base - sin romper firmas)
void Cz101FilterInit(double sampleRate, Cz101FilterParams& params);
void Cz101FilterUpdateParams(Cz101FilterParams& params, Cz101FilterVoiceState& filterState);
float Cz101FilterProcess(float input, const Cz101FilterParams& params, Cz101FilterVoiceState& filterState);

// Declaraciones auxiliares para el motor multifiltro extendido del proyecto
void CzMultiCalcCoeffs(double sampleRate, const CzFilterExtendedParams& params, Cz101FilterCoefficients& coeffs);
float CzMultiFilterProcess(float input, const CzFilterExtendedParams& params, Cz101FilterVoiceState& filterState);

/**
 * @struct CzPatchData
 * @brief Estructura binaria limpia que representa el mapa completo de parámetros de un sonido CZ.
 */
struct CzPatchData {
    uint8_t dcoWaveform1 = 0;
    uint8_t dcoWaveform2 = 0;
    uint8_t dcwKeyFollow = 0;
    uint8_t dcaKeyFollow = 0;
    
    uint8_t dcoEnvelopeRates[8]  = {0};
    uint8_t dcoEnvelopeLevels[8] = {0};
    uint8_t dcwEnvelopeRates[8]  = {0};
    uint8_t dcwEnvelopeLevels[8] = {0};
    uint8_t dcaEnvelopeRates[8]  = {0};
    uint8_t dcaEnvelopeLevels[8] = {0};
    
    uint8_t cz1VelocityAmp = 0;
    uint8_t cz1AftertouchWave = 0;
    bool isCz1ExtensionActive = false;
};

/**
 * @brief Procesa una trama cruda de bytes MIDI SysEx de entrada y extrae los datos de síntesis.
 */
bool ParseCzSysExMessage(const uint8_t* sysexBytes, size_t lengthSize, CzPatchData& outPatch) noexcept;

/**
 * @brief Cuantización fija de 12 bits vintage emulando el DAC original del Casio CZ.
 * @param sample Input de audio flotante (-1.0 a 1.0)
 * @return Muestra cuantizada con comportamiento de DAC de 12 bits
 */
inline float CzQuantize12BitVintage(float sample) noexcept {
    const float qScale = 2048.0f;
    float signedSample = sample * qScale;
    
    // Truncamiento directo (simula la pérdida de bits sin redondeo suave dithered)
    int16_t truncated = static_cast<int16_t>(signedSample);
    
    float truncationError = static_cast<float>(truncated) / qScale;
    return truncationError;
}

/**
 * @brief Aplica el modelado de cuantización vintage de 12 bits al búfer de audio.
 */
inline void CzQuantize12BitVintageBlock(const float* input, float* output, int numSamples, double sampleRate) noexcept {
    (void)sampleRate;
    if (!input || !output) return;
    
    for (int i = 0; i < numSamples; ++i) {
        output[i] = CzQuantize12BitVintage(input[i]);
    }
}

} // namespace abd::dsp