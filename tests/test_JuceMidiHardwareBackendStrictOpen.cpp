/**
 * @file test_JuceMidiHardwareBackendStrictOpen.cpp
 * @brief Hermetic unit tests for HITO-SHARED-SYNC / SS2 & SS2.1: Strict MIDI Endpoint Open without Fallback.
 * @details Validates precedence rules, explicit Model B ownership lifecycle,
 *          clear semantic distinction between RequestedEndpointUnavailable and BackendOpenFailed,
 *          and preservation of virtual endpoint availability prior to SS4 policy activation.
 * @author ABDSynths
 * @date 2026
 */

#include <HardwareMidiDetect/JuceMidiHardwareBackend.h>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <set>
#include <vector>

namespace abd::hwid::tests
{

class MockMidiDeviceProvider final : public IMidiDeviceProvider
{
public:
    void setAvailableOutputs(std::vector<juce::MidiDeviceInfo> outputs)
    {
        outputs_ = std::move(outputs);
    }

    void setAvailableInputs(std::vector<juce::MidiDeviceInfo> inputs)
    {
        inputs_ = std::move(inputs);
    }

    void setFailNextOpen(bool fail)
    {
        failNextOpen_ = fail;
    }

    void setEndpointUnavailable(const std::string& identifier, bool unavailable)
    {
        if (unavailable)
            unavailableEndpoints_.insert(identifier);
        else
            unavailableEndpoints_.erase(identifier);
    }

    [[nodiscard]] std::vector<juce::MidiDeviceInfo> getAvailableOutputs() const override
    {
        return outputs_;
    }

    [[nodiscard]] std::vector<juce::MidiDeviceInfo> getAvailableInputs() const override
    {
        return inputs_;
    }

    [[nodiscard]] bool isEndpointAvailable(const juce::String& identifier) const override
    {
        return unavailableEndpoints_.find(identifier.toStdString()) == unavailableEndpoints_.end();
    }

    bool openExactOutput(const juce::String& identifier) override
    {
        openExactOutputCalls_.push_back(identifier.toStdString());

        if (failNextOpen_)
            return false;

        for (const auto& dev : outputs_)
        {
            if (dev.identifier == identifier)
            {
                outputOpened_        = true;
                openedOutIdentifier_ = identifier.toStdString();
                return true;
            }
        }
        return false;
    }

    bool openExactInput(const juce::String& identifier, juce::MidiInputCallback* /*callback*/) override
    {
        openExactInputCalls_.push_back(identifier.toStdString());
        inputOpened_ = true;
        return true;
    }

    void closeOutput() noexcept override
    {
        outputOpened_ = false;
        openedOutIdentifier_.clear();
        closeOutputCalls_++;
    }

    void closeInput() noexcept override
    {
        inputOpened_ = false;
        closeInputCalls_++;
    }

    void sendSysExMessage(const std::vector<uint8_t>& bytes) override
    {
        sentSysEx_.push_back(bytes);
    }

    [[nodiscard]] bool isOutputOpen() const noexcept override
    {
        return outputOpened_;
    }

    [[nodiscard]] std::string getOpenedOutputIdentifier() const override
    {
        return openedOutIdentifier_;
    }

    [[nodiscard]] const std::vector<std::string>& getOpenExactOutputCalls() const noexcept
    {
        return openExactOutputCalls_;
    }

    [[nodiscard]] int getCloseOutputCalls() const noexcept
    {
        return closeOutputCalls_;
    }

    [[nodiscard]] bool isOutputOpened() const noexcept
    {
        return outputOpened_;
    }

    [[nodiscard]] const std::vector<std::vector<uint8_t>>& getSentSysEx() const noexcept
    {
        return sentSysEx_;
    }

private:
    std::vector<juce::MidiDeviceInfo> outputs_;
    std::vector<juce::MidiDeviceInfo> inputs_;
    std::set<std::string> unavailableEndpoints_;
    std::vector<std::string> openExactOutputCalls_;
    std::vector<std::string> openExactInputCalls_;
    std::vector<std::vector<uint8_t>> sentSysEx_;
    std::string openedOutIdentifier_;
    bool failNextOpen_{false};
    bool outputOpened_{false};
    bool inputOpened_{false};
    int closeOutputCalls_{0};
    int closeInputCalls_{0};
};

TEST_CASE("HITO-SHARED-SYNC / SS2 - Positive: Exact StableDeviceId Match Opens Solely Requested Endpoint", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "USB Audio Device Index 0";
    dev0.identifier = "usb_dev_0_fallback_hazard";

    juce::MidiDeviceInfo dev1;
    dev1.name       = "Behringer DeepMind 12D";
    dev1.identifier = "deepmind_12d_endpoint_stable";

    mockProvider->setAvailableOutputs({dev0, dev1});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "deepmind_12d_endpoint_stable";
    req.requestedDisplayName    = "";

    const auto res = backend.openStrictOutput(req);

    // 1. Success
    CHECK(res.outcome == MidiEndpointOpenOutcome::Opened);
    CHECK(res.resolutionMethod == MidiEndpointResolutionMethod::StableDeviceIdExact);
    CHECK(res.resolvedStableDeviceId == "deepmind_12d_endpoint_stable");
    CHECK(res.resolvedDisplayName == "Behringer DeepMind 12D");
    CHECK(res.diagnosticCode == "OK_MIDI_ENDPOINT_OPENED");
    CHECK(res.isSuccess());

    // 2. Exact call made, device 0 NEVER called
    const auto& calls = mockProvider->getOpenExactOutputCalls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0] == "deepmind_12d_endpoint_stable");
    CHECK(mockProvider->isOutputOpened());
    CHECK(backend.isOutputOpen());
}

