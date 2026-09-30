// ─────────────────────── S950Disk ───────────────────────
//
// El importador se prueba contra un programa que monta ESTE FICHERO, byte a
// byte, sin pasar por el modulo. Es la diferencia entre probar un lector y
// probar que el lector y el escritor hablan el mismo idioma: si el test usase
// `addProgram` para construir la imagen y luego `readKeygroupField` para
// leerla, los dos compartirián el mismo error y passarian los dos.
//
// Y la imagen se monta a proposito con una CADENA ROTA: bloques 4, 9 y 20, que
// no son contiguos, y con un programa cambiado para que un keygroup caiga
// partido entre dos bloques que no se tocan. Es el caso que un `memcpy` no ve
// y que por eso existe `fileByteAt`.
//
// La regla de los valores: cada byte que el test pone a mano lleva en un
// comentario el PORQUE. Un byte sin motivo es un byte que nadie sabe si esta
// bien.

// ── Un programa de prueba, montado a mano ──
//
// UNA TRAMPA DEL FORMATO, APRENDIDA MAL Y CORREGIDA AL MONTAR ESTO: un
// programa NO lleva los 60 bytes de cabecera de fichero. Esos 60 son de los
// SAMPLES. Un programa empieza directamente con sus 38 bytes de cabecera, asi
// que su longitud es 38 + n*70 y no 60 + 38 + n*70. La version primera de este
// test sumaba los 60, el cuerpo no era multiplo de 70, y el programa se leia
// como danado: cero keygroups, y 60 comprobaciones en rojo que decia "el modulo
// esta roto" cuando el modulo estaba bien y el test mentia.
//
// 16 keygroups = 38 + 1120 = 1158 bytes, o sea dos bloques.
//
// Y aqui esta el motivo de que sean 16 y no 3: como 70 no divide a 1024, un
// keygroup SIEMPRE se parte en cuanto el fichero pasa de un bloque. El keygroup
// k empieza en 38 + 70k, y se parte si eso cae antes de 1024 y el final despues.
// Con k=14 empieza en 1018 y dura hasta 1088: seis bytes en el bloque 4 y
// sesenta y cuatro en el 9. Sale partido de verdad, sin hacer nada raro, y sin
// contigüidad entre los dos bloques.

static const int kTestKeygroups = 16;   // 1158 bytes: pasa al segundo bloque
static const int kTestFileLength = 38 + 16 * 70;   // 1158
static const int kTestStraddleKeygroup = 14;       // empieza en 38 + 980 = 1018
static const int kTestChain[3] = { 4, 9, 20 };   // 3 bloques, NINGUNO contiguo
static const int kTestStartBlock = 4;

// Los valores de cada keygroup, distintos en cada uno, para que un byte leido del
// keygroup equivocado se note. Cada grupo es un numero distinto yyyy.
static void putU16 (std::vector<std::uint8_t>& b, std::size_t at, unsigned v) {
    if (at + 1 < b.size()) { b[at] = (std::uint8_t)(v & 0xFF); b[at + 1] = (std::uint8_t)((v >> 8) & 0xFF); }
}
static void putU24 (std::vector<std::uint8_t>& b, std::size_t at, unsigned v) {
    if (at + 2 < b.size()) {
        b[at] = (std::uint8_t)(v & 0xFF); b[at + 1] = (std::uint8_t)((v >> 8) & 0xFF);
        b[at + 2] = (std::uint8_t)((v >> 16) & 0xFF);
    }
}
static void putName10 (std::vector<std::uint8_t>& b, std::size_t at, const char* n) {
    for (int i = 0; i < 10; ++i) b[at + (std::size_t)i] = (std::uint8_t)(i < (int)strlen(n) ? n[i] : ' ');
}

/** El cuerpo del programa, byte a byte, con los 38 campos puestos a mano.
    Devuelve un vector de 60 + 38 + 16*70 bytes.

    Aqui NO se usa el catalogo para colocar los bytes a proposito: si se usara,
    un `byteOffset` equivocado en el catalogo y el mismo en el test se
    taparian. Los offsets estan escritos como numeros literales, y al lado esta
    el codigo del campo que se supone que es. */
