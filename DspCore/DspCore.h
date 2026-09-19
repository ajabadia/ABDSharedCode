/*
  ==============================================================================

    DspCore.h
    Utilidades del nucleo DSP libres de JUCE.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspCore,
    namespace abd::dsp). Hasta la Fase 1 [5/6] vivia en ABDNeural/Source/DSP/
    DspCore.h; el proyecto conserva un shim de compatibilidad que lo re-exporta
    con el nombre antiguo (`namespace dsp = abd::dsp;`), de modo que los 20+
    ficheros que decian dsp::AudioBuffer no cambian una linea. Editad AQUI, no
    en el shim.

    Puertos LITERALES de JUCE 8.0.12 (solo cambia el namespace):
      - juce_core/maths/juce_MathsFunctions.h  (jmin/jmax/jmap/jlimit,
        MathConstants, ignoreUnused, approximatelyEqual)
      - juce_audio_basics/buffers/juce_FloatVectorOperations.h/.cpp
        (ScopedNoDenormals, incluida la mascara MXCSR 0x8040: FTZ|DAZ)

    Regla de la casa (roadmap, "quitar juce_* del motor interno"): el port es
    una copia literal del fuente JUCE con el mismo orden de operaciones; el
    audio no puede cambiar ni un bit. Verificado por DSPReferenceTest
    (bit-exacto) y la paridad WASM<->nativo.

    WASM (emscripten): DSP_HAS_SSE_INTRINSICS no se define -> ScopedNoDenormals
    es un no-op, igual que en JUCE cuando no hay SSE/NEON (WebAssembly no
    flusha denormales y no necesita esta proteccion).

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
  #define DSP_HAS_SSE_INTRINSICS 1
#elif defined(__SSE2__)
  #define DSP_HAS_SSE_INTRINSICS 1
#endif

#if defined(DSP_HAS_SSE_INTRINSICS)
  #include <xmmintrin.h>
#endif

namespace abd::dsp
{

//==============================================================================
/** Handy function for avoiding unused variables warning.
    Port literal de dsp::ignoreUnused. */
template <typename... Types>
void ignoreUnused (Types&&...) noexcept {}

//==============================================================================
/** Common constants.
    Port literal de dsp::MathConstants (mismos literales long double). */
template <typename FloatType>
struct MathConstants
{
    /** A predefined value for Pi */
    static constexpr FloatType pi = static_cast<FloatType> (3.141592653589793238L);

    /** A predefined value for 2 * Pi */
    static constexpr FloatType twoPi = static_cast<FloatType> (2 * 3.141592653589793238L);

    /** A predefined value for Pi / 2 */
    static constexpr FloatType halfPi = static_cast<FloatType> (3.141592653589793238L / 2);

    /** A predefined value for Euler's number */
    static constexpr FloatType euler = static_cast<FloatType> (2.71828182845904523536L);

    /** A predefined value for sqrt (2) */
    static constexpr FloatType sqrt2 = static_cast<FloatType> (1.4142135623730950488L);
};

//==============================================================================
/** Equivalent to operator==, but suppresses float-equality warnings.
    Sustituto del juce::approximatelyEqual de dos argumentos tal y como lo
    consumen las aserciones de JUCE (solo se usa dentro de dspAssert); la
    version con tolerancias de JUCE no aporta nada aqui y no cruza al audio. */
template <typename Type>
constexpr bool approximatelyEqual (Type a, Type b) noexcept
{
    return a == b;
}

//==============================================================================
// Lavado de denormales (port de JUCE_UNDENORMALISE, definido en
// juce_core/maths/juce_MathsFunctions.h).
//
// JUCE lo define asi SOLO en plataformas Intel:
//     #define JUCE_UNDENORMALISE(x)   { (x) += 0.1f; (x) -= 0.1f; }
// y como macro vacio en el resto.
//
// Dos hechos que importan:
//
//  1. NO es un no-op aritmetico. (x + 0.1f) - 0.1f redondea dos veces en
//     float: aplasta los denormales (su proposito) pero tambien PERTURBA los
//     valores normales (~2e-8 absolutos para x ~ 1.0, mas que el presupuesto
//     de ulps del motor). No es un lavado inocuo: es un cambio de resultado.
//
//  2. JUCE_INTEL solo se define bajo JUCE_WINDOWS / JUCE_MAC / JUCE_LINUX, y
//     NUNCA bajo __wasm__ (juce_TargetPlatform.h). Es decir: juce::Reverb
//     calcula distinto en el build nativo y en el build WASM. Esa era, medida,
//     una de las fuentes de divergencia del escenario C de paridad.
//
// El port es un no-op deliberado y UNIFORME en las dos plataformas: la paridad
// bit a bit nativo <-> WASM es la invariante del motor. El problema que el
// macro resuelve (penalizacion por denormales en x86) ya lo cubre
// dsp::ScopedNoDenormals con FTZ|DAZ, que es determinista porque cambia el modo
// de la FPU y no toca las muestras; en WASM no hay esa penalizacion.
//
// La variante fiel a JUCE se conserva tras DSP_UNDENORMALISE_JUCE_POLICY para
// el test que demuestra que el port es literal (ver
// Tests/DspReverbParityTest.cpp): con la politica de JUCE el port coincide bit
// a bit con juce::Reverb, y sin ella difiere solo en lo que documenta arriba.
//
// Uso: se llama en los bucles de realimentacion, donde JUCE lo llamaba, para
// dejar marcado el punto exacto de la diferencia de politica.

// La condicion de arquitectura es PROPIA, no JUCE_INTEL: DspCore.h es un header
// JUCE-free y no incluye JUCE, asi que JUCE_INTEL estaria sin definir en el
// momento de parsear este fichero (el port habria sido siempre no-op y el test
// del port literal no probaria nada). Se replican las mismas arquitecturas que
// hacen que JUCE_INTEL sea 1: x86 de 32/64 bits. wasm32 define __wasm__ y
// ninguno de los macros de abajo, igual que en JUCE.
#if defined (_M_IX86) || defined (_M_X64) || defined (__i386__) || defined (__x86_64__)
  #define DSP_HOST_IS_X86 1
#else
  #define DSP_HOST_IS_X86 0
#endif

#if defined (DSP_UNDENORMALISE_JUCE_POLICY) && DSP_UNDENORMALISE_JUCE_POLICY
  #if DSP_HOST_IS_X86
    #define dspUndenormalise(x)     do { (x) += 0.1f; (x) -= 0.1f; } while (false)
  #else
    #define dspUndenormalise(x)     do { } while (false)
  #endif
#else
  /** No-op uniforme (ver arriba). Recibe la referencia para documentar el punto. */
  inline void undenormalise (float&) noexcept {}
  #define dspUndenormalise(x)         ::abd::dsp::undenormalise (x)
#endif

//==============================================================================
// Asercion del port (sustituye a jassert): aborta en Debug, se compila fuera
// en Release. Nunca afecta al resultado numerico.

#if ! defined (NDEBUG)
  #define dspAssert(expression)      assert (expression)
#else
  #define dspAssert(expression)      static_cast<void> (true && (expression))
#endif

//==============================================================================
// Some indispensable min/max functions
// Ports literales de dsp::jmax / dsp::jmin.

/** Returns the larger of two values. */
template <typename Type>
constexpr Type jmax (Type a, Type b)                                   { return a < b ? b : a; }

/** Returns the larger of three values. */
template <typename Type>
constexpr Type jmax (Type a, Type b, Type c)                           { return a < b ? (b < c ? c : b) : (a < c ? c : a); }

/** Returns the larger of four values. */
template <typename Type>
constexpr Type jmax (Type a, Type b, Type c, Type d)                   { return jmax (a, jmax (b, c, d)); }

/** Returns the smaller of two values. */
template <typename Type>
constexpr Type jmin (Type a, Type b)                                   { return b < a ? b : a; }

/** Returns the smaller of three values. */
template <typename Type>
constexpr Type jmin (Type a, Type b, Type c)                           { return b < a ? (c < b ? c : b) : (c < a ? c : a); }

/** Returns the smaller of four values. */
template <typename Type>
constexpr Type jmin (Type a, Type b, Type c, Type d)                   { return jmin (a, jmin (b, c, d)); }

/** Remaps a normalised value (between 0 and 1) to a target range.
    This effectively returns (targetRangeMin + value0To1 * (targetRangeMax - targetRangeMin)).
    Port literal de dsp::jmap (3 argumentos). */
template <typename Type>
constexpr Type jmap (Type value0To1, Type targetRangeMin, Type targetRangeMax)
{
    return targetRangeMin + value0To1 * (targetRangeMax - targetRangeMin);
}

/** Remaps a value from a source range to a target range.
    Port literal de dsp::jmap (5 argumentos). */
template <typename Type>
Type jmap (Type sourceValue, Type sourceRangeMin, Type sourceRangeMax, Type targetRangeMin, Type targetRangeMax)
{
    dspAssert (! approximatelyEqual (sourceRangeMax, sourceRangeMin)); // mapping from a range of zero will produce NaN!
    return targetRangeMin + ((targetRangeMax - targetRangeMin) * (sourceValue - sourceRangeMin)) / (sourceRangeMax - sourceRangeMin);
}

/** Constrains a value to keep it within a given range.
    Port literal de dsp::jlimit (mismo orden de comparaciones). */
template <typename Type>
Type jlimit (Type lowerLimit,
             Type upperLimit,
             Type valueToConstrain) noexcept
{
    dspAssert (lowerLimit <= upperLimit); // if these are in the wrong order, results are unpredictable

    return valueToConstrain < lowerLimit ? lowerLimit
                                         : (upperLimit < valueToConstrain ? upperLimit
                                                                          : valueToConstrain);
}

