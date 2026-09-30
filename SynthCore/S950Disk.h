/*
  ==============================================================================

    S950Disk.h
    Lee y ESCRIBE los bytes de un programa del Akai S900/S950 sobre la imagen
    de un disco, usando el catalogo de S950PatchFields.h (namespace abd::synth).

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::SynthCore).

    QUE ES, Y POR QUE NO ES "un struct con los campos". El programa de una
    maquina de 1995 no esta en un array: esta en una imagen de 800 o 1600
    bloques de 1024 bytes, repartida por una tabla de asignacion, encadenada. Un
    keygroup de 70 bytes puede empezar en el byte 1010 de un bloque y terminar en
    el byte 9 del SIGUIENTE, que no tiene por que ser el siguiente en numero.
    O sea que un offset NO es una direccion: es un indice dentro de una cadena.

    Por eso la primera mitad de este fichero es la geometria del disco
    (directorio, tabla de asignacion, cadena) y la segunda son los campos. Y por
    eso la lectura y la escritura van de un byte en uno, resolviendo la cadena
    por el camino, en vez de pedir un `memcpy` de 70 bytes: un keygroup que
    pasa de un bloque a otro no es un rango contiguo, y un memcpy daria basura
    sin quejarse.

    LO QUE ESCRIBE, Y LO QUE NO. Escribe lo que se puede escribir en su sitio: el
    VALOR de un campo. No anade ni quita ficheros, ni samples, ni keygroups, salvo
    crear un programa nuevo, que si es reversible y se hace al final.

    Y esto es una decision, no una limitacion que se quede sin hacer: cambiar la
    estructura de un disco es peligroso por una razon concreta —la tabla de
    asignacion es una lista encadenada y el directorio tiene que seguir siendo
    contiguo— y ese trabajo pertenece a un modulo de edicion con sus propias
    pruebas, no a un importador. Un importador que intentase reescribir la
    estructura sin esas pruebas seria peor que uno que no la toca.

    LAS TRES TRAMPAS DEL FORMATO, Y DONDE ESTAN RESUELTAS.

      1. EL BYTE VA UNO POR UNO. `keygroupByteAt()` resuelve un offset del
         registro a una posicion de la imagen cruzando la cadena. Ver el
         comentario de arriba: el caso raro no es raro, es lo normal.

      2. EL 0xFF DEL PUERTO ES EL 0 DEL PANEL. El byte de salida guarda el valor
         del panel MENOS UNO, y el "todos" se guarda como 0xFF. Leido como
         `uint8`, el 0xFF es 255, que no es una salida: es la de todos. Esta en
         `readKeygroupField` / `writeKeygroupField`.

      3. LOS CUATRO FLAGS COMPARTEN EL BYTE 18. Escribir el valor entero
         perderia los otros tres, y el bit 0x02, que nadie ha descifrado, con
         ellos. Por eso un flag se escribe con lectura-modificacion-escritura del
         byte. Ver `writeKeygroupField`.

    LA CUARTA, QUE ES UNA DE ESTO MAS, Y MAS INTERESANTE: LA ENVOLVENTE DEL
    FILTRO EN BLANCO. Un S900 no tenia envolvente de filtro, y lo que escribia
    en sus cuatro bytes no era una envolvente: eran ESPACIOS. Un 0x20 leido como
    valor es 32, y 32 no es un ajuste de envolvente que nadie eligio, es una
    letra. Leerlo como 32 suena a una envolvente lenta y equivocada que no se
    puede corregir desde el panel, porque el panel no sabe que hay ahi.

    Asi que leer devuelve la envolvente plana —sustain 99, el resto 0— y escribir
    pone primero esa misma plana y luego el byte pedido, que es lo que hace la
    maquina cuando alguien toca un campo de un programa viejo. Esta en
    `vcfEnvelopeIsBlank` y en los dos metodos de campo.

    DE DONDE VIENE, Y QUE SE COPIO. `_RESOURCES/Mz950-main` (AGPLv3) tiene un
    lector de disco equivalente. Aqui NO se copio codigo: la geometria del disco
    son DATOS de la maquina —el directorio son 64 entradas de 24 bytes en 0x000,
    la tabla de asignacion empieza en 0x600, un programa tiene una cabecera de 38
    bytes— y un importador los necesita igual. Lo que si es nuestro es el
    recorrido, los nombres y las pruebas. Los VALORES POR DEFECTO de un keygroup
    nuevo tampoco estan copiados de ninguna parte: se escriben campo a campo a
    traves del catalogo, en `writeFreshKeygroup`, que es la unica funcion del
    fichero que pone bytes en un sitio nuevo.

    CONTRATO. `readKeygroupField` y sus hermanas devuelven `false` en vez de
    lanzar: un disco puede estar danado, y un importador que lanza ante un
    fichero raro no puede decir cual. Lo que es un BUG de programacion —un indice
    de campo que no existe, un keygroup de mas— se comprueba con `dspAssert`.

  ==============================================================================
*/

#pragma once

