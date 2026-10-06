/*
  ==============================================================================

    S950PatchFields.h
    El CATALOGO de patches del Akai S950: que campos tiene un keygroup, donde
    vive cada uno y que rango de panel tiene (namespace abd::synth).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore).

    QUE ES. Un panel y un motor que no dicen lo mismo del mismo byte es la
    forma mas barata de perder tiempo: el motor aplica un rango, el panel
    muestra otro, y nadie se entera hasta que un patch importado suena raro.
    Este fichero es la respuesta — UNA tabla, en datos, de la que leen los dos.
    No decide nada: describe.

    DE DONDE VIENE, Y QUE SE COPIO. `_RESOURCES/Mz950-main` (AGPLv3) tiene la
    tabla equivalente repartida entre `S950/Disk.cpp` (offset, rango y codificacion
    de cada byte) y `S950/PluginProcessor.cpp` (los trims de Perform). Aqui NO se
    copio codigo: la tabla se reescribio desde cero, con otros nombres, otra
    agrupacion y otro contrato. Lo que SI son datos de la MAQUINA —el byte 42 es
    el fine de la zona soft, y va de 0 a 255— son hechos, no codigo, y un
    importador los necesita igual. Ver `docs/mz950-reaprovechamiento.md`, fila B3.

    LAS TRES COSAS QUE HACE ESTA TABLA, Y POR QUE CADA UNA PIDE ALGO DISTINTO.

      1. DICE DONDE. `byteOffset` es la posicion DENTRO del registro de 70 bytes
         de un keygroup, no dentro del fichero: un programme esta encadenado a
         traves de la tabla de asignacion, asi que un keygroup de 70 bytes puede
         partirse entre el final de un bloque y el principio de otro que no tiene
         nada que ver. Un offset absoluto en el fichero seria un numero que
         miente.      2. DICE COMO SE LEE. `encoding` no es decorativo. Tres de los cuatro tipos
         guardados no son "un byte": el signo, el puerto y los bits comparten
         byte con otras cosas, y quien los lea como un `uint8` obtendra un numero
         que existe y no significa nada. Ver la nota de ENCODINGS.

      3. DICE QUE RANGO TIENE, Y NO ES SIEMPRE 0..99. El 0..99 es lo que el
         panel imprime, pero el almacenamiento va mas lejos: los keys son
         0..127 (una tecla MIDI entera), el fine de zona es         0..255 (medio byte de un offset de altura de 16 bits, en dieciseiseavos
         de semitono), y el switch de velocidad es 1..128 con un 128 que
         significa "no hay segunda zona". Un importador que recorte a 0..99
         porque es lo que ve escrito en el panel estara bien en 35 de 38 campos
         y mal en los otros tres.

    ENCODINGS. Los cuatro:

      - `Unsigned`: el byte tal cual.
      - `Signed`: complemento a dos de 8 bits, o sea -50..+50. El byte 128 no es
        128, es -128, y el 255 no es 255, es -1.
      - `Port`: el panel 0..10 se guarda UNO MENOS, y "todos" se guarda 0xFF.
        O sea que el 0xFF del disco es el 0 del panel, y no un 255.
      - `Bit`: un bit suelto del byte 18. Los cuatro flags de este grupo
        comparten byte, asi que escribirlos de uno en uno —leer, cambiar un bit,
        escribir— es lo que deja vivos los bits que nadie ha descifrado todavia.
        Por eso `bitMask` existe y por eso `setKeygroupField` tiene que conservar
        el resto del byte en vez de escribir el valor entero.

    EL BYTE 18 TIENE CUATRO BITS CONOCIDOS Y UNO QUE NO. Los conocidos son los de
    esta tabla, y_masked 0x1D. Lo que hay a 0x02 no esta decodificado por el
    estudio, asi que aqui se declara explicitamente (`reservedBitMask`) en vez de
    dejarse como un misterio: un byte del que nadie sabe que un bit esta vivo es un
    byte que se pierde en la primera escritura.

    LO QUE ESTE FICHERO NO HACE.

      - No lee ni escribe discos. Eso es trabajo de `S950Disk.h`, que es quien
        consulta esta tabla; aqui no hay I/O ni estado.
      - No decide los valores por defecto de un preset. Un preset es una
        combinacion de estos campos, no una fila mas.
      - No contiene las curvas de unidades. Que el byte 4 valga 99 son 4.2
        segundos es un dato MEDIDO, de otra tabla, y meterlo aqui seria
        prometer una precision que un campo de panel no tiene. Ver el comentario
        de `PatchField` sobre por que el rango va en unidades de panel.

    CONTRATO. Cabecera pura, sin estado, sin asignaciones, `constexpr`, C++17,
    `noexcept`. `patchFieldCount` y `fieldAt()` no lanzan: un indice fuera de rango
    es un bug de compilacion, no una condicion de ejecucion, y por eso hay
    `findField()` para lo que si puede fallar en runtime. Lo que SI tiene estado y
    reserva memoria es `S950Disk.h`, que es quien consulta esta tabla.

  ==============================================================================
*/

