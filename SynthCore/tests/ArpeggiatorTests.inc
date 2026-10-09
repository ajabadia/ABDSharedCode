// ─────────────────────────── Arpeggiator ───────────────────────────
//
// El motor de arpegiación compartido (abd::synth::Arpeggiator), agnóstico de
// framework. Lo que se fija aquí:
//
//  - Los 11 modos con su secuencia EXACTA de note-on sobre el pool [60,64,67].
//    La tabla no es una invención: es la misma ley que ya fija la suite
//    SynthEngineUnitTests_ArpSeq de ABDEep, reproducida aquí sin JUCE.
//  - El orden de PULSACIÓN (modo 9) frente al de AFINACIÓN (modo 0): el pool
//    se ordena por nota, pero "as played" respeta el orden de los dedos.
//  - El pool de octavas (y el techo MIDI 127), HOLD, KEY SYNC y que tras
//    allNotesOff() no quede NI UNA nota sonando (sin colgadas).
//  - La ley de la puerta: el apagado se programa en lround(duracion·gate)
//    muestras dentro del paso, y gate 0 no programa NADA (el apagado lo pone
//    el flush del bloque siguiente).
//  - Zero-alloc: generate() corre entero con el contador global de operator
//    new sin que se mueva ni una vez.
//
// Este .inc se incluye DENTRO de `namespace abd::synth::tests` (lo abre
// SynthCoreTests.cpp), igual que ModMatrixTests.inc: por eso NO abre namespace
// propio y NO incluye cabeceras de std (ya están arriba). Arpeggiator.h se
// incluye arriba, FUERA del namespace; un #include aquí sería no-op por el
// guard, así que se omite deliberadamente.

// Recoge eventos en un buffer fijo: ni std::vector ni nada que aloje, para
// poder medir el zero-alloc de generate() con el contador global de new.
struct ArpCollector
{
    abd::synth::Arpeggiator::NoteEvent ev[128];
    int n = 0;
    void operator()(const abd::synth::Arpeggiator::NoteEvent& e)
    {
        if (n < 128) ev[n++] = e;
    }
};

static void testArpeggiatorModes()
{
    printf("=== Arpeggiator: los 11 modos ===\n");

    // Secuencia esperada de los primeros 8 note-on con pool [60,64,67].
    // Los modos 8 (Random) y 10 (Chord) no son una lista: se comprueban aparte.
    const int esperado[11][8] = {
        {60, 64, 67, 60, 64, 67, 60, 64}, //  0 Up
        {67, 64, 60, 67, 64, 60, 67, 64}, //  1 Down
        {60, 64, 67, 64, 60, 64, 67, 64}, //  2 Up&Down
        {60, 60, 60, 60, 64, 64, 64, 64}, //  3 Up Inv
        {67, 64, 60, 64, 67, 64, 60, 64}, //  4 Down Inv
        {60, 64, 67, 64, 60, 64, 67, 64}, //  5 Up&Down Inv
        {60, 67, 64, 64, 67, 60, 60, 67}, //  6 Up Alt
        {67, 60, 64, 64, 60, 67, 67, 60}, //  7 Down Alt
        {0, 0, 0, 0, 0, 0, 0, 0},         //  8 Random  (aparte)
        {67, 60, 64, 67, 60, 64, 67, 60}, //  9 As Played (67,60,64 pulsadas)
        {0, 0, 0, 0, 0, 0, 0, 0},         // 10 Chord   (aparte)
    };

    int primerosOn[11];
    for (int m = 0; m <= 10; ++m) primerosOn[m] = -1;

    for (int mode = 0; mode <= 10; ++mode)
    {
        if (mode == 8 || mode == 10) continue;

        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(mode);
        arp.setGate(0.5f);

        // El modo 9 se juega en el ORDEN DE PULSACIÓN: 67 primero.
        if (mode == 9)
        {
            arp.noteOn(67, 1.0f);
            arp.noteOn(60, 0.9f);
            arp.noteOn(64, 0.8f);
        }
        else
        {
            arp.noteOn(60, 1.0f);
            arp.noteOn(64, 0.9f);
            arp.noteOn(67, 0.8f);
        }

        ArpCollector c;
        arp.generate(44100, c);

        int onNotes[64];
        int on = 0;
        for (int i = 0; i < c.n; ++i)
            if (c.ev[i].isNoteOn && on < 64) onNotes[on++] = c.ev[i].note;

        primerosOn[mode] = on > 0 ? onNotes[0] : -1;

        check(on >= 8, "el modo emite 8 note-on o más en un segundo");

        bool match = on >= 8;
        for (int k = 0; match && k < 8; ++k)
            match = (onNotes[k] == esperado[mode][k]);

        char msg[96];
        snprintf(msg, sizeof(msg), "modo %d reproduce su secuencia esperada", mode);
        check(match, msg);
    }

    // La distinta que importa: cronológico (dedos) vs afinación (pitch).
    check(primerosOn[0] == 60 && primerosOn[9] == 67,
          "el modo 0 ordena por afinación y el 9 por orden de pulsación");

    // ── Modo 8: Random determinista con la semilla ──
    {
        abd::synth::Arpeggiator a1, a2;
        a1.prepare(44100.0);
        a1.setEnabled(true);
        a1.setMode(8);
        a1.setGate(0.5f);
        a1.setRandomSeed(1234);
        a1.noteOn(60, 1.0f);
        a1.noteOn(64, 0.9f);
        a1.noteOn(67, 0.8f);
        a2.prepare(44100.0);
        a2.setEnabled(true);
        a2.setMode(8);
        a2.setGate(0.5f);
        a2.setRandomSeed(1234);
        a2.noteOn(60, 1.0f);
        a2.noteOn(64, 0.9f);
        a2.noteOn(67, 0.8f);

        ArpCollector c1, c2;
        a1.generate(44100, c1);
        a2.generate(44100, c2);

        bool mismoRecorrido = (c1.n == c2.n);
        for (int i = 0; mismoRecorrido && i < c1.n; ++i)
            mismoRecorrido = c1.ev[i].note == c2.ev[i].note && c1.ev[i].sampleOffset == c2.ev[i].sampleOffset && c1.ev[i].isNoteOn == c2.ev[i].isNoteOn;
        check(mismoRecorrido, "Random con la misma semilla repite el mismo recorrido");

        bool soloPool = true;
        for (int i = 0; i < c1.n; ++i)
            if (c1.ev[i].isNoteOn && c1.ev[i].note != 60 && c1.ev[i].note != 64 && c1.ev[i].note != 67)
                soloPool = false;
        check(soloPool, "Random solo elige notas del pool");
    }

    // ── Modo 10: Chord, las 3 notas en el MISMO instante ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(10);
        arp.setGate(0.5f);
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 0.9f);
        arp.noteOn(67, 0.8f);

        ArpCollector c;
        arp.generate(44100, c);

        bool acorde = c.n >= 3;
        if (acorde)
            acorde = c.ev[0].isNoteOn && c.ev[1].isNoteOn && c.ev[2].isNoteOn && c.ev[0].note == 60 && c.ev[1].note == 64 && c.ev[2].note == 67 && c.ev[0].sampleOffset == c.ev[1].sampleOffset && c.ev[0].sampleOffset == c.ev[2].sampleOffset;
        check(acorde, "el modo Chord emite las 3 notas juntas en la misma muestra");
    }
}

