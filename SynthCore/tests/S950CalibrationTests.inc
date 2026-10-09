// ─────────────────────── S950Calibration ───────────────────────
//
// ESTE FICHERO NO TIENE NI UN NUMERO MEDIDO, Y LOS TESTS LO SABEN.
//
// La tabla de calibracion esta vacia a proposito (ver la cabecera de
// S950Calibration.h: una medicion pertenece a quien la midio, y la del estudio
// Mz950 es AGPL). Eso no es un TODO, es un estado, y lo que hay que provar aqui
// es que el estado se comporta: que "no medido" sale como desconocido y NUNCA
// como cero, que no se rellenan huecos sin permiso, y que el arnes de medicion
// funciona.
//
// LOS DATOS SON SINTETICOS Y SU VERDAD LA PONE ESTE FICHERO. Eso es lo que hace
// que el test del arnes valga sin depender de ninguna medicion real: se genera
// una curva que uno mismo se inventa, se le mete un analisis que la falsea de
// una forma conocida, y se comprueba que el arnes devuelve la curva original.
// Si la cadena de medicion estuviera rota, aqui se veria.

// La curva de verdad que este test se inventa. No es la del S950: es una
// exponencial limpia, para que el arnes tenga algo exacto que recuperar y el
// error que quede sea del arnes y no del dato.
static double truthEnvTime(int stored)
{
    return 0.010 * std::exp(0.06 * (double)stored);
}

// El analisis que mide, que no es perfecto: la ventana promedia y por eso
// ENGANA siempre en el mismo sentido: lee la verdad mas un 8%. Un sesgo
// de un solo sentido es el caso facil; el dificil son dos medidas del mismo
// ajuste que se contradicen, y ese esta en el test 7.
static double biasedRead(double truth)
{
    return truth * 1.08;
}

