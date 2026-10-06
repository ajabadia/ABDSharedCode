#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include "ScopeTap.h"
#include "TriggerDetector.h"
#include "TapId.h"

namespace abd::scope {

/**
 * Wire-protocol slug for a tap: explicit registered id wins, otherwise a
 * deterministic snake_case slug is derived from the display name.
 */
inline std::string getTapSlug(const ScopeTap* tap) {
    if (tap == nullptr) return "";
    if (!tap->getId().empty()) return tap->getId();
    return makeSlug(tap->getName());
}

/**
 * Message-thread Frame Serializer that decodes SPSC samples, calculates frame metrics,
 * and serializes into wire-protocol JSON for WebUI delivery.
 */
class ScopeFrameSerializer final {
public:
    explicit ScopeFrameSerializer(size_t targetSamples = 512)
        : m_targetSamples(targetSamples),
          m_tempL(targetSamples * 4, 0.0f),
          m_tempR(targetSamples * 4, 0.0f),
          m_frameL(targetSamples, 0.0f),
          m_frameR(targetSamples, 0.0f)
    {
    }

    /**
     * Poll active tap and serialize frame into JSON wire protocol string.
     * Returns empty string if buffer has not accumulated targetSamples yet.
     */
    std::string serializeActiveFrame(ScopeTap* activeTap, float sampleRate) {
        if (activeTap == nullptr || !activeTap->isActive()) return "";

        const size_t available = activeTap->getAvailableRead();
        if (available < m_targetSamples) return "";

        const bool isStereo = (activeTap->getType() == ScopeTapType::StereoAudio);

        // 1. Read available samples from ring buffer
        const size_t readCount = std::min(available, m_tempL.size());
        activeTap->read(m_tempL.data(), isStereo ? m_tempR.data() : nullptr, readCount);

        // 2. Extract the latest contiguous block of m_targetSamples (1:1 audio rate)
        // Taking the contiguous tail avoids fractional sample stepping that causes
        // waveform jitter and frequency warping.
        const size_t offset = (readCount > m_targetSamples) ? (readCount - m_targetSamples) : 0;
        const size_t count = std::min(readCount, m_targetSamples);

        float sumSqL = 0.0f, sumSqR = 0.0f;
        float peakL = 0.0f, peakR = 0.0f;

        for (size_t i = 0; i < m_targetSamples; ++i) {
            const size_t srcIdx = (i < count) ? (offset + i) : (count > 0 ? count - 1 : 0);
            const float sL = m_tempL[srcIdx];
            m_frameL[i] = sL;
            sumSqL += sL * sL;
            peakL = std::max(peakL, std::abs(sL));

            if (isStereo) {
                const float sR = m_tempR[srcIdx];
                m_frameR[i] = sR;
                sumSqR += sR * sR;
                peakR = std::max(peakR, std::abs(sR));
            }
        }

        const float rmsL = std::sqrt(sumSqL / static_cast<float>(m_targetSamples));
        const float rmsR = isStereo ? std::sqrt(sumSqR / static_cast<float>(m_targetSamples)) : rmsL;

        // 3. Trigger detection (bypassed for control signals)
        const bool isControl = (activeTap->getType() == ScopeTapType::ControlSignal);
        TriggerResult trigger;
        if (!isControl) {
            trigger = TriggerDetector::process(m_frameL.data(), m_targetSamples, sampleRate);
        }

        // 4. Construct Wire Protocol JSON
        std::ostringstream json;
        json << std::fixed << std::setprecision(4);
        json << "{\"signalType\":\"" << toString(activeTap->getType()) << "\","
             << "\"tapId\":\"" << getTapSlug(activeTap) << "\","
             << "\"sampleRate\":" << static_cast<int>(sampleRate) << ","
             << "\"numSamples\":" << m_targetSamples << ","
             << "\"triggerIndex\":" << trigger.triggerIndex << ","
             << "\"triggerFraction\":" << trigger.triggerFraction << ","
             << "\"estimatedFrequencyHz\":" << trigger.estimatedFrequencyHz << ","
             << "\"detectedNoteName\":\"" << trigger.noteName << "\","
             << "\"rmsL\":" << rmsL << ",\"rmsR\":" << rmsR << ","
             << "\"peakL\":" << peakL << ",\"peakR\":" << (isStereo ? peakR : peakL) << ","
             << "\"timeDataL\":[";

        for (size_t i = 0; i < m_targetSamples; ++i) {
            if (i > 0) json << ",";
            json << m_frameL[i];
        }
        json << "]";

        if (isStereo) {
            json << ",\"timeDataR\":[";
            for (size_t i = 0; i < m_targetSamples; ++i) {
                if (i > 0) json << ",";
                json << m_frameR[i];
            }
            json << "]";
        }

        json << "}";
        return json.str();
    }

private:
    size_t m_targetSamples;
    std::vector<float> m_tempL;
    std::vector<float> m_tempR;
    std::vector<float> m_frameL;
    std::vector<float> m_frameR;
};

} // namespace abd::scope
