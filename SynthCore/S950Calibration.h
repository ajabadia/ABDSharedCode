/*
  ==============================================================================

    S950Calibration.h
    Las curvas de UNIDADES MEDIDAS del Akai S950: que curva existe, en que
    unidad esta, sobre que rango de panel, y —cuando alguien la haya medido—
    que valor tiene (namespace abd::synth).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore).

    QUE ES Y QUE NO ES. `S950PatchFields.h` dice que campo es cada byte y en que
    rango se mueve. Esto dice CUANTO VALE: que "attack 80" son 2.8 segundos, que
    "LFO rate 45" son 4 Hz, que "warp time 50" son 69 ms. Sin esto el importador
    funciona igual —guarda y devuelve bytes— pero un motor que quiera SUENAR como
    la maquina no tiene ni por donde empezar.

    ─────────────────────────────────────────────────────────────────────────
    LA TABLA ESTA VACIA, Y ESO ES LO IMPORTANTE.

    No hay ni un número medido en este fichero, y no es una cantidad que falte
    por escribir: es una decisión, y la razón está en `read()`.

    Una curva de calibración son RESULTADOS EXPERIMENTALES: alguien puso una
    sonda en un osciloscopio, barrió un mando y apuntó lo que pasó. Eso no es un
    hecho de formato que se pueda redescubrir con un editor hex —un hecho de
    formato lo es, y por eso los offsets de byte de S950PatchFields.h están
    aquí—, pero una medición pertenece a quien la hizo, y su licencia es otra.
    El estudio Mz950 tiene una campaña de medición completa y es AGPLv3.

    Y el coste de NO tener los números es cero para lo que este repo hace hoy,
    mientras que el coste de inventarlos sería enorme:

      - Un importador no los necesita. `S950Disk.h` lee y escribe el VALOR
        GUARDADO, que es un entero de panel. Ni lo mira.
      - Un renderer sí los necesita, y por eso va a tenerlos… de una medición
        propia, hecha con `S950CalibrationHarness.h`, que es de aquí.

    LA REGLA, Y ES LA RAZÓN DE TODO ESTE FICHERO: **lo que no se ha medido
    devuelve `std::nullopt`, y NUNCA cero.**

    El tentación sería devolver 0. Sería un desastre silencioso: un motor que
    pidiera el attack y recibiera 0 lo interpretaría como "instantáneo", y una
    envolvente instantánea es un CLICK. Un motor que lo recibiera como 0 no
    tendría ningún motivo para sospechar, porque 0 es un número. `std::optional`
    convierte "no lo sé" en un tipo distinto de "vale 0", que es justo lo que
    hace falta para que el compilador ayude en vez de estorbar.

    ─────────────────────────────────────────────────────────────────────────
    LA INTERPOLACIÓN, Y POR QUÉ ES LINEAL EN LOG Y NO UNA LEY DE POTENCIA.

    Una ley de potencia encaja muy bien en las curvas de este tipo de panel,    y por eso es la primera que se escribe y la que casi siempre queda. Aquí no:
    este tipo de panel **salta**, no se desliza. La diferencia se ve al medir —
    un paso de 5 unidades dentro de una década no es la mitad de un paso de 10,
    y los pasos alternan de tamaño en vez de repartirse— y un ajuste de potencia
    no puede reproducir un salto: entre dos puntos medidos suaviza donde la
    máquina da un escalón.

    Así que entre dos puntos MEDIDOS se interpola linealmente en el logaritmo,
    que es lo que respeta un eje de tiempo sin fingir más de lo que se sabe. Y
    fuera del rango medido NO se extrapola salvo que se pida explícitamente, y
    que pedirlo salga en el informe del arnés. Ver `read()`.

    ─────────────────────────────────────────────────────────────────────────
    LO QUE SE DECLARA Y LO QUE SE MIDE.

    Lo que se DECLARA aquí es la FORMA de cada curva: su código, su unidad, si su
    eje crece o baja, y el rango de panel que cubre. Eso sale de
    `S950PatchFields.h`, que ya está probado, y es dato de formato. Lo que se
    MIDE son los puntos. Y la separación está en la API, no en un comentario:
    `curveInfo()` no cambia nunca, `pointCount()` sí.

    CONTRATO. Header-only, `constexpr` donde se puede, `noexcept`, C++17, sin
    estado global y sin asignaciones en el hilo de audio. Una calibración
    cargada es un `const S950Calibration&` que vive mientras ella viva.

  ==============================================================================
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <vector>

namespace abd::synth
{

//==============================================================================
/** Las curvas de calibracion que el S950 tiene. El enumerable es parte del
    contrato: un host elige una con `CalCurveId` y no con una cadena, para que
    cambiar el nombre no pueda cambiar lo que suena.
*/
enum class CalCurveId
{
    /** Tiempo de las etapas de envolvente, en segundos. Es UNA curva para
        attack, decay y release: la maquina no los mide por separado, y el
        punto es la misma tabla. El sustain NO va aqui —no es un tiempo, es un
        nivel, y tiene su propia curva. */
    EnvelopeTime,