TEST_CASE("HITO-SHARED-SYNC / SS2 - Positive: Exact DisplayName Match without StableId", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "Device 0";
    dev0.identifier = "id_0";

    juce::MidiDeviceInfo dev1;
    dev1.name       = "Roland Boutique JX-08";
    dev1.identifier = "jx08_unique_id";

    mockProvider->setAvailableOutputs({dev0, dev1});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "";
    req.requestedDisplayName    = "Roland Boutique JX-08";

    const auto res = backend.openStrictOutput(req);

    CHECK(res.outcome == MidiEndpointOpenOutcome::Opened);
    CHECK(res.resolutionMethod == MidiEndpointResolutionMethod::DisplayNameExact);
    CHECK(res.resolvedStableDeviceId == "jx08_unique_id");
    CHECK(res.resolvedDisplayName == "Roland Boutique JX-08");
    CHECK(res.isSuccess());

    const auto& calls = mockProvider->getOpenExactOutputCalls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0] == "jx08_unique_id");
    CHECK(backend.isOutputOpen());
}

TEST_CASE("HITO-SHARED-SYNC / SS2 - Critical Negative: Nonexistent StableDeviceId Never Opens Index 0", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "LoopBe1 Virtual Port (Index 0 Hazard)";
    dev0.identifier = "loopbe1_hazard";

    mockProvider->setAvailableOutputs({dev0});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "nonexistent_synth_id";
    req.requestedDisplayName    = "";

    const auto res = backend.openStrictOutput(req);

    // Assert fail-closed
    CHECK(res.outcome == MidiEndpointOpenOutcome::RequestedEndpointNotFound);
    CHECK(res.diagnosticCode == "ERR_MIDI_ENDPOINT_NOT_FOUND");
    CHECK(res.resolvedStableDeviceId.empty());
    CHECK_FALSE(res.isSuccess());

    // Invariant: openExactOutput was NEVER invoked
    CHECK(mockProvider->getOpenExactOutputCalls().empty());
    CHECK_FALSE(mockProvider->isOutputOpened());
    CHECK_FALSE(backend.isOutputOpen());
}

TEST_CASE("HITO-SHARED-SYNC / SS2 - Critical Negative: Nonexistent DisplayName Never Opens Index 0", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "Active Device 0";
    dev0.identifier = "active_0";

    mockProvider->setAvailableOutputs({dev0});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "";
    req.requestedDisplayName    = "Unknown Device Name";

    const auto res = backend.openStrictOutput(req);

    CHECK(res.outcome == MidiEndpointOpenOutcome::RequestedEndpointNotFound);
    CHECK(res.diagnosticCode == "ERR_MIDI_ENDPOINT_NOT_FOUND");
    CHECK(mockProvider->getOpenExactOutputCalls().empty());
    CHECK_FALSE(backend.isOutputOpen());
}

TEST_CASE("HITO-SHARED-SYNC / SS2 - Critical Negative: Empty Selection is Rejected", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "Active Device 0";
    dev0.identifier = "active_0";

    mockProvider->setAvailableOutputs({dev0});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "";
    req.requestedDisplayName    = "";

    const auto res = backend.openStrictOutput(req);

    CHECK(res.outcome == MidiEndpointOpenOutcome::InvalidSelection);
    CHECK(res.diagnosticCode == "ERR_MIDI_ENDPOINT_INVALID_SELECTION");
    CHECK(mockProvider->getOpenExactOutputCalls().empty());
    CHECK_FALSE(backend.isOutputOpen());
}

