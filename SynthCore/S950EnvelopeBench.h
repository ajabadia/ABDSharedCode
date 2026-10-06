/*
  ==============================================================================

    S950EnvelopeBench.h
    El BANCO DE PRUEBAS de la calibracion: renderiza una envolvente con tiempos
    CONOCIDOS y mide su duracion por el mismo analisis que usaria una sonda
    (namespace abd::synth::bench).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore).

    ─────────────────────────────────────────────────────────────────────────
    QUE PIEZA ES ESTA, Y CUAL DE LAS DOS QUE FALTABAN.

    `S950CalibrationHarness.h` es el ARNES: corrige el sesgo de una sesion de
    medicion y produce la tabla. En su propia cabecera dice, con razon, que no
    mide: "un arnes que midiese seria un banco de pruebas, y eso es otra pieza".

    Esta es esa otra pieza, y solo SU MITAD.

    Lo que hace es lo que hace falta para que lo otro sea honesto: renderiza una
    envolvente cuya respuesta se conoce —porque es NUESTRA, no de la maquina—,
    la pasa por el MISMO analisis que pasaria por la maquina, y devuelve la
    pareja `observado - verdadero`. Ese numero es el sesgo, y el arnes lo resta.
    Sin el, los puntos que salen de una medicion son numeros con un error del
    que nadie sabe el origen, que es PEOR que no tener tabla: la tabla existe y
    parece buena.

    ─────────────────────────────────────────────────────────────────────────
    LO QUE ESTE BANCO NO ES, Y POR QUE IMPORTA DECIRLO PRIMERO.

    ESTO NO PRODUCE NI UN PUNTO DE LA CURVA DEL S950. Ni uno.

    La verdad que se usa aqui la pone este repositorio. El attack de un S950 de
    verdad es otra cosa, y medirlo exige el aparato y la maquina. Ejecutar este
    banco deja `S950Calibration.h` exactamente igual de vacia, y debe: es el
    mismo argumento de siempre, y la tabla del estudio Mz950 que si tiene los
    numeros es AGPLv3.

    Lo que produce es la calibracion DEL METODO. Y hay una diferencia importante
    entre "el metodo esta mal medido" y "el metodo tiene un sesgo que hay que
    restar": lo segundo es lo que el arnes ya sabe restar, y solo se puede
    comprobar que se resta bien si el sesgo se ha calculado de verdad y no
    inventado.

    ─────────────────────────────────────────────────────────────────────────
    EL SESGO, Y POR QUE NO PUEDE SER UNA CONSTANTE.

    La idea de la medicion es sencilla y la trampa tambien: no se mira cuando se
    acaba la envolvente, se mira el paso de una barrida por una VENTANA DE
    ANALISIS. Y una ventana promedia, no mira: promedia. Eso mete un error que
    depende de la VELOCIDAD con la que pasa la señal, y por eso no se arregla
    con un factor fijo: el mismo factor medido con dos velocidades distintas da
    dos numeros distintos.

    Por eso el analisis de aqui es una media movil de verdad, y por eso el banco
    devuelve el sesgo POR PUNTO y no uno global. Si el sesgo saliera constante,
    este fichero estaria haciendo el trabajo del test que multiplicaba por 1.08,
    que es el caso facil: un sesgo de un solo sentido y del mismo tamaño en todas
    partes. Y un sesgo que no cambia con la velocidad no es un sesgo de ventana.

    El segundo efecto, y el que mas startling resulta, es que la ventana VE LO
    QUE PUEDE. Si la envolvente dura menos que la ventana, el promediado no llega
    nunca al umbral, y el analisis no ve una enveloped corta: no ve NADA. Eso no
    es un tiempo de cero, es una medicion IMPOSIBLE, y `DurationReading` lo
    distingue con `MeasureOutcome`. Ver `visibilityFloor()`.

    ─────────────────────────────────────────────────────────────────────────
    LA VERDAD ES ANALITICA, Y ESO SE COMPRUEBA.

    Cada forma tiene su verdad en forma cerrada, y las dos dependen del UMBRAL
    que usa el analisis, no solo de la forma. Mismo mando, misma ventana,
    distinta forma: distinto sesgo, porque la forma cambia lo que la ventana
    promedia. Y eso obliga a que el umbral tambien entre en la cuenta, asi que
    `trueOnSeconds()` lleva los dos.

      - `Linear`: el umbral se cruza en `threshold * rise` subiendo, y en
        `rise * (1 - threshold)` antes del final de la bajada, y la longitud por
        encima del umbral es `2 * rise * (1 - threshold) + hold`.
      - `Exponential`: un RC de constante `tau = rise / ln(2)`, NORMALIZADO para
        que la subida llegue a 1 y la bajada termine en 0. Los dos cruces salen
        de la misma cuenta, y la longitud es
        `rise + hold + tau * log( (1 - th (1 - k)) / (k + th (1 - k)) )` con
        `k = exp(-rise / tau)`.

    Y aqui hay un detalle que salio MIDIENDO, no leyendo, que es de los que
    cuestan un banco entero. La primera version fijaba el umbral de construccion
    en 0.5 y daba por hecha la verdad `rise + hold` para cualquier umbral. Con
    umbral 0.5 la verdad era correcta; con 0.25 y con 0.75 el conteo real del
    render se separaba de la verdad un 58% en AMBOS sentidos. No era un error
    de redondeo: era que con tau fija la subida NUNCA pasa de 0.5, de modo que
    un umbral de 0.25 la cuenta desde la mitad de la subida y uno de 0.75 no la
    cuenta nunca. Un banco que affirmsa una verdad para unos umbrales y se
    calla para otros no mide: estima. La version de ahora normaliza el render y
    trae la verdad completa, y el test la contrasta contra el conteo a mano con
    tres umbrales distintos y ventana de UNA muestra.

    Con umbral 0.5 la formula exponencial se reduce a `rise + hold`, que es la
    cuenta que decia la primera version. No por casualidad: 0.5 es la mitad, y
    una forma que llega a la mitad justo en `rise` tiene el cruce de subida en
    `rise` y el de bajada Other en `rise`. Pero solo a 0.5.

    ─────────────────────────────────────────────────────────────────────────
    CONTRATO. Header-only, sin estado global, `noexcept` donde se puede, C++17.

    Reserva memoria, asi que vive FUERA del hilo de audio, igual que la sesion
    del arnés. A diferencia del arnés, esto no es codigo de motor: es instrumentacion,
    y no lo incluye nadie en un motor. `analyseDuration()` si es la funcion que
    una sesion real tendria que compartir con la sonda, y por eso esta aqui y no
    en un .cpp suelto: una copia del analisis en dos sitios mide con dos reglas
    distintas y el sesgo que se corrige es el de la otra.

  ==============================================================================
*/