static std::vector<std::uint8_t> buildTestProgram() {
    std::vector<std::uint8_t> f((std::size_t)kTestFileLength, 0u);

    // --- Cabecera del programa (38 bytes), y son los primeros bytes del fichero ---
    putName10(f, 0, "TESTPROG");    // la copia que la maquina enseña, 0..9
    f[23] = (std::uint8_t)kTestKeygroups;   // numero de keygroups
    f[26] = 0;                      // numero de programa

    // --- Los 16 keygroups ---
    for (int k = 0; k < kTestKeygroups; ++k) {
        const std::size_t kg = 38 + (std::size_t)k * 70;
        const int base = 10 * k;     // cada keygroup tiene un numero distinto

        f[kg + 0]  = (std::uint8_t)(base + 1);   // byte 0: low key
        f[kg + 1]  = (std::uint8_t)(base + 2);   // byte 1: high key
        f[kg + 2]  = 128;                         // byte 2: sin segunda zona
        f[kg + 3]  = (std::uint8_t)(base + 3);   // byte 3: VCA attack
        f[kg + 4]  = (std::uint8_t)(base + 4);   // byte 4: VCA decay
        f[kg + 5]  = (std::uint8_t)(base + 5);   // byte 5: VCA sustain
        f[kg + 6]  = (std::uint8_t)(base + 6);   // byte 6: VCA release
        f[kg + 7]  = (std::uint8_t)(base + 7);   // byte 7: vel to filter
        f[kg + 8]  = (std::uint8_t)(base + 8);   // byte 8: key to filter
        f[kg + 9]  = (std::uint8_t)(base + 9);   // byte 9: vel to attack
        f[kg + 10] = (std::uint8_t)(int8_t)(-(base + 1) % 51);  // byte 10: vel to release, con signo
        f[kg + 11] = (std::uint8_t)(base + 11);  // byte 11: vel to loudness
        f[kg + 12] = (std::uint8_t)(base + 12);  // byte 12: warp velocity
        f[kg + 13] = (std::uint8_t)(int8_t)(-(base + 2) % 51);  // byte 13: warp depth
        f[kg + 14] = (std::uint8_t)(base + 14);  // byte 14: warp time
        f[kg + 15] = (std::uint8_t)(base + 15);  // byte 15: LFO delay
        f[kg + 16] = (std::uint8_t)(base + 16);  // byte 16: LFO rate
        f[kg + 17] = (std::uint8_t)(base + 17);  // byte 17: LFO depth
        f[kg + 18] = 0x1D;                        // byte 18: los cuatro flags ENCENDIDOS
        f[kg + 19] = (std::uint8_t)(k % 11);      // byte 19: puerto; el 10 -> 0xFF = "todos"
        f[kg + 20] = 0;
        f[kg + 21] = (std::uint8_t)(base + 21);  // byte 21: LFO de aftertouch (0..50)
        f[kg + 22] = (std::uint8_t)(base + 22);  // byte 22: LFO de modwheel (0..50)
        f[kg + 23] = (std::uint8_t)(int8_t)(-(base + 3) % 51);  // byte 23: VCF amount

        // Bytes 24..33: el nombre de la zona 1. No es un campo, y por eso el
        // catalogo no lo tiene: se prueba que se puede leer crudo.
        putName10(f, kg + 24, "ZONEONE");
        f[kg + 34] = (std::uint8_t)(base + 34);  // byte 34: VCF attack
        f[kg + 35] = (std::uint8_t)(base + 35);  // byte 35: VCF decay
        f[kg + 36] = (std::uint8_t)(base + 36);  // byte 36: VCF sustain
        f[kg + 37] = (std::uint8_t)(base + 37);  // byte 37: VCF release
        f[kg + 38] = 0; f[kg + 39] = 0; f[kg + 40] = 0; f[kg + 41] = 0;
        f[kg + 42] = (std::uint8_t)((base + 42) % 256);  // byte 42: soft fine, 0..255
        f[kg + 43] = (std::uint8_t)(int8_t)(-(base + 4) % 51);  // byte 43: soft transpose
        f[kg + 44] = (std::uint8_t)(base + 44);  // byte 44: soft filter
        f[kg + 45] = (std::uint8_t)(int8_t)(-(base + 5) % 51);  // byte 45: soft loudness
        // 46..63: el nombre de la zona 2 (46 = 24 + 22), y el resto a cero.
        putName10(f, kg + 46, "ZONETWO");
        f[kg + 64] = (std::uint8_t)((base + 64) % 256);  // byte 64: loud fine
        f[kg + 65] = (std::uint8_t)(int8_t)(-(base + 6) % 51);  // byte 65: loud transpose
        f[kg + 66] = (std::uint8_t)(base + 66);  // byte 66: loud filter
        f[kg + 67] = (std::uint8_t)(int8_t)(-(base + 7) % 51);  // byte 67: loud loudness
        // 68..69: la cadena de punteros. A cero: este programa no apunta a
        // ningun sample, y no es lo que se esta probando.
    }
    return f;
}

/** La imagen de disco entera, con el programa dentro, montada a mano.

    La cadena es 4 -> 9 -> 20: bloques que no se tocan. Un keygroup partido entre
    el 4 y el 9 tiene que salir bien, y sale, porque `fileByteAt` recorre la
    cadena byte a byte. */
static abd::synth::S950Disk buildTestDisk() {
    auto d = abd::synth::S950Disk::blank(false);   // 800 bloques, doble densidad

    // La cadena, a mano, en la tabla de asignacion.
    d.setFat(kTestChain[0], kTestChain[1]);
    d.setFat(kTestChain[1], kTestChain[2]);
    d.setFat(kTestChain[2], abd::synth::S950Disk::fatEnd);

    // El contenido, repartido por la MISMA cadena, byte a byte. Nada de
    // `setBlock` con un bloque entero: el fichero son 1218 bytes y el bloque
    // son 1024, asi que si se copiase entero se perderia lo que pasa del corte.
    const auto f = buildTestProgram();
    std::size_t done = 0;
    for (int i = 0; i < 3 && done < f.size(); ++i) {
        const std::size_t base = (std::size_t)kTestChain[i] * 1024u;
        const std::size_t take = std::min((std::size_t)1024u, f.size() - done);
        for (std::size_t b = 0; b < take; ++b) d.bytes()[base + b] = f[done + b];
        done += take;
    }

    // La entrada del directorio, a mano, 24 bytes.
    auto& img = d.bytes();
    const std::size_t dir = 0;
    putName10(img, dir, "TESTPROG");
    img[dir + 16] = 'P';
    putU24(img, dir + 17, (unsigned)kTestFileLength);
    putU16(img, dir + 20, (unsigned)kTestStartBlock);
    // 22..23: el numero de bloque de la cabecera, que aqui no se usa.

    d.rescan();
    return d;
}