TEST_CASE("HITO-SHARED-SYNC / SS2 - Critical Negative: Ambiguous Display Name Rejects Opening", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "USB MIDI";
    dev0.identifier = "usb_midi_port_A";

    juce::MidiDeviceInfo dev1;
    dev1.name       = "USB MIDI";
    dev1.identifier = "usb_midi_port_B";

    mockProvider->setAvailableOutputs({dev0, dev1});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "";
    req.requestedDisplayName    = "USB MIDI";

    const auto res = backend.openStrictOutput(req);

    CHECK(res.outcome == MidiEndpointOpenOutcome::RequestedEndpointAmbiguous);
    CHECK(res.diagnosticCode == "ERR_MIDI_ENDPOINT_AMBIGUOUS");
    CHECK(mockProvider->getOpenExactOutputCalls().empty());
    CHECK_FALSE(backend.isOutputOpen());
}

TEST_CASE("HITO-SHARED-SYNC / SS2 - Critical Negative: Contradictory Name Rejects Opening", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "Actual Device Name";
    dev0.identifier = "stable_id_123";

    mockProvider->setAvailableOutputs({dev0});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "stable_id_123";
    req.requestedDisplayName    = "Contradictory Name";

    const auto res = backend.openStrictOutput(req);

    CHECK(res.outcome == MidiEndpointOpenOutcome::InvalidSelection);
    CHECK(res.diagnosticCode == "ERR_MIDI_ENDPOINT_INVALID_SELECTION");
    CHECK(mockProvider->getOpenExactOutputCalls().empty());
    CHECK_FALSE(backend.isOutputOpen());
}

TEST_CASE("HITO-SHARED-SYNC / SS2 - Critical Negative: Backend Failure Has No Fallback", "[shared][midi][endpoint][strict_open][no_fallback]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "Fallback Hazard";
    dev0.identifier = "hazard_0";

    juce::MidiDeviceInfo dev1;
    dev1.name       = "Target Synth";
    dev1.identifier = "target_1";

    mockProvider->setAvailableOutputs({dev0, dev1});
    mockProvider->setFailNextOpen(true); // Simulate driver open error

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "target_1";

    const auto res = backend.openStrictOutput(req);

    CHECK(res.outcome == MidiEndpointOpenOutcome::BackendOpenFailed);
    CHECK(res.diagnosticCode == "ERR_MIDI_ENDPOINT_OPEN_FAILED");
    CHECK_FALSE(res.isSuccess());

    // Verify it only attempted opening target_1 and NEVER attempted hazard_0
    const auto& calls = mockProvider->getOpenExactOutputCalls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0] == "target_1");
    CHECK_FALSE(backend.isOutputOpen());
}

// ==============================================================================
// SS2.1 Test Cases: Explicit Ownership, Lifecycle, Outcome Coverage, and Safety
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS2.1 - Caso 9: Successful Open Owns Exactly Resolved Endpoint", "[shared][midi][endpoint][strict_open][ownership]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "Hazard Device Index 0";
    dev0.identifier = "hazard_id_0";

    juce::MidiDeviceInfo dev1;
    dev1.name       = "Secondary Keyboard Index 1";
    dev1.identifier = "kb_id_1";

    juce::MidiDeviceInfo dev2;
    dev2.name       = "Target Synth Desktop Index 2";
    dev2.identifier = "target_synth_id_2";

    mockProvider->setAvailableOutputs({dev0, dev1, dev2});

    // Step 1: Request an endpoint whose index is non-zero (dev2)
    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "target_synth_id_2";

    int initialCloseCalls = 0;
    {
        JuceMidiHardwareBackend backend(mockProvider);

        const auto res = backend.openStrictOutput(req);

        // Step 2 & 3: Provider receives exactly one open call, belonging exactly to resolved ID
        const auto& calls = mockProvider->getOpenExactOutputCalls();
        REQUIRE(calls.size() == 1);
        CHECK(calls[0] == "target_synth_id_2");
        CHECK(res.resolvedStableDeviceId == "target_synth_id_2");

        // Step 4: No second endpoint opened
        CHECK(mockProvider->getOpenedOutputIdentifier() == "target_synth_id_2");
        CHECK(backend.getOpenedOutputIdentifier() == "target_synth_id_2");

        // Step 5: Handle remains valid during lifetime of backend owner
        CHECK(backend.isOutputOpen());
        CHECK(mockProvider->isOutputOpened());

        // Step 6: Explicit close releases the handle cleanly
        backend.closeOpenedOutput();
        CHECK_FALSE(backend.isOutputOpen());
        CHECK_FALSE(mockProvider->isOutputOpened());
        CHECK(backend.getOpenedOutputIdentifier().empty());
        CHECK(backend.getOpenedOutput() == nullptr);

        initialCloseCalls = mockProvider->getCloseOutputCalls();
        CHECK(initialCloseCalls >= 1);
    }

    // Step 7: Destruction leaves no dangling pointer or leaked ownership
    CHECK_FALSE(mockProvider->isOutputOpened());
}

