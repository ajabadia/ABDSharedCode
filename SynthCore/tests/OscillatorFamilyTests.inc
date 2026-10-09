// ─────────────────────────── La familia de osciladores ───────────────────────────
//
// Lo que prueba este bloque, y por que:
//
//  - El CONTRATO. Que los dos miembros sean de la familia lo dice el compilador
//    (IsOscillator), no un comentario; y que el rasgo muerda se prueba con un
//    oscilador a medias, que tiene que salir del rasgo por lo que le falta.
//  - El IDIOMA. Una frecuencia pedida tiene que salir como esa frecuencia, en los
//    dos miembros y con el sobremuestreo que sea; y un ancho de pulso de 0,5 tiene
//    que ser cuadrada en los dos. Si esto fallara, cambiar de oscilador cambiaria
//    la afinacion o el nivel, que es justo lo que la familia existe para evitar.
//  - La FIDELIDAD DEL APARATO, medida en lo que se puede medir sin el banco de
//    pruebas: el pliegue del triangulo donde la curva medida lo pone, los dos
//    niveles del rectangulo, el diente de sierra asimetrico, el periodo clavado
//    por la ley del nucleo, y que el perfil sea de verdad el hardware (cambiar la
//    capacidad y ver mover el periodo).
//  - El LIMITADO DE BANDA: el diezmador tapa lo que tiene que tapar y deja pasar
//    lo que tiene que dejar, y el VCO a 8x no tiene mas basura que a 1x (medido
//    con una DFT de un solo bin en una frecuencia que NO es armonico).
//
// El periodo se mide con la fase del propio oscilador y con el cruce INTERPOLADO
// (no contando muestras): una muestra de error sobre cien ciclos seria un 1% de
// error de medida, que es mas que la tolerancia que se quiere comprobar.

namespace oscTestDetail
{

/** Un oscilador a medias: le falta getPhase(). El rasgo tiene que decir que NO,
    que es la mitad util de "el contrato lo comprueba el compilador". */
struct HalfOscillator
{
    void prepare(double) noexcept {}
    void reset() noexcept {}
    void setFrequency(double) noexcept {}
    bool setWaveform(OscWaveform) noexcept { return true; }
    bool setOversampling(int) noexcept { return true; }
    void setPulseWidth(double) noexcept {}
    double processSample() noexcept { return 0.0; }
    double getFrequency() const noexcept { return 0.0; }
    OscWaveform getWaveform() const noexcept { return OscWaveform::Sawtooth; }
    int getOversampling() const noexcept { return 1; }
    double getPulseWidth() const noexcept { return 0.5; }
    double getLatencySamples() const noexcept { return 0.0; }
    static bool supportsWaveform(OscWaveform) noexcept { return true; }
    static bool supportsOversampling(int) noexcept { return true; }
};

/** Nada que ver con un oscilador. */
struct NotAnOscillator
{
};

} // namespace oscTestDetail

/** El periodo medio del oscilador, en muestras, con el cruce de cada vuelta
    interpolado dentro de la muestra. Necesita al menos dos vueltas; con menos,
    devuelve la estimacion gruesa por fase acumulada. */
template <typename Oscillator>
static double oscMeasurePeriodSamples(Oscillator& osc, int numSamples)
{
    double cycles   = 0.0;
    double previous = osc.getPhase();
    double firstAt  = -1.0;
    double lastAt   = -1.0;
    int wraps       = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        osc.processSample();

        const double phase = osc.getPhase();
        const double delta = phase - previous;

        if (delta > 0.0)
        {
            cycles += delta;
        }
        else if (delta < 0.0)
        {
            // La vuelta ha caido dentro de esta muestra: la fraccion se saca de lo
            // que le quedaba a la fase anterior para llegar a 1.
            const double advance  = (1.0 - previous) + phase;
            const double fraction = advance > 0.0 ? (1.0 - previous) / advance : 0.0;
            const double at       = static_cast<double>(i - 1) + fraction;

            if (firstAt < 0.0)
                firstAt = at;

            lastAt = at;
            ++wraps;
        }

        previous = phase;
    }

    if (wraps >= 2 && lastAt > firstAt)
        return (lastAt - firstAt) / static_cast<double>(wraps - 1);

    return cycles > 0.0 ? static_cast<double>(numSamples) / cycles : 0.0;
}

