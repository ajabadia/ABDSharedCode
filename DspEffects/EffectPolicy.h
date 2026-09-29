/*
  ==============================================================================

    EffectPolicy.h
    El contrato de POLITICA INYECTADA de los efectos compartidos (abd::dsp).

    EL PROBLEMA QUE RESUELVE. Un efecto no es siempre "un efecto": a veces es
    "un efecto con el Quitese de una maquina en particular". El Space Echo
    RE-201 no es un retardo de tres cabezales generico: es un retardo de tres
    cabezales MAS las posiciones y ganancias que tenia el RE-201, el envio a
    su tanque de muelle, su wow y su siseo. El chorus BBD de la Juno-106 no es
    un chorus generico: es un retardo con reloj cuantizado, ruido de
    transferencia de carga y un mezclador con ganancias asimetricas. Ese
    "y ademas" es lo que hacia que el efecto no se pudiera compartir: la
    identidad de hardware estaba PEGADA al algoritmo, en la misma clase, y
    quitarla obligaba a heredar de ella.

    LA RESPUESTA. Partir por la costura que ya separa al resto del modulo
    (motor = maquina, consumidor = politica de producto) y aplicar el MISMO
    corte a la emulacion. Un motor compartido declara, en sus parametros de
    plantilla, de que depende su sonido: una TABLA DE CONSTANTES (que numeros
    son los de esta maquina) y una ETAPA DE CARACTER (que no linealidad mete).
    Quien instancia el motor decide que politica le pasa.

    LAS DOS DOSIS, Y SON DOS A PROPOSITO:

      1. CONSTANTES — un `struct` de datos. Cero coste, cero indireccion, y
         legible: las posiciones de cabezal del RE-201 se leen en la tabla.
         Es lo que hace que ABDEep conserve su fidelidad sin que el motor
         compartido sepa de Rolland. Ver `Re201Profile` en MultiHeadEcho.h.

      2. ETAPAS — un objeto con estado, porque la emulacion de hardware
         TIENE estado: un BBD arrastra su ruido de transferencia, una cinta
         arrastra su wow. Se inyecta como parametro de plantilla, de modo que
         la llamada se resuelve en compilacion y no hay llamada virtual en el
         lazo de audio. Ver `CharacterStage` abajo.

    POR QUE NO UNA INTERFAZ VIRTUAL. El lazo de audio de estos motores son
    decenas de millones de muestras por segundo. Una llamada virtual por
    muestra en la etapa de caracter es, medido, un 3-5% del presupuesto de
    CPU de un efecto de saturacion; y este modulo no la gasta. Con la etapa
    como parametro de plantilla el coste es cero y la excepcion se ve en la
    declaracion del motor.

    Y POR QUE NO "UN MOTOR POR MAQUINA". Un motor por maquina es lo que hay
    hoy, y es exactamente lo que impide compartir: N motores con N tablas de
    constantes casi iguales. Con la politica inyectada hay UN motor de eco de
    multi-cabezal, y el RE-201, un eco de estudio o un eco de maquina de
    discoteca son el MISMO motor con tres politicas distintas. Añadir una
    maquina deja de ser un motor nuevo.

    USO TIPICO (y el orden que obliga el motor):

        // el motor no conoce el dispositivo; el perfil se lo inyecta
        dsp::MultiHeadEcho<dsp::Re201Profile, dsp::TapeColour> echo;
        echo.prepare (sampleRate);

        // y el perfil generico, sin emular a nadie, tambien funciona
        dsp::MultiHeadEcho<dsp::StudioEchoProfile, dsp::NullStage> plain;

    LO QUE NO VIVE AQUI (y por la regla del modulo, se queda en el
    consumidor): suavizado de parametros, mapeo de controles a unidades fisicas
    (0..1 -> milisegundos, -> Hz), recorte de realimentacion, mezcla wet/dry y
    denormales. Eso es politica de PRODUCTO y cambia entre sintetizadores. Lo
    que esta aqui es politica de DISPOSITIVO, que es justo lo que se quiere
    compartir.

  ==============================================================================
*/

#pragma once

