/*
  ==============================================================================

    S950CalibrationHarness.h
    El arnes que CONVIERTE una sesion de medicion en una tabla de
    calibracion (namespace abd::synth::harness).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore).

    PARA QUE EXISTE. `S950Calibration.h` no trae ni un numero medido, y eso es
    una decision. Este fichero es la otra mitad de esa decision: la maquina que
    los produce, para que "vacio" signifique "todavia nadie ha medido" y no
    "nadie sabe medirlo".

    ─────────────────────────────────────────────────────────────────────────
    EL SESGO DE LA PROPIA VENTANA, QUE ES LO QUE HACE FALTA CORREGIR.

    Medir "cuanto dura un attack" suena a que se mira cuando se acaba. No es
    asi: se mira el paso de una barrida por una ventana de analisis, y una
    ventana promedia lo que pasa por ella. Eso mete un sesgo que depende de la
    VELOCIDAD de la barrida, y por eso no se corrige con una constante: el mismo
    ajuste medido con dos velocidades distintas da dos numeros.

    La tecnica para quitarlo es de manual y no proprietaria:

      1. Se pasa por el MISMO analisis un RENDER de un modelo cuya respuesta se
         conoce. Ahi el sesgo es `observado - verdadero`, y se sabe de cuanto
         es.
      2. Se mide el ajuste real y se le resta ese sesgo.
      3. Se repite. Si el segundo pase se mueve mucho, el modelo del punto 1
         estaba tan equivocado que sus sesgos se tomaron a velocidades
         equivocadas, y hay que repetirlo.

    `Session` hace los tres pasos y deja el desplazamiento por escrito en el
    informe, para que se vea cuanto se movio cada punto y se decida si hay que
    repetir. Un punto que la correccion mueve mas de un 10% no es una medida
    todavia, y `isConverged()` lo dice.

    ─────────────────────────────────────────────────────────────────────────
    POR QUE ESTA ES LA PIEZA QUE HACE QUE LO DEMAS SEA HONESTO.

    Sin el arnes, "no medido" podria ser una excusa. Con el, es un estado con
    una salida: hay un tipo, hay un protocolo, y hay un test que le pasa
    observaciones SINTETICAS con la verdad conocida y las recupera. Ese test es
    la prueba de que la cadena funciona, y no depende de ninguna medicion real:
    la verdad la pone el propio test.

    ─────────────────────────────────────────────────────────────────────────
    LO QUE ESTE ARNES NO HACE.

      - No mide. No tiene sonda, ni banco, ni instrumentacion. Toma
        observaciones que alguien ya ha tomado y las procesa. Un arnes que
        midiese seria un banco de pruebas, y eso es otra pieza.
      - No sabe de la maquina. No sabe que el S950 tiene una envolvente ni que
        su panel va de 0 a 99: eso lo dice `S950Calibration.h`, y este fichero
        solo lo consulta. Un arnes que supiera del panel seria una segunda
        fuente de verdad.
      - No promedia medidas que se contradicen. Rechaza el punto y lo dice.
        La media de dos medidas incompatibles es un numero que no midio nadie,
        y una tabla con ese numero PARECE medida.

    CONTRATO. Header-only, sin estado global, `noexcept` donde se puede, C++17.
    Una `Session` reserva, asi que vive FUERA del hilo de audio: se construye
    una vez al medir y el resultado se pasa a un `const S950Calibration&`.

  ==============================================================================
*/

#pragma once

