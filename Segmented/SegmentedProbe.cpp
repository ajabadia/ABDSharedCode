// Sonda de compilacion Y EJECUCION del modulo Segmented (no se instala en
// nadie): instanciar la clase y ejercitarla valida el header contra JUCE real.
// Compilable como ejecutable de prueba (target ABDShared_SegmentedProbe,
// opt-in) o como TU enlazado por cualquier consumidor.
#include "Segmented.h"

namespace abd::ui::probe
{

struct Probe
{
    Segmented segmented;

    Probe()
    {
        segmented.setSegments ({ "NEURONiK", "Neurotik" });
        segmented.setDisabledIndices ({ 1 });
        segmented.setDisabledNote ("Requiere el motor Neurotik");
        segmented.setColours (juce::Colour (0xff141d2b), juce::Colour (0xff7e9bb5),
                              juce::Colour (0xff00c3ff), juce::Colour (0xff0a0e14));

        segmented.setActive (0);
        jassert (segmented.getActive() == 0);

        // La edicion de usuario notifica; el estado que llega de fuera, no.
        int received = -1;
        segmented.onChange = [&received] (int index) { received = index; };

        segmented.onChange (1);
        jassert (received == 1);

        segmented.setActive (1);
        jassert (segmented.getActive() == 1);

        segmented.setSize (120, 22);
        segmented.resized();
    }
};

} // namespace abd::ui::probe

int main()
{
    abd::ui::probe::Probe probe;
    return 0;
}