//==============================================================================
/** Port literal de juce::roundToInt.

    Mismo truco de doble precision que JUCE: sumar 2^52 + 2^51 al valor
    promovido a double y leer los 32 bits bajos. Redondea al entero mas
    cercano con los empates AL PAR (difiere de std::lround en los empates),
    y esa diferencia se oye: MidiMessage::floatValueToMidiByte cuantiza la
    velocity de nota con esta funcion.

    Todos los objetivos del proyecto (x86, x64 y wasm) son little-endian, que
    es la rama que JUCE compila con JUCE_BIG_ENDIAN a 0.
*/
template <typename FloatType>
int roundToInt (const FloatType value) noexcept
{
    static_assert (sizeof (int) == 4, "roundToInt asume int de 32 bits");

    union { int asInt[2]; double asDouble; } n;
    n.asDouble = ((double) value) + 6755399441055744.0;

    return n.asInt[0];
}

//==============================================================================
/** Helper class providing an RAII-based mechanism for temporarily disabling
    denormals on your CPU.
    Port literal de dsp::ScopedNoDenormals: misma mascara 0x8040 (FTZ | DAZ)
    sobre el MXCSR via _mm_getcsr/_mm_setcsr. */
class ScopedNoDenormals
{
public:
    ScopedNoDenormals() noexcept
    {
      #if defined (DSP_HAS_SSE_INTRINSICS)
        intptr_t mask = 0x8040;

        fpsr = (intptr_t) _mm_getcsr();
        _mm_setcsr ((unsigned int) (fpsr | mask));
      #endif
    }

    ~ScopedNoDenormals() noexcept
    {
      #if defined (DSP_HAS_SSE_INTRINSICS)
        _mm_setcsr ((unsigned int) fpsr);
      #endif
    }

private:
  #if defined (DSP_HAS_SSE_INTRINSICS)
    intptr_t fpsr;
  #endif
};

//==============================================================================
/** Sustituto minimo de juce::Range<Type>: solo la superficie que consumen
    FloatVectorOperations::findMinAndMax y AudioBuffer::findMinMax/
    getMagnitude (constructores, getStart, getEnd). */
template <typename Type>
class Range
{
public:
    Range() = default;

    Range (Type minValue, Type maxValue) noexcept
       : startValue (minValue), endValue (maxValue)
    {
    }

    Type getStart() const noexcept   { return startValue; }
    Type getEnd() const noexcept     { return endValue; }

private:
    Type startValue {};
    Type endValue {};
};

//==============================================================================
/** Port literal de juce::isPositiveAndBelow (juce_core/maths/
    juce_MathsFunctions.h): checks that a value is greater than or equal to
    zero, and less than a given upper limit. */
template <typename Type>
constexpr bool isPositiveAndBelow (Type value, Type upperLimit) noexcept
{
    dspAssert (Type() <= upperLimit); // makes no sense to call this unless upper limit is >= 0

    return Type() <= value && value < upperLimit;
}

//==============================================================================
/**
    Collection of simple vector operations on an array of floats.

    Port literal de juce::FloatVectorOperations (juce_audio_basics/buffers/
    juce_FloatVectorOperations.cpp), convertido a header-only: se portan los
    bucles escalares del fuente. Las rutas intrinsecas de JUCE (SSE/ARM NEON/
    vDSP) solo se compilan con JUCE_USE_SSE_INTRINSICS/JUCE_USE_ARM_NEON/
    JUCE_USE_VDSP_FRAMEWORK activos; este proyecto no define ninguno (ni en
    MSVC ni en emscripten), por lo que JUCE ejecuta estos mismos bucles
    escalares.

    Bit-exactitud (la razon de ser del port literal): addWithMultiply,
    copyWithMultiply y multiply (dest, src, mult) son add(d, mul(s, m))
    SEPARADOS, nunca fma() — JUCE no los fusiona y, bajo /fp:precise sin
    /arch:AVX ni /fp:fast, MSVC tampoco puede fusionar el bucle escalar. Cada
    producto redondea a f32 y cada suma redondea a f32, identico a la ruta
    intrinseca SSE2 de JUCE. Arbitrado por DSPReferenceTest (bit-exacto) y la
    paridad WASM<->nativo.

    @tags{Audio}
*/
struct FloatVectorOperations
{
    //==========================================================================
    template <typename Size>
    static void clear (float* dest, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = 0.0f;
    }

    template <typename Size>
    static void clear (double* dest, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = 0.0;
    }

    template <typename Size>
    static void fill (float* dest, float valueToFill, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = valueToFill;
    }

    template <typename Size>
    static void fill (double* dest, double valueToFill, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = valueToFill;
    }

    template <typename Size>
    static void copy (float* dest, const float* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = src[i];
    }

    template <typename Size>
    static void copy (double* dest, const double* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = src[i];
    }

    //==========================================================================
    template <typename Size>
    static void copyWithMultiply (float* dest, const float* src, float multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = src[i] * multiplier;
    }

    template <typename Size>
    static void copyWithMultiply (double* dest, const double* src, double multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = src[i] * multiplier;
    }

    template <typename Size>
    static void copyWithMultiply (float* dest, const float* src1, const float* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = src1[i] * src2[i];
    }

    template <typename Size>
    static void copyWithMultiply (double* dest, const double* src1, const double* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = src1[i] * src2[i];
    }

    //==========================================================================
    template <typename Size>
    static void add (float* dest, float amount, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += amount;
    }

    template <typename Size>
    static void add (double* dest, double amount, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += amount;
    }

    template <typename Size>
    static void add (float* dest, const float* src, float amount, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += amount * src[i];
    }

    template <typename Size>
    static void add (double* dest, const double* src, double amount, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += amount * src[i];
    }

    template <typename Size>
    static void add (float* dest, const float* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += src[i];
    }

    template <typename Size>
    static void add (double* dest, const double* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += src[i];
    }

    //==========================================================================
    template <typename Size>
    static void subtract (float* dest, const float* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] -= src[i];
    }

    template <typename Size>
    static void subtract (double* dest, const double* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] -= src[i];
    }

    //==========================================================================
    template <typename Size>
    static void addWithMultiply (float* dest, const float* src, float multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += src[i] * multiplier;
    }

    template <typename Size>
    static void addWithMultiply (double* dest, const double* src, double multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += src[i] * multiplier;
    }

    template <typename Size>
    static void addWithMultiply (float* dest, const float* src1, const float* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += src1[i] * src2[i];
    }

    template <typename Size>
    static void addWithMultiply (double* dest, const double* src1, const double* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] += src1[i] * src2[i];
    }

    //==========================================================================
    template <typename Size>
    static void multiply (float* dest, float multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] *= multiplier;
    }

    template <typename Size>
    static void multiply (double* dest, double multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] *= multiplier;
    }

    template <typename Size>
    static void multiply (float* dest, const float* src, float multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] *= src[i] * multiplier;
    }

    template <typename Size>
    static void multiply (double* dest, const double* src, double multiplier, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] *= src[i] * multiplier;
    }

    template <typename Size>
    static void multiply (float* dest, const float* src1, const float* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] *= src1[i] * src2[i];
    }

    template <typename Size>
    static void multiply (double* dest, const double* src1, const double* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] *= src1[i] * src2[i];
    }

    //==========================================================================
    template <typename Size>
    static void negate (float* dest, const float* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = -src[i];
    }

    template <typename Size>
    static void negate (double* dest, const double* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = -src[i];
    }

    //==========================================================================
    template <typename Size>
    static void abs (float* dest, const float* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = std::abs (src[i]);
    }

    template <typename Size>
    static void abs (double* dest, const double* src, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = std::abs (src[i]);
    }

    //==========================================================================
    template <typename Size>
    static void min (float* dest, const float* src, float comp, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmin (src[i], comp);
    }

    template <typename Size>
    static void min (double* dest, const double* src, double comp, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmin (src[i], comp);
    }

    template <typename Size>
    static void min (float* dest, const float* src1, const float* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmin (src1[i], src2[i]);
    }

    template <typename Size>
    static void min (double* dest, const double* src1, const double* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmin (src1[i], src2[i]);
    }

    //==========================================================================
    template <typename Size>
    static void max (float* dest, const float* src, float comp, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmax (src[i], comp);
    }

    template <typename Size>
    static void max (double* dest, const double* src, double comp, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmax (src[i], comp);
    }

    template <typename Size>
    static void max (float* dest, const float* src1, const float* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmax (src1[i], src2[i]);
    }

    template <typename Size>
    static void max (double* dest, const double* src1, const double* src2, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jmax (src1[i], src2[i]);
    }

    //==========================================================================
    template <typename Size>
    static void clip (float* dest, const float* src, float lowLimit, float highLimit, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jlimit (lowLimit, highLimit, src[i]);
    }

    template <typename Size>
    static void clip (double* dest, const double* src, double lowLimit, double highLimit, Size num) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            dest[i] = jlimit (lowLimit, highLimit, src[i]);
    }

    //==========================================================================
    template <typename Size>
    static bool areAlmostEqual (const float* a, const float* b, Size num, float tolerance) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            if (! approximatelyEqual (a[i], b[i]) && std::abs (a[i] - b[i]) > tolerance)
                return false;

        return true;
    }

    template <typename Size>
    static bool areAlmostEqual (const double* a, const double* b, Size num, double tolerance) noexcept
    {
        for (int i = 0; i < (int) num; ++i)
            if (! approximatelyEqual (a[i], b[i]) && std::abs (a[i] - b[i]) > tolerance)
                return false;

        return true;
    }

    //==========================================================================
    template <typename Size>
    static Range<float> findMinAndMax (const float* src, Size num) noexcept
    {
        if (num <= 0)
            return {};

        auto mn = src[0];
        auto mx = mn;

        for (int i = 1; i < (int) num; ++i)
        {
            const auto v = src[i];

            if (mx < v)  mx = v;
            if (v < mn)  mn = v;
        }

        return { mn, mx };
    }

    template <typename Size>
    static Range<double> findMinAndMax (const double* src, Size num) noexcept
    {
        if (num <= 0)
            return {};

        auto mn = src[0];
        auto mx = mn;

        for (int i = 1; i < (int) num; ++i)
        {
            const auto v = src[i];

            if (mx < v)  mx = v;
            if (v < mn)  mn = v;
        }

        return { mn, mx };
    }
};

//==============================================================================
// Declaracion adelantada: la sobrecarga applyGain(AudioBuffer&) de la base de
// smoothers se instancia solo en punto de uso, cuando AudioBuffer ya esta
// completa (se define mas abajo en este fichero).
template <typename Type> class AudioBuffer;

//==============================================================================
/**
    A base class for the smoothed value classes.

    Port literal de juce::SmoothedValueBase (juce_audio_basics/utilities/
    juce_SmoothedValue.h), incluidas las tres sobrecargas de applyGain
    (cuerpos literales; dependen de dsp::FloatVectorOperations y
    dsp::AudioBuffer, ambos porteados en este fichero).
*/
template <typename SmoothedValueType>
class SmoothedValueBase
{
private:
    //==============================================================================
    template <typename T> struct FloatTypeHelper;

    template <template <typename> class SmoothedValueClass, typename FloatType>
    struct FloatTypeHelper <SmoothedValueClass <FloatType>>
    {
        using Type = FloatType;
    };

    template <template <typename, typename> class SmoothedValueClass, typename FloatType, typename SmoothingType>
    struct FloatTypeHelper <SmoothedValueClass <FloatType, SmoothingType>>
    {
        using Type = FloatType;
    };

public:
    using FloatType = typename FloatTypeHelper<SmoothedValueType>::Type;

    //==============================================================================
    /** Constructor. */
    SmoothedValueBase() = default;

    //==============================================================================
    /** Returns true if the current value is currently being interpolated. */
    bool isSmoothing() const noexcept                    { return countdown > 0; }

    /** Returns the current value of the ramp. */
    FloatType getCurrentValue() const noexcept           { return currentValue; }

    //==============================================================================
    /** Returns the target value towards which the smoothed value is currently moving. */
    FloatType getTargetValue() const noexcept            { return target; }

    /** Sets the current value and the target value.
        @param newValue    the new value to take
    */
    void setCurrentAndTargetValue (FloatType newValue)
    {
        target = currentValue = newValue;
        countdown = 0;
    }

    //==============================================================================
    /** Applies a smoothed gain to a stream of samples
        S[i] *= gain
        @param samples Pointer to a raw array of samples
        @param numSamples Length of array of samples
        Port literal de SmoothedValueBase::applyGain.
    */
    void applyGain (FloatType* samples, int numSamples) noexcept
    {
        dspAssert (numSamples >= 0);

        if (isSmoothing())
        {
            for (int i = 0; i < numSamples; ++i)
                samples[i] *= getNextSmoothedValue();
        }
        else
        {
            FloatVectorOperations::multiply (samples, target, numSamples);
        }
    }

    //==============================================================================
    /** Computes output as a smoothed gain applied to a stream of samples.
        Sout[i] = Sin[i] * gain
        @param samplesOut A pointer to a raw array of output samples
        @param samplesIn  A pointer to a raw array of input samples
        @param numSamples The length of the array of samples
        Port literal de SmoothedValueBase::applyGain.
    */
    void applyGain (FloatType* samplesOut, const FloatType* samplesIn, int numSamples) noexcept
    {
        dspAssert (numSamples >= 0);

        if (isSmoothing())
        {
            for (int i = 0; i < numSamples; ++i)
                samplesOut[i] = samplesIn[i] * getNextSmoothedValue();
        }
        else
        {
            FloatVectorOperations::multiply (samplesOut, samplesIn, target, numSamples);
        }
    }

    //==============================================================================
    /** Applies a smoothed gain to a buffer.
        Port literal de SmoothedValueBase::applyGain.
    */
    void applyGain (AudioBuffer<FloatType>& buffer, int numSamples) noexcept
    {
        dspAssert (numSamples >= 0);

        if (isSmoothing())
        {
            if (buffer.getNumChannels() == 1)
            {
                auto* samples = buffer.getWritePointer (0);

                for (int i = 0; i < numSamples; ++i)
                    samples[i] *= getNextSmoothedValue();
            }
            else
            {
                for (auto i = 0; i < numSamples; ++i)
                {
                    auto gain = getNextSmoothedValue();

                    for (int channel = 0; channel < buffer.getNumChannels(); channel++)
                        buffer.setSample (channel, i, buffer.getSample (channel, i) * gain);
                }
            }
        }
        else
        {
            buffer.applyGain (0, numSamples, target);
        }
    }

private:
    //==============================================================================
    FloatType getNextSmoothedValue() noexcept
    {
        return static_cast <SmoothedValueType*> (this)->getNextValue();
    }

protected:
    //==============================================================================
    FloatType currentValue = 0;
    FloatType target = currentValue;
    int countdown = 0;
};

//==============================================================================
/**
    A namespace containing a set of types used for specifying the smoothing
    behaviour of the SmoothedValue class.

    Port literal de juce::ValueSmoothingTypes.
*/
namespace ValueSmoothingTypes
{
    /**
        Used to indicate a linear smoothing between values.
    */
    struct Linear {};

    /**
        Used to indicate a smoothing between multiplicative values.
    */
    struct Multiplicative {};
}

//==============================================================================
/**
    A utility class for values that need smoothing to avoid audio glitches.

    Port literal de juce::SmoothedValue (juce_audio_basics/utilities/
    juce_SmoothedValue.h, JUCE 8.0.12): mismas ramas, mismo orden de
    operaciones y misma division; el audio no puede cambiar.

    Nota: la maquinaria de aserciones de Multiplicative (el objetivo nunca
    puede ser 0) usa dspAssert en lugar de jassert.
*/
template <typename FloatType, typename SmoothingType = ValueSmoothingTypes::Linear>
class SmoothedValue   : public SmoothedValueBase <SmoothedValue <FloatType, SmoothingType>>
{
public:
    //==============================================================================
    /** Constructor. */
    SmoothedValue() noexcept
        : SmoothedValue ((FloatType) (std::is_same_v<SmoothingType, ValueSmoothingTypes::Linear> ? 0 : 1))
    {
    }

    /** Constructor. */
    SmoothedValue (FloatType initialValue) noexcept
    {
        // Multiplicative smoothed values cannot ever reach 0!
        dspAssert (! (std::is_same_v<SmoothingType, ValueSmoothingTypes::Multiplicative>
                    && approximatelyEqual (initialValue, (FloatType) 0)));

        // Visual Studio can't handle base class initialisation with CRTP
        this->currentValue = initialValue;
        this->target = this->currentValue;
    }

    //==============================================================================
    /** Reset to a new sample rate and ramp length.
        @param sampleRate           The sample rate
        @param rampLengthInSeconds  The duration of the ramp in seconds
    */
    void reset (double sampleRate, double rampLengthInSeconds) noexcept
    {
        dspAssert (sampleRate > 0 && rampLengthInSeconds >= 0);
        reset ((int) std::floor (rampLengthInSeconds * sampleRate));
    }

    /** Set a new ramp length directly in samples.
        @param numSteps     The number of samples over which the ramp should be active
    */
    void reset (int numSteps) noexcept
    {
        stepsToTarget = numSteps;
        this->setCurrentAndTargetValue (this->target);
    }

    //==============================================================================
    /** Set the next value to ramp towards.
        @param newValue     The new target value
    */
    void setTargetValue (FloatType newValue) noexcept
    {
        if (approximatelyEqual (newValue, this->target))
            return;

        if (stepsToTarget <= 0)
        {
            this->setCurrentAndTargetValue (newValue);
            return;
        }

        // Multiplicative smoothed values cannot ever reach 0!
        dspAssert (! (std::is_same_v<SmoothingType, ValueSmoothingTypes::Multiplicative>
                    && approximatelyEqual (newValue, (FloatType) 0)));

        this->target = newValue;
        this->countdown = stepsToTarget;

        setStepSize();
    }

    //==============================================================================
    /** Compute the next value.
        @returns Smoothed value
    */
    FloatType getNextValue() noexcept
    {
        if (! this->isSmoothing())
            return this->target;

        --(this->countdown);

        if (this->isSmoothing())
            setNextValue();
        else
            this->currentValue = this->target;

        return this->currentValue;
    }

    //==============================================================================
    /** Skip the next numSamples samples.
        This is identical to calling getNextValue numSamples times. It returns
        the new current value.
        @see getNextValue
    */
    FloatType skip (int numSamples) noexcept
    {
        if (numSamples >= this->countdown)
        {
            this->setCurrentAndTargetValue (this->target);
            return this->target;
        }

        skipCurrentValue (numSamples);

        this->countdown -= numSamples;
        return this->currentValue;
    }

private:
    //==============================================================================
    template <typename T = SmoothingType>
    void setStepSize() noexcept
    {
        if constexpr (std::is_same_v<T, ValueSmoothingTypes::Linear>)
        {
            step = (this->target - this->currentValue) / (FloatType) this->countdown;
        }
        else if constexpr (std::is_same_v<T, ValueSmoothingTypes::Multiplicative>)
        {
            step = std::exp ((std::log (std::abs (this->target)) - std::log (std::abs (this->currentValue))) / (FloatType) this->countdown);
        }
    }

    //==============================================================================
    template <typename T = SmoothingType>
    void setNextValue() noexcept
    {
        if constexpr (std::is_same_v<T, ValueSmoothingTypes::Linear>)
        {
            this->currentValue += step;
        }
        else if constexpr (std::is_same_v<T, ValueSmoothingTypes::Multiplicative>)
        {
            this->currentValue *= step;
        }
    }

    //==============================================================================
    template <typename T = SmoothingType>
    void skipCurrentValue (int numSamples) noexcept
    {
        if constexpr (std::is_same_v<T, ValueSmoothingTypes::Linear>)
        {
            this->currentValue += step * (FloatType) numSamples;
        }
        else if constexpr (std::is_same_v<T, ValueSmoothingTypes::Multiplicative>)
        {
            this->currentValue *= (FloatType) std::pow (step, numSamples);
        }
    }

    //==============================================================================
    FloatType step = FloatType();
    int stepsToTarget = 0;
};

template <typename FloatType>
using LinearSmoothedValue = SmoothedValue <FloatType, ValueSmoothingTypes::Linear>;

//==============================================================================
// Helpers de JUCE usados por AudioBuffer. Fuente de cada port:
//   numElementsInArray   -> juce_core/maths/juce_MathsFunctions.h
//   unalignedPointerCast -> juce_core/memory/juce_Memory.h
// (la nota anterior apuntaba a juce_core/system/juce_StandardHeader.h, que solo
//  declara la version, no ninguno de los dos helpers)

/** Handy function for getting the number of elements in a const array.
    Port de juce::numElementsInArray.

    CORREGIDO (2026-09-19, hallado al escribir DspEffectsParityTest): el array
    entra POR REFERENCIA (`Type (&)[N]`, como en JUCE), no por valor. Con el
    parametro por valor el array decae a puntero en la llamada, asi que el
    `sizeof (array) / sizeof (array[0])` del cuerpo medía el PUNTERO: devolvia
    `sizeof(Type*) / sizeof(Type)`, que en los tres consumidores (que pasan el
    array `Type* [32]` llamado `preallocatedChannelSpace`) era 1.

    Los tres consumidores son internos de AudioBuffer, de modo que la
    comprobacion `numChannels < 32` se evaluaba como `numChannels < 1` y la rama
    de la memoria PREASIGNADA quedaba muerta (inalcanzable):

      - `allocateChannels` (ctor de buffer sobre memoria externa) hacia un
        `allocatedData.malloc` por buffer en vez de usar `preallocatedChannelSpace`.
        Es exactamente el malloc que JUCE evita a proposito en esa rama ("blow up
        things like Pro-Tools"), y le tocaba a cada AudioBuffer que referencia
        canales ya reservados — no a un caso raro.
      - El constructor y la asignacion de movimiento iban siempre por la rama de
        aliasar `other.channels` en vez de copiar los punteros al hueco propio
        (esa rama era, por tanto, la unica viva).

    El sintoma que lo delato fue el aviso de GCC `-Wsizeof-pointer-div`, que
    aparecia en cuanto se instanciaba el ctor de memoria externa. Con la forma de
    JUCE el array no decae, se devuelve N, y llamar con un puntero ya no compila
    (antes compilaba y devolvia basura: la peor combinacion posible). El
    static_assert de DspCoreTests lo fija en tiempo de compilacion.

    NOTA (unica diferencia textual con JUCE): el `static_cast<int>` explicito. La
    forma de JUCE devuelve `N` tal cual, con N de tipo `size_t`; el cast deja la
    conversion size_t -> int a la vista sin depender de que compilador y que
    avisos haya activado cada build. */
template <typename Type, size_t N>
constexpr int numElementsInArray (Type (&)[N]) noexcept     { return static_cast<int> (N); }

/** Converts a pointer to a pointer of a different type without caring about
    alignment. Port literal de juce::unalignedPointerCast. */
template <typename Type>
Type unalignedPointerCast (void* data) noexcept                { return reinterpret_cast<Type> (data); }

template <typename Type>
Type unalignedPointerCast (const void* data) noexcept          { return reinterpret_cast<Type> (data); }

//==============================================================================
//
// Allocador unificado del AudioBuffer (sustituto minimo de HeapBlock y del
// std::unique_ptr<Type[]> de la mitad Basename del fuente JUCE).
//
//==============================================================================
/**
    Sustituto minimo de juce::HeapBlock (malloc/calloc/allocate con liberacion
    automatica, clear y swap). Excepcion documentada del port literal (nota
    anti-drift del equipo).

    CORREGIDO (Fase 1 [5/6], hallado al portar juce::Reverb): `clear` y
    `allocate` tomaban BYTES y JUCE toma ELEMENTOS (multiplica por
    sizeof(ElementType)). La discrepancia era latente porque hasta ahora el unico
    consumidor era AudioBuffer (que no usa HeapBlock) y DspMidiBuffer, cuyo
    bloque es uint8_t (elementos == bytes). juce::Reverb si la expone:
    `buffer.clear((size_t) bufferSize)` sobre un HeapBlock<float> limpia 4x
    memoria con la semantica de JUCE, y con la de bytes dejaba 3/4 del buffer de
    cada comb filter sin inicializar y realimentaba basura (la reverb divergia a
    ~1e35 en pocos bloques). Ahora las dos coinciden con JUCE.
*/
template <typename ElementType, bool throwOnFailure = false>
class HeapBlock
{
public:
    HeapBlock() = default;

    ~HeapBlock()
    {
        release();
    }

    HeapBlock (const HeapBlock&) = delete;
    HeapBlock& operator= (const HeapBlock&) = delete;

    HeapBlock (HeapBlock&& other) noexcept
        : data (other.data)
    {
        other.data = nullptr;
    }

    HeapBlock& operator= (HeapBlock&& other) noexcept
    {
        std::swap (data, other.data);
        return *this;
    }

    void malloc (size_t newNumElements, size_t elementSize = sizeof (ElementType))
    {
        release();
        data = static_cast<ElementType*> (::malloc (newNumElements * elementSize));
    }

    void calloc (size_t newNumElements, size_t elementSize = sizeof (ElementType))
    {
        release();
        data = static_cast<ElementType*> (::calloc (newNumElements, elementSize));
    }

    /** Asigna y, opcionalmente, limpia. Numero de ELEMENTOS, como JUCE. */
    void allocate (size_t newNumElements, bool zeroFill)
    {
        release();
        data = static_cast<ElementType*> (zeroFill ? ::calloc (newNumElements, sizeof (ElementType))
                                                   : ::malloc (newNumElements * sizeof (ElementType)));
    }

    void free()
    {
        release();
    }

    /** Rellena de ceros hasta el numero de ELEMENTOS indicado, como JUCE
        (zeromem (data, sizeof (ElementType) * numElements)). */
    void clear (size_t numElements) noexcept
    {
        if (data != nullptr)
            std::memset (data, 0, sizeof (ElementType) * numElements);
    }

    template <typename OtherBlock>
    void swapWith (OtherBlock& other) noexcept
    {
        std::swap (data, other.data);
    }

    ElementType* get() const noexcept                       { return data; }
    operator ElementType*() const noexcept                  { return data; }
    ElementType* operator->() const noexcept                { return data; }

private:
    void release()
    {
        ::free (data);
        data = nullptr;
    }

    ElementType* data = nullptr;
};

//==============================================================================
//
// Multi-channel buffer containing floating point audio samples.
// Port literal de juce::AudioBuffer (juce_audio_basics/buffers/
// juce_AudioSampleBuffer.h, JUCE 8.0.12): misma distribucion de memoria
// (punteros de canal intercalados con el audio, flag isClear), mismos
// cuerpos, mismas ramas.
//
// Diferencias documentadas (todas inocuas para el audio):
//  - JUCE_LEAK_DETECTOR omitido.
//  - jassert -> dspAssert.
//  - literales 0 de JUCE en getRMSLevel/reverse escritos 0.0/0.0f
//    (valor identico).
//  - la mitad Basename y la mitad AudioBuffer del fuente JUCE (dos
//    definiciones equivalentes segun macro) se unifican: siempre la mitad
//    AudioBuffer, con std::unique_ptr<Type[]> como HeapBlock.
//  - jmap(0, i, numSamples - 1) del bucle multicanal de
//    SmoothedValueBase::applyGain (analisis 2026-09-18): jmap devuelve 0
//    exacto en su primer punto de apoyo (targetRangeMin), por lo que JUCE
//    multiplica el resto de canales por 0.0f muestra a muestra. El port
//    escribe 0.0f directamente; en el AudioBuffer esta etiqueta NO es
//    observable (getSample tras applyGain devuelve 0 exacto en ambos).
//
//==============================================================================
template <typename Type>
class AudioBuffer
{
public:
    //==============================================================================
    /** Creates an empty buffer with 0 channels and 0 length. */
    AudioBuffer() noexcept
       : channels (static_cast<Type**> (preallocatedChannelSpace))
    {
    }

