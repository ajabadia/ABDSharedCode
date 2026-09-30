// ────────────── S950Calibration: un renderer con la tabla vacía ──────────────
//
// EL OTRO LADO DE "NO MEDIDO", Y POR QUE NO ES LO MISMO QUE "CLICK".
//
// `S950Calibration.h` esta vacia a proposito y los tests de al lado ya dicen que
// eso es un estado correcto. Aqui se prueba la consecuencia que ahi no se ve:
// que un renderer que CONSULTA esa tabla, con la tabla vacia, se degrada a
// SILENCIO, y no a un click.
//
// Y la palabra importante es "degrada", no "suena". Un click no es un sonido
// raro: es un error que el motor no tiene motivo para sospechar. Si el motor
// hace un click, el oido oye un fallo del motor y nadie mira la tabla. Si el
// motor no suena, el oido oye silencio y la primera pregunta es "¿esta cargado
// el instrumento?", que es la pregunta CORRECTA. Lo que hay que evitar no es el
// ruido: es el ruido que se disfraza de averia.
//
// QUE SE MIDE, Y COMO. Un click no es una cosa abstracta: es un SALTO entre dos
// muestras consecutivas. Una envolvente es continua —pasa por todos los
// niveles— asi que el salto maximo entre muestras de una envolvente bien puesta
// lo fija la pendiente del ataque mas rapido posible. En una tabla vacia, el
// salto es de la amplitud entera: de 0 a 1 en una muestra. Eso se mide, no se
// argumenta: el renderer devuelve el salto maximo y el test lo mira.
//
// ASI QUE HAY DOS RENDERERS, Y SON EL MISMO CODIGO CON UNA POLITICA.
//
// No es un truco para tener un fallo y su contraparte: es que la decision de que
// hacer con un dato que no se sabe es JUSTO eso, una politica, y la politica es
// la unica linea de codigo de la que depende toda la diferencia entre click y
// silencio. Poner las dos en el mismo sitio, con la misma tabla, es lo que
// demuestra que la diferencia la hace la politica y no otra cosa.
//
// LO QUE NO ES. Este renderer no es codigo de motor: es un doble de prueba, y
// por eso vive aqui y no en un .h. Lo que se prueba no es SU calidad de sonido,
// que es la de un linea recta, sino el CONTRATO: que la tabla vacia produce una
// ausencia con nombre, y que un llamante que la respeta no puede producir un
// click aunque quiera. El renderer real, si algun dia existe, tendra que hacer
// lo mismo, y este test es el que se lo comprueba.

static const float kRenderAmplitude = 1.0f;

// Que hace un renderer con un valor de panel que la tabla NO sabe convertir.
//
// La enumeracion esta porque el fallo que se quiere evitar no es un valor mal
// calculado: es un valor que no se ha calculado y ha salido como si sí.
enum class UnknownPolicy
{
    RequireKnown,   ///< no se sabe -> no se toca la nota. Silencio, y se dice.
    ZeroFill        ///< no se sabe -> se llena con 0. Un 0 de tiempo es un click.
};

//==============================================================================
/** Un renderer minimo que consulta la tabla de calibracion.

    Es un attack-decay de una sola nota: sube de 0 a la amplitud en el tiempo que
    diga la tabla, y baja en la mitad de ese tiempo. Nada de sustain, nada de
    release, nada de velocidad. Solo lo justo para que un ataque mal resuelto se
    vea en los datos, que es lo que se va a mirar.

    Y devuelve tres cosas, no una: los samples, el salto maximo entre muestras
    consecutivos, y cuantos valores de panel no ha podido resolver. La segunda
    es la medida del click y la tercera es la de la honestidad, que son
    preguntas distintas y conviene poder hacer las dos. */
struct EnvelopeRender
{
    std::vector<float> samples;
    double  maxStep = 0.0;      ///< el mayor salto entre muestras consecutivas
    int     unknown = 0;        ///< valores de panel que la tabla no ha resuelto
    bool    played  = false;    ///< si ha tocado algo
};

