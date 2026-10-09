// ─────────────────────────── ControlSequencer ───────────────────────────
//
// El secuenciador de control compartido (abd::synth::ControlSequencer). Lo
// que se fija aquí:
//
//  - Los 32 pasos BIPOLARES: setStep es 1-based, clampea a [−1,+1], ignora
//    los índices fuera de rango, y un recorrido completo del secuenciador
//    devuelve en cada paso SU valor exacto (−1 .. +1 de punta a punta).
//  - La tabla de los 16 divisores de reloj (quartersPerStep), con los
//    valores exactos del manual y el clamp de los índices fuera de rango.
//  - La ley matemática del swing: r = 0,5 + 0,25·swing sobre la duración base
//    del paso; el paso PAR dura r·base y el IMPAR (2−r)·base. Los dos pasos
//    del par suman siempre 2·base, y subir el swing alarga el par y acorta
//    el impar. (La suite de ABDEep ya fija que con swing 0 el paso par dura
//    LA MITAD: 2756,25 de 5512,5 — aquí se mide lo mismo en otra división.)
//  - El slew exponencial: coef = 1 − exp(−1/(s·sr)), con s = slew + mod de
//    matriz clampeado a [0,1]. Slew 0 salta en una muestra; s = 0,5 tras
//    0,5 s recorre ≈ 1−e^−1 del camino; s = 1,0 (tau doble) va más lento.
//
// Este .inc se incluye DENTRO de `namespace abd::synth::tests` (lo abre
// SynthCoreTests.cpp): NO abre namespace propio ni incluye cabeceras de std.
// ControlSequencer.h se incluye arriba, FUERA del namespace.

// Mide cuántas nextSample() dura el paso par (0→1) y el impar (1→2) con la
// configuración dada: base = quartersPerStep(7) · 60/120 · 44100 = 22050.
static void medirDuracionesSwing (float swing, int& dPar, int& dImpar)
{
    abd::synth::ControlSequencer cs;
    cs.prepare (44100.0);
    cs.setEnabled (true);
    cs.setClockDivider (7);        // 1 negra por paso (quartersPerStep(7) = 1)
    cs.setMasterBpm (120.0f);      // base = 60/120 · 44100 = 22050 muestras
    cs.setLength (32);
    cs.setSwing (swing);

    dPar = 0;
    while (cs.getCurrentStep() == 0 && dPar < 1000000) { cs.nextSample(); ++dPar; }

    dImpar = 0;
    while (cs.getCurrentStep() == 1 && dImpar < 1000000) { cs.nextSample(); ++dImpar; }
}

// Suelta el secuenciador con slew dado y devuelve el valor tras k muestras.
// Divisor 0 y 20 BPM: base = 16 · 60/20 · 44100 = 2.116.800 — el primer paso
// (par, 1.058.400) dura un rato, así que k muestras caben siempre dentro.
static float correrSlew (float slew, float slewMod, int k)
{
    abd::synth::ControlSequencer cs;
    cs.prepare (44100.0);
    cs.setEnabled (true);
    cs.setClockDivider (0);
    cs.setMasterBpm (20.0f);
    cs.setLength (32);
    cs.setSlewRate (slew);
    cs.setSlewModulation (slewMod);
    cs.setStep (1, 1.0f);          // objetivo +1; el paso 0 arranca en 0
    float v = 0.0f;
    for (int i = 0; i < k; ++i) v = cs.nextSample();
    return v;
}

static void testControlSequencerBipolarSteps()
{
    printf("=== ControlSequencer: 32 pasos bipolares ===\n");

    // ── Límites de setStep: 1-based, fuera de rango ignorado, clamp ──
    {
        abd::synth::ControlSequencer cs;
        cs.setEnabled (true);
        cs.setStep (1, 0.25f);
        cs.setStep (0, -0.999f);    // el 0 no existe: se ignora
        cs.setStep (33, -0.999f);   // el 33 tampoco: se ignora
        cs.prepare (44100.0);
        const float v = cs.nextSample();
        check (std::abs (v - 0.25f) < 1.0e-6f,
               "setStep es 1-based: los pasos 0 y 33 se ignoran");

        cs.setStep (1, 5.0f);
        check (cs.nextSample() == 1.0f, "el paso se clampea a +1 por arriba");
        cs.setStep (1, -5.0f);
        check (cs.nextSample() == -1.0f, "y a −1 por abajo");
    }

    // ── Recorrido completo: 32 pasos, cada uno con SU valor ──
    {
        abd::synth::ControlSequencer cs;
        float esperado[32];
        for (int i = 0; i < 32; ++i)
        {
            esperado[i] = -1.0f + 2.0f * static_cast<float>(i) / 31.0f;
            cs.setStep (i + 1, esperado[i]);
        }
        cs.prepare (44100.0);
        cs.setEnabled (true);
        cs.setLength (32);          // por defecto viene con 1: hace falta ampliarlo

        // La duración sale de la TABLA, no de una suposición: el divisor por
        // defecto (13 = 1/16) a 120 BPM son 5512,5 muestras por paso, y con
        // swing 0 los 32 pasos suman 32·base (los pares alargan y los impares
        // acortan, pero la pareja siempre suma 2·base).
        const double porPaso = abd::synth::ControlSequencer::quartersPerStep (13)
                             * 60.0 / 120.0 * 44100.0;
        const int kRecorrido = static_cast<int> (32.0 * porPaso) + 44100;  // + 1 s de margen

        bool visto[32] = {};
        float vistoValor[32] = {};
        for (int k = 0; k < kRecorrido; ++k)
        {
            const float v = cs.nextSample();
            const int s = cs.getCurrentStep();
            if (s >= 0 && s < 32 && !visto[s])
            {
                visto[s] = true;
                vistoValor[s] = v;
            }
        }

        bool todos = true, valores = true;
        for (int i = 0; i < 32; ++i)
        {
            if (!visto[i]) todos = false;
            if (std::abs (vistoValor[i] - esperado[i]) > 1.0e-6f) valores = false;
        }
        check (todos, "el recorrido pasa por los 32 pasos y vuelve a empezar");
        check (valores, "cada paso devuelve SU valor bipolar exacto (−1..+1)");
    }
}

