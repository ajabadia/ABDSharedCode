/*
  ==============================================================================

    DspLeakedObjectDetector.h
    Detector de fugas del nucleo DSP, libre de JUCE (Fase 1, paso 6/6).

    Port de juce::LeakedObjectDetector + JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    (juce_core/memory/juce_LeakedObjectDetector.h). Sustituye al macro de JUCE en
    las clases del motor para que dejen de depender de juce_core.

    Misma semantica que JUCE, incluida la parte que mas se olvida: **en Release el
    detector NO existe** (JUCE lo compila solo con JUCE_CHECK_MEMORY_LEAKS, que a
    su vez depende de JUCE_DEBUG). Aqui la puerta es `! defined (NDEBUG)`, que es
    el mismo valor de JUCE_DEBUG, asi que el Release sigue igual de ligero que
    antes y solo cambia el diagnostico de Debug.

    Diferencias documentadas (ninguna afecta al audio):
      - El contador es std::atomic<int> en vez del juce::Atomic<int>.
      - Los avisos van por dspDbg/dspAssert (stderr) en vez de DBG/jassertfalse.

  ==============================================================================
*/

#pragma once

#include "DspCore.h"
#include "DspDebug.h"

#include <atomic>

namespace abd::dsp
{

/** Port de juce::LeakedObjectDetector: cuenta instancias por clase y avisa en el
    apagado de los estaticos si queda alguna viva (o si se borra de mas). */
template <class OwnerClass>
class LeakedObjectDetector
{
public:
    LeakedObjectDetector() noexcept                                 { ++(getCounter().numObjects); }
    LeakedObjectDetector (const LeakedObjectDetector&) noexcept     { ++(getCounter().numObjects); }

    LeakedObjectDetector& operator= (const LeakedObjectDetector&) noexcept = default;

    ~LeakedObjectDetector()
    {
        if (--(getCounter().numObjects) < 0)
        {
            dspDbg ("*** Dangling pointer deletion! Class: " << getLeakedObjectClassName());
            dspAssert (false);
        }
    }

private:
    class LeakCounter
    {
    public:
        LeakCounter() = default;

        ~LeakCounter()
        {
            if (numObjects.load() > 0)
            {
                dspDbg ("*** Leaked objects detected: " << numObjects.load()
                        << " instance(s) of class " << getLeakedObjectClassName());
                dspAssert (false);
            }
        }

        std::atomic<int> numObjects { 0 };
    };

    static const char* getLeakedObjectClassName()
    {
        return OwnerClass::getLeakedObjectClassName();
    }

    static LeakCounter& getCounter() noexcept
    {
        static LeakCounter counter;
        return counter;
    }
};

} // namespace abd::dsp

//==============================================================================
#if ! defined (NDEBUG)

  /** Port de JUCE_LEAK_DETECTOR (solo Debug). */
  /* Termina en ';' porque los llamantes no ponen ';' tras el macro (igual que
     JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR). */
  #define dspLeakDetector(OwnerClass)                                                  \
      friend class ::abd::dsp::LeakedObjectDetector<OwnerClass>;                            \
      static const char* getLeakedObjectClassName() noexcept { return #OwnerClass; }    \
      ::abd::dsp::LeakedObjectDetector<OwnerClass> dspLeakDetector_ { };

#else

  #define dspLeakDetector(OwnerClass)  /* sin detector en Release, como JUCE */

#endif

/** Port de JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR. */
#define dspDeclareNonCopyableWithLeakDetector(ClassName)     \
    ClassName (const ClassName&) = delete;                   \
    ClassName& operator= (const ClassName&) = delete;        \
    dspLeakDetector (ClassName)