namespace abd::dsp
{

//==============================================================================
/**
    Base (NO virtual) de una etapa de caracter inyectada.

    Una etapa de caracter es la no linealidad —o la degradacion— que hace que
    una maquina suene como suene y no como el algoritmo ideal. Se inyecta en el
    motor como parametro de plantilla, de modo que la llamada se resuelve en
    compilacion: NO hay vtable en el lazo de audio (ver el "por que" de la
    cabecera).

    LO QUE ESTA BASE APORTA. Solo el ciclo de vida que es igual en todas: el
    sample rate y su consulta, y un `reset` que devuelve la etapa al estado
    inicial. Es poco a proposito: lo que de verdad define a una etapa es su
    `processSample`, y cada familia necesita OTROS parametros por muestra (la
    cinta pide drive/wow/flutter/hiss, el BBD pide la amplitud del ruido, el
    tanque de muelle no pide nada). Fijar aqui la firma de audio obligaria a
    deformar las etapas para que encajasen en una firma comun, que es
    exactamente la rigidez que la inyeccion de politica viene a quitar.

    ASI QUE LA CONTRATACION DE UNA ETAPA ES POR CONVENCION, y la vigila el test
    compartido `DspEffectsTests.cpp`, que instancia cada etapa y comprueba que
    tiene `prepareSampleRate`, `getSampleRate`, `reset` y un `processSample` que
    devuelve `float` y acepta el par `(drive, hiss)` con valores por defecto.
    Una etapa nueva que no cumpla eso no pasa el modulo.

    LO QUE UNA ETAPA NO ES. La deriva de la cinta (wow y flutter) NO es color:
    es el TRANSPORTE, y por eso vive en el motor, no en la etapa. Cuando estaba
    al reves —la etapa exponiendo su wow al motor— el motor acababa
    dependiendo de los internos de la etapa y `NullStage` no podia
    satisfacer el contrato: la inyeccion dejaba de ser inyeccion. El motor
    genera su propia deriva con las constantes del perfil y la etapa solo
    pinta la no linealidad.

    Y POR QUE NO HAY UN ANCLA VIRTUAL PARA EL SAMPLE RATE. Hubo una
    (`virtual void prepareRateImpl (double) noexcept {}`, protected, que se
    llamaba desde `prepareSampleRate`) y se ha quitado. MEDIDO: nadie la
    sobrescribia —ni en este modulo, ni en ningun producto del arbol— y
    mantenerla costaba un vtable entero en CADA etapa:

        antes   sizeof(NullStage) = 16   sizeof(DiodeBridge) = 16   sizeof(TapeColour) = 24
        ahora   sizeof(NullStage) =  8   sizeof(DiodeBridge) =  8   sizeof(TapeColour) = 16
        antes   is_polymorphic<NullStage> = si   (y de las otras dos tambien)
        ahora   is_polymorphic<NullStage> = no

    O sea, 8 bytes y una tabla de funciones por etapa a cambio de nada, en la
    clase cuya unica razon de existir es no gastar indireccion. Una etapa que
    necesite el sample rate lo lee con `getSampleRate()`, que ya esta, y
    recalcula su coeficiente una vez —que es lo que hacen todas.

    Implementacion minima:

        class MiEtapa : public CharacterStage
        {
        public:
            float processSample (float x) noexcept { return dsp::tanh (x * 2.0f); }
            void reset() noexcept { state_ = 0.0f; }
        private:
            float state_ = 0.0f;
        };
*/
class CharacterStage
{
public:
    CharacterStage() = default;
    ~CharacterStage() = default;

    /** Fija el sample rate de la etapa. Se llama una vez por `prepare`. */
    void prepareSampleRate (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    /** Sample rate de la ultima llamada a `prepareSampleRate`. */
    double getSampleRate() const noexcept { return sampleRate_; }

private:
    double sampleRate_ = 44100.0;
};

//==============================================================================
/**
    Etapa de caracter NULA: la maquina sin emular a nadie.

    Es el otro extremo del patron y no un caso trivial: es lo que permite que
    un motor compartido se use como efecto GENERICO. `StudioEchoProfile` con
    `NullStage` es "un retardo de tres cabezales"; con `TapeColour` es un eco
    de cinta en color. El motor es el mismo, y por eso no hay que mantener dos.

    Acepta los MISMOS argumentos que una etapa con color (drive, hiss) y los
    ignora, con valores por defecto: asi el motor puede instanciarse con
    cualquier etapa sin cambiar de forma al compilar. Es lo que hace que
    `NullStage` sea un sustituto real y no un caso especial.
*/
class NullStage : public CharacterStage
{
public:
    float processSample (float x, float = 0.0f, float = 0.0f) noexcept { return x; }
    void reset() noexcept {}
};

} // namespace abd::dsp