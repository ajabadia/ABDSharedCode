#pragma once

#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <atomic>
#include <mutex>
#include <limits>
#include "ScopeTap.h"
#include "TapId.h"

namespace abd::scope {

/**
 * Central Data Collector managing multiple taps across a synth audio engine.
 */
class ScopeDataCollector final {
public:
    static constexpr size_t npos = (std::numeric_limits<size_t>::max)();

    ScopeDataCollector() = default;
    ~ScopeDataCollector() = default;
    ScopeDataCollector(const ScopeDataCollector&) = delete;
    ScopeDataCollector& operator=(const ScopeDataCollector&) = delete;

    /**
     * Register a new tap during initialization (not during audio processing).
     *
     * @param name           Human-readable display name (e.g. "Master Output").
     * @param type           Signal class (StereoAudio / MonoAudio / ControlSignal).
     * @param bufferCapacity Ring buffer capacity per channel.
     * @param id             Optional stable wire-protocol slug. When empty the
     *                       serializer derives a deterministic slug from @p name.
     * @return Pointer to registered tap for audio thread writing.
     */
    ScopeTap* registerTap(std::string name, ScopeTapType type,
                          size_t bufferCapacity = 4096, std::string id = {}) {
        std::lock_guard<std::mutex> lock(m_registrationMutex);
        auto tap = std::make_unique<ScopeTap>(std::move(name), type, bufferCapacity, std::move(id));
        ScopeTap* ptr = tap.get();
        m_taps.push_back(std::move(tap));

        // Auto-select first registered tap
        if (m_taps.size() == 1) {
            ptr->setActive(true);
            m_activeTapIndex.store(0, std::memory_order_relaxed);
        }
        return ptr;
    }

    /**
     * Select active tap by index (called from UI / message thread).
     */
    void selectTap(size_t index) noexcept {
        const size_t total = m_taps.size();
        if (index >= total) return;

        for (size_t i = 0; i < total; ++i) {
            m_taps[i]->setActive(i == index);
        }
        m_activeTapIndex.store(index, std::memory_order_relaxed);
    }

    /**
     * Select active tap by id slug or display name (UI / message thread).
     * Resolution order: explicit id -> display name / derived slug -> substring fallback.
     */
    bool selectTap(std::string_view nameOrId) {
        const size_t idx = findTapIndex(nameOrId);
        if (idx == npos) return false;
        selectTap(idx);
        return true;
    }

    /**
     * Resolve a tap id slug or display name to its index without mutating state.
     * Message-thread safe (does not touch the vector contents).
     */
    size_t findTapIndex(std::string_view query) const {
        const size_t total = m_taps.size();

        // 1) Exact explicit wire id
        for (size_t i = 0; i < total; ++i) {
            const std::string& id = m_taps[i]->getId();
            if (!id.empty() && id == query) return i;
        }
        if (query.empty()) return npos;

        // 2) Case-insensitive display name or deterministic derived slug
        const std::string queryLower = toLowerAscii(query);
        for (size_t i = 0; i < total; ++i) {
            const std::string& name = m_taps[i]->getName();
            if (toLowerAscii(name) == queryLower || makeSlug(name) == queryLower) return i;
        }

        // 3) Lenient substring fallback (e.g. "osc" -> "Osc 1 (DWGS)")
        for (size_t i = 0; i < total; ++i) {
            const std::string nameLower = toLowerAscii(m_taps[i]->getName());
            if (nameLower.find(queryLower) != std::string::npos
                || queryLower.find(nameLower) != std::string::npos) return i;
        }

        return npos;
    }

    [[nodiscard]] ScopeTap* getActiveTap() const noexcept {
        const size_t idx = m_activeTapIndex.load(std::memory_order_relaxed);
        return (idx < m_taps.size()) ? m_taps[idx].get() : nullptr;
    }

    [[nodiscard]] size_t getTapCount() const noexcept {
        return m_taps.size();
    }

    [[nodiscard]] const ScopeTap* getTap(size_t index) const noexcept {
        return (index < m_taps.size()) ? m_taps[index].get() : nullptr;
    }

    /**
     * Deactivate all taps (e.g., when UI is closed or scope is destroyed).
     */
    void deactivateAll() noexcept {
        for (auto& tap : m_taps) {
            tap->setActive(false);
        }
    }

private:
    std::mutex m_registrationMutex;
    std::vector<std::unique_ptr<ScopeTap>> m_taps;
    std::atomic<size_t> m_activeTapIndex { 0 };
};

} // namespace abd::scope
