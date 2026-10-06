/*
  ==============================================================================

    JunoBbdProfile.h
    El coro BBD de los Juno como PERFIL: solo numeros, ningun codigo.

    UBICACION CANONICA: aqui, en ABDSharedCode (modulo ABDShared::DspEffects,
    namespace abd::dsp). Port del coro BBD de ABDJUNiO601
    (Source/Synth/ChorusBBD.{h,cpp} + Synth/BBDFilter.h), que a su vez emula el
    MN3009. El motor es `JunoBBD`, en DspEffects/JunoBBD.h.

    QUE ES. Una tabla de constantes por modelo. Ni una linea de audio. Es la
    primera dosis de `EffectPolicy.h`: "que numeros son los de esta maquina".

        dsp::JunoBBD<dsp::JunoBbdJ106Profile, dsp::BbdNoiseStage> chorus;
        chorus.prepare (sampleRate);
        chorus.setMode (dsp::JunoBbdMode::ChorusI);
        chorus.setRate (0.513f);
        chorus.setDepth (0.65f);
        chorus.setMix (0.50f);
        chorus.setHissLevelDb (-68.0f);
        chorus.process (left, right, outL, outR);

    LAS DOS VARIANTES, Y LA COSA HONESTA QUE HAY QUE SABER. Hay un
    `ChorusModel { J60, J106 }` en el original, PERO HOY LOS DOS MODELOS SUENAN
    IGUAL, y conviene saber por qu\u00e9 antes de tocar nada:

      - El enum existe, `setChorusModel` lo escribe, y `process()` NUNCA LO LEE.
        En el original no hay ni una linea que ramifique por modelo.
      - El original SI tiene el MECANISMO de distinguirlos: los parametros de
        calibracion del coro se registran por triplicado (`chorusDelayI_J60`,
        `chorusDelayI_J106`, ... en Core/CalibrationSettings.cpp). Lo que pasa es
        que `regTriple` les da a los tres el MISMO valor por defecto, asi que
        hasta que alguien guarde una calibracion distinta por modelo, J60 y J106
        dan los mismos numeros.

    Que hace este perfil, entonces? NO inventar una diferencia que no existe.
    Deja las dos filas, con los numeros que hoy son iguales, y el MECANISMO
    preparado: en cuanto haya una calibracion real por modelo, se cambia una
    fila y no hay que tocar el motor. Poner aqui numeros "de manual" que el
    proyecto no usa seria fabricar una segunda verdad, que es justo el fallo
    caro de un perfil.

  ==============================================================================
*/

#pragma once

namespace abd::dsp
{

//==============================================================================
/**
    Las constantes de un coro BBD. Solo datos: el motor las lee.

    Los rangos van commented con el valor de fabrica de JUNiO601, que es el
    unico sitio del suite donde estos numeros existen hoy.
*/
struct JunoBbdProfile
{
    //--- Retardo y modulacion ---------------------------------------------
    float delayI;        // ms, retardo base del modo I      (3.2)
    float delayII;       // ms, retardo base del modo II     (3.3)
    float depthI;        // ms, barrido del modo I           (2.13)
    float depthII;       // ms, barrido del modo II          (1.71)
    float depthBoth;     // ms, barrido del modo I+II        (0.236)
    float modDepthScale; // multiplica el barrido             (1.5)
    float rateI;         // Hz, LFO del modo I               (0.513)
    float rateII;        // Hz, LFO del modo II              (0.78)
    float rateBoth;      // Hz, LFO acelerado I+II           (7.7)

    //--- Reloj del BBD -----------------------------------------------------
    float clockTrim;         // tolerancia de reloj por linea     (0.015)
    float minDelayMs;        // suelo del retardo                 (0.1)
    float minClockHz;        // techo del reloj antes de recortar(5000)
    float cteCoeff;          // perdida de transferencia de carga (4468)
    float cteInvClockCentre; // centro de esa perdida (1/40000)   (2.5e-5)
    float gainTrim;          // asimetria L/R                     (0.04)

    //--- Mezclador IC6 -----------------------------------------------------
    float gainDry; // ganancia de seco                  (0.863)
    float gainWet; // ganancia de mojado                (1.257)

    //--- Saturacion de la linea -------------------------------------------
    float satDrive; // base del drive, ANTES del boost  (0.1)
    float satBoost; // calibracion de saturacion         (1.2)
    float satSlew;  // constante de suavizado del drive  (0.001)

    //--- Filtros de reconstruccion ----------------------------------------
    float biquadFc;       // Hz, pasabajos TPT de 2º orden     (8000)
    float biquadQ;        // Butterworth                       (0.7071)
    float poleFc;         // Hz, inclinacion de agudas        (20000)
    float lineMinSeconds; // longitud minima de la linea       (0.020)