    //==============================================================================
    /** Creates a buffer with a specified number of channels and samples.

        The contents of the buffer will initially be undefined, so use clear() to
        set all the samples to zero.

        The buffer will allocate its memory internally, and this will be released
        when the buffer is deleted. If the memory can't be allocated, this will
        throw a std::bad_alloc exception.
    */
    AudioBuffer (int numChannelsToAllocate,
                 int numSamplesToAllocate)
       : numChannels (numChannelsToAllocate),
         size (numSamplesToAllocate)
    {
        dspAssert (size >= 0 && numChannels >= 0);
        allocateData();
    }

    /** Creates a buffer using a pre-allocated block of memory.

        Note that if the buffer is resized or its number of channels is changed, it
        will re-allocate memory internally and copy the existing data to this new area,
        so it will then stop directly addressing this memory.

        @param dataToReferTo    a pre-allocated array containing pointers to the data
                                for each channel that should be used by this buffer. The
                                buffer will only refer to this memory, it won't try to delete
                                it when the buffer is deleted or resized.
        @param numChannelsToUse the number of channels to use - this must correspond to the
                                number of elements in the array passed in
        @param numSamples       the number of samples to use - this must correspond to the
                                size of the arrays passed in
    */
    AudioBuffer (Type* const* dataToReferTo,
                 int numChannelsToUse,
                 int numSamples)
        : numChannels (numChannelsToUse),
          size (numSamples)
    {
        dspAssert (dataToReferTo != nullptr);
        dspAssert (numChannelsToUse >= 0 && numSamples >= 0);
        allocateChannels (dataToReferTo, 0);
    }

    /** Creates a buffer using a pre-allocated block of memory.

        Note that if the buffer is resized or its number of channels is changed, it
        will re-allocate memory internally and copy the existing data to this new area,
        so it will then stop directly addressing this memory.

        @param dataToReferTo    a pre-allocated array containing pointers to the data
                                for each channel that should be used by this buffer. The
                                buffer will only refer to this memory, it won't try to delete
                                it when the buffer is deleted or resized.
        @param numChannelsToUse the number of channels to use - this must correspond to the
                                number of elements in the array passed in
        @param startSample      the offset within the arrays at which the data begins
        @param numSamples       the number of samples to use - this must correspond to the
                                size of the arrays passed in
    */
    AudioBuffer (Type* const* dataToReferTo,
                 int numChannelsToUse,
                 int startSample,
                 int numSamples)
        : numChannels (numChannelsToUse),
          size (numSamples)
    {
        dspAssert (dataToReferTo != nullptr);
        dspAssert (numChannelsToUse >= 0 && startSample >= 0 && numSamples >= 0);
        allocateChannels (dataToReferTo, startSample);
    }

    /** Copies another buffer.

        This buffer will make its own copy of the other's data, unless the buffer was created
        using an external data buffer, in which case both buffers will just point to the same
        shared block of data.
    */
    AudioBuffer (const AudioBuffer& other)
       : numChannels (other.numChannels),
         size (other.size),
         allocatedBytes (other.allocatedBytes)
    {
        if (allocatedBytes == 0)
        {
            allocateChannels (other.channels, 0);
        }
        else
        {
            allocateData();

            if (other.isClear)
            {
                clear();
            }
            else
            {
                for (int i = 0; i < numChannels; ++i)
                    FloatVectorOperations::copy (channels[i], other.channels[i], size);
            }
        }
    }

    /** Copies another buffer onto this one.

        This buffer's size will be changed to that of the other buffer.
    */
    AudioBuffer& operator= (const AudioBuffer& other)
    {
        if (this != &other)
        {
            setSize (other.getNumChannels(), other.getNumSamples(), false, false, false);

            if (other.isClear)
            {
                clear();
            }
            else
            {
                isClear = false;

                for (int i = 0; i < numChannels; ++i)
                    FloatVectorOperations::copy (channels[i], other.channels[i], size);
            }
        }

        return *this;
    }

    /** Destructor.

        This will free any memory allocated by the buffer.
    */
    ~AudioBuffer() = default;

    /** Move constructor. */
    AudioBuffer (AudioBuffer&& other) noexcept
        : numChannels (other.numChannels),
          size (other.size),
          allocatedBytes (other.allocatedBytes),
          allocatedData (std::move (other.allocatedData)),
          isClear (other.isClear)
    {
        if (numChannels < (int) numElementsInArray (preallocatedChannelSpace))
        {
            channels = preallocatedChannelSpace;

            for (int i = 0; i < numChannels; ++i)
                preallocatedChannelSpace[i] = other.channels[i];
        }
        else
        {
            channels = other.channels;
        }

        other.numChannels = 0;
        other.size = 0;
        other.allocatedBytes = 0;
    }

    /** Move assignment. */
    AudioBuffer& operator= (AudioBuffer&& other) noexcept
    {
        numChannels = other.numChannels;
        size = other.size;
        allocatedBytes = other.allocatedBytes;
        allocatedData = std::move (other.allocatedData);
        isClear = other.isClear;

        if (numChannels < (int) numElementsInArray (preallocatedChannelSpace))
        {
            channels = preallocatedChannelSpace;

            for (int i = 0; i < numChannels; ++i)
                preallocatedChannelSpace[i] = other.channels[i];
        }
        else
        {
            channels = other.channels;
        }

        other.numChannels = 0;
        other.size = 0;
        other.allocatedBytes = 0;
        return *this;
    }

    //==============================================================================
    /** Returns the number of channels of audio data that this buffer contains.

        @see getNumSamples, getReadPointer, getWritePointer
    */
    int getNumChannels() const noexcept                             { return numChannels; }

    /** Returns the number of samples allocated in each of the buffer's channels.

        @see getNumChannels, getReadPointer, getWritePointer
    */
    int getNumSamples() const noexcept                              { return size; }

    /** Returns a pointer to an array of read-only samples in one of the buffer's channels.

        For speed, this doesn't check whether the channel number is out of range,
        so be careful when using it!

        If you need to write to the data, do NOT call this method and const_cast the
        result! Instead, you must call getWritePointer so that the buffer knows you're
        planning on modifying the data.
    */
    const Type* getReadPointer (int channelNumber) const noexcept
    {
        dspAssert (isPositiveAndBelow (channelNumber, numChannels));
        return channels[channelNumber];
    }

    /** Returns a pointer to an array of read-only samples in one of the buffer's channels.

        For speed, this doesn't check whether the channel number or index are out of range,
        so be careful when using it!

        If you need to write to the data, do NOT call this method and const_cast the
        result! Instead, you must call getWritePointer so that the buffer knows you're
        planning on modifying the data.
    */
    const Type* getReadPointer (int channelNumber, int sampleIndex) const noexcept
    {
        dspAssert (isPositiveAndBelow (channelNumber, numChannels));
        dspAssert (isPositiveAndBelow (sampleIndex, size));
        return channels[channelNumber] + sampleIndex;
    }

    /** Returns a writeable pointer to one of the buffer's channels.

        For speed, this doesn't check whether the channel number is out of range,
        so be careful when using it!

        Note that if you're not planning on writing to the data, you should always
        use getReadPointer instead.

        This will mark the buffer as not cleared and the hasBeenCleared method will return
        false after this call. If you retain this write pointer and write some data to
        the buffer after calling its clear method, subsequent clear calls will do nothing.
        To avoid this either call this method each time you need to write data, or use the
        setNotClear method to force the internal cleared flag to false.

        @see setNotClear
    */
    Type* getWritePointer (int channelNumber) noexcept
    {
        dspAssert (isPositiveAndBelow (channelNumber, numChannels));
        isClear = false;
        return channels[channelNumber];
    }

    /** Returns a writeable pointer to one of the buffer's channels.

        For speed, this doesn't check whether the channel number or index are out of range,
        so be careful when using it!

        Note that if you're not planning on writing to the data, you should
        use getReadPointer instead.

        This will mark the buffer as not cleared and the hasBeenCleared method will return
        false after this call. If you retain this write pointer and write some data to
        the buffer after calling its clear method, subsequent clear calls will do nothing.
        To avoid this either call this method each time you need to write data, or use the
        setNotClear method to force the internal cleared flag to false.

        @see setNotClear
    */
    Type* getWritePointer (int channelNumber, int sampleIndex) noexcept
    {
        dspAssert (isPositiveAndBelow (channelNumber, numChannels));
        dspAssert (isPositiveAndBelow (sampleIndex, size));
        isClear = false;
        return channels[channelNumber] + sampleIndex;
    }

    /** Returns an array of pointers to the channels in the buffer.

        Don't modify any of the pointers that are returned, and bear in mind that
        these will become invalid if the buffer is resized.
    */
    const Type* const* getArrayOfReadPointers() const noexcept            { return channels; }

    /** Returns an array of pointers to the channels in the buffer.

        Don't modify any of the pointers that are returned, and bear in mind that
        these will become invalid if the buffer is resized.

        This will mark the buffer as not cleared and the hasBeenCleared method will return
        false after this call. If you retain this write pointer and write some data to
        the buffer after calling its clear method, subsequent clear calls will do nothing.
        To avoid this either call this method each time you need to write data, or use the
        setNotClear method to force the internal cleared flag to false.

        @see setNotClear
    */
    Type* const* getArrayOfWritePointers() noexcept                       { isClear = false; return channels; }