#pragma once

#include "SynthCore/S950CalibrationHarness.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace abd::synth::bench
{

//==============================================================================
/** La forma del pulso de referencia que se renderiza.

    Las dos son exactas y las dos son distintas a proposito: ver la cabecera
    sobre por que la verdad analitica depende de la forma. */
enum class ReferenceShape
{
    Linear,     ///< rampa recta: el umbral se cruza en `threshold * rise`
    Exponential ///< RC: la constante se elige para cruzar en `rise` exacto
};

//==============================================================================
/** Como esta puesto el banco.

    `windowSeconds` va en SEGUNDOS y no en muestras, y es la decision que mas
    importa de las dos. Una sonda real tiene una ventana de un tiempo, y
    expresarla en muestras hace que el mismo banco de 64 muestras mida cosas
    distintas a 44.1 kHz que a 96 kHz: el error seria de la maquina cuando es de
    como se ha puesto la maquina. En segundos, el sesgo depende de la señal y no
    de la tasa, que es lo unico que hace falta para que el numero sea comparable
    entre sesiones. */
struct BenchConfig
{
    double sampleRate    = 48000.0; ///< Hz. Por debajo de 1000 se corrige, como en el motor
    double windowSeconds = 0.002;   ///< duracion de la media movil. El sesgo sale de aqui
    double threshold     = 0.5;     ///< nivel al que se considera que la envolvente esta puesta
    double holdSeconds   = 0.0;     ///< meseta a 1.0 entre la subida y la bajada

    /** Cuanto se sigue escuchando la sonda DESPUES de que el pulso se acaba.

        No es relleno, y sin el la forma exponencial no se puede medir NUNCA: su
        bajada se acerca al umbral asintoticamente, asi que el cruce cae
        JUSTO en la ultima muestra del pulso nominal y el analisis responde
        `NeverFell` para todos los rises. Con la cola, la sonda ve terminar la
        bajada y el cruce queda DENTRO del render.

        Ponerla a 0 hace alcanzable `NeverFell` a proposito, y con razon: es lo
        que pasa cuando la grabacion se corta antes de tiempo, y ese estado tiene
        que existir para poder distinguirlo de "ha durado poco". */
    double tailSeconds = 0.01; ///< segundos de escucha despues del final del pulso nominal

    /** La ventana en muestras, redondeada hacia arriba y con un minimo de 1.

        Redondear ARRIBA y no al entero mas proximo: una ventana de medio sample
        de menos promedia de menos, y el sesgo se come justo el efecto que
        estamos midiendo. */
    int windowSamples() const noexcept
    {
        const double sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
        const int n     = static_cast<int>(std::ceil(windowSeconds * sr));
        return n < 1 ? 1 : n;
    }

    double effectiveSampleRate() const noexcept
    {
        return sampleRate > 1000.0 ? sampleRate : 44100.0;
    }
};

//==============================================================================
/** Lo que el analisis ha sacado del render, INCLUYENDO lo que NO ha sacado.

    Y el `outcome` es la mitad importante del tipo. Un analisis que no ve una
    envolvente corta tiene que poder decir "no la he visto", y si no puede
    decirlo tiene que inventarse un numero —que es el fallo de todo este
    trabajo—. Por eso `seconds` es `std::optional<double>`: no hay un 0 de
    salida, hay una ausencia con nombre. */
enum class MeasureOutcome
{
    Measured,  ///< se ha visto la envolvente y `seconds` tiene valor
    NeverRose, ///< el promediado NUNCA llega al umbral: no se distingue de un silencio
    NeverFell, ///< se paso de alto el umbral y no ha vuelto a bajar antes de acabar el render
    Empty      ///< no habia nada que analizar
};

//==============================================================================
/** Una lectura de duracion, con su veredicto. */
struct DurationReading
{
    MeasureOutcome outcome = MeasureOutcome::Empty;

    /// El tiempo por encima del umbral. SIN VALOR si el analisis no ha visto nada.
    std::optional<double> seconds;

    int firstSample = -1; ///< indice de la primera muestra por encima, o -1
    int lastSample  = -1; ///< indice de la ultima, o -1

    double peakWindowed = 0.0; ///< el mayor nivel del promediado. Util para POR QUE no se vio
};

/** Cuantas muestras por encima del umbral ocupa el tramo que se ha medido.

    Va aparte de `seconds` porque la pregunta es distinta: `seconds` es CUANTO,
    y esto es SI SE PUEDE saber cuanto. */
inline int aboveSamples(const DurationReading& r) noexcept
{
    if (r.firstSample < 0 || r.lastSample < r.firstSample) return 0;
    return r.lastSample - r.firstSample + 1;
}

//==============================================================================
//==============================================================================
/** La constante de tiempo del RC de referencia.

    `tau = rise / ln(2)` y no al reves: hace que el RC sin normalizar llegue a
    LA MITAD justo en `rise`, que es la convencion de un attack —el tiempo que se
    tarda en llegar a la mitad de su carrera— y no una cuenta hecha para que
    cuadre. La normalizacion va aparte, en `referenceTop()`, y es lo que hace que
    el pulso llegue a 1 y no se quede en 0.5. */
inline double referenceTau(double riseSeconds) noexcept
{
    constexpr double kLn2 = 0.69314718055994530942;
    const double rise     = riseSeconds > 0.0 ? riseSeconds : 0.0;
    return rise / kLn2;
}

//==============================================================================
/** El nivel NORMALIZADO que alcanza el RC al cabo de `rise`.

    Con `tau = rise / ln(2)` esto vale 0.5, y se divide por el para que la
    subida llegue a 1. Sin esa division el pulso tendria un escalon de media
    unidad al final de la bajada, y un escalon es un click: contaminaria el
    promediado con un transitorio que no esta midiendo nada, y el sesgo que sale
    de ahi seria de un artefacto del render y no de la ventana. */
inline double referenceTop(double riseSeconds) noexcept
{
    const double rise = riseSeconds > 0.0 ? riseSeconds : 0.0;
    if (rise <= 0.0) return 1.0;
    return 1.0 - std::exp(-rise / referenceTau(rise));
}

//==============================================================================
/** La longitud por encima del umbral de un pulso de verdad, en forma cerrada.

    `Linear`: el nivel es `t / rise` subiendo, asi que entra en umbral en
    `threshold * rise`, y en la bajada sale en `rise * (1 - threshold)` antes del
    final. El tramo por encima dura `2 * rise * (1 - threshold) + hold`.

    `Exponential`: la constante de tiempo se deriva para que el umbral se cruce
    en `rise` EXACTO —`tau = -rise / ln(1 - threshold)`—, y como la bajada hace
    el camino simetrico, tambien cruza `rise` despues de la meseta. El tramo por
    encima dura entonces `rise + hold`.

    Y OJO con esa segunda cuenta, que es la que se escribio mal la primera vez:
    son `rise`, NO `2 * rise`. El tramo empieza en `t = rise` —donde SUBE— y
    acaba en `t = 2 * rise + hold` —donde BAJA—, y lo que dura es la diferencia.
    Con `2 * rise` el banco se inventaba un sesgo del −50% en TODAS las medidas
    exponenciales: enorme, constante y falso. Se caczo midiendo, no leyendo, y
    es justo el fallo que el resto de este trabajo rechaza —un error de verdad, del
    que nadie sabria el origen—.

    El `threshold` se recorta a (0,1) porque en los extremos la cuenta no tiene
    sentido: con umbral 0 el tramo seria infinito y con umbral 1 seria vacio. */
inline double trueOnSeconds(ReferenceShape shape, double riseSeconds,
                            const BenchConfig& cfg) noexcept
{
    const double rise = riseSeconds > 0.0 ? riseSeconds : 0.0;
    const double hold = cfg.holdSeconds > 0.0 ? cfg.holdSeconds : 0.0;

    double th = cfg.threshold;
    if (th <= 0.0) th = 0.0;
    if (th >= 1.0) th = 0.9999999;

    switch (shape)
    {
        case ReferenceShape::Linear:
            // El umbral se cruza antes en la subida que en la bajada, y por eso
            // el factor NO es `1 - threshold` en los dos: la bajada empieza en 1.
            return 2.0 * rise * (1.0 - th) + hold;

        case ReferenceShape::Exponential:
        default: {
            // Los dos cruces del RC normalizado, y el tramo es la diferencia.
            // Se puede escribir de dos maneras, y la de arriba es la que NO
            // necesita casos: `subiendo` es el nivel por debajo del umbral del
            // que hay que salir para cruzarlo, y `above` el nivel por el que hay
            // que bajar en la bajada. Con umbral 0 los dos son 1 y el tramo es
            // el pulso entero —`2 * rise + hold`—; con umbral 1 los dos se
            // juntan y el tramo es la meseta sola —`hold`—. Los dos extremos
            // salen bien de la misma ecuacion, que es lo que se le pide a una
            // forma cerrada.
            const double tau = referenceTau(rise);
            const double k   = std::exp(-rise / tau);
            const double lo  = 1.0 - th * (1.0 - k);
            const double hi  = k + th * (1.0 - k);

            if (lo <= 0.0 || hi <= 0.0)
                return hold;

            return rise + hold + tau * std::log(lo / hi);
        }
    }
}

/** El nivel del pulso en el instante `t`. El renderizable de referencia. */
inline float referenceLevel(ReferenceShape shape, double t, double riseSeconds,
                            double holdSeconds) noexcept
{
    const double rise = riseSeconds > 0.0 ? riseSeconds : 0.0;
    const double hold = holdSeconds > 0.0 ? holdSeconds : 0.0;

    if (rise <= 0.0)
        return 0.0f;

    if (t < 0.0)
        return 0.0f;

    if (t < rise)
    {
        if (shape == ReferenceShape::Exponential)
        {
            // El RC crudo, `1 - exp(-t / tau)`, DIVIDIDO por lo que alcanza en
            // `rise`. La division es lo que hace que la subida llegue a 1.0 y
            // empareje con la meseta sin escalon: el render tiene que ser la
            // forma del pulso, no la forma del pulso mas un salto, porque un
            // salto es un transitorio que el promediado contaria como duracion.
            const double tau = referenceTau(rise);
            const double top = referenceTop(rise);
            return static_cast<float>((1.0 - std::exp(-t / tau)) / top);
        }

        return static_cast<float>(t / rise);
    }

    if (t < rise + hold)
        return 1.0f;

    const double d = t - rise - hold;

    if (d < rise)
    {
        if (shape == ReferenceShape::Exponential)
        {
            // La bajada es el mismo RC reflejado, y con la misma normalizacion
            // para que arranque en 1.0 y acabe en 0.0 justo en `d = rise`.
            const double tau        = referenceTau(rise);
            const double top        = referenceTop(rise);
            const double floorLevel = std::exp(-rise / tau);
            return static_cast<float>((std::exp(-d / tau) - floorLevel) / top);
        }

        return static_cast<float>(1.0 - d / rise);
    }

    return 0.0f;
}

//==============================================================================
/** Renderiza el pulso completo a `cfg.sampleRate`, mas la cola de escucha.

    La cola NO cambia la verdad: el pulso acaba en cero y en cero se queda, asi
    que anadir Samples despues no alarga el tramo por encima del umbral. Lo que
    hace es dar sitio al ULTIMO CRUCE, que es lo que un analisis necesita ver
    para poder decir "ha bajado" en vez de "se ha acabado el rollo". */
inline std::vector<float> renderReference(ReferenceShape shape, double riseSeconds,
                                          const BenchConfig& cfg)
{
    const double rise = riseSeconds > 0.0 ? riseSeconds : 0.0;
    const double hold = cfg.holdSeconds > 0.0 ? cfg.holdSeconds : 0.0;
    const double sr   = cfg.effectiveSampleRate();

    const double total = 2.0 * rise + hold + (cfg.tailSeconds > 0.0 ? cfg.tailSeconds : 0.0);

    if (rise <= 0.0 || total <= 0.0)
        return {};

    // El tope no es una prudencia de compilador: un rise de una hora son 170
    // millones de muestras, y reservarlas para responder "no lo he visto" es
    // la manera mas cara de no tener nada.
    constexpr double kMaxSeconds = 600.0;
    if (total > kMaxSeconds)
        return {};

    const std::size_t n = static_cast<std::size_t>(std::ceil(total * sr));
    if (n < 2)
        return {};

    std::vector<float> out(n);

    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        out[i]         = referenceLevel(shape, t, rise, hold);
    }

    return out;
}

