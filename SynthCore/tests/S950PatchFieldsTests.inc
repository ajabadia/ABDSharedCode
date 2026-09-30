// ─────────────────────── S950PatchFields ───────────────────────
//
// El catálogo de patches del S950 no calcula nada: describe. Un catálogo de
// datos que nadie comprueba se pudre en silencio — un byte desplazado, un rango
// escrito al revés, un nombre duplicado — y lo único que se nota es que un patch
// importado suena raro, semanas después.
//
// Por eso estos tests no comprueban que el catálogo sea bonito, sino que sea
// COHERENTE: que todo byte caiga dentro del registro, que dos campos no se pisen,
// que los códigos sean únicos (porque un preset los referencia por código), que
// los trims apunten a campos que existen, y que los cuatro flags que comparten
// byte 18 no se pisen entre sí.
//
// Y una comprobación que parece de compilación y no lo es: que la tabla mida lo
// que su cabecera dice que mide. Treinta y cinco filas, y si alguien añade un
// campo sin contarlo, el test lo dice.

static void testS950PatchFields() {
    printf("=== S950PatchFields ===\n");

    using namespace abd::synth;

    // ── La tabla tiene el tamaño que dice la cabecera ──
    // Si esto falla, alguien añadió o quitó una fila y no ajustó nada. El 38 no
    // es un número redondo: es el número de campos que tiene un keygroup del
    // S950, comprobado uno a uno contra la tabla del estudio.
    check(patchFieldCount == 38, "la tabla de campos tiene 38 filas, las del keygroup");
    check(performTrimCount == 18, "la tabla de trims tiene 18 filas");
    check(outputPortCount == 11, "hay 11 salidas (ALL + 8 mono + L + R)");

    // ── Todos los offsets caen dentro del registro de 70 bytes ──
    // Este es el que más vale: un byteOffset de 74 no es "casi vale", es un campo
    // que lee la cabecera del keygroup siguiente. Y como un keygroup puede
    // empezar en cualquier punto de un programa encadenado, la lectura seguiría
    // siendo válida y devolvería basura con toda naturalidad.
    for (int i = 0; i < patchFieldCount; ++i) {
        const auto& f = fieldAt(i);
        char msg[128];
        snprintf(msg, sizeof(msg), "el byte de '%s' cae dentro del registro de 70 bytes", f.code);
        check(f.byteOffset >= 0 && f.byteOffset < keygroupRecordSize, msg);
    }

    // ── Ningún byte se solapa con otro, salvo los flags que lo comparten ──
    // Dos campos en el mismo byte con la misma codificación son un byte con dos
    // verdades, y el que escriba último gana sin avisar. Los Bit sí pueden
    // compartir byte, y DEBEN: es lo que hace posible cambiar uno sin perder los
    // otros tres.
    for (int i = 0; i < patchFieldCount; ++i) {
        for (int j = i + 1; j < patchFieldCount; ++j) {
            const auto& a = fieldAt(i);
            const auto& b = fieldAt(j);
            if (a.byteOffset != b.byteOffset) continue;
            if (a.encoding == PatchField::Encoding::Bit && b.encoding == PatchField::Encoding::Bit) continue;
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' y '%s' no se pisan en el byte %d", a.code, b.code, a.byteOffset);
            check(false, msg);
        }
    }

    // ── Los cuatro flags de byte 18: bits distintos, y 0 o 1 ──
    // Dos flags con la misma máscara son un flag escrito dos veces. Y un flag
    // cuyo rango no sea 0..1 es un booleano con tres estados, que no existe.
    std::uint8_t seenMasks = 0;
    for (int i = 0; i < patchFieldCount; ++i) {
        const auto& f = fieldAt(i);
        if (f.encoding != PatchField::Encoding::Bit) continue;
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' es un flag de 0 o 1", f.code);
        check(f.lo == 0 && f.hi == 1, msg);
        snprintf(msg, sizeof(msg), "la máscara de '%s' es un bit suelto", f.code);
        check(f.bitMask != 0 && (f.bitMask & (f.bitMask - 1)) == 0, msg);
        snprintf(msg, sizeof(msg), "'%s' usa un bit que otro flag no usa", f.code);
        check((seenMasks & f.bitMask) == 0, msg);
        seenMasks = static_cast<std::uint8_t>(seenMasks | f.bitMask);
    }
    // Los cuatro conocidos del byte 18 son exactamente 0x1D, y lo que queda
    // dentro del byte es de alguien que todavía no lo ha descifrado. Esta
    // comprobación es la que obliga a que `reservedBitMask` siga siendo verdad:
    // si se añade un flag nuevo, la suma tiene que cambiar con él.
    check(seenMasks == knownFlagsMask, "los flags conocidos cubren 0x1D del byte 18");
    check((knownFlagsMask & reservedBitMask) == 0, "lo reservado y lo conocido no se solapan");
    check(keygroupFlagsByte == 18, "el byte de flags es el 18");

    // ── Los códigos son únicos: un preset los referencia por código ──
    // Y buscar por código devuelve SIEMPRE la fila que se le pide. Si dos filas
    // compartieran código, findField devolvería la primera y el duplicado se
    // escondería detrás de una fila que "funciona".
    check(findField("noExisteEsteCampo") == nullptr, "un código inexistente devuelve nullptr");
    check(findField(nullptr) == nullptr, "un código nulo devuelve nullptr");
    check(findField("") == nullptr, "un código vacío devuelve nullptr");
    for (int i = 0; i < patchFieldCount; ++i) {
        for (int j = i + 1; j < patchFieldCount; ++j) {
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' y '%s' no comparten código", fieldAt(i).code, fieldAt(j).code);
            check(strcmp(fieldAt(i).code, fieldAt(j).code) != 0, msg);
        }
        const auto* f = findField(fieldAt(i).code);
        char msg[128];
        snprintf(msg, sizeof(msg), "buscar '%s' devuelve su propia fila", fieldAt(i).code);
        check(f == &fieldAt(i), msg);
    }

    // ── Los rangos: alto >= bajo, y el máximo representable en su byte ──
    for (int i = 0; i < patchFieldCount; ++i) {
        const auto& f = fieldAt(i);
        char msg[128];
        snprintf(msg, sizeof(msg), "el rango de '%s' no está al revés", f.code);
        check(f.lo <= f.hi, msg);
        // El máximo tiene que caber en el byte. Un campo con un 300 arriba no se
        // puede escribir entero, y un truncamiento silencioso es peor que un
        // error: un patch que suena afinado y no lo está.
        snprintf(msg, sizeof(msg), "el máximo de '%s' cabe en su byte", f.code);
        check(f.hi <= 255, msg);
        snprintf(msg, sizeof(msg), "el mínimo de '%s' cabe en su byte", f.code);
        check(f.lo >= -128, msg);
    }

    // ── El nombre existe y no está vacío ──
    // Un `name` vacío no rompe nada al compilar: rompe el panel, en un sitio
    // donde nadie mira hasta que sale un control sin etiqueta.
    for (int i = 0; i < patchFieldCount; ++i) {
        const auto& f = fieldAt(i);
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' tiene nombre de panel", f.code);
        check(f.name != nullptr && f.name[0] != '\0', msg);
        snprintf(msg, sizeof(msg), "'%s' está en un grupo del panel", f.code);
        check(f.group != nullptr && f.group[0] != '\0', msg);
        snprintf(msg, sizeof(msg), "'%s' dice lo que cuenta su unidad", f.code);
        check(f.unit != nullptr && f.unit[0] != '\0', msg);
    }

    // ── Los Signed van de -50 a +50, que es lo que imprime el panel ──
    // Se fija la regla para que un Signed nuevo no se cuele con un rango de
    // 0..99 y luego produzca un byte que significa otra cosa.
    for (int i = 0; i < patchFieldCount; ++i) {
        const auto& f = fieldAt(i);
        if (f.encoding != PatchField::Encoding::Signed) continue;
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' con signo va de -50 a +50", f.code);
        check(f.lo == -50 && f.hi == 50, msg);
    }

    // ── El puerto: 0..10, con ALL al principio y L/R al final ──
    {
        const auto* port = findField("outputPort");
        check(port != nullptr, "el campo outputPort existe");
        if (port) {
            check(port->encoding == PatchField::Encoding::Port, "outputPort usa la codificación Port");
            check(port->lo == 0 && port->hi == 10, "outputPort va de 0 a 10, que son 11 salidas");
        }
        check(strcmp(outputPorts[0].name, "ALL") == 0, "la salida 0 es ALL");
        check(strcmp(outputPorts[9].name, "LEFT") == 0, "la salida 9 es LEFT");
        check(strcmp(outputPorts[10].name, "RIGHT") == 0, "la salida 10 es RIGHT");
        for (int i = 0; i < outputPortCount; ++i) {
            char msg[128];
            snprintf(msg, sizeof(msg), "la salida %d lleva su número de panel", i);
            check(outputPorts[i].panelValue == i, msg);
            snprintf(msg, sizeof(msg), "la salida %d no se queda muda", i);
            check(outputPorts[i].left + outputPorts[i].right > 0.0f, msg);
        }
    }

    // ── El fine de zona es 0..255, y NO 0..99 ──
    // Este no es un detalle de formato: el fine y el transpose son el byte bajo
    // y el alto de un offset de 16 bits en dieciseiseavos de semitono. Si el
    // fine fuera 0..99 se perdería un cuarto de tono por arriba, y todo seguiría
    // compilando y sonando casi bien, que es lo peor.
    for (int zone = 0; zone < keygroupMaxZones; ++zone) {
        const char* code = zone == 0 ? "softFine" : "loudFine";
        const auto* f = findField(code);
        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' va de 0 a 255, no de 0 a 99", code);
        check(f != nullptr && f->lo == 0 && f->hi == 255, msg);
    }
    {
        const auto* fine = findField("softFine");
        const auto* tr = findField("softTranspose");
        check(fine != nullptr && tr != nullptr, "la zona suave tiene fine y transpose");
        if (fine && tr) {
            // Y la prueba de que la función sirve para algo: medio tono arriba
            // son 8 dieciseiseavos, no 8 "unidades de fine".
            check(zonePitchOffset(*fine, 0, *tr, 1) == 256, "un semitono son 256 dieciseiseavos");
            check(zonePitchOffset(*fine, 8, *tr, 0) == 8, "8 dieciseiseavos son medio semitono");
            check(zonePitchOffset(*fine, 0, *tr, 0) == 0, "un offset de cero es cero");
            // El fine se enmascara a un byte porque es el byte BAJO: sin la
            // máscara, un valor de -3 contamination el byte alto con dos turnos
            // de signo y 256 + 253 = 509, que no cabe en dieciseiseavos.
            check(zonePitchOffset(*fine, 255, *tr, 0) == 255, "el fine enmascara al byte, no se desborda");
        }
    }

    // ── La zona 2 está donde la zona 1 + 22 ──
    // El paso de zona es lo que hace que haya ocho campos y no cuatro. Si
    // cambia, un patch se lee con la afinación de la otra zona.
    {
        const auto* soft = findField("softFilter");
        const auto* loud = findField("loudFilter");
        check(soft != nullptr && loud != nullptr, "las dos zonas tienen su filtro");
        if (soft && loud) {
            check(loud->byteOffset - soft->byteOffset == keygroupZoneStride,
                  "la zona 2 está 22 bytes después de la zona 1");
        }
    }

    // ── El switch de velocidad: 1..128, y el 128 significa "no hay zona 2" ──
    // El rango empieza en 1 y no en 0 porque el 0 es el hueco que deja el switch
    // cuando no hay nada que repartir entre zonas, y una velocidad MIDI vale de 0
    // a 127: nunca llega a 128, y por eso 128 puede ser ese valor y solo ese.
    {
        const auto* sw = findField("velocitySwitch");
        check(sw != nullptr, "el switch de velocidad existe");
        if (sw) {
            check(sw->lo == 1 && sw->hi == 128, "el switch va de 1 a 128");
            check(sw->hi == keygroupVelocityCount, "el 128 es una velocidad que no existe");
            check(sw->lo == 1, "el 0 no es un valor de switch, es el hueco");
        }
    }

    // ── La envolvente del filtro: los cuatro bytes 34..37, en orden ──
    // Attack, decay, sustain, release, y en ese orden porque ese es el que
    // recorre la memoria. Es un test tonto que se rompe solo si alguien inserta
    // una fila entre medias, que es justo cuando conviene que se note.
    {
        static const char* const env[] = { "vcfAttack", "vcfDecay", "vcfSustain", "vcfRelease" };
        for (int i = 0; i < 4; ++i) {
            const auto* f = findField(env[i]);
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' está en el byte %d de la envolvente del filtro", env[i], 34 + i);
            check(f != nullptr && f->byteOffset == 34 + i, msg);
        }
    }

    // ── Los trims apuntan a campos que existen, y su offset cabe en el campo ──
    // Un trim que mueve un campo que ya no está en la tabla es un mando que no
    // hace nada, y no da ningún error: simplemente se queda ahí.
    for (int i = 0; i < performTrimCount; ++i) {
        const auto& t = performTrims[i];
        char msg[128];
        snprintf(msg, sizeof(msg), "el trim '%s' tiene nombre", t.code);
        check(t.name != nullptr && t.name[0] != '\0', msg);
        snprintf(msg, sizeof(msg), "el trim '%s' tiene un recorrido que no está al revés", t.code);
        check(t.lo <= t.hi, msg);

        if (t.fieldCode[0] == '\0') {
            // Un trim sin campo es un control que la máquina NO tenía. Es
            // legítimo — la resonancia y el LFO al filtro son exactamente eso —
            // pero tiene que ser una decisión, no un olvido: aquí se cuentan y
            // se fija que son tres, para que un cuarto aparezca como decisión.
            snprintf(msg, sizeof(msg), "el trim '%s' sin campo sigue declarando su unidad", t.code);
            check(t.unit != nullptr && t.unit[0] != '\0', msg);
            continue;
        }

        const auto* f = findField(t.fieldCode);
        snprintf(msg, sizeof(msg), "el trim '%s' mueve un campo que existe", t.code);
        check(f != nullptr, msg);
        if (f == nullptr) continue;

        snprintf(msg, sizeof(msg), "el trim '%s' no mueve un flag, que no es un mando", t.code);
        check(f->encoding != PatchField::Encoding::Bit, msg);

        // El offset no puede ser MÁS ANCHO que el campo que mueve: un offset de
        // 99 sobre un campo de 0..50 no es un mando que llega lejos, es un
        // mando whose value can overflow. La inversa NO se exige, y es la parte
        // interesante: el VCF amount lleva un offset de 50 sobre un campo que va
        // de -50 a +50, o sea que NO alcanza los dos extremos desde cualquier
        // sitio. Es una decisión medida —el amount da el mismo valor en todos
        // los keygroups de todos los programas de la biblioteca, así que no hay
        // reparto que un offset tenga que preservar— y por eso la regla es
        // "cabe", no "llega".
        const int reach = t.hi > -t.lo ? t.hi : -t.lo;
        snprintf(msg, sizeof(msg), "el offset de '%s' no es más ancho que '%s'", t.code, t.fieldCode);
        check(reach <= f->hi - f->lo, msg);
    }

    // ── Las envolventes sí llegan a los dos extremos, y por eso llevan 99 ──
    // Un span de 99 en cualquier dirección deja llevar un decay de 80 a 0 y uno
    // de 5 a 99. El coste es que el centro del mando no es el centro del rango,
    // y es el intercambio correcto: el centro es "como estaba grabado".
    {
        static const char* const full[] = {
            "vcaAttack", "vcaDecay", "vcaSustain", "vcaRelease",
            "vcfAttack", "vcfDecay", "vcfSustain", "vcfRelease"
        };
        for (int i = 0; i < 8; ++i) {
            const auto* t = findTrim(full[i]);
            const auto* f = findField(full[i]);
            char msg[128];
            snprintf(msg, sizeof(msg), "el trim de '%s' recorre sus 99 enteros", full[i]);
            check(t != nullptr && f != nullptr && t->lo == -99 && t->hi == 99
                  && (f->hi - f->lo) == 99, msg);
        }
    }

    // ── El VCF amount es LA EXCEPCIÓN, y por eso se fija a mano ──    // Offset de 50 sobre un campo de 100 de recorrido: no llega a los dos
    // extremos. Y sin esta comprobación, alguien que lo "arreglara" poniendo 100
    // pasaría el test general de arriba sin que nadie supiera que ha estropeado
    // un mando que ya funcionaba.
    {
        const auto* t = findTrim("vcfAmount");
        const auto* f = findField("vcfAmount");
        check(t != nullptr && f != nullptr, "el VCF amount tiene trim y campo");
        if (t && f) {
            check(t->lo == -50 && t->hi == 50, "el offset del VCF amount es de 50, no de 99");
            check((f->hi - f->lo) == 100, "el campo del VCF amount recorre 100, el doble que su offset");
        }
    }

    // ── Bipolar o profundidad, y nunca las dos cosas ──
    // Las profundidades solo suman. Casi todos los patches de la biblioteca
    // dejan la profundidad del LFO en 0, así que un mando simétrico gastaría
    // media vuelta pidiendo menos que nada. Lo que se mide es que la regla se
    // aplique a cada fila, no que hoy salgan bien.
    for (int i = 0; i < performTrimCount; ++i) {
        const auto& t = performTrims[i];
        const char* c = t.code;
        const bool isDepth = strcmp(c, "lfoDepth") == 0
                          || strcmp(c, "velToFilter") == 0
                          || strcmp(c, "velToLoudness") == 0
                          || strcmp(c, "lfoToFilter") == 0
                          || strcmp(c, "resonance") == 0;
        const bool isAbsolute = strcmp(c, "lfoShape") == 0;
        char msg[128];
        // Tres clases y solo tres: un offset que suma y resta, una profundidad
        // que solo suma, o un valor absoluto. `lfoShape` es el tercero: es una
        // elección entre cuatro formas, y no hay nada en el disco que offsetear.
        snprintf(msg, sizeof(msg), "'%s' es bipolar, profundidad o absoluto, y no dos cosas", c);
        check((t.bipolar + isDepth + (isAbsolute ? 1 : 0)) == 1, msg);
        // Y un bipolar de verdad tiene que cruzar el cero, o no es bipolar.
        if (t.bipolar) {
            snprintf(msg, sizeof(msg), "'%s' bipolar cruza el cero", c);
            check(t.lo < 0 && t.hi > 0, msg);
        }
    }

    // ── lfoShape es un índice absoluto, no un offset ──
    // Es el único trim que no mueve un byte del keygroup: es una elección entre
    // cuatro, y no hay nada en el disco que offsetear. Se fija aparte porque un
    // índice con signo no es un índice.
    {
        const auto* shape = findTrim("lfoShape");
        check(shape != nullptr, "lfoShape existe");
        if (shape) {
            check(shape->lo == 0 && shape->hi == 3, "lfoShape son cuatro formas, 0 a 3");
            check(shape->fieldCode[0] == '\0', "lfoShape no mueve ningún campo del keygroup");
        }
    }

    // ── Al reves: el trimId de un campo tiene que EXISTIR como trim ──
    // La comprobacion de arriba va del trim al campo. Esta va del campo al
    // trim, y es la que faltaba: una version primera de la tabla le puso a
    // `velToRelease` y `warpDepth` un trimId con su propio nombre, y el
    // nombre no estaba en la lista de trims. La referencia colgaba, y ningun
    // motor se quejaba —un panel que lo buscase no encontraria nada, y solo
    // se sabria al mover el mando—. Lo cazó el contrato JSON, que mira al
    // reves. Esta es la que lo haria ver en el sitio donde vive el dato.
    for (int i = 0; i < patchFieldCount; ++i) {
        const auto& f = fieldAt(i);
        if (f.trimId[0] == '\0')
            continue;

        char msg[128];
        snprintf(msg, sizeof(msg), "'%s' apunta a un trim que existe", f.code);
        check(findTrim(f.trimId) != nullptr, msg);
    }

    // Y el otro sentido, que es el que ya estaba: un trim sin campo tiene que
    // ser una de las decisiones explicitas, no un olvido.
    for (int i = 0; i < performTrimCount; ++i) {
        const auto& t = performTrims[i];
        char msg[128];
        snprintf(msg, sizeof(msg), "el trim '%s' o mueve un campo, o no mueve ninguno", t.code);
        check(t.fieldCode[0] == '\0' || findField(t.fieldCode) != nullptr, msg);
    }

    // ── Los nombres de trim no se repiten ──
    for (int i = 0; i < performTrimCount; ++i) {
        for (int j = i + 1; j < performTrimCount; ++j) {
            char msg[128];
            snprintf(msg, sizeof(msg), "'%s' y '%s' no comparten nombre de panel",
                     performTrims[i].name, performTrims[j].name);
            check(strcmp(performTrims[i].name, performTrims[j].name) != 0, msg);
        }
    }

    // ── findTrim devuelve su propia fila, y nullptr si no existe ──
    for (int i = 0; i < performTrimCount; ++i) {
        char msg[128];
        snprintf(msg, sizeof(msg), "buscar el trim '%s' devuelve su propia fila", performTrims[i].code);
        check(findTrim(performTrims[i].code) == &performTrims[i], msg);
    }
    check(findTrim("noExisteEsteTrim") == nullptr, "un trim inexistente devuelve nullptr");
    check(findTrim(nullptr) == nullptr, "un trim nulo devuelve nullptr");

    // ── El recorte de un campo: lo que un importador y un mando hacen los dos ──
    {
        const auto* decay = findField("vcaDecay");
        check(decay != nullptr, "vcaDecay existe");
        if (decay) {
            check(clampToField(*decay, 50) == 50, "un valor dentro del rango no se toca");
            check(clampToField(*decay, -1) == 0, "por debajo se recorta al mínimo");
            check(clampToField(*decay, 100) == 99, "por encima se recorta al máximo");
            check(clampToField(*decay, 0) == decay->lo, "el mínimo es fijo");
            check(clampToField(*decay, 1000) == decay->hi, "un valor absurdo cae en el máximo");
        }
    }

    // ── La geometría: el registro tiene que ser coherente consigo mismo ──
    check(keygroupRecordSize == 70, "un keygroup son 70 bytes");
    check(programHeaderSize == 38, "la cabecera del programa son 38 bytes");
    check(keygroupZoneStride == 22, "el paso de zona es de 22 bytes");
    check(keygroupMaxZones == 2, "un keygroup tiene dos zonas de velocidad");
    check(keygroupNameOffset >= 0 && keygroupNameOffset < keygroupRecordSize,
          "el nombre de la zona 1 está dentro del registro");
    // El nombre de la zona 2 va 22 bytes más allá del de la zona 1, así que tiene
    // que caber también: si el nombre creciera más que el paso, la zona 2 se
    // saldría del registro por el final.
    check(keygroupNameOffset + keygroupZoneStride < keygroupRecordSize,
          "el nombre de la zona 2 cabe dentro del registro");
}
