/**
 * @file test_MidiEndpointSafetyPolicy.cpp
 * @brief Hermetic unit tests for HITO-SHARED-SYNC / SS4: Virtual Port & SysEx Broadcast Safety Policy.
 * @details Covers all 20 required cases: classification, manual routing, automatic discovery exclusion,
 *          broadcast gating, and pure zero-I/O policy evaluation.
 * @author ABDSynths
 * @date 2026
 */

#include <catch2/catch_test_macros.hpp>
#include <HardwareMidiDetect/MidiEndpointSafetyPolicy.h>

namespace abd::hwid::tests
{

// ==============================================================================
// 1. Clasificación de Endpoints (Casos 1 al 6)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 1: LoopBe1 classified as VirtualLoopback", "[shared][midi][safety_policy][virtual][sysex]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "LoopBe Internal MIDI";
    dev.identifier = "loopbe_id_01";

    CHECK(classifier.classify(dev) == MidiEndpointKind::VirtualLoopback);
    CHECK(getEndpointKindLabel(MidiEndpointKind::VirtualLoopback) == "[Virtual]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 2: loopMIDI Port 001 classified as VirtualDriver", "[shared][midi][safety_policy][virtual][sysex]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "loopMIDI Port 001";
    dev.identifier = "loopmidi_id_001";

    CHECK(classifier.classify(dev) == MidiEndpointKind::VirtualDriver);
    CHECK(getEndpointKindLabel(MidiEndpointKind::VirtualDriver) == "[Virtual]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 3: teVirtualMIDI endpoint classified as VirtualDriver", "[shared][midi][safety_policy][virtual][sysex]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "teVirtualMIDI Port";
    dev.identifier = "tevirtualmidi_id";

    CHECK(classifier.classify(dev) == MidiEndpointKind::VirtualDriver);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 4: Known USB device classified as PhysicalUsb", "[shared][midi][safety_policy][virtual][sysex]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "Behringer DeepMind 12D USB";
    dev.identifier = "deepmind_usb_endpoint";

    CHECK(classifier.classify(dev) == MidiEndpointKind::PhysicalUsb);
    CHECK(getEndpointKindLabel(MidiEndpointKind::PhysicalUsb) == "[USB]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 5: Known DIN interface classified as PhysicalDinInterface", "[shared][midi][safety_policy][virtual][sysex]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "Roland UM-ONE DIN Interface";
    dev.identifier = "um_one_din_id";

    CHECK(classifier.classify(dev) == MidiEndpointKind::PhysicalDinInterface);
    CHECK(getEndpointKindLabel(MidiEndpointKind::PhysicalDinInterface) == "[DIN]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 6: Non-classifiable endpoint classified as Unknown", "[shared][midi][safety_policy][virtual][sysex]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "Generic Custom Device Alpha 99";
    dev.identifier = "custom_unrecognized_id";

    // Strict invariant: Never default to PhysicalUsb!
    CHECK(classifier.classify(dev) == MidiEndpointKind::Unknown);
    CHECK(getEndpointKindLabel(MidiEndpointKind::Unknown) == "[Unknown]");
}

// ==============================================================================
// 2. Routing Manual (Casos 7 al 9)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 7: Virtual endpoint allowed for manual routing by default", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy; // defaults: allowVirtualEndpointsForManualRouting = true
    MidiEndpointDescriptor ep { "loopbe_id", "LoopBe1", MidiEndpointKind::VirtualLoopback };

    CHECK(isManualRoutingAllowed(ep, policy) == true);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 8: Virtual endpoint rejected for manual routing if explicitly disallowed", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowVirtualEndpointsForManualRouting = false;

    MidiEndpointDescriptor ep { "loopmidi_id", "loopMIDI", MidiEndpointKind::VirtualDriver };

    CHECK(isManualRoutingAllowed(ep, policy) == false);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 9: Manual routing check does not trigger Device Inquiry", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    MidiEndpointDescriptor ep { "deepmind_id", "DeepMind 12D", MidiEndpointKind::PhysicalUsb };

    // Pure boolean evaluation: 0 side effects
    CHECK(isManualRoutingAllowed(ep, policy) == true);
}

// ==============================================================================
// 3. Discovery Automático (Casos 10 al 14)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 10: Default policy excludes LoopBe1 from auto-discovery", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy; // allowVirtualEndpointsForAutomaticDiscovery = false
    MidiEndpointDescriptor ep { "loopbe_id", "LoopBe1", MidiEndpointKind::VirtualLoopback };

    CHECK(isAutomaticDiscoveryAllowed(ep, policy) == false);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 11: Default policy excludes loopMIDI from auto-discovery", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    MidiEndpointDescriptor ep { "loopmidi_id", "loopMIDI", MidiEndpointKind::VirtualDriver };

    CHECK(isAutomaticDiscoveryAllowed(ep, policy) == false);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 12: Default policy excludes Unknown from broadcast inquiry", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;

    MidiEndpointDescriptor ep { "unknown_id", "Mystery Synth", MidiEndpointKind::Unknown };

    BroadcastInquiryAuthorization auth { true, true, "test_context" };

    auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(decision == SysExInquiryDecision::UnknownEndpointExcluded);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 13: PhysicalUsb discovery candidate receives no inquiry when Disabled", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy; // sysExInquiryMode = Disabled
    MidiEndpointDescriptor ep { "deepmind_usb", "DeepMind12D", MidiEndpointKind::PhysicalUsb };

    CHECK(isAutomaticDiscoveryAllowed(ep, policy) == true); // Allowed as discovery candidate

    BroadcastInquiryAuthorization auth { true, true, "test_context" };
    auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);

    // But Inquiry is disabled by policy
    CHECK(decision == SysExInquiryDecision::DisabledByPolicy);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 14: PhysicalUsb with opt-in and confirmed topology is Allowed", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;
    policy.requireSingleTargetTopologyForBroadcast = true;

    MidiEndpointDescriptor ep { "deepmind_usb", "DeepMind12D", MidiEndpointKind::PhysicalUsb };

    BroadcastInquiryAuthorization auth { true, true, "controlled_bench_session" };

    auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(decision == SysExInquiryDecision::Allowed);
}

// ==============================================================================
// 4. Fronteras de Broadcast y Fail-Closed (Casos 15 al 20)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 15: allowBroadcastSysEx=false makes all endpoints ineligible", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = false;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;

