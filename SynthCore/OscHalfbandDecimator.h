/*
  ==============================================================================

    OscHalfbandDecimator.h
    LA DECIMACION DE UN OSCILADOR SOBREMUESTREADO: de la frecuencia interna a la
    frecuencia de salida, en etapas de halfband (namespace abd::synth).

    QUE ES. Un nucleo de oscilador que corre a 2, 4 u 8 veces la frecuencia de
    salida tiene que volver a bajar, y bajarlo con "una de cada N" seria tirar el
    sobremuestreo por la ventana: lo que se gana al correr mas rapido es que las
    discontinuidades se colocan en su instante exacto, y para que eso llegue a la
    salida hay que FILTRAR antes de tirar muestras. Eso es lo que hace esto: un
    pasa-bajo de media banda por cada etapa, evaluado solo cuando toca.

    POR QUE HALFBAND, Y POR QUE DISPERSO. Un pasa-bajo de media banda tiene la
    mitad de sus coeficientes en cero (los que estan a distancia par del centro,
    cuyo seno se anula), asi que saltarselos exactamente es la mitad del trabajo,
    y no es una aproximacion: valen cero de verdad. Encadenando etapas de 2 en 2
    se llega a 8 con tres filtros cortos en vez de uno de 8 veces mas largo, que
    es lo que hace pagable el sobremuestreo dentro de una voz (el ultimo es el
    largo porque es el que fija la banda que sobrevive).

    QUE NO HACE, Y POR QUE. No interpola (subir de frecuencia es otra pieza, y no
    la pide nadie hoy) y NO compensa su propio retardo de grupo: lo declara en
    latencySamples() para que el consumidor decida si lo alinea (un oscilador que
    se mezcla con otro suele querer que si). Tampoco reserva memoria: las etapas,
    las lineas y los coeficientes son arrays de tamano fijo, y prepare() no
    asigna.

    Es deliberadamente ajeno al contrato de la familia de osciladores
    (OscillatorFamily.h): no sabe ni de formas de onda ni de frecuencias, solo de
    muestras. Un miembro de la familia lo usa por dentro.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cmath>

namespace abd::synth
{

namespace oscHalfbandDetail
{

/** La funcion de Bessel modificada de primera especie de orden 0, por su serie
    (terminos (x^2/4)^k / (k!)^2, hasta que uno se pierde bajo 1e-17 de la suma).
    Es lo unico que hace falta para enventanar con Kaiser. */
inline double besselI0(double x) noexcept
{
    double sum     = 1.0;
    double term    = 1.0;
    const double y = x * x * 0.25;

    for (int k = 1; k < 60; ++k)
    {
        term *= y / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;

        if (term < sum * 1.0e-17)
            break;
    }

    return sum;
}

} // namespace oscHalfbandDetail

//==============================================================================
/** Una decimacion por 2^etapas con filtros de media banda enventanados con
    Kaiser, las etapas cortas delante y la larga al final. */
class OscHalfbandDecimator
{
public:
    /** El factor de sobremuestreo mas alto que sabe bajar. */
    static constexpr int maxFactor = 8;

    /** Las etapas que tiene un factor: 1 -> ninguna, 2 -> una, 4 -> dos, 8 -> tres. */
    static int stagesForFactor(int factor) noexcept
    {
        switch (factor)
        {
            case 2:
                return 1;
            case 4:
                return 2;
            case 8:
                return 3;
            default:
                return 0;
        }
    }

    /** El factor legal mas parecido, redondeando hacia arriba y con el 8 como
        tope: un factor de 3 no existe, y quedarse con el 2 mas cercano seria
        bajar la banda sin decirlo. */
    static int legalFactor(int factor) noexcept
    {
        if (factor <= 1)
            return 1;

        if (factor <= 2)
            return 2;

        if (factor <= 4)
            return 4;

        return 8;
    }

    //==========================================================================
    /** Sin etapas: la salida es la entrada, muestra a muestra. */
    OscHalfbandDecimator() noexcept
    {
        prepare(1);
    }

    explicit OscHalfbandDecimator(int factor) noexcept
    {
        prepare(factor);
    }

    /** El factor a bajar y sus etapas. Se llama una vez y al cambiarlo, no en el
        lazo: reconstruye los coeficientes y limpia la historia. */
    void prepare(int factor) noexcept
    {
        factor_    = legalFactor(factor);
        numStages_ = stagesForFactor(factor_);

        double latency = 0.0;

        for (int k = 0; k < numStages_; ++k)
        {
            const bool last = (k + 1 == numStages_);
            const int taps  = last ? longTaps : shortTaps;

            stage_[k].prepare(taps);

            // El retardo de cada etapa, llevado al ritmo de SALIDA: su retardo de
            // grupo esta en muestras de SU entrada, y su entrada corre a
            // 2^(etapas que quedan, ella incluida) veces la salida.
            latency += static_cast<double>((taps - 1) / 2) / static_cast<double>(1 << (numStages_ - k));
        }

        latencySamples_ = latency;

        reset();
    }

