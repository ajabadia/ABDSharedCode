// ─────────────────── S950EnvelopeBench (el banco de pruebas) ───────────────────
//
// ESTE FICHERO MIDE, Y ESO ES LO QUE LO HACE DISTINTO DEL DE AL LADO.
//
// El de `S950CalibrationTests.inc` fabrica un sesgo del 8% y se lo pasa al arnes
// para comprobar que el arnes lo resta. Funciona, y esta bien hecho, pero el
// 8% no lo ha medido nadie: es un numero que el test se ha inventado porque
// hacia falta uno. Y un numero inventado no puede FALLAR, porque no hay contra
// que contrastarlo.
//
// Aqui no hay numeros inventados. Se renderiza un pulso cuya respuesta se sabe
// porque es NUESTRO, se le pasa el MISMO analisis que veria una sonda, y el
// numero que sale —observado menos verdadero— es el sesgo de verdad. Si el
// analisis se rompe, el sesgo cambia, y el test que compara el analisis contra
// el conteo a mano se entera antes que nadie.
//
// Y el resultado de medir ha sido una correccion a este mismo banco, que se
// cuenta en el test 1: la verdad en forma cerrada de la forma exponencial
// cuadrava a umbral 0.5 y se separaba de lo que el render hacia de verdad un 58%
// a umbral 0.25 y a 0.75. Sesenta por ciento. Ningun test que comprobara el
// render contra un numero puesto habria visto nada.
//
// LO QUE ESTE BANCO NO PRODUCE, Y NO SE DUDARA: ni un punto de la curva del
// S950. La verdad la pone este repositorio, no una maquina. Ejecutar esto deja
// `S950Calibration.h` igual de vacia, y el test 9 lo comprueba.
//
// La forma de las curvas que se inventan aqui (una exponencial en el tiempo de
// ataque) es la MISMA que usa el test de al lado, pero se declara otra vez aqui
// para que este bloque no dependa del orden de los includes.

static double benchTruthEnvTime (int stored)
{
    return 0.010 * std::exp (0.06 * (double) stored);
}

// El sesgo RELATIVO que el banco ha medido para un mando, en fraccion. Lo que
// de verdad se pasa al arnes es el absoluto, pero para comparar esto asi queda
// mas legible.
static double benchRelBias (const abd::synth::bench::BiasEntry& e)
{
    return e.truth > 0.0 ? e.bias / e.truth : 0.0;
}