//==============================================================================
/** EL ANALISIS. La media movil, y la lectura del tiempo por encima del umbral.

    Esta es la funcion que una sesion real tiene que compartir con la sonda, y
    por eso es la UNICA que hay: una segunda copia del analisis seria un segundo
    criterio de medida, y el sesgo que se corrige seria el de la otra.

    La ventana es centrada, con division por las muestras QUE HAY en cada borde
    en vez de por el ancho completo. No es un detalle: en el flanco de subida
    todavia no se han llenado las muestras de la izquierda, y dividir por el
    ancho completo ahi bajaria el nivel del promediado justo donde se decide si
    la envolvente ha empezado. El borde tiene menos informacion y tiene que
    parecerlo. */
inline DurationReading analyseDuration(const std::vector<float>& rendered,
                                       const BenchConfig& cfg)
{
    DurationReading r;

    const std::size_t n = rendered.size();
    if (n == 0)
    {
        r.outcome = MeasureOutcome::Empty;
        return r;
    }

    const int w     = cfg.windowSamples();
    const int half  = (w - 1) / 2;
    const double th = cfg.threshold;

    double peak    = 0.0;
    int runStart   = -1;
    int runEnd     = -1;
    int bestStart  = -1;
    int bestEnd    = -1;
    int bestLength = 0;

    for (std::size_t i = 0; i < n; ++i)
    {
        // Los indices van en int porque la ventana es petite; en int64 se
        // multiplicarian los casteos por nada.
        const std::ptrdiff_t centre = static_cast<std::ptrdiff_t>(i);
        const std::ptrdiff_t lo     = std::max<std::ptrdiff_t>(0, centre - half);
        const std::ptrdiff_t hi     = std::min<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(n) - 1, centre + half);

        double sum = 0.0;
        for (std::ptrdiff_t k = lo; k <= hi; ++k)
            sum += rendered[static_cast<std::size_t>(k)];

        const double count = static_cast<double>(hi - lo + 1);
        const double level = sum / count;

        if (level > peak) peak = level;

        if (level > th)
        {
            if (runStart < 0) runStart = static_cast<int>(i);
            runEnd = static_cast<int>(i);
        }
        else
        {
            if (runStart >= 0)
            {
                const int length = runEnd - runStart + 1;
                if (length > bestLength)
                {
                    bestLength = length;
                    bestStart  = runStart;
                    bestEnd    = runEnd;
                }
                runStart = -1;
                runEnd   = -1;
            }
        }
    }

    // Un tramo que se ha cerrado justo en la ultima muestra tambien cuenta: si no,
    // un render que acaba subiendo daria un tramo mas corto del que es.
    if (runStart >= 0)
    {
        const int length = runEnd - runStart + 1;
        if (length > bestLength)
        {
            bestLength = length;
            bestStart  = runStart;
            bestEnd    = runEnd;
        }
    }

    r.peakWindowed = peak;
    r.firstSample  = bestStart;
    r.lastSample   = bestEnd;

    if (bestLength == 0)
    {
        // El promediado no ha llegado al umbral NI UNA VEZ. Y esto no es "ha
        // durado 0": es que no se ha distinguido de un silencio, y son dos cosas
        // que un banco de pruebas tiene que poder contar por separado.
        r.outcome = MeasureOutcome::NeverRose;
        return r;
    }

    // El tramo se ha comido el final del render: la bajada no ha terminado
    // dentro de lo que hay. Tambien es una medicion IMPOSIBLE, no un tiempo.
    if (bestEnd == static_cast<int>(n) - 1 && peak > th)
    {
        r.outcome = MeasureOutcome::NeverFell;
        return r;
    }

    const double sr = cfg.effectiveSampleRate();
    r.seconds       = static_cast<double>(bestLength) / sr;
    r.outcome       = MeasureOutcome::Measured;
    return r;
}

