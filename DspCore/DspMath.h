/*
  ==============================================================================

    DspMath.h
    Matematicas trascendentes DETERMINISTAS del sustrato compartido (abd::dsp).

    POR QUE EXISTE. La libm del sistema NO es la misma en todas las
    plataformas: medido en esta maquina (barrido de 400.000 argumentos),
    MSVC y musl/emscripten difieren en 1 ulp en 611/400.000 valores de
    sinf y 24.694/400.000 de atanf. En un lazo de realimentacion ese 1 ulp
    se amplifica por encima del presupuesto de 0 ulps de la paridad del
    motor (aparecia como 10 ulps en una muestra de la cola del chorus en el
    escenario C de Tests/neuronik_wasm_parity.mjs).

    La invariante del motor es paridad bit a bit nativo <-> WASM. La unica
    forma de sostenerla con trascendentes es calcularlas con una secuencia
    FIJA de operaciones IEEE-754 basicas (+ - * /), identica en los dos
    toolchains. Eso es este fichero.

    AVISO DE SONIDO: los valores NO son los de la libm de la plataforma.
    Sustituirla es un cambio de sonido deliberado y documentado (el motor es
    pre-1.0; ver HANDOFF.md). Se usa como REFERENCIA de precision en
    DspCoreTests.cpp, nunca como implementacion.

    CONTRATO DE EVALUACION. Para que nativo y WASM evalúen el polinomio igual
    hay que impedir la contraccion FMA (clang puede fusionar a*b+c en fmaf;
    MSVC /O2 no contrae). El target WASM compila con -ffp-contract=off (ver
    wasm/CMakeLists.txt). Sin esa bandera, este modulo no garantiza paridad.

    Precision objetivo: error absoluto ~1e-7 en los rangos de audio (el
    epsilon de float es ~1,2e-7), suficiente para LFOs y saturacion.

  ==============================================================================
*/

#pragma once

namespace abd::dsp
{

namespace dspmath_detail
{
    constexpr float kPi       = 3.14159265358979323846f;
    constexpr float kHalfPi   = 1.57079632679489661923f;
    constexpr float kQuarterPi = 0.78539816339744830961f;
    // pi/2 partido en dos floats (Cody-Waite): resta la cancelacion de q*pi/2
    // sin perder precision. hi = (float)(pi/2); lo = pi/2 - hi en float.
    constexpr float kHalfPiHi = 1.57079637050628662109375f;
    constexpr float kHalfPiLo = -4.371139000186243e-08f;
    /** tan(pi/8): frontera de la reduccion de atan. */
    constexpr float kTanPi8   = 0.41421356237309504880f;

    /** sin(r) para |r| <= pi/4 (Taylor, 4 terminos). */
    inline float sinPoly (float r) noexcept
    {
        const float r2 = r * r;
        float p = -1.0f / 5040.0f;      // -1/7!
        p = p * r2 + (1.0f / 120.0f);   //  1/5!
        p = p * r2 - (1.0f / 6.0f);     // -1/3!
        p = p * r2 + 1.0f;
        return r * p;
    }

    /** cos(r) para |r| <= pi/4 (Taylor, 5 terminos). */
    inline float cosPoly (float r) noexcept
    {
        const float r2 = r * r;
        float p = 1.0f / 40320.0f;      //  1/8!
        p = p * r2 - (1.0f / 720.0f);   // -1/6!
        p = p * r2 + (1.0f / 24.0f);    //  1/4!
        p = p * r2 - 0.5f;              // -1/2!
        p = p * r2 + 1.0f;
        return p;
    }

    /** Redondeo a entero mas cercano, sin libm (empates hacia arriba en magnitud,
        consistente en los dos toolchains). */
    inline int roundToInt (float v) noexcept
    {
        return v >= 0.0f ? (int) (v + 0.5f) : (int) (v - 0.5f);
    }
}

/** sin(x) determinista (sin libm). x en radianes. */
inline float sin (float x) noexcept
{
    const int   q = dspmath_detail::roundToInt (x / dspmath_detail::kHalfPi);
    const float r = x - (float) q * dspmath_detail::kHalfPiHi - (float) q * dspmath_detail::kHalfPiLo;

    switch (q & 3)
    {
        case 0:  return  dspmath_detail::sinPoly (r);
        case 1:  return  dspmath_detail::cosPoly (r);
        case 2:  return -dspmath_detail::sinPoly (r);
        default: return -dspmath_detail::cosPoly (r);
    }
}

/** cos(x) determinista (sin libm). x en radianes. */
inline float cos (float x) noexcept
{
    const int   q = dspmath_detail::roundToInt (x / dspmath_detail::kHalfPi);
    const float r = x - (float) q * dspmath_detail::kHalfPiHi - (float) q * dspmath_detail::kHalfPiLo;

    switch (q & 3)
    {
        case 0:  return  dspmath_detail::cosPoly (r);
        case 1:  return -dspmath_detail::sinPoly (r);
        case 2:  return -dspmath_detail::cosPoly (r);
        default: return  dspmath_detail::sinPoly (r);
    }
}

/** atan(x) determinista (sin libm).

    Reduccion por identidades (solo + - * /):
        atan(a) = pi/4 + atan((a-1)/(a+1))   para a > tan(pi/8)
    y, para |a| > 1, la misma identidad converge sola (maximo 2-3 pasos).
    El polinomio final es la serie de Taylor de atan (hasta t^15) sobre
    |t| <= tan(pi/8).
*/
inline float atan (float x) noexcept
{
    const bool  neg = x < 0.0f;
    float       a   = neg ? -x : x;
    float       angle = 0.0f;

    while (a > dspmath_detail::kTanPi8)
    {
        a = (a - 1.0f) / (a + 1.0f);
        angle += dspmath_detail::kQuarterPi;
    }

    const float t2 = a * a;
    float p = -1.0f / 15.0f;
    p = p * t2 + (1.0f / 13.0f);
    p = p * t2 - (1.0f / 11.0f);
    p = p * t2 + (1.0f / 9.0f);
    p = p * t2 - (1.0f / 7.0f);
    p = p * t2 + (1.0f / 5.0f);
    p = p * t2 - (1.0f / 3.0f);
    p = p * t2 + 1.0f;

    const float result = angle + a * p;
    return neg ? -result : result;
}

} // namespace abd::dsp
