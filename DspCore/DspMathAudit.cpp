/*
  ==============================================================================

    DspMathAudit.cpp  (andamiaje de medicion, NO parte del modulo)

    Compara `abd::dsp::*` contra la libm de la plataforma, que es la
    REFERENCIA de verdad, para responder a una pregunta concreta: las
    trascendentes deterministas estan CORRECTAS, o solo son deterministas?

    No entra en CMake ni en el modulo: esto se compila a mano y se tira.

    Como compilarlo (MSVC, desde la raiz de ABDSharedCode):

        cl /nologo /EHsc /std:c++17 /O2 /I. DspCore/DspMathAudit.cpp /Fe:audit.exe
        audit.exe

    Y en gcc/clang:

        g++ -std=c++17 -O2 -I. DspCore/DspMathAudit.cpp -o audit && ./audit

  ==============================================================================
*/

#include "DspCore/DspMath.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using namespace abd::dsp;

static float ulpsApart(float a, float b) noexcept
{
    int32_t ia = 0, ib = 0;
    std::memcpy(&ia, &a, 4);
    std::memcpy(&ib, &b, 4);

    int64_t oa = ia, ob = ib;
    if (oa < 0) oa = 0x80000000LL - oa;
    if (ob < 0) ob = 0x80000000LL - ob;

    return static_cast<float>(oa > ob ? oa - ob : ob - oa);
}

struct Acc
{
    double maxAbs = 0.0;
    double atAbs  = 0.0;
    float maxUlp  = 0.0f;
    float atUlp   = 0.0f;
    int64_t n     = 0;
    int64_t nan   = 0;
    int64_t inf   = 0;

    void add(float got, float want) noexcept
    {
        ++n;

        if (std::isnan(got) || std::isnan(want))
        {
            ++nan;
            return;
        }
        if (std::isinf(got) != std::isinf(want))
        {
            ++inf;
            return;
        }
        if (std::isinf(got)) return;

        const double d = std::fabs((double)got - (double)want);

        if (d > maxAbs)
        {
            maxAbs = d;
            atAbs  = (double)got;
        }

        const float u = ulpsApart(got, want);
        if (u > maxUlp)
        {
            maxUlp = u;
            atUlp  = got;
        }
    }

    void report(const char* name) const
    {
        std::printf("  %-26s max|err| = %.3e   max ulps = %8.0f   nan/inf descuadrados = %lld/%lld  (n=%lld)\n",
                    name, maxAbs, (double)maxUlp, (long long)nan, (long long)inf, (long long)n);
    }
};

template <typename F, typename R>
static Acc sweep(F ours, R ref, float lo, float hi, int64_t steps)
{
    Acc a;

    for (int64_t i = 0; i <= steps; ++i)
    {
        const float x = lo + (hi - lo) * (float)i / (float)steps;
        a.add(ours(x), ref(x));
    }

    return a;
}

