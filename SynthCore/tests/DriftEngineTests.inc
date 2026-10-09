// ─────────────────────────── DriftEngine ───────────────────────────
//
// El motor de drift analógico compartido (abd::synth::DriftEngine): ruido de
// Brown / random-walk suavizado con LCG propio (driftSeed, arranca en 1 para
// todas las instancias — por eso dos motores con los mismos parámetros son
// bit a bit iguales). Lo que se fija aquí:
//
//  - Determinismo: misma configuración → misma trayectoria, muestra a
//    muestra, bit a bit; y con drift a 0 las cinco salidas quedan en cero
//    EXACTO (la rama de amplitud 0 es determinista, útil para calibración).
//  - Escalado de amplitud: el target se multiplica por la amplitud y el
//    slew es independiente de ella, así que con la MISMA semilla un motor
//    con voiceDrift 1,0 nunca da menos recorrido que uno con 0,25, y su pico
//    es claramente mayor. paramDrift 0 mantiene el drift del filtro en cero.
//  - Invariancia al sample rate: el slew se expresa por SEGUNDO y se divide
//    por el sampleRate, y el intervalo entre targets escala con el sample
//    rate — el mismo proceso a 44,1 y 88,2 kHz coincide en el tiempo real.
//
// Este .inc se incluye DENTRO de `namespace abd::synth::tests` (lo abre
// SynthCoreTests.cpp): NO abre namespace propio ni incluye cabeceras de std.
// DriftEngine.h se incluye arriba, FUERA del namespace.

static void testDriftEngineDeterminism()
{
    printf("=== DriftEngine: determinismo ===\n");

    // ── Dos motores idénticos: la misma trayectoria, bit a bit ──
    abd::synth::DriftEngine a, b;
    a.setSampleRate(44100.0);
    a.setDriftParams(1.0f, 1.0f, 0.5f);
    b.setSampleRate(44100.0);
    b.setDriftParams(1.0f, 1.0f, 0.5f);

    bool igualdad = true;
    // 40000 muestras ≈ 0,9 s: pasan DOS cambios de target (el intervalo a
    // driftRate 0,5 es ~0,7 s), así que se ejercita el LCG, no solo el glide.
    for (int i = 0; i < 40000; ++i)
    {
        a.nextSample();
        b.nextSample();
        if (a.getOsc1PitchDrift() != b.getOsc1PitchDrift()) igualdad = false;
        if (a.getVcfCutoffDrift() != b.getVcfCutoffDrift()) igualdad = false;
        if (a.getEnvTimeDrift() != b.getEnvTimeDrift()) igualdad = false;
    }
    check(igualdad, "dos motores con los mismos parámetros recorren la misma trayectoria (bit a bit)");

    // ── Drift a 0: las cinco salidas quedan en cero exacto ──
    abd::synth::DriftEngine z;
    z.setSampleRate(44100.0);
    z.setDriftParams(0.0f, 0.0f, 0.5f);
    bool ceros = true;
    for (int i = 0; i < 10000; ++i)
    {
        z.nextSample();
        if (z.getOsc1PitchDrift() != 0.0f) ceros = false;
        if (z.getOsc2PitchDrift() != 0.0f) ceros = false;
        if (z.getVcfCutoffDrift() != 0.0f) ceros = false;
        if (z.getVcfResonanceDrift() != 0.0f) ceros = false;
        if (z.getEnvTimeDrift() != 0.0f) ceros = false;
    }
    check(ceros, "con drift a 0 las cinco salidas quedan en cero exacto");
}

static void testDriftEngineAmplitudeScaling()
{
    printf("=== DriftEngine: escalado de amplitud e invariancia al sample rate ===\n");

    // ── Misma semilla, amplitudes 1,0 vs 0,25 ──
    abd::synth::DriftEngine fuerte, debil;
    fuerte.setSampleRate(44100.0);
    fuerte.setDriftParams(1.0f, 0.0f, 0.5f);
    debil.setSampleRate(44100.0);
    debil.setDriftParams(0.25f, 0.0f, 0.5f);

    bool monoton = true, paramCero = true;
    float maxFuerte = 0.0f, maxDebil = 0.0f;
    for (int i = 0; i < 40000; ++i)
    {
        fuerte.nextSample();
        debil.nextSample();

        const float vf = fuerte.getOsc1PitchDrift();
        const float vd = debil.getOsc1PitchDrift();
        if (std::abs(vf) + 1.0e-6f < std::abs(vd)) monoton = false;
        if (std::abs(vf) > maxFuerte) maxFuerte = std::abs(vf);
        if (std::abs(vd) > maxDebil) maxDebil = std::abs(vd);

        // paramDrift = 0: el filtro NO deriva ni una muestra.
        if (fuerte.getVcfCutoffDrift() != 0.0f) paramCero = false;
        if (fuerte.getVcfResonanceDrift() != 0.0f) paramCero = false;
    }
    check(paramCero, "paramDrift 0 mantiene el drift del filtro en cero");
    check(monoton, "amplitud 1,0 nunca da menos recorrido que 0,25 (misma semilla)");
    check(maxFuerte > maxDebil && maxFuerte > 0.01f,
          "la amplitud mayor alcanza un pico claramente mayor");

    // ── Invariancia al sample rate: misma pared de tiempo, mismos valores ──
    abd::synth::DriftEngine a44, a88;
    a44.setSampleRate(44100.0);
    a44.setDriftParams(1.0f, 1.0f, 0.5f);
    a88.setSampleRate(88200.0);
    a88.setDriftParams(1.0f, 1.0f, 0.5f);

    float maxDiff = 0.0f;
    for (int i = 0; i < 22050; ++i) // 0,5 s de tiempo real
    {
        a44.nextSample();
        a88.nextSample();
        a88.nextSample(); // 88,2 kHz: dos muestras por una

        const float d1 = std::abs(a44.getOsc1PitchDrift() - a88.getOsc1PitchDrift());
        const float d2 = std::abs(a44.getVcfCutoffDrift() - a88.getVcfCutoffDrift());
        if (d1 > maxDiff) maxDiff = d1;
        if (d2 > maxDiff) maxDiff = d2;
    }
    check(maxDiff < 0.01f,
          "el mismo proceso a 44,1 y 88,2 kHz coincide en el tiempo real (invariante al SR)");
    check(std::abs(a44.getOsc1PitchDrift()) > 0.01f,
          "el drift de afinación se ha movido de verdad (la comparación no es 0 vs 0)");
}
