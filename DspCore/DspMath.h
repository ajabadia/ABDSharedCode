/*
  ==============================================================================

    DspMath.h
    Matematicas trascendentes DETERMINISTAS del sustrato compartido (abd::dsp).

    POR QUE EXISTE. La libm del sistema NO es la misma en todas las
    plataformas: medido en esta maquina (barrido de 400.000 argumentos),
    MSVC y musl/emscripten difieren en 1 ulp en 611/400.000 valores de
    sinf y 24.694/400.000 de atanf. En un lazo de realimentacion ese 1 ulp
    se amplifica por encima del presupuesto de 0 ulps de la paridad del
    motor (aparecia como 10 ulps en una muestra de la cola del chorus en el
    escenario C de Tests/neuronik_wasm_parity.mjs).

    La invariante del motor es paridad bit a bit nativo <-> WASM. La unica
    forma de sostenerla con trascendentes es calcularlas con una secuencia
    FIJA de operaciones IEEE-754 basicas (+ - * /), identica en los dos
    toolchains. Eso es este fichero.

    AVISO DE SONIDO: los valores NO son los de la libm de la plataforma.
    Sustituirla es un cambio de sonido deliberado y documentado (el motor es
    pre-1.0; ver HANDOFF.md). Se usa como REFERENCIA de precision en
    DspCoreTests.cpp, nunca como implementacion.

    CONTRATO DE EVALUACION. Para que nativo y WASM evalúen el polinomio igual
    hay que impedir la contraccion FMA (clang puede fusionar a*b+c en fmaf;
    MSVC /O2 no contrae). El target WASM compila con -ffp-contract=off (ver
    wasm/CMakeLists.txt). Sin esa bandera, este modulo no garantiza paridad.

    Precision MEDIDA (no estimada): con `DspCore/DspMathAudit.cpp`, que compara
    contra la libm de la plataforma barrido a barrido,

        sin [-8, 8]    max|err| = 3.6e-07      (3 ulp)
        cos [-8, 8]    max|err| = 4.2e-07      (3 ulp)
        atan [-64, 64] max|err| = 1.2e-07
        tanh [-8, 8]   max|err| = 1.2e-07
        log2 [1e-6,1e6] max|err| = 1.9e-06     (1 ulp; la magnitud del resultado es ~20)
        exp2 [-80, 80] 3 ulp
        pow(x, 2.5)    hasta ~27 ulp

    O sea que el objetivo de 1e-7 se cumplia en atan y tanh pero NO en sin ni cos,
    que se quedan en ~3 ulp: el error de Taylor de `sinPoly` en el borde del rango
    (|r| = pi/4) es justo el termino que se deja fuera, r^9/9!. Con 3 ulp no se
    oye en un LFO ni en una saturacion, asi que se DOCUMENTA la cifra real en vez
    de pagar con un termino mas del polinomio, que si cambiaba el sonido de todo
    lo que ya usa estas funciones.

    `pow` es el peor caso y por una razon que no es un fallo: se calcula como
    `exp2(y * log2(x))`, o sea que un error de 1 ulp en el logaritmo se multiplica
    por `y` y despues por `ln2` al volver a exponenciar. Un mapeo exponencial de
    frecuencia (20..8000 Hz, por ejemplo) tiene unos ~30 ulp de error relativo,
    3 partes por millon, que no es audible. Quien necesite mas tiene que usar
    `std::pow` en el host.

    CONTRATO DE NaN E INFINITO. Toda la familia propaga NaN: `sin(NaN)`,
    `cos(NaN)`, `atan(NaN)`, `log2(NaN)`, `tan(NaN)` y `tanh(NaN)` devuelven
    NaN, y lo mismo con `+inf` y `-inf` en `sin`/`cos`. No es una courtesy, es
    lo que hace que un fallo se vea: en un lazo de realimentacion un NaN no se
    va nunca, y es la unica pista de que un parametro llego roto. Antes `sin` y
    `cos` NO lo cumplian: `sin(NaN)` daba 0 y `cos(NaN)` daba 1, porque el
    plegado de la mantiza del rango grande lee los BITS del argumento
    (0x7fc00000 -> mascara -> 1.0f) y devolvia un numero eniendo. Medido. Un
    parametro en NaN entraba en el LFO del chorus, `sin` lo convertia en un 0
    perfecto, y el efecto seguia sonando NORMAL con la modulacion muerta en vez
    de delatar el fallo. Si anades una funcion a esta familia, que propague
    NaN: el plegado de la mantiza del rango grande es util, pero no puede ser
    la primera cosa que se encuentra un NaN.

    QUE HAY Y POR QUE. sin, cos y atan estaban porque el reverb de Freeverb y
    el chorus las necesitan. tanh, log2, exp2 y pow estan por los efectos
    compartidos de DspEffects: la saturacion de cinta, el puente de diodos del
    ring modulator y los mapeos EXPONENCIALES de frecuencia (p. ej. el ring
    mod mapea 0..1 a 20..8000 Hz con 20*400^t) son justo los sitios donde un
    1 ulp de libm se oye amplificado por un lazo de realimentacion, que es el
    mismo fallo que justifico este fichero. Se mantienen aqui y no en
    DspEffects porque son matematica, no politica de efecto: cualquier
    consumidor del sustrato que necesite un mapeo exponencial los tiene.

  ==============================================================================
*/