/** La magnitud de una frecuencia en un buffer, con ventana de Hann (una
    rectangular deja pasar la fuga de los armonicos vecinos, que es mas que la
    basura que se quiere medir). */
static double oscToneMagnitude(const double* samples, int count, double frequency, double sampleRate)
{
    const double twoPi = 6.283185307179586477;
    double real        = 0.0;
    double imag        = 0.0;
    double window      = 0.0;

    for (int i = 0; i < count; ++i)
    {
        const double w     = 0.5 - 0.5 * std::cos(twoPi * static_cast<double>(i) / static_cast<double>(count - 1));
        const double angle = twoPi * frequency * static_cast<double>(i) / sampleRate;

        real += w * samples[i] * std::cos(angle);
        imag -= w * samples[i] * std::sin(angle);
        window += w;
    }

    if (window <= 0.0)
        return 0.0;

    return 2.0 * std::sqrt(real * real + imag * imag) / window;
}

//==============================================================================
static void testOscillatorFamilyContract()
{
    printf("=== Oscillator family: contract & language ===\n");

    // El contrato, comprobado al COMPILAR. Es la mitad del valor de la familia: un
    // miembro nuevo no se queda a medias sin que nadie se entere.
    static_assert(IsOscillator<OscVcoCa72>::value, "el VCO de rampa tiene que ser de la familia");
    static_assert(IsOscillator<OscPolyBlep>::value, "el oscilador de fase tiene que ser de la familia");
    static_assert(!IsOscillator<oscTestDetail::NotAnOscillator>::value, "el rasgo no puede decir que si a cualquier cosa");
    static_assert(!IsOscillator<oscTestDetail::HalfOscillator>::value, "un oscilador sin getPhase() no es de la familia");

    check(IsOscillator<OscVcoCa72>::value && IsOscillator<OscPolyBlep>::value, "both members are of the family");
    check(!IsOscillator<oscTestDetail::HalfOscillator>::value, "a half-done member is not (in the trait or in the family)");
    check(!IsOscillator<int>::value, "and neither is a type that has nothing to do with it");

    check(OscVcoCa72::kind == OscKind::Circuit, "the ramp VCO declares itself a circuit piece");
    check(OscPolyBlep::kind == OscKind::Phase, "the phase oscillator declares itself a phase piece");

    // El idioma: bits de forma, curvas y recortes.
    check((oscWaveformBit(OscWaveform::Sawtooth) | oscWaveformBit(OscWaveform::Rectangle)) == 5u,
          "the waveform bits are the enum's own order");
    check(oscNominalPeak == 1.0, "the family's nominal peak is 1");

    check(std::abs(oscFrequencyHzFromNote(69.0) - 440.0) < 1.0e-9, "note 69 is 440 Hz");
    check(std::abs(oscFrequencyHzFromNote(81.0) - 880.0) < 1.0e-6, "note 81 is 880 Hz");
    check(std::abs(oscNoteFromFrequencyHz(440.0) - 69.0) < 1.0e-9, "440 Hz is note 69");
    // La curva de nota pasa por exp2/log2 en float (es la que ya tenia el modulo):
    // la vuelta cierra a una milesima de cent, que es la precision de la cuenta.
    check(std::abs(oscNoteFromFrequencyHz(oscFrequencyHzFromNote(61.0)) - 61.0) < 1.0e-4, "note -> Hz -> note closes");

    check(oscClampFrequencyHz(-5.0) == oscMinFrequencyHz, "a negative frequency clamps to the family floor");
    check(oscClampFrequencyHz(1.0e6) == oscMaxFrequencyHz, "a silly frequency clamps to the family ceiling");
    check(oscClampFrequencyHz(std::numeric_limits<double>::quiet_NaN()) == oscMinFrequencyHz,
          "a NaN frequency clamps instead of propagating");

    check(oscClampPulseWidth(0.0) == oscMinPulseWidth, "a zero width clamps to the family floor");
    check(oscClampPulseWidth(0.5) == 0.5, "half width is square, untouched");
    check(oscClampPulseWidth(1.0) == oscMaxPulseWidth, "a full width clamps to the family ceiling");
}

