// ─────────────────────────── LfoAnalog ───────────────────────────
//
// El LFO analógico compartido (abd::synth::LfoAnalog) — NO confundir con el
// LFO.h dual estilo MS2000, que tiene sus propios tests en testLFO(). Este es
// el de las 7 formas continuas: kSine, kTriangle, kSquare, kRampUp, kRampDown,
// kSampleHold, kSampleGlide. Lo que se fija aquí:
//
//  - Las 7 formas con su valor EXACTO en p ≈ 1/8 (seno 0,707, triángulo 0,5,
//    cuadrada +1, rampas ±0,75), todas bipolares dentro de [−1,+1], y que
//    seno/triángulo recorren el rango completo mientras la cuadrada solo
//    toma ±1.
//  - Sample & Hold sostiene el valor durante el ciclo; Sample & Glide
//    converge suavemente al nuevo objetivo.
//  - El slew limita la pendiente a 2/(tiempo·sr) por muestra: sin slew la
//    cuadrada salta de 2 unidades de golpe, con slew 1 el salto se reparte.
//  - El techo audio-rate de 1280 Hz (setRate se clampea a 0,005..1280) y el
//    suelo de 0,005 Hz, medidos por la fase que queda tras una ventana.
//  - El fade-in del delay: 40 % silencio + 60 % rampa lineal (spec DeepMind),
//    y que setDelayScale dobla la ventana.
//  - getUnipolar() = bipolar·0,5 + 0,5 exacto, siempre dentro de [0,1].
//
// Este .inc se incluye DENTRO de `namespace abd::synth::tests` (lo abre
// SynthCoreTests.cpp): NO abre namespace propio ni incluye cabeceras de std.
// LfoAnalog.h se incluye arriba, FUERA del namespace.

static void testLFOAnalogWaveforms()
{
    printf("=== LfoAnalog: las 7 formas ===\n");

    // ── Valor de referencia en p = 5512/44100 ≈ 1/8 del ciclo ──
    float valores[5];
    for (int s = 0; s < 5; ++s)
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setRate (1.0f);
        l.setShape (s);
        l.reset();
        float v = 0.0f;
        for (int i = 0; i < 5512; ++i) v = l.nextSample();
        valores[s] = v;
    }

    check (std::abs (valores[0] - 0.7071f) < 0.002f, "kSine vale ≈ 0,707 en p=1/8");
    check (std::abs (valores[1] - 0.5f)   < 0.002f, "kTriangle vale 0,5 en p=1/8");
    check (valores[2] == 1.0f,                       "kSquare está en alto antes de p=0,5");
    check (std::abs (valores[3] + 0.75f)  < 0.002f, "kRampUp vale −0,75 en p=1/8");
    check (std::abs (valores[4] - 0.75f)  < 0.002f, "kRampDown vale +0,75 en p=1/8");

    bool distintas = true;
    for (int a = 0; a < 5 && distintas; ++a)
        for (int b = a + 1; b < 5 && distintas; ++b)
            if (std::abs (valores[a] - valores[b]) < 0.02f) distintas = false;
    check (distintas, "las cinco formas continuas NO son la misma onda");

    // ── Un ciclo completo de las 7: bipolares, con los extremos esperados ──
    bool todasDentro = true, extremosOk = true, cuadradaSola = true;
    for (int s = 0; s <= 6; ++s)
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setRate (1.0f);
        l.setShape (s);
        l.reset();

        float mn = 1.0f, mx = -1.0f;
        for (int i = 0; i < 44100; ++i)
        {
            const float v = l.nextSample();
            if (v < -1.001f || v > 1.001f) todasDentro = false;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }

        if (s == 0 || s == 1)
            if (mn > -0.99f || mx < 0.99f) extremosOk = false;
        if (s == 2)
            if (mn != -1.0f || mx != 1.0f) cuadradaSola = false;
    }
    check (todasDentro, "las 7 formas son bipolares: todas dentro de [−1,+1]");
    check (extremosOk, "seno y triángulo recorren el rango completo [−1,+1]");
    check (cuadradaSola, "la cuadrada solo toma los dos extremos ±1");

    // ── Sample & Hold sostiene el valor durante todo el ciclo ──
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setRate (1.0f);
        l.setShape (5);            // kSampleHold
        l.reset();
        const float primero = l.nextSample();
        bool sostiene = true;
        for (int i = 0; i < 20000; ++i)   // < 1 ciclo: sin wrap, sin nuevo sorteo
            if (l.nextSample() != primero) sostiene = false;
        check (sostiene, "Sample & Hold mantiene el valor mientras no llega el wrap de fase");
    }

    // ── Sample & Glide se mueve hacia el nuevo objetivo ──
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setRate (1.0f);
        l.setShape (6);            // kSampleGlide
        l.reset();
        float prev = l.nextSample();
        float maxDelta = 0.0f;
        for (int i = 1; i < 88200; ++i)   // 2 ciclos: al menos un wrap
        {
            const float v = l.nextSample();
            const float d = std::abs (v - prev);
            if (d > maxDelta) maxDelta = d;
            prev = v;
        }
        check (maxDelta > 1.0e-6f, "Sample & Glide converge hacia el nuevo objetivo");
    }

    // ── Slew: pendiente máxima 2/(tiempo·sr) por muestra ──
    {
        auto maxDelta = [] (float slew)
        {
            abd::synth::LfoAnalog l;
            l.setSampleRate (44100.0);
            l.setRate (2.0f);
            l.setShape (2);        // kSquare: el salto más brusco posible
            l.setSlew (slew);
            l.reset();
            float prev = l.nextSample();
            float maxD = 0.0f;
            for (int i = 1; i < 46000; ++i)
            {
                const float v = l.nextSample();
                const float d = std::abs (v - prev);
                if (d > maxD) maxD = d;
                prev = v;
            }
            return maxD;
        };

        check (maxDelta (0.0f) > 1.9f,
               "sin slew la cuadrada salta de 2 unidades de una muestra a otra");
        check (maxDelta (1.0f) <= 2.0f / (0.5f * 44100.0f) + 1.0e-6f,
               "slew 1 limita el salto a 2/(0,5 s · sr) por muestra");
    }

    // ── getUnipolar = bipolar · 0,5 + 0,5, exacto y dentro de [0,1] ──
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setRate (3.0f);
        l.setShape (1);            // kTriangle: recorre todo
        l.reset();
        bool exacto = true, dentro = true;
        for (int i = 0; i < 44100; ++i)
        {
            const float v = l.nextSample();
            const float u = l.getUnipolar();
            if (u != v * 0.5f + 0.5f) exacto = false;
            if (u < 0.0f || u > 1.0f) dentro = false;
        }
        check (exacto, "getUnipolar() es exactamente bipolar · 0,5 + 0,5");
        check (dentro, "y se queda siempre dentro de [0,1]");
    }
}