    //--- Siseo y degradacion ----------------------------------------------
    float hissLevelDb;     // dB, siseo base                     (-68)
    float hissMultiplier;  // desgaste (1.0 = nuevo)            (1.0)
    float leakGain;        // ganancia del siseo de fuga         (8.8e-3)
    float leakMinFrac;     // fuga minima en el fondo del LFO   (0.0126)
    float clickGain;       // clic primario                     (0.11)
    float slowClickGain;   // clic secundario                   (0.022)
    float clickThreshold;  // umbral de disparo del clic        (0.95)
    float clickDurationMs; // duracion del clic                 (180)
    float clickRingHz;     // resonancia del anillo de clic      (30)
    float clickRingQ;      // Q del anillo                      (18)
    float clickRingGain;   // ganancia del anillo               (0.06)
    float mainsHz;         // Hz, zumbido de red                 (60)
    float mainsA1;         // armonico 1                         (7.9e-5)
    float mainsA2;         // armonico 2                         (2.2e-5)
    float mainsA3;         // armonico 3                         (9.8e-6)
    float noiseLpCutoffHz; // Hz, LP del siseo blanco           (20000)
    float hissColor;       // 0 = rosa, 1 = blanco del siseo    (0.4)
    float noiseShelfHz;    // Hz, shelf del siseo rosa           (3000)
    float noiseShelfDb;    // dB, elevacion del shelf            (6)
    float leakHpHz;        // Hz, HP del siseo de fuga           (800)

    //--- Valores por defecto de los mandos --------------------------------
    float defaultRate;  // Hz                                 (0.513)
    float defaultDepth; // (0.65)
    float defaultMix;   // (0.50)
};

//==============================================================================
/** El coro tal y como sale de fabrica en el Juno-106. */
struct JunoBbdJ106Profile
{
    static constexpr JunoBbdProfile value =
        {
            /* delayI        */ 3.2f,
            /* delayII       */ 3.3f,
            /* depthI        */ 2.13f,
            /* depthII       */ 1.71f,
            /* depthBoth     */ 0.236f,
            /* modDepthScale */ 1.5f,
            /* rateI         */ 0.513f,
            /* rateII        */ 0.78f,
            /* rateBoth      */ 7.7f,
            /* clockTrim     */ 0.015f,
            /* minDelayMs    */ 0.1f,
            /* minClockHz    */ 5000.0f,
            /* cteCoeff      */ 4468.0f,
            /* cteInvClockCentre */ 1.0f / 40000.0f,
            /* gainTrim      */ 0.04f,
            /* gainDry       */ 0.863f,
            /* gainWet       */ 1.257f,
            /* satDrive      */ 0.1f,
            /* satBoost      */ 1.2f,
            /* satSlew       */ 0.001f,
            /* biquadFc      */ 8000.0f,
            /* biquadQ       */ 0.7071f,
            /* poleFc        */ 20000.0f,
            /* lineMinSeconds */ 0.020f,
            /* hissLevelDb   */ -68.0f,
            /* hissMultiplier */ 1.0f,
            /* leakGain      */ 8.8e-3f,
            /* leakMinFrac   */ 0.0126f,
            /* clickGain     */ 0.11f,
            /* slowClickGain */ 0.022f,
            /* clickThreshold */ 0.95f,
            /* clickDurationMs */ 180.0f,
            /* clickRingHz   */ 30.0f,
            /* clickRingQ    */ 18.0f,
            /* clickRingGain */ 0.06f,
            /* mainsHz       */ 60.0f,
            /* mainsA1       */ 7.9e-5f,
            /* mainsA2       */ 2.2e-5f,
            /* mainsA3       */ 9.8e-6f,
            /* noiseLpCutoffHz */ 20000.0f,
            /* hissColor       */ 0.4f,
            /* noiseShelfHz  */ 3000.0f,
            /* noiseShelfDb  */ 6.0f,
            /* leakHpHz      */ 800.0f,
            /* defaultRate   */ 0.513f,
            /* defaultDepth  */ 0.65f,
            /* defaultMix    */ 0.50f};
};

//==============================================================================
/**
    El coro tal y como sale de fabrica en el Juno-60.

    IDENTICO al J106 a proposito, y con el motivo escrito en la cabecera del
    fichero: en JUNiO601 los dos modelos reciben el mismo valor por defecto en
    todos los parametros de calibracion del coro, y el `ChorusModel` del
    original no se lee nunca. La fila esta para que la diferencia, cuando
    exista, sea un cambio de numeros y no un cambio de codigo.
*/
struct JunoBbdJ60Profile
{
    static constexpr JunoBbdProfile value = JunoBbdJ106Profile::value;
};

} // namespace abd::dsp
