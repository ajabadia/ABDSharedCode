// ─────────────────────────── EnvelopeAnalog ───────────────────────────
//
// La envolvente ADSR analógica compartida (abd::synth::EnvelopeAnalog) — NO
// confundir con ADSREnvelope.h (la clásica, ya cubierta por testEnvelope()).
// Esta es la de DeepMind: 4 fases con Stage explícito, curvatura ajustable
// por etapa en [−1,+1], modulación de curva por matriz, sustain con offset,
// loopMode y bypassSustain. Lo que se fija aquí:
//
//  - El ciclo de fases kIdle → kAttack → kDecay → kSustain → kRelease →
//    kIdle con sus duraciones (~10 ms por fase a 44,1 kHz con 0,01 s), el
//    ataque monótono y el sustain clavado en el nivel pedido.
//  - Las 4 transiciones con sus políticas: por defecto decay → sustain;
//    loopMode re-ataca tras el decay; bypassSustain salta al release.
//  - setTimeScale: la implementación divide el incremento por el escala, así
//    que escala 0,5 ACORTA la fase (la cabecera dice lo contrario para >1;
//    aquí se fija el comportamiento REAL para que una divergencia salte).
//  - La curvatura: en el punto medio del ataque, curva −1 sube rápido
//    (≈p^0,25), curva 0 es lineal y curva +1 sube lento (≈p^4); setCurves
//    clampea a [−1,+1] y la modulación de matriz se SUMA a la curva base.
//  - El curve de sustain deriva como máximo ±10 % del nivel sostenido, y
//    sustainEfectivo() aplica el offset clampeado a [0,1].
//
// Este .inc se incluye DENTRO de `namespace abd::synth::tests` (lo abre
// SynthCoreTests.cpp): NO abre namespace propio ni incluye cabeceras de std.
// EnvelopeAnalog.h se incluye arriba, FUERA del namespace.

using EnvStage = abd::synth::EnvelopeAnalog::Stage;

// Dispara la envolvente con los parámetros estándar y devuelve la etapa
// tras k muestras. 0,01 s por fase a 44,1 kHz = ~441 muestras por fase.
static EnvStage etapaTras(bool loop, bool bypass, float timeScale, int k)
{
    abd::synth::EnvelopeAnalog e;
    e.setSampleRate(44100.0);
    e.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
    e.setCurves(0.0f, 0.0f, 0.0f, 0.0f);
    e.setLoopMode(loop);
    e.setBypassSustain(bypass);
    if (timeScale != 1.0f) e.setTimeScale(timeScale);
    e.trigger();
    for (int i = 0; i < k; ++i) e.nextSample();
    return e.getCurrentStage();
}

// Nivel del ataque tras k muestras con la curva de ataque dada.
static float nivelAtaqueConCurva(float curva, int k)
{
    abd::synth::EnvelopeAnalog e;
    e.setSampleRate(44100.0);
    e.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
    e.setCurves(curva, 0.0f, 0.0f, 0.0f);
    e.trigger();
    float v = 0.0f;
    for (int i = 0; i < k; ++i) v = e.nextSample();
    return v;
}

static void testEnvelopeAnalogPhases()
{
    printf("=== EnvelopeAnalog: las 4 fases ADSR ===\n");

    {
        abd::synth::EnvelopeAnalog e;
        e.setSampleRate(44100.0);
        e.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        e.setCurves(0.0f, 0.0f, 0.0f, 0.0f);

        check(!e.isActive() && e.getCurrentStage() == EnvStage::kIdle,
              "arranca en reposo (kIdle)");
        check(e.nextSample() == 0.0f, "en reposo devuelve 0");

        e.trigger();
        check(e.getCurrentStage() == EnvStage::kAttack, "trigger() entra en ataque");

        bool mono = true, rango = true;
        float prev = -1.0f;
        for (int i = 0; i < 500; ++i)
        {
            const float v = e.nextSample();
            if (v < prev) mono = false;
            if (v < -0.001f || v > 1.001f) rango = false;
            prev = v;
            // Solo se mide el ATAQUE: en cuanto la etapa cambia a decay la
            // curva ya baja a propósito, y eso no es no-monotonicidad.
            if (e.getCurrentStage() != EnvStage::kAttack) break;
        }
        check(mono, "el ataque es monótono creciente");
        check(rango, "y se queda dentro de [0,1]");
        check(e.getCurrentStage() == EnvStage::kDecay,
              "tras ~10 ms el ataque termina y pasa a decay");

        for (int i = 0; i < 500; ++i) e.nextSample();
        check(e.getCurrentStage() == EnvStage::kSustain,
              "tras otros ~10 ms entra en sustain");
        check(std::abs(e.getCurrentLevel() - 0.5f) < 0.001f,
              "el sustain se clava en 0,5 (curva plana, sin offset)");

        e.release();
        check(e.getCurrentStage() == EnvStage::kRelease, "release() entra en release");
        for (int i = 0; i < 500; ++i) e.nextSample();
        check(!e.isActive() && e.getCurrentStage() == EnvStage::kIdle,
              "acabado el release vuelve a kIdle");
        check(e.nextSample() == 0.0f, "tras el release la salida es 0");
    }

    // ── Las tres transiciones con política ──
    check(etapaTras(false, false, 1.0f, 950) == EnvStage::kSustain,
          "por defecto: decay → sustain");
    check(etapaTras(true, false, 1.0f, 950) == EnvStage::kAttack,
          "loopMode: tras el decay re-ataca (sin pasar por sustain)");
    check(etapaTras(false, true, 1.0f, 950) == EnvStage::kRelease,
          "bypassSustain: salta directo al release (one-shot)");

    // ── setTimeScale: escala 0,5 acorta la fase (comportamiento real) ──
    check(etapaTras(false, false, 1.0f, 300) == EnvStage::kAttack,
          "a escala normal, a las 300 muestras aún está atacando");
    check(etapaTras(false, false, 0.5f, 300) == EnvStage::kDecay,
          "timeScale 0,5 duplica la velocidad: en 300 muestras ya hace decay");
}