static void testArpeggiatorNotesAndOctaves()
{
    printf("=== Arpeggiator: octavas, HOLD, KEY SYNC y notas colgadas ===\n");

    // ── Pool de 2 octavas: [60,64,67] + [72,76,79] ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.5f);
        arp.setOctaves(2);
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 0.9f);
        arp.noteOn(67, 0.8f);

        ArpCollector c;
        arp.generate(44100, c);

        const int esperado6[6] = {60, 64, 67, 72, 76, 79};
        int onNotes[64], on = 0;
        for (int i = 0; i < c.n; ++i)
            if (c.ev[i].isNoteOn && on < 64) onNotes[on++] = c.ev[i].note;

        bool ok = on >= 6;
        for (int k = 0; ok && k < 6; ++k) ok = (onNotes[k] == esperado6[k]);
        check(ok, "2 octavas amplían el pool a [60,64,67,72,76,79]");
    }

    // ── setOctaves(99) se clampa a 6, y 60+12·6 = 132 > 127 no se poola ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.5f);
        arp.setOctaves(99);
        arp.noteOn(60, 1.0f);

        ArpCollector c;
        arp.generate(44100, c);

        const int esperado6[6] = {60, 72, 84, 96, 108, 120};
        int onNotes[64], on = 0;
        for (int i = 0; i < c.n; ++i)
            if (c.ev[i].isNoteOn && on < 64) onNotes[on++] = c.ev[i].note;

        bool ok = on >= 6;
        for (int k = 0; ok && k < 6; ++k) ok = (onNotes[k] == esperado6[k]);
        check(ok, "setOctaves(99) queda en 6 y no poola nada por encima de 127");
    }

    // ── Nota 125 con 2 octavas: 137 > 127 se salta; solo queda la 125 ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.5f);
        arp.setOctaves(2);
        arp.noteOn(125, 1.0f);

        ArpCollector c;
        arp.generate(44100, c);

        bool solo125 = c.n > 0;
        for (int i = 0; i < c.n; ++i)
            if (c.ev[i].isNoteOn && c.ev[i].note != 125) solo125 = false;
        check(solo125, "el byte MIDI no da para más: la octava > 127 no suena nunca");
    }

    // ── HOLD: soltar la tecla no saca la nota del ciclo ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setHold(true);
        arp.noteOn(60, 1.0f);
        arp.noteOff(60);
        check(arp.getHeldCount() == 1, "con HOLD la nota sigue retenida al soltar la tecla");

        arp.setHold(false);
        arp.noteOff(60);
        check(arp.getHeldCount() == 0, "sin HOLD la nota se va al soltar");
    }

    // ── KEY SYNC: pulsar una tecla reinicia el ciclo al primer paso ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.5f);
        arp.setKeySync(true);
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 0.9f);
        arp.noteOn(67, 0.8f);

        ArpCollector c1;
        arp.generate(44100, c1); // avanza ~10 pasos

        arp.noteOn(62, 0.5f); // tecla nueva: key sync reinicia

        ArpCollector c2;
        arp.generate(44100, c2);

        int primerOn = -1;
        for (int i = 0; i < c2.n; ++i)
            if (c2.ev[i].isNoteOn)
            {
                primerOn = c2.ev[i].note;
                break;
            }

        // Sin el reinicio, el paso 10 % 4 = 2 daría 64; con él, vuelve al 60.
        check(primerOn == 60, "KEY SYNC reinicia el ciclo: la nota nueva vuelve a empezar por 60");
    }

    // ── Sin notas colgadas: allNotesOff() y el flush del bloque siguiente ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setGate(0.0f); // gate 0: el motor NO programa apagados
        arp.noteOn(60, 1.0f);

        ArpCollector c1;
        arp.generate(44100, c1);
        bool huboOn = false;
        for (int i = 0; i < c1.n; ++i)
            if (c1.ev[i].isNoteOn) huboOn = true;
        check(huboOn, "con gate 0 la nota suena (sin apagado programado)");

        arp.allNotesOff();

        ArpCollector c2;
        arp.generate(44100, c2);
        int offs     = 0;
        bool offDe60 = true;
        for (int i = 0; i < c2.n; ++i)
        {
            if (!c2.ev[i].isNoteOn)
            {
                ++offs;
                if (c2.ev[i].note != 60) offDe60 = false;
            }
        }
        check(offs == 1 && offDe60, "allNotesOff() hace que el flush apague exactamente lo que sonaba");

        ArpCollector c3;
        arp.generate(44100, c3);
        check(c3.n == 0, "una vez apagado, no queda NI UNA nota colgada");
    }
}