static EnvelopeRender renderFromCalibration (const abd::synth::S950Calibration& cal,
                                             int stored, double sampleRate,
                                             int sampleCount, UnknownPolicy policy)
{
    EnvelopeRender out;

    if (sampleCount <= 0 || sampleRate <= 1000.0)
        return out;

    // El vector se dimensiona SIEMPRE, mida o no mida. Un renderer que devuelve
    // menos samples de los que le han pedido no es un renderer que se calla: es
    // un renderer que haNano cosas de las que el llamante no se entera, y eso
    // es peor que un click porque desplaza el audio un rato y nadie lo oye.
    out.samples.assign (static_cast<std::size_t> (sampleCount), 0.0f);

    // Lo unico que se le pide a la tabla: cuanto dura el ataque de este mando.
    const auto attackOpt = cal.read (abd::synth::CalCurveId::EnvelopeTime, stored);

    if (! attackOpt.has_value())
    {
        // ── AQUI ESTA LA DIFERENCIA ENTRE CLICK Y SILENCIO ──
        //
        // Un valor de ataque DESCONOCIDO. Lo que no se puede es dejar que salga
        // como un numero sin comprobar. Con `ZeroFill` sale un ataque de cero
        // segundos, y un ataque de cero no es un ataque: es un salto de la
        // amplitud entera en una muestra, que es exactamente un click. Con
        // `RequireKnown` no se toca la nota y se cuenta el desconcierto.
        ++out.unknown;

        if (policy == UnknownPolicy::ZeroFill)
        {
            // Un ataque de cero segundos NO es un ataque muy corto: es la
            // amplitud entera entre dos muestras consecutivas. La primera
            // muestra se queda en silencio —el audio estaba parado antes de la
            // nota— y la siguiente ya esta a fondo. Ese par es el click, y por
            // eso la primera no se pone a amplitud: si se pusiera, el vector
            // entero seria constante, su salto maximo seria cero, y este test
            // estaria "midiendo" un click que no esta en los datos.
            //
            // Que sedontenara asi no es un descuido del doble: es el modo de
            // fallo tipico, que es dejar el valor ya calculado al principio de
            // la cadena y que el ataque valga lo que valga.
            if (out.samples.size() > 1)
                out.samples[1] = kRenderAmplitude;

            out.played = true;
        }

        for (std::size_t i = 1; i < out.samples.size(); ++i)
        {
            const double salto = std::abs (static_cast<double> (out.samples[i])
                                       - static_cast<double> (out.samples[i - 1]));
            if (salto > out.maxStep) out.maxStep = salto;
        }

        return out;
    }

    const double attack = *attackOpt;

    // Un ataque no medido que salga en cero o menos seguira siendo un click, y
    // el motivo es que la tabla lo permite: `read()` no es quien tiene que
    // impedirlo, porque su unico deber es no mentir. Un ataque de cero es
    // instantaneo, y lo instantaneo es audible.
    const double decay = attack * 0.5;

    const int rise = static_cast<int> (std::ceil (attack * sampleRate));
    const int fall = static_cast<int> (std::ceil (decay * sampleRate));

    out.played = true;

    for (int n = 0; n < sampleCount; ++n)
    {
        float level = 0.0f;

        if (rise <= 0)
        {
            // Un ataque de cero que ha llegado hasta aqui. Se dibuja, y luego
            // el test ve el click. No se corrige a mano: si se corrigiera aqui,
            // el test estaria probando que el renderer sabe sortear el fallo, y
            // lo que hay que probar es que el fallo llega hasta el audio.
            level = kRenderAmplitude;
        }
        else if (n < rise)
        {
            level = kRenderAmplitude * (static_cast<float> (n) / static_cast<float> (rise));
        }
        else if (fall <= 0 || n < rise + fall)
        {
            const float t = fall <= 0 ? 0.0f
                : static_cast<float> (n - rise) / static_cast<float> (fall);
            level = kRenderAmplitude * (1.0f - t);
        }

        out.samples[static_cast<std::size_t> (n)] = level;
    }

    for (int n = 1; n < sampleCount; ++n)
    {
        const double salto = std::abs (static_cast<double> (out.samples[static_cast<std::size_t> (n)])
                                   - static_cast<double> (out.samples[static_cast<std::size_t> (n - 1)]));
        if (salto > out.maxStep) out.maxStep = salto;
    }

    return out;
}

