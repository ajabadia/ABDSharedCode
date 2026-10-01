/*
  ==============================================================================

    DspMathHarness.cpp  (arnés de paridad y coste, NO parte del módulo)

    El MISMO archivo se compila de dos maneras:

      - NATIVO, con un `main` que imprime el informe y devuelve un código.
      - WASM, con `-DDspMathHarnessWasm`: sin `main`, sin `stdio`, sin `vector` y
        con una entrada `extern "C"` que deja el informe en un buffer de la
        memoria lineal del módulo, que es lo que lee `tools/dsp_math_wasm_parity.mjs`.

    Que sea UN archivo y no dos es lo que hace que la paridad signifique algo: si
    el caso nativo y el caso WASM fueran dos listas escritas a mano, bastaría con
    que alguien añadiera un caso a una y se olvidara de la otra para que la
    paridad dijera "OK" sin haber comparado nada.

    EL FORMATO, una línea por función:

        <nombre> <16 bits en hex> <ns neto> <x mediana> <presupuesto x> <veredicto>

    Los bits van PRIMERO y en hex, y no como decimales, a propósito. Un
    0.70710677 impreso no distingue un ulp de otra implementación, y lo que se
    quiere detectar es exactamente el ulp. El lado de node compara los 17
    primeros campos y se ignora el resto, que es lo que depende de la máquina.

    EL COSTE SE MIDE CON UN BLANCO, y esto no es un detalle. El bucle de medicion
    cuesta ~2 ns por iteración (cargar el caso, sumar, repetir), y sin restarlo
    una funcion que cuesta 2 ns aparece como 2 ns y una que no cuesta nada
    aparece IGUAL. Medido con el blanco restado, la diferencia entre `pow2i` y
    `pow` es de 2.3 a 38 ns; sin el, los dos salen a "lo que manda el bucle".

    Uso: lo llama `tools/run_dsp_math_harness.py`, que además lee el fuente y el
    objeto compilado. Compilado a mano:

        g++ -std=c++17 -O2 -I. DspCore/DspMathHarness.cpp -o harness && ./harness

  ==============================================================================
*/

#include "DspCore/DspMathHarness.h"

namespace
{

using namespace abd::dsp::harness;

// Cuantas veces se repite el bloque de casos. MEDIDO: con 20.000 el reloj (1 ms
// de granularidad) solo daba 4 tic y las cifras salian en escalones de 3.125 ns;
// con 1.000.000 el orden de las once funciones se repite entre corridas, que es
// lo unico que hace que el numero sirva. Se puede bajar por linea de comandos en
// la pata WASM, donde el coste se INFORMA pero no se usa de puerta.
#if ! defined (DspMathHarnessRepeticiones)
 #define DspMathHarnessRepeticiones 1000000
#endif
constexpr int kRepeticiones = DspMathHarnessRepeticiones;

//==============================================================================
/** Llama a la entrada `e` con el caso `i` y devuelve el resultado.

    Los dos argumentos se leen de las dos columnas de casos, y eso es lo que
    hace que `pow` se mida con exponente 0.5 en unos casos y 3.0 en otros: el
    mismo camino de código con las dos ramas de la asintota, que es donde
    empezarían a discrepar dos implementaciones. */
inline float evaluar (const Entrada& e, int i) noexcept
{
    const float x = kCasos[i];
    if (e.dos != nullptr)
        return e.dos (x, e.usaSegundoArgumento ? kCasosB[i] : kSegundoArgumento);

    return e.uno (x);
}

/** El bucle de medicion sin llamada a ninguna funcion: es el blanco que se resta.

    Se escribe como el gemelo exacto del bucle de arriba, con `kCasos[i]` en vez
    de `evaluar (e, i)`. Si divergieran, el blanco valdria otra cosa y el coste
    neto saldria negativo, que es como se nota si esto algún dia deja de ser el
    mismo bucle. */
inline float bucleEnBlanco (float& acumulador) noexcept
{
    for (int r = 0; r < kRepeticiones; ++r)
        for (int i = 0; i < kNumCasos; ++i)
            acumulador += kCasos[i];

    return acumulador;
}

/** El bucle de medicion de una entrada. */
inline float bucleDe (const Entrada& e, float& acumulador) noexcept
{
    for (int r = 0; r < kRepeticiones; ++r)
        for (int i = 0; i < kNumCasos; ++i)
            acumulador += evaluar (e, i);

    return acumulador;
}

/** Nanos por llamada. `milis` son milisegundos FRACCIONALES del reloj, que es lo
    que hay en las dos patas: en la de WASM el reloj del host tiene resolucion
    de submicrosegundo y truncarlo a entero habia gastado toda. */
inline double nanosPorLlamada (double milis) noexcept
{
    return milis * 1.0e6 / (double) (kRepeticiones * kNumCasos);
}

/** Mediana de once valores, por inserción.

    A mano y no con `std::sort` porque esta pata no tiene biblioteca estándar, y
    porque once elementos no justificanQuarter stack. */
float mediana (float* v, int n) noexcept
{
    for (int i = 1; i < n; ++i)
    {
        const float clave = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > clave) { v[j + 1] = v[j]; --j; }
        v[j + 1] = clave;
    }