int main()
{
    std::printf("\n=== 1. PRECISION EN EL RANGO DE AUDIO (error relativo a libm) ===\n");

    {
        Acc a = sweep(+[](float x) { return sin(x); }, +[](float x) { return std::sin(x); }, -8.0f, 8.0f, 400000);
        a.report("sin  [-8, 8]");

        Acc b = sweep(+[](float x) { return cos(x); }, +[](float x) { return std::cos(x); }, -8.0f, 8.0f, 400000);
        b.report("cos  [-8, 8]");

        Acc c = sweep(+[](float x) { return atan(x); }, +[](float x) { return std::atan(x); }, -64.0f, 64.0f, 400000);
        c.report("atan [-64, 64]");

        Acc d = sweep(+[](float x) { return tanh(x); }, +[](float x) { return std::tanh(x); }, -8.0f, 8.0f, 400000);
        d.report("tanh [-8, 8]");

        Acc e = sweep(+[](float x) { return log2(x); }, +[](float x) { return std::log2(x); }, 1e-6f, 1e6f, 400000);
        e.report("log2 [1e-6, 1e6]");

        Acc f = sweep(+[](float x) { return exp2(x); }, +[](float x) { return std::exp2(x); }, -80.0f, 80.0f, 400000);
        f.report("exp2 [-80, 80]");

        Acc g = sweep(+[](float x) { return pow(x, 2.5f); }, +[](float x) { return std::pow(x, 2.5f); }, 1e-3f, 1e3f, 200000);
        g.report("pow(x,2.5) [1e-3,1e3]");
    }

    std::printf("\n=== 2. ERROR DE REDUCCION DE RANGO EN sin/cos (|r| -> pi/4) ===\n");
    std::printf("  (el polinomio se evalua con |r| <= pi/4; el maximo error esta ahi)\n");

    {
        Acc worst;
        for (int i = 0; i <= 200000; ++i)
        {
            const float r = -0.7853981634f + 1.5707963268f * (float)i / 200000.0f;
            worst.add(sin(r), std::sin(r));
        }
        worst.report("sin  |r| <= pi/4");

        Acc worst2;
        for (int i = 0; i <= 200000; ++i)
        {
            const float r = -0.7853981634f + 1.5707963268f * (float)i / 200000.0f;
            worst2.add(cos(r), std::cos(r));
        }
        worst2.report("cos  |r| <= pi/4");
    }

    std::printf("\n=== 3. CASOS LIMITE: lo que libm define y el codigo no ===\n");

    struct Case
    {
        const char* what;
        float x;
    };
    const Case cases[] =
        {
            {"log2(1e-40) denormal", 1e-40f},
            {"log2(1.1754944e-38)", 1.1754944e-38f},
            {"log2(0)", 0.0f},
            {"exp2(128.5)", 128.5f},
            {"exp2(129.0)", 129.0f},
            {"exp2(200)", 200.0f},
            {"exp2(-200)", -200.0f},
            {"atan(inf)", INFINITY},
            {"sin(1e6)", 1e6f},
            {"cos(1e7)", 1e7f},
        };

    for (const auto& c : cases)
    {
        std::printf("  %-24s x = %-14g ", c.what, (double)c.x);

        if (std::strstr(c.what, "log2"))
            std::printf("nuestro = %-14g libm = %g\n", (double)log2(c.x), std::log2(c.x));
        else if (std::strstr(c.what, "exp2"))
            std::printf("nuestro = %-14g libm = %g\n", (double)exp2(c.x), std::exp2(c.x));
        else if (std::strstr(c.what, "atan"))
            std::printf("nuestro = %-14g libm = %g\n", (double)atan(c.x), std::atan(c.x));
        else if (std::strstr(c.what, "sin"))
            std::printf("nuestro = %-14g libm = %g\n", (double)sin(c.x), std::sin(c.x));
        else
            std::printf("nuestro = %-14g libm = %g\n", (double)cos(c.x), std::cos(c.x));
    }

    std::printf("\n=== 4. pow2i FUERA del rango exponencial del float ===\n");
    for (int n : {100, 126, 127, 128, 129, 130, 200, 1000, -126, -127, -128, -200, -1000})
    {
        const float got  = pow2i(n);
        const float libm = std::ldexp(1.0f, n);

        const char* verdict = got == libm ? "ok"
                                          : (std::isinf(libm) ? "  <-- DEBERIA ser infinito"
                                                              : (libm == 0.0f ? "  <-- DEBERIA ser cero" : "  <-- MAL"));

        std::printf("  pow2i(%6d) = %-16g libm = %-12g%s\n", n, (double)got, (double)libm, verdict);
    }

    std::printf("\n=== 5. exp2 CRUCANDO el limite de overflow ===\n");
    for (float x : {127.5f, 128.0f, 128.4f, 128.5f, 129.0f, 130.0f, 200.0f})
    {
        const float got     = exp2(x);
        const float libm    = std::exp2(x);
        const bool signFlip = (got == 0.0f && std::signbit(got) != std::signbit(libm));

        std::printf("  exp2(%7.1f) = %-14g libm = %-14g%s%s\n", (double)x, (double)got, (double)libm,
                    got == libm ? "" : "  <-- MAL",
                    signFlip ? "   Y CON SIGNO CAMBIADO" : "");
    }

    std::printf("\n=== 6. CONTINUIDAD/MONOTONIA en log2 (los 10 ultimos bits de la mantisa) ===\n");
    {
        bool jumps      = false;
        float worstJump = 0.0f;
        float prev      = log2(1.0f);

        for (uint32_t i = 1; i < 400000; ++i)
        {
            union
            {
                uint32_t u;
                float f;
            } v;
            v.u              = 0x3f800000u + i; // 1.0f, 1.0f+ulp, 1.0f+2ulp, ...
            const float cur  = log2(v.f);
            const float step = cur - prev;

            if (step < 0.0f || step > worstJump)
            {
                if (step > worstJump) worstJump = step;
            }
            if (step < 0.0f) jumps = true;
            prev = cur;
        }

        std::printf("  log2 justo por encima de 1: saltos hacia atras = %s, salto maximo = %.3e\n",
                    jumps ? "SI" : "no", (double)worstJump);
    }

    std::printf("\n=== 7. DENORMALES: que pasa alLavarlos en un lazo de realimentacion ===\n");
    {
        // El lazo tipico: realimentar un valor denormal miles de veces.
        float x = 1e-30f;
        for (int i = 0; i < 200; ++i) x = x * 0.5f; // bajo a denormal

        std::printf("  tras 200 pasos de realimentacion por 0.5: x = %.6e (denormal: %s)\n",
                    (double)x, std::fpclassify(x) == FP_SUBNORMAL ? "si" : "no");

        // atanh es lo que usa log2 por dentro; con un denormal por la puerta de atanh?
        std::printf("  atanh(1e-30) = %.6e  (deberia ser ~1e-30, no un salto)\n", (double)atanh(1e-30f));
        std::printf("  log2(1e-30) = %.6g   libm = %.6g\n", (double)log2(1e-30f), std::log2(1e-30f));
    }

    std::printf("\n=== 8. BIT-IDENTIDAD EN EL RANGO QUE YA SUENA ===\n");
    std::printf("  (sin/cos arreglados tienen que dar EXACTAMENTE lo mismo que antes\n");
    std::printf("   para |x| <= 16, o el arreglo habria cambiado el sonido del chorus)\n");

    {
        // La reduccion que habia ANTES del arreglo, aqui replicada.
        auto oldSin = [](float x) {
            const int q   = (int)(x / 1.57079632679489661923f + 0.5f);
            const float r = x - (float)q * 1.57079637050628662109375f - (float)q * -4.371139000186243e-08f;
            switch (q & 3)
            {
                case 0:
                    return r - (r * r * r) / 6.0f + (r * r * r * r * r) / 120.0f - (r * r * r * r * r * r * r) / 5040.0f;
                case 1:
                    return 1.0f - r * r / 2.0f + r * r * r * r / 24.0f - r * r * r * r * r * r / 720.0f + r * r * r * r * r * r * r * r / 40320.0f;
                case 2:
                    return -(r - (r * r * r) / 6.0f + (r * r * r * r * r) / 120.0f - (r * r * r * r * r * r * r) / 5040.0f);
                default:
                    return -(1.0f - r * r / 2.0f + r * r * r * r / 24.0f - r * r * r * r * r * r / 720.0f + r * r * r * r * r * r * r * r / 40320.0f);
            }
        };

        int64_t diffs = 0;
        float firstAt = 0.0f;

        for (int i = 0; i <= 2000000; ++i)
        {
            const float x = -16.0f + 32.0f * (float)i / 2000000.0f;
            const float a = sin(x);
            const float b = oldSin(x);

            if (std::memcmp(&a, &b, sizeof(float)) != 0)
            {
                if (diffs == 0) firstAt = x;
                ++diffs;
            }
        }

        std::printf("  sin: diferencias de bits en [-16, 16] = %lld", (long long)diffs);

        if (diffs != 0) std::printf("   (primera en x = %.6g)", (double)firstAt);
        std::printf("  -> %s\n", diffs == 0 ? "IDENTICO" : "CAMBIA EL SONIDO");
    }

    return 0;
}