static void testEnvelopeAnalogCurves()
{
    printf("=== EnvelopeAnalog: curvatura [-1,+1] ===\n");

    // ── Curvatura del ataque en el punto medio (p ≈ 0,499 en la muestra 220) ──
    const float mNeg  = nivelAtaqueConCurva(-1.0f, 220); // exponente 0,25: sube rápido
    const float mCero = nivelAtaqueConCurva(0.0f, 220);  // lineal
    const float mPos  = nivelAtaqueConCurva(1.0f, 220);  // exponente 4: sube lento

    check(mNeg > 0.7f, "curva −1: subida rápida (≈ p^0,25) en el punto medio");
    check(mCero > 0.45f && mCero < 0.55f, "curva 0: lineal en el punto medio");
    check(mPos < 0.15f, "curva +1: subida lenta (≈ p^4) en el punto medio");
    check(mNeg > mCero && mCero > mPos,
          "las tres curvas están ordenadas y son distintas (curvatura no lineal)");

    // ── setCurves clampea a [−1,+1]: (99,−99,50,−50) se comporta como (1,−1,1,−1) ──
    {
        abd::synth::EnvelopeAnalog a, b;
        a.setSampleRate(44100.0);
        a.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        a.setCurves(99.0f, -99.0f, 50.0f, -50.0f);
        a.trigger();
        b.setSampleRate(44100.0);
        b.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        b.setCurves(1.0f, -1.0f, 1.0f, -1.0f);
        b.trigger();

        bool identico = true;
        for (int i = 0; i < 1500; ++i)
            if (a.nextSample() != b.nextSample()) identico = false;
        check(identico, "setCurves clampea a [−1,+1]: fuera de rango = tope");
    }

    // ── La modulación de matriz se SUMA a la curva base (y también clampea) ──
    {
        abd::synth::EnvelopeAnalog a, b;
        a.setSampleRate(44100.0);
        a.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        a.setCurves(0.0f, 0.0f, 0.0f, 0.0f);
        a.setCurveModulation(EnvStage::kAttack, 99.0f); // clampeado a +1
        a.trigger();
        b.setSampleRate(44100.0);
        b.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        b.setCurves(1.0f, 0.0f, 0.0f, 0.0f); // curva base +1
        b.trigger();

        bool identico = true;
        for (int i = 0; i < 1500; ++i)
            if (a.nextSample() != b.nextSample()) identico = false;
        check(identico, "la modulación de curva se suma a la base y clampea igual");
    }

    // ── Sustain: la curva +1 deriva como máximo el 10 % del nivel ──
    {
        abd::synth::EnvelopeAnalog curva, plana;
        curva.setSampleRate(44100.0);
        curva.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        curva.setCurves(0.0f, 0.0f, 1.0f, 0.0f);
        curva.trigger();
        plana.setSampleRate(44100.0);
        plana.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        plana.setCurves(0.0f, 0.0f, 0.0f, 0.0f);
        plana.trigger();

        float vCurva = 0.0f, vPlana = 0.0f;
        for (int i = 0; i < 23000; ++i)
        {
            vCurva = curva.nextSample();
            vPlana = plana.nextSample();
        }

        check(std::abs(vPlana - 0.5f) < 1.0e-6f,
              "sustain con curva 0 se queda clavado en 0,5");
        check(vCurva > 0.505f && vCurva < 0.53f,
              "sustain con curva +1 sube pero dentro del tope del 10 % (≈ 0,517)");
    }

    // ── sustainEfectivo: offset sumado y clampeado a [0,1] ──
    {
        abd::synth::EnvelopeAnalog e;
        e.setParameters(0.01f, 0.01f, 0.5f, 0.01f);
        e.setSustainOffset(0.5f);
        check(e.sustainEfectivo() == 1.0f, "sustain 0,5 + offset 0,5 = 1,0");
        e.setSustainOffset(0.6f);
        check(e.sustainEfectivo() == 1.0f, "el offset se clampea: no pasa de 1,0");
        e.setSustainOffset(-0.8f);
        check(e.sustainEfectivo() == 0.0f, "ni baja de 0,0");
    }
}