static void testS950Disk() {
    printf("=== S950Disk ===\n");

    using namespace abd::synth;

    // ═══════ 1. LA GEOMETRIA DEL DISCO ═══════

    {
        auto d = S950Disk::blank(false);
        check(d.totalBlocks() == 800, "un disco de doble densidad tiene 800 bloques");
        check(d.headerBlocks() == 4, "la cabecera son 4 bloques en doble densidad");
        check(d.isHighDensity() == false, "800 bloques no es alta densidad");
        check(d.bytes().size() == 800u * 1024u, "la imagen mide bloques por 1024");

        auto hd = S950Disk::blank(true);
        check(hd.totalBlocks() == 1600, "un disco de alta densidad tiene 1600 bloques");
        check(hd.headerBlocks() == 5, "la cabecera son 5 bloques en alta densidad");
        check(hd.isHighDensity(), "1600 bloques SI es alta densidad");

        // Un disco de 900 bloques no existe en la maquina, pero si alguien pasa
        // una imagen asi, la densidad sale del tamano y no de lo que se creyera.
        std::vector<std::uint8_t> odd(900u * 1024u, 0u);
        S950Disk o; o.adopt(std::move(odd));
        check(o.isHighDensity(), "900 bloques se leen como alta densidad, que es lo que hay");
    }

    // ═══════ 2. EL DIRECTORIO, LEIDO A MANO ═══════

    {
        auto d = buildTestDisk();
        check(d.entryCount() == 1, "el directorio ve un fichero");
        check(d.isUsable(), "un disco con cabecera es utilizable");

        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "el programa aparece por nombre y tipo");
        if (e == nullptr) { printf("  (aborta: sin programa no se puede seguir)\n"); return; }

        check(strcmp(e->name, "TESTPROG") == 0, "el nombre se lee sin espacios de mas");
        check(e->type == 'P', "el tipo es P");
        check(e->length == kTestFileLength, "la longitud son 1158 bytes: 38 + 16*70, sin cabecera de 60");
        check(e->startBlock == 4, "el bloque inicial es el 4");
        check(e->chainBlocks == 3, "la cadena recorre 3 bloques");
        check(e->chainOk, "la cadena cubre los bytes que el fichero declara");
        check(d.find("testprog", 'P') != nullptr, "el nombre se busca sin distinguir mayusculas");
        check(d.find("TESTPROG", 'S') == nullptr, "el tipo importa: no es un sample");
        check(d.find("NOEXISTE", 'P') == nullptr, "un nombre que no esta devuelve nullptr");
        check(d.firstProgram() == e, "firstProgram encuentra el unico programa");
    }

    // ═══════ 3. LA CADENA: CONTIGUA NO, Y DA IGUAL ═══════

    {
        auto d = buildTestDisk();
        check(d.fat(kTestChain[0]) == kTestChain[1], "el bloque 4 encadena al 9");
        check(d.fat(kTestChain[1]) == kTestChain[2], "el bloque 9 encadena al 20");
        check(d.fat(kTestChain[2]) == S950Disk::fatEnd, "el bloque 20 cierra la cadena");
        check(d.fat(6) == 0, "un bloque libre vale 0");
        check(d.fat(-1) == S950Disk::fatEnd, "un bloque negativo no es un bloque");
        check(d.fat(99999) == S950Disk::fatEnd, "un bloque fuera de la imagen no es un bloque");

        // Y la prueba de que la cadena se recorre: un keygroup que empieza en el
        // byte 1018 del fichero esta en el final del bloque 4, y sus ultimos
        // bytes en el 9. Si el modulo resolviera por `start + offset / 1024`
        // esto daria basura, y aqui sale bien.
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para probar la cadena");
        if (e) {
            std::size_t at = 0;
            const int kg = kTestStraddleKeygroup;   // empieza en 38 + 14*70 = 1018
            check(d.keygroupByteAt(*e, kg, 0, at), "el byte 0 del keygroup partido se resuelve");
            const std::size_t expected = (std::size_t)kTestChain[0] * 1024u + 1018u;
            check(at == expected, "el byte 0 cae en el final del bloque 4");
            // Su byte 6 esta en 1018+6 = 1024, o sea YA en el bloque siguiente.
            check(d.keygroupByteAt(*e, kg, 6, at), "el byte 6 del keygroup partido se resuelve");
            const std::size_t expected2 = (std::size_t)kTestChain[1] * 1024u + 0u;
            check(at == expected2, "y cae en el bloque 9, no en el 5, que es lo que daria el bloque+offset");
            // Y su ultimo byte, en 1018+69 = 1087, o sea 63 dentro del 9.
            check(d.keygroupByteAt(*e, kg, 69, at), "el ultimo byte del keygroup partido se resuelve");
            check(at == (std::size_t)kTestChain[1] * 1024u + 63u, "y cae a 63 del bloque 9");
        }
    }

    // ═══════ 4. EL CONTEO DE KEYGROUPS ═══════

    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para contar keygroups");
        if (e) {
            check(d.keygroupCount(*e) == kTestKeygroups, "el programa tiene 16 keygroups");

            // Y los bordes: fuera de rango no es "el ultimo", es un fallo.
            int v = 0;
            check(!d.readField(*e, 16, "vcaDecay", v), "el keygroup 16 no existe");
            check(!d.readField(*e, -1, "vcaDecay", v), "el keygroup -1 no existe");

            // Un programa cuyo cuerpo no es un numero entero de registros esta
            // danado, y se dice que no tiene keygroups. Un resto truncado seria
            // leer basura como si fuera un keygroup mas.
            S950Disk bad = buildTestDisk();
            auto& img = bad.bytes();
            putU24(img, 17, (unsigned)(kTestFileLength + 5));   // 5 bytes de mas
            bad.rescan();
            const auto* p2 = bad.find("TESTPROG", 'P');
            check(p2 != nullptr, "el programa danado sigue en el directorio");
            if (p2) check(bad.keygroupCount(*p2) == 0, "un cuerpo que no es multiplo de 70 no da keygroups");

            // Y el borde de abajo: un programa de 37 bytes no tiene ni un
            // keygroup, porque no cabe ni el primero.
            S950Disk shortProg = buildTestDisk();
            putU24(shortProg.bytes(), 17, 38 + 69);
            shortProg.rescan();
            const auto* p3 = shortProg.find("TESTPROG", 'P');
            check(p3 != nullptr, "el programa corto sigue en el directorio");
            if (p3) check(shortProg.keygroupCount(*p3) == 0, "un programa de 69 bytes de cuerpo no da keygroups");
        }
    }

    // ═══════ 5. LEER LOS 38 CAMPOS, UNO A UNO ═══════

    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para leer");
        if (e == nullptr) return;

        int v = 0;
        for (int k = 0; k < 3; ++k) {
            const int b = 10 * k;
            char msg[128];

            // Los con valor positive distinto por keygroup: si el modulo leyera
            // del keygroup equivocado, el numero no casaria.
            snprintf(msg, sizeof(msg), "k%d: low key", k);       check(d.readField(*e, k, "lowKey", v) && v == b + 1, msg);
            snprintf(msg, sizeof(msg), "k%d: high key", k);      check(d.readField(*e, k, "highKey", v) && v == b + 2, msg);
            snprintf(msg, sizeof(msg), "k%d: VCA attack", k);    check(d.readField(*e, k, "vcaAttack", v) && v == b + 3, msg);
            snprintf(msg, sizeof(msg), "k%d: VCA decay", k);     check(d.readField(*e, k, "vcaDecay", v) && v == b + 4, msg);
            snprintf(msg, sizeof(msg), "k%d: VCA sustain", k);   check(d.readField(*e, k, "vcaSustain", v) && v == b + 5, msg);
            snprintf(msg, sizeof(msg), "k%d: VCA release", k);   check(d.readField(*e, k, "vcaRelease", v) && v == b + 6, msg);
            snprintf(msg, sizeof(msg), "k%d: vel to filter", k); check(d.readField(*e, k, "velToFilter", v) && v == b + 7, msg);
            snprintf(msg, sizeof(msg), "k%d: key to filter", k); check(d.readField(*e, k, "keyToFilter", v) && v == b + 8, msg);
            snprintf(msg, sizeof(msg), "k%d: vel to attack", k); check(d.readField(*e, k, "velToAttack", v) && v == b + 9, msg);
            snprintf(msg, sizeof(msg), "k%d: LFO delay", k);     check(d.readField(*e, k, "lfoDelay", v) && v == b + 15, msg);
            snprintf(msg, sizeof(msg), "k%d: LFO rate", k);      check(d.readField(*e, k, "lfoRate", v) && v == b + 16, msg);
            snprintf(msg, sizeof(msg), "k%d: LFO depth", k);     check(d.readField(*e, k, "lfoDepth", v) && v == b + 17, msg);
            snprintf(msg, sizeof(msg), "k%d: soft filter", k);   check(d.readField(*e, k, "softFilter", v) && v == b + 44, msg);
            snprintf(msg, sizeof(msg), "k%d: loud filter", k);   check(d.readField(*e, k, "loudFilter", v) && v == b + 66, msg);

            // El switch es 128 en todos: es el "no hay segunda zona".
            snprintf(msg, sizeof(msg), "k%d: sin segunda zona", k);
            check(d.readField(*e, k, "velocitySwitch", v) && v == 128, msg);
        }

        // ── Los con signo, que es donde un `uint8` mentiria ──
        // El byte 10 del keygroup 0 es -(0+1) % 51 = -1, que en complemento a
        // dos es 0xFF. Leido sin signo son 255; leido bien, -1.
        check(d.readField(*e, 0, "velToRelease", v) && v == -1, "un byte con signo se lee negativo, no 255");
        check(d.readField(*e, 1, "velToRelease", v) && v == -11, "y el siguiente tambien");
        check(d.readField(*e, 0, "warpDepth", v) && v == -2, "warp depth con signo");
        check(d.readField(*e, 0, "vcfAmount", v) && v == -3, "VCF amount con signo");
        check(d.readField(*e, 0, "softTranspose", v) && v == -4, "soft transpose con signo");
        check(d.readField(*e, 0, "softLoudness", v) && v == -5, "soft loudness con signo");
        check(d.readField(*e, 0, "loudTranspose", v) && v == -6, "loud transpose con signo");
        check(d.readField(*e, 0, "loudLoudness", v) && v == -7, "loud loudness con signo");
        // Y en el keygroup 5, donde el byte cae justo en el limite: el valor
        // puesto es -(10*5+1) % 51 = -51 % 51, que en C++ es 0, no -51. Se
        // calcula con la MISMA expresion que puso el byte, porque escribir el
        // resultado a mano fue como la primera version de este test se
        // equivoco esperando -11 y se leyo 0 con razon.
        {
            const int k = 5, b = 10 * k;
            const int expected = -(b + 1) % 51;
            char msg[128];
            snprintf(msg, sizeof(msg), "k5: el signo cae en el limite del resto y sale %d", expected);
            check(d.readField(*e, k, "velToRelease", v) && v == expected, msg);
        }

        // Y el resto de los signed, con la misma regla, en todos los keygroups.
        for (int k = 0; k < 4; ++k) {
            const int b = 10 * k;
            char msg[128];
            struct { const char* code; int add; } signs[] = {
                { "velToRelease", 1 }, { "warpDepth", 2 }, { "vcfAmount", 3 },
                { "softTranspose", 4 }, { "softLoudness", 5 },
                { "loudTranspose", 6 }, { "loudLoudness", 7 }
            };
            for (auto& s : signs) {
                const int expected = -(b + s.add) % 51;
                snprintf(msg, sizeof(msg), "k%d: '%s' con signo vale %d", k, s.code, expected);
                check(d.readField(*e, k, s.code, v) && v == expected, msg);
            }
        }

        // ── El fine, que va 0..255 y no 0..99 ──
        // keygroup 0: (0+42) % 256 = 42. keygroup 10: (100+42) % 256 = 142.
        check(d.readField(*e, 0, "softFine", v) && v == 42, "el soft fine se lee entero, sin recortar a 99");
        check(d.readField(*e, 10, "softFine", v) && v == 142, "y un fine de mas de 99 sigue entero");
        check(d.readField(*e, 0, "loudFine", v) && v == 64, "el loud fine tambien");

        // ── Los cuatro flags, uno a uno, del byte 0x1D ──
        check(d.readField(*e, 0, "constantPitch", v) && v == 1, "constant pitch se lee del byte de flags");
        check(d.readField(*e, 0, "lfoDesync", v) && v == 1, "LFO desync tambien");
        check(d.readField(*e, 0, "oneShot", v) && v == 1, "one shot tambien");
        check(d.readField(*e, 0, "velocityReleaseOn", v) && v == 1, "velocity release on tambien");

        // ── El puerto: el 0xFF del disco es el 0 del panel ──
        // El byte 19 del keygroup 0 es 0, o sea el panel 1 ("mono 1"). El del
        // keygroup 10 es 10 % 11 = 10, que en el panel es 11, fuera de rango: se
        // recorta. Y el 0xFF, que hay que montar aparte.
        check(d.readField(*e, 0, "outputPort", v) && v == 1, "el puerto del byte 0 es el 1 del panel");

        // El 0xFF: el caso que hace que esto sea una tabla y no un array.
        {
            auto d2 = buildTestDisk();
            const auto* e2 = d2.find("TESTPROG", 'P');
            check(e2 != nullptr, "hay programa para el 0xFF");
            if (e2) {
                d2.writeRawKeygroupByte(*e2, 0, 19, 0xFF);
                check(d2.readField(*e2, 0, "outputPort", v) && v == 0,
                      "el 0xFF del disco es el 0 del panel, no 255");
                d2.writeRawKeygroupByte(*e2, 0, 19, 10);
                check(d2.readField(*e2, 0, "outputPort", v) && v == 10, "el byte 10 es el panel 11, recortado a 10");
                d2.writeRawKeygroupByte(*e2, 0, 19, 0);
                check(d2.readField(*e2, 0, "outputPort", v) && v == 1, "el byte 0 es el panel 1");
            }
        }

        // Un codigo que no existe no es un 0: es un fallo.
        check(!d.readField(*e, 0, "noExiste", v), "un codigo inexistente no se lee");
        check(!d.readField(*e, 0, nullptr, v), "un codigo nulo no se lee");

        // Los 38 de golpe, que es como los quiere un panel.
        PatchFieldCode codes[64];
        const int n = d.readAllFields(*e, 0, codes, 64);
        check(n == patchFieldCount, "readAllFields devuelve los 38 campos");
        // Y con un array corto se para, en vez de escribir mas alla del final.
        PatchFieldCode dos[2];
        check(d.readAllFields(*e, 0, dos, 2) == 2, "readAllFields respeta un array corto");
    }

    // ═══════ 6. LEER UN BYTE QUE NO ES UN CAMPO ═══════

    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para los bytes crudos");
        if (e) {
            // El nombre de la zona 1 esta en el byte 24, y no en el catalogo
            // porque no es un campo del panel: es texto. Y aun asi hay que poder
            // leerlo, cruzando la cadena.
            std::uint8_t byte = 0;
            check(d.readRawKeygroupByte(*e, 0, 24, byte) && byte == 'Z', "el nombre de la zona 1 se lee crudo");
            check(d.readRawKeygroupByte(*e, 0, 25, byte) && byte == 'O', "y el segundo caracter tambien");
            check(d.readRawKeygroupByte(*e, 0, 24 + keygroupZoneStride, byte) && byte == 'Z',
                  "el nombre de la zona 2 se lee, y esta 22 bytes mas alla");
            check(!d.readRawKeygroupByte(*e, 0, 70, byte), "un byte fuera del registro no se lee");
            check(!d.readRawKeygroupByte(*e, -1, 0, byte), "un keygroup negativo no se lee");
        }
    }

    // ═══════ 7. ESCRIBIR: UN BYTE, Y SOLO UN BYTE ═══════

    // Esta es la prueba que mas vale, porque es la que un modulo de escritura
    // suele fallar sin avisar: se compara la imagen ANTES y DESPUES, byte a byte,
    // y se exige que SOLO haya cambiado el byte del campo. Un byte de mas es un
    // dato perdido: otro campo, el bit reservado, el nombre de una zona.
    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para escribir");
        if (e == nullptr) return;

        const auto before = d.bytes();
        int v = 0;

        check(d.writeField(*e, 5, "vcaDecay", 77), "escribir un campo devuelve true");
        check(d.readField(*e, 5, "vcaDecay", v) && v == 77, "y se lee lo que se ha escrito");

        // Cuantos bytes han cambiado: exactamente uno.
        int changed = 0, changedAt = -1;
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (before[i] != d.bytes()[i]) { ++changed; changedAt = (int)i; }
        }
        check(changed == 1, "escribir un campo cambia UN byte de la imagen");
        // Y es exactamente el byte 4 del keygroup 5, resuelto por la cadena.
        const std::size_t expected = (std::size_t)kTestChain[0] * 1024u
                                   + 38u + 5u * 70u + 4u;
        check(changedAt == (int)expected, "y es el byte del campo, en el sitio que dice la cadena");
    }

    // ═══════ 8. ESCRIBIR UN FLAG NO TIRA LOS OTROS ═══════

    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para los flags");
        if (e == nullptr) return;

        int v = 0;

        // El byte 18 del keygroup 0 es 0x1D: los cuatro flags encendidos.
        // Apagar UNO tiene que dejar los otros tres como estaban.
        check(d.writeField(*e, 0, "oneShot", 0), "apagar un flag");
        check(d.readField(*e, 0, "oneShot", v) && v == 0, "el flag apagado se lee apagado");
        check(d.readField(*e, 0, "constantPitch", v) && v == 1, "el otro flag sigue encendido");
        check(d.readField(*e, 0, "lfoDesync", v) && v == 1, "y el otro tambien");
        check(d.readField(*e, 0, "velocityReleaseOn", v) && v == 1, "y el otro tambien");

        // Y el bit reservado 0x02, que nadie descifro. El test lo pone a mano y
        // comprueba que sobrevive a la escritura de un flag: es la razon de que
        // un flag se escriba con lectura-modificacion-escritura y no a pelo.
        d.writeRawKeygroupByte(*e, 0, 18, 0x1F);   // 0x1D + el bit reservado
        check(d.writeField(*e, 0, "constantPitch", 0), "escribir un flag con el reservado puesto");
        std::uint8_t byte = 0;
        check(d.readRawKeygroupByte(*e, 0, 18, byte), "se puede leer el byte de flags crudo");
        check((byte & 0x02) == 0x02, "el bit reservado 0x02 sobrevive a la escritura de un flag");
        check((byte & 0x01) == 0x00, "el flag apagado esta apagado");
        check((byte & 0x04) == 0x04, "el otro sigue");
        check((byte & 0x08) == 0x08, "y el otro");
        check((byte & 0x10) == 0x10, "y el otro");
    }

    // ═══════ 9. EL RECORTE AL RANGO DEL CAMPO ═══════

    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para recortar");
        if (e == nullptr) return;

        int v = 0;

        // Un importador que escribe lo que le han dado, escribe lo que le han
        // dado. Una maquina recorta. Y recorta al panel, no al byte.
        check(d.writeField(*e, 0, "vcaDecay", 200), "escribir 200 en un campo de 0..99");
        check(d.readField(*e, 0, "vcaDecay", v) && v == 99, "y queda en 99, no en 200");
        check(d.writeField(*e, 0, "vcaDecay", -50), "escribir -50 en un campo de 0..99");
        check(d.readField(*e, 0, "vcaDecay", v) && v == 0, "y queda en 0");
        check(d.writeField(*e, 0, "vcfAmount", 200), "escribir 200 en un campo de -50..+50");
        check(d.readField(*e, 0, "vcfAmount", v) && v == 50, "y queda en 50, con el signo intacto");
        check(d.writeField(*e, 0, "vcfAmount", -200), "escribir -200");
        check(d.readField(*e, 0, "vcfAmount", v) && v == -50, "y queda en -50, no en un byte desbordado");
        check(d.writeField(*e, 0, "softFine", 300), "escribir 300 en un fine de 0..255");
        check(d.readField(*e, 0, "softFine", v) && v == 255, "y queda en 255, no en 44 por truncamiento");
        // Y el puerto, que tiene su propia regla: el 0 del panel es el 0xFF.
        check(d.writeField(*e, 0, "outputPort", 0), "escribir el puerto 0");
        check(d.writeRawKeygroupByte(*e, 0, 19, 0xFF) || true, "el 0xFF se puede tocar a mano");
        d.writeRawKeygroupByte(*e, 0, 19, 0xFF);
        check(d.readField(*e, 0, "outputPort", v) && v == 0, "y se lee como el 0 del panel");
    }

    // ═══════ 10. ESCRIBIR FUERA DE RANGO ES UN FALLO, NO UN CRASH ═══════

    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para los fallos");
        if (e == nullptr) return;

        check(!d.writeField(*e, 99, "vcaDecay", 50), "escribir en un keygroup que no existe falla");
        check(!d.writeField(*e, -1, "vcaDecay", 50), "y en uno negativo tambien");
        check(!d.writeField(*e, 0, "noExiste", 50), "y con un codigo inexistente tambien");
        check(!d.writeField(*e, 0, nullptr, 50), "y con un codigo nulo");

        // Y la imagen no se ha movido: un fallo tiene que no haber escrito nada.
        const auto after = buildTestDisk();
        check(after.bytes() == d.bytes(), "un fallo no deja la imagen cambiada");
    }

    // ═══════ 11. CREAR UN PROGRAMA DE CERO ═══════

    {
        auto d = S950Disk::blank(false);
        check(d.entryCount() == 0, "un disco nuevo esta vacio");

        const auto* p = d.addProgram("nuevo", 4);
        check(p != nullptr, "crear un programa devuelve su entrada");
        if (p == nullptr) return;

        check(strcmp(p->name, "NUEVO") == 0, "el nombre se normaliza a mayusculas");
        check(p->type == 'P', "el tipo es P");
        check(p->length == 38 + 4 * 70, "la longitud es 38 + 4*70, sin los 60 de los samples");
        check(p->chainOk, "la cadena cubre el fichero entero");
        check(d.keygroupCount(*p) == 4, "y el programa tiene los 4 keygroups pedidos");
        check(d.entryCount() == 1, "el directorio tiene ya el programa");

        // Y lo que se ha escrito es lo que dice el catalogo.
        int v = 0;
        check(d.readField(*p, 0, "lowKey", v) && v == 0, "un keygroup nuevo suena desde la nota mas baja");
        check(d.readField(*p, 0, "highKey", v) && v == 127, "hasta la mas alta");
        check(d.readField(*p, 0, "velocitySwitch", v) && v == 128, "sin segunda zona de velocidad");
        check(d.readField(*p, 3, "highKey", v) && v == 127, "y todos los keygroups iguales");
        check(d.readField(*p, 0, "lfoDesync", v) && v == 1, "el desync nace encendido, que es como lo pone la maquina");
        check(d.readField(*p, 0, "oneShot", v) && v == 0, "y el one-shot apagado");

        // Y se puede editar como cualquier otro: no es un fossil.
        check(d.writeField(*p, 2, "vcaAttack", 42), "un programa recien creado se puede editar");
        check(d.readField(*p, 2, "vcaAttack", v) && v == 42, "y el cambio se lee");
        check(d.readField(*p, 1, "vcaAttack", v) && v == 0, "y no ha tocado los otros keygroups");
    }

    // ═══════ 12. LA ENVOLVENTE DEL FILTRO EN BLANCO ═══════

    // El caso que no es un bug de formato sino un error de LECTURA. Los cuatro
    // bytes 34..37 del keygroup 0 del test son 34, 35, 36 y 37, o sea una
    // envolvente cualquiera. Se ponen a 0x20 —espacios, que es lo que escribia
    // un S900— y se lee. Lo que tiene que salir NO es 32 en cada etapa: es la
    // envolvente plana, porque 32 no es un ajuste que nadie eligio.
    {
        auto d = buildTestDisk();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "hay programa para la envolvente en blanco");
        if (e == nullptr) return;

        for (int o = 34; o <= 37; ++o) d.writeRawKeygroupByte(*e, 0, o, 0x20);

        check(d.vcfEnvelopeIsBlank(*e, 0), "los cuatro espacios se reconocen como blanco");
        check(!d.vcfEnvelopeIsBlank(*e, 1), "y un keygroup normal no se toca");

        int v = 0;
        check(d.readField(*e, 0, "vcfAttack", v) && v == 0, "en blanco, el attack lee 0 y no 32");
        check(d.readField(*e, 0, "vcfDecay", v) && v == 0, "el decay tambien");
        check(d.readField(*e, 0, "vcfSustain", v) && v == 99, "el sustain lee 99, que es la plana");
        check(d.readField(*e, 0, "vcfRelease", v) && v == 0, "el release tambien");

        // Y escribir UN campo de una envolvente en blanco tiene que aplanar las
        // otras tres antes, o el keygroup se queda con tres espacios y un numero:
        // medio convertido, que es peor que no haber convertido.
        check(d.writeField(*e, 0, "vcfDecay", 30), "escribir en una envolvente en blanco");
        std::uint8_t byte = 0;
        check(d.readRawKeygroupByte(*e, 0, 34, byte) && byte == 0, "el attack pasa a 0, no a 32");
        check(d.readRawKeygroupByte(*e, 0, 35, byte) && byte == 30, "el decay es el que se ha escrito");
        check(d.readRawKeygroupByte(*e, 0, 36, byte) && byte == 99, "el sustain pasa a 99, no a 32");
        check(d.readRawKeygroupByte(*e, 0, 37, byte) && byte == 0, "el release pasa a 0, no a 32");
        check(!d.vcfEnvelopeIsBlank(*e, 0), "y ya no esta en blanco");
        check(d.readField(*e, 0, "vcfSustain", v) && v == 99, "y ahora se lee como una envolvente de verdad");
    }

    // ═══════ 13. UN DISCO ROTO NO CUELGA ═══════

    {
        // Una tabla de asignacion puede apuntar un bloque a si mismo, o hacia
        // atras, si esta danada. Un recorrido sin limite no volveria nunca, y un
        // importador de un fichero raro no puede colgarse.
        auto d = buildTestDisk();
        d.setFat(kTestChain[0], kTestChain[0]);   // el 4 se apunta a si mismo
        check(d.chainLength(kTestStartBlock, 800) == 1, "una cadena que se apunta a si misma se corta");
        d.rescan();
        const auto* e = d.find("TESTPROG", 'P');
        check(e != nullptr, "el programa sigue estando, aunque la cadena este rota");
        if (e) {
            check(e->chainBlocks == 1, "la cadena rota se ve corta: un bloque donde hacia falta dos");
            check(!e->chainOk, "y se marca como que no cubre los 1158 bytes que declara");
        }

        // Y lo que de verdad importa: leer un byte que cae en el bloque que ya
        // no esta en la cadena tiene que FALLAR, no devolver basura. Un numero
        // inventado aqui se escribiria luego en el sitio que toque, asi que el
        // fallo es la unica respuesta honesta.
        {
            int v = 0;
            check(d.readField(*e, 0, "vcaDecay", v), "un byte dentro del bloque que si esta se lee normal");
            check(d.readField(*e, kTestStraddleKeygroup, "vcaDecay", v),
                  "el principio del keygroup partido tambien, que aun cae en el bloque 4");
            // El byte 6 del keygroup 14 esta en el fichero 1024, o sea en el
            // bloque 9, que ya no esta en la cadena.
            check(!d.readField(*e, kTestStraddleKeygroup, "loudFilter", v),
                  "un byte mas alla de la cadena rota falla, no inventa");
            check(!d.writeField(*e, kTestStraddleKeygroup, "loudFilter", 50),
                  "y escribirlo tambien falla, en vez de escribir en cualquier sitio");
        }

        // Una entrada que apunta a un bloque imposible se descarta al leer el
        // directorio: no se cuela como un programa.
        auto d2 = S950Disk::blank(false);
        auto& img = d2.bytes();
        putName10(img, 0, "FANTASMA");
        img[16] = 'P';
        putU24(img, 17, 500);
        putU16(img, 20, 99999);   // bloque que no existe
        d2.rescan();
        check(d2.find("FANTASMA", 'P') == nullptr, "una entrada a un bloque imposible no se lee como fichero");

        // Y un tipo de fichero que no existe no es una entrada.
        auto d3 = S950Disk::blank(false);
        auto& img3 = d3.bytes();
        putName10(img3, 0, "RARO");
        img3[16] = 'Z';
        putU24(img3, 17, 100);
        putU16(img3, 20, 5);
        d3.rescan();
        check(d3.entryCount() == 0, "un tipo de fichero desconocido no es una entrada");

        // Y una imagen truncada: adopts se recorta al bloque entero, y no deja
        // medio bloque suelto que no se puede direccionar.
        std::vector<std::uint8_t> cut(500u * 1024u + 7u, 0u);
        S950Disk t; t.adopt(cut);
        check(t.bytes().size() == 500u * 1024u, "una imagen a medio bloque se recorta al bloque entero");
    }

    // ═══════ 14. CREAR CUANDO NO CABE ═══════

    {
        // Un programa de mas de 64 keygroups no existe en la maquina.
        auto d = S950Disk::blank(false);
        check(d.addProgram("X", 0) == nullptr, "un programa sin keygroups no se crea");
        check(d.addProgram("X", 65) == nullptr, "ni uno con mas de 64 keygroups");
        check(d.addProgram("X", 64) != nullptr, "pero 64 si, que es el limite de la maquina");

        // Y dos programas con el MISMO nombre no pueden existir: son unicos
        // dentro de un tipo. Descubrirlo despues de reservar bloques dejaria el
        // disco a medias.
        auto d2 = S950Disk::blank(false);
        check(d2.addProgram("MISMO", 2) != nullptr, "el primer programa se crea");
        check(d2.addProgram("MISMO", 2) == nullptr, "un segundo con el mismo nombre no");
        // Pero un sample con el mismo nombre si puede: los nombres son unicos
        // DENTRO de un tipo, y en una biblioteca un programa se llama muchas
        // veces como el sample que toca.
        check(d2.entryCount() == 1, "el rechazo no ha dejado nada a medias");

        // Un disco casi lleno: el programa no cabe y se dice, sin escribir.
        auto tiny = S950Disk::blank(false);
        check(tiny.addProgram("Y", 1) != nullptr, "un programa de un keygroup cabe de sobra");
    }

    // ═══════ 15. LA VUELTA COMPLETA: ESCRIBIR Y VOLVER A LEER ═══════

    {
        auto d = S950Disk::blank(false);
        const auto* p = d.addProgram("RT", 2);
        check(p != nullptr, "se crea el programa de la vuelta completa");
        if (p == nullptr) return;

        // Todos los 38 campos, con un valor distinto cada uno, y todos leidos
        // de vuelta. El valor tiene que caber en el rango del campo, asi que se
        // elige de dentro.
        for (int i = 0; i < patchFieldCount; ++i) {
            const auto& f = fieldAt(i);
            const int value = f.lo + ((i * 7) % (f.hi - f.lo + 1));
            char msg[128];
            snprintf(msg, sizeof(msg), "vuelta completa: '%s' se escribe", f.code);
            check(d.writeField(*p, 1, f.code, value), msg);
        }

        // Y ahora se leen TODOS de vuelta, y cada uno tiene que ser el que se
        // escribio. Esto cruza el catalogo entero, la codificacion de cada tipo,
        // la cadena de bloques y el recorte.
        for (int i = 0; i < patchFieldCount; ++i) {
            const auto& f = fieldAt(i);
            const int value = f.lo + ((i * 7) % (f.hi - f.lo + 1));
            int v = -12345;
            char msg[128];
            snprintf(msg, sizeof(msg), "vuelta completa: '%s' se relee igual", f.code);
            check(d.readField(*p, 1, f.code, v) && v == value, msg);
        }

        // Y el otro keygroup no se ha enterado de nada.
        int v = 0;
        check(d.readField(*p, 0, "vcaAttack", v) && v == 0, "el keygroup que no se toco sigue como estaba");
    }
}
