/*
  ==============================================================================

    OscVcoCa72Profile.h
    EL PERFIL DEL VCO DE RANPA: los numeros del circuito, como DATOS
    (namespace abd::synth).

    QUE ES UN PERFIL, EN ESTE REPO. Es la pieza que hace que el motor no sepa de
    que hardware esta hecho: el motor es el mismo para todos y el hardware entra
    como una tabla de numeros inyectada (asi lo hacen DspEffects con
    profiles/Re201Profile.h y compania). Aqui el motor es OscVcoCa72 y este
    fichero es el banco de pruebas del instrumento: capacidades, umbrales,
    retardos y las constantes de cada conformador de onda.

    DE DONDE SALEN LOS NUMEROS. De la propia medida del circuito: un ajuste sobre
    barridos de ngspice del esquema transcrito, a 25 C, en el banco de pruebas del
    estudio del que salio el instrumento. Son DATOS del hardware (como la VAF de
    un transistor o la capacidad de un condensador), no codigo: la referencia de
    la que se tomaron esta bajo GPL y de ella NO se ha copiado ni una linea: el
    algoritmo de OscVcoCa72 esta escrito de nuevo desde la idea (integracion de
    una rampa con reinicio por comparador y conformadores en tension), y lo unico
    que viaja son los numeros medidos del aparato, que sin ellos la pieza no
    seria el aparato sino un oscilador generico.

    QUE NO TRAE, Y ES A PROPOSITO. La tabla MEDIDA de la transferencia del
    triangulo (la curva de Q2/Q1, 891 puntos en la referencia) no se incrusta:
    se INYECTA (OscVcoCa72::setTriangleTable). Lo que trae el perfil por defecto
    son TRES puntos que son el resultado de esa curva en el barrido real del
    nucleo: su pliegue y sus dos extremos. Con tres puntos el triangulo sale
    asimetrico (lo es en el aparato) y la diferencia con la curva completa es la
    de la curvatura entre punto y punto, no la de la forma. Asi el dato masivo
    queda en quien lo tenga y el perfil sigue siendo legible.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cmath>

namespace abd::synth
{

//==============================================================================
/** El nucleo: el condensador que integra la corriente de temporizacion, el umbral
    del disparador de Schmitt, el reinicio y su espera, y el retardo del
    comparador. Todo a 25 C y sin carga (tensiones de circuito abierto). */
struct OscVcoCa72Core
{
    /** El condensador de temporizacion (0.01 uF de poliestireno), F. Los
        condensadores de union de la rampa van aparte. */
    double c1 = 0.01e-6;
    /** Lo que anade el nodo de la rampa (uniones del colector, puerta y sustrato), F. */
    double cPar = 1.594822e-11;
    /** La fuga que sale del nodo de la rampa, A. */
    double iLeak = 1.246916e-11;
    /** La tension de la rampa a la que dispara el Schmitt con una rampa lenta, V. */
    double vThreshold = -3.970812;
    /** El techo de la rampa despues del reinicio, y cuanto baja por V/s de pendiente. */
    double vTop0       = 0.012771;
    double topPerSlope = 2.824424e-07;
    /** Cuanto se mantiene la rampa arriba mientras suelta el transistor, s. */
    double tHold = 1.203511e-06;
    /** El retardo del comparador despues del umbral, contra ln(pendiente en V/s). */
    double delayLnSlope[8] = {4.4359, 5.18819, 5.94048, 6.69278, 7.44507, 8.19736, 8.94966, 9.70195};
    double delay[8]        = {8.581449e-06,
                              6.858305e-06,
                              5.389057e-06,
                              4.194429e-06,
                              3.246414e-06,
                              2.513420e-06,
                              1.954638e-06,
                              1.532251e-06};
    /** Lo que la corriente de temporizacion cae por volt de rampa, por amperio de
        corriente (el efecto Early, algo amplificado por la realimentacion del
        emisor del par). El convertidor de la referencia la resuelve por iteracion
        en los dos extremos de la rampa, y alli la relacion I(-4 V)/I(0 V) baja de
        ~0,94 en el centro del recorrido a 0,881 en su tope: o sea que su pendiente
        casi se dobla hacia arriba. Aqui es UNA constante, la del centro del
        recorrido, y por eso la curvatura de la rampa se queda corta en la parte
        alta (centesimas de su excursion: ver la nota de fidelidad en
        OscVcoCa72.h). */
    double earlySlope = 0.0158;
    /** La corriente de temporizacion mas alta que el modelo da, A: el tope del
        recorrido del aparato, muy por encima de la nota mas aguda que toca (ver
        OscVcoCa72::maxFrequencyHz). */
    double timingMax = 5e-3;
};