    //==============================================================================
    /** Changes the buffer's size or number of channels.

        This can expand or contract the buffer's length, and add or remove channels.

        Note that if keepExistingContent and avoidReallocating are both true, then it will
        only avoid reallocating if neither the channel count or length in samples increase.

        If the required memory can't be allocated, this will throw a std::bad_alloc exception.

        @param newNumChannels       the new number of channels.
        @param newNumSamples        the new number of samples.
        @param keepExistingContent  if this is true, it will try to preserve as much of the
                                    old data as it can in the new buffer.
        @param clearExtraSpace      if this is true, then any extra channels or space that is
                                    allocated will also be cleared. If false, then this space is left
                                    uninitialised.
        @param avoidReallocating    if this is true, then changing the buffer's size won't reduce the
                                    amount of memory that is currently allocated (but it will still
                                    increase it if the new size is bigger than the amount it currently has).
                                    If this is false, then a new allocation will be done so that the buffer
                                    uses the minimum amount of memory that it needs.
    */
    void setSize (int newNumChannels,
                  int newNumSamples,
                  bool keepExistingContent = false,
                  bool clearExtraSpace = false,
                  bool avoidReallocating = false)
    {
        dspAssert (newNumChannels >= 0);
        dspAssert (newNumSamples >= 0);

        if (newNumSamples != size || newNumChannels != numChannels)
        {
            auto allocatedSamplesPerChannel = ((size_t) newNumSamples + 3) & ~3u;
            auto channelListSize = ((static_cast<size_t> (1 + newNumChannels) * sizeof (Type*)) + 15) & ~15u;
            auto newTotalBytes = ((size_t) newNumChannels * (size_t) allocatedSamplesPerChannel * sizeof (Type))
                                    + channelListSize + 32;

            if (keepExistingContent)
            {
                if (avoidReallocating && newNumChannels <= numChannels && newNumSamples <= size)
                {
                    // no need to do any remapping in this case, as the channel pointers will remain correct!
                }
                else
                {
                    HeapBlock<char, true> newData;
                    newData.allocate (newTotalBytes, clearExtraSpace || isClear);

                    auto numSamplesToCopy = (size_t) jmin (newNumSamples, size);

                    auto newChannels = unalignedPointerCast<Type**> (newData.get());
                    auto newChan     = unalignedPointerCast<Type*> (newData + channelListSize);

                    for (int j = 0; j < newNumChannels; ++j)
                    {
                        newChannels[j] = newChan;
                        newChan += allocatedSamplesPerChannel;
                    }

                    if (! isClear)
                    {
                        auto numChansToCopy = jmin (numChannels, newNumChannels);

                        for (int i = 0; i < numChansToCopy; ++i)
                            FloatVectorOperations::copy (newChannels[i], channels[i], (int) numSamplesToCopy);
                    }

                    allocatedData.swapWith (newData);
                    allocatedBytes = newTotalBytes;
                    channels = newChannels;
                }
            }
            else
            {
                if (avoidReallocating && allocatedBytes >= newTotalBytes)
                {
                    if (clearExtraSpace || isClear)
                        allocatedData.clear (newTotalBytes);
                }
                else
                {
                    allocatedBytes = newTotalBytes;
                    allocatedData.allocate (newTotalBytes, clearExtraSpace || isClear);
                    channels = unalignedPointerCast<Type**> (allocatedData.get());
                }

                auto* chan = unalignedPointerCast<Type*> (allocatedData + channelListSize);

                for (int i = 0; i < newNumChannels; ++i)
                {
                    channels[i] = chan;
                    chan += allocatedSamplesPerChannel;
                }
            }

            channels[newNumChannels] = nullptr;
            size = newNumSamples;
            numChannels = newNumChannels;
        }
    }

    /** Makes this buffer point to a pre-allocated set of channel data arrays.

        There's also a constructor that lets you specify arrays like this, but this
        lets you change the channels dynamically.

        Note that if the buffer is resized or its number of channels is changed, it
        will re-allocate memory internally and copy the existing data to this new area,
        so it will then stop directly addressing this memory.

        The hasBeenCleared method will return false after this call.

        @param dataToReferTo    a pre-allocated array containing pointers to the data
                                for each channel that should be used by this buffer. The
                                buffer will only refer to this memory, it won't try to delete
                                it when the buffer is deleted or resized.
        @param newNumChannels   the number of channels to use - this must correspond to the
                                number of elements in the array passed in
        @param newStartSample   the offset within the arrays at which the data begins
        @param newNumSamples    the number of samples to use - this must correspond to the
                                size of the arrays passed in
    */
    void setDataToReferTo (Type* const* dataToReferTo,
                           int newNumChannels,
                           int newStartSample,
                           int newNumSamples)
    {
        dspAssert (dataToReferTo != nullptr);
        dspAssert (newNumChannels >= 0 && newNumSamples >= 0);

        size = newNumSamples;

        if (newNumChannels <= numChannels)
        {
            numChannels = newNumChannels;

            std::transform (dataToReferTo, dataToReferTo + numChannels, channels, [&] (auto* src)
            {
                dspAssert (src != nullptr);
                return src + newStartSample;
            });

            channels[numChannels] = nullptr;
            isClear = false;
        }
        else
        {
            if (allocatedBytes != 0)
            {
                allocatedBytes = 0;
                allocatedData.free();
            }

            numChannels = newNumChannels;
            allocateChannels (dataToReferTo, newStartSample);
        }

        dspAssert (! isClear);
    }

    /** Makes this buffer point to a pre-allocated set of channel data arrays.

        There's also a constructor that lets you specify arrays like this, but this
        lets you change the channels dynamically.

        Note that if the buffer is resized or its number of channels is changed, it
        will re-allocate memory internally and copy the existing data to this new area,
        so it will then stop directly addressing this memory.

        The hasBeenCleared method will return false after this call.

        @param dataToReferTo    a pre-allocated array containing pointers to the data
                                for each channel that should be used by this buffer. The
                                buffer will only refer to this memory, it won't try to delete
                                it when the buffer is deleted or resized.
        @param newNumChannels   the number of channels to use - this must correspond to the
                                number of elements in the array passed in
        @param newNumSamples    the number of samples to use - this must correspond to the
                                size of the arrays passed in
    */
    void setDataToReferTo (Type* const* dataToReferTo,
                           int newNumChannels,
                           int newNumSamples)
    {
        setDataToReferTo (dataToReferTo, newNumChannels, 0, newNumSamples);
    }

    /** Resizes this buffer to match the given one, and copies all of its content across.

        The source buffer can contain a different floating point type, so this can be used to
        convert between 32 and 64 bit float buffer types.

        The hasBeenCleared method will return false after this call if the other buffer
        contains data.
    */
    template <typename OtherType>
    void makeCopyOf (const AudioBuffer<OtherType>& other, bool avoidReallocating = false)
    {
        setSize (other.getNumChannels(), other.getNumSamples(), false, false, avoidReallocating);

        if (other.hasBeenCleared())
        {
            clear();
        }
        else
        {
            isClear = false;

            for (int chan = 0; chan < numChannels; ++chan)
            {
                auto* dest = channels[chan];
                auto* src = other.getReadPointer (chan);

                for (int i = 0; i < size; ++i)
                    dest[i] = static_cast<Type> (src[i]);
            }
        }
    }

    //==============================================================================
    /** Clears all the samples in all channels and marks the buffer as cleared.

        This method will do nothing if the buffer has been marked as cleared (i.e. the
        hasBeenCleared method returns true.)

        @see hasBeenCleared, setNotClear
    */
    void clear() noexcept
    {
        if (isClear)
            return;

        for (int i = 0; i < numChannels; ++i)
            FloatVectorOperations::clear (channels[i], size);

        isClear = true;
    }

    /** Clears a specified region of all the channels.

        This will mark the buffer as cleared if the entire buffer contents are cleared.

        For speed, this doesn't check whether the channel and sample number
        are in-range, so be careful!

        This method will do nothing if the buffer has been marked as cleared (i.e. the
        hasBeenCleared method returns true.)

        @see hasBeenCleared, setNotClear
    */
    void clear (int startSample, int numSamples) noexcept
    {
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (isClear)
            return;

        for (int i = 0; i < numChannels; ++i)
            FloatVectorOperations::clear (channels[i] + startSample, numSamples);

        isClear = (startSample == 0 && numSamples == size);
    }