TEST_CASE("HITO-SHARED-SYNC / SS2.1 - Caso 10: RequestedEndpointUnavailable Differentiated from BackendOpenFailed", "[shared][midi][endpoint][strict_open][unavailable]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo dev0;
    dev0.name       = "Active Host Port";
    dev0.identifier = "active_port_0";

    juce::MidiDeviceInfo dev1;
    dev1.name       = "Enumerated But Locked Synth Port";
    dev1.identifier = "locked_synth_1";

    mockProvider->setAvailableOutputs({dev0, dev1});

    // Mark locked_synth_1 as unavailable prior to open (e.g. exclusive lock by another process)
    mockProvider->setEndpointUnavailable("locked_synth_1", true);

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "locked_synth_1";

    const auto res = backend.openStrictOutput(req);

    // Invariant: Must fail with RequestedEndpointUnavailable, NOT BackendOpenFailed
    CHECK(res.outcome == MidiEndpointOpenOutcome::RequestedEndpointUnavailable);
    CHECK(res.diagnosticCode == "ERR_MIDI_ENDPOINT_UNAVAILABLE");
    CHECK_FALSE(res.isSuccess());

    // Invariant: Low-level openExactOutput was NEVER invoked because it was intercepted pre-open
    CHECK(mockProvider->getOpenExactOutputCalls().empty());
    CHECK_FALSE(backend.isOutputOpen());
    CHECK_FALSE(mockProvider->isOutputOpened());
}

TEST_CASE("HITO-SHARED-SYNC / SS2.1 - Caso 11: Re-opening Closes Previous Endpoint and Failures Leave Handle Closed", "[shared][midi][endpoint][strict_open][lifecycle]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo devA;
    devA.name       = "Endpoint Alpha";
    devA.identifier = "id_alpha";

    juce::MidiDeviceInfo devB;
    devB.name       = "Endpoint Beta";
    devB.identifier = "id_beta";

    mockProvider->setAvailableOutputs({devA, devB});

    JuceMidiHardwareBackend backend(mockProvider);

    // 1. Open Alpha successfully
    MidiEndpointSelectionRequest reqA;
    reqA.requestedStableDeviceId = "id_alpha";
    auto resA                    = backend.openStrictOutput(reqA);
    CHECK(resA.isSuccess());
    CHECK(backend.getOpenedOutputIdentifier() == "id_alpha");

    // 2. Now open Beta: backend must cleanly close Alpha first
    MidiEndpointSelectionRequest reqB;
    reqB.requestedStableDeviceId = "id_beta";
    auto resB                    = backend.openStrictOutput(reqB);
    CHECK(resB.isSuccess());
    CHECK(backend.getOpenedOutputIdentifier() == "id_beta");

    // 3. Now attempt opening a nonexistent Gamma: backend must close handle and remain closed
    MidiEndpointSelectionRequest reqGamma;
    reqGamma.requestedStableDeviceId = "nonexistent_gamma";
    auto resGamma                    = backend.openStrictOutput(reqGamma);
    CHECK(resGamma.outcome == MidiEndpointOpenOutcome::RequestedEndpointNotFound);
    CHECK_FALSE(backend.isOutputOpen());
    CHECK(backend.getOpenedOutputIdentifier().empty());
    CHECK_FALSE(mockProvider->isOutputOpened());
}

TEST_CASE("HITO-SHARED-SYNC / SS2.1 - Caso 12: Virtual Endpoint Allowed in SS2 without Safety Policy (Reserved for SS4)", "[shared][midi][endpoint][strict_open][virtual_policy_deferred]")
{
    auto mockProvider = std::make_shared<MockMidiDeviceProvider>();

    juce::MidiDeviceInfo virtualDev;
    virtualDev.name       = "loopMIDI Port 1";
    virtualDev.identifier = "virtual_loopmidi_1";

    mockProvider->setAvailableOutputs({virtualDev});

    JuceMidiHardwareBackend backend(mockProvider);

    MidiEndpointSelectionRequest req;
    req.requestedStableDeviceId = "virtual_loopmidi_1";
    req.requestedDisplayName    = "loopMIDI Port 1";

    // In SS2 / SS2.1, without SS4 MidiEndpointSafetyPolicy active, explicit selection of a valid endpoint succeeds
    const auto res = backend.openStrictOutput(req);
    CHECK(res.outcome == MidiEndpointOpenOutcome::Opened);
    CHECK(res.diagnosticCode == "OK_MIDI_ENDPOINT_OPENED");
    CHECK(res.isSuccess());
    CHECK(backend.isOutputOpen());
    CHECK(backend.getOpenedOutputIdentifier() == "virtual_loopmidi_1");
}

} // namespace abd::hwid::tests