    /** Velocidad del LFO, en hercios. Lineal en hertz, no exponencial: una
        unidad es un numero fijo de hertz este donde se este. */
    LfoRate,

    /** Constante de tiempo del WARP, en segundos. El panel la llama tiempo,
        pero no se parece a la envolvente: cubre 22:1 donde la envolvente cubre
        mil a uno. */
    WarpTime,

    /** Frecuencia de corte del filtro, en hercios. Sube con el byte. */
    FilterCutoff,

    /** Cuanto mueve la envolvente del filtro el corte, en OCTAVAS a cantidad
        maxima. Sube con el byte. El detalle de que una octava sea un factor dos
        y no un doubling lineal es la mitad de acertar esta curva. */
    FilterEnvOctaves,

    /** Nivel de sustain, en DECIBELIOS respecto al pico. BAJA con el byte, que
        es lo contrario que los otros: un sustain mas alto suena mas fuerte. */
    SustainDb,

    count
};

//==============================================================================
/** Un punto medido de una curva: el valor de panel y lo que se midio ahi. */
struct CalPoint
{
    int stored;   // el valor de panel, el mismo dominio que S950PatchFields
    double value; // en la unidad de la curva
};

//==============================================================================
/** La FORMA de una curva. Esto no se mide: se DECLARA, y sale del dominio del
    panel. Es lo que un panel necesita para dibujarse los ejes aunque no haya ni
    un punto medido, y por eso existe incluso con la tabla vacia.
*/
struct CalCurveInfo
{
    CalCurveId id;
    const char* code;      // clave estable, para un host o un fichero
    const char* name;      // lo que ve la persona
    const char* unit;      // "s", "Hz", "octavas", "dB"
    const char* axisLabel; // que es el eje X: el valor de panel

    int storedLo; // rango de panel que la curva cubre
    int storedHi;

    bool risesWithStored; // true = mas byte, mas valor (un attack mas lento)
    bool logarithmic;     // si el eje se escala en log, que casi siempre

    /** Si el valor tiene que ser ESTRICTAMENTE POSITIVO.

        Y esto NO es lo mismo que `logarithmic`, que es el error que se cometio
        al escribir la primera version: se puso "si la curva es de tiempo o de
        frecuencia, el valor no puede ser cero" y se dedujo del hecho de que su
        eje fuera logaritmico. Son cosas distintas. Los decibelios tienen eje
        logaritmico y admiten el 0 —0 dB es el nivel de referencia, no un
        instante— y el silencio de un sustain apagado ES un valor legitimo que
        hay que poder escribir. Un filtro que rechazara el 0 dB no podria
        representar un sustain al minimo.

        En cambio un tiempo de cero no es un tiempo: es un instante, y una
        envolvente de duracion cero es un click. Por eso las curvas de tiempo y
        de frecuencia lo exigen y las de dB no. */
    bool positiveOnly;

    /** Si se puede extrapolar mas alla de lo medido. Por defecto no, y el
        motivo es que un extremo que nadie ha medido es el extremo que se
        extrapola con una conjetura: el rapido se termina antes de que la
        sonda lo vea, y el lento dura mas que la nota. */
    bool allowExtrapolation;
};