static void testControlSequencerClockTable()
{
    printf("=== ControlSequencer: tabla de 16 divisores ===\n");

    static const float tabla[16] = {
        16.0f,        //  0  4 notas enteras
        12.0f,        //  1  3 notas enteras
        8.0f,         //  2  2 notas enteras
        4.0f,         //  3  1 nota entera
        2.0f,         //  4  1/2
        1.5f,         //  5  3/8 (negra con puntillo)
        4.0f / 3.0f,  //  6  1/3
        1.0f,         //  7  1/4
        0.75f,        //  8  3/16
        2.0f / 3.0f,  //  9  1/6
        0.5f,         // 10  1/8
        0.375f,       // 11  3/32
        1.0f / 3.0f,  // 12  1/12
        0.25f,        // 13  1/16 (el valor por defecto del manual)
        3.0f / 16.0f, // 14  3/64
        1.0f / 6.0f   // 15  1/24
    };

    bool todaLaTabla = true;
    for (int i = 0; i < 16; ++i)
        if (std::abs (abd::synth::ControlSequencer::quartersPerStep (i) - tabla[i]) > 1.0e-6f)
            todaLaTabla = false;
    check (todaLaTabla, "las 16 divisiones coinciden con la tabla del manual");

    check (std::abs (abd::synth::ControlSequencer::quartersPerStep (-1) - tabla[0]) < 1.0e-6f,
           "un divisor negativo se clampea al primero");
    check (std::abs (abd::synth::ControlSequencer::quartersPerStep (99) - tabla[15]) < 1.0e-6f,
           "un divisor > 15 se clampea al último");
}

static void testControlSequencerSwingAndSlew()
{
    printf("=== ControlSequencer: ley del swing y slew exponencial ===\n");

    // ── Swing 0: r = 0,5 — el paso par dura LA MITAD de la base (22050) ──
    int par0 = 0, impar0 = 0;
    medirDuracionesSwing (0.0f, par0, impar0);
    check (std::abs (par0 - 11025) <= 1,
           "swing 0: el paso par dura r·base = 0,5 · 22050 = 11025");
    check (std::abs (impar0 - 33075) <= 1,
           "swing 0: el paso impar dura (2−r)·base = 1,5 · 22050 = 33075");

    // ── Swing 1: r = 0,75 — el par alarga, el impar se acorta ──
    int par1 = 0, impar1 = 0;
    medirDuracionesSwing (1.0f, par1, impar1);
    check (std::abs (par1 - 16537.5) <= 1,
           "swing 1: el paso par alarga a 0,75 · 22050 = 16537,5");
    check (std::abs (impar1 - 27562.5) <= 1,
           "swing 1: el paso impar se acorta a 1,25 · 22050 = 27562,5");

    check (par1 > par0 && impar1 < impar0,
           "el swing reparte el par: sube el par, baja el impar");
    check (std::abs ((par0 + impar0) - 44100) <= 2 && std::abs ((par1 + impar1) - 44100) <= 2,
           "el par completo suma SIEMPRE 2 · base = 44100");

    // ── Slew exponencial ──
    const float v05 = correrSlew (0.5f, 0.0f, 22050);
    const float v10 = correrSlew (1.0f, 0.0f, 22050);
    const float vmod = correrSlew (0.0f, 0.5f, 22050);
    const float v00 = correrSlew (0.0f, 0.0f, 1);

    // s = 0,5 -> tau 0,5 s: tras 0,5 s queda 1 − e^−1 ≈ 0,6321
    check (std::abs (v05 - 0.6321f) < 0.01f,
           "slew 0,5 tras 0,5 s sigue la exponencial (≈ 1 − e^−1)");
    // s = 1,0 -> tau 1 s: tras 0,5 s queda 1 − e^−0,5 ≈ 0,3935
    check (std::abs (v10 - 0.3935f) < 0.01f,
           "slew 1,0 (tau doble) va más lento (≈ 1 − e^−0,5)");
    check (v05 > v10, "más segundos de slew = glide más lento");
    check (std::abs (vmod - v05) < 0.02f,
           "la modulación de matriz se SUMA al mando de slew");
    check (v00 == 1.0f, "slew 0 salta al objetivo en una sola muestra");
    check (v05 < 1.0f, "el glide se acerca pero no rebasa el objetivo");

    // Monótonía del glide: nunca retrocede hacia el objetivo.
    {
        abd::synth::ControlSequencer cs;
        cs.prepare (44100.0);
        cs.setEnabled (true);
        cs.setClockDivider (0);
        cs.setMasterBpm (20.0f);
        cs.setLength (32);
        cs.setSlewRate (0.5f);
        cs.setStep (1, 1.0f);

        float prev = -1.0f;
        bool mono = true;
        for (int i = 0; i < 22050; ++i)
        {
            const float v = cs.nextSample();
            if (v < prev) mono = false;
            prev = v;
        }
        check (mono, "el glide es monótono (nunca rebasa ni retrocede)");
    }
}