//==============================================================================
static void testOscHalfbandDecimator()
{
    printf("=== OscHalfbandDecimator ===\n");

    check(OscHalfbandDecimator::stagesForFactor(1) == 0 && OscHalfbandDecimator::stagesForFactor(2) == 1 && OscHalfbandDecimator::stagesForFactor(4) == 2 && OscHalfbandDecimator::stagesForFactor(8) == 3,
          "a halfband stage per octave: 1/2/4/8 -> 0/1/2/3");
    check(OscHalfbandDecimator::legalFactor(3) == 4 && OscHalfbandDecimator::legalFactor(0) == 1 && OscHalfbandDecimator::legalFactor(9) == 8,
          "an illegal factor rounds to a legal one instead of silently changing the band");

    {
        OscHalfbandDecimator through(1);
        bool passThrough = true;

        for (int i = 0; i < 64; ++i)
        {
            const double in = 0.01 * static_cast<double>(i);
            double out      = -1.0;

            if (!through.process(in, out) || out != in)
                passThrough = false;
        }

        check(passThrough, "factor 1 is a sample-for-sample pass-through");
        check(through.latencySamples() == 0.0, "factor 1 has no latency");
    }

    {
        OscHalfbandDecimator two(2);
        OscHalfbandDecimator four(4);
        OscHalfbandDecimator eight(8);

        check(two.factor() == 2 && four.factor() == 4 && eight.factor() == 8, "the factor is the one asked");
        check(eight.latencySamples() > four.latencySamples() && four.latencySamples() > two.latencySamples() && two.latencySamples() > 0.0,
              "the group delay grows with the factor and is declared");
    }

    {
        // Ganancia unidad en continua: un valor constante tiene que salir igual.
        OscHalfbandDecimator eight(8);
        double last = 0.0;
        int outputs = 0;

        for (int i = 0; i < 4000; ++i)
        {
            double out = 0.0;

            if (eight.process(0.25, out))
            {
                last = out;
                ++outputs;
            }
        }

        check(outputs == 500, "factor 8 gives one output sample every eight pushes");
        check(std::abs(last - 0.25) < 1.0e-12, "a constant comes out as the same constant");
    }

    {
        // Lo que tiene que DEJAR pasar (0,1 de la entrada, dentro de la banda) y lo
        // que tiene que TAPAR (0,4 de la entrada, por encima del nuevo Nyquist).
        const double inputRate = 88200.0;
        OscHalfbandDecimator dec(2);

        auto peakOf = [inputRate](OscHalfbandDecimator& d, double tone) {
            double peak = 0.0;
            int outputs = 0;

            for (int i = 0; i < 6000; ++i)
            {
                const double in = std::sin(6.283185307179586477 * tone * static_cast<double>(i) / inputRate);
                double out      = 0.0;

                if (d.process(in, out))
                {
                    ++outputs;
                    if (outputs > 200)
                        peak = std::max(peak, std::abs(out));
                }
            }

            return peak;
        };

        // (a 0,025 de la entrada la senal de SALIDA tiene veinte muestras por
        // ciclo y el pico medido es el de la senal, no el de la rejilla)
        const double passed = peakOf(dec, 0.025 * inputRate);

        dec.reset();
        const double stopped = peakOf(dec, 0.40 * inputRate);

        check(passed > 0.98, "a tone inside the surviving band comes out whole");
        check(stopped < 0.05, "a tone above the new Nyquist is filtered, not folded back in");
        printf("        (deja pasar %.4f, tapa %.6f)\n", passed, stopped);
    }
}