/** El retardo del comparador para una pendiente, s. Interpola en el logaritmo de
    la pendiente y recorta en los dos extremos de la tabla: es el propio ajuste
    del banco de pruebas, y por eso vive con los numeros y no en el motor. */
inline double oscVcoCa72DelayAt(const OscVcoCa72Core& core, double slope) noexcept
{
    const auto x = std::log(slope > 1.0e-9 ? slope : 1.0e-9);

    if (x <= core.delayLnSlope[0])
        return core.delay[0];

    for (int i = 1; i < 8; ++i)
    {
        if (x <= core.delayLnSlope[i])
        {
            const auto a = (x - core.delayLnSlope[i - 1]) / (core.delayLnSlope[i] - core.delayLnSlope[i - 1]);

            return core.delay[i - 1] + (core.delay[i] - core.delay[i - 1]) * a;
        }
    }

    return core.delay[7];
}

//==============================================================================
/** El conformador de ondas: el separador (un divisor de tension), el diente de
    sierra, el comparador del rectangulo con su histeresis y su retardo, y las
    dos areas que el reinicio anade (el rizo del triangulo y el retraso del
    separador). */
struct OscVcoCa72Shaper
{
    /** El separador: Vsep = ganancia * Vrampa + desviacion (un barrido de continua). */
    double bufGain   = 2.0346;
    double bufOffset = 3.9098;
    /** La parte del separador que sale al diente de sierra: un divisor resistivo. */
    double sawRatio = 4.7 / 9.0;
    /** El comparador del rectangulo: las tensiones de separador a las que conmuta
        a bajo (cayendo) y a alto (subiendo) con una polarizacion de ancho de 0 V,
        y cuanto se mueven por volt de esa polarizacion. */
    double rectFall0    = -0.0371;
    double rectRise0    = 0.3738;
    double rectPerWidth = -0.9983;
    /** Los dos niveles del rectangulo: el transistor cortado (0 V) y saturado. */
    double rectHigh = 0.0;
    double rectLow  = -4.2954;
    /** El tiempo de sus dos flancos: sube este tiempo despues del reinicio de la
        rampa, y baja este otro despues de que la rampa pase el umbral de continua
        por `rectFallOverdrive` voltios. */
    double rectRiseDelay     = 2.74e-6;
    double rectFallDelay     = 3.75e-6;
    double rectFallOverdrive = 4.94e-3;
    /** Lo que el reinicio anade mas alla de un escalon ideal en su instante, como
        areas (V s): el rizo del triangulo y el retraso del separador. */
    double triGlitch = -3.8e-7;
    double bufLag    = -4.8e-7;
};

//==============================================================================
/** Un punto de la transferencia del triangulo: de la tension del separador a la
    tension de salida (V -> V). */
struct OscVcoCa72TrianglePoint
{
    double buffer;
    double out;
};

/** La transferencia del triangulo que trae el perfil: el pliegue medido del
    aparato (el minimo de la curva de Q2/Q1, en el barrido del nucleo) y sus dos
    extremos, que salen de evaluar esa curva en el techo y en el umbral de la
    rampa. Con estos tres puntos el triangulo del aparato sale como es: cae
    durante algo menos de la mitad del ciclo y sube durante algo mas, y sus dos
    picos no valen lo mismo. */
inline constexpr OscVcoCa72TrianglePoint oscVcoCa72DefaultTriangle[3] = {
    {-4.169214, 1.703494},  // el barrido en el umbral (el fondo del diente de sierra)
    {-0.038000, -1.985911}, // el pliegue: donde la curva medida tiene su minimo
    {3.935784, 1.502639},   // el barrido en el techo del diente de sierra
};

//==============================================================================
/** El perfil completo: el nucleo y el conformador, y la tabla del triangulo. */
struct OscVcoCa72Profile
{
    OscVcoCa72Core core;
    OscVcoCa72Shaper shaper;

    /** La transferencia del triangulo, en tensiones de separador CRECIENTES. Los
        puntos tienen que vivir mas que el oscilador que los usa: no se copian (un
        consumidor que quiera la curva medida completa la inyecta con
        OscVcoCa72::setTriangleTable y responde de su vida). */
    const OscVcoCa72TrianglePoint* triangleTable = oscVcoCa72DefaultTriangle;
    int triangleTableSize                        = 3;
};

/** El VCO del aparato como era en el banco de pruebas: los numeros de arriba y
    nada mas. */
inline constexpr OscVcoCa72Profile oscVcoCa72ReferenceProfile() noexcept
{
    return OscVcoCa72Profile{};
}

/** Cuantos puntos admite la tabla del triangulo. Es un tope de cordura para el
    que la inyecta, no una limitacion del motor (no reserva nada). */
inline constexpr int oscVcoCa72MaxTrianglePoints = 8192;

} // namespace abd::synth