#pragma once

#include <cstddef>
#include <cstdint>

namespace abd::synth
{

//==============================================================================
/** Un campo de un keygroup del S950, tal y como lo guarda el disco.

    Solo lo que hace falta para ENCONTRARLO y para SABER QUE RANGO TIENE. Los
    campos van en unidades de panel, no en segundos ni en hercios: un motor los
    convierte con su propia tabla de curvas, y meter aqui una conversion sería
    atar este fichero a una forma de tocar un filtro que no es la suya.

    @code       el nombre corto y estable, en camelCase. Es la clave con la que
                un motor, un importador o un preset la referencian, así que
                forma parte del contrato: renombrarla rompe lo que ya existe.
    @name       el nombre que ve la persona. Es de la maquina, no de este
                proyecto, y va en ingles como va en el panel.
    @byteOffset posicion dentro del registro de 70 bytes del keygroup.
    @lo, @hi   el rango de panel, inclusivo. Es lo que recorta una escritura.
    @encoding   como se lee y escribe el byte. Ver ENCODINGS en la cabecera.
    @bitMask    solo para `Bit`: que bit del byte es.
    @group      en que pestana del panel aparece, que es como se lee la tabla.
    @trimId     el trim de Perform que lo mueve, o cadena vacia si ninguno lo
                mueve. Un campo con trim es un campo cuyo valor efectivo es
                `valor + trim`, y por eso estan en la misma tabla y no en dos.
    @unit       lo que cuenta la unidad, para quien tenga que explicar el numero.
*/
struct PatchField
{
    const char* code;
    const char* name;
    int byteOffset;
    int lo;
    int hi;
    enum class Encoding
    {
        Unsigned,
        Signed,
        Port,
        Bit
    };
    Encoding encoding;
    std::uint8_t bitMask;
    const char* group;
    const char* trimId;
    const char* unit;
};

//==============================================================================
/** Un trim de Perform: un OFFSET sobre el valor de un campo del keygroup.

    La regla de la maquina que hace que esto sea un offset y no un valor es la de
    la fila C3 del inventario: el trim se SUMA al valor del keygroup, no lo
    sustituye. La diferencia se ve al cambiar de patch — un offset deja la
    relacion entre keygroups como el programador la chose; un valor absoluto la
    borra.

    Que el recorrido sea 0..99 y no mas es una decision medida, no una comodidad:
    un span de 99 en cualquier dirección permite alcanzar todo el recorrido de un
    campo desde cualquier punto de partida. El coste es que el centro del mando
    no es el centro del rango, y es el intercambio correcto: el centro es "como
    estaba grabado", que es el valor que cualquiera quiere volver a encontrar.

    CON UNA EXCEPCION MEDIDA: `vcfAmount` lleva 50 y no 99. Con la logica de
    arriba haria falta 100, porque su campo va de -50 a +50 y hace falta un
    offset de ese tamaño para llegar a un extremo desde el otro. Se evaluó y se
    descartó por un dato de la biblioteca: el VCF amount es el MISMO valor en
    todos los keygroups de todos los programas, asi que no hay reparto entre
    ellos que un offset tenga que preservar, y llega igual. Lo que se pierde es
    poder invertir un keygroup que ya esta en +50, que son 34 en toda la
    biblioteca. Un offset no tiene que alcanzar los dos extremos; tiene que ser
    mas estrecho que el campo que mueve.

    @code      identificador estable del trim.
    @name      lo que ve la persona.
    @fieldCode el campo del keygroup que mueve, o cadena vacia si mueve algo que
               no esta en el keygroup (la resonancia, el LFO al filtro, la forma
               del LFO: controles que la maquina no tenia).
    @lo, @hi   recorrido del OFFSET, no del campo. Un offset va de -span a +span.
    @bipolar   si el offset suma y resta. Los que solo suman son PROFUNDIDADES, y
               una profundidad negativa no es "un poco menos", es nada.
    @unit      lo que cuenta la unidad del offset.
*/
struct PerformTrim
{
    const char* code;
    const char* name;
    const char* fieldCode;
    int lo;
    int hi;
    bool bipolar;
    const char* unit;
};

//==============================================================================
/** La tabla. Treinta y ocho campos, en el orden en que se recorre un keygroup.

    El orden es el del panel por Byte, no el alfabetico, porque es el orden en el
    que uno los va a buscar. `constexpr` con `inline` para que sea una sola
    instancia con varias unidades de traduccion enlazadas a la vez, que es
    justo lo que pasa con un target INTERFACE.
*/
inline constexpr PatchField patchFields[] =
    {
        //--- Byte 0..2: que teclas y que velocidades responden
        {"lowKey", "Low key", 0, 0, 127, PatchField::Encoding::Unsigned, 0x00, "KEYS", "", "nota MIDI"},
        {"highKey", "High key", 1, 0, 127, PatchField::Encoding::Unsigned, 0x00, "KEYS", "", "nota MIDI"},
        // El 128 es el valor que dice "no hay segunda zona", y no hay velocidad que
        // llegue a el. Por eso el rango es 1..128 y no 0..127: el 0 no es un valor
        // valido, es el hueco que deja el switch cuando no hay nada que repartir.
        {"velocitySwitch", "Velocity switch", 2, 1, 128, PatchField::Encoding::Unsigned, 0x00, "VELOCITY", "", "nota MIDI"},

        //--- Byte 3..6: envolvente de amplitud
        {"vcaAttack", "VCA attack", 3, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcaAttack", "0..99"},
        {"vcaDecay", "VCA decay", 4, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcaDecay", "0..99"},
        {"vcaSustain", "VCA sustain", 5, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcaSustain", "0..99"},
        {"vcaRelease", "VCA release", 6, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcaRelease", "0..99"},

        //--- Byte 7..11: la velocidad y el teclado moviendo cosas
        {"velToFilter", "Velocity to filter", 7, 0, 99, PatchField::Encoding::Unsigned, 0x00, "VELOCITY", "velToFilter", "0..99"},
        {"keyToFilter", "Key to filter", 8, 0, 99, PatchField::Encoding::Unsigned, 0x00, "FILTER", "", "0..99"},
        {"velToAttack", "Velocity to attack", 9, 0, 99, PatchField::Encoding::Unsigned, 0x00, "VELOCITY", "", "0..99"},
        // Los dos siguientes NO tienen trim, y es una decision, no un olvido: en
        // el estudio ninguno de los dos se puede mover con un offset. Una version
        // anterior de esta tabla les puso un trimId con su propio nombre, y la
        // referencia colgaba: el campo apuntaba a un trim que no existia. No hacia
        // falta ningun motor para verlo —lo vio un test que mira al reves— y
        // hacia justo el daño que este catalogo existe para evitar.
        {"velToRelease", "Velocity to release", 10, -50, 50, PatchField::Encoding::Signed, 0x00, "VELOCITY", "", "-50..+50"},
        {"velToLoudness", "Velocity to loudness", 11, 0, 99, PatchField::Encoding::Unsigned, 0x00, "VELOCITY", "velToLoudness", "0..99"},

        //--- Byte 12..14: WARP, un bend en el ataque
        {"warpVelocity", "Warp velocity", 12, 0, 99, PatchField::Encoding::Unsigned, 0x00, "TUNING", "", "0..99"},
        {"warpDepth", "Warp depth", 13, -50, 50, PatchField::Encoding::Signed, 0x00, "TUNING", "", "semitonos"},
        {"warpTime", "Warp time", 14, 0, 99, PatchField::Encoding::Unsigned, 0x00, "TUNING", "", "0..99"},

        //--- Byte 15..17 y 21..22: el LFO
        {"lfoDelay", "LFO delay", 15, 0, 99, PatchField::Encoding::Unsigned, 0x00, "LFO", "lfoDelay", "0..99"},
        {"lfoRate", "LFO rate", 16, 0, 99, PatchField::Encoding::Unsigned, 0x00, "LFO", "lfoRate", "0..99"},
        {"lfoDepth", "LFO depth", 17, 0, 99, PatchField::Encoding::Unsigned, 0x00, "LFO", "lfoDepth", "0..99"},
        // Los dos siguientes son 0..50 y no 0..99 porque el S950 los imprime asi. Un
        // mando que llega a 99 donde la maquina llega a 50 es un mando que hay que
        // explicar, y el criterio de la maquina es que el panel manda.
        {"lfoAftertouch", "LFO from aftertouch", 21, 0, 50, PatchField::Encoding::Unsigned, 0x00, "LFO", "", "0..50"},
        {"lfoModwheel", "LFO from modwheel", 22, 0, 50, PatchField::Encoding::Unsigned, 0x00, "LFO", "", "0..50"},

        //--- Byte 19: la salida
        {"outputPort", "Output", 19, 0, 10, PatchField::Encoding::Port, 0x00, "KEYS", "", "puerto"},

        //--- Byte 23 y 34..37: el filtro y su envolvente
        {"vcfAmount", "VCF amount", 23, -50, 50, PatchField::Encoding::Signed, 0x00, "FILTER", "vcfAmount", "-50..+50"},
        {"vcfAttack", "VCF attack", 34, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcfAttack", "0..99"},
        {"vcfDecay", "VCF decay", 35, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcfDecay", "0..99"},
        {"vcfSustain", "VCF sustain", 36, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcfSustain", "0..99"},
        {"vcfRelease", "VCF release", 37, 0, 99, PatchField::Encoding::Unsigned, 0x00, "ENVELOPES", "vcfRelease", "0..99"},

        //--- Byte 42..45: la zona SUAVE, la de la velocidad baja
        // El fine es 0..255 porque NO es una altura: es el byte BAJO de un offset de
        // altura de 16 bits con signo, y lo alto es el transpose. Los dos juntos son
        // un offset en dieciseiseavos de semitono. Leer el fine como 0..99 y
        // recortarlo es el error de un cuarto de tono sin que nada se queje.
        {"softFine", "Soft fine", 42, 0, 255, PatchField::Encoding::Unsigned, 0x00, "TUNING", "", "1/16 semitono"},
        {"softTranspose", "Soft transpose", 43, -50, 50, PatchField::Encoding::Signed, 0x00, "TUNING", "", "semitonos"},
        {"softFilter", "Soft filter", 44, 0, 99, PatchField::Encoding::Unsigned, 0x00, "FILTER", "vcfCutoff", "0..99"},
        {"softLoudness", "Soft loudness", 45, -50, 50, PatchField::Encoding::Signed, 0x00, "VELOCITY", "", "-50..+50"},

        //--- Byte 64..67: la zona DURA, la de la velocidad alta
        // Mismo grupo que el anterior, 22 bytes mas adelante. Un keygroup puede
        // tener las dos zonas con cortes distintos —el 54% de los keygroups de dos
        // zonas de la biblioteca lo hace— y por eso son ocho campos y no cuatro.
        {"loudFine", "Loud fine", 64, 0, 255, PatchField::Encoding::Unsigned, 0x00, "TUNING", "", "1/16 semitono"},
        {"loudTranspose", "Loud transpose", 65, -50, 50, PatchField::Encoding::Signed, 0x00, "TUNING", "", "semitonos"},
        {"loudFilter", "Loud filter", 66, 0, 99, PatchField::Encoding::Unsigned, 0x00, "FILTER", "vcfCutoff", "0..99"},
        {"loudLoudness", "Loud loudness", 67, -50, 50, PatchField::Encoding::Signed, 0x00, "VELOCITY", "", "-50..+50"},

        //--- Byte 18, cuatro bits sueltos que comparten byte
        {"constantPitch", "Constant pitch", 18, 0, 1, PatchField::Encoding::Bit, 0x01, "TUNING", "", "si/no"},
        {"lfoDesync", "LFO desync", 18, 0, 1, PatchField::Encoding::Bit, 0x04, "LFO", "", "si/no"},
        {"oneShot", "One shot", 18, 0, 1, PatchField::Encoding::Bit, 0x08, "KEYS", "", "si/no"},
        {"velocityReleaseOn", "Velocity release on", 18, 0, 1, PatchField::Encoding::Bit, 0x10, "VELOCITY", "", "si/no"},
};

/** Cuantos campos hay. La tabla es la verdad; esto se comprueba contra ella. */
inline constexpr int patchFieldCount =
    static_cast<int>(sizeof(patchFields) / sizeof(patchFields[0]));

//==============================================================================
/** El byte 18, con sus bits que nadie ha descodificado y que hay que conservar.

    Los cuatro flags que se conocen de este byte son `reservedBitMask` ^ 0xFF, es
    decir 0x1D. El 0x02 no lo descifra el estudio, asi que se declara en vez de
    dejarlo como un misterio: un byte del que nadie sabe que un bit esta vivo es
    un byte que se pierde en la primera escritura. Ver `setKeygroupField`.
*/
inline constexpr std::uint8_t keygroupFlagsByte = 18;
inline constexpr std::uint8_t knownFlagsMask    = 0x1D;
inline constexpr std::uint8_t reservedBitMask   = 0xE2; // lo que NO es de esta tabla

//==============================================================================
/** Los cuatro nombres del byte de salida, en el orden del panel.

    El valor guardado va UNO MENOS que el del panel, y el 0xFF del disco es el 0
    del panel. La tabla se da en valores de PANEL, que son los que un importador
    quiere escribir y los que un motor quiere leer; el desfase lo aplica el que
    toque el byte, en `readKeygroupField` / `writeKeygroupField`.
*/
struct OutputPort
{
    int panelValue; // 0..10, lo que dice el panel
    const char* name;
    float left;
    float right;
};

inline constexpr OutputPort outputPorts[] =
    {
        {0, "ALL", 1.0f, 1.0f},
        {1, "MONO1", 1.0f, 1.0f},
        {2, "MONO2", 1.0f, 1.0f},
        {3, "MONO3", 1.0f, 1.0f},
        {4, "MONO4", 1.0f, 1.0f},
        {5, "MONO5", 1.0f, 1.0f},
        {6, "MONO6", 1.0f, 1.0f},
        {7, "MONO7", 1.0f, 1.0f},
        {8, "MONO8", 1.0f, 1.0f},
        {9, "LEFT", 1.0f, 0.0f},
        {10, "RIGHT", 0.0f, 1.0f},
};

inline constexpr int outputPortCount =
    static_cast<int>(sizeof(outputPorts) / sizeof(outputPorts[0]));

//==============================================================================
/** Los trims de Perform: los offsets que el ejecutor mueve encima del keygroup.

    Dieciocho filas para quince controles, porque `softFilter` y `loudFilter` son
    UN control —el filtro del keygroup— sobre los dos bytes. Es la decision
    correcta: un keygroup puede tener las dos zonas con cortes distintos, y
    moverlas por separado dejaria al usuario sin la relacion que el programador
    eligio.

    Los tres con `fieldCode` vacio son controles que la maquina NO tenia. No son
    una excusa para rellenar el hueco: son la diferencia entre un S950 y un
    S950 con resonancia, que es justo lo que la fila C1 del inventario dice que
    falta.
*/
inline constexpr PerformTrim performTrims[] =
    {
        {"vcfCutoff", "VCF Filter", "softFilter", -99, 99, true, "0..99 del panel"},
        {"vcfAmount", "VCF Amnt", "vcfAmount", -50, 50, true, "-50..+50"},
        {"vcaAttack", "VCA Attack", "vcaAttack", -99, 99, true, "0..99 del panel"},
        {"vcaDecay", "VCA Decay", "vcaDecay", -99, 99, true, "0..99 del panel"},
        {"vcaSustain", "VCA Sustain", "vcaSustain", -99, 99, true, "0..99 del panel"},
        {"vcaRelease", "VCA Release", "vcaRelease", -99, 99, true, "0..99 del panel"},
        {"vcfAttack", "VCF Attack", "vcfAttack", -99, 99, true, "0..99 del panel"},
        {"vcfDecay", "VCF Decay", "vcfDecay", -99, 99, true, "0..99 del panel"},
        {"vcfSustain", "VCF Sustain", "vcfSustain", -99, 99, true, "0..99 del panel"},
        {"vcfRelease", "VCF Release", "vcfRelease", -99, 99, true, "0..99 del panel"},
        {"lfoRate", "LFO Rate", "lfoRate", -99, 99, true, "0..99 del panel"},
        {"lfoDelay", "LFO Delay", "lfoDelay", -99, 99, true, "0..99 del panel"},
        // La profundidad SOLO SUMA. Casi todos los patches de la biblioteca la dejan
        // en 0, así que un mando simetrico gastaria media vuelta pidiendo menos que
        // nada. Se evaluó hacerlo bipolar y se descartó: no hay nada que quitar.
        {"lfoDepth", "LFO Pitch Depth", "lfoDepth", 0, 99, false, "0..99 del panel"},
        {"velToFilter", "Vel Freq", "velToFilter", 0, 99, false, "0..99 del panel"},
        {"velToLoudness", "Vel Loudness", "velToLoudness", 0, 99, false, "0..99 del panel"},
        {"lfoToFilter", "LFO Filter Depth", "", 0, 99, false, "0..99 del panel"},
        {"resonance", "Resonance", "", 0, 99, false, "0..99"},
        {"lfoShape", "LFO Shape", "", 0, 3, false, "indice"},
};

inline constexpr int performTrimCount =
    static_cast<int>(sizeof(performTrims) / sizeof(performTrims[0]));

//==============================================================================
/** La geometria del registro, que el catalogo no puede deducir de si mismo.

    Los offsets de aquí son de POSICION, no de campo: los campos viven en
    `byteOffset`, dentro de estos limites. Lo que hay entre ellos —los nombres de
    zona, la cadena de punteros— no es ningun campo del panel, y por eso no está en
    la tabla de arriba sino aquí.
*/
inline constexpr int keygroupRecordSize    = 70;  // bytes por keygroup
inline constexpr int programHeaderSize     = 38;  // bytes antes del primer keygroup
inline constexpr int keygroupZoneStride    = 22;  // la zona 2 esta 22 bytes mas alla
inline constexpr int keygroupNameOffset    = 24;  // el nombre del sample de la zona 1
inline constexpr int keygroupNameSize      = 10;  // caracteres del nombre
inline constexpr int keygroupChainOffset   = 68;  // puntero a la zona, 12 bytes al final
inline constexpr int keygroupVelocityCount = 128; // velocidades que responde un keygroup
inline constexpr int keygroupMaxZones      = 2;   // zonas de velocidad, NO capas
inline constexpr int keygroupMaxCount      = 64;  // keygroups por programa
inline constexpr int keygroupCountOffset   = 23;  // byte de la cabecera que lleva el numero
inline constexpr int programNumberOffset   = 26;  // numero de programa que ve el panel
inline constexpr int fileNameSize          = 10;  // nombre de fichero, en el directorio

//==============================================================================
/** El campo de la tabla, por indice. `index` en 0..`patchFieldCount`; fuera de
    rango es un bug de compilacion, no una condicion de ejecucion. */
constexpr const PatchField& fieldAt(int index) noexcept
{
    return patchFields[index];
}

/** El campo por su codigo, o `nullptr` si no existe. Para lo que puede fallar
    en runtime: un preset traido de fuera puede pedir un campo que no esta. */
const PatchField* findField(const char* code) noexcept
{
    if (code == nullptr)
        return nullptr;

    for (int i = 0; i < patchFieldCount; ++i)
    {
        // Comparacion por codigo, sin `std::string`: esta tabla se consulta
        // desde el hilo de audio, y recorrer los caracteres es mejor que
        // construir nada.
        const char* a = patchFields[i].code;
        const char* b = code;

        while (*a != '\0' && *a == *b)
        {
            ++a;
            ++b;
        }

        if (*a == *b)
            return &patchFields[i];
    }

    return nullptr;
}

/** El trim por su codigo, o `nullptr` si no existe. */
const PerformTrim* findTrim(const char* code) noexcept
{
    if (code == nullptr)
        return nullptr;

    for (int i = 0; i < performTrimCount; ++i)
    {
        const char* a = performTrims[i].code;
        const char* b = code;

        while (*a != '\0' && *a == *b)
        {
            ++a;
            ++b;
        }

        if (*a == *b)
            return &performTrims[i];
    }

    return nullptr;
}

/** Los dos bytes de altura de una zona, montados en el offset que represents.

    El transpose es el byte ALTO con signo y el fine el BAJO sin signo: juntos,
    dieciseiseavos de semitono, que es como la maquina cuenta la altura. Por eso
    el fine va 0..255 y no 0..99 — no es una escala de panel, es la mitad baja de
    un numero de 16 bits, y recortarlo a 99 seria un cuarto de tono de error sin
    que nada fallara.

    @returns  el offset en dieciseiseavos de semitono, signed.
*/
constexpr int zonePitchOffset(const PatchField& fine, int fineValue,
                              const PatchField& transpose, int transposeValue) noexcept
{
    return (transposeValue << 8) | (fineValue & 0xFF);
}

//==============================================================================
/** Recorta un valor al rango de un campo, como hace el panel.

    Es la operacion que un importador y un mando tienen que hacer los dos, y que
    si cada uno hace la suya acaba siendo distinta. `dspAssert` en vez de nada:
    un rango con lomin > hi es un error de tabla, no de datos.
*/
constexpr int clampToField(const PatchField& field, int value) noexcept
{
    return value < field.lo ? field.lo : (value > field.hi ? field.hi : value);
}

} // namespace abd::synth