inline constexpr CalCurveInfo calibrationCurves[] =
    {
        {CalCurveId::EnvelopeTime, "envelopeTime", "Envelope time", "s",
         "Attack / Decay / Release", 0, 99, false, true, true, false},
        {CalCurveId::LfoRate, "lfoRate", "LFO rate", "Hz",
         "LFO rate", 0, 99, true, false, true, false},
        {CalCurveId::WarpTime, "warpTime", "Warp time", "s",
         "Warp time", 0, 99, true, true, true, false},
        {CalCurveId::FilterCutoff, "filterCutoff", "Filter cutoff", "Hz",
         "Soft / Loud filter", 0, 99, true, true, true, false},
        {CalCurveId::FilterEnvOctaves, "filterEnvOctaves", "Filter env", "octaves",
         "VCF amount", -50, 50, false, false, false, false},
        {CalCurveId::SustainDb, "sustainDb", "Sustain level", "dB",
         "Attack / Decay / Sustain", 0, 99, false, true, false, false},
};

inline constexpr int calibrationCurveCount =
    static_cast<int>(sizeof(calibrationCurves) / sizeof(calibrationCurves[0]));

//==============================================================================
/** Una calibracion: las curvas, con sus puntos medidos.

    Se copia y se mueve como cualquier otra. La que se usa en el hilo de audio
    es una `const S950Calibration&` ya construida, y `read()` no reserva nada:
    interpolar dos puntos es aritmetica, no asignacion.
*/
class S950Calibration
{
public:
    //==========================================================================
    /** Una calibracion VACIA: todas las curvas declaradas y ninguna medida.

        Es la que se usa por defecto, y no es un caso degradado: es la respuesta
        correcta del repo a "todavia nadie ha medido esto". Un host la puede
        dejar puesta sin coste y sin riesgo, y todo lo que lea de ella sale
        `nullopt`. */
    static S950Calibration unmeasured()
    {
        return S950Calibration();
    }

    //==========================================================================
    /** La forma de una curva, medida o no. Nunca devuelve `nullptr`: un id
        invalido es un bug de compilacion, no una condicion de ejecucion. */
    static const CalCurveInfo& curveInfo(CalCurveId id) noexcept
    {
        return calibrationCurves[static_cast<int>(id)];
    }

    /** La forma de una curva por su codigo, o `nullptr`. */
    static const CalCurveInfo* findCurve(const char* code) noexcept
    {
        if (code == nullptr)
            return nullptr;

        for (int i = 0; i < calibrationCurveCount; ++i)
        {
            const char* a = calibrationCurves[i].code;
            const char* b = code;

            while (*a != '\0' && *a == *b)
            {
                ++a;
                ++b;
            }

            if (*a == *b)
                return &calibrationCurves[i];
        }

        return nullptr;
    }

    //==========================================================================
    /** Cuantos puntos medidos tiene una curva. CERO es lo normal hoy, y no es
        un error. */
    int pointCount(CalCurveId id) const noexcept
    {
        return static_cast<int>(points[static_cast<int>(id)].size());
    }

    /** Si una curva tiene lo MINIMO para ser usable: dos puntos, porque entre
        uno solo no se puede interpolar nada. Un punto es una medida suelta, que
        es informacion de una sesion de medicion, no una curva. */
    bool isMeasured(CalCurveId id) const noexcept { return pointCount(id) >= 2; }

    /** El punto i de una curva, o `nullptr` si no existe. */
    const CalPoint* pointAt(CalCurveId id, int index) const noexcept
    {
        const auto& p = points[static_cast<int>(id)];
        if (index < 0 || index >= static_cast<int>(p.size()))
            return nullptr;

        return &p[static_cast<std::size_t>(index)];
    }

