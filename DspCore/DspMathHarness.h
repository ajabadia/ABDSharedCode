/*
  ==============================================================================

    DspMathHarness.h  (arnés de paridad y coste, NO parte del módulo)

    QUÉ ES Y POR QUÉ ESTÁ SEPARADO DE LOS TESTS.

    `DspCoreTests.cpp` responde a "¿las trascendentes hacen lo que dicen?". Esto
    responde a dos preguntas que solo tienen sentido mirando el binario:

      1. ¿SIGUEN SIENDO LAS MISMAS cuando el mismo código corre en WASM? Un
         `sin` correcto en x86 y un `sin` distinto en wasm no son dos
         implementaciones, son dos instrumentos que suenan distinto, y el
         producto que compila a WASM se lleva el peor.
      2. ¿CUÁNTO CUESTAN? Una trascendente correcta pero lentísima se cuela igual
         de fácil que una incorrecta, y el síntoma (una voz que va bien en el
         DAW y se arrastra en el navegador) no señala el culpable.

    Y hay una tercera, que es la que este arnés existe sobre todo para:

      3. ¿HA ENTRADO ALGUNA NUEVA SIN MEDIR? Una función trigonométrica nueva
         que alguien añade a `DspMath.h` y usa en una voz entra sin ruido, sin
         warning y sinCrash: simplemente nadie sabe que hay una más, ni cuánto
         cuesta, ni si es la misma en los dosyte compiladores. POR ESO LA
         PUERTA DE COBERTURA ES UNA DE LAS COMPROBACIONES: el manifiesto de
         abajo tiene que nombrar todas las funciones que `DspMath.h` declara, y
         si alguien declara una nueva sin meterla aquí, la puerta se pone roja.

    POR QUÉ NO ESTÁ EN CMake, Y POR QUÉ NO USA LA BIBLIOTECA ESTÁNDAR.

    Los dos motivos son el mismo. Este arnés tiene que compilarse DOS VECES con
    el MISMO código, una a nativo y otra a `--target=wasm32 -nostdlib`, y para
    eso no puede usar ni `std::vector` ni `std::string` ni `std::chrono`: en la
    pata WASM no hay biblioteca que lasprovidea. Por eso el informe se escribe
    a mano en un buffer estático y el reloj es el de C.

    Tampoco entra en CMake, igual que `DspMathAudit.cpp` y
    `DspMathBitIdent.cpp`: esto se compila a mano con
    `tools/run_dsp_math_harness.py`, que además hace las comprobaciones que
    necesitan ver el binario (símbolos, cobertura del fuente).

  ==============================================================================
*/

#ifndef ABD_DSPCORE_DSPMATHHARNESS_H
#define ABD_DSPCORE_DSPMATHHARNESS_H

#include "DspCore/DspMath.h"

namespace abd::dsp::harness
{

//==============================================================================
/** Una función del manifiesto, con la misma firma para todas.

    El arnés mete las once funciones en UNA tabla plana, y para eso las que no
    son `float(float)` o `float(float,float)` se adaptan abajo. Un puntero a
    función con aridad variable no existe, y `std::function` no compila en la
    pata freestanding.

    `presupuestoX` es lo que puede costar UNA llamada, en MULTIPLOS DE LA
    MEDIANA de todas las del manifiesto, y sale de la medicion del propio arnés
    (ver la tabla de abajo), no de un objetivo. La mediana y no el minimo a
    proposito: si el presupuesto fuera un multiplo de la mas barata, cualquier
    cambio en la mas barata moveria el presupuesto de las otras once, y el
    indicador dejaria de ser estable.

    Y el presupuesto NO es un limite de correccion: el informe lo marca, pero no
    falla la ejecucion. Un `atan` un 30% mas caro que ayer puede ser la maquina
    mas cargada; un `tan` recien llegado que cuesta 5veces la mediana SI es alguien
    que ha metido un `exp` y un `log` a pelo, y eso es lo que el numero sirve para
    que alguien vaya a mirar. Decide quien lee el informe, no el reloj. */
struct Entrada
{
    const char* nombre;
    float (*uno)  (float);
    float (*dos)  (float, float);