//==============================================================================
//==============================================================================
/** Si la lectura es una MEDICION o solo un pico.

    Y aqui hay otro fallo que solo aparece MIDiendo, que es el que mas caro sale
    de todos: con la ventana muy grande frente a la envolvente, el promediado
    puede cruzar el umbral en UNA o dos muestras aunque la envolvente sea
    larguisima. El analisis devuelve entonces "ha durado 0.02 ms" para un pulso
    de 5 ms, sin avisar y con toda la pinta de un numero.

    Eso no es un sesgo grande: es un numero que no describe nada, y es peor que
    un sesgo grande porque el sesgo grande se nota. Si este retorno fuera a
    `addReference()`, restaria un 99% de un punto real y la tabla saldria
    movida en la direccion contraria sin que nadie pudiera decir por que.

    El criterio es de una linea y sale de la propia ventana: el tramo por encima
    del umbral tiene que ser AL MENOS tan largo como la ventana. Por debajo de
    eso no cabe dentro de la ventana, y lo que se ha medido no es la duracion
    de nada. Con una ventana de 20 ms y un rise de 5 ms el tramo son 20 muestras
    frente a las 960 de la ventana, y el banco lo declara NO MEDIDO. */
inline bool isTrustworthy(const DurationReading& r, const BenchConfig& cfg) noexcept
{
    return r.outcome == MeasureOutcome::Measured && aboveSamples(r) >= cfg.windowSamples();
}

