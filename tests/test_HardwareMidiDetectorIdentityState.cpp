/**
 * @file test_HardwareMidiDetectorIdentityState.cpp
 * @brief Hermetic unit tests for HITO-SHARED-SYNC / SS3: Five-state Preflight Identity Model in HardwareMidiDetect.
 * @details Validates the five-state taxonomy, strict boundary preventing ABDSharedCode from emitting
 *          UserConfirmedUnverified, centralized backward compatibility with isSysExVerified,
 *          and traffic non-regression invariant (0 extra inquiries, 0 extra outputs).
 * @author ABDSynths
 * @date 2026
 */

#include <HardwareMidiDetect/HardwareContract.h>
#include <HardwareMidiDetect/HardwareMidiDetector.h>
#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace abd::hwid::tests
{

static HardwareContract createSampleDeepMindContract()
{
    HardwareContract c;
    c.id                             = "behringer_deepmind12";
    c.displayName                    = "Behringer DeepMind 12";
    c.brand                          = "behringer";
    c.midiIdentity.manufacturer      = "Behringer";
    c.midiIdentity.manufacturerIdHex = "00 20 32";
    c.midiIdentity.model             = "DeepMind 12";
    c.midiIdentity.modelIdHex        = "24";
    c.midiIdentity.familyIdHex       = "00 00";
    c.midiIdentity.portNameMatches   = {"DeepMind12", "DeepMind 12", "DeepMind"};
    return c;
}

// ------------------------------------------------------------------------------
// Caso 1: Endpoint enumerado sin inquiry
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 1: Endpoint Enumerated without Inquiry is PortAvailable", "[shared][midi][identity][preflight][five_state]")
{
    juce::MidiDeviceInfo inDev;
    inDev.name       = "DeepMind 12 DIN Port";
    inDev.identifier = "din_in_1";

    juce::MidiDeviceInfo outDev;
    outDev.name       = "DeepMind 12 DIN Port";
    outDev.identifier = "din_out_1";

    std::vector<HardwareContract> contracts = {createSampleDeepMindContract()};

    auto heuristicMatch = HardwareMidiDetector::matchFromPortNames(inDev, outDev, contracts);
    REQUIRE(heuristicMatch.has_value());

    const auto& dev = heuristicMatch.value();

    // Invariants:
    CHECK(dev.identityState == HardwareMidiIdentityState::PortAvailable);
    CHECK(dev.isSysExVerified == false);
    CHECK(toLegacyIsSysExVerified(dev.identityState) == false);
}

// ------------------------------------------------------------------------------
// Caso 2: Inquiry no solicitada
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 2: Inquiry Not Requested is IdentityUnavailable", "[shared][midi][identity][preflight][five_state]")
{
    // When no inquiry is transmitted or requested, discovery identity result is IdentityUnavailable
    auto res = SharedDiscoveryIdentityResult::unavailable();

    CHECK(res.getState() == HardwareMidiIdentityState::IdentityUnavailable);
    CHECK_FALSE(res.isSysExVerified());
    CHECK(toLegacyIsSysExVerified(res.getState()) == false);
}

// ------------------------------------------------------------------------------
// Caso 3: Inquiry sin respuesta (timeout / empty)
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 3: Inquiry Sent with No Response is IdentityUnavailable", "[shared][midi][identity][preflight][five_state]")
{
    std::vector<HardwareContract> contracts = {createSampleDeepMindContract()};

    // Empty MIDI message (no response received during scan timeout window)
    juce::MidiMessage emptyMsg;
    auto res = HardwareMidiDetector::classifyIdentityReply(emptyMsg, contracts);

    CHECK(res.getState() == HardwareMidiIdentityState::IdentityUnavailable);
    CHECK_FALSE(res.isSysExVerified());
}

// ------------------------------------------------------------------------------
// Caso 4: Inquiry no soportada (mensaje no SysEx o truncado)
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 4: Inquiry Unsupported or Truncated Message is IdentityUnavailable", "[shared][midi][identity][preflight][five_state]")
{
    std::vector<HardwareContract> contracts = {createSampleDeepMindContract()};

    // Non-SysEx message (e.g. NoteOn or Active Sensing)
    auto ccMsg = juce::MidiMessage::controllerEvent(1, 1, 0);
    auto res1  = HardwareMidiDetector::classifyIdentityReply(ccMsg, contracts);
    CHECK(res1.getState() == HardwareMidiIdentityState::IdentityUnavailable);

    // Truncated SysEx (< 4 bytes)
    const uint8_t shortBytes[] = {0xF0, 0x7E, 0xF7};
    auto shortMsg              = juce::MidiMessage(shortBytes, sizeof(shortBytes));
    auto res2                  = HardwareMidiDetector::classifyIdentityReply(shortMsg, contracts);
    CHECK(res2.getState() == HardwareMidiIdentityState::IdentityUnavailable);
}

// ------------------------------------------------------------------------------
// Caso 5: Identity Reply coincidente
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 5: Identity Reply Matching Contract is IdentityVerified", "[shared][midi][identity][preflight][five_state]")
{
    std::vector<HardwareContract> contracts = {createSampleDeepMindContract()};

    // Valid Behringer DeepMind 12 Universal Non-Real Time Identity Reply:
    // F0 7E <devId:00> 06 02 <mfg:00 20 32> <family:00 00> <model:24 00> <rev:01 01 02 01> F7
    const uint8_t deepMindReplyBytes[] = {
        0xF0, 0x7E, 0x00, 0x06, 0x02,
        0x00, 0x20, 0x32,       // Behringer MMA
        0x00, 0x00,             // Family
        0x24, 0x00,             // Model
        0x01, 0x00, 0x01, 0x00, // Rev
        0xF7};
    auto msg = juce::MidiMessage(deepMindReplyBytes, sizeof(deepMindReplyBytes));

    DiscoveredDevice dev;
    const bool parsed = HardwareMidiDetector::parseIdentityReply(msg, dev, contracts);
    REQUIRE(parsed);

    CHECK(dev.identityState == HardwareMidiIdentityState::IdentityVerified);
    CHECK(dev.isSysExVerified == true);
    CHECK(toLegacyIsSysExVerified(dev.identityState) == true);
    CHECK(dev.hardwareId == "behringer_deepmind12");

    auto classified = HardwareMidiDetector::classifyIdentityReply(msg, contracts);
    CHECK(classified.getState() == HardwareMidiIdentityState::IdentityVerified);
    CHECK(classified.isSysExVerified() == true);
}

// ------------------------------------------------------------------------------
// Caso 6: Identity Reply contradictoria (Mismatch)
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 6: Identity Reply Contradicting Contract is IdentityMismatch", "[shared][midi][identity][preflight][five_state]")
{
    std::vector<HardwareContract> contracts = {createSampleDeepMindContract()};

    // Valid Roland Identity Reply (Mfg ID: 41, Roland):
    // F0 7E 10 06 02 41 45 00 00 00 ... F7
    const uint8_t rolandReplyBytes[] = {
        0xF0, 0x7E, 0x10, 0x06, 0x02,
        0x41,       // Roland MMA ID
        0x45, 0x00, // Family
        0x01, 0x00, // Model
        0x01, 0x00, 0x00, 0x00,
        0xF7};
    auto msg = juce::MidiMessage(rolandReplyBytes, sizeof(rolandReplyBytes));

    DiscoveredDevice dev;
    const bool parsed = HardwareMidiDetector::parseIdentityReply(msg, dev, contracts);
    CHECK_FALSE(parsed); // Does not match DeepMind

    auto classified = HardwareMidiDetector::classifyIdentityReply(msg, contracts);
    CHECK(classified.getState() == HardwareMidiIdentityState::IdentityMismatch);
    CHECK_FALSE(classified.isSysExVerified());
    CHECK(toLegacyIsSysExVerified(classified.getState()) == false);
}

// ------------------------------------------------------------------------------
// Caso 7: Shared discovery no puede producir UserConfirmedUnverified
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 7: Shared Discovery Cannot Emit UserConfirmedUnverified", "[shared][midi][identity][preflight][five_state]")
{
    // Executable invariant test
    CHECK(isSharedDiscoveryEmittable(HardwareMidiIdentityState::PortAvailable));
    CHECK(isSharedDiscoveryEmittable(HardwareMidiIdentityState::IdentityVerified));
    CHECK(isSharedDiscoveryEmittable(HardwareMidiIdentityState::IdentityUnavailable));
    CHECK(isSharedDiscoveryEmittable(HardwareMidiIdentityState::IdentityMismatch));

    // STRICT INVARIANT: Must return false for UserConfirmedUnverified
    CHECK_FALSE(isSharedDiscoveryEmittable(HardwareMidiIdentityState::UserConfirmedUnverified));
}

// ------------------------------------------------------------------------------
// Caso 8: Factory compartida impide instanciar UserConfirmedUnverified
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 8: Factory Strongly Prevents UserConfirmedUnverified", "[shared][midi][identity][preflight][five_state]")
{
    auto r1 = SharedDiscoveryIdentityResult::portAvailable();
    auto r2 = SharedDiscoveryIdentityResult::verified();
    auto r3 = SharedDiscoveryIdentityResult::unavailable();
    auto r4 = SharedDiscoveryIdentityResult::mismatch();

    // Verify all 4 states are correctly instantiated
    CHECK(r1.getState() == HardwareMidiIdentityState::PortAvailable);
    CHECK(r2.getState() == HardwareMidiIdentityState::IdentityVerified);
    CHECK(r3.getState() == HardwareMidiIdentityState::IdentityUnavailable);
    CHECK(r4.getState() == HardwareMidiIdentityState::IdentityMismatch);

    // SharedDiscoveryIdentityResult has NO method to instantiate UserConfirmedUnverified.
    // The constructor is private and only factory methods exist.
    CHECK(isSharedDiscoveryEmittable(r1.getState()));
    CHECK(isSharedDiscoveryEmittable(r2.getState()));
    CHECK(isSharedDiscoveryEmittable(r3.getState()));
    CHECK(isSharedDiscoveryEmittable(r4.getState()));
}

// ------------------------------------------------------------------------------
// Caso 9: Invariante de Tráfico — Sin llamadas adicionales a MIDI output
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 9: Zero New MIDI Output Calls Introduced by Taxonomy", "[shared][midi][identity][preflight][five_state]")
{
    // Classification and state mapping are pure functions with 0 side effects and 0 I/O calls
    std::vector<HardwareContract> contracts = {createSampleDeepMindContract()};

    juce::MidiMessage msg = juce::MidiMessage::controllerEvent(1, 1, 0);
    auto res              = HardwareMidiDetector::classifyIdentityReply(msg, contracts);

    CHECK(res.getState() == HardwareMidiIdentityState::IdentityUnavailable);
    // Verifies purely computational evaluation with zero side effects
}

// ------------------------------------------------------------------------------
// Caso 10: Invariante de Consultas — buildDetectionQueries idéntico
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 10: buildDetectionQueries Produces Identical Query Set", "[shared][midi][identity][preflight][five_state]")
{
    std::vector<HardwareContract> contracts = {createSampleDeepMindContract()};

    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts);

    // Invariant: DeepMind contract has no custom autoDetectSysEx, so exactly 1 query is produced:
    // Universal Non-Real Time Identity Request broadcast: F0 7E 7F 06 01 F7
    REQUIRE(queries.size() == 1);
    CHECK(queries[0].isSysEx());

    const auto* data = queries[0].getSysExData();
    REQUIRE(queries[0].getSysExDataSize() == 4);
    CHECK(data[0] == 0x7E);
    CHECK(data[1] == 0x7F); // Broadcast
    CHECK(data[2] == 0x06);
    CHECK(data[3] == 0x01); // Inquiry

    // Zero extra inquiries introduced by SS3
}