static void testArpeggiatorGateAndZeroAlloc()
{
    printf("=== Arpeggiator: ley de la puerta y zero-alloc ===\n");

    // Bloques de 2756 muestras: solo cabe el paso 0 (par, duracion 2756,25 a
    // 44,1 kHz / 8 Hz con swing 0). Así el apagado programado cae DENTRO del
    // segundo bloque y se puede medir sin interferencias de retriggers.
    const int kBloque = 2756;

    // gate 0.5 -> lround(2756.25 · 0.5) = 1378
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.5f);
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 0.9f);
        arp.noteOn(67, 0.8f);

        ArpCollector c1;
        arp.generate(kBloque, c1);
        int offsEnC1 = 0;
        for (int i = 0; i < c1.n; ++i)
            if (!c1.ev[i].isNoteOn) ++offsEnC1;
        check(offsEnC1 == 0, "el apagado NO se emite en el mismo bloque: queda pendiente");

        ArpCollector c2;
        arp.generate(kBloque, c2);
        int primerOff = -1;
        for (int i = 0; i < c2.n; ++i)
            if (!c2.ev[i].isNoteOn)
            {
                primerOff = c2.ev[i].sampleOffset;
                break;
            }
        check(primerOff == 1378, "gate 0.5 apaga en lround(duracion · 0.5) = 1378 muestras");
    }

    // gate 0.25 -> lround(2756.25 · 0.25) = 689
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.25f);
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 0.9f);
        arp.noteOn(67, 0.8f);

        ArpCollector c1;
        arp.generate(kBloque, c1);
        ArpCollector c2;
        arp.generate(kBloque, c2);
        int primerOff = -1;
        for (int i = 0; i < c2.n; ++i)
            if (!c2.ev[i].isNoteOn)
            {
                primerOff = c2.ev[i].sampleOffset;
                break;
            }
        check(primerOff == 689, "gate 0.25 apaga en 689 muestras (la ley es lineal en gate)");
    }

    // gate 0: ningún apagado programado, nunca.
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.0f);
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 0.9f);
        arp.noteOn(67, 0.8f);

        ArpCollector c1;
        arp.generate(kBloque, c1);
        ArpCollector c2;
        arp.generate(kBloque, c2);
        int offs = 0;
        for (int i = 0; i < c2.n; ++i)
            if (!c2.ev[i].isNoteOn) ++offs;
        check(offs == 0, "gate 0 no programa NINGÚN apagado (0 = silencio, no = corte)");
    }

    // ── Zero-alloc: 32 generate() de 1024 muestras sin mover el contador ──
    {
        abd::synth::Arpeggiator arp;
        arp.prepare(44100.0);
        arp.setEnabled(true);
        arp.setMode(0);
        arp.setGate(0.5f);
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 0.9f);
        arp.noteOn(67, 0.8f);

        ArpCollector z;
        const long long antes = gTestAllocations;
        for (int i = 0; i < 32; ++i)
            arp.generate(1024, z);
        const long long delta = gTestAllocations - antes;

        printf("  (alojamientos: %lld antes, %lld durante 32 generate())\n", antes, delta);
        check(delta == 0, "generate() corre entero sin alojar memoria (zero-alloc)");
        check(z.n > 0, "y encima emite eventos (el test no mide una corrida vacía)");
    }
}