    /** Clears a specified region of just one channel.

        For speed, this doesn't check whether the channel and sample number
        are in-range, so be careful!

        This method will do nothing if the buffer has been marked as cleared (i.e. the
        hasBeenCleared method returns true.)

        @see hasBeenCleared, setNotClear
    */
    void clear (int channel, int startSample, int numSamples) noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (! isClear)
            FloatVectorOperations::clear (channels[channel] + startSample, numSamples);
    }

    /** Returns true if the buffer has been entirely cleared.

        Note that this does not actually measure the contents of the buffer - it simply
        returns a flag that is set when the buffer is cleared, and which is reset whenever
        functions like getWritePointer are invoked. That means the method is quick, but it
        may return false negatives when in fact the buffer is still empty.
    */
    bool hasBeenCleared() const noexcept                            { return isClear; }

    /** Forces the internal cleared flag of the buffer to false.

        This may be useful in the case where you are holding on to a write pointer and call
        the clear method before writing some data. You can then use this method to mark the
        buffer as containing data so that subsequent clear calls will succeed. However a
        better solution is to call getWritePointer each time you need to write data.
    */
    void setNotClear() noexcept                                     { isClear = false; }

    //==============================================================================
    /** Returns a sample from the buffer.

        The channel and index are not checked - they are expected to be in-range. If not,
        an assertion will be thrown, but in a release build, you're into 'undefined behaviour'
        territory.
    */
    Type getSample (int channel, int sampleIndex) const noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (isPositiveAndBelow (sampleIndex, size));
        return *(channels[channel] + sampleIndex);
    }

    /** Sets a sample in the buffer.

        The channel and index are not checked - they are expected to be in-range. If not,
        an assertion will be thrown, but in a release build, you're into 'undefined behaviour'
        territory.

        The hasBeenCleared method will return false after this call.
    */
    void setSample (int destChannel, int destSample, Type newValue) noexcept
    {
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (isPositiveAndBelow (destSample, size));
        *(channels[destChannel] + destSample) = newValue;
        isClear = false;
    }

    /** Adds a value to a sample in the buffer.

        The channel and index are not checked - they are expected to be in-range. If not,
        an assertion will be thrown, but in a release build, you're into 'undefined behaviour'
        territory.

        The hasBeenCleared method will return false after this call.
    */
    void addSample (int destChannel, int destSample, Type valueToAdd) noexcept
    {
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (isPositiveAndBelow (destSample, size));
        *(channels[destChannel] + destSample) += valueToAdd;
        isClear = false;
    }

    /** Applies a gain multiple to a region of one channel.

        For speed, this doesn't check whether the channel and sample number
        are in-range, so be careful!
    */
    void applyGain (int channel, int startSample, int numSamples, Type gain) noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (isClear)
            return;

        auto* d = channels[channel] + startSample;
        FloatVectorOperations::multiply (d, gain, numSamples);
    }

    /** Applies a gain multiple to a region of all the channels.

        For speed, this doesn't check whether the sample numbers
        are in-range, so be careful!
    */
    void applyGain (int startSample, int numSamples, Type gain) noexcept
    {
        for (int i = 0; i < numChannels; ++i)
            applyGain (i, startSample, numSamples, gain);
    }

    /** Applies a gain multiple to all the audio data. */
    void applyGain (Type gain) noexcept
    {
        applyGain (0, size, gain);
    }

    /** Applies a range of gains to a region of a channel.

        The gain that is applied to each sample will vary from
        startGain on the first sample to endGain on the last Sample,
        so it can be used to do basic fades.

        For speed, this doesn't check whether the sample numbers
        are in-range, so be careful!
    */
    void applyGainRamp (int channel, int startSample, int numSamples,
                        Type startGain, Type endGain) noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (isClear)
            return;

        const auto increment = (endGain - startGain) / (float) numSamples;
        auto* d = channels[channel] + startSample;

        while (--numSamples >= 0)
        {
            *d++ *= startGain;
            startGain += increment;
        }
    }

    /** Applies a range of gains to a region of all channels.

        The gain that is applied to each sample will vary from
        startGain on the first sample to endGain on the last Sample,
        so it can be used to do basic fades.

        For speed, this doesn't check whether the sample numbers
        are in-range, so be careful!
    */
    void applyGainRamp (int startSample, int numSamples,
                        Type startGain, Type endGain) noexcept
    {
        for (int i = 0; i < numChannels; ++i)
            applyGainRamp (i, startSample, numSamples, startGain, endGain);
    }

    /** Adds samples from another buffer to this one.

        The hasBeenCleared method will return false after this call if samples have
        been added.

        @param destChannel          the channel within this buffer to add the samples to
        @param destStartSample      the start sample within this buffer's channel
        @param source               the source buffer to add from
        @param sourceChannel        the channel within the source buffer to read from
        @param sourceStartSample    the offset within the source buffer's channel to start reading samples from
        @param numSamples           the number of samples to process
        @param gainToApplyToSource  an optional gain to apply to the source samples before they are
                                    added to this buffer's samples

        @see copyFrom
    */
    void addFrom (int destChannel,
                  int destStartSample,
                  const AudioBuffer& source,
                  int sourceChannel,
                  int sourceStartSample,
                  int numSamples,
                  Type gainToApplyToSource = Type (1)) noexcept
    {
        dspAssert (&source != this
                   || sourceChannel != destChannel
                   || sourceStartSample + numSamples <= destStartSample
                   || destStartSample + numSamples <= sourceStartSample);
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (destStartSample >= 0 && numSamples >= 0 && destStartSample + numSamples <= size);
        dspAssert (isPositiveAndBelow (sourceChannel, source.numChannels));
        dspAssert (sourceStartSample >= 0 && sourceStartSample + numSamples <= source.size);

        if (numSamples <= 0 || source.isClear)
            return;

        auto* d = channels[destChannel] + destStartSample;
        auto* s = source.channels[sourceChannel] + sourceStartSample;

        if (isClear)
        {
            isClear = false;
            FloatVectorOperations::copyWithMultiply (d, s, gainToApplyToSource, numSamples);
        }
        else
        {
            FloatVectorOperations::addWithMultiply (d, s, gainToApplyToSource, numSamples);
        }
    }

    /** Adds samples from an array of floats to one of the channels.

        The hasBeenCleared method will return false after this call if samples have
        been added.

        @param destChannel          the channel within this buffer to add the samples to
        @param destStartSample      the start sample within this buffer's channel
        @param source               the source data to use
        @param numSamples           the number of samples to process
        @param gainToApplyToSource  an optional gain to apply to the source samples before they are
                                    added to this buffer's samples

        @see copyFrom
    */
    void addFrom (int destChannel,
                  int destStartSample,
                  const Type* source,
                  int numSamples,
                  Type gainToApplyToSource = Type (1)) noexcept
    {
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (destStartSample >= 0 && numSamples >= 0 && destStartSample + numSamples <= size);
        dspAssert (source != nullptr);

        if (numSamples <= 0)
            return;

        auto* d = channels[destChannel] + destStartSample;

        if (isClear)
        {
            isClear = false;
            FloatVectorOperations::copyWithMultiply (d, source, gainToApplyToSource, numSamples);
        }
        else
        {
            FloatVectorOperations::addWithMultiply (d, source, gainToApplyToSource, numSamples);
        }
    }

    /** Adds samples from an array of floats, applying a gain ramp to them.

        The hasBeenCleared method will return false after this call if samples have
        been added.

        @param destChannel          the channel within this buffer to add the samples to
        @param destStartSample      the start sample within this buffer's channel
        @param source               the source data to use
        @param numSamples           the number of samples to process
        @param startGain            the gain to apply to the first sample (this is multiplied with
                                    the source samples before they are added to this buffer)
        @param endGain            The gain that would apply to the sample after the final sample.
                                    The gain that applies to the final sample is
                                    (numSamples - 1) / numSamples * (endGain - startGain). This
                                    ensures a continuous ramp when supplying the same value in
                                    endGain and startGain in subsequent blocks. The gain is linearly
                                    interpolated between the first and last samples.
    */
    void addFromWithRamp (int destChannel,
                          int destStartSample,
                          const Type* source,
                          int numSamples,
                          Type startGain,
                          Type endGain) noexcept
    {
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (destStartSample >= 0 && numSamples >= 0 && destStartSample + numSamples <= size);
        dspAssert (source != nullptr);

        if (numSamples <= 0)
            return;

        isClear = false;
        const auto increment = (endGain - startGain) / (Type) numSamples;
        auto* d = channels[destChannel] + destStartSample;

        while (--numSamples >= 0)
        {
            *d++ += startGain * *source++;
            startGain += increment;
        }
    }

    /** Copies samples from another buffer to this one.

        @param destChannel          the channel within this buffer to copy the samples to
        @param destStartSample      the start sample within this buffer's channel
        @param source               the source buffer to read from
        @param sourceChannel        the channel within the source buffer to read from
        @param sourceStartSample    the offset within the source buffer's channel to start reading samples from
        @param numSamples           the number of samples to process

        @see addFrom
    */
    void copyFrom (int destChannel,
                   int destStartSample,
                   const AudioBuffer& source,
                   int sourceChannel,
                   int sourceStartSample,
                   int numSamples) noexcept
    {
        dspAssert (&source != this
                   || sourceChannel != destChannel
                   || sourceStartSample + numSamples <= destStartSample
                   || destStartSample + numSamples <= sourceStartSample);
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (destStartSample >= 0 && destStartSample + numSamples <= size);
        dspAssert (isPositiveAndBelow (sourceChannel, source.numChannels));
        dspAssert (sourceStartSample >= 0 && numSamples >= 0 && sourceStartSample + numSamples <= source.size);

        if (numSamples <= 0)
            return;

        if (source.isClear)
        {
            if (! isClear)
                FloatVectorOperations::clear (channels[destChannel] + destStartSample, numSamples);
        }
        else
        {
            isClear = false;
            FloatVectorOperations::copy (channels[destChannel] + destStartSample,
                                         source.channels[sourceChannel] + sourceStartSample,
                                         numSamples);
        }
    }

    /** Copies samples from an array of floats into one of the channels.

        The hasBeenCleared method will return false after this call if samples have
        been copied.

        @param destChannel          the channel within this buffer to copy the samples to
        @param destStartSample      the start sample within this buffer's channel
        @param source               the source data to use
        @param numSamples           the number of samples to process

        @see addFrom
    */
    void copyFrom (int destChannel,
                   int destStartSample,
                   const Type* source,
                   int numSamples) noexcept
    {
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (destStartSample >= 0 && numSamples >= 0 && destStartSample + numSamples <= size);
        dspAssert (source != nullptr);

        if (numSamples <= 0)
            return;

        isClear = false;
        FloatVectorOperations::copy (channels[destChannel] + destStartSample, source, numSamples);
    }

    /** Copies samples from an array of floats into one of the channels, applying a gain to it.

        The hasBeenCleared method will return false after this call if samples have
        been copied.

        @param destChannel          the channel within this buffer to copy the samples to
        @param destStartSample      the start sample within this buffer's channel
        @param source               the source data to use
        @param numSamples           the number of samples to process
        @param gain                 the gain to apply

        @see addFrom
    */
    void copyFrom (int destChannel,
                   int destStartSample,
                   const Type* source,
                   int numSamples,
                   Type gain) noexcept
    {
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (destStartSample >= 0 && numSamples >= 0 && destStartSample + numSamples <= size);
        dspAssert (source != nullptr);

        if (numSamples <= 0)
            return;

        auto* d = channels[destChannel] + destStartSample;
        isClear = false;
        FloatVectorOperations::copyWithMultiply (d, source, gain, numSamples);
    }

    /** Copies samples from an array of floats into one of the channels, applying a gain ramp.

        The hasBeenCleared method will return false after this call if samples have
        been copied.

        @param destChannel          the channel within this buffer to copy the samples to
        @param destStartSample      the start sample within this buffer's channel
        @param source               the source data to use
        @param numSamples           the number of samples to process
        @param startGain            the gain to apply to the first sample (this is multiplied with
                                    the source samples before they are copied to this buffer)
        @param endGain            The gain that would apply to the sample after the final sample.
                                    The gain that applies to the final sample is
                                    (numSamples - 1) / numSamples * (endGain - startGain). This
                                    ensures a continuous ramp when supplying the same value in
                                    endGain and startGain in subsequent blocks. The gain is linearly
                                    interpolated between the first and last samples.
    */
    void copyFromWithRamp (int destChannel,
                           int destStartSample,
                           const Type* source,
                           int numSamples,
                           Type startGain,
                           Type endGain) noexcept
    {
        dspAssert (isPositiveAndBelow (destChannel, numChannels));
        dspAssert (destStartSample >= 0 && numSamples >= 0 && destStartSample + numSamples <= size);
        dspAssert (source != nullptr);

        if (numSamples <= 0)
            return;

        isClear = false;
        const auto increment = (endGain - startGain) / (Type) numSamples;
        auto* d = channels[destChannel] + destStartSample;

        while (--numSamples >= 0)
        {
            *d++ = startGain * *source++;
            startGain += increment;
        }
    }

    /** Returns a Range indicating the lowest and highest sample values in a given section.

        @param channel      the channel to read from
        @param startSample  the start sample within the channel
        @param numSamples   the number of samples to check
    */
    Range<Type> findMinMax (int channel, int startSample, int numSamples) const noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (isClear)
            return { Type (0.0), Type (0.0) };

        return FloatVectorOperations::findMinAndMax (channels[channel] + startSample, numSamples);
    }

    /** Finds the highest absolute sample value within a region of a channel. */
    Type getMagnitude (int channel, int startSample, int numSamples) const noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (isClear)
            return Type (0.0);

        const auto r = findMinMax (channel, startSample, numSamples);
        return jmax (r.getStart(), -r.getStart(), r.getEnd(), -r.getEnd());
    }

    /** Finds the highest absolute sample value within a region on all channels. */
    Type getMagnitude (int startSample, int numSamples) const noexcept
    {
        Type mag (0.0);

        if (isClear)
            return mag;

        for (int i = 0; i < numChannels; ++i)
            mag = jmax (mag, getMagnitude (i, startSample, numSamples));

        return mag;
    }

    /** Returns the root mean squared level for a region of a channel. */
    Type getRMSLevel (int channel, int startSample, int numSamples) const noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (numSamples <= 0 || isClear || ! isPositiveAndBelow (channel, numChannels))
            return Type (0.0);

        auto* data = channels[channel] + startSample;
        double sum = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            auto sample = data[i];
            sum += sample * sample;
        }

        return static_cast<Type> (std::sqrt (sum / numSamples));
    }

    /** Reverses a part of a channel. */
    void reverse (int channel, int startSample, int numSamples) const noexcept
    {
        dspAssert (isPositiveAndBelow (channel, numChannels));
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (isClear)
            return;

        std::reverse (channels[channel] + startSample,
                      channels[channel] + startSample + numSamples);
    }

    /** Reverses a part of the buffer. */
    void reverse (int startSample, int numSamples) const noexcept
    {
        dspAssert (startSample >= 0 && numSamples >= 0 && startSample + numSamples <= size);

        if (isClear)
            return;

        for (int i = 0; i < numChannels; ++i)
            reverse (i, startSample, numSamples);
    }

    //==============================================================================
    /** This allows templated code that takes an AudioBuffer to access its sample type. */
    using SampleType = Type;

