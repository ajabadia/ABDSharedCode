/*
  Prueba de bit-identidad: el DspMath.h NUEVO contra el VIEJO (sacado de git),
  en el rango que ya sonaba. NO forma parte del modulo: se compila a mano.

      git show HEAD:DspCore/DspMath.h > /tmp/old_math_raw.h
      sed -e 's/namespace abd::dsp/namespace oldimpl/' \
          -e 's/dspmath_detail/old_detail/g' /tmp/old_math_raw.h > /tmp/old_math.h
      g++ -std=c++17 -O2 -I. /tmp/bitident.cpp -o /tmp/bitident && /tmp/bitident
*/

#include "DspCore/DspMath.h"
#include "old_math.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

static int64_t diffsSin = 0, diffsCos = 0;
static float   firstSin = 0.0f, firstCos = 0.0f;

static void compare (double lo, double hi, int64_t steps, const char* label)
{
    for (int64_t i = 0; i <= steps; ++i)
    {
        const float x = static_cast<float> (lo + (hi - lo) * (double) i / (double) steps);

        const float newS = abd::dsp::sin (x);
        const float oldS = oldimpl::sin (x);
        if (std::memcmp (&newS, &oldS, 4) != 0) { if (diffsSin == 0) firstSin = x; ++diffsSin; }

        const float newC = abd::dsp::cos (x);
        const float oldC = oldimpl::cos (x);
        if (std::memcmp (&newC, &oldC, 4) != 0) { if (diffsCos == 0) firstCos = x; ++diffsCos; }
    }

    std::printf ("  %-22s sin: %lld diffs%s | cos: %lld diffs%s\n",
                 label, (long long) diffsSin, diffsSin ? "" : "  (identico)",
                 (long long) diffsCos, diffsCos ? "" : "  (identico)");

    if (diffsSin) { std::printf ("      primera en x = %.9g\n", (double) firstSin); diffsSin = 0; firstSin = 0.0f; }
    if (diffsCos) { std::printf ("      primera en x = %.9g\n", (double) firstCos); diffsCos = 0; firstCos = 0.0f; }
}

int main()
{
    std::printf ("\nBIT-IDENTIDAD nuevo vs viejo (git HEAD)\n");
    std::printf ("  Si salen >0 diffs dentro de [-16, 16], el arreglo cambio el sonido.\n\n");

    compare (-16.0,  16.0,  4000000, "[-16, 16] audio");
    compare (-8.0,    8.0,  2000000, "[-8, 8] audio");
    compare (-3.14159, 3.14159, 2000000, "[-pi, pi] audio");

    std::printf ("\n  (fuera de [-16, 16] SI cambia, y es lo que se queria arreglar:\n");
    std::printf ("   el viejo %(x) daba basura por la perdida de la reduccion en float)\n");

    {
        const float xs[] = { 1e3f, 1e4f, 1e5f, 1e6f, 1e7f };
        for (float x : xs)
        {
            std::printf ("   sin(%-9g) nuevo = %-14.7g viejo = %-14.7g libm = %-14.7g\n",
                         (double) x, (double) abd::dsp::sin (x), (double) oldimpl::sin (x),
                         (double) std::sin (x));
        }
    }

    return 0;
}