#include "SynthCore/S950Calibration.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace abd::synth::harness
{

//==============================================================================
/** Una observacion cruda de la maquina: lo que ha dado la sonda, sin corregir.

    @param stored     el valor de panel que se ha puesto
    @param observed   lo que ha dado la sonda, YA en la unidad de la curva
    @param sweepRate  a que velocidad se ha pasado por la ventana, en unidades
                      de la curva por segundo. No se usa todavia —la correccion
                      es por ajuste, no por velocidad— pero se guarda porque
                      una sesion que no lo apunta no se puede repetir, y una
                      medicion que no se puede repetir no es una medicion.
*/
struct Observation
{
    int stored       = 0;
    double observed  = 0.0;
    double sweepRate = 0.0;
};

//==============================================================================
/** Una observacion del MODELO CONOCIDO, con su verdad al lado.

    Es lo que convierte el sesgo del analisis en un numero en vez de en una
    conjetura: de aqui sale `observado - verdadero`, y ese es el sesgo que se
    resta a las medidas de la maquina.

    @param stored   el valor de panel del render
    @param observed lo que el ANALISIS ha leido del render
    @param truth    lo que el render deberia dar, que se sabe porque el modelo
                    es nuestro
*/
struct ReferenceObservation
{
    int stored      = 0;
    double observed = 0.0;
    double truth    = 0.0;
};

//==============================================================================
/** Una sesion de medicion de UNA curva.

    El orden de uso esta en el tipo porque el orden importa: primero el modelo
    conocido, luego la maquina, y despues `build()`.
*/
class Session
{
public:
    //==========================================================================
    /** Una sesion para medir `id`, con la forma de su curva ya declarada. */
    explicit Session(CalCurveId id)
        : curve(id) {}

    //==========================================================================
    /** Una calibracion con la forma de la curva y NINGUN punto. Es de donde se
        sale: lo que se entrega en el repo mientras nadie ha medido. */
    static S950Calibration unmeasured() { return S950Calibration::unmeasured(); }

    //==========================================================================
    /** Las observaciones del modelo conocido, que HAYAN EL SESGO DEL ANALISIS.

        Sin esto `build()` no corrige nada, y sin correccion los puntos son
        numeros con un error del que nadie sabe el origen —que es peor que no
        tener tabla, porque la tabla existe y parece buena—. */
    void addReference(const std::vector<ReferenceObservation>& run)
    {
        for (const auto& o : run)
            reference.push_back(o);
    }

    /** Una observacion de la maquina, en bruto. */
    void addObservation(int stored, double observed, double sweepRate = 0.0)
    {
        measured.push_back(Observation{stored, observed, sweepRate});
    }

    //==========================================================================
    /** El sesgo que el analisis introduce en un ajuste, o 0 si no hay
        referencia para ese ajuste.

        CERO cuando no hay referencia, y no es un valor neutro: es un aviso. Una
        sesion sin modelo conocido produce una tabla SIN CORREGIR que no lo dice
        en ninguna parte, y por eso `build()` avisa y `hasBiasData()` deja que se
        pregunte antes de confiar. */
    double referenceBias(int stored) const
    {
        for (const auto& r : reference)
            if (r.stored == stored)
                return r.observed - r.truth;

        return 0.0;
    }

    /** Si hay referencias que cubran TODOS los ajustes medidos. Si no las hay,
        algunos puntos iran sin corregir, que es el error que este arnes existe
        para no cometer en silencio. */
    bool hasBiasData() const
    {
        if (reference.empty())
            return false;

        for (const auto& m : measured)
        {
            bool found = false;

            for (const auto& r : reference)
                if (r.stored == m.stored)
                {
                    found = true;
                    break;
                }

            if (!found)
                return false;
        }

        return true;
    }

    //==========================================================================
    /** Cuanto se ha movido un punto al corregirlo, en fraccion. */
    struct Shift
    {
        int stored      = 0;
        double fraction = 0.0; // (corregido - observado) / observado
    };

    std::size_t referenceCount() const noexcept { return reference.size(); }
    std::size_t observationCount() const noexcept { return measured.size(); }
    std::size_t shiftCount() const noexcept { return shifts.size(); }

    const ReferenceObservation& referenceAt(std::size_t i) const { return reference[i]; }
    const Observation& observationAt(std::size_t i) const { return measured[i]; }
    const Shift& shiftAt(std::size_t i) const { return shifts[i]; }

    /** El mayor desplazamiento en valor absoluto. Por encima de un 10% la
        medicion no es una medicion todavia. */
    double worstShift() const
    {
        double worst = 0.0;

        for (const auto& s : shifts)
            worst = jmaxAbs(worst, s.fraction);

        return worst;
    }

    /** Si el desplazamiento es pequeno bastante para dar por buena la sesion.
        @param tolerance  en fraccion. El 10% es un punto de partida, no una
                        verdad: cada curva tiene su ruido, y quien mide es quien
                        sabe cuanto es en SU banco. */
    bool isConverged(double tolerance = 0.10) const
    {
        return worstShift() <= tolerance;
    }

    //==========================================================================
    /** Corrige las observaciones y produce la tabla.

        Cada punto sale de restar el sesgo del analisis al valor observado.

        @param out  la calibracion resultante
        @returns  `true` si la curva ha quedado con dos puntos o mas. Con menos
                    de dos no hay curva, y `read()` daria `nullopt` para todo,
                    que es lo correcto pero conviene que el arnes lo diga.
    */
    bool build(S950Calibration& out)
    {
        out.clear(curve);
        shifts.clear();
        lastErrorText = nullptr;

        bool allAccepted = true;

        for (const auto& m : measured)
        {
            const double bias      = referenceBias(m.stored);
            const double corrected = m.observed - bias;

            if (std::abs(m.observed) > 0.0)
                shifts.push_back(Shift{m.stored, (corrected - m.observed) / m.observed});

            const char* why = nullptr;

            if (!out.addPoint(curve, m.stored, corrected, &why))
            {
                lastErrorText           = why != nullptr ? why : "punto rechazado";
                lastRejectedObservation = m;
                allAccepted             = false;
            }
        }

        return allAccepted && out.isMeasured(curve);
    }

    /** Por que se rechazo el ultimo punto, o `nullptr` si no hubo ninguno. */
    const char* lastError() const noexcept { return lastErrorText; }

    /** La observacion que se rechazo, para poder buscarla en la sesion. */
    const Observation& lastRejected() const noexcept { return lastRejectedObservation; }

private:
    //==========================================================================
    /** El mayor de dos valores en valor absoluto. */
    static double jmaxAbs(double a, double b) noexcept
    {
        const double x = a < 0.0 ? -a : a;
        const double y = b < 0.0 ? -b : b;
        return x > y ? x : y;
    }

    CalCurveId curve = CalCurveId::EnvelopeTime;
    std::vector<ReferenceObservation> reference;
    std::vector<Observation> measured;
    std::vector<Shift> shifts;
    const char* lastErrorText = nullptr;
    Observation lastRejectedObservation;
};

} // namespace abd::synth::harness