static void testS950CalibrationRender() {
    printf("=== S950Calibration: renderer con la tabla vacia ===\n");

    using namespace abd::synth;

    const double sr = 48000.0;
    const int muestras = 4800;   // 100 ms, de sobra para ver un ataque

    // ═══════ 1. LA TABLA ESTA VACIA, Y ESO SE COMPRUEBA PUNTO A PUNTO ═══════
    //
    // Antes de mirar el audio, el dato. Las seis curvas declaradas, ninguna
    // medida, y `read()` sin valor en NINGUN mando de NINGUNA curva. Es lo
    // primero que tiene que ser verdad, porque todo lo de abajo viene de ahi.

    {
        S950Calibration cal = S950Calibration::unmeasured();
        int puntosTotales = 0;
        int sinMedir = 0;

        for (int i = 0; i < calibrationCurveCount; ++i)
        {
            const auto id = static_cast<CalCurveId> (i);
            const auto& info = cal.curveInfo (id);

            puntosTotales += cal.pointCount (id);
            if (! cal.isMeasured (id)) ++sinMedir;

            // Todos los mandos del rango declarado, sin excepcion.
            for (int stored = info.storedLo; stored <= info.storedHi; ++stored)
                if (cal.read (id, stored).has_value()) sinMedir = -1000;
        }

        check(puntosTotales == 0, "la tabla que se entrega no tiene ni un punto en ninguna curva");
        check(sinMedir == calibrationCurveCount,
              "y las seis curvas dan desconocido en todos sus mandos, sin una sola excepcion");
    }

    // ═══════ 2. UN RENDERER QUE EXIGE SABER: SILENCIO ═══════
    //
    // El caso bueno. Con la tabla vacia, el renderer no toca la nota, devuelve
    // silencio exacto y dice cuantos mandos no ha podido resolver. Tres cosas
    // distintas, y las tres importan: el silencio evita el click, el cero exacto
    // evita el ruido de un valor "casi cero", y el contador hace que lo
    // infradato sea DIAGNOSTICABLE en vez de anonymously mudo.

    {
        S950Calibration cal = S950Calibration::unmeasured();

        const auto r = renderFromCalibration (cal, 50, sr, muestras, UnknownPolicy::RequireKnown);

        check(r.samples.size() == static_cast<std::size_t> (muestras),
              "el renderer entrega todos los samples, los haya puesto o no");
        check(! r.played, "con la tabla vacia no toca la nota");
        check(r.unknown == 1, "y dice exactamente cuantos mandos no ha podido resolver");

        bool todoCero = true;
        bool algunoRaro = false;

        for (float v : r.samples)
        {
            if (v != 0.0f) todoCero = false;
            // Un NaN o un infinito aqui seria un fallo peor que un click: es un
            // fallo que se propaga al resto de la cadena y no se oye aqui.
            if (! std::isfinite (v)) algunoRaro = true;
        }

        check(todoCero, "y el audio es silencio EXACTO, no un valor pequeno que se oye poco");
        check(! algunoRaro, "y no hay ningun NaN ni infinito que se propague al resto de la cadena");
        check(r.maxStep == 0.0, "y el salto maximo entre muestras es cero, que es lo que aleja el click");
    }

    // ═══════ 3. EL MISMO RENDERER, LA MISMA TABLA, Y LA OTRA POLITICA: CLICK ═══════
    //
    // Aqui esta el motivo entero de este fichero. Mismo codigo, misma tabla, un
    // `?:` de diferencia. Y la diferencia no es de tono: es de CONTINUIDAD. Un
    // ataque de cero segundos no es un ataque muy corto, es la amplitud entera
    // entre dos muestras consecutivas.

    {
        S950Calibration cal = S950Calibration::unmeasured();

        const auto bueno = renderFromCalibration (cal, 50, sr, muestras, UnknownPolicy::RequireKnown);
        const auto malo  = renderFromCalibration (cal, 50, sr, muestras, UnknownPolicy::ZeroFill);

        check(malo.played && ! bueno.played,
              "con la politica de rellenar con cero, el renderer creyese que sabe el ataque");
        check(malo.unknown == bueno.unknown,
              "y los dos saben igual de poco: el desconcierto es el mismo, solo cambia que se hace");
        check(malo.samples.size() > 1
              && malo.samples[0] == 0.0f
              && malo.samples[1] == kRenderAmplitude,
              "y de la primera muestra a la siguiente se salta de 0 a la amplitud entera");
        check(malo.maxStep == static_cast<double> (kRenderAmplitude),
              "el salto maximo es de la amplitud entera: eso es un click, medido");
        check(malo.maxStep > bueno.maxStep * 1000.0,
              "y es mil veces mayor que el del renderer que sabe que no sabe");
    }

    // ═══════ 4. POR QUE UN SALTO GRANDE ES UN CLICK Y NO UN SONIDO ═══════
    //
    // Para que el numero de arriba no sea una opinion, hace falta un criterio
    // independiente de la amplitud: la pendiente de la envolvente. Una envolvente
    // CONTINUA no puede saltar mas de lo que la pendiente de su ataque mas
    // rapido permite, porque la pendiente es justo lo que fija el ataque. Con
    // una tabla medida y un ataque de verdad, el salto sale pequeno; con la
    // tabla vacia y un cero, sale de la amplitud entera. Los dos numeros se
    // miden en el mismo sitio y se comparan con el mismo criterio.

    {
        S950Calibration cal = S950Calibration::unmeasured();

        // Una tabla CON puntos, para tener un ataque de verdad con el que
        // comparar. Estos numeros son de prueba y no salen de ninguna maquina:
        // es la misma idea que el test del arnes, la verdad la pone el test.
        cal.addPoint (CalCurveId::EnvelopeTime, 0, 0.0005);
        cal.addPoint (CalCurveId::EnvelopeTime, 99, 5.0);

        const auto medido = renderFromCalibration (cal, 50, sr, muestras, UnknownPolicy::RequireKnown);

        check(cal.isMeasured (CalCurveId::EnvelopeTime),
              "con dos puntos la curva esta medida y el renderer ya sabe el ataque");
        check(medido.unknown == 0, "y no tiene ningun mando sin resolver");
        check(medido.played, "y toca la nota");

        // 0.5 ms a 48 k son 24 muestras, asi que el salto maximo de una rampa
        // de ataque de verdad anda en una fraccion pequena de la amplitud. Lejos
        // de la amplitud entera.
        check(medido.maxStep > 0.0 && medido.maxStep < 0.1,
              "con un ataque medido, el salto entre muestras es una pendiente, no un salto");
        // Y el otro extremo de la comparacion, con la tabla VACIA y la politica
        // que se cree lo que no sabe. Los dos numeros salen del mismo sitio y
        // del mismo criterio, que es lo que hace que la comparacion signifique
        // algo: un ataque de verdad tiene una pendiente, y un ataque de cero
        // tiene la amplitud entera.
        S950Calibration vacia = S950Calibration::unmeasured();
        const auto cero = renderFromCalibration (vacia, 50, sr, muestras, UnknownPolicy::ZeroFill);

        check(medido.maxStep < cero.maxStep,
              "y con la tabla vacia y relleno a cero el salto es de otra magnitud, que es el punto");
    }

    // ═══════ 5. UN SOLO PUNTO NO ES UNA CURVA, Y TAMPOCO ES UN CLICK ═══════
    //
    // El estado intermedio que mas se va a encontrar en la practica: alguien ha
    // medido algo y ha parado. Un punto no se interpola con nada, asi que
    // `read()` sigue sin saber nada, y el renderer correcto vuelve a callarse.
    // Un punto de mas no es mejor que ninguno: es la misma ausencia.

    {
        S950Calibration cal = S950Calibration::unmeasured();
        cal.addPoint (CalCurveId::EnvelopeTime, 50, 0.30);

        check(cal.pointCount (CalCurveId::EnvelopeTime) == 1, "un punto se acepta");
        check(! cal.isMeasured (CalCurveId::EnvelopeTime), "pero un punto solo no es una curva");
        check(! cal.read (CalCurveId::EnvelopeTime, 50).has_value()
              && ! cal.read (CalCurveId::EnvelopeTime, 0).has_value(),
              "y ni siquiera en el mando medido hay curva: entre uno solo no hay nada que interpolar");

        const auto r = renderFromCalibration (cal, 50, sr, muestras, UnknownPolicy::RequireKnown);
        check(! r.played && r.maxStep == 0.0,
              "el renderer que exige saber se calla igual, en vez de inventarse una duracion");
    }

    // ═══════ 6. Y LO MISMO PARA TODAS LAS CURVAS, NO SOLO PARA EL TIEMPO ═══════
    //
    // El ataque es el ejemplo visible porque un ataque corto se oye como un
    // click. Las otras cinco curves degradan de la misma manera, y en un motor
    // real un `sustainDb` que se rellena con 0 no es un click: es un sustain
    // que se apaga solo, que es un fallo igual de dificil de atribuir. La
    // diferencia de ahi no es el sintoma: es el diagnostico equivocado.

    {
        S950Calibration cal = S950Calibration::unmeasured();
        int desconocidos = 0;

        for (int i = 0; i < calibrationCurveCount; ++i)
        {
            const auto id = static_cast<CalCurveId> (i);
            const auto& info = cal.curveInfo (id);
            const int stored = info.storedLo + (info.storedHi - info.storedLo) / 2;

            if (! cal.read (id, stored).has_value()) ++desconocidos;
        }

        check(desconocidos == calibrationCurveCount,
              "las seis curvas degradan igual: ninguna devuelve un numero que nadie ha medido");

        // Y el punto que sostiene el contrato entero, tal y como esta escrito
        // en la cabecera de `S950Calibration.h`: confundir el desconocido con un
        // 0 produce una envolvente instantanea. El `positiveOnly` de las
        // curvas de tiempo y frecuencia es la primera linea de defensa —el dato
        // no entra si no es positivo—, pero la segunda es el `?:` del llamante,
        // que es la que se acaba de probar aqui.
        int tiemposConPositivo = 0;
        for (int i = 0; i < calibrationCurveCount; ++i)
            if (calibrationCurves[i].positiveOnly) ++tiemposConPositivo;

        check(tiemposConPositivo == 4,
              "las cuatro curvas de tiempo y frecuencia exigen un valor positivo, porque cero no es un tiempo");
        check(calibrationCurves[static_cast<int> (CalCurveId::SustainDb)].logarithmic
              && ! calibrationCurves[static_cast<int> (CalCurveId::SustainDb)].positiveOnly,
              "mientras que los decibelios admiten el cero, que es un nivel legitimo y no un instante");
        check(cal.readOr (CalCurveId::EnvelopeTime, 50, 0.0) == 0.0,
              "`readOr` con un cero de reserva devuelve un click, que es justo por lo que es peligroso");
    }

    // ═══════ 7. EL INVARIANTE DE LA FASE ═══════
    //
    // Nada de lo anterior ha añadido un punto a la tabla que se entrega. Medir
    // como degrada un renderer NO es medir la maquina, y la diferencia entre las
    // dos cosas es justo lo que esta fase protege: que "vacio" siga diciendo
    // "nadie ha medido esto" y no "hemos mirado y no hemos visto nada".

    {
        S950Calibration cal = S950Calibration::unmeasured();
        int conPuntos = 0;

        for (int i = 0; i < calibrationCurveCount; ++i)
            conPuntos += cal.pointCount (static_cast<CalCurveId> (i));

        check(conPuntos == 0, "tras probar como degrada un renderer, la tabla sigue sin un solo punto");
        check(! cal.read (CalCurveId::EnvelopeTime, 50).has_value(),
              "y sigue devolviendo desconocido, que es lo unico que puede devolverse con honestidad");
    }
}