    //==========================================================================
    /** El valor de una curva en un valor de panel.

        DEVUELVE `std::nullopt` CUANDO NO SE SABE, Y ESO ES LO IMPORTANTE. Un
        `nullopt` aqui no es un fallo del motor: es el motor diciendo "este
        numero no lo tenemos". Confundirlo con un 0 produce una envolvente
        instantanea, que es un click, y un motor que hace un click no tiene
        motivo para sospechar que el problema es de datos.

        @param stored   el valor de panel, ya recortado al rango de la curva por
                        quien llama; aqui no se recorta.
        @param allowExtrapolate  si se puede devolver un valor FUERA del rango
                        medido. Por defecto NO, y con razon: el extremo que
                        nadie ha medido es el que se rellena con una conjetura,
                        y una conjetura en un ataque no suena a ataque, suena
                        a error.

        @returns  el valor en la unidad de la curva, o `nullopt`.
    */
    std::optional<double> read(CalCurveId id, int stored,
                               bool allowExtrapolate = false) const noexcept
    {
        const auto& p = points[static_cast<int>(id)];

        if (p.size() < 2)
            return std::nullopt; // sin medir, o con un solo punto: no hay curva

        const bool log = calibrationCurves[static_cast<int>(id)].logarithmic;

        // UN PUNTO MEDIDO SE DEVUELVE EXACTO, Y PRIMERO DE TODO.
        //
        // Esta comprobacion va antes que la de los limites a proposito, y es el
        // fallo mas facil de cometer en esta funcion: un punto medido que cae
        // justo en el PRIMERO o en el ULTIMO de la lista es, a la vez, "el
        // extremo del rango" y "un valor medido". Si se mira primero el limite,
        // el primer y el ultimo punto de cada curva devolvian `nullopt` — es
        // decir, el motor decia "no lo se" sobre los dos unicos datos que si
        // que estaban medidos, y se comian el resto de la tabla.
        for (std::size_t i = 0; i < p.size(); ++i)
            if (p[i].stored == stored)
                return p[i].value;

        // Detras del primer punto medido.
        if (stored < p.front().stored)
        {
            if (!allowExtrapolate)
                return std::nullopt;

            return slopeTo(p, stored, log, 0);
        }

        // Delante del ultimo.
        if (stored > p.back().stored)
        {
            if (!allowExtrapolate)
                return std::nullopt;

            return slopeTo(p, stored, log, static_cast<int>(p.size()) - 1);
        }

        // Entre dos puntos medidos: lineal en log si el eje es log, lineal si
        // no. Nunca una ley de potencia, que no puede reproducir un escalon.
        for (std::size_t i = 1; i < p.size(); ++i)
        {
            if (stored > p[i].stored)
                continue;

            const double span = static_cast<double>(p[i].stored) - static_cast<double>(p[i - 1].stored);

            if (span <= 0.0)
                return p[i].value;

            const double t = (static_cast<double>(stored) - static_cast<double>(p[i - 1].stored)) / span;

            if (!log)
                return p[i - 1].value + t * (p[i].value - p[i - 1].value);

            // En log, los dos extremos tienen que ser positivos; si no lo son,
            // la curva se lee en lineal, que es peor pero no es un NaN.
            if (p[i - 1].value <= 0.0 || p[i].value <= 0.0)
                return p[i - 1].value + t * (p[i].value - p[i - 1].value);

            const double a = std::log(p[i - 1].value);
            const double b = std::log(p[i].value);

            return std::exp(a + t * (b - a));
        }

        return std::nullopt;
    }

    /** Igual que `read()`, pero con un valor de reserva en vez de `nullopt`.

        Para el caso en que el llamante GENUINAMENTE tiene un valor: un motor
        con una curva propia que quiere usar mientras la del S950 no esta
        medida. Y es peligroso a proposito —cambia un `nullopt` por un numero
        sin que nadie lo note—, asi que el nombre lo dice. */
    double readOr(CalCurveId id, int stored, double fallback,
                  bool allowExtrapolate = false) const noexcept
    {
        const auto v = read(id, stored, allowExtrapolate);
        return v.has_value() ? *v : fallback;
    }