//==============================================================================
static void testOscVcoCa72Frequency()
{
    printf("=== OscVcoCa72: frequency ===\n");

    OscVcoCa72 osc;
    osc.prepare(44100.0);
    osc.setWaveform(OscWaveform::Sawtooth);

    const double wanted[] = {55.0, 110.0, 440.0, 1000.0, 2000.0, 4000.0};
    double worst          = 0.0;
    bool allGood          = true;

    for (double hz : wanted)
    {
        osc.reset();
        osc.setFrequency(hz);

        const double period   = oscMeasurePeriodSamples(osc, 88200);
        const double measured = 44100.0 / period;
        const double error    = std::abs(measured - hz) / hz;

        worst = std::max(worst, error);

        // La frecuencia que declara la pieza es la pedida (o la resolucion de la
        // biseccion, que es una parte en mil millones).
        if (error > 0.002 || std::abs(osc.getFrequency() - hz) > hz * 1.0e-9)
            allGood = false;
    }

    check(allGood, "the ramp's measured frequency is the one asked, from 55 Hz to 4 kHz");
    printf("        (peor error medido: %.4f%%)\n", 100.0 * worst);

    // La ley del aparato es la corriente, no la fase: el DOBLE de frecuencia sigue
    // siendo la MITAD de periodo, que es lo que hace que la afinacion sea del
    // aparato y no de una recta puesta a mano.
    osc.reset();
    osc.setFrequency(220.0);
    const double p220 = oscMeasurePeriodSamples(osc, 88200);

    osc.reset();
    osc.setFrequency(440.0);
    const double p440 = oscMeasurePeriodSamples(osc, 88200);

    check(std::abs(p220 / p440 - 2.0) < 2.0e-3, "an octave is exactly double the period");

    // Lo que la pieza da de verdad cuando le piden mas de lo que tiene, y el
    // recorrido que declara: getFrequency() no puede mentir.
    osc.setFrequency(50000.0);
    const double ceiling = std::min(oscMaxFrequencyHz, osc.maxFrequencyHz());
    check(osc.getFrequency() == ceiling, "above its ceiling the piece reports the ceiling, not the ask");

    osc.setFrequency(-3.0);
    check(osc.getFrequency() == oscMinFrequencyHz, "below the floor it reports the floor");

    osc.setFrequency(4000.0);
    check(osc.maxFrequencyHz() > 4000.0, "the piece reaches past the top of the instrument's measured range");

    // El sobremuestreo NO puede mover la afinacion: es la misma rampa, solo que
    // mirada (y diezmada) mas veces.
    double periods[4]    = {0.0, 0.0, 0.0, 0.0};
    const int factors[4] = {1, 2, 4, 8};

    for (int i = 0; i < 4; ++i)
    {
        osc.reset();
        check(osc.setOversampling(factors[i]), "the VCO accepts 1x, 2x, 4x and 8x");
        osc.setFrequency(440.0);
        periods[i] = oscMeasurePeriodSamples(osc, 44100);
    }

    bool sameFrequency = true;

    for (int i = 1; i < 4; ++i)
        if (std::abs(periods[i] - periods[0]) / periods[0] > 2.0e-3)
            sameFrequency = false;

    check(sameFrequency, "oversampling does not move the frequency");

    // La puerta del circuito: la corriente de temporizacion.
    osc.setOversampling(1);
    osc.reset();
    osc.setTimingCurrent(1.0e-4);
    const double byCurrent = oscMeasurePeriodSamples(osc, 88200);
    check(std::abs(osc.getFrequency() - 44100.0 / byCurrent) / (44100.0 / byCurrent) < 1.0e-3,
          "getFrequency() reports the period the timing current gives");

    osc.reset();
    osc.setTimingCurrent(2.0e-4);
    const double twiceCurrent = oscMeasurePeriodSamples(osc, 88200);
    check(std::abs(byCurrent / twiceCurrent - 2.0) < 0.03,
          "twice the timing current is half the ramp (the delays' absolute seconds are the rest)");

    osc.setTimingCurrent(1.0e9);
    check(osc.getFrequency() <= oscMaxFrequencyHz * (1.0 + 1.0e-9),
          "the circuit's door is also clamped to the family's range");
}