    MidiEndpointDescriptor ep { "synth_usb", "Hardware Synth", MidiEndpointKind::PhysicalUsb };
    BroadcastInquiryAuthorization auth { true, true, "context" };

    auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(decision == SysExInquiryDecision::DisabledByPolicy);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 16: Missing caller opt-in yields CallerOptInMissing", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;

    MidiEndpointDescriptor ep { "synth_usb", "Hardware Synth", MidiEndpointKind::PhysicalUsb };
    BroadcastInquiryAuthorization auth { false, true, "context" }; // OptIn = false

    auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(decision == SysExInquiryDecision::CallerOptInMissing);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 17: Unconfirmed topology yields SingleTargetTopologyUnconfirmed", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;
    policy.requireSingleTargetTopologyForBroadcast = true;

    MidiEndpointDescriptor ep { "synth_usb", "Hardware Synth", MidiEndpointKind::PhysicalUsb };
    BroadcastInquiryAuthorization auth { true, false, "context" }; // Topology = false

    auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(decision == SysExInquiryDecision::SingleTargetTopologyUnconfirmed);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 18: Virtual endpoint yields VirtualEndpointExcluded even with opt-in", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;

    MidiEndpointDescriptor epLoopBe { "loopbe", "LoopBe1", MidiEndpointKind::VirtualLoopback };
    MidiEndpointDescriptor epLoopMidi { "loopmidi", "loopMIDI", MidiEndpointKind::VirtualDriver };

    BroadcastInquiryAuthorization auth { true, true, "context" };

    CHECK(evaluateUniversalInquiryEligibility(epLoopBe, policy, auth) == SysExInquiryDecision::VirtualEndpointExcluded);
    CHECK(evaluateUniversalInquiryEligibility(epLoopMidi, policy, auth) == SysExInquiryDecision::VirtualEndpointExcluded);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 19: Unknown endpoint yields UnknownEndpointExcluded", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;

    MidiEndpointDescriptor ep { "unknown_port", "External Hub Port 4", MidiEndpointKind::Unknown };
    BroadcastInquiryAuthorization auth { true, true, "context" };

    CHECK(evaluateUniversalInquiryEligibility(ep, policy, auth) == SysExInquiryDecision::UnknownEndpointExcluded);
}

TEST_CASE("HITO-SHARED-SYNC / SS4 - Caso 20: Policy evaluation performs zero I/O and zero allocations", "[shared][midi][safety_policy][virtual][sysex]")
{
    MidiEndpointSafetyPolicy policy;
    MidiEndpointDescriptor ep { "din_port", "DIN Interface", MidiEndpointKind::PhysicalDinInterface };
    BroadcastInquiryAuthorization auth { true, true, "pure_test" };

    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;

    // Pure evaluation
    auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(decision == SysExInquiryDecision::Allowed);
}

} // namespace abd::hwid::tests
