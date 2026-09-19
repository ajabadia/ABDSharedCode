/*
  ==============================================================================

    DspDebug.h
    Salida de depuracion del nucleo DSP, libre de JUCE (Fase 1, paso 6/6).

    Sustituto de juce::DBG para el motor: mismo uso (`dspDbg ("a " << x)`) y
    misma politica que JUCE, que es la que importa:

      - En Release NO se compila nada: el macro desaparece y la expresion no se
        evalua (igual que JUCE_DEBUG a 0). Con `! defined (NDEBUG)` se replica
        exactamente el valor de JUCE_DEBUG.
      - En Debug escribe en stderr. JUCE manda el texto al Logger del sistema,
        pero el motor no tiene logger: stderr es el equivalente inocuo.

    No es una dependencia de audio: solo lo usan rutas de diagnostico.

  ==============================================================================
*/

#pragma once

#if ! defined (NDEBUG)

  #include <cstdio>
  #include <sstream>
  #include <string>

  namespace abd::dsp
  {
      /** Escribe una linea de diagnostico (solo Debug). */
      inline void debugPrint (const std::string& text)
      {
          std::fprintf (stderr, "%s\n", text.c_str());
      }
  }

  /** Port de DBG(): compone el mensaje por streaming y lo escribe en Debug. */
  #define dspDbg(expr)                                            \
      do {                                                        \
          std::ostringstream dspDbgStream_;                       \
          dspDbgStream_ << expr;                                  \
          ::abd::dsp::debugPrint (dspDbgStream_.str());                \
      } while (false)

#else

  /** En Release el mensaje no se compila ni se evalua, como DBG en JUCE. */
  #define dspDbg(expr)  do { } while (false)

#endif