//==============================================================================
static void testOscVcoCa72Waveforms()
{
    printf("=== OscVcoCa72: waveforms ===\n");

    OscVcoCa72 osc;
    osc.prepare(44100.0);

    check(OscVcoCa72::supportsWaveform(OscWaveform::Sawtooth) && OscVcoCa72::supportsWaveform(OscWaveform::Triangle) && OscVcoCa72::supportsWaveform(OscWaveform::Rectangle),
          "the ramp core gives sawtooth, triangle and rectangle");
    check(!OscVcoCa72::supportsWaveform(OscWaveform::Sine), "and it does NOT have a sine, and says so");

    const OscWaveform before = osc.getWaveform();
    check(!osc.setWaveform(OscWaveform::Sine) && osc.getWaveform() == before,
          "asking for the sine changes nothing instead of pretending");

    // Los tres conformadores, medidos: recorrido y forma.
    struct Shape
    {
        OscWaveform waveform;
        const char* name;
        double minOut;
        double maxOut;
    };

    const Shape shapes[3] = {
        {OscWaveform::Sawtooth, "sawtooth", -1.10, 1.10},
        {OscWaveform::Triangle, "triangle", -1.10, 1.10},
        {OscWaveform::Rectangle, "rectangle", -1.10, 1.10}};

    for (const auto& shape : shapes)
    {
        osc.reset();
        osc.setFrequency(110.0);
        osc.setWaveform(shape.waveform);

        double low  = 1.0e9;
        double high = -1.0e9;

        for (int i = 0; i < 44100; ++i)
        {
            const double v = osc.processSample();
            low            = std::min(low, v);
            high           = std::max(high, v);
        }

        char message[128];
        snprintf(message, sizeof(message), "the %s stays inside the family's +-1 (peaks %.3f / %.3f)",
                 shape.name, low, high);
        check(low > shape.minOut && high < shape.maxOut, message);

        // Cada forma tiene que usar el recorrido, no quedarse a medio camino.
        snprintf(message, sizeof(message), "the %s reaches both sides (%.3f / %.3f)", shape.name, low, high);
        check(low < -0.85 && high > 0.75, message);
    }

    // El diente de sierra del aparato es ASIMETRICO (no sube lo que baja), y el
    // triangulo tiene su pliegue donde la curva MEDIDA lo pone. Son las dos cosas
    // que distinguen esta pieza de un oscilador generico, asi que se miden.
    osc.reset();
    osc.setFrequency(110.0);
    osc.setWaveform(OscWaveform::Triangle);

    double trough      = 1.0e9;
    double troughPhase = 0.0;

    for (int i = 0; i < 88200; ++i)
    {
        const double v = osc.processSample();

        if (v < trough)
        {
            trough      = v;
            troughPhase = osc.getPhase();
        }
    }

    check(troughPhase > 0.42 && troughPhase < 0.58,
          "the triangle's fold falls where the measured transfer puts it (about 49% of the cycle)");

    // Y la tabla es INYECTABLE: con otra transferencia, el pliegue se va donde
    // diga la tabla. Es lo que permite meter la curva completa medida sin tocar
    // el motor.
    {
        // El barrido del separador va de 3,936 a -4,169 V; el pliegue se pone en la
        // fase 0,86 (rampa -3,413 V, separador -3,035 V).
        static const OscVcoCa72TrianglePoint moved[3] = {
            {-4.169214, 1.0},
            {-3.035000, -1.0},
            {3.935784, 1.0}};

        osc.setTriangleTable(moved, 3);
        osc.reset();
        osc.setFrequency(110.0);

        double movedTrough = 1.0e9;
        double movedPhase  = 0.0;

        for (int i = 0; i < 44100; ++i)
        {
            const double v = osc.processSample();

            if (v < movedTrough)
            {
                movedTrough = v;
                movedPhase  = osc.getPhase();
            }
        }

        check(movedPhase > 0.80 && movedPhase < 0.92, "an injected measured table moves the fold where it says");

        osc.setTriangleTable(nullptr, 0);
        osc.reset();
        osc.setFrequency(110.0);

        double backTrough = 1.0e9;
        double backPhase  = 0.0;

        for (int i = 0; i < 44100; ++i)
        {
            const double v = osc.processSample();

            if (v < backTrough)
            {
                backTrough = v;
                backPhase  = osc.getPhase();
            }
        }

        check(backPhase > 0.42 && backPhase < 0.58, "and the profile's table comes back when the injection is lifted");
    }

    // El rectangulo y su ancho: la familia fija que 0,5 sea cuadrada; el VCO baja
    // hasta donde su comparador sube al reinicio, y lo declara.
    osc.reset();
    osc.setWaveform(OscWaveform::Rectangle);
    osc.setFrequency(110.0);

    check(osc.minPulseWidth() > oscMinPulseWidth, "the comparator's narrowest is above the family's floor");
    check(osc.maxPulseWidth() == oscMaxPulseWidth, "and its widest is the family's");

    osc.setPulseWidth(0.001);
    check(osc.getPulseWidth() == osc.minPulseWidth(), "a width narrower than the comparator clamps to its own floor, not below it");

    const double duties[2] = {0.2, 0.5};

    for (double duty : duties)
    {
        osc.setPulseWidth(duty);
        osc.reset();

        int high  = 0;
        int total = 0;

        for (int i = 0; i < 44100; ++i)
        {
            const double v = osc.processSample();

            if (i > 200)
            {
                if (v > 0.0)
                    ++high;

                ++total;
            }
        }

        const double measured = static_cast<double>(high) / static_cast<double>(total);

        char message[128];
        snprintf(message, sizeof(message), "a width of %.2f comes out as %.3f of the cycle", duty, measured);
        check(std::abs(measured - duty) < 0.01, message);
    }

    // El rectangulo tiene DOS niveles, no una rampa: la mayoria de las muestras
    // esta pegada a uno de los dos.
    osc.setPulseWidth(0.5);
    osc.reset();

    int atLevels = 0;

    for (int i = 0; i < 44100; ++i)
    {
        const double v = osc.processSample();

        if (std::abs(v) > 0.9)
            ++atLevels;
    }

    check(atLevels > 40000, "the rectangle is a comparator: two levels, not a slope");
}

