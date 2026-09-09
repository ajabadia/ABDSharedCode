#pragma once

/*
 * Compatibility shim — the canonical VoiceAllocator lives in
 * ../SynthCore/VoiceAllocator.h (namespace abd::synth). This header keeps the
 * historical abd::lutdsp include path working via re-exports.
 * NOTE: sync artifact, not hand-maintained — edit SynthCore/VoiceAllocator.h.
 */

#include "../SynthCore/VoiceAllocator.h"

namespace abd::lutdsp
{
    using abd::synth::PolyMode;
    using abd::synth::VoiceState;

    template <size_t MaxVoices = 8>
    using VoiceAllocator = abd::synth::VoiceAllocator<MaxVoices>;
}