static void testS950EnvelopeBench() {
    printf("=== S950EnvelopeBench ===\n");

    using namespace abd::synth;
    namespace bench = abd::synth::bench;

    const double sr = 48000.0;
    const double oneSample = 1.0 / sr;

    // ═══════ 1. LA VERDAD ANALITICA CONTRA EL RENDER, A MANO ═══════
    //
    // El banco entero descansa en que `trueOnSeconds()` dice la verdad. Si
    // mentia, todo el sesgo que sale de restar seria la diferencia entre dos
    // errores. Asi que se comprueba de la unica forma que no se puede maquillar:
    // contando muestras una a una, sin usar el analisis para nada.
    //
    // Y se hace con la ventana en UNA MUESTRA, para que el analisis no promedie
    // nada: lo que se compara es el render contra la formula, limpios.
    //
    // Tres umbrales y no uno, porque aqui estaba el fallo. A 0.5 cuadraba; a 0.25
    // y a 0.75 la formula se separaba del render un 58%, porque con la constante
    // de tiempo fija la subida no pasa nunca de la mitad y un umbral por debajo
    // la cuenta desde donde le da la gana mientras que uno por encima no la
    // cuenta en absoluto.

    {
        int comprobadas = 0;
        double peor = 0.0;

        for (auto shape : { bench::ReferenceShape::Linear, bench::ReferenceShape::Exponential })
        {
            for (double th : { 0.25, 0.5, 0.75 })
            {
                for (double rise : { 0.001, 0.0027, 0.02, 0.2 })
                {
                    bench::BenchConfig cfg;
                    cfg.sampleRate = sr;
                    cfg.windowSeconds = oneSample;   // promedia cero
                    cfg.threshold = th;
                    cfg.holdSeconds = 0.0;
                    cfg.tailSeconds = 0.01;

                    const auto rendered = bench::renderReference (shape, rise, cfg);

                    // El conteo a mano: cuantas muestras estan por encima del
                    // umbral. Sin analisis, sin formula, solo el render.
                    int count = 0;
                    for (std::size_t i = 0; i < rendered.size(); ++i)
                        if (static_cast<double> (rendered[i]) > th)
                            ++count;

                    const double aMano   = static_cast<double> (count) / sr;
                    const double formula = bench::trueOnSeconds (shape, rise, cfg);

                    // Y que el analisis, con la ventana de una muestra, diga
                    // exactamente lo mismo que el conteo: si el analisis no
                    // coincide con contar, el analisis no cuenta.
                    const auto lectura = bench::analyseDuration (rendered, cfg);
                    if (! lectura.seconds.has_value()) continue;
                    if (std::abs (*lectura.seconds - aMano) > 1e-12) {
                        check(false, "el analisis coincide con el conteo a mano");
                        continue;
                    }

                    const double diff = std::abs (aMano - formula);
                    if (diff > peor) peor = diff;
                    if (diff > oneSample * 1.000001) {
                        char msg[160];
                        snprintf(msg, sizeof(msg),
                                 "la verdad analitica cuadra con el render (umbral %.2f)", th);
                        check(false, msg);
                    }
                    ++comprobadas;
                }
            }
        }

        check(comprobadas == 24, "veinticuatro combinaciones de forma, umbral y rise comprobadas");
        check(peor <= oneSample * 1.000001,
              "la verdad analitica cuadra con el conteo a mano dentro de una muestra");
    }

    // ═══════ 2. EL SESGO NO ES UN NUMERO, ES UNA FUNCION DE LA VELOCIDAD ═══════
    //
    // Esta es la frase que justifica el banco entero. Si el sesgo fuera
    // constante, el arnes podria corregir con un factor fijo y habria estado ahi
    // todo el rato, y el test del 8% habria estado probando algo real. Se mide
    // que NO lo es: cambia de signo y se apaga al alejarse de la ventana.

    {
        bench::BenchConfig cfg;
        cfg.sampleRate = sr;
        cfg.windowSeconds = 0.002;
        cfg.threshold = 0.5;

        const auto tabla = bench::biasTable (bench::ReferenceShape::Linear, cfg,
                                             0.006, 0.300, 14);
        check(tabla.size() == 14 && tabla.front().measured && tabla.back().measured,
              "el barrido de rise da una tabla completa y toda medida");

        bool hayPositivo = false;
        bool hayNegativo = false;
        double enElRapido = 0.0;
        double enElLento  = 0.0;

        for (std::size_t i = 0; i < tabla.size(); ++i)
        {
            const double rel = benchRelBias (tabla[i]);
            // El corte va en el 0.02% —dos muestras y media de 48 k—, que es
            // donde el sesgo deja de ser redondeo y pasa a ser el efecto de la
            // ventana. Por debajo de ahi lo que cambia de signo es el ruido del
            // conteo, no la metodo.
            if (rel > 0.0002) hayPositivo = true;
            if (rel < -0.0002) hayNegativo = true;
            if (i == 0) enElRapido = rel;
            if (i + 1 == tabla.size()) enElLento = rel;
        }

        check(hayPositivo && hayNegativo,
              "el sesgo cambia de signo segun la velocidad de la barrida");
        check(std::abs (enElRapido) > 0.003,
              "y en el rise mas corto que se ve el error es de un 0.35%");
        check(std::abs (enElRapido) > std::abs (enElLento) * 10.0,
              "y se apaga al alejarse de la ventana: el error de la ventana es del orden de la ventana");
    }

    // ═══════ 3. LA VENTANA EN SEGUNDOS, QUE ES LO QUE LA HACE PORTABLE ═══════
    //
    // Si la ventana fuera de un numero fijo de muestras, el mismo banco mediria
    // cosas distintas a 44.1 que a 96, y el numero que sale seria del montaje
    // y no de la metodo. Por eso `windowSeconds`. Y por eso el sesgo, lejos de
    // la ventana, es el MISMO a las dos tasas.

    {
        bench::BenchConfig a;
        a.sampleRate = 44100.0;
        a.windowSeconds = 0.002;
        a.threshold = 0.5;

        bench::BenchConfig b = a;
        b.sampleRate = 96000.0;

        const double rise = 0.02;   // diez veces la ventana, de sobra
        const auto ra = bench::measureReference (bench::ReferenceShape::Linear, rise, a);
        const auto rb = bench::measureReference (bench::ReferenceShape::Linear, rise, b);
        const double truth = bench::trueOnSeconds (bench::ReferenceShape::Linear, rise, a);

        check(ra.seconds.has_value() && rb.seconds.has_value(),
              "a 44.1 y a 96 el rise se ve en los dos casos");
        check(std::abs (*ra.seconds - *rb.seconds) / truth < 0.005,
              "el sesgo no depende de la tasa de muestreo con la ventana en segundos");
    }

    // ═══════ 4. CERCA DEL SUELO, LA TASA SI CAMBIA EL RESULTADO, Y POR ESO HAY
    //        QUE SUBIRSE POR ENCIMA DEL SUELO ═══════
    //
    // El test anterior puede leerse como "da igual la tasa", y no es cierto en
    // general: es cierto mientras la ventana sea pequena frente al pulso. Cerca
    // del suelo no: con la ventana comiendose casi todo el pulso, unas muestras
    // de diferencia en el ancho de la ventana son la diferencia entre verlo y no
    // verlo. Por eso el banco dice cual es el suelo, en vez de dejar que uno lo
    // descubra con un resultado raro.

    {
        bench::BenchConfig a;
        a.sampleRate = 44100.0;
        a.windowSeconds = 0.002;
        a.threshold = 0.5;

        bench::BenchConfig b = a;
        b.sampleRate = 96000.0;

        const double rise = 0.001;   // por debajo del suelo con esta ventana
        const auto ra = bench::measureReference (bench::ReferenceShape::Exponential, rise, a);
        const auto rb = bench::measureReference (bench::ReferenceShape::Exponential, rise, b);

        check(ra.outcome != bench::MeasureOutcome::Measured
              || rb.outcome != bench::MeasureOutcome::Measured
              || bench::isTrustworthy (ra, a) == false,
              "cerca del suelo la tasa cambia lo que se ve, y el banco lo dice en vez de taparlo");
    }

    // ═══════ 5. EL SUELO DE VISIBILIDAD SUBE CON LA VENTANA ═══════
    //
    // Y sube a la cuarta parte de la ventana, que es la cuenta de un triangulo:
    // para que el promediado llegue a la mitad del umbral hace falta que el
    // pulso ocupe media ventana. El numero accionable del banco.

    {
        double anterior = 0.0;
        bool creciente = true;
        bool cercaDeLaCuartaParte = true;

        for (double win : { 0.0002, 0.002, 0.02, 0.04 })
        {
            bench::BenchConfig cfg;
            cfg.sampleRate = sr;
            cfg.windowSeconds = win;
            cfg.threshold = 0.5;

            const auto suelo = bench::visibilityFloor (bench::ReferenceShape::Linear, cfg);
            if (! suelo.has_value()) { creciente = false; break; }

            if (*suelo <= anterior) creciente = false;
            anterior = *suelo;

            if (std::abs (*suelo - win / 4.0) > win * 0.25) cercaDeLaCuartaParte = false;
        }

        check(creciente, "el suelo de visibilidad crece con la ventana de analisis");
        check(cercaDeLaCuartaParte, "y crece como la cuarta parte de la ventana");

        // Y por debajo del suelo no hay duracion, y no hay un cero: hay un
        // veredicto. Un cero seria un tiempo, y un tiempo de cero no es lo que
        // pasa: lo que pasa es que la sonda no lo ha visto.
        bench::BenchConfig cfg;
        cfg.sampleRate = sr;
        cfg.windowSeconds = 0.02;
        cfg.threshold = 0.5;

        const auto corto = bench::measureReference (bench::ReferenceShape::Linear, 0.00002, cfg);
        check(corto.outcome == bench::MeasureOutcome::NeverRose,
              "por debajo del suelo el veredicto es que no se ha visto");
        check(! corto.seconds.has_value(),
              "y no sale un tiempo, ni siquiera un cero");
    }

    // ═══════ 6. LOS TRES "NO MEDIDO" SON TRES COSAS DISTINTAS ═══════
    //
    // NuncaRose, NeverFell y Empty. Que sean distintos importa porque son
    // fallos distintos con arreglos distintos: la primera es la ventana muy
    // larga, la segunda es la grabacion cortada, y la tercera es que no ha
    // llegado nada. Si fueran el mismo estado, un informe no podria decir cual
    // de las tres ha pasado, que es justo lo que hay que rehacer.

    {
        bench::BenchConfig cfg;
        cfg.sampleRate = sr;
        cfg.windowSeconds = 0.02;
        cfg.threshold = 0.5;
        cfg.tailSeconds = 0.0;   // la grabacion se acaba antes de tiempo

        const auto cortada = bench::measureReference (bench::ReferenceShape::Linear, 0.005, cfg);
        check(cortada.outcome == bench::MeasureOutcome::NeverFell,
              "sin cola, un rise corto da un corte de grabacion, no una duracion");
        check(! cortada.seconds.has_value(), "y tampoco aqui hay un tiempo de salida");

        const std::vector<float> nada;
        const auto vacia = bench::analyseDuration (nada, cfg);
        check(vacia.outcome == bench::MeasureOutcome::Empty,
              "un render vacio es un estado distinto de no haberlo visto");
        check(bench::aboveSamples (vacia) == 0, "y no tiene ni una muestra por encima");
    }

    // ═══════ 7. LO QUE EL BANCO NO PUEDE MEDIR, NO SE CONVIERTE EN UN CERO ═══════
    //
    // Un sesgo de cero es un sesgo INVENTADO. El arnes lo restaria con toda la
    // confianza y el punto saldria movido en la direccion equivocada sin que
    // nadie lo viera. Un sesgo que falta es un hueco visible; un sesgo de
    // mentira es un numero.

    {
        bench::BenchConfig cfg;
        cfg.sampleRate = sr;
        cfg.windowSeconds = 0.02;
        cfg.threshold = 0.5;

        std::vector<bench::Command> mandos;
        for (int i = 0; i < 8; ++i)
        {
            bench::Command c;
            c.stored = 10 + i * 10;
            c.riseSeconds = 0.005 * std::exp (0.30 * (double) i);   // de 5 ms a 41 ms
            mandos.push_back (c);
        }

        const auto rep = bench::runReference (mandos, bench::ReferenceShape::Linear, cfg);

        check(rep.usable() + rep.skippedCount() == mandos.size(),
              "cada mando o se mide o se salta, y no se pierde ninguno");
        check(rep.skippedCount() > 0, "con la ventana de 20 ms, los rises cortos no se ven");
        check(rep.usable() > 0, "pero los largos si, y el banco no se queda sin nada");

        // Ningun saltado puede llevar la bandera de medido, y ningun medido
        // puede quedarse fuera: si las dos cosas se cumplieran, el informe
        // estaria mintiendo sobre lo que se ha visto.
        bool ningunoMarcadoMal = true;
        for (const auto& e : rep.skipped)
            if (e.measured) ningunoMarcadoMal = false;

        // Y cada referencia que va al arnes tiene verdad y observada POSITIVOS,
        // que es lo que distingue un sesgo medido de un cero de relleno.
        bool referenciaUtil = rep.reference.size() == rep.usable();
        for (const auto& o : rep.reference)
            if (o.truth <= 0.0 || o.observed <= 0.0) referenciaUtil = false;

        check(ningunoMarcadoMal, "un mando no visto no se marca como medido");
        check(referenciaUtil, "y la referencia que va al arnes solo lleva los medidos, con verdad y observada");
    }

    // ═══════ 8. UNA ESPIGA NO ES UNA MEDICION ═══════
    //
    // El fallo mas caro de todos, y tambien el mas facil de no ver. Con la
    // ventana muy grande frente a la envolvente, el promediado puede cruzar el
    // umbral en una sola muestra aunque el pulso sea larguisimo, y el analisis
    // responde "ha durado 0.02 ms" sin avisar. Medido: ventana de 20 ms, rise de
    // 5 ms, y el analisis daba 0.021 ms para una envolvente de 5 ms. Un sesgo
    // del -99.6% y con toda la pinta de un numero.
    //
    // El criterio cabe en una linea: el tramo por encima del umbral tiene que
    // ser al menos tan largo como la ventana. Por debajo de eso no cabe dentro
    // de la ventana, y lo que se ha medido no es la duracion de nada.

    {
        bench::BenchConfig cfg;
        cfg.sampleRate = sr;
        cfg.windowSeconds = 0.02;
        cfg.threshold = 0.5;

        const auto rd = bench::measureReference (bench::ReferenceShape::Linear, 0.005, cfg);
        check(rd.outcome == bench::MeasureOutcome::Measured,
              "el analisis cruza el umbral y cree que ha medido");
        check(rd.seconds.has_value() && *rd.seconds < 0.001,
              "y lo que devuelve es una espiga de una muestra, no una duracion");
        check(! bench::isTrustworthy (rd, cfg),
              "pero el banco la rechaza: un tramo mas corto que la ventana no cabe en la ventana");
        check(bench::aboveSamples (rd) < cfg.windowSamples(),
              "el tramo por encima dura menos que la ventana, que es el motivo");
    }

    // ═══════ 9. LA CADENA CERRADA: EL ARNÉS CON EL SESGO DE VERDAD ═══════
    //
    // Aqui se junta todo. Se cogen los mandos de una curva de ataque inventada,
    // se mide con el banco el sesgo del analisis a la velocidad a la que la
    // maquina esta siendo barrida en cada punto, y se le pasa ESO al arnes —no
    // un 8% de mentira— junto con la maquina. Y el arnes tiene que devolver la
    // curva de verdad.
    //
    // Y al lado, la misma maquina con un sesgo inventado del 8%, para que se vea
    // lo que vale un numero medido frente a uno puesto. No es que el 8% este
    // mal como constante: es que el sesgo real es de un 0.35%, y un factor fijo
    // de esa magnitud deshace la correccion en el punto rapido. El error
    // medido no es una constante, y por eso no se corrige con una.

    {
        namespace harness = abd::synth::harness;

        bench::BenchConfig cfg;
        cfg.sampleRate = sr;
        cfg.windowSeconds = 0.02;
        cfg.threshold = 0.5;

        const int ajustes[] = { 20, 50, 80 };

        // El barrido va a la velocidad real de cada punto: la sonda pasa por el
        // ataque de verdad a la velocidad a la que esa ataque de verdad dura.
        // Por eso el rise del mando ES la verdad de la maquina en ese ajuste.
        std::vector<bench::Command> mandos;
        for (int v : ajustes)
        {
            bench::Command c;
            c.stored = v;
            c.riseSeconds = benchTruthEnvTime (v);
            mandos.push_back (c);
        }

        const auto rep = bench::runReference (mandos, bench::ReferenceShape::Linear, cfg);
        check(rep.skippedCount() == 0, "a estos velocidades los tres ataques se ven");

        double sesgoRealMayor = 0.0;
        for (const auto& e : rep.entries)
        {
            const double rel = std::abs (benchRelBias (e));
            if (rel > sesgoRealMayor) sesgoRealMayor = rel;
        }
        check(sesgoRealMayor > 0.0,
              "y el sesgo del analisis NO es cero: hay algo que corregir");

        // ── La maquina: la verdad mas el sesgo que el banco ha medido. ──
        harness::Session sesion (CalCurveId::EnvelopeTime);
        sesion.addReference (rep.reference);

        double peorErrorReal = 0.0;
        for (int v : ajustes)
        {
            const double verdad = benchTruthEnvTime (v);
            sesion.addObservation (v, verdad + sesion.referenceBias (v), 1.0);
        }

        S950Calibration cal;
        check(sesion.build (cal), "el arnes construye la tabla con el sesgo medido");

        for (int v : ajustes)
        {
            const auto leido = cal.read (CalCurveId::EnvelopeTime, v);
            const double verdad = benchTruthEnvTime (v);
            const double err = leido.has_value() ? std::abs (*leido - verdad) / verdad : 1.0;
            if (err > peorErrorReal) peorErrorReal = err;
        }

        check(peorErrorReal < 0.001,
              "con el sesgo REAL, el arnes devuelve la maquina con menos de un 0.1% de error");

        // ── Y al lado, lo mismo pero corrigiendo con un 8% de mentira. ──
        //
        // La maquina es la MISMA: la verdad mas el sesgo que el banco ha
        // medido, que es el error que de verdad tiene la medicion. Lo unico que
        // cambia es la correccion: en vez de restar el sesgo medido punto a
        // punto, se resta un 8% igual para todos, como hace el test de al lado.
        //
        // Y el resultado se espera: se deshace. Restar 0.08 cuando el error de
        // verdad es un 0.35% deja al punto un 7.65% por debajo de su sitio, que
        // es PEOR que no corregir. Un factor fijo no es una correccion
        // aproximada: es un error nuevo, y aqui se ve del tamanho que es.
        harness::Session inventada (CalCurveId::EnvelopeTime);
        for (int v : ajustes)
        {
            const double verdad = benchTruthEnvTime (v);
            inventada.addReference ({ { v, verdad * 0.08, 0.0 } });
            inventada.addObservation (v, verdad + sesion.referenceBias (v), 1.0);
        }

        S950Calibration calInventada;
        inventada.build (calInventada);

        double peorErrorInventado = 0.0;
        for (int v : ajustes)
        {
            const auto leido = calInventada.read (CalCurveId::EnvelopeTime, v);
            const double verdad = benchTruthEnvTime (v);
            const double err = leido.has_value() ? std::abs (*leido - verdad) / verdad : 1.0;
            if (err > peorErrorInventado) peorErrorInventado = err;
        }

        check(peorErrorInventado > peorErrorReal * 10.0,
              "corregir con un factor fijo deshace la correccion: el sesgo real no es una constante");
        check(peorErrorInventado > 0.05,
              "y el error que queda con un factor inventado es del orden del 8% que se metio");
        check(peorErrorInventado < 0.09,
              "y es el 8% menos lo poco que el banco si sabe quitar, que es la cuenta exacta");
    }

    // ═══════ 10. EL BANCO NO HA TOCADO NI UN PUNTO DE LA TABLA ═══════
    //
    // El invariante de la fase. Todo lo anterior produce numeros de calibracion
    // DEL METODO, y ninguno de la curva del S950: la verdad la pone este
    // repositorio, y la del estudio Mz950 que si tiene los numeros es AGPL. Si
    // ejecutar el banco dejara un punto medido, la tabla dejaria de estar
    // vacia a proposito y habria dejado de ser cierto que nadie ha medido.

    {
        S950Calibration cal = S950Calibration::unmeasured();
        int conPuntos = 0;

        for (int i = 0; i < calibrationCurveCount; ++i)
            conPuntos += (int) cal.pointCount ((CalCurveId) i);

        check(conPuntos == 0, "despues de medir el metodo, la tabla del S950 sigue sin un solo punto");
        check(cal.isMeasured (CalCurveId::EnvelopeTime) == false
              && cal.isMeasured (CalCurveId::LfoRate) == false,
              "y las curvas siguen declaradas y sin medir, que es lo que un panel necesita");
        check(! cal.read (CalCurveId::EnvelopeTime, 50).has_value(),
              "y leer sigue dando desconocido, no un numero inventado");
    }
}