//==============================================================================
/** El pulso de referencia, medido. El atajo de las dos mitades juntas. */
inline DurationReading measureReference(ReferenceShape shape, double riseSeconds,
                                        const BenchConfig& cfg)
{
    const std::vector<float> rendered = renderReference(shape, riseSeconds, cfg);
    return analyseDuration(rendered, cfg);
}

//==============================================================================
/** Un mando del banco: que valor de panel y que rise se le pide. */
struct Command
{
    int stored         = 0;   ///< el valor de panel, el mismo dominio de la sonda real
    double riseSeconds = 0.0; ///< la duracion de subida que se le ordena
};

//==============================================================================
/** Una fila de la tabla de sesgo, con el veredicto al lado. */
struct BiasEntry
{
    int stored             = 0;
    double riseSeconds     = 0.0;
    double truth           = 0.0;   ///< el tramo por encima del umbral, en forma cerrada
    double observed        = 0.0;   ///< lo que ha leido el analisis. SIN VALOR si no lo ha visto
    double bias            = 0.0;   ///< observado - verdadero. 0 si no se ha medido
    bool measured          = false; ///< si el analisis ha visto este pulso CON CONFIANZA
    MeasureOutcome outcome = MeasureOutcome::Empty;
};

//==============================================================================
/** El informe de una pasada: las filas que se pueden usar y las que no. */
struct RunReport
{
    /// Las filas para `Session::addReference()`. Solo las MEDIDAS.
    std::vector<harness::ReferenceObservation> reference;

