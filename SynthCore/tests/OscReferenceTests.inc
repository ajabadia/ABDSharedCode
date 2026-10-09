namespace oscTestDetail
{

/** Un miembro que le falta getPhase(): el rasgo tiene que decir que NO. */
struct HalfOscillatorWithWidth
{
    void prepare(double) noexcept {}
    void reset() noexcept {}
    void setFrequency(double) noexcept {}
    bool setWaveform(::abd::synth::OscWaveform) noexcept { return true; }
    bool setOversampling(int) noexcept { return true; }
    void setPulseWidth(double) noexcept {}
    double processSample() noexcept { return 0.0; }
    double getFrequency() const noexcept { return 0.0; }
    ::abd::synth::OscWaveform getWaveform() const noexcept { return ::abd::synth::OscWaveform::Sawtooth; }
    int getOversampling() const noexcept { return 1; }
    double getPulseWidth() const noexcept { return 0.5; }
    double getLatencySamples() const noexcept { return 0.0; }
    static bool supportsWaveform(::abd::synth::OscWaveform) noexcept { return true; }
    static bool supportsOversampling(int) noexcept { return true; }
};

} // namespace oscTestDetail

#include "../OscReference.h"
#include "../OscillatorFamily.h"

namespace abd::synth::tests
{

//==============================================================================
static void testOscReferenceContract()
{
    printf("=== OscReference: contract ===\n");

    // El miembro nuevo entra en el rasgo; el existente tambien.
    static_assert(IsOscillator<OscReference>::value, "el miembro de referencia tiene que ser de la familia");
    static_assert(IsOscillator<OscVcoCa72>::value, "el VCO de rampa sigue siendo de la familia");
    static_assert(IsOscillator<OscPolyBlep>::value, "el oscilador de fase sigue siendo de la familia");

    // Un miembro a medias NO entra, aunque tenga todo menos getPhase().
    static_assert(IsOscillator<::oscTestDetail::HalfOscillatorWithWidth>::value == false,
                  "un miembro sin getPhase() no es de la familia");
    static_assert(IsOscillator<int>::value == false,
                  "y tampoco lo es un tipo que no tiene nada que ver");
    static_assert(IsOscillator<::oscTestDetail::HalfOscillatorWithWidth>::value == false,
                  "un miembro sin getPhase() no es de la familia");

    check(IsOscillator<OscReference>::value == true, "the reference member is of the family");
    check(IsOscillator<OscReference>::value == true, "the reference member is of the family");
    check(IsOscillator<OscReference>::value == true, "the reference member is of the family");
    check((bool)(IsOscillator<OscReference>::value) && (bool)(IsOscillator<OscVcoCa72>::value) && (bool)(IsOscillator<OscPolyBlep>::value),
          "the new member joins the existing two");
    check(IsOscillator<::oscTestDetail::HalfOscillatorWithWidth>::value == false,
          "a member that lacks getPhase() does not pass, even with everything else");
    check(IsOscillator<int>::value == false,
          "and neither does an unrelated type");
    check(IsOscillator<::oscTestDetail::HalfOscillatorWithWidth>::value == false,
          "a member that lacks getPhase() does not pass, even with everything else");
    check((IsOscillator<int>::value) == false,
          "and neither does an unrelated type");
    check((IsOscillator<::oscTestDetail::HalfOscillatorWithWidth>::value) == false,
          "a member that lacks getPhase() does not pass, even with everything else");
    check((IsOscillator<int>::value) == false,
          "and neither does an unrelated tipo");

    // El miembro declara que es de fase.
    check(OscReference::kind == OscKind::Phase, "the reference member declares itself a phase piece");
    check(OscReference::kind == OscPolyBlep::kind,
          "the reference member and the existing phase member declare the same kind");
}

//==============================================================================
static void testOscReferenceLanguage()
{
    printf("=== OscReference: language ===\n");

    OscReference osc;

    osc.prepare(44100.0);

    // Formas: dice que sierra y seno las tiene, y que triangulo y rectangulo no.
    check(OscReference::supportsWaveform(OscWaveform::Sawtooth), "the reference member supports sawtooth");
    check(OscReference::supportsWaveform(OscWaveform::Sine), "the reference member supports sine");
    check(!OscReference::supportsWaveform(OscWaveform::Triangle),
          "the reference member says triangle is not ready yet");
    check(!OscReference::supportsWaveform(OscWaveform::Rectangle),
          "the reference member says rectangle is not ready yet");

    // Que rechazar un waveform no mueva nada es parte del contrato.
    const OscWaveform before = osc.getWaveform();
    check(!osc.setWaveform(OscWaveform::Triangle) && osc.getWaveform() == before,
          "asking for a missing waveform changes nothing");

    // Esas dos formas se activan sin mentir.
    check(osc.setWaveform(OscWaveform::Sawtooth), "the reference member accepts sawtooth");
    check(osc.getWaveform() == OscWaveform::Sawtooth, "and keeps it");
    check(osc.setWaveform(OscWaveform::Sine), "the reference member accepts sine");
    check(osc.getWaveform() == OscWaveform::Sine, "and keeps it");

    // Sobremuestreo: solo el 1.
    check(OscReference::supportsOversampling(1), "the reference member accepts 1x");
    check(!OscReference::supportsOversampling(2), "the reference member says no to 2x");
    check(!OscReference::supportsOversampling(8), "and to 8x");
    check(osc.setOversampling(1), "1x is accepted");
    check(!osc.setOversampling(4) && osc.getOversampling() == 1,
          "a rejected factor changes nothing");

    // Frecuencia: la familia pide Hz y el miembro devuelve lo que usa.
    osc.setFrequency(220.0);
    osc.reset();
    check(std::abs(osc.getFrequency() - 220.0) < 1.0e-9, "the asked 220 Hz is the one used");

    osc.setFrequency(50000.0);
    check(osc.getFrequency() <= oscMaxFrequencyHz * (1.0 + 1.0e-9),
          "the family floor/ceiling is respected");

    osc.setFrequency(440.0);

    // Ancho: el idioma del ancho esta en su lugar aunque el miembro aun no lo use.
    osc.setPulseWidth(0.25);
    check(osc.getPulseWidth() == 0.25, "the pulse width is stored in the family's language");

    osc.setPulseWidth(0.0);
    check(osc.getPulseWidth() == oscMinPulseWidth, "a zero width clamps to the family floor");

    osc.setPulseWidth(1.0);
    check(osc.getPulseWidth() == oscMaxPulseWidth, "a full width clamps to the family ceiling");

    osc.setPulseWidth(0.5);

    // Salida nominal: bipolar y dentro de +-oscNominalPeak.
    bool bounded = true;
    for (int i = 0; i < 44100; ++i)
    {
        const double v = osc.processSample();

        if (!std::isfinite(v) || std::abs(v) > oscNominalPeak * 1.05)
            bounded = false;
    }

    check(bounded, "sine and sawtooth stay finite inside the family's nominal +-1");
}

//==============================================================================
static void testOscReferenceResetAndProcessBlock()
{
    printf("=== OscReference: reset & block equivalence ===\n");

    // Reset idempotencia: dos instancias desde reset dan lo mismo.
    {
        OscReference one;
        OscReference two;

        one.prepare(48000.0);
        two.prepare(48000.0);
        one.setFrequency(311.0);
        two.setFrequency(311.0);
        one.setWaveform(OscWaveform::Sine);
        two.setWaveform(OscWaveform::Sine);
        one.reset();
        two.reset();

        bool identical = true;

        for (int i = 0; i < 256; ++i)
            if (one.processSample() != two.processSample())
                identical = false;

        check(identical, "two identical pieces from reset give bit-identical samples");
    }

    // El bloque de la familia, la misma frase para float y para double.
    {
        OscReference a;
        OscReference b;

        a.prepare(44100.0);
        b.prepare(44100.0);
        a.setFrequency(330.0);
        b.setFrequency(330.0);
        a.setWaveform(OscWaveform::Sine);
        b.setWaveform(OscWaveform::Sine);
        a.reset();
        b.reset();

        float floats[256];
        double doubles[256];

        processBlock(a, floats, 256);
        processBlock(b, doubles, 256);

        bool same = true;

        for (int i = 0; i < 256; ++i)
            if (floats[i] != static_cast<float>(doubles[i]))
                same = false;

        check(same, "processBlock is the same phrase for a float host and a double engine");

        processBlock(a, static_cast<float*>(nullptr), 8);
        check(true, "processBlock with no buffer is a no-op, not a crash");
    }

    // Un incremento que no se puede dibujar no produce NaN.
    {
        OscReference broken;
        broken.prepare(0.0);
        broken.setFrequency(0.0);

        bool finite = true;

        for (int i = 0; i < 64; ++i)
            if (!std::isfinite(broken.processSample()))
                finite = false;

        check(finite, "a nonsense sample rate gives silence, not NaN");
    }
}

} // namespace abd::synth::tests
