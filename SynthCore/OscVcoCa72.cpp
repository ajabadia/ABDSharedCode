/*
  ==============================================================================

    OscVcoCa72.cpp
    El VCO de rampa: el nucleo, sus conformadores y el limitado de banda
    (namespace abd::synth). Ver la cabecera del .h para el contrato y para las
    decisiones de fidelidad.

    LA NORMALIZACION DEL IDIOMA SALE DEL PROPIO PERFIL. La familia pide una salida
    bipolar y nominalmente +-1, y el aparato no da eso: da tensiones de circuito
    abierto (el separador tiene una desviacion de continua de casi 4 V, el diente
    de sierra no sube lo mismo que baja, el rectangulo conmuta entre 0 y -4,3 V).
    En vez de meter tres constantes magicas, la pieza las DERIVA de su perfil en
    cada `updateDerived`: mira donde llega cada conformador en el barrido real del
    nucleo y divide por ahi. Asi, cambiar el perfil cambia las normalizaciones con
    el, y no hay dos numeros que puedan separarse.

    LO QUE ESO DEJA ASIMETRICO, Y NO SE TAPA. El diente de sierra del aparato
    sube menos de lo que baja (se normaliza por el lado grande y el otro queda en
    +-0,94); el triangulo tiene el pliegue descentrado y sus dos picos no valen lo
    mismo (se normaliza por el mayor y el otro queda en 0,86). Es como es: la
    alternativa era recortar o recentrar, o sea sonar como otro oscilador.

  ==============================================================================
*/

#include "OscVcoCa72.h"