    /// Los `stored` que el analisis no ha podido ver, con su motivo.
    std::vector<BiasEntry> skipped;

    /// Todas las filas, sean medidas o no. Para el informe.
    std::vector<BiasEntry> entries;

    /// Cuantas filas se han podido usar. Un numero que se mira ANTES de
    /// indexar la referencia, porque ahi es donde se pierde la sesion sin que nadie
    /// lo note. */
    std::size_t usable() const noexcept { return reference.size(); }
    std::size_t skippedCount() const noexcept { return skipped.size(); }
};

//==============================================================================
/** Mide una pasada de mandos y devuelve el informe.

    Y HERE ESTA LA PARTE QUE HACE QUE ESTE BANCO NO Mienta: un pulso que el
    analisis no ve **no se convierte en un sesgo de cero**. Va a `skipped`, con
    su motivo, y no entra en `reference`. Porque un sesgo de cero es un sesgo
    INVENTADO, y el arnes lo restaria con toda la confianza, y el punto saldría
    movido en la direccion equivocada sin que nadie lo viera.

    Un sesgo que falta es un hueco visible. Un sesgo de mentira es un numero. */
inline RunReport runReference(const std::vector<Command>& commands,
                              ReferenceShape shape, const BenchConfig& cfg)
{
    RunReport report;

    for (const auto& c : commands)
    {
        const DurationReading reading = measureReference(shape, c.riseSeconds, cfg);

        BiasEntry e;
        e.stored      = c.stored;
        e.riseSeconds = c.riseSeconds;
        e.truth       = trueOnSeconds(shape, c.riseSeconds, cfg);
        e.outcome     = reading.outcome;
        e.measured    = isTrustworthy(reading, cfg);

        if (e.measured && reading.seconds.has_value())
        {
            e.observed = *reading.seconds;
            e.bias     = e.observed - e.truth;

            report.reference.push_back(harness::ReferenceObservation{
                c.stored, e.observed, e.truth});
        }
        else
        {
            report.skipped.push_back(e);
        }

        report.entries.push_back(e);
    }

    return report;
}