//==============================================================================
static void testOscVcoCa72BandLimiting()
{
    printf("=== OscVcoCa72: band limiting ===\n");

    // Un diente de sierra de 7 kHz tiene su cuarto armonico en 28 kHz, que sin
    // limitar se pliega a 16,1 kHz — que no es armonico de 7 kHz, o sea basura.
    // Se mide ese bin con 1x y con 8x: es la unica manera de decir "el
    // sobremuestreo sirve" sin creerse una grafica.
    static double buffer[8192];

    const double fundamental = 7000.0;
    const double alias       = 44100.0 - 4.0 * fundamental; // 16,1 kHz
    const int factors[2]     = {1, 8};
    double magnitudes[2]     = {0.0, 0.0};

    for (int k = 0; k < 2; ++k)
    {
        OscVcoCa72 osc;
        osc.prepare(44100.0);
        osc.setWaveform(OscWaveform::Sawtooth);
        osc.setOversampling(factors[k]);
        osc.setFrequency(fundamental);
        osc.reset();

        // Los primeros cientos de muestras llenan el diezmador: no son del render.
        for (int i = 0; i < 256; ++i)
            osc.processSample();

        for (int i = 0; i < 8192; ++i)
            buffer[i] = osc.processSample();

        magnitudes[k] = oscToneMagnitude(buffer, 8192, alias, 44100.0);
    }

    printf("        (basura en %.0f Hz: 1x %.3e, 8x %.3e)\n", alias, magnitudes[0], magnitudes[1]);

    check(magnitudes[1] <= magnitudes[0] * 1.05, "8x is not worse than 1x where the aliasing shows");
    check(magnitudes[1] < 0.02, "and at 8x that bin is far below the fundamental (it is not a harmonic)");
}