    /** Olvida la historia sin tocar el factor. */
    void reset() noexcept
    {
        for (int k = 0; k < maxStages; ++k)
            stage_[k].reset();
    }

    /** El factor que se esta bajando (1, 2, 4 u 8). */
    int factor() const noexcept { return factor_; }

    /** Lo que la decimacion retrasa la salida, en muestras de salida. Con factor
        1 vale exactamente 0. */
    double latencySamples() const noexcept { return latencySamples_; }

    /** Una muestra de la entrada sobremuestreada. Devuelve `true` (y escribe
        `out`) solo cada 2^etapas llamadas; el resto devuelve `false` y no toca
        `out`. */
    bool process(double input, double& out) noexcept
    {
        double value = input;

        for (int k = 0; k < numStages_; ++k)
        {
            if (!stage_[k].push(value, value))
                return false;
        }

        out = value;
        return true;
    }

private:
    static constexpr int maxStages = 3;

    /** Los taps de una etapa corta y los de la larga. La larga es la ultima
        porque es la que fija la banda que sobrevive; las cortas solo van
        limpiando lo que las etapas siguientes ya no necesitan. */
    static constexpr int shortTaps = 23;
    static constexpr int longTaps  = 79;
    static constexpr int maxTaps   = longTaps;

    /** La beta del enventanado de Kaiser: da unos 100 dB de stopband en las
        etapas largas, que es mas de lo que pide un oscilador. */
    static constexpr double kaiserBeta = 10.06;

    //==========================================================================
    /** Una etapa: los coeficientes, la linea de retardo y la fase de la
        decimacion. La linea es doble para que la ventana de la convolucion sea
        siempre contigua y no haga falta ramificar en el lazo. */
    struct Stage
    {
        void prepare(int length) noexcept
        {
            n                   = length | 1; // impar
            const int centre    = n / 2;
            const double i0beta = oscHalfbandDetail::besselI0(kaiserBeta);

            for (int i = 0; i < n; ++i)
            {
                const double k    = static_cast<double>(i - centre);
                const double sinc = (k == 0.0)
                                        ? 0.5
                                        : std::sin(pi * k * 0.5) / (pi * k);
                const double r    = k / static_cast<double>(centre);
                const double win  = oscHalfbandDetail::besselI0(
                                       kaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) /
                                   i0beta;

                taps[i] = sinc * win;
            }

            // Los taps a distancia PAR del centro valen cero de verdad (su seno se
            // anula) y se saltan en la suma: es la mitad del trabajo, y la razon
            // de que el sobremuestreo por 8 sea pagable dentro de una voz.
            for (int i = 0; i < n; ++i)
            {
                if (i != centre && ((i - centre) % 2) == 0)
                    taps[i] = 0.0;
            }

            // Ganancia unitaria en continua, para que la decimacion no cambie el
            // nivel: se normaliza DESPUES de anular, que es cuando ya se sabe
            // cuanto suman de verdad.
            double total = 0.0;

            for (int i = 0; i < n; ++i)
                total += taps[i];

            if (total != 0.0)
            {
                for (int i = 0; i < n; ++i)
                    taps[i] /= total;
            }

            clear();
        }

        void reset() noexcept { clear(); }

        /** Empuja una muestra; devuelve `true` y escribe `y` solo cada segunda. */
        bool push(double input, double& y) noexcept
        {
            const bool finite = std::isfinite(input);

            // Un valor no finito no envenena la linea (se guarda un cero) pero si
            // sale, sale tal cual: perder un NaN es peor que verlo.
            const double stored = finite ? input : 0.0;

            pos           = (pos + 1 == n) ? 0 : pos + 1;
            line[pos]     = stored;
            line[pos + n] = stored;

            phase = !phase;

            if (phase)
                return false;

            y = windowSum();

            if (!finite)
                y = input;

            return true;
        }

    private:
        /** La convolucion con la ventana de las ultimas `n` muestras. Solo suma
            los taps distintos de cero: el del centro y los de distancia impar. */
        double windowSum() const noexcept
        {
            const int centre  = n / 2;
            const double* win = &line[pos + 1];

            double acc = taps[centre] * win[centre];

            for (int i = (centre % 2 == 0) ? 1 : 0; i < n; i += 2)
                acc += taps[i] * win[i];

            return acc;
        }

        void clear() noexcept
        {
            for (int i = 0; i < 2 * maxTaps; ++i)
                line[i] = 0.0;

            pos   = (n > 0) ? n - 1 : 0;
            phase = false;
        }

        static constexpr double pi = 3.14159265358979323846;

        double taps[maxTaps]     = {};
        double line[2 * maxTaps] = {};
        int n                    = 0;
        int pos                  = 0;
        bool phase               = false;
    };

    //==========================================================================
    Stage stage_[maxStages];
    int factor_            = 1;
    int numStages_         = 0;
    double latencySamples_ = 0.0;
};

} // namespace abd::synth