//==============================================================================
/** La tabla de sesgo que se imprime al elegir la velocidad de barrido.

    Sirve para decidir con numeros, no con suerte: la razon de que el arnes corrija
    por punto es que el sesgo depende de la velocidad, y esta tabla es lo que
    deja verlo. Si saliera una columna plana, el arnes podria corregir con una
    constante y habria estado ahi todo el rato. */
inline std::vector<BiasEntry> biasTable(ReferenceShape shape, const BenchConfig& cfg,
                                        double minRiseSeconds, double maxRiseSeconds,
                                        int steps)
{
    std::vector<BiasEntry> out;

    if (steps < 1) return out;
    if (maxRiseSeconds < minRiseSeconds) return out;

    for (int i = 0; i < steps; ++i)
    {
        const double t = steps == 1 ? minRiseSeconds
                                    : minRiseSeconds + (maxRiseSeconds - minRiseSeconds) * (static_cast<double>(i) / static_cast<double>(steps - 1));

        Command c;
        c.stored      = i;
        c.riseSeconds = t;

        const DurationReading reading = measureReference(shape, c.riseSeconds, cfg);

        BiasEntry e;
        e.stored      = i;
        e.riseSeconds = c.riseSeconds;
        e.truth       = trueOnSeconds(shape, c.riseSeconds, cfg);
        e.outcome     = reading.outcome;
        e.measured    = isTrustworthy(reading, cfg);

        if (e.measured && reading.seconds.has_value())
        {
            e.observed = *reading.seconds;
            e.bias     = e.observed - e.truth;
        }

        out.push_back(e);
    }

    return out;
}