// ─────────────── Retrigger: off+on en la MISMA muestra del paso ───────────────
//
// La ley que fija la corrección de sounding[]: cuando una nota que YA suena
// vuelve a salir en un paso (pool de una sola nota, cada ciclo la repite), el
// motor la apaga y reenciende EN LA MUESTRA DEL PASO: el off del retrigero va
// justo delante del on nuevo, con el mismo sampleOffset, y nunca suelto. Y
// como cada retrigero QUITA la entrada vieja de sonando, unos cuantos
// retrigeros después sigue habiendo UNA sola nota sonando: el flush de
// allNotesOff apaga exactamente una vez — ni cero (nota colgada) ni ocho
// (entradas duplicadas en la lista).
//
// Con gate 0 el motor no programa apagados de puerta, así que TODOS los off
// del flujo son off de retrigero: la ley se puede exigir sin excepciones. Y
// si la corrección de sonando[] se revertiera, cada marca emitiría un off por
// cada entrada duplicada — los off dejarían de emparejarse con su on — y este
// test fallaría.
static void testArpeggiatorRetrigger()
{
    printf("=== Arpeggiator: retrigero off+on en la misma muestra ===\n");

    abd::synth::Arpeggiator arp;
    arp.prepare(44100.0);
    arp.setEnabled(true);
    arp.setMode(0);    // Up: el pool es [60] y cada paso repite la nota
    arp.setGate(0.0f); // sin puerta: todo off es de retrigero
    arp.noteOn(60, 0.9f);

    ArpCollector c;
    arp.generate(44100, c); // ~8 pasos de 8 Hz dentro del segundo

    int offs              = 0; // off emitidos (con gate 0, todos son retrigeros)
    int parejas           = 0; // ... de los cuales el SIGUIENTE evento es un on de la misma nota
    int mismaMuestra      = 0; // ... y además en el MISMO sampleOffset
    bool marcasCrecientes = true;
    int ultimaMarca       = -1;

    for (int i = 0; i < c.n; ++i)
    {
        if (c.ev[i].isNoteOn) continue;
        ++offs;

        if (c.ev[i].sampleOffset <= ultimaMarca) marcasCrecientes = false;
        ultimaMarca = c.ev[i].sampleOffset;

        if (i + 1 < c.n && c.ev[i + 1].isNoteOn && c.ev[i + 1].note == c.ev[i].note)
        {
            ++parejas;
            if (c.ev[i + 1].sampleOffset == c.ev[i].sampleOffset) ++mismaMuestra;
        }
    }

    check(offs >= 2, "una nota sola retriggeriza en cada paso: hay varios off en el segundo");
    check(parejas == offs, "cada off va seguido en el MISMO paso por su on de la misma nota");
    check(mismaMuestra == offs, "el off del retrigero y su on nuevo caen en la MISMA muestra");
    check(marcasCrecientes, "y cada retrigero es una marca de paso NUEVA, no la misma repetida");

    // ── sounding[] no se duplica: el flush apaga exactamente una vez ──
    arp.allNotesOff();
    ArpCollector d;
    arp.generate(44100, d);

    int offsFlush = 0;
    bool solo60   = true;
    for (int i = 0; i < d.n; ++i)
        if (!d.ev[i].isNoteOn)
        {
            ++offsFlush;
            if (d.ev[i].note != 60) solo60 = false;
        }

    check(d.n == 1 && offsFlush == 1 && solo60,
          "el flush apaga la nota 60 EXACTAMENTE una vez (ni cero ni ocho: sounding[] está deduplicado)");

    ArpCollector e;
    arp.generate(44100, e);
    check(e.n == 0, "y al bloque siguiente no queda ni un evento: sin off repetidos ni colgados");
}