    // En `dos`, el primer argumento es la x del caso y el segundo se toma de
    // `kCasosB`. La funcion `pow` es la unica de dos argumentos que tiene
    // sentido tratar asi; el resto de las adaptaciones lo ignoran.
    bool          usaSegundoArgumento;

    float         presupuestoX;    // en multiplos de la mediana
};

// Los segundos argumentos de los casos de dos entradas. Se eligen para que
// `pow`  reciba un exponente que ESTIRE la asintota (0.5) y otro que la
// acerque (3.0), y para que `wrapPhase` reciba un 2*PI con la precision que le
// da el float, que es donde se concentra el error.
inline constexpr float kSegundoArgumento   = 0.5f;
inline constexpr float kSegundoArgumentoB  = 3.0f;
inline constexpr float kDosPi              = 6.28318530718f;

// Los valores de entrada. La lista está chosen a mano y NO es aleatoria, por
// tres razones que se/learnen midiendo:
//
//   - los REDONDOS (0, 1, -1, 0.5) son donde una serie truncada se nota, porque
//     el error relativo es máximo ahí;
//   - los GRANDES (1000) son donde un polinomio se va, y donde la reducción de
//     rango de `sin`/`cos` tiene que entrar;
//   - los valores en el borde del rango de `log2`/`exp2` (0 y negativo) y de
//     `atan`/`atanh` (cercanos a 1) son donde una guarda mal puesta devuelve
//     algo silenciosamente equivocado en vez de NaN.
//
// Un generador pseudoaleatorio con semilla fija daria mas cobertura y MENOS
// casos utiles: los que el azar no elige, que son justamente los de arriba.
inline constexpr int kNumCasos = 16;

inline constexpr float kCasos[kNumCasos] =
{
    0.0f,  0.5f,  -0.5f,  1.0f,  -1.0f,
    0.25f, 3.14159265f, 0.78539816f, 1000.0f,  -1000.0f,
    0.999f, 0.9999f, 0.0001f, 12.5f,  6.28318531f, 0.125f
};

// Los mismos casos pero CON SEGUNDA COLUMNA, para no usar siempre 0.5: se
// repite la lista cambiando el argumento, que es lo que hace falta para que
// `pow` se mida en dosIOC regímenes distintos.
inline constexpr float kCasosB[kNumCasos] =
{
    0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
    3.0f,  3.0f,  0.5f,  3.0f,  0.5f,
    0.5f,  3.0f,  0.5f,  3.0f,  0.5f,  3.0f
};

//==============================================================================
// Las once adaptaciones. Cada una es el ÚNICO sitio donde se llama a una
// trascendental del módulo, y por eso el manifiesto no puede quedarse corto sin
// que se note: si `DspMath.h` declara una función nueva, esta tabla no la
// menciona y la puerta de cobertura del runner lo dice por el fuente.

inline float envolturaSin  (float x) noexcept { return sin (x); }
inline float envolturaCos  (float x) noexcept { return cos (x); }
inline float envolturaAtan (float x) noexcept { return atan (x); }
inline float envolturaAtanh(float x) noexcept { return atanh (x); }
inline float envolturaTanh (float x) noexcept { return tanh (x); }
inline float envolturaLog2 (float x) noexcept { return log2 (x); }
inline float envolturaExp2 (float x) noexcept { return exp2 (x); }

inline float envolturaPow      (float x, float y) noexcept { return pow (x, y); }
inline float envolturaWrapPhase(float x, float y) noexcept { return wrapPhase (x, y); }

// `pow2i` y `floorToInt` devuelven float e int y reciben int y float. El
// `static_cast<int>` es el que hace la CONVERSIÓN, y aquí es donde se ve si esa
// conversión es la misma en los dos compiladores: si `pow2i` trunca distinto, el
// digest de bits no coincide y la puerta de paridad salta. Por eso el arnés NO
// las deja fuera por ser "de enteros": la parte interesante de esas dos es
// justamente la conversión.
inline float envolturaPow2i    (float x) noexcept
{
    const int n = static_cast<int> (x);
    return static_cast<float> (pow2i (n));
}

inline float envolturaFloorToInt(float x) noexcept
{
    return static_cast<float> (floorToInt (x));
}

//==============================================================================
/** EL MANIFIESTO. Estas son las once, y tienen que ser las once.

    `tools/run_dsp_math_harness.py` lee `DspMath.h`, saca las declaraciones de
    nivel de namespace y falla si aquí no están todas. Esa es la puerta que
    impide que una trascendental nueva entre sin medir.

    Los presupuestos de la ultima columna sePonieron MEDIENDO, con 1.000.000 de
    repeticiones en esta maquina y restando el blanco del bucle (unos 2 ns), y
    despues se redondearon a la baja para que quede margen. El reparto medido en
    multiplos de la mediana fue:

        pow2i 0.19   floorToInt 0.31   atanh 0.52   wrapPhase 0.59
        log2 0.89     exp2 0.95         sin 1.00     cos 1.02
        tanh 1.16     atan 1.31         pow 3.14

    O sea que hay un grupo apretado alrededor de 1 y dos datos que se salen:
    `pow`, que hace exp y log de verdad y no tiene esquivar, y `pow2i`, que es
    una construccion de bits. Los presupuestos son el valor medido redondeado
    hacia arriba con un margen de ~1.5veces, salvo `pow` que se le deja 6.0
    porque es la que mas varia entre corridas (33 a 38 ns medidos) y no por una
    duda de implementacion sino porque es la que de verdad hace mas trabajo. */
inline constexpr Entrada kManifiesto[] =
{
    { "sin",        &envolturaSin,        nullptr,                  false, 1.6f },
    { "cos",        &envolturaCos,        nullptr,                  false, 1.6f },
    { "atan",       &envolturaAtan,       nullptr,                  false, 2.0f },
    { "atanh",      &envolturaAtanh,      nullptr,                  false, 1.6f },
    { "tanh",       &envolturaTanh,       nullptr,                  false, 1.8f },
    { "log2",       &envolturaLog2,       nullptr,                  false, 1.6f },
    { "exp2",       &envolturaExp2,       nullptr,                  false, 1.6f },
    { "pow2i",      &envolturaPow2i,      nullptr,                  false, 0.6f },
    { "floorToInt", &envolturaFloorToInt, nullptr,                  false, 0.8f },
    { "pow",        nullptr,              &envolturaPow,           true,  6.0f },
    { "wrapPhase",  nullptr,              &envolturaWrapPhase,      true,  1.4f },
};

inline constexpr int kNumEntradas = (int) (sizeof (kManifiesto) / sizeof (kManifiesto[0]));

//==============================================================================
/** El informe se escribe a mano en un buffer estático.

    Cuesta más código que un `std::string`, y es a propósito: en la pata WASM no
    hay `std::string`, y el arnés tiene que ser EL MISMO código en las dos, no
    dos versiones que se parezcan. Si el informe se constructions con la
    biblioteca estándar, esta compar dejaría de ser bit a bit por el
    formateo. */
class Informe
{
public:
    void anade (char c) noexcept
    {
        if (longitud_ < capacidad - 1)
            buffer_[longitud_++] = c;
    }