namespace abd::synth
{

namespace
{
/** La corriente de temporizacion mas baja que el modelo admite, A. Por debajo de
    la fuga de la rampa el condensador se cargaria hacia arriba y el nucleo no
    tendria umbral que cruzar; la familia no pide nada por debajo de 0,05 Hz, que
    son ordenes de magnitud mas corriente que esto. */
constexpr double minTimingCurrent = 1.0e-12;

/** Cuantos pasos de biseccion resuelven la corriente para una frecuencia. Con 40
    el intervalo relativo queda por debajo de 1e-12, que en esta ley es una
    millonésima de cent: la resolucion no se oye. */
constexpr int timingSolves = 40;
} // namespace

//==============================================================================
OscVcoCa72::OscVcoCa72() noexcept
{
    prepare(44100.0);
    setFrequency(440.0);
    reset();
}

OscVcoCa72::OscVcoCa72(const OscVcoCa72Profile& wantedProfile) noexcept
{
    setProfile(wantedProfile);
    prepare(44100.0);
    reset();
    timingCurrent = 0.0;
    setFrequency(440.0);
}

//==============================================================================
void OscVcoCa72::setProfile(const OscVcoCa72Profile& wantedProfile) noexcept
{
    const double wanted = frequencyHz;

    profile = wantedProfile;

    // Un perfil de cero no puede colgar el lazo de audio: se sanean SOLO los
    // divisores y las leyes que lo colgarian, no los numeros del aparato.
    if (!(profile.core.earlySlope > 0.0))
        profile.core.earlySlope = 1.0e-3;

    if (!(profile.core.c1 + profile.core.cPar > 0.0))
        profile.core.c1 = 1.0e-8;

    if (!(std::abs(profile.shaper.bufGain) > 1.0e-9))
        profile.shaper.bufGain = 1.0;

    if (profile.triangleTable == nullptr || profile.triangleTableSize <= 0)
    {
        profile.triangleTable     = oscVcoCa72DefaultTriangle;
        profile.triangleTableSize = 3;
    }

    updateDerived();

    // La ley del aparato ha cambiado: la corriente que daba esta frecuencia ya no
    // es la misma, asi que se resuelve de nuevo.
    timingCurrent = 0.0;
    frequencyHz   = wanted;
    setFrequency(wanted);
}

const OscVcoCa72Profile& OscVcoCa72::getProfile() const noexcept
{
    return profile;
}

void OscVcoCa72::setTriangleTable(const OscVcoCa72TrianglePoint* points, int count) noexcept
{
    if (points == nullptr || count <= 0)
    {
        profile.triangleTable     = oscVcoCa72DefaultTriangle;
        profile.triangleTableSize = 3;
    }
    else
    {
        profile.triangleTable     = points;
        profile.triangleTableSize = std::min(count, oscVcoCa72MaxTrianglePoints);
    }

    updateDerived();
}

//==============================================================================
void OscVcoCa72::prepare(double sampleRate) noexcept
{
    if (sampleRate > 0.0 && std::isfinite(sampleRate))
        sampleRateHz = sampleRate;
    else
        sampleRateHz = 44100.0;

    dt = 1.0 / (sampleRateHz * static_cast<double>(oversample));

    sawDecimator.prepare(oversample);
    triDecimator.prepare(oversample);
    rectDecimator.prepare(oversample);

    updateDerived();
}

void OscVcoCa72::reset() noexcept
{
    ramp         = profile.core.vTop0;
    holdLeft     = 0.0;
    resetIn      = 0.0;
    riseIn       = 0.0;
    fallIn       = 0.0;
    resetPending = false;
    risePending  = false;
    fallPending  = false;
    rectIsLow    = false;
    sub          = 0;

    sawLine.reset();
    triLine.reset();
    rectLine.reset();

    sawDecimator.reset();
    triDecimator.reset();
    rectDecimator.reset();

    outSaw  = 0.0;
    outTri  = 0.0;
    outRect = 0.0;
}

//==============================================================================
void OscVcoCa72::setFrequency(double hz) noexcept
{
    const double ceiling = std::min(oscMaxFrequencyHz, maxFrequencyHz());

    double wanted = oscClampFrequencyHz(hz);

    if (!(wanted <= ceiling))
        wanted = ceiling > oscMinFrequencyHz ? ceiling : oscMinFrequencyHz;

    if (wanted == frequencyHz && timingCurrent > 0.0)
        return;

    frequencyHz   = wanted;
    timingCurrent = timingCurrentForFrequency(frequencyHz);

    // Lo que de verdad sale del modelo. Si la biseccion se ha quedado en el tope
    // del perfil, la frecuencia pedida no se alcanza y aqui se ve: la pieza
    // devuelve la suya, no la que le pidieron. Si SI se alcanza (a una parte en
    // mil millones), se declara lo pedido, para que getFrequency() sea el numero
    // que pidio el panel y no un 19999,999999998.
    const double achieved = 1.0 / periodSecondsForTimingCurrent(timingCurrent);

    if (!(achieved > 0.0) || !std::isfinite(achieved))
        return;

    if (std::abs(achieved - frequencyHz) > frequencyHz * 1.0e-9)
        frequencyHz = achieved;
}

bool OscVcoCa72::setWaveform(OscWaveform wanted) noexcept
{
    if (!supportsWaveform(wanted))
        return false;

    waveform = wanted;

    return true;
}

bool OscVcoCa72::setOversampling(int factor) noexcept
{
    if (!supportsOversampling(factor))
        return false;

    if (factor == oversample)
        return true;

    oversample = factor;
    dt         = 1.0 / (sampleRateHz * static_cast<double>(oversample));

    // La rampa y sus sucesos NO se tocan: el nucleo es de tiempo continuo y
    // cambiar el factor no mueve la fase. Lo que arranca de silencio es el
    // limitado de banda, como en el aparato al cambiar de modo.
    sawDecimator.prepare(oversample);
    triDecimator.prepare(oversample);
    rectDecimator.prepare(oversample);

    sawLine.reset();
    triLine.reset();
    rectLine.reset();

    outSaw  = 0.0;
    outTri  = 0.0;
    outRect = 0.0;
    sub     = 0;

    return true;
}

void OscVcoCa72::setPulseWidth(double duty01) noexcept
{
    // Primero el idioma de la familia (recorta y trata el NaN) y despues lo que
    // este comparador puede dar de verdad. Mas estrecho que su minimo el
    // rectangulo no subiria nunca al reinicio, y eso no es un pulso fino: es un
    // rectangulo pegado abajo.
    const double wanted = oscClampPulseWidth(duty01);

    pulseWidth = std::min(std::max(wanted, minDuty), maxDuty);

    updatePulseThresholds();
}

void OscVcoCa72::setTimingCurrent(double amps) noexcept
{
    // La puerta del circuito se recorta al recorrido de la FAMILIA: la pieza no
    // sale con una frecuencia que su idioma no sabe pedir.
    const double lo = timingCurrentForFrequency(oscMinFrequencyHz);
    const double hi = timingCurrentForFrequency(oscMaxFrequencyHz);

    double wanted = amps;

    if (!(wanted > lo))
        wanted = lo;

    if (wanted > hi)
        wanted = hi;

    if (wanted == timingCurrent)
        return;

    timingCurrent = wanted;

    const double achieved = 1.0 / periodSecondsForTimingCurrent(timingCurrent);

    frequencyHz = achieved > 0.0 && std::isfinite(achieved) ? achieved : frequencyHz;
}

//==============================================================================
double OscVcoCa72::processSample() noexcept
{
    const auto& sh = profile.shaper;

    const TimingLaw law = timingLawFor(timingCurrent);
    const double a      = law.a;
    const double b      = law.b;
    const double c      = law.c;

    // El rectangulo baja cuando el separador cae por debajo de su umbral, con el
    // sobrepico del comparador ya sumado (el comparador tarda en conmutar).
    const double vFall = (vFallBuffer - sh.bufOffset) / sh.bufGain - sh.rectFallOverdrive;

    for (int k = 0; k < oversample; ++k)
    {
        sub = k;

        step(a, b, c, vFall, riseBuffer);

        const double buffer = bufferVoltageOf(ramp);
        const double level  = rectIsLow ? sh.rectLow : sh.rectHigh;

        const double s = sawLine.push(sh.sawRatio * buffer);
        const double t = triLine.push(triangleVoltageOf(buffer));
        const double r = rectLine.push(level);

        double decimated = 0.0;

        if (sawDecimator.process(s, decimated))
            outSaw = decimated;

        if (triDecimator.process(t, decimated))
            outTri = decimated;

        if (rectDecimator.process(r, decimated))
            outRect = decimated;
    }

    switch (waveform)
    {
        case OscWaveform::Triangle:
            return outTri / triScale;

        case OscWaveform::Rectangle:
            return (outRect - rectCenter) / rectHalf;

        case OscWaveform::Sawtooth:
        default:
            return outSaw / sawScale;
    }
}

//==============================================================================
double OscVcoCa72::getPhase() const noexcept
{
    const double phase = phaseOfRampVoltage(ramp);

    return std::min(std::max(phase, 0.0), 1.0);
}

//==============================================================================
OscVcoCa72::TimingLaw OscVcoCa72::timingLawFor(double amps) const noexcept
{
    const auto& core = profile.core;

    const double current = std::max(amps, minTimingCurrent);

    return TimingLaw{std::max(current - core.iLeak, minTimingCurrent),
                     std::max(timingSlopeFor(current), 1.0e-30),
                     std::max(core.c1 + core.cPar, 1.0e-18)};
}

//==============================================================================
double OscVcoCa72::rampVoltageAtPhase(double phase) const noexcept
{
    const auto& core = profile.core;

    const TimingLaw law = timingLawFor(timingCurrent);
    const double bigA   = law.a / law.b;
    const double v0     = core.vTop0;
    const double sw     = std::log(std::max((v0 + bigA) / (core.vThreshold + bigA), 1.0 + 1.0e-12));
    const double p      = std::min(std::max(phase, 0.0), 1.0);

    // v(p) = (v0 + A) e^(-p sw) - A: la caida del condensador despejada por la
    // parte del tiempo que lleva.
    return (v0 + bigA) * std::exp(-p * sw) - bigA;
}

double OscVcoCa72::phaseOfRampVoltage(double rampVolts) const noexcept
{
    const auto& core = profile.core;

    const TimingLaw law = timingLawFor(timingCurrent);
    const double bigA   = law.a / law.b;
    const double v0     = core.vTop0;
    const double above  = rampVolts + bigA;

    if (!(above > 0.0) || !(v0 + bigA > 0.0))
        return 0.0;

    const double sw = std::log((v0 + bigA) / (core.vThreshold + bigA));

    if (!(sw > 0.0))
        return 0.0;

    return std::log((v0 + bigA) / above) / sw;
}

double OscVcoCa72::getLatencySamples() const noexcept
{
    return sawDecimator.latencySamples() + 1.0 / static_cast<double>(oversample);
}

double OscVcoCa72::maxFrequencyHz() const noexcept
{
    const double top = 1.0 / periodSecondsForTimingCurrent(profile.core.timingMax);

    return std::min(oscMaxFrequencyHz, top);
}

//==============================================================================
double OscVcoCa72::periodSecondsForTimingCurrent(double amps) const noexcept
{
    const auto& core = profile.core;

    const TimingLaw law = timingLawFor(amps);
    const double a      = law.a;
    const double b      = law.b;
    const double c      = law.c;

    // El techo de la rampa baja un poco cuando la rampa corre; se evalua dos
    // veces y ya esta (la correccion que se aplica a si misma es de milesimas de
    // voltio sobre cuatro, asi que la segunda pasada no mueve nada medible).
    double vTop = core.vTop0;

    for (int i = 0; i < 2; ++i)
        vTop = core.vTop0 - core.topPerSlope * ((a + b * vTop) / c);

    const double bigA = a / b;

    // La rampa cae de vTop a vThreshold con C dv/dt = -(a + b v): el tiempo sale
    // del logaritmo de los dos extremos desplazados por a/b.
    const double ratio = std::max((vTop + bigA) / (core.vThreshold + bigA), 1.0);
    const double tRamp = (c / b) * std::log(ratio);

    const double slopeAtThreshold = (a + b * core.vThreshold) / c;
    const double tDelay           = oscVcoCa72DelayAt(core, slopeAtThreshold);

    return tRamp + tDelay + core.tHold;
}

double OscVcoCa72::timingCurrentForFrequency(double hz) const noexcept
{
    const double target = 1.0 / std::max(hz, oscMinFrequencyHz);

    // EN LOGARITMO, y no es un detalle: la corriente util va de las decimas de
    // nanoamperio de la nota mas grave al miliamperio del tope, cuatro decadas.
    // Bisecar en lineal da la misma precision ABSOLUTA en todo el recorrido, o
    // sea una precision relativa pesima en la parte baja (que es donde la ley
    // del nucleo es mas sensible: el periodo va como 1/i0).
    double lo = std::log(minTimingCurrent);
    double hi = std::log(std::max(profile.core.timingMax, 2.0 * minTimingCurrent));

    // Ni a tope llega el nucleo a esa frecuencia: se devuelve el tope (y
    // getFrequency() dira cual es la que de verdad sale).
    if (periodSecondsForTimingCurrent(std::exp(hi)) > target)
        return std::exp(hi);

    for (int i = 0; i < timingSolves; ++i)
    {
        const double mid = 0.5 * (lo + hi);

        if (periodSecondsForTimingCurrent(std::exp(mid)) > target)
            lo = mid;
        else
            hi = mid;
    }

    return std::exp(0.5 * (lo + hi));
}

//==============================================================================
void OscVcoCa72::updateDerived() noexcept
{
    const auto& core = profile.core;
    const auto& sh   = profile.shaper;

    const double bufferTop    = bufferVoltageOf(core.vTop0);
    const double bufferBottom = bufferVoltageOf(core.vThreshold);

    // Diente de sierra: se normaliza por el lado mayor de su barrido.
    sawScale = std::max(std::abs(sh.sawRatio) * std::max(std::abs(bufferTop), std::abs(bufferBottom)), 1.0e-9);

    // Triangulo: por el mayor de los tres valores que visita (el pliegue y los dos
    // extremos del barrido), y sin salirse del barrido si la tabla es mas larga.
    double triMax = std::max(std::abs(triangleVoltageOf(bufferTop)), std::abs(triangleVoltageOf(bufferBottom)));

    for (int i = 0; i < profile.triangleTableSize; ++i)
    {
        const auto& point = profile.triangleTable[i];

        if (point.buffer >= bufferBottom && point.buffer <= bufferTop)
            triMax = std::max(triMax, std::abs(point.out));
    }

    triScale = std::max(triMax, 1.0e-9);

    // Rectangulo: sus dos niveles, centrados.
    rectCenter = 0.5 * (sh.rectHigh + sh.rectLow);
    rectHalf   = std::max(0.5 * std::abs(sh.rectHigh - sh.rectLow), 1.0e-9);

    // El ancho de pulso mas estrecho que este comparador da. La condicion es que
    // su umbral de SUBIDA quede por debajo del techo del separador (si no, el
    // rectangulo no sube al reinicio y se queda abajo para siempre); el umbral de
    // subida esta la histeresis por encima del de bajada, asi que el limite es el
    // de bajada que cae histeresis por debajo del techo. El maximo solo lo pone el
    // idioma de la familia: hacia arriba el comparador si sabe llegar.
    minDuty = oscMinPulseWidth;
    maxDuty = oscMaxPulseWidth;

    if (sh.rectPerWidth < 0.0)
    {
        const double bufferAtFall = bufferTop + (sh.rectFall0 - sh.rectRise0);
        const double narrowest    = dutyFromBufferVoltage(bufferAtFall);

        minDuty = std::min(std::max(narrowest, oscMinPulseWidth), oscMaxPulseWidth);
    }

    updatePulseThresholds();
}

//==============================================================================
double OscVcoCa72::dutyToWidth(double duty) const noexcept
{
    const auto& sh = profile.shaper;

    if (std::abs(sh.rectPerWidth) < 1.0e-12)
        return 0.0;

    // La fase pedida (en TIEMPO), llevada a tension de rampa y de ahi a tension
    // de separador: el umbral de bajada tiene que estar donde la rampa esta
    // cuando lleva esa parte de su caida. Ojo con "la fraccion de la tension":
    // como la rampa es concava, la mitad del recorrido cae en el 49,26% del
    // tiempo, y usar la tension mentiria el ancho de pulso.
    const double bufferFall = bufferVoltageOf(rampVoltageAtPhase(duty));

    return (bufferFall - sh.rectFall0) / sh.rectPerWidth;
}

double OscVcoCa72::dutyFromBufferVoltage(double bufferVolts) const noexcept
{
    const auto& sh = profile.shaper;

    if (std::abs(sh.bufGain) < 1.0e-12)
        return 0.5;

    return phaseOfRampVoltage((bufferVolts - sh.bufOffset) / sh.bufGain);
}

void OscVcoCa72::updatePulseThresholds() noexcept
{
    const auto& sh     = profile.shaper;
    const double width = dutyToWidth(pulseWidth);

    vFallBuffer = sh.rectFall0 + sh.rectPerWidth * width;
    riseBuffer  = sh.rectRise0 + sh.rectPerWidth * width;
}

//==============================================================================
double OscVcoCa72::triangleVoltageOf(double bufferVolts) const noexcept
{
    const auto* table = profile.triangleTable;
    const int count   = profile.triangleTableSize;

    // Sin tabla, la transferencia es la identidad: el triangulo sale del propio
    // barrido, que es lo que un perfil sin tabla puede prometer.
    if (table == nullptr || count <= 0)
        return bufferVolts;

    if (count == 1)
        return table[0].out;

    if (bufferVolts <= table[0].buffer)
        return table[0].out;

    if (bufferVolts >= table[count - 1].buffer)
        return table[count - 1].out;

    // La tabla es pequena (tres puntos por defecto) pero puede inyectarse larga:
    // biseccion, que es logaritmica y no depende del tamano.
    int lo = 0;
    int hi = count - 1;

    while (hi - lo > 1)
    {
        const int mid = (lo + hi) / 2;

        if (table[mid].buffer <= bufferVolts)
            lo = mid;
        else
            hi = mid;
    }

    const double span = table[hi].buffer - table[lo].buffer;

    if (!(span > 0.0))
        return table[lo].out;

    const double t = (bufferVolts - table[lo].buffer) / span;

    return table[lo].out + (table[hi].out - table[lo].out) * t;
}

//==============================================================================
void OscVcoCa72::step(double a, double b, double c, double vFall, double riseBuffer) noexcept
{
    const auto& core = profile.core;
    const auto& sh   = profile.shaper;

    const double threshold = core.vThreshold;

    double elapsed = 0.0;

    for (;;)
    {
        const double left  = dt - elapsed;
        const bool holding = holdLeft > 0.0;

        const double rampEnd = holding ? ramp : advanceRamp(ramp, left, a, b, c);

        double next = left;
        int which   = 0;

        const auto take = [&next, &which](double at, int what) {
            if (at < next)
            {
                next  = at;
                which = what;
            }
        };

        // El suceso mas temprano de lo que queda del paso.
        if (holding)
            take(holdLeft, 1);

        if (resetPending)
            take(resetIn, 2);

        if (risePending)
            take(riseIn, 3);

        if (fallPending)
            take(fallIn, 4);

        if (!holding && !resetPending && ramp > threshold && rampEnd <= threshold)
            take(std::min(std::max(crossingTime(ramp, threshold, a, b, c), 0.0), left), 5);

        if (!holding && !rectIsLow && !fallPending && ramp > vFall && rampEnd <= vFall)
            take(std::min(std::max(crossingTime(ramp, vFall, a, b, c), 0.0), left), 6);

        // Se lleva todo al instante del suceso.
        if (!holding)
            ramp = advanceRamp(ramp, next, a, b, c);

        holdLeft = std::max(holdLeft - next, 0.0);

        if (resetPending)
            resetIn -= next;

        if (risePending)
            riseIn -= next;

        if (fallPending)
            fallIn -= next;

        elapsed += next;

        const double frac = std::min(std::max(elapsed / dt, 1.0e-9), 1.0);

        switch (which)
        {
            case 2:
                resetPending = false;
                doReset(a, b, c, frac, riseBuffer);
                break;

            case 3:
                risePending = false;

                if (rectIsLow)
                {
                    rectIsLow = false;
                    rectLine.step(sh.rectHigh - sh.rectLow, frac);
                }
                break;

            case 4:
                fallPending = false;

                if (!rectIsLow)
                {
                    rectIsLow = true;
                    rectLine.step(sh.rectLow - sh.rectHigh, frac);
                }
                break;

            case 5:
                // La rampa ha cruzado el umbral: el comparador cita el reinicio,
                // y su retardo depende de la pendiente (una rampa lenta tarda mas
                // en conmutar). El reinicio NO es instantaneo.
                ramp         = threshold;
                resetIn      = oscVcoCa72DelayAt(core, (a + b * threshold) / c);
                resetPending = true;
                break;

            case 6:
                // El rectangulo ha pasado su umbral: su flanco sale un poco
                // despues (el transistor saliendo de saturacion).
                ramp        = vFall;
                fallIn      = sh.rectFallDelay;
                fallPending = true;
                break;

            default:
                break;
        }

        if (which == 0)
            break;
    }
}

void OscVcoCa72::doReset(double a, double b, double c, double frac, double riseBuffer) noexcept
{
    const auto& core = profile.core;
    const auto& sh   = profile.shaper;

    const double slope = (a + b * ramp) / c;
    const double vTop  = core.vTop0 - core.topPerSlope * slope;
    const double buf0  = bufferVoltageOf(ramp);
    const double buf1  = bufferVoltageOf(vTop);

    // Los escalones de los dos conformadores, en su instante exacto dentro de la
    // muestra, y las dos areas que el reinicio anade: el rizo del triangulo
    // mientras el separador barre el pliegue y el retraso del separador.
    sawLine.step(sh.sawRatio * (buf1 - buf0), frac);
    sawLine.impulse(sh.sawRatio * sh.bufLag / dt, frac);

    triLine.step(triangleVoltageOf(buf1) - triangleVoltageOf(buf0), frac);
    triLine.impulse(sh.triGlitch / dt, frac);

    // Si el rectangulo sigue abajo cuando la rampa vuelve arriba, su flanco de
    // subida queda citado (el transistor saliendo de saturacion tarda).
    if (rectIsLow && buf1 >= riseBuffer)
    {
        risePending = true;
        riseIn      = sh.rectRiseDelay;
    }

    // Un flanco de bajada pendiente queda adelantado por el propio reinicio.
    fallPending = false;

    ramp     = vTop;
    holdLeft = core.tHold;
}

} // namespace abd::synth