    //==========================================================================
    /** Añade un punto medido. Devuelve `false` si el punto no vale, y explica
        por que en `why`: un arnés de medicion recibe basura —una sonda que se
        ha soltado, un numero que ha salido negativo— y aceptarla seria meter un
        error en la tabla sin que nadie lo supiera.

        Rechaza lo que de verdad no puede estar bien:
          - un `stored` fuera del rango declarado de la curva;
          - un valor que no es un numero, o que no es positivo en una curva de
            tiempo o de frecuencia, donde cero no es un tiempo;
          - un `stored` repetido con OTRO valor, que es el caso perverso: dos
            medidas del mismo ajuste que no coinciden. Se rechaza en vez de
            promediar, porque la media de dos medidas que se contradicen es un
            numero que no ha medido nadie.

        Un `stored` repetido con el MISMO valor se acepta y se ignora, que es lo
        que pasa cuando la misma lectura se pasa dos veces. */
    bool addPoint(CalCurveId id, int stored, double value,
                  const char** why = nullptr) noexcept
    {
        const auto& info = curveInfo(id);

        if (stored < info.storedLo || stored > info.storedHi)
        {
            if (why != nullptr) *why = "el valor de panel esta fuera del rango de la curva";
            return false;
        }

        if (!(value == value) || std::isinf(value))
        {
            if (why != nullptr) *why = "el valor medido no es un numero";
            return false;
        }

        if (info.positiveOnly && value <= 0.0)
        {
            if (why != nullptr) *why = "una curva de tiempo o de frecuencia no puede valer cero";
            return false;
        }

        auto& p = points[static_cast<int>(id)];

        for (const auto& existing : p)
        {
            if (existing.stored != stored)
                continue;

            if (existing.value == value)
                return true; // la misma lectura, dos veces

            if (why != nullptr) *why = "dos medidas del mismo ajuste no coinciden, y la media no la midio nadie";
            return false;
        }

        p.push_back(CalPoint{stored, value});

        // Ordenado por `stored`, siempre. Insertar en su sitio es O(n) y la tabla
        // tiene veinte puntos; ordenarlo al final obliga a `read()` a mirar en
        // un orden que alguien puede haber cambiado.
        std::sort(p.begin(), p.end(),
                  [](const CalPoint& a, const CalPoint& b) { return a.stored < b.stored; });

        return true;
    }

    /** Descarta todos los puntos de una curva. Para empezar de cero cuando una
        sesion de medicion se ha estropeado. */
    void clear(CalCurveId id) noexcept { points[static_cast<int>(id)].clear(); }

    /** Vacia todas las curvas. Devuelve la calibracion al estado de salida de
        fabrica, que es "nada medido". */
    void clearAll() noexcept
    {
        for (auto& p : points)
            p.clear();
    }

private:
    //==========================================================================
    /** El valor que tendria la curva en `atStored` si se siguiera la pendiente
        del par de puntos que ancla en `anchorIndex`.

        `atStored` es DONDE se pregunta y `anchorIndex` es el punto de partida.
        Son dos cosas distintas y confundirlas devuelve el valor del ancla, que
        es justo el bug que hizo la primera version de esto: `read()` pasaba el
        `stored` del primer punto en vez del que se le pedia, y toda
        extrapolacion devolvia el extremo sin moverse de el.

        Se extrapola EN LOG cuando la curva es logaritmica, porque es lo
        coherente con la interpolacion: una recta en un eje de tiempo que se
        corta en 0 es un tiempo negativo, y un tiempo negativo es un cambio de
        signo, que es un click. */
    double slopeTo(const std::vector<CalPoint>& p, int atStored,
                   bool log, int anchorIndex) const noexcept
    {
        const int n = static_cast<int>(p.size());

        // Un par de puntos: el del anclaje y su vecino. Al anclar en el ultimo
        // punto el vecino va hacia atras, que es lo unico que se puede.
        int a = anchorIndex;
        int b = anchorIndex + 1;

        if (b >= n)
        {
            a = anchorIndex - 1;
            b = anchorIndex;
        }

        if (a < 0 || b < 0 || a >= n || b >= n)
            return p[static_cast<std::size_t>(anchorIndex)].value;

        const double s0 = static_cast<double>(p[static_cast<std::size_t>(a)].stored);
        const double s1 = static_cast<double>(p[static_cast<std::size_t>(b)].stored);
        const double v0 = p[static_cast<std::size_t>(a)].value;
        const double v1 = p[static_cast<std::size_t>(b)].value;

        if (s1 == s0)
            return v0;

        const double t = (static_cast<double>(atStored) - s0) / (s1 - s0);

        if (!log || v0 <= 0.0 || v1 <= 0.0)
            return v0 + t * (v1 - v0);

        return std::exp(std::log(v0) + t * (std::log(v1) - std::log(v0)));
    }

    std::vector<CalPoint> points[static_cast<int>(CalCurveId::count)];
};

} // namespace abd::synth