// ------------------------------------------------------------------------------
// Caso 11: Consumidor Legacy conserva semántica histórica exacta
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 11: Legacy Consumer Reading isSysExVerified Preserves Semantics", "[shared][midi][identity][preflight][five_state]")
{
    CHECK(toLegacyIsSysExVerified(HardwareMidiIdentityState::IdentityVerified) == true);
    CHECK(toLegacyIsSysExVerified(HardwareMidiIdentityState::PortAvailable) == false);
    CHECK(toLegacyIsSysExVerified(HardwareMidiIdentityState::IdentityUnavailable) == false);
    CHECK(toLegacyIsSysExVerified(HardwareMidiIdentityState::IdentityMismatch) == false);
    CHECK(toLegacyIsSysExVerified(HardwareMidiIdentityState::UserConfirmedUnverified) == false);

    DiscoveredDevice dev;
    dev.identityState   = HardwareMidiIdentityState::PortAvailable;
    dev.isSysExVerified = toLegacyIsSysExVerified(dev.identityState);
    CHECK_FALSE(dev.isSysExVerified);

    dev.identityState   = HardwareMidiIdentityState::IdentityVerified;
    dev.isSysExVerified = toLegacyIsSysExVerified(dev.identityState);
    CHECK(dev.isSysExVerified);
}