//==============================================================================
static void testOscPolyBlep()
{
    printf("=== OscPolyBlep ===\n");

    OscPolyBlep osc;
    osc.prepare(44100.0);

    check(OscPolyBlep::supportsOversampling(1) && !OscPolyBlep::supportsOversampling(2) && !OscPolyBlep::supportsOversampling(8),
          "the phase member is already band-limited at 1x and says so");

    check(osc.setOversampling(1), "1x is accepted");
    check(!osc.setOversampling(4) && osc.getOversampling() == 1, "4x is refused and nothing changes");

    osc.setFrequency(440.0);
    const double period = oscMeasurePeriodSamples(osc, 44100);
    check(std::abs(44100.0 / period - 440.0) < 0.5, "the phase counter gives the frequency asked");

    // El tope del miembro: 0,45 de Nyquist.
    osc.setFrequency(30000.0);
    const double ceiling = OscPolyBlep::maxFrequencyFractionOfNyquist * 44100.0 * 0.5;
    check(std::abs(osc.getFrequency() - ceiling) < 1.0e-9, "its own ceiling is 0.45 of Nyquist, and it reports it");

    osc.setFrequency(220.0);

    // Las cuatro formas, en su recorrido, y el ancho del rectangulo.
    const OscWaveform all[4] = {OscWaveform::Sawtooth, OscWaveform::Triangle, OscWaveform::Rectangle, OscWaveform::Sine};
    bool allSupported        = true;
    bool allBounded          = true;

    for (OscWaveform waveform : all)
    {
        osc.reset();

        if (!osc.setWaveform(waveform))
            allSupported = false;

        for (int i = 0; i < 22050; ++i)
        {
            const double v = osc.processSample();

            if (!std::isfinite(v) || std::abs(v) > 1.10)
                allBounded = false;
        }
    }

    check(allSupported, "the phase member gives all four basic shapes");
    check(allBounded, "all four stay finite inside the family's nominal +-1 (polyBLEP overshoots a few per cent)");

    osc.setWaveform(OscWaveform::Rectangle);
    osc.setPulseWidth(0.25);
    osc.reset();

    int high  = 0;
    int total = 0;

    for (int i = 0; i < 44100; ++i)
    {
        if (osc.processSample() > 0.0)
            ++high;

        ++total;
    }

    check(std::abs(static_cast<double>(high) / static_cast<double>(total) - 0.25) < 0.02,
          "its width means the same 0..1 the family fixes");

    osc.setPulseWidth(0.5);

    // Un incremento que no se puede dibujar NO se convierte en NaN: esta pieza
    // puede acabar sumada en un buffer, y un NaN lo envenena entero.
    {
        OscPolyBlep broken;
        broken.prepare(0.0);
        broken.setFrequency(0.0);

        bool finite = true;

        for (int i = 0; i < 64; ++i)
            if (!std::isfinite(broken.processSample()))
                finite = false;

        check(finite, "a nonsense sample rate gives silence, not NaN");
    }
}