    return v[n / 2];
}

} // namespace

//==============================================================================
/** El informe. Es lo unico que hacen las dos patas, y por eso no depende de
    como se haya compilado. */
static void construirInforme (Informe& r)
{
    float neto[kNumEntradas];
    double msMedidos[kNumEntradas];

    // 1. El blanco. Se mide una vez, antes que nada, y con el MISMO numero de
    //    repeticiones que las funciones: si se midiera con menos, la resta
    //    seria de dos cosas distintas y daria un neto negativo sin motivo.
    float basuraBlanco = 0.0f;
    const double t0 = ahoraMilis();
    volatile float sinking = bucleEnBlanco (basuraBlanco);
    const double t1 = ahoraMilis();
    const double nsBlanco = nanosPorLlamada (t1 - t0);
    (void) sinking;

    // 2. Cada función. El bucle se llama dentro de una función aparte a proposito:
    //    si el `for` estuviera pegado al `clock()`, el optimizador puede decidir
    //    que el bucle no tiene efectos y borrarlo, y la funcion "cuesta" 0 ns.
    for (int f = 0; f < kNumEntradas; ++f)
    {
        const double a = ahoraMilis();
        float basura = 0.0f;
        volatile float s = bucleDe (kManifiesto[f], basura);
        const double b = ahoraMilis();
        (void) s;

        msMedidos[f] = b - a;
        const double bruto = nanosPorLlamada (msMedidos[f]);
        double n = bruto - nsBlanco;
        if (n < 0.0) n = 0.0;      // por debajo del ruido del reloj, no hay mas
        neto[f] = (float) n;
    }

    // 3. La mediana, que es la referencia de todos los presupuestos.
    float copia[kNumEntradas];
    for (int f = 0; f < kNumEntradas; ++f) copia[f] = neto[f];
    const float medianaNeto = mediana (copia, kNumEntradas);

    // 4. El informe. El nombre va PRIMERO, para que una linea se pueda leer sin
    //    tener que contar, y los bits van justo detras porque son lo que se
    //    compara entre las dos patas y no dependen de la maquina.
    for (int f = 0; f < kNumEntradas; ++f)
    {
        const Entrada& e = kManifiesto[f];

        r.texto (e.nombre);

        for (int i = 0; i < kNumCasos; ++i)
        {
            r.anade (' ');
            r.bits (evaluar (e, i));
        }

        const float x = (medianaNeto > 0.0f) ? neto[f] / medianaNeto : 0.0f;

        r.anade (' ');
        r.numero (neto[f]);
        r.anade (' ');
        r.numero (x);
        r.anade (' ');
        r.numero (e.presupuestoX);
        r.anade (' ');

        // El veredicto solo existe si la corrida ha durado lo suficiente. Con
        // pocas repeticiones el reloj no resuelve y decir "ok" seria decir
        // "no he medido nada" con otra palabra.
        if (msMedidos[f] < kTiempoMinimoParaJuzgarMs)
            r.texto ("sin-medir");
        else
            r.texto (x > e.presupuestoX ? "SOBRE-PRESUPUESTO" : "ok");

        r.anade ('\n');
    }

    // Y una linea de pie con lo que hace falta para interpretar el resto.
    r.texto ("# blanco ");
    r.numero ((float) nsBlanco);
    r.texto (" ns  mediana ");
    r.numero (medianaNeto);
    r.texto (" ns  casos ");
    r.entero ((unsigned long) kNumCasos);
    r.texto ("  repeticiones ");
    r.entero ((unsigned long) kRepeticiones);
    r.anade ('\n');
}

//==============================================================================
#if defined (DspMathHarnessWasm)

extern "C" {

/** La entrada que llama el lado de node. Devuelve un puntero dentro de la
    memoria lineal del modulo; el .mjs lo lee como cadena UTF-8 hasta el NUL.

    Ojo con esto, que es la trampa de este tipo de arnés: el puntero SOLO vale
    hasta la siguiente llamada, porque el buffer se reutiliza. El lado de node
    tiene que leerlo ANTES de volver a llamar, y lo hace (una sola vez). */
const char* dspMathHarnessReport()
{
    static Informe informe;
    construirInforme (informe);
    return informe.datos();
}

} // extern "C"

#else

#include <cstdio>

int main()
{
    Informe informe;
    construirInforme (informe);

    if (informe.seHaLleno())
        std::printf ("[AVISO] el informe se ha llenado: faltan casos por medir\n");

    std::printf ("DspMathHarness %d funciones x %d casos\n", kNumEntradas, kNumCasos);
    std::printf ("%s", informe.datos());
    return 0;
}

#endif