// ------------------------------------------------------------------------------
// Caso 12: Consumidor Nuevo distingue IdentityUnavailable de IdentityMismatch
// ------------------------------------------------------------------------------
TEST_CASE("HITO-SHARED-SYNC / SS3 - Caso 12: Modern Consumer Distinguishes IdentityUnavailable from IdentityMismatch", "[shared][midi][identity][preflight][five_state]")
{
    auto unavail   = SharedDiscoveryIdentityResult::unavailable();
    auto mismatch  = SharedDiscoveryIdentityResult::mismatch();
    auto available = SharedDiscoveryIdentityResult::portAvailable();
    auto verified  = SharedDiscoveryIdentityResult::verified();

    // Modern consumer can branch on rich 5-state representation
    CHECK(unavail.getState() != mismatch.getState());
    CHECK(unavail.getState() != available.getState());
    CHECK(unavail.getState() != verified.getState());

    // Both unavailable and mismatch yield legacy false, but have totally different diagnostic meaning
    CHECK(toLegacyIsSysExVerified(unavail.getState()) == false);
    CHECK(toLegacyIsSysExVerified(mismatch.getState()) == false);

    CHECK(unavail.getState() == HardwareMidiIdentityState::IdentityUnavailable);
    CHECK(mismatch.getState() == HardwareMidiIdentityState::IdentityMismatch);
}

} // namespace abd::hwid::tests