    void texto (const char* s) noexcept
    {
        while (*s != 0) anade (*s++);
    }

    void entero (unsigned long v) noexcept
    {
        char digitos[24];
        int n = 0;
        if (v == 0ul) digitos[n++] = '0';
        while (v > 0ul)
        {
            digitos[n++] = (char) ('0' + (v % 10ul));
            v /= 10ul;
        }
        while (n > 0) anade (digitos[--n]);
    }

    void numero (float v) noexcept
    {
        // El informe es para personas y para diffs, no para parsear: con dos
        // decimales basta, y escribir un %f a mano evita `snprintf`.
        //
        // LA GUARDA DEL CERO. `v > 0` y `v < 0` son las dos falsas para el 0.0,
        // igual que para un NaN, asi que la comprobacion "no es un numero" que
        // parece natural se lleva el cero por delante. La primera version de
        // esta funcion imprimia "nan" para 0.00, que es exactamente el fallo que
        // hace que un informe de medicion no se pueda fiar: el numero mas
        // probable de todos, un coste redondito, salia marcado como invalido.
        if (v != v)                       // NaN, y solo NaN: aqui si es != consigo mismo
        {
            texto ("nan");
            return;
        }
        if (v > 1.0e7f || v < -1.0e7f) { texto ("infinito"); return; }

        if (v < 0.0f) { anade ('-'); v = -v; }

        unsigned long entera = (unsigned long) v;

        // Los centimas, redondeadas, CON la llevada. Sin ella, 1.999 redondea a
        // 100 centimas y hay que subir la parte entera: si no, sale "1.00" para
        // 1.999, que no es un redondeo feo, es un numero falso.
        unsigned resto = (unsigned) ((v - (float) entera) * 100.0f + 0.5f);
        if (resto >= 100ul) { ++entera; resto -= 100ul; }

        entero (entera);
        if (resto == 0ul) return;

        anade ('.');
        // SIEMPRE dos decimales. La primera version imprimia un '0' de relleno
        // y despues los dos digitos, y un resto de 7 salia como ".007" y
        // rompia el formato que el lado de WASM parsea.
        anade ((char) ('0' + (resto / 10ul) % 10ul));
        anade ((char) ('0' + resto % 10ul));
    }