//==============================================================================
static void testOscillatorFamilyLanguage()
{
    printf("=== Oscillator family: one language, two pieces ===\n");

    // La promesa de la familia, medida: la misma frecuencia es la misma
    // frecuencia, y el mismo ancho de pulso el mismo ancho, en las dos piezas.
    {
        OscVcoCa72 ramp;
        OscPolyBlep phase;

        ramp.prepare(44100.0);
        phase.prepare(44100.0);

        ramp.setWaveform(OscWaveform::Sawtooth);
        phase.setWaveform(OscWaveform::Sawtooth);

        ramp.setFrequency(220.0);
        phase.setFrequency(220.0);

        ramp.reset();
        phase.reset();

        const double rampPeriod  = oscMeasurePeriodSamples(ramp, 44100);
        const double phasePeriod = oscMeasurePeriodSamples(phase, 44100);

        check(std::abs(rampPeriod - phasePeriod) / phasePeriod < 0.01,
              "the same 220 Hz in the two members is the same period (within 1%)");
        printf("        (periodo: rampa %.3f muestras, fase %.3f)\n", rampPeriod, phasePeriod);
    }

    // El bloque de la familia, la misma frase para float y para double.
    {
        OscPolyBlep a;
        OscPolyBlep b;

        a.prepare(44100.0);
        b.prepare(44100.0);
        a.setFrequency(330.0);
        b.setFrequency(330.0);
        a.setWaveform(OscWaveform::Triangle);
        b.setWaveform(OscWaveform::Triangle);
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

    // Determinismo: dos piezas iguales, del reset en adelante, dan lo MISMO. Sin
    // esto, un render no se puede comparar con otro.
    {
        OscVcoCa72 one;
        OscVcoCa72 two;

        one.prepare(48000.0);
        two.prepare(48000.0);
        one.setFrequency(311.0);
        two.setFrequency(311.0);
        one.setOversampling(4);
        two.setOversampling(4);
        one.setWaveform(OscWaveform::Rectangle);
        two.setWaveform(OscWaveform::Rectangle);
        one.reset();
        two.reset();

        bool identical = true;

        for (int i = 0; i < 1024; ++i)
            if (one.processSample() != two.processSample())
                identical = false;

        check(identical, "two identical pieces from reset give bit-identical samples");
    }

    // El retardo declarado: a 1x la linea de escalon entrega la muestra anterior,
    // y el sobremuestreo anade el suyo. Un host que alinea voces lo necesita.
    {
        OscVcoCa72 osc;
        osc.prepare(44100.0);

        osc.setOversampling(1);
        const double atOne = osc.getLatencySamples();

        osc.setOversampling(8);
        const double atEight = osc.getLatencySamples();

        check(atOne == 1.0, "at 1x the piece is one sample late, and says it");
        check(atEight > atEight / 8.0 && atEight > 1.0, "at 8x the decimator adds its group delay on top");
        printf("        (retardo: 1x %.2f muestras, 8x %.2f)\n", atOne, atEight);
    }

    // El PERFIL ES EL HARDWARE. Si se pide la misma corriente y se dobla la
    // capacidad de temporizacion, el periodo se dobla: la pieza no tiene una
    // afinacion propia escondida, la tiene su perfil.
    {
        OscVcoCa72 base;
        base.prepare(44100.0);
        base.setTimingCurrent(1.0e-4);

        OscVcoCa72Profile heavy = oscVcoCa72ReferenceProfile();
        heavy.core.c1           = 2.0 * heavy.core.c1;

        OscVcoCa72 heavier(heavy);
        heavier.prepare(44100.0);
        heavier.setTimingCurrent(1.0e-4);

        const double lightPeriod = oscMeasurePeriodSamples(base, 44100);
        const double heavyPeriod = oscMeasurePeriodSamples(heavier, 44100);

        check(std::abs(heavyPeriod / lightPeriod - 2.0) < 0.03,
              "doubling the timing capacitor doubles the period at the same current");
    }

    // Los numeros del perfil del banco de pruebas, como canario: si alguien los
    // toca sin querer, esto lo dice.
    {
        const OscVcoCa72Profile& reference = oscVcoCa72ReferenceProfile();

        check(reference.core.c1 == 0.01e-6 && reference.core.vThreshold == -3.970812,
              "the reference profile is the measured core (C1 and the comparator's threshold)");
        check(reference.shaper.sawRatio == 4.7 / 9.0 && reference.shaper.rectLow == -4.2954,
              "and the measured shaper (the saw's divider and the rectangle's low level)");
        check(reference.triangleTable != nullptr && reference.triangleTableSize == 3,
              "with the triangle's transfer as three measured points, ready to be replaced");
    }
}