#pragma once

#include <cstdint>
#include <limits>

namespace abd::dsp
{

namespace dspmath_detail
{
constexpr float kPi        = 3.14159265358979323846f;
constexpr float kHalfPi    = 1.57079632679489661923f;
constexpr float kQuarterPi = 0.78539816339744830961f;
// pi/2 partido en dos floats (Cody-Waite): resta la cancelacion de q*pi/2
// sin perder precision. hi = (float)(pi/2); lo = pi/2 - hi en float.
constexpr float kHalfPiHi = 1.57079637050628662109375f;
constexpr float kHalfPiLo = -4.371139000186243e-08f;
/** tan(pi/8): frontera de la reduccion de atan. */
constexpr float kTanPi8 = 0.41421356237309504880f;

/** sin(r) para |r| <= pi/4 (Taylor, 4 terminos). */
inline float sinPoly(float r) noexcept
{
    const float r2 = r * r;
    float p        = -1.0f / 5040.0f;          // -1/7!
    p              = p * r2 + (1.0f / 120.0f); //  1/5!
    p              = p * r2 - (1.0f / 6.0f);   // -1/3!
    p              = p * r2 + 1.0f;
    return r * p;
}

/** cos(r) para |r| <= pi/4 (Taylor, 5 terminos). */
inline float cosPoly(float r) noexcept
{
    const float r2 = r * r;
    float p        = 1.0f / 40320.0f;          //  1/8!
    p              = p * r2 - (1.0f / 720.0f); // -1/6!
    p              = p * r2 + (1.0f / 24.0f);  //  1/4!
    p              = p * r2 - 0.5f;            // -1/2!
    p              = p * r2 + 1.0f;
    return p;
}

/** Redondeo a entero mas cercano, sin libm (empates hacia arriba en magnitud,
    consistente en los dos toolchains). */
inline int roundToInt(float v) noexcept
{
    return v >= 0.0f ? (int)(v + 0.5f) : (int)(v - 0.5f);
}

/**
    Argumentos hasta los que la reduccion en float aguanta. Por debajo se
    usa el camino de float de siempre, BIT A BIT IGUAL que antes, para que
    nada de lo que ya sonaba cambie al arreglar la cola.
*/
constexpr float kFloatReductionLimit = 16.0f;

/** Por encima de 2^52 ya no hay un valor "correcto" con el que comparar. */
constexpr float kReductionFoldsAbove = 4503599627370496.0f; // 2^52

/**
    Reduce `x` a [-pi/4, pi/4] y devuelve el cuadrante (`q & 3`).

    POR QUE HACE FALTA Y POR QUE EN DOBLE. En float, `x - q*pi/2` pierde
    ~ulp(x)/2, o sea que la reduccion se degrada PROPORCIONAL al argumento: el
    error no es un teclear constante sino que crece con `x`. Medido con
    `DspMathAudit.cpp` antes de arreglarlo:

        sin(1e6)  ->  -0.34024   (libm: -0.349993)   1% desviado
        cos(1e7)  ->  -0.750667  (libm: -0.907270)  17% desviado

    En doble la perdida es ~ulp_d(x)/2, y como el double tiene 52 bits de
    mantisa contra los 24 del float sobran 28: para cualquier float la
    reduccion en doble sale con toda la precision que el float pueda
    representar. Y sigue siendo determinista, porque la aritmetica IEEE en
    doble da el mismo resultado en nativo y en WASM: el contrato de "sin
    libm y con la misma secuencia de operaciones" se mantiene.

    Los tres rangos:
      - |x| <= 16: el camino de float de siempre, sin tocar un bit.
      - 16 < |x| <= 2^52: reduccion en doble, que ya coincide con la libm.
      - |x| > 2^52: TODOS esos float son enteros con espaciado >= 2^29, mucho
        mayor que 2*pi, asi que el resultado depende de unos ultimos bits que
        ya no estan en el argumento y no hay respuesta correcta que
        perseguir. Lo que si hay que evitar es el desbordamiento del
        casteo a entero (indefinido) y devolver basura, asi que el argumento
        se pliega a la mantiza baja, que es determinista y acotada.
*/
inline int reduce(float x, float& outR) noexcept
{
    const float absX = x < 0.0f ? -x : x;

    // NaN e infinito TIENEN que salir como NaN, y antes de las ramas de
    // rango. Sin esta guarda, `sin(NaN)` daba 0 y `cos(NaN)` daba 1: los dos
    // caian en el plegado de la mantiza de abajo, que lee los BITS del
    // argumento (0x7fc00000 -> mascara -> 1.0f), y el resultado era un
    // numero ENIENDO que no tiene nada que ver con la entrada. Medido:
    //
    //     sin(NaN) = 0        (libm: NaN)
    //     cos(NaN) = 1        (libm: NaN)
    //     sin(inf) = 0        (libm: NaN)
    //
    // Y eso no era un detalle teorico, era una TRAMPA DE AUDIO: un parametro
    // en NaN (un host que lo mande, un 0/0 en un mapeo) entraba en el LFO
    // del chorus, `sin` lo convertia en un 0 perfecto, y el efecto seguia
    // sonando NORMAL con la modulacion muerta en vez de poisoning el motor
    // y delatar el fallo. Un NaN silencioso es peor que un NaN ruidoso: en
    // un lazo de realimentacion el NaN es lo que te avisa, y aqui se
    // escondia. Ademas deja la familia INCOHERENTE consigo misma: `atan`,
    // `tan`, `tanh` y `log2` ya propagaban NaN, y `sin`/`cos` no.
    //
    // `!(absX <= max())` cubre NaN y los dos infinitos de una vez: NaN no
    // satisface NINGUNA comparacion, y el infinito es mayor que cualquier
    // cota. NO vale `!(absX >= 0)`, que es lo que se puso la primera vez y
    // solo cogia al NaN: el valor absoluto de +inf SI es >= 0, asi que
    // `sin(inf)` seguia dando 0 y `cos(inf)` dando 1. Medido antes de
    // corregirlo, que es la razon de la segunda vuelta.
    if (!(absX <= std::numeric_limits<float>::max()))
    {
        outR = std::numeric_limits<float>::quiet_NaN();
        return 0;
    }

    if (absX <= kFloatReductionLimit)
    {
        const int q = roundToInt(x / kHalfPi);
        outR        = x - (float)q * kHalfPiHi - (float)q * kHalfPiLo;

        return q & 3;
    }

    if (absX <= kReductionFoldsAbove)
    {
        constexpr double kHalfPiD = 1.5707963267948966;

        const double xd      = static_cast<double>(x);
        const double scaled  = xd / kHalfPiD;
        const double rounded = scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5;
        const long long q    = static_cast<long long>(rounded);

        outR = static_cast<float>(xd - static_cast<double>(q) * kHalfPiD);

        return static_cast<int>(q & 3LL);
    }

    // Plegado determinista: se quedan los 22 bits altos de la mantiza, que
    // dan un argumento en [1,2), y se entra por el camino de float.
    union
    {
        std::uint32_t asInt;
        float asFloat;
    } v;
    v.asFloat = x;
    v.asInt   = (v.asInt & 0x003fffffu) | 0x3f800000u;

    const float folded = v.asFloat - 1.0f; // [0, 1)

    const int q = roundToInt(folded / kHalfPi);
    outR        = folded - (float)q * kHalfPiHi - (float)q * kHalfPiLo;

    return q & 3;
}
} // namespace dspmath_detail

/** sin(x) determinista (sin libm). x en radianes. */
inline float sin(float x) noexcept
{
    float r;
    const int quadrant = dspmath_detail::reduce(x, r);

    switch (quadrant)
    {
        case 0:
            return dspmath_detail::sinPoly(r);
        case 1:
            return dspmath_detail::cosPoly(r);
        case 2:
            return -dspmath_detail::sinPoly(r);
        default:
            return -dspmath_detail::cosPoly(r);
    }
}

/** cos(x) determinista (sin libm). x en radianes. */
inline float cos(float x) noexcept
{
    float r;
    const int quadrant = dspmath_detail::reduce(x, r);

    switch (quadrant)
    {
        case 0:
            return dspmath_detail::cosPoly(r);
        case 1:
            return -dspmath_detail::sinPoly(r);
        case 2:
            return -dspmath_detail::cosPoly(r);
        default:
            return dspmath_detail::sinPoly(r);
    }
}

/** atan(x) determinista (sin libm).

    Reduccion por identidades (solo + - * /):
        atan(a) = pi/4 + atan((a-1)/(a+1))   para a > tan(pi/8)
    y, para |a| > 1, la misma identidad converge sola (maximo 2-3 pasos).
    El polinomio final es la serie de Taylor de atan (hasta t^15) sobre
    |t| <= tan(pi/8).
*/
inline float atan(float x) noexcept
{
    if (x != x)
        return x; // NaN entra, NaN sale

    const bool neg = x < 0.0f;
    float a        = neg ? -x : x;
    float angle    = 0.0f;

    // El infinito se atiene a +-pi/2. Sin esta guarda, el bucle de reduccion
    // calcula (inf-1)/(inf+1) = inf/inf = NaN, la guarda `NaN > tan(pi/8)` es
    // falsa y se sale con `a` ya en NaN. Medido antes de arreglarlo:
    // atan(inf) devolvia NaN. Y eso no es un NaN sterile: la saturacion esta
    // metida en un lazo de realimentacion, y un unico NaN lo envenena entero.
    if (a == std::numeric_limits<float>::infinity())
        return neg ? -dspmath_detail::kHalfPi : dspmath_detail::kHalfPi;

    while (a > dspmath_detail::kTanPi8)
    {
        a = (a - 1.0f) / (a + 1.0f);
        angle += dspmath_detail::kQuarterPi;
    }

    const float t2 = a * a;
    float p        = -1.0f / 15.0f;
    p              = p * t2 + (1.0f / 13.0f);
    p              = p * t2 - (1.0f / 11.0f);
    p              = p * t2 + (1.0f / 9.0f);
    p              = p * t2 - (1.0f / 7.0f);
    p              = p * t2 + (1.0f / 5.0f);
    p              = p * t2 - (1.0f / 3.0f);
    p              = p * t2 + 1.0f;

    const float result = angle + a * p;
    return neg ? -result : result;
}

//==============================================================================
/** atanh(t) determinista para |t| <= 1/3 (serie de potencias impares). */
inline float atanh(float t) noexcept
{
    // Horner sobre t^2 con los coeficientes 1/15, 1/13, 1/11, 1/9, 1/7, 1/5, 1/3
    // y el termino constante 1. TODOS positivos: atanh solo tiene potencias
    // impares, y el signo lo aporta la propia t.
    const float t2 = t * t;
    float p        = 1.0f / 15.0f;
    p              = p * t2 + 1.0f / 13.0f;
    p              = p * t2 + 1.0f / 11.0f;
    p              = p * t2 + 1.0f / 9.0f;
    p              = p * t2 + 1.0f / 7.0f;
    p              = p * t2 + 1.0f / 5.0f;
    p              = p * t2 + 1.0f / 3.0f;
    p              = p * t2 + 1.0f;

    return t * p;
}

/** 2^n como float, por su campo de exponente (sin libm, exacto por construccion).

    FUERA del rango del exponente del float no se puede construir por bits, y lo
    que sale de `(n + 127) << 23` no es un 2^n: son bits reinterpretados. Y el
    desbordamiento no se quedaba en "infinito", que al menos seria inofensivo.
    Medido con `DspCore/DspMathAudit.cpp` antes de arreglarlo:

        pow2i( 128) =  inf            (correcto, por casualidad)
        pow2i( 129) = -0.0            donde toca +inf   <-- SIGNO CAMBIADO
        pow2i( 200) = -1.39e-17       donde toca +inf
        pow2i(1000) =  5.96e-08       donde toca +inf   <-- magnitud INVENTADA
        pow2i(-127) =  0.0            donde toca 5.88e-39
        pow2i(-200) = -7.21e+16       donde toca 0.0

    Asi que se recorta al rango representable y se satura, que es lo que hace la
    libm: 2^n es +infinito para n >= 128 y 0 para n <= -150 (por debajo del
    denormal minimo, 2^-149, y 2^-150 cae justo en el punto medio que redondea a
    cero). Son comparaciones de enteros, no trascendentes, asi que el contrato de
    "solo + - * /" se respeta.

    Y hay una segunda mitad: 2^-127 y siguientes son DENORMALES (FLT_MIN = 2^-126),
    y un denormal no se construye poniendo el exponente a 0, porque eso es un
    cero. Se construye con el bit de MANTISA: 2^n = 2^(n+149) * 2^-149, o sea
    un 1 en la posicion n+149 del campo de mantiza. Medido antes de arreglarlo:
    pow2i(-127) daba 0.0 y pow2i(-128) daba -inf, donde tocaban 5.88e-39 y
    2.94e-39.
*/
inline float pow2i(int n) noexcept
{
    if (n >= 128) return std::numeric_limits<float>::infinity();
    if (n <= -150) return 0.0f;

    union
    {
        std::uint32_t asInt;
        float asFloat;
    } v;

    if (n < -126)
        v.asInt = 1u << static_cast<int>(n + 149); // denormal: 2^(n+149) * 2^-149
    else
        v.asInt = static_cast<std::uint32_t>(n + 127) << 23;

    return v.asFloat;
}

/** suelo entero exacto: trunca y corrige. Solo conversiones enteras, sin libm.
    `truncToInt` es un cast float->int, que trunca hacia cero y esta definido.
    El rango se recorta antes porque el cast fuera de int es UB. */
inline int floorToInt(float v) noexcept
{
    v = (v > 1.0e9f ? 1.0e9f : (v < -1.0e9f ? -1.0e9f : v));

    const int i = static_cast<int>(v);

    return (v < 0.0f && static_cast<float>(i) > v) ? i - 1 : i;
}

//==============================================================================
/** Envuelve un acumulador de fase en [0, twoPi) en TIEMPO CONSTANTE.

    POR QUE ESTA AQUI Y NO UN `while` EN CADA MOTOR. Los LFOs de DspEffects
    (chorus, anillo modulador) acumulan fase con un `if` de una sola resta, que
    solo envuelve mientras la incremento sea < 2*pi. Con `while` el envuelto
    funciona, pero su coste es PROPORCIONAL al numero de vueltas: con un rate de
    1e9 Hz son 22 676 iteraciones POR MUESTRA, y con rate infinito el bucle no
    termina nunca, en el hilo de audio. Este helper hace el mismo trabajo en un
    paso, restando de golpe las vueltas enteras.

    LA GARDA DE NaN NO ES COSMETICA. Con el `if`, una fase que se vuelve NaN se
    queda NaN PARA SIEMPRE (`NaN >= twoPi` es falso), y como la fase alimenta el
    retardo del chorus, un solo parametro raro de un host envenena el motor
    entero de forma permanente. Aqui NaN, infinito y desborde devuelven 0: el
    acumulador reinicia y el motor se recupera en la siguiente muestra.

    Medido: reproduce BIT A BIT el `if` de una sola resta en todo el rango de
    audio (rate 0..40 Hz a 44.1/48/96/192 kHz) y en el guion de la paridad
    congelada del chorus. Solo difiere donde el `if` ya era incorrecto: con rate
    = 2.18 veces el sample rate, el `if` dejaba la fase en 7.39 rad, fuera de
    [0, 2*pi).
*/
inline float wrapPhase(float phase, float twoPi) noexcept
{
    // Sin esta guarda, un `modulo` nulo o negativo daria division por cero.
    if (!(twoPi > 0.0f))
        return phase;

    const float turns = phase / twoPi;

    // `!(x < limite)` es tambien la forma de detectar NaN, que no se compara
    // bien con nada. Un acumulador que ya no es un numero vuelve a cero.
    if (!(turns > -1.0e9f && turns < 1.0e9f))
        return 0.0f;

    // Un paso: se resta el numero ENTERO de vueltas de golpe, no una a una.
    phase -= twoPi * static_cast<float>(floorToInt(turns));

    // El redondeo de la multiplicacion puede dejar la fase justo en el limite o
    // un pelo al otro lado; un solo paso mas la deja dentro, y no da una vuelta
    // extra porque por construccion esta a menos de una de cada extremo.
    if (phase >= twoPi)
        phase -= twoPi;
    else if (phase < 0.0f)
        phase += twoPi;

    return phase;
}

//==============================================================================
/** tanh(x) determinista (sin libm).

    Reduccion por la identidad del ANGULO DOBLE, tanh(2u) = 2t/(1+t^2): se
    reduce |x| a <= 0.5 evaluando la serie de Maclaurin, y luego se aplica la
    identidad hasta recuperar el argumento. Solo + - * /.

    Por encima de 8 devuelve +-1: tanh(8) = 0.99999977, y la saturacion de
    cinta/pedal nunca pasa de ahi, asi que el recorte no es audible y evita
    hasta veinte pasos de identidad.

    ARTEFACTO CONOCIDO. La reduccion por identidades de angulo double cambia
    de rama en |x| = 0.5, y alli la serie se trunca en un punto en una rama y
    en otro en la siguiente, de modo que hay un salto de ~1e-8. Es DOS ORDENES
    por debajo del presupuesto de ~1e-7 de este fichero e inaudible, asi que se
    documenta en vez de pagarse con mas polinomio. Lo fija
    `testDspMathExpLog` de DspCoreTests.cpp, que mide la monotonia con 1e-6 de
    tolerancia por este motivo (la imparidad, que sale de un signo y no de una
    serie, si se exige estricta). */
inline float tanh(float x) noexcept
{
    if (x > 8.0f) return 1.0f;
    if (x < -8.0f) return -1.0f;

    const float sign = x < 0.0f ? -1.0f : 1.0f;
    float a          = x < 0.0f ? -x : x;

    int halvings = 0;
    while (a > 0.5f)
    {
        a *= 0.5f;
        ++halvings;
    }

    // Maclaurin de tanh hasta x^15 sobre |x| <= 0.5 (error ~1e-8).
    const float a2 = a * a;
    float p        = -0.00145583439f;
    p              = p * a2 + 0.00359212804f;
    p              = p * a2 - 0.00886323553f;
    p              = p * a2 + 0.02186948854f;
    p              = p * a2 - 0.05396825397f;
    p              = p * a2 + 0.13333333333f;
    p              = p * a2 - 0.33333333333f;
    p              = p * a2 + 1.0f;

    float t = a * p;

    for (int i = 0; i < halvings; ++i)
        t = (2.0f * t) / (1.0f + t * t);

    return sign * t;
}

/** log2(x) determinista para x > 0 (sin libm).

    Se separa la potencia de dos por bits (m en [1,2)) y se evalua
    log2(m) = 2*log2(e) * atanh((m-1)/(m+1)), con t <= 1/3.

    Dominio: x > 0. Para lo demas (cero, negativo, NaN) devuelve -infinito, que
    es lo que le corresponde al cero y marca el error sin inventarse un numero.
    La comparacion con `!(x > 0)` es la que atrapa tambien el NaN.

    NO se escribe como `-1.0f / 0.0f`: MSVC lo rechaza en tiempo de compilacion
    (C2124) aunque la division solo se llegue a ejecutar si se cumple la guarda. */
inline float log2(float x) noexcept
{
    if (x != x)
        return x; // NaN entra, NaN sale. Antes devolvia -inf, que es PEOR: un
                  // NaN se convierte en un numero que parece real y sigue
                  // poisonando todo lo que toque a la vuelta.

    if (!(x > 0.0f))
        return -std::numeric_limits<float>::infinity();

    union
    {
        std::uint32_t asInt;
        float asFloat;
    } v;
    v.asFloat = x;

    int e = static_cast<int>(v.asInt >> 23) - 127;

    // Los DENORMALES tienen el campo de exponente a 0, con lo que `e` sale como
    // -127 y la mantiza [1,2) no representa al numero: falta el factor 2^-24.
    // Medido antes de arreglarlo: log2(1e-40) devolvia -126.99 cuando el valor
    // bueno es -132.88, casi 6 unidades de error justo en la region donde un
    // bucle de realimentacion se queda_small y con denormales.
    //
    // Multiplicar por 2^24 es EXACTO (es una potencia de dos) y saca cualquier
    // denormal al rango normal: el denormal mas pequeno es 2^-149 y 2^-149 *
    // 2^24 = 2^-125, que ya es normal (FLT_MIN = 2^-126).
    if (e == -127)
    {
        v.asFloat = x * pow2i(24);
        e         = static_cast<int>(v.asInt >> 23) - 127 - 24;
    }

    // Saca el 2^e: la mantisa queda en [1,2) para x normalizado.
    v.asInt = (v.asInt & 0x007fffffu) | 0x3f800000u;

    const float m = v.asFloat;
    const float t = (m - 1.0f) / (m + 1.0f);

    return static_cast<float>(e) + 2.8853900817779268f * atanh(t);
}

/** exp2(x) = 2^x determinista (sin libm).

    Reduccion x = n + f con f en [-0.5, 0.5]: 2^n se construye por bits y 2^f
    con la serie de Maclaurin de exp(f*ln2) (6 terminos, error ~2e-8).

    LA SATURACION NO SE PUEDE DELEGAR ENTERA en `pow2i`, y ese era el bug: con
    n = 128 el resultado puede ser FINITO (2^127.5 = 2.4e38) o infinito (2^128.5),
    y saturando `pow2i` se perdia el caso finito. Medido antes de arreglarlo:

        exp2(127.5) =  inf        donde toca 2.406e+38   <-- finito marcado como inf
        exp2(128.5) = -0.0        donde toca +inf       <-- SIGNO CAMBIADO
        exp2( 200 ) = -1.39e-17   donde toca +inf
        exp2(-200 ) = -7.21e+16   donde toca 0.0

    Asi que se parte n en dos: `nHi` se queda en el rango que `pow2i` construye
    bien y el resto se multiplica aparte. Asi el desbordamiento lo decide la
    multiplicacion en float, que es donde tiene que decidirse: 2^127 por
    1.414 da 2.4e38 (cabe) y por 2.83 da 4.8e38 (no cabe -> infinito).
*/
inline float exp2(float x) noexcept
{
    if (x != x)
        return x; // NaN entra, NaN sale

    const float n = static_cast<float>(floorToInt(x + 0.5f));
    const float f = x - n;

    // ln2^k / k!, ya CON el factorial dividido (2^f = exp(f*ln2)).
    constexpr float ln2   = 0.6931471805599453f;
    constexpr float ln2Sq = 0.2402265069591005f;  // ln2^2 / 2
    constexpr float ln2Cu = 0.0555041086648215f;  // ln2^3 / 6
    constexpr float ln2Qd = 0.00961812910762846f; // ln2^4 / 24
    constexpr float ln2Fi = 0.00133335581464284f; // ln2^5 / 120
    constexpr float ln2Sx = 0.00015403530393437f; // ln2^6 / 720

    float p = ln2Sx;
    p       = p * f + ln2Fi;
    p       = p * f + ln2Qd;
    p       = p * f + ln2Cu;
    p       = p * f + ln2Sq;
    p       = p * f + ln2;
    p       = p * f + 1.0f;

    // n cabe en int porque `floorToInt` recorta a +-1e9. Se parte en dos porque
    // `pow2i` solo construye bien [-149, 127].
    int nHi = static_cast<int>(n);
    int nLo = 0;

    if (nHi > 127)
    {
        nLo = nHi - 127;
        nHi = 127;
    }
    if (nHi < -150)
    {
        nLo = nHi;
        nHi = -150;
    }

    float result = p * pow2i(nHi);

    if (nLo != 0)
        result *= pow2i(nLo > 127 ? 127 : nLo);

    // Por debajo de 2^-150 no hay nada que representar, ni siquiera un denormal.
    return n < -150.0f ? 0.0f : result;
}

/** pow(x, y) determinista (sin libm) para x > 0: exp2(y * log2(x)).

    Con `y == 0` se devuelve 1 sin calcular nada, y no es una optimizacion: con
    una base negativa `log2` da -infinito y `0 * -inf` es NaN, o sea que sin
    esta guarda `pow(-1, 0)` devolvia NaN donde le toca 1. Este modulo no suelta
    NaN desde una entrada finita (ver `log2`), y el NaN en una realimentacion no
    se va nunca. Para `pow(400, 0)` el valor ya era 1 exacto, asi que esto no
    cambia ni un bit de lo que ya sonaba.
*/
inline float pow(float x, float y) noexcept
{
    if (y == 0.0f)
        return 1.0f;

    return exp2(y * log2(x));
}

} // namespace abd::dsp