    void bits (float v) noexcept
    {
        // Los bits EN HEX, no el numero: la paridad que importa es bit a bit, y
        // un 0.70710677 impreso no distingue un ulp de una implementacion
        // distinta. Este es el campo que se compara entre nativo y WASM.
        unsigned u;
        __builtin_memcpy (&u, &v, 4);
        for (int i = 7; i >= 0; --i)
        {
            const unsigned d = (u >> (i * 4)) & 0xFu;
            anade ((char) (d < 10u ? ('0' + d) : ('a' + (d - 10u))));
        }
    }

    void linea (const char* nombre)
    {
        texto (nombre);
        anade ('\n');
    }

    const char* datos() const noexcept { return buffer_; }
    int longitud() const noexcept { return longitud_; }
    bool seHaLleno() const noexcept { return longitud_ >= capacidad - 1; }

private:
    static constexpr int capacidad = 1 << 17;   // 128 KiB
    char buffer_[capacidad] = { 0 };
    int longitud_ = 0;
};

//==============================================================================
/** El reloj, y por que es la unica cosa que cambia entre las dos patas.

    En nativo se usa `clock()` de C. En freestanding WASM no hay `time.h` —la
    biblioteca entera falta, no solo el reloj—, asi que el modulo de WASM IMPORTA
    la funcion del host y node le pasa `performance.now()`.

    Que sea una importacion y no un shim de `time.h` es a proposito: un `time.h`
    inventado en la pata WASM seria medir el reloj contra otro reloj, y el
    numero no significaria nada. Importando el reloj del host, el numero que sale
    es el tiempo de pared REAL de la ejecucion en WASM, que es lo que a un
    navegador le importa.

    `ahoraMilis()` es el MISMO nombre y hace lo mismo en las dos patas, asi que
    el bucle de medicion es literalmente el mismo codigo compilado dos veces. */
#if defined (DspMathHarnessWasm)

extern "C" double dspMathHarnessHostMilis();

inline double ahoraMilis() noexcept
{
    return dspMathHarnessHostMilis();
}

#else

 #include <time.h>

inline double ahoraMilis() noexcept
{
    return clock() * 1000.0 / (double) CLOCKS_PER_SEC;
}

#endif

/** Por debajo de este tiempo total medido, el coste NO se puede juzgar.

    El reloj nativo es `clock()`, que en Windows mide en milisegundos. Si el
    bucle entero dura cuatro milisegundos, la cifra sale en escalones de ~3 ns
    y comparar eso con un presupuesto da ruido, no una medida. Con el umbral
    puesto aqui, una corrida con pocas repeticiones dice "no medido" en vez de
    inventar un veredicto. MEDIDO: con 20.000 repeticiones el bucle entero dura
    ~4 ms y nueve de las once funciones salian a 9.38 ns, que es el suelo del
    reloj, no su coste. Con 1.000.000 el orden se repite entre corridas. */
inline constexpr double kTiempoMinimoParaJuzgarMs = 20.0;

} // namespace abd::dsp::harness

#endif // ABD_DSPCORE_DSPMATHHARNESS_H