static void testS950Calibration()
{
    printf("=== S950Calibration ===\n");

    using namespace abd::synth;

    // ═══════ 1. LA TABLA NACE VACIA, Y ESO ES UN ESTADO CORRECTO ═══════

    {
        auto cal = S950Calibration::unmeasured();

        // Las seis curvas estan DECLARADAS aunque no haya ni un punto. La forma
        // sale del dominio del panel, que ya esta probado, y es lo que un panel
        // necesita para dibujarse los ejes.
        check(calibrationCurveCount == 6, "hay seis curvas declaradas");

        for (int i = 0; i < calibrationCurveCount; ++i)
        {
            const auto& c = calibrationCurves[i];
            char msg[160];
            snprintf(msg, sizeof(msg), "'%s' tiene codigo, nombre y unidad", c.code);
            check(c.code != nullptr && c.code[0] != '\0' && c.name != nullptr && c.name[0] != '\0' && c.unit != nullptr && c.unit[0] != '\0', msg);
            snprintf(msg, sizeof(msg), "'%s' cubre un rango de panel con lo<=hi", c.code);
            check(c.storedLo <= c.storedHi, msg);
        }

        for (int i = 0; i < calibrationCurveCount; ++i)
        {
            const auto& c = calibrationCurves[i];
            char msg[160];
            snprintf(msg, sizeof(msg), "'%s' no tiene ningun punto medido", c.code);
            check(cal.pointCount(c.id) == 0, msg);
            snprintf(msg, sizeof(msg), "'%s' no se declara medida sin puntos", c.code);
            check(!cal.isMeasured(c.id), msg);
        }
    }

    // ═══════ 2. LO QUE NO SE SABE ES DISTINTO DE CERO ═══════

    // ESTE ES EL TEST IMPORTANTE DEL FICHERO.
    //
    // Un `read()` que devolviera 0 por defecto seria un motor que pide el
    // attack y recibe "instantaneo", y una envolvente instantanea es un CLICK.
    // Peor: 0 es un numero, asi que el motor no tendria nada de sospechar. Un
    // motor que hace un click no sospecha de los datos, sospecha del motor.
    // Por eso lo desconocido tiene que ser un TIPO distinto del valor.
    {
        auto cal = S950Calibration::unmeasured();

        for (int i = 0; i < calibrationCurveCount; ++i)
        {
            const auto& c = calibrationCurves[i];
            const auto v  = cal.read(c.id, 50);
            char msg[160];
            snprintf(msg, sizeof(msg), "'%s' sin medir devuelve desconocido, no cero", c.code);
            check(!v.has_value(), msg);
        }

        // Y en TODO el rango, no solo en el punto medio.
        for (int s = 0; s <= 99; ++s)
        {
            if (!cal.read(CalCurveId::EnvelopeTime, s).has_value()) continue;
            check(false, "una curva vacia no puede devolver nada en ningun punto");
            break;
        }

        // Un punto suelto NO es una curva: entre uno solo no se interpola nada.
        check(cal.addPoint(CalCurveId::EnvelopeTime, 50, 0.3), "un punto se acepta");
        check(!cal.isMeasured(CalCurveId::EnvelopeTime), "pero un punto solo no es una curva");
        check(!cal.read(CalCurveId::EnvelopeTime, 50).has_value(),
              "y con un solo punto se sigue sin saber el valor");
    }

    // ═══════ 3. CON PUNTOS, LA CURVA RESPONDE ═══════

    {
        auto cal = S950Calibration::unmeasured();

        // Tres puntos con una verdad inventada y exacta.
        for (int s : {20, 50, 80})
            cal.addPoint(CalCurveId::EnvelopeTime, s, truthEnvTime(s));

        check(cal.isMeasured(CalCurveId::EnvelopeTime), "con tres puntos ya hay curva");
        check(cal.pointCount(CalCurveId::EnvelopeTime) == 3, "y son tres");

        // En los puntos medidos se lee EXACTO lo medido, no una interpolacion
        // que pase cerca. Un punto medido que se devuelve distinto es un dato
        // que ha cambiado de opinion.
        for (int s : {20, 50, 80})
        {
            const auto v = cal.read(CalCurveId::EnvelopeTime, s);
            char msg[160];
            snprintf(msg, sizeof(msg), "el punto medido %d se lee exacto", s);
            check(v.has_value() && *v == truthEnvTime(s), msg);
        }

        // Y entre ellos hay algo, y esa cosa esta entre los dos extremos: una
        // interpolacion que se sale del rango de sus vecinos es un defecto, no
        // una sorpresa.
        const auto mid = cal.read(CalCurveId::EnvelopeTime, 35);
        const auto lo  = cal.read(CalCurveId::EnvelopeTime, 20);
        const auto hi  = cal.read(CalCurveId::EnvelopeTime, 80);
        check(mid.has_value() && lo.has_value() && hi.has_value(),
              "entre dos puntos hay valor, y tambien en los extremos");
        if (mid && lo && hi)
            check(*mid >= *lo && *mid <= *hi, "el valor intermedio cae entre sus vecinos");

        // La curva del envelope BAJA con el byte: un attack mas grande dura mas.
        // Si un dia alguien invierte el signo, esta comprobacion lo dice.
        check(!S950Calibration::curveInfo(CalCurveId::EnvelopeTime).risesWithStored,
              "el tiempo de envolvente baja con el valor de panel");
        // Y el cutoff del filtro SUBE. Lo contrario seria un filtro al reves.
        check(S950Calibration::curveInfo(CalCurveId::FilterCutoff).risesWithStored,
              "el corte del filtro sube con el valor de panel");
        // Y el sustain es un nivel en dB: 0 es silencio y 99 es mas fuerte, asi
        // que el numero baja cuando el sonido sube. Es la unica que va al reves
        // de "mas byte, mas valor", y por eso se declara.
        check(!S950Calibration::curveInfo(CalCurveId::SustainDb).risesWithStored,
              "el sustain en dB baja cuando el nivel sube");
    }

    // ═══════ 4. NO SE EXTRAPOLA SIN PERMISO ═══════

    {
        auto cal = S950Calibration::unmeasured();
        for (int s : {40, 60})
            cal.addPoint(CalCurveId::EnvelopeTime, s, truthEnvTime(s));

        // Fuera del rango medido no se dice nada. El extremo que nadie ha
        // medido es el que se rellena con una conjetura, y una conjetura en un
        // ataque no suena a ataque: suena a error.
        check(!cal.read(CalCurveId::EnvelopeTime, 0).has_value(),
              "por debajo del primer punto medido no se inventa");
        check(!cal.read(CalCurveId::EnvelopeTime, 99).has_value(),
              "ni por encima del ultimo");
        check(cal.read(CalCurveId::EnvelopeTime, 50).has_value(), "pero entre los dos si");

        // Y si se pide, se extrapolay se dice. La extrapolar es legitima para
        // un motor que tiene su propia opinion; lo que no es legitimo es que
        // ocurra sin que nadie lo pidiera.
        const auto below = cal.read(CalCurveId::EnvelopeTime, 0, true);
        const auto first = cal.read(CalCurveId::EnvelopeTime, 40);
        check(below.has_value() && first.has_value(), "con permiso, por debajo hay valor");
        check(below && first && *below < *first, "y sigue la pendiente: hacia abajo es mas corto");

        const auto above = cal.read(CalCurveId::EnvelopeTime, 99, true);
        const auto last  = cal.read(CalCurveId::EnvelopeTime, 60);
        check(above.has_value() && last.has_value(), "con permiso, por encima hay valor");
        check(above && last && *above > *last, "y hacia arriba es mas largo");

        // Y la extrapolar en una curva logaritmica NUNCA puede dar un tiempo
        // negativo. Un tiempo negativo es un cambio de signo, y un cambio de
        // signo en una envolvente es un click. Que aqui no lo haya es el motivo
        // de interpolar en log y no en lineal.
        auto checkPositive = [&cal](int s) {
            const auto v = cal.read(CalCurveId::EnvelopeTime, s, true);
            return !v || *v > 0.0;
        };
        bool allPositive = true;
        for (int s = 0; s <= 99; ++s)
            if (!checkPositive(s))
            {
                allPositive = false;
                break;
            }

        check(allPositive, "ni extrapolando sale un tiempo negativo en todo el recorrido");
    }

    // ═══════ 5. LA INTERPOLACION ES EN LOG CUANDO EL EJE ES LOG ═══════

    {
        auto cal = S950Calibration::unmeasured();
        // Dos puntos cuya media aritmetica y cuya media geometrica se ven
        // bastante: 1 y 100 dan 50.5 y 10. Y el filtro declara eje log, asi que
        // tiene que salir 10.
        //
        // OJO con el punto medio: el eje de panel va de 0 a 99, y su punto medio
        // es 49.5, no 50. La primera version de este test leyo 50 y espero 10,
        // y recibio 10.19 — que no era un fallo de la interpolacion sino del
        // punto que se le pedia. Se mide en 49, que si es el punto medio.
        cal.addPoint(CalCurveId::FilterCutoff, 0, 1.0);
        cal.addPoint(CalCurveId::FilterCutoff, 98, 100.0);

        const auto v = cal.read(CalCurveId::FilterCutoff, 49);
        check(v.has_value(), "un filtro con dos puntos se lee");
        check(v && *v > 5.0 && *v < 20.0, "y en el punto medio sale la media GEOMETRICA, no la aritmetica");
        check(v && std::abs(*v - 10.0) < 0.01, "que para 1 y 100 son 10 exactos");

        // Y una curva NO logaritmica interpola en lineal. El LFO rate lo es:
        // una unidad es un numero fijo de hercios, y la media de 1 y 100 Hz a
        // media distancia SI son 50.5 Hz. Ponerlo en log seria el error
        // clasico de tratar todos los ejes como si fueran de tiempo.
        auto lin = S950Calibration::unmeasured();
        lin.addPoint(CalCurveId::LfoRate, 0, 1.0);
        lin.addPoint(CalCurveId::LfoRate, 98, 100.0);

        const auto w = lin.read(CalCurveId::LfoRate, 49);
        check(w.has_value() && std::abs(*w - 50.5) < 0.01,
              "el LFO interpola en lineal, porque su eje NO es de tiempo");
    }

    // ═══════ 6. LO QUE SE RECHAZA Y POR QUE ═══════

    {
        auto cal        = S950Calibration::unmeasured();
        const char* why = nullptr;

        // Un valor de panel fuera del rango DECLARADO de la curva. No es lo
        // mismo que fuera del rango medido: este lo sabemos de antemano, sin
        // mirar ningun punto.
        check(!cal.addPoint(CalCurveId::EnvelopeTime, 200, 0.5, &why),
              "un valor de panel fuera de 0..99 se rechaza");
        check(why != nullptr && why[0] != '\0', "y explica por que");

        // NaN e infinito: no son numeros, y un NaN en una tabla se propaga a
        // todo lo que la interpole.
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        check(!cal.addPoint(CalCurveId::EnvelopeTime, 50, nan, &why), "un NaN se rechaza");
        check(!cal.addPoint(CalCurveId::EnvelopeTime, 51, inf, &why), "un infinito se rechaza");

        // Cero en una curva de tiempo o de frecuencia. Cero no es un tiempo: es
        // un instante, y una envolvente de duracion cero es un click.
        check(!cal.addPoint(CalCurveId::EnvelopeTime, 50, 0.0, &why),
              "un tiempo de cero se rechaza: cero no es un tiempo, es un click");
        check(!cal.addPoint(CalCurveId::FilterCutoff, 50, -1.0, &why),
              "una frecuencia negativa se rechaza");

        // Y una curva donde el cero SI vale: los dB del sustain. El silencio es
        // un valor legitimo, y un filtro que lo rechazara no podria representar
        // un sustain apagado.
        auto db = S950Calibration::unmeasured();
        check(db.addPoint(CalCurveId::SustainDb, 0, -40.0), "en dB el valor se acepta");
        check(db.addPoint(CalCurveId::SustainDb, 99, 0.0), "y el 0 dB tambien");

        // Y el caso perverso: DOS MEDIDAS DEL MISMO AJUSTE QUE SE CONTRADICEN.
        // No se promedian. La media de dos medidas incompatibles es un numero
        // que no midio nadie, y una tabla con ese numero PARECE medida.
        auto dup = S950Calibration::unmeasured();
        dup.addPoint(CalCurveId::EnvelopeTime, 50, 0.3);
        check(!dup.addPoint(CalCurveId::EnvelopeTime, 50, 0.9, &why),
              "dos medidas del mismo ajuste que no coinciden se rechazan");
        check(why != nullptr && std::strstr(why, "no coinciden") != nullptr,
              "y el motivo dice que es una contradiccion, no un dato raro");
        check(dup.pointCount(CalCurveId::EnvelopeTime) == 1, "y no se cuela el segundo");

        // La misma lectura pasada dos veces SI se acepta: es la misma medida.
        check(dup.addPoint(CalCurveId::EnvelopeTime, 50, 0.3), "la misma lectura dos veces se acepta");
        check(dup.pointCount(CalCurveId::EnvelopeTime) == 1, "y no cuenta dos veces");

        // Y los puntos se quedan ordenados por `stored` sin que nadie lo pida,
        // porque `read()` los recorre en orden y un orden que puede cambiar es
        // una tabla que puede mentir.
        auto ord = S950Calibration::unmeasured();
        for (int s : {80, 20, 50})
            ord.addPoint(CalCurveId::EnvelopeTime, s, truthEnvTime(s));

        bool ascending = true;
        for (int i = 1; i < ord.pointCount(CalCurveId::EnvelopeTime); ++i)
            if (ord.pointAt(CalCurveId::EnvelopeTime, i)->stored <= ord.pointAt(CalCurveId::EnvelopeTime, i - 1)->stored)
                ascending = false;

        check(ascending, "los puntos quedan ordenados aunque se añadan desordenados");
    }

    // ═══════ 7. EL ARNÉS RECUPERA LA VERDAD DE DATOS SINTETICOS ═══════

    {
        using namespace abd::synth::harness;

        // Una sesion de verdad, con el fallo de verdad: el analisis engaña un
        // 8% en el mismo sentido, y el arnes tiene que quitarlo.
        Session s(CalCurveId::EnvelopeTime);

        // 1. El modelo conocido pasa por el mismo analisis. Aqui la verdad son
        //    0.0104 y 0.3 segundos, y el analisis los lee un 8% altos.
        s.addReference({
            {20, biasedRead(truthEnvTime(20)), truthEnvTime(20)},
            {50, biasedRead(truthEnvTime(50)), truthEnvTime(50)},
            {80, biasedRead(truthEnvTime(80)), truthEnvTime(80)},
        });

        check(s.hasBiasData(), "con referencia para cada ajuste, el sesgo se conoce");

        // 2. La maquina, con el mismo fallo del analisis, porque la maquina
        //    REAL tambien la pasa por una ventana.
        for (int v : {20, 50, 80})
            s.addObservation(v, biasedRead(truthEnvTime(v)), 1.0);

        // 3. Corregir. Y lo que sale tiene que ser la verdad, que este test
        //    CONOCE y por eso puede exigirlo.
        S950Calibration cal;
        const bool built = s.build(cal);

        check(built, "la sesion produce una curva");
        check(cal.isMeasured(CalCurveId::EnvelopeTime), "con sus tres puntos");

        for (int v : {20, 50, 80})
        {
            const auto got    = cal.read(CalCurveId::EnvelopeTime, v);
            const double want = truthEnvTime(v);
            char msg[160];
            snprintf(msg, sizeof(msg), "el punto %d se recupera con el error del analisis quitado", v);
            check(got.has_value() && std::abs(*got - want) / want < 0.01, msg);
        }

        // Y que la correccion ha movido los puntos DE VERDAD, no por casualidad:
        // sin sesgo habria un 8% de error, y aquí no lo hay.
        check(s.worstShift() > 0.07 && s.worstShift() < 0.09,
              "la correccion ha movido los puntos lo que el analisis falseaba");
        check(s.isConverged(0.10), "y la sesion ha convergido segun el criterio");

        // Y sin el modelo conocido NO hay correccion. Eso tiene que ser visible,
        // porque una tabla sin corregir que no lo dice es la peor de las
        // opciones: existe y parece buena.
        Session bare(CalCurveId::EnvelopeTime);
        for (int v : {20, 50, 80})
            bare.addObservation(v, biasedRead(truthEnvTime(v)), 1.0);

        check(!bare.hasBiasData(), "sin referencia, el sesgo NO se conoce");

        S950Calibration bareCal;
        check(bare.build(bareCal), "y aun asi produce una tabla");
        const auto uncorrected = bareCal.read(CalCurveId::EnvelopeTime, 50);
        const double want      = truthEnvTime(50);
        check(uncorrected.has_value() && std::abs(*uncorrected - want) / want > 0.05,
              "pero es una tabla SIN corregir, y el error se nota: un 8% fuera");
    }

    // ═══════ 8. EL ARNÉS NO ES CONVENCENTE CUANDO NO DEBE ═══════

    {
        using namespace abd::synth::harness;

        // Un analisis que falsea un 40%: la correccion mueve los puntos una
        // barbaridad, y una medicion movida un 40% no es una medicion. El
        // arnes tiene que poder decirlo, porque quien decide si la sesion vale
        // no puede ser el arnes.
        Session wild(CalCurveId::EnvelopeTime);
        wild.addReference({
            {20, truthEnvTime(20) * 1.40, truthEnvTime(20)},
            {50, truthEnvTime(50) * 1.40, truthEnvTime(50)},
        });
        for (int v : {20, 50})
            wild.addObservation(v, truthEnvTime(v) * 1.40, 1.0);

        // SIN `build()` NO HAY INFORME, y el desplazamiento sale de ahi. La
        // primera version de este test miraba `worstShift()` sin haber construido
        // nada, recibia 0 y decia que el desplazamiento era "0", que es
        // justo lo que un arnes roto diria. Construir antes de mirar.
        S950Calibration wildCal;
        wild.build(wildCal);

        // El desplazamiento NO es del 40% del analisis, sino de lo que la
        // correccion GUIENTA sobre lo observado: quitar un 40% de algo que
        // estaba un 40% arriba deja un 28.6% por debajo de lo leido. Es la
        // misma cuenta y por eso el umbral es 0.25 y no 0.30.
        check(wild.worstShift() > 0.25, "un analisis que falsea un 40% se ve en el desplazamiento");
        check(!wild.isConverged(0.10), "y la sesion NO ha convergido con el criterio normal");
        check(wild.isConverged(0.50), "pero converge con un criterio mas laxo, que es de quien mide");

        // Y el punto recuperado es la verdad: quitar el 40% de un 40% deja la
        // curva original, que es justo lo que se quiere de un arnes.
        const auto recovered = wildCal.read(CalCurveId::EnvelopeTime, 50);
        check(recovered.has_value() && std::abs(*recovered - truthEnvTime(50)) / truthEnvTime(50) < 0.01,
              "y aun asi recupera la verdad: el error era del analisis, no de la maquina");

        // Y una sesion con un solo ajuste medido: un punto no es una curva, y el
        // arnes tiene que decirlo en vez de devolver una tabla de un punto que
        // `read()` no podria usar.
        Session one(CalCurveId::EnvelopeTime);
        one.addReference({{50, 0.3, 0.25}});
        one.addObservation(50, 0.30, 1.0);

        S950Calibration cal;
        check(!one.build(cal), "una sesion de un solo ajuste no produce curva");
        check(!cal.isMeasured(CalCurveId::EnvelopeTime), "y la tabla sigue sin medir");
        check(!cal.read(CalCurveId::EnvelopeTime, 50).has_value(), "y leer sigue dando desconocido");
    }

    // ═══════ 9. EL ARNÉS PROPORCIONA LO QUE RECHAZA ═══════

    {
        using namespace abd::synth::harness;

        Session s(CalCurveId::EnvelopeTime);
        s.addReference({
            {20, 0.1, 0.1},
            {50, 0.5, 0.5},
            {80, 2.0, 2.0},
        });
        s.addObservation(20, 0.1, 1.0);
        s.addObservation(50, 0.5, 1.0);
        s.addObservation(80, 0.0, 1.0); // un tiempo de cero: no es un tiempo

        S950Calibration cal;
        check(!s.build(cal), "una sesion con un punto imposible no se da por buena");
        check(s.lastError() != nullptr, "y dice cual fue el problema");
        check(s.lastRejected().stored == 80, "y senala que ajuste fue");
        check(cal.pointCount(CalCurveId::EnvelopeTime) == 2,
              "los otros dos puntos siguen en la tabla: uno malo no tira los buenos");
    }
}