#include "DspCore/DspCore.h"
#include "S950PatchFields.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace abd::synth
{

//==============================================================================
/** El codigo de un campo, tal y como lo devuelve `readAllFields`: el indice en
    la tabla del catalogo, o un entero.

    Es un `int` y no un puntero a la fila porque `readAllFields` llena un array
    que se le pasa, y un array de punteros a una tabla estatica obliga a que
    quien lo rellene sepa de antemano donde esta la tabla. Un indice y una tabla
    se emparejan solos. */
using PatchFieldCode = int;

//==============================================================================
/** Una entrada del directorio: un fichero del disco.

    Un disco de S950 tiene como mucho 64, y un programa ('P') es un fichero mas.
    Los nombres son unicos DENTRO de un tipo, no en el disco: las imagenes de la
    biblioteca llamar a un programa igual que al sample que toca, asi que buscar
    por nombre sin tipo daria el que este antes en el directorio.
*/
struct DiskEntry
{
    int  slot = -1;              // posicion en el directorio, 0..63
    char name[11] = { 0 };       // 10 caracteres y un terminador
    char type = 0;               // 'P' programa, 'S' sample, 'D' kit, 'O' global
    int  length = 0;             // bytes del fichero, cabecera de 60 incluida
    int  startBlock = 0;         // primer bloque de la cadena
    int  chainBlocks = 0;        // cuantos bloques recorre la cadena
    bool chainOk = false;        // la cadena cubre los bytes que el fichero declara
};

//==============================================================================
/** La imagen de un disco S900/S950, y lo que se puede hacer con ella.

    Es un EDITOR, no una vista de audio: tiene estado y puede escribir. El hilo de
    audio no viene aqui —lo que viene es un patch ya construido, yendo detras de
    un `patch` de ABDNeural, no de una imagen de disco.
*/
class S950Disk
{
public:
    //==========================================================================
    // La geometria del disco. Son DATOS de la maquina, no eleccion de este
    // proyecto: por eso estan en `constexpr` y no en un parametro.

    /** El tamano de un bloque. Es el unico numero del que sale todo lo demas. */
    static constexpr int blockSize = 1024;

    /** El directorio: 64 entradas de 24 bytes, en el principio de la imagen. */
    static constexpr int dirOffset = 0x000;
    static constexpr int dirEntries = 64;
    static constexpr int dirEntrySize = 24;

    /** La tabla de asignacion: una palabra de 16 bits por bloque, desde 0x600. */
    static constexpr int fatOffset = 0x600;
    static constexpr int fatEnd = 0x8000;   // fin de cadena

    /** Los dos formatos que la maquina leia, y el disco decides cual por su
        tamano: 800 bloques es doble densidad, 1600 es alta. */
    static constexpr int doubleDensityBlocks = 800;
    static constexpr int highDensityBlocks = 1600;

    /** La cabecera de cada fichero, antes de su contenido.

        Y AQUI ESTA LA TRAMPA DEL FORMATO: existe, pero solo para los SAMPLES. Un
        programa NO la tiene —empieza directamente con sus 38 bytes de cabecera—,
        asi que la longitud de un programa es `38 + n * 70` y no `60 + 38 + n * 70`.
        Sumarle los 60 hace que el cuerpo no sea multiplo de 70 y que el programa
        se lea como danado, con cero keygroups. Ver `keygroupCount`. */
    static constexpr int fileHeaderSize = 60;

    //==========================================================================
    /** Un disco vacio y formateado, de la densidad que se pida.

        Un S950 nuevo es una imagen de ceros, porque el directorio y la tabla de
        asignacion vacios son exactamente ceros. No hay que escribir un "formato"
        aparte, y por eso no hay forma de que un disco nuevo esté medio formateado.
    */
    static S950Disk blank (bool highDensity)
    {
        const int blocks = highDensity ? highDensityBlocks : doubleDensityBlocks;
        S950Disk d;
        d.image.assign (static_cast<std::size_t> (blocks) * blockSize, 0u);
        d.rescan();
        return d;
    }

    /** Adopta una imagen ya leida de un fichero. Un tamano que no sea un numero
        entero de bloques se recorta al ultimo bloque entero, que es lo unico
        que se puede direccionar. */
    void adopt (std::vector<std::uint8_t> bytes)
    {
        const auto whole = static_cast<std::size_t> (blockSize) * (bytes.size() / static_cast<std::size_t> (blockSize));
        image.resize (whole);
        std::copy (bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t> (whole), image.begin());
        rescan();
    }

    //==========================================================================
    /** Cuantos bloques tiene la imagen. */
    int totalBlocks() const noexcept
    {
        return static_cast<int> (image.size() / static_cast<std::size_t> (blockSize));
    }

    /** La densidad, que se deduce del tamano y no al reves: un disco de 1600
        bloques es de alta densidad aunque nadie lo diga. */
    bool isHighDensity() const noexcept { return totalBlocks() > doubleDensityBlocks; }

    /** Donde empiezan los datos: tras la cabecera de directorio y tabla, que son
        4 bloques en doble densidad y 5 en alta. Un fichero NUNCA empieza antes. */
    int headerBlocks() const noexcept { return isHighDensity() ? 5 : 4; }

    /** Un disco tan pequeno que no tiene donde meter una cabecera no es un disco. */
    bool isUsable() const noexcept { return totalBlocks() > headerBlocks(); }

    //==========================================================================
    // El directorio
    //==========================================================================

    /** Cuantas entradas hay, ya leidas. */
    int entryCount() const noexcept { return static_cast<int> (entries.size()); }

    const DiskEntry& entry (int index) const noexcept { return entries[static_cast<std::size_t> (index)]; }

    /** La entrada de un fichero por nombre y tipo, o `nullptr`.

        El tipo es OBLIGATORIO y no es un detalle: en una imagen de biblioteca un
        programa y el sample que toca pueden llamarse igual, y buscar sin tipo
        devuelve el primero que encuentre en el directorio, que no es necesariamente
        el que se queria. */
    const DiskEntry* find (const char* name, char type) const noexcept
    {
        if (name == nullptr)
            return nullptr;

        for (const auto& e : entries)
            if (e.type == type && sameName (e.name, name))
                return &e;

        return nullptr;
    }

    /** El primer programa del disco, o `nullptr` si no hay ninguno. Para lo que
        solo quiere "un programa cualquiera" y no deberia tener que saber el
        nombre. */
    const DiskEntry* firstProgram() const noexcept
    {
        for (const auto& e : entries)
            if (e.type == 'P')
                return &e;

        return nullptr;
    }

    //==========================================================================
    // La tabla de asignacion y la cadena
    //==========================================================================

    /** El valor de la tabla para un bloque. 0 es libre, `fatEnd` es el final de
        una cadena, y cualquier otra cosa es el bloque siguiente. */
    int fat (int block) const noexcept
    {
        if (block < 0 || block >= totalBlocks())
            return fatEnd;

        const auto at = static_cast<std::size_t> (fatOffset)
                      + static_cast<std::size_t> (block) * 2u;
        return readU16 (at);
    }

    void setFat (int block, int value) noexcept
    {
        if (block < 0 || block >= totalBlocks())
            return;

        writeU16 (static_cast<std::size_t> (fatOffset) + static_cast<std::size_t> (block) * 2u,
                  static_cast<unsigned> (value));
    }

    /** Cuantos bloques recorre la cadena que empieza en `start`.

        Se cuenta hasta `maxBlocks` y se para. No es prudencia por prudencia: una
        tabla de asignacion DANADA puede apuntar un bloque a si mismo o hacia
        atras, y un recorrido sin limite no volveria nunca. Un disco roto tiene
        que poder decir "esto esta mal" y seguir, no colgar el proceso. */
    int chainLength (int start, int maxBlocks) const noexcept
    {
        if (maxBlocks <= 0)
            return 0;

        const auto limit = std::min (maxBlocks, totalBlocks());
        std::vector<bool> seen (static_cast<std::size_t> (totalBlocks()), false);

        int block = start;
        int n = 0;

        while (block != fatEnd && block >= 0 && block < totalBlocks() && n < limit)
        {
            if (seen[static_cast<std::size_t> (block)])
                break;

            seen[static_cast<std::size_t> (block)] = true;
            ++n;
            block = fat (block);
        }

        return n;
    }

    /** La posicion en la IMAGEN de un byte del FICHERO, cruzando la cadena.

        Devuelve `false` si el byte cae fuera de la cadena, y no "un numero
        cualquiera": un offset fuera de rango tiene que ser un fallo, porque un
        valor inventado aqui se escribiria en el sitio que toque.

        Un byte del fichero `offset` vive en el bloque `offset / 1024` de la
        cadena, en la posicion `offset % 1024` de ese bloque. La division es
        entera y el modulo es su complemento: por eso el caso de un keygroup
        partido entre dos bloques no necesita un caso especial, solo necesita que
        se mire un byte cada vez. */
    bool fileByteAt (const DiskEntry& e, std::size_t offset, std::size_t& imageAt) const noexcept
    {
        if (offset >= static_cast<std::size_t> (e.length))
            return false;

        const auto index = static_cast<int> (offset / static_cast<std::size_t> (blockSize));

        if (index >= e.chainBlocks)
            return false;

        // La cadena se recorre aqui, cada byte. Con un fichero de 70 bytes por
        // keygroup y hasta 64, son unos 4600 recorridoes: nada. Y a cambio el
        // caso del keygroup partido sale bien sin una rama mas.
        int block = e.startBlock;

        for (int i = 0; i < index; ++i)
        {
            if (block == fatEnd || block < 0 || block >= totalBlocks())
                return false;

            block = fat (block);
        }

        if (block == fatEnd || block < 0 || block >= totalBlocks())
            return false;

        imageAt = static_cast<std::size_t> (block) * static_cast<std::size_t> (blockSize)
                + (offset % static_cast<std::size_t> (blockSize));

        return imageAt < image.size();
    }

    /** El byte del registro de un keygroup, resuelto hasta la imagen.

        `offset` es un `byteOffset` del catalogo: esta dentro del keygroup, y el
        keygroup esta dentro del programa, que empieza tras su cabecera. */
    bool keygroupByteAt (const DiskEntry& program, int keygroup,
                         int offset, std::size_t& imageAt) const noexcept
    {
        if (program.type != 'P' || offset < 0 || offset >= keygroupRecordSize)
            return false;

        if (keygroup < 0 || keygroup >= keygroupCount (program))
            return false;

        return fileByteAt (program, fileOffsetOf (program, keygroup, offset), imageAt);
    }

    /** El offset, DENTRO DEL FICHERO, de un byte de un keygroup. Es lo que
        conecta el `byteOffset` del catalogo con la cadena de bloques.

        Sin la cabecera de 60 bytes de los samples: un programa empieza con sus
        38. Ver el aviso de `keygroupCount`. */
    static std::size_t fileOffsetOf (const DiskEntry& program, int keygroup, int offset) noexcept
    {
        return static_cast<std::size_t> (programHeaderSize)
             + static_cast<std::size_t> (keygroup) * static_cast<std::size_t> (keygroupRecordSize)
             + static_cast<std::size_t> (offset);
    }

    /** Cuantos keygroups tiene un programa.

        Se deduce de la LONGITUD, no de un campo de la cabecera: lo que manda es
        que el cuerpo sea un numero entero de registros de 70 bytes. Si no lo es,
        el programa esta danado y se dice que no tiene ninguno — devolver un
        resto truncado seria leer basura como si fuera un keygroup mas.

        OJO, y es el error mas facil de cometer con este formato: la cabecera de
        60 bytes que tiene `fileHeaderSize` es de los SAMPLES, no de los
        programas. Un programa empieza directamente con sus 38 bytes de cabecera,
        asi que su longitud es `38 + n * 70` y no `60 + 38 + n * 70`. Restar los
        60 de mas hace que el cuerpo no sea multiplo de 70 y que el programa se
        lea como danado: cero keygroups, en vez de los 16 que tiene de verdad. */
    int keygroupCount (const DiskEntry& program) const noexcept
    {
        if (program.type != 'P')
            return 0;

        if (program.length < programHeaderSize + keygroupRecordSize)
            return 0;

        const auto body = static_cast<int> (program.length) - programHeaderSize;
        return (body % keygroupRecordSize == 0) ? body / keygroupRecordSize : 0;
    }

    //==========================================================================
    // La envolvente del filtro en blanco. Ver la cabecera, punto 4.
    //==========================================================================

    /** Si los cuatro bytes de la envolvente del filtro de un keygroup son
        ESPACIOS, que es lo que escribia un S900 y lo que significa "este programa
        no tiene envolvente de filtro". */
    bool vcfEnvelopeIsBlank (const DiskEntry& program, int keygroup) const noexcept
    {
        for (int o = 0; o < 4; ++o)
        {
            std::size_t at = 0;
            if (! keygroupByteAt (program, keygroup, vcfFirstByte + o, at))
                return false;

            if (image[at] != blankByte)
                return false;
        }

        return true;
    }

    /** El byte de las cuatro etapas de la envolvente del filtro. */
    static constexpr int vcfFirstByte = 34;

    /** Lo que un S900 dejaba en esos cuatro bytes: un espacio, no un 32. */
    static constexpr std::uint8_t blankByte = 0x20;

    //==========================================================================
    // LEER UN CAMPO POR SU CODIGO. Esta es la funcion que hace que el catalogo
    // sirva de algo: nadie tiene que saber que el byte 42 es el fine.

    /** Lee un campo de un keygroup y lo devuelve en UNIDADES DE PANEL.

        Las cuatro codificaciones se resuelven aqui, y son lo que hace que leer
        "el byte" no baste:

          - `Unsigned`: el byte tal cual.
          - `Signed`: complemento a dos, asi que 0x80 es -128 y no 128.
          - `Port`: el byte va uno menos que el panel, y el 0xFF es el 0 del
            panel ("todos"). Sin esto, un programa en stereo leido daria 255.
          - `Bit`: un bit del byte 18, y solo ese bit.

        Y el caso de la envolvente en blanco: si los cuatro bytes son espacios,
        lo que sale es la envolvente PLANA —sustain 99, el resto 0— y no un 32 en
        cada etapa. Un 32 leido aqui seria una envolvente lenta y falsa que
        ademas no se puede arreglar desde el panel, porque el panel no sabe que
        hay un espacio ahi.

        @returns  `false` si el byte no se puede resolver. Nunca lanza. */
    bool readKeygroupField (const DiskEntry& program, int keygroup,
                            const PatchField& field, int& out) const noexcept
    {
        out = 0;

        std::size_t at = 0;
        if (! keygroupByteAt (program, keygroup, field.byteOffset, at))
            return false;

        const auto raw = image[at];

        switch (field.encoding)
        {
            case PatchField::Encoding::Signed:
                // A un byte con signo de verdad, que es lo que la maquina
                // escribe. El rango del campo dice -50..+50, pero el byte puede
                // traer cualquier cosa: eso se recorta, no se inventa.
                out = static_cast<int> (static_cast<std::int8_t> (raw));
                break;

            case PatchField::Encoding::Port:
                out = (raw == 0xFFu) ? 0 : static_cast<int> (raw) + 1;
                break;

            case PatchField::Encoding::Bit:
                out = (raw & field.bitMask) != 0u ? 1 : 0;
                break;

            case PatchField::Encoding::Unsigned:
            default:
                out = static_cast<int> (raw);
                break;
        }

        // El blanco de la envolvente del filtro, antes de recortar.
        if (isVcfField (field) && vcfEnvelopeIsBlank (program, keygroup))
        {
            out = (field.byteOffset == vcfFirstByte + 2) ? 99 : 0;
            return true;
        }

        out = clampToField (field, out);
        return true;
    }

    /** Lee por CODIGO, que es como lo llama un preset. */
    bool readField (const DiskEntry& program, int keygroup, const char* code, int& out) const noexcept
    {
        const auto* f = findField (code);
        if (f == nullptr)
            return false;

        return readKeygroupField (program, keygroup, *f, out);
    }

    /** Lee los 38 campos de golpe, en el orden de la tabla.

        Para un panel, que los quiere todos de una vez. Llena `out` y devuelve
        cuantos ha llenado; si `out` es mas corto que la tabla, se para, en vez
        de escribir mas alla del final. */
    int readAllFields (const DiskEntry& program, int keygroup, PatchFieldCode* out, int capacity) const noexcept
    {
        int n = 0;

        for (int i = 0; i < patchFieldCount && n < capacity; ++i)
        {
            if (! readKeygroupField (program, keygroup, fieldAt (i), out[n]))
                break;

            ++n;
        }

        return n;
    }

    //==========================================================================
    // ESCRIBIR UN CAMPO. Lo que se puede cambiar en su sitio.

    /** Escribe un campo de un keygroup, en unidades de panel.

        Lo que hace esta funcion y no una escritura de byte a byte:

          - RECORTA al rango del campo, siempre. Un importador que escribe lo que
            le han dado escribe lo que le han dado; una maquina recorta. Y el
            recorte aqui es el del panel, no el de `uint8_t`, asi que un 200 en
            un campo de 0..99 se guarda como 99 y no como 200.
          - TRADUCE la codificacion al revés que la lee: el signo a complemento
            a dos, el puerto al byte (con el 0xFF del "todos"), el flag a su
            mascara.
          - CONSERVA el resto del byte en los flags, que es lo que hace posible
            cambiar uno sin perder los otros tres ni el bit 0x02 que nadie ha
            descifrado.
          - ARREGLA la envolvente en blanco antes de escribir, poniendo la
            plana, que es lo que hace la maquina.

        Lo que NO hace, y por lo tanto devuelve `false`: mover ficheros, cambiar
        el numero de keygroups, o escribir fuera del registro de uno. Escribir a
        ojo en la imagen cambiaria cosas que esta funcion no sabe medir. */
    bool writeKeygroupField (const DiskEntry& program, int keygroup,
                             const PatchField& field, int value)
    {
        std::size_t at = 0;
        if (! keygroupByteAt (program, keygroup, field.byteOffset, at))
            return false;

        // La envolvente en blanco se pone plana ANTES de escribir el byte, como
        // la maquina: si no, escribir un solo campo dejas los otros tres
        // espacios y un keygroup medio convertido.
        if (isVcfField (field) && vcfEnvelopeIsBlank (program, keygroup))
            flattenVcfEnvelope (program, keygroup);

        const auto clipped = clampToField (field, value);
        auto raw = static_cast<std::uint8_t> (clipped);

        switch (field.encoding)
        {
            case PatchField::Encoding::Signed:
                raw = static_cast<std::uint8_t> (static_cast<std::int8_t> (clipped));
                break;

            case PatchField::Encoding::Port:
                // Al reves de la lectura: el 0 del panel es el 0xFF del disco, y
                // el resto va uno menos.
                raw = (clipped == 0) ? 0xFFu : static_cast<std::uint8_t> (clipped - 1);
                break;

            case PatchField::Encoding::Bit:
            {
                // Lectura-modificacion-escritura. Escribir el byte entero seria
                // tirar los otros tres flags y el bit reservado sin que nadie se
                // entere, y ese es exactamente el fallo que no se ve.
                const auto others = static_cast<std::uint8_t> (image[at] & static_cast<std::uint8_t> (~field.bitMask));
                raw = static_cast<std::uint8_t> (others | (clipped != 0 ? field.bitMask : 0u));
                break;
            }

            case PatchField::Encoding::Unsigned:
            default:
                break;
        }

        image[at] = raw;
        return true;
    }

    /** Escribe por CODIGO, que es como lo llama un preset. */
    bool writeField (const DiskEntry& program, int keygroup, const char* code, int value)
    {
        const auto* f = findField (code);
        if (f == nullptr)
            return false;

        return writeKeygroupField (program, keygroup, *f, value);
    }

    /** Escribe un valor, YA en unidades de panel, en el byte que la maquina
        guarda. Para quien necesita el byte y no el campo, y por eso el nombre
        lo dice: no recorta, no traduce y no arregla nada. */
    bool writeRawKeygroupByte (const DiskEntry& program, int keygroup, int offset,
                               std::uint8_t value) noexcept
    {
        std::size_t at = 0;
        if (! keygroupByteAt (program, keygroup, offset, at))
            return false;

        image[at] = value;
        return true;
    }

    /** Lee un byte del registro, sin interpretar. Para los bytes que NO son
        campos: los nombres de zona, la cadena de punteros. */
    bool readRawKeygroupByte (const DiskEntry& program, int keygroup, int offset,
                              std::uint8_t& out) const noexcept
    {
        std::size_t at = 0;
        if (! keygroupByteAt (program, keygroup, offset, at))
            return false;

        out = image[at];
        return true;
    }

    //==========================================================================
    // CREAR UN PROGRAMA. La unica operacion de este fichero que mete bytes
    // nuevos en un sitio que antes no existia, y por eso la unica que reserva.

    /** Crea un programa con `keygroups` keygroups en blanco, y devuelve su
        entrada. Devuelve `nullptr` si no cabe: disco lleno, directorio lleno, o
        un numero de keygroups que no valida.

        Lo que se escribe, y donde:

          - La CABECERA del programa: 38 bytes. El nombre va en 0..9 (es la copia
            que la maquina pone en su pantalla, no la del directorio), el numero
            de keygroups en el byte 23, y el numero de programa en el 26.
          - Un KEYGROUP por cada uno, con los valores por defecto de la maquina,
            escritos CAMPO A CAMPO a traves del catalogo. No hay una plantilla de
            70 bytes copiada de ninguna parte: hay 38 llamadas que dicen lo que
            ponen.

        El nombre se normaliza antes: 10 caracteres, mayusculas, imprimibles. Un
        nombre con acento o mas largo no es un error, es un nombre que la maquina
        no puede enseyar, y recortarlo aqui es mejor que rechazarlo. */
    const DiskEntry* addProgram (const char* name, int keygroups)
    {
        if (keygroups < 1 || keygroups > keygroupMaxCount)
            return nullptr;

        char clean[11] = { 0 };
        normaliseName (name, clean);

        // Los nombres son unicos DENTRO de un tipo, y hay que comprobarlo antes
        // de reservar bloques: descubrirlo despues dejaria el disco a medias.
        if (find (clean, 'P') != nullptr)
            return nullptr;

        const auto payload = static_cast<std::size_t> (programHeaderSize)
                           + static_cast<std::size_t> (keygroups) * static_cast<std::size_t> (keygroupRecordSize);

        std::vector<std::uint8_t> body (payload, 0u);
        writeProgramHeader (body.data(), clean, keygroups);
        writeFreshKeygroups (body.data() + programHeaderSize, keygroups);

        const auto written = addFile (clean, 'P', body, payload);
        if (written < 0)
            return nullptr;

        rescan();
        return find (clean, 'P');
    }

    //==========================================================================
    // Acceso crudo, para un test o una herramienta que construya una imagen.

    const std::vector<std::uint8_t>& bytes() const noexcept { return image; }
    std::vector<std::uint8_t>& bytes() noexcept { return image; }

    /** Un bloque entero de la imagen, para escribirlo entero. `block` fuera de
        rango devuelve `false` en vez de recortar: un bloque que no existe no es
        un bloque vacio. */
    bool blockAt (int block, std::uint8_t* out) const noexcept
    {
        if (block < 0 || block >= totalBlocks() || out == nullptr)
            return false;

        const auto from = static_cast<std::size_t> (block) * static_cast<std::size_t> (blockSize);
        std::copy (image.begin() + static_cast<std::ptrdiff_t> (from),
                   image.begin() + static_cast<std::ptrdiff_t> (from + static_cast<std::size_t> (blockSize)),
                   out);
        return true;
    }

    /** Escribe un bloque entero de la imagen, sin comprobar la cadena ni el
        directorio. Para montar una imagen; escribir DATOS de fichero por aqui si
        es saltarse la comprobacion de que el byte existe. */
    bool setBlock (int block, const std::uint8_t* in) noexcept
    {
        if (block < 0 || block >= totalBlocks() || in == nullptr)
            return false;

        const auto at = static_cast<std::size_t> (block) * static_cast<std::size_t> (blockSize);
        std::copy (in, in + blockSize, image.begin() + static_cast<std::ptrdiff_t> (at));
        return true;
    }

    /** Reescribe el directorio y la tabla desde la imagen actual. Se llama solo
        tras cambiar el contenido; `addProgram` lo hace al final. */
    void rescan() noexcept
    {
        entries.clear();

        for (int i = 0; i < dirEntries; ++i)
        {
            const auto o = static_cast<std::size_t> (dirOffset + i * dirEntrySize);
            if (o + static_cast<std::size_t> (dirEntrySize) > image.size())
                break;

            if (image[o] == 0x00u)                 // ranura libre
                continue;

            const auto type = static_cast<char> (image[o + 16]);
            if (type != 'P' && type != 'S' && type != 'D' && type != 'O')
                continue;                           // no es una entrada

            DiskEntry e;
            e.slot       = i;
            readFixedName (o, e.name);
            e.type       = type;
            e.length     = readU24 (o + 17);
            e.startBlock = static_cast<int> (readU16 (o + 20));

            if (e.startBlock < 0 || e.startBlock >= totalBlocks())
                continue;

            e.chainBlocks = chainLength (e.startBlock, totalBlocks());
            const auto needed = (e.length + blockSize - 1) / blockSize;
            e.chainOk    = e.chainBlocks >= needed;

            entries.push_back (e);
        }
    }

private:
    //==========================================================================
    /** Los 16 bits de la imagen en `at`, en orden de la maquina: little-endian.
        Un offset fuera de la imagen devuelve 0, que para una tabla de asignacion
        es "bloque libre": un disco truncado se lee como un disco con hueco, no
        como uno que se inventa los datos. */
    unsigned readU16 (std::size_t at) const noexcept
    {
        if (at + 1 >= image.size())
            return 0u;

        return static_cast<unsigned> (image[at])
             | (static_cast<unsigned> (image[at + 1]) << 8);
    }

    void writeU16 (std::size_t at, unsigned value) noexcept
    {
        if (at + 1 >= image.size())
            return;

        image[at]     = static_cast<std::uint8_t> (value & 0xFFu);
        image[at + 1] = static_cast<std::uint8_t> ((value >> 8) & 0xFFu);
    }

    /** 24 bits, que es como la maquina guarda el tamaño de un fichero: cabe en
        dos bytes y sobra con el tercero. */
    unsigned readU24 (std::size_t at) const noexcept
    {
        if (at + 2 >= image.size())
            return 0u;

        return static_cast<unsigned> (image[at])
             | (static_cast<unsigned> (image[at + 1]) << 8)
             | (static_cast<unsigned> (image[at + 2]) << 16);
    }

    /** Un nombre del directorio: diez bytes, y lo que no sea imprimible se lee
        como un espacio. Un nombre con bytes raros se muestra con huecos en vez
        de ensuciar el resto de la linea. */
    void readFixedName (std::size_t at, char* out) const noexcept
    {
        for (int i = 0; i < fileNameSize; ++i)
        {
            const auto c = image[at + static_cast<std::size_t> (i)];
            out[i] = (c >= 0x20u && c < 0x7Fu) ? static_cast<char> (c) : ' ';
        }

        out[fileNameSize] = '\0';

        while (out[0] != '\0' && out[static_cast<std::size_t> (strlen (out)) - 1] == ' ')
            out[static_cast<std::size_t> (strlen (out)) - 1] = '\0';
    }

    /** El nombre tal y como la maquina lo escribiria: diez caracteres, en
        mayusculas, y lo que no se pueda imprimir se cambia por un espacio. */
    static void normaliseName (const char* name, char* out) noexcept
    {
        for (int i = 0; i < fileNameSize; ++i)
        {
            const auto c = (name != nullptr) ? static_cast<unsigned char> (name[std::min<std::size_t> (static_cast<std::size_t> (i), std::strlen (name))]) : 0x20u;
            const auto up = (c >= 'a' && c <= 'z') ? static_cast<unsigned char> (c - ('a' - 'A')) : c;
            out[i] = (up >= 0x20u && up < 0x7Fu) ? static_cast<char> (up) : ' ';
        }

        out[fileNameSize] = '\0';

        while (out[0] != '\0' && out[static_cast<std::size_t> (strlen (out)) - 1] == ' ')
            out[static_cast<std::size_t> (strlen (out)) - 1] = '\0';

        if (out[0] == '\0')
        {
            // Un nombre vacio no es un nombre: la maquina enseña algo, y lo que
            // enseña cuando no hay nada es UNTITLED.
            std::memcpy (out, "UNTITLED", 8);
            out[8] = '\0';
        }
    }

    /** Dos nombres son el mismo si coinciden sin distinguir mayusculas y sin
        que esten los espacios de mas. La maquina no distingue, asi que comparar
        mas que eso daria dos ficheros donde hay uno.

        Se comparara Caracter a Caracter y se CORTA en la primera diferencia. Un
        recorrido que se limita a mirar el final —"compara hasta que uno se
        acaba"— daria "TESTPROG" y "OTROPROG" por iguales cuando su ultimo
        caracter coincide, que es justo el fallo que hace que un find() con un
        nombre que no existe encuentre algo. */
    static bool sameName (const char* a, const char* b) noexcept
    {
        if (a == nullptr || b == nullptr)
            return false;

        int i = 0, j = 0;

        for (;;)
        {
            // Los espacios al principio no cuentan: un nombre empieza donde
            // empieza, y los de padding no son parte de el.
            while (a[i] == ' ') { ++i; }
            while (b[j] == ' ') { ++j; }

            const auto ca = upperNameChar (a[i]);
            const auto cb = upperNameChar (b[j]);

            if (ca != cb)
                return false;

            if (ca == '\0')
                return true;    // los dos se han acabado a la vez

            ++i;
            ++j;
        }
    }

    /** Un caracter de nombre, en mayusculas. La maquina no distingue, asi que
        comparar en mayusculas es comparar como compara ella. */
    static char upperNameChar (char c) noexcept
    {
        return (c >= 'a' && c <= 'z') ? static_cast<char> (c - ('a' - 'A')) : c;
    }

    /** Los cuatro bytes de un campo que es una etapa de la envolvente del
        filtro. */
    static bool isVcfField (const PatchField& field) noexcept
    {
        return field.byteOffset >= vcfFirstByte && field.byteOffset < vcfFirstByte + 4
            && field.encoding == PatchField::Encoding::Unsigned
            && field.hi == 99 && field.lo == 0;
    }

    /** Pone la envolvente del filtro plana: 0, 0, 99, 0. Es lo que hay que
        escribir antes de tocar un campo de un programa viejo, o el keygroup se
        queda con tres espacios y un numero. */
    void flattenVcfEnvelope (const DiskEntry& program, int keygroup) noexcept
    {
        static const std::uint8_t flat[4] = { 0u, 0u, 99u, 0u };

        for (int o = 0; o < 4; ++o)
        {
            std::size_t at = 0;
            if (keygroupByteAt (program, keygroup, vcfFirstByte + o, at))
                image[at] = flat[o];
        }
    }

    /** La cabecera de 38 bytes de un programa nuevo. */
    void writeProgramHeader (std::uint8_t* at, const char* clean, int keygroups) const noexcept
    {
        std::copy (clean, clean + fileNameSize, at);
        at[keygroupCountOffset] = static_cast<std::uint8_t> (keygroups);
        at[programNumberOffset] = nextProgramNumber();
    }

    /** El numero de programa mas bajo que no este usando nadie. Es lo que la
        maquina enseña en su pantalla, y no un dato del motor. */
    std::uint8_t nextProgramNumber() const noexcept
    {
        bool taken[128] = { false };

        for (const auto& e : entries)
        {
            if (e.type != 'P')
                continue;

            std::size_t at = 0;
            if (fileByteAt (e, static_cast<std::size_t> (programNumberOffset), at)
                && at < image.size())
                taken[image[at] & 0x7Fu] = true;
        }

        int n = 0;
        while (n < 127 && taken[n])
            ++n;

        return static_cast<std::uint8_t> (n);
    }

    /** Un keygroup nuevo, escrito CAMPO A CAMPO a traves del catalogo.

        No hay una plantilla de 70 bytes pegada aqui. Hay treinta y ocho llamadas
        que dicen lo que ponen y por que, y el resto son los bytes que no son
        campos: los dos nombres de zona y la cadena de punteros, que se dejan a
        cero.

        Los valores son los de un S950 recien salido de fabrica: todo el teclado,
        sin segunda zona de velocidad, y cada ajuste en el punto que la maquina
        pone. La envolvente del filtro nace EN BLANCO —los cuatro espacios—, que
        es lo que significa "esta maquina no tenia envolvente de filtro" y no
        "una envolvente con 32 en todas las etapas". */
    void writeFreshKeygroups (std::uint8_t* base, int keygroups) const noexcept
    {
        for (int k = 0; k < keygroups; ++k)
        {
            auto* kg = base + static_cast<std::size_t> (k) * static_cast<std::size_t> (keygroupRecordSize);

            std::fill (kg, kg + keygroupRecordSize, std::uint8_t { 0 });

            for (int i = 0; i < patchFieldCount; ++i)
                putFieldValue (kg, fieldAt (i), defaultValueFor (fieldAt (i)));

            // Los dos nombres de zona a "2 SAMPLE", que es como la biblioteca
            // marca una zona sin usar. No es un campo del panel, asi que no sale
            // del catalogo: sale de la geometria.
            static const char unusedZone[] = "2 SAMPLE";
            for (int z = 0; z < keygroupMaxZones; ++z)
            {
                auto* zone = kg + keygroupNameOffset
                           + static_cast<std::size_t> (z) * static_cast<std::size_t> (keygroupZoneStride);
                std::copy (unusedZone, unusedZone + 8, zone);
                std::fill (zone + 8, zone + keygroupNameSize, std::uint8_t { ' ' });
            }

            // La cadena de punteros a las zonas se deja a cero a proposito: este
            // importador no apunta a ningun sample, y un puntero ahi que no
            // apunta a nada es peor que no tenerlo.
        }
    }

    /** El valor de fabrica de un campo, y el motivo cuando no es el de su
        `lo`. Un campo cuyo valor por defecto no se puede decir en una palabra
        lleva su porque aqui. */
    static int defaultValueFor (const PatchField& field) noexcept
    {
        const char* c = field.code;

        // El teclado entero y sin segunda zona de velocidad: un keygroup nuevo
        // suena en todo lo que se pueda tocar.
        if (std::strcmp (c, "lowKey") == 0)        return 0;
        if (std::strcmp (c, "highKey") == 0)       return keygroupVelocityCount - 1;
        // El 128 del switch es el "no hay segunda zona", y es el valor de
        // fabrica: un keygroup nuevo tiene UNA zona.
        if (std::strcmp (c, "velocitySwitch") == 0) return 128;

        // Los flags: constante apagada, one-shot apagada, desync ENCENDIDO (lo
        // escribe la maquina asi) y el release por velocidad apagado.
        if (std::strcmp (c, "constantPitch") == 0)     return 0;
        if (std::strcmp (c, "oneShot") == 0)           return 0;
        if (std::strcmp (c, "lfoDesync") == 0)         return 1;
        if (std::strcmp (c, "velocityReleaseOn") == 0) return 0;

        // Todo lo demas nace en su minimo, que es donde un mando empieza.
        return field.lo;
    }

    /** Escribe un valor de panel en el byte que la maquina guarda, a pelo y
        dentro de un buffer: la version sin imagen de `writeKeygroupField`, para
        construir un keygroup nuevo. Sin recorte y sin blancos, porque los
        valores vienen de `defaultValueFor` y son de la tabla. */
    static void putFieldValue (std::uint8_t* keygroup, const PatchField& field, int value) noexcept
    {
        auto* at = keygroup + field.byteOffset;
        const auto v = clampToField (field, value);

        switch (field.encoding)
        {
            case PatchField::Encoding::Signed:
                *at = static_cast<std::uint8_t> (static_cast<std::int8_t> (v));
                break;
            case PatchField::Encoding::Port:
                *at = (v == 0) ? 0xFFu : static_cast<std::uint8_t> (v - 1);
                break;
            case PatchField::Encoding::Bit:
                if (v != 0)
                    *at = static_cast<std::uint8_t> (*at | field.bitMask);
                break;
            case PatchField::Encoding::Unsigned:
            default:
                *at = static_cast<std::uint8_t> (v);
                break;
        }
    }

    /** Mete un fichero en bloques libres y en una ranura libre del directorio.

        Los bloques se buscan DESPUES de la cabecera, y el mas bajo primero, para
        que dos imagenes del mismo contenido salgan identicas. Eso no es
        estetica: es lo que hace que un test pueda comparar una imagen entera.

        @returns  el bloque inicial, o -1 si no cabe. */
    int addFile (const char* clean, char type,
                 const std::vector<std::uint8_t>& contents, std::size_t declaredLength)
    {
        const auto need = std::max (1, static_cast<int> ((declaredLength + blockSize - 1) / static_cast<std::size_t> (blockSize)));

        std::vector<int> free;
        for (int b = headerBlocks(); b < totalBlocks() && static_cast<int> (free.size()) < need; ++b)
            if (fat (b) == 0)
                free.push_back (b);

        if (static_cast<int> (free.size()) < need)
            return -1;

        int slot = -1;
        for (int i = 0; i < dirEntries; ++i)
            if (image[static_cast<std::size_t> (dirOffset + i * dirEntrySize)] == 0x00u) { slot = i; break; }

        if (slot < 0)
            return -1;

        for (int i = 0; i < need; ++i)
        {
            const auto blockFrom = static_cast<std::size_t> (i) * static_cast<std::size_t> (blockSize);
            const auto blockAt_ = static_cast<std::size_t> (free[static_cast<std::size_t> (i)]) * static_cast<std::size_t> (blockSize);
            const auto take = std::min (static_cast<std::size_t> (blockSize),
                                        contents.size() > blockFrom ? contents.size() - blockFrom : 0u);

            std::fill (image.begin() + static_cast<std::ptrdiff_t> (blockAt_),
                       image.begin() + static_cast<std::ptrdiff_t> (blockAt_ + static_cast<std::size_t> (blockSize)),
                       std::uint8_t { 0 });

            if (take > 0)
                std::copy (contents.begin() + static_cast<std::ptrdiff_t> (blockFrom),
                           contents.begin() + static_cast<std::ptrdiff_t> (blockFrom + take),
                           image.begin() + static_cast<std::ptrdiff_t> (blockAt_));

            // La cadena salta de un bloque libre al siguiente libre, que pueden
            // no ser contiguos. Por eso el keygroup partido entre bloques tiene
            // que resolverse byte a byte, y por eso este bucle no dice nada de
            // contigüidad.
            setFat (free[static_cast<std::size_t> (i)],
                    i == need - 1 ? fatEnd : free[static_cast<std::size_t> (i) + 1]);
        }

        const auto d = static_cast<std::size_t> (dirOffset + slot * dirEntrySize);
        std::fill (image.begin() + static_cast<std::ptrdiff_t> (d),
                   image.begin() + static_cast<std::ptrdiff_t> (d + static_cast<std::size_t> (dirEntrySize)),
                   std::uint8_t { 0 });

        std::copy (clean, clean + fileNameSize, image.begin() + static_cast<std::ptrdiff_t> (d));
        image[d + 16] = static_cast<std::uint8_t> (type);

        const auto len = static_cast<unsigned> (declaredLength);
        image[d + 17] = static_cast<std::uint8_t> (len & 0xFFu);
        image[d + 18] = static_cast<std::uint8_t> ((len >> 8) & 0xFFu);
        image[d + 19] = static_cast<std::uint8_t> ((len >> 16) & 0xFFu);
        image[d + 20] = static_cast<std::uint8_t> (free[0] & 0xFF);
        image[d + 21] = static_cast<std::uint8_t> ((free[0] >> 8) & 0xFF);

        return free[0];
    }

    std::vector<std::uint8_t> image;
    std::vector<DiskEntry>     entries;
};

//==============================================================================
/** El indice de un campo por su codigo, o -1. Lo que un panel recorre, y lo que
    hace el emparejamiento con la tabla del catalogo. */
constexpr int patchFieldIndex (const char* code) noexcept
{
    for (int i = 0; i < patchFieldCount; ++i)
    {
        const char* a = patchFields[i].code;
        const char* b = code;

        if (b == nullptr)
            return -1;

        while (*a != '\0' && *a == *b) { ++a; ++b; }

        if (*a == *b)
            return i;
    }

    return -1;
}

} // namespace abd::synth