private:
    //==============================================================================
    void allocateData()
    {
        dspAssert (size >= 0);

        auto channelListSize = (size_t) (numChannels + 1) * sizeof (Type*);
        auto requiredSampleAlignment = std::alignment_of_v<Type>;
        size_t alignmentOverflow = channelListSize % requiredSampleAlignment;

        if (alignmentOverflow != 0)
            channelListSize += requiredSampleAlignment - alignmentOverflow;

        allocatedBytes = (size_t) numChannels * (size_t) size * sizeof (Type) + channelListSize + 32;
        allocatedData.malloc (allocatedBytes);

        if (allocatedData.get() == nullptr)
        {
            // Allocation failure!
            dspAssert (false);
            allocatedBytes = 0;
            return;
        }

        channels = unalignedPointerCast<Type**> (allocatedData.get());
        auto chan = unalignedPointerCast<Type*> (allocatedData + channelListSize);

        for (int i = 0; i < numChannels; ++i)
        {
            channels[i] = chan;
            chan += size;
        }

        channels[numChannels] = nullptr;
        isClear = false;
    }

    void allocateChannels (Type* const* dataToReferTo, int offset)
    {
        dspAssert (offset >= 0);

        // (try to avoid doing a malloc here, as that'll blow up things like Pro-Tools)
        if (numChannels < (int) numElementsInArray (preallocatedChannelSpace))
        {
            channels = static_cast<Type**> (preallocatedChannelSpace);
        }
        else
        {
            allocatedData.malloc (numChannels + 1, sizeof (Type*));
            channels = unalignedPointerCast<Type**> (allocatedData.get());
        }

        for (int i = 0; i < numChannels; ++i)
        {
            // you have to pass in the same number of valid pointers as numChannels
            dspAssert (dataToReferTo[i] != nullptr);
            channels[i] = dataToReferTo[i] + offset;
        }

        channels[numChannels] = nullptr;
        isClear = false;
    }

    /*  On iOS/arm7 the alignment of `double` is greater than the alignment of
        `std::max_align_t`, so we can't trust max_align_t. Instead, we query
        lots of primitive types and use the maximum alignment of all of them.
    */
    static constexpr size_t getMaxAlignment() noexcept
    {
        constexpr size_t alignments[] { alignof (std::max_align_t),
                                        alignof (void*),
                                        alignof (float),
                                        alignof (double),
                                        alignof (long double),
                                        alignof (short int),
                                        alignof (int),
                                        alignof (long int),
                                        alignof (long long int),
                                        alignof (bool),
                                        alignof (char),
                                        alignof (char16_t),
                                        alignof (char32_t),
                                        alignof (wchar_t) };

        size_t max = 0;

        for (const auto elem : alignments)
            max = jmax (max, elem);

        return max;
    }

    int numChannels = 0, size = 0;
    size_t allocatedBytes = 0;
    Type** channels = nullptr;
    HeapBlock<char, true> allocatedData;
    Type* preallocatedChannelSpace[32];
    bool isClear = false;
    static constexpr size_t maxAlignment = getMaxAlignment();
};

//==============================================================================
template <typename Type>
bool operator== (const AudioBuffer<Type>& a, const AudioBuffer<Type>& b)
{
    if (a.getNumChannels() != b.getNumChannels())
        return false;

    for (auto c = 0; c < a.getNumChannels(); ++c)
    {
        const auto begin = [c] (auto& x) { return x.getReadPointer (c); };
        const auto end = [c] (auto& x) { return x.getReadPointer (c) + x.getNumSamples(); };

        if (! std::equal (begin (a), end (a), begin (b), end (b)))
            return false;
    }

    return true;
}

template <typename Type>
bool operator!= (const AudioBuffer<Type>& a, const AudioBuffer<Type>& b)
{
    return ! (a == b);
}

//==============================================================================
/** Ports literales de utilidades de juce_core/maths que consume el motor.
    Los aliases enteros deben declararse ANTES de Random.
*/
using int32 = int32_t;
using int64 = int64_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

//==============================================================================
/** Port literal de juce::Random (juce_core/maths/juce_Random) reducido a la
    superficie que consume el motor: setSeed + nextFloat. LCG idéntico
    (multiplicador 0x5deece66d, máscara 48 bits, retorno seed>>16). Los
    consumidores ya siembran con semillas deterministas (sin entropía de
    reloj — filosofía WASM-safe del proyecto).
*/
class Random
{
public:
    Random() = default;

    void setSeed (int64 newSeed) noexcept
    {
        seed = newSeed;
    }

    int nextInt() noexcept
    {
        seed = (int64) (((((uint64) seed) * 0x5deece66dLL) + 11) & 0xffffffffffffLL);
        return (int) (seed >> 16);
    }

    float nextFloat() noexcept
    {
        auto result = static_cast<float> (static_cast<uint32> (nextInt()))
                      / (static_cast<float> (std::numeric_limits<uint32>::max()) + 1.0f);
        return dsp::jmin (result, 1.0f - std::numeric_limits<float>::epsilon());
    }

private:
    int64 seed = 1;
};

} // namespace abd::dsp

//==============================================================================
// Detector de fugas: sustituto del JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
// de juce_core. Se incluye desde aqui para que el macro siga estando disponible
// donde antes lo ponia juce_core, sin tocar a los consumidores.
#include "DspLeakedObjectDetector.h"
#include "DspMath.h"