static void testLFOAnalogRateRange()
{
    printf("=== LfoAnalog: rango de rate 0,005 – 1280 Hz ===\n");

    // ── Techo: setRate(5000) se clampa a 1280 Hz ──
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setShape (0);
        l.setRate (5000.0f);       // clampeado a 1280
        l.reset();
        for (int i = 0; i < 10000; ++i) l.nextSample();

        const double fase = l.getPhase();
        const double esperada = std::fmod (10000.0 * (1280.0 / 44100.0), 1.0);
        check (std::abs (fase - esperada) < 1.0e-6,
               "setRate(5000) se clampa al techo de 1280 Hz (la fase lo delata)");
    }

    // ── Suelo: setRate(1e−6) se clampa a 0,005 Hz ──
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setShape (0);
        l.setRate (0.000001f);     // clampeado a 0,005
        l.reset();
        for (int i = 0; i < 441000; ++i) l.nextSample();   // 10 s de tiempo real

        const double fase = l.getPhase();
        // 10 s · 0,005 Hz = 0,05 de ciclo; sin clamp sería 1e−5 de ciclo.
        check (std::abs (fase - 0.05) < 1.0e-3,
               "setRate mínimo se clampa a 0,005 Hz (0,05 de ciclo en 10 s)");
    }

    // ── Audio-rate: a 1280 Hz el seno cruza el cero miles de veces por s ──
    {
        abd::synth::LfoAnalog l;
        l.setSampleRate (44100.0);
        l.setShape (0);
        l.setRate (1280.0f);
        l.reset();
        int cruces = 0;
        float prev = l.nextSample();
        for (int i = 1; i < 44100; ++i)
        {
            const float v = l.nextSample();
            // Los DOS sentidos: 1280 Hz son 1280 ciclos = 2560 cruces por
            // segundo contando bajadas y subidas.
            if ((prev > 0.0f && v <= 0.0f) || (prev < 0.0f && v >= 0.0f)) ++cruces;
            prev = v;
        }
        check (cruces > 2000, "a 1280 Hz el seno cruza el cero > 2000 veces por segundo");
    }
}

static void testLFOAnalogDelayFade()
{
    printf("=== LfoAnalog: fade-in del delay (40 %% silencio, 60 %% rampa) ===\n");

    // Cuadrada a 0,01 Hz: en los primeros 0,1 s la forma vale +1 siempre,
    // así que la salida ES la ganancia del fade y se puede leer directa.
    abd::synth::LfoAnalog l;
    l.setSampleRate (44100.0);
    l.setShape (2);                // kSquare
    l.setRate (0.01f);
    l.setDelay (0.1f);             // 4410 muestras: 1764 silencio + 2646 rampa
    l.reset();

    float v = 0.0f;
    bool silencio = true;
    for (int i = 0; i <= 1764; ++i)
    {
        v = l.nextSample();
        if (v != 0.0f) silencio = false;
    }
    check (silencio, "el primer 40 % de 0,1 s (1764 muestras) está en silencio");

    for (int i = 1765; i <= 3087; ++i) v = l.nextSample();
    check (std::abs (v - 0.5f) < 0.01f, "a mitad del fade la amplitud vale ~0,5");

    for (int i = 3088; i <= 4409; ++i) v = l.nextSample();
    check (v < 0.9999f, "aún dentro del fade no llega al máximo");

    v = l.nextSample();            // i = 4410: fin de la ventana
    check (v > 0.9999f, "transcurridos 0,1 s el fade termina y la onda sale a tope");

    // ── setDelayScale(2) dobla la ventana: 3000 < 3528 sigue mudo ──
    {
        abd::synth::LfoAnalog l2;
        l2.setSampleRate (44100.0);
        l2.setShape (2);
        l2.setRate (0.01f);
        l2.setDelay (0.1f);
        l2.setDelayScale (2.0f);
        l2.reset();
        float w = 1.0f;
        for (int i = 0; i <= 3000; ++i) w = l2.nextSample();
        check (w == 0.0f, "setDelayScale(2) dobla la ventana silenciosa (3000 < 3528)");
    }
}