//==============================================================================
/** El rise MAS CORTO que el analisis ve con esta ventana, o nada si no ve ninguno.

    Y este es el numero accionable de todo el banco: por debajo de el, la sonda
    no mide, y da igual cuantas veces se repita la medicion porque el problema
    no es el ruido, es que la ventana es mas larga que la envolvente.

    Con la ventana de 2 ms por defecto y un umbral de 0.5, una envolvente
    exponencial deja de verse por debajo de unos 4 ms —y un attack de S950 va de
    0 a 99, con el extremo rapido MUCHO mas rapido que eso. O sea: la ventana
    que hace falta para ver un attack largo es una ventana que no ve el attack
    corto, y por eso el barrido tiene que ir a velocidades que se puedan
    distinguir. Eso es lo que decide esta funcion. */
inline std::optional<double> visibilityFloor(ReferenceShape shape, const BenchConfig& cfg)
{
    // Busqueda del rise MAS CORTO que se ve. El invariante es: `lo` no se ve y
    // `hi` si, asi que al terminar `hi` es el suelo con la precision de la
    // busqueda. Se busca en escala LOG porque el suelo esta donde la ventana
    // empieza a comer al pulso, y esa region es multiplicativa.
    double lo = 1e-9; // no se ve: ridiculamente corto para cualquier ventana
    double hi = 1.0;  // se ve en cualquier configuracion razonable

    const auto visible = [&](double rise) {
        return measureReference(shape, rise, cfg).outcome == MeasureOutcome::Measured;
    };

    if (visible(lo)) return lo;            // ya ve desde el origen: no hay suelo
    if (!visible(hi)) return std::nullopt; // no ve ni un segundo: no hay ningun dato

    for (int i = 0; i < 48; ++i)
    {
        const double mid = std::sqrt(lo * hi);
        if (visible(mid))
            hi = mid;
        else
            lo = mid;
    }

    return hi;
}

} // namespace abd::synth::bench
