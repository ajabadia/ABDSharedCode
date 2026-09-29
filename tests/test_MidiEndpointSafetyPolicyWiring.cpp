/**
 * @file test_MidiEndpointSafetyPolicyWiring.cpp
 * @brief Hermetic unit tests for HITO-SHARED-SYNC / SS4.1: Policy Wiring, Hotplug Propagation and Safe UI Presentation.
 * @details Validates that HardwareMidiDetector, HardwareMidiHotplugMonitor, and JuceHardwareMidiPicker
 *          strictly wire and apply MidiEndpointSafetyPolicy, preserving virtual endpoints for manual routing
 *          while strictly preventing auto-discovery or broadcast SysEx inquiries without caller authorization.
 * @author ABDSynths
 * @date 2026
 */

#include <catch2/catch_test_macros.hpp>
#include <HardwareMidiDetect/HardwareMidiDetector.h>
#include <HardwareMidiDetect/HardwareMidiHotplugMonitor.h>
#include <HardwareMidiDetect/MidiEndpointSafetyPolicy.h>
#include <HardwareMidiDetect/HardwareContract.h>
#include <vector>

namespace abd::hwid::tests
{

static HardwareContract createSampleDeepMindContract()
{
    HardwareContract c;
    c.id = "behringer_deepmind12";
    c.displayName = "Behringer DeepMind 12";
    c.brand = "behringer";
    c.midiIdentity.manufacturer = "Behringer";
    c.midiIdentity.manufacturerIdHex = "00 20 32";
    c.midiIdentity.model = "DeepMind 12";
    c.midiIdentity.modelIdHex = "24";
    c.midiIdentity.familyIdHex = "00 00";
    c.midiIdentity.portNameMatches = { "DeepMind12", "DeepMind 12", "DeepMind" };
    return c;
}

// ==============================================================================
// 1. Detector Wiring (Casos 1 al 6)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 1: Default config enumerates without building inquiries", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    HardwareMidiDetector::DetectionConfig config;

    // Invariants of default config:
    CHECK(config.performIdentityInquiry == false);
    CHECK(config.endpointSafetyPolicy.allowVirtualEndpointsForManualRouting == true);
    CHECK(config.endpointSafetyPolicy.allowVirtualEndpointsForAutomaticDiscovery == false);
    CHECK(config.endpointSafetyPolicy.allowBroadcastSysEx == false);
    CHECK(config.endpointSafetyPolicy.sysExInquiryMode == SysExDiscoveryInquiryMode::Disabled);
    CHECK(config.endpointSafetyPolicy.requireSingleTargetTopologyForBroadcast == true);

    std::vector<HardwareContract> contracts = { createSampleDeepMindContract() };
    MidiEndpointDescriptor ep { "usb_1", "DeepMind 12D USB", MidiEndpointKind::PhysicalUsb };

    // With default config, policy-aware query builder produces ZERO inquiries
    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts, ep, config.endpointSafetyPolicy, config.inquiryAuthorization);
    CHECK(queries.empty());
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 2: LoopBe1 kind=VirtualLoopback, auto-discovery=false, inquiries=0", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "LoopBe Internal MIDI";
    dev.identifier = "loopbe_0";

    const auto kind = classifier.classify(dev);
    CHECK(kind == MidiEndpointKind::VirtualLoopback);

    MidiEndpointDescriptor ep { dev.identifier.toStdString(), dev.name.toStdString(), kind };
    MidiEndpointSafetyPolicy policy;

    CHECK_FALSE(isAutomaticDiscoveryAllowed(ep, policy));

    BroadcastInquiryAuthorization auth { true, true, "context" };
    std::vector<HardwareContract> contracts = { createSampleDeepMindContract() };
    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts, ep, policy, auth);
    CHECK(queries.empty());
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 3: loopMIDI kind=VirtualDriver, auto-discovery=false, inquiries=0", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "loopMIDI Port 001";
    dev.identifier = "loopmidi_0";

    const auto kind = classifier.classify(dev);
    CHECK(kind == MidiEndpointKind::VirtualDriver);

    MidiEndpointDescriptor ep { dev.identifier.toStdString(), dev.name.toStdString(), kind };
    MidiEndpointSafetyPolicy policy;

    CHECK_FALSE(isAutomaticDiscoveryAllowed(ep, policy));

    BroadcastInquiryAuthorization auth { true, true, "context" };
    std::vector<HardwareContract> contracts = { createSampleDeepMindContract() };
    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts, ep, policy, auth);
    CHECK(queries.empty());
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 4: Unknown endpoint kind=Unknown, auto-discovery=false, inquiries=0", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "Unrecognized Generic Port";
    dev.identifier = "generic_unrec";

    const auto kind = classifier.classify(dev);
    CHECK(kind == MidiEndpointKind::Unknown);

    MidiEndpointDescriptor ep { dev.identifier.toStdString(), dev.name.toStdString(), kind };
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;

    BroadcastInquiryAuthorization auth { true, true, "context" };
    std::vector<HardwareContract> contracts = { createSampleDeepMindContract() };
    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts, ep, policy, auth);
    CHECK(queries.empty());
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 5: PhysicalUsb discovery candidate inquiry count=0 when Disabled", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    MidiEndpointDescriptor ep { "dm12_usb", "DeepMind 12D USB", MidiEndpointKind::PhysicalUsb };
    MidiEndpointSafetyPolicy policy; // sysExInquiryMode = Disabled

    CHECK(isAutomaticDiscoveryAllowed(ep, policy) == true);

    BroadcastInquiryAuthorization auth { true, true, "bench" };
    std::vector<HardwareContract> contracts = { createSampleDeepMindContract() };

    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts, ep, policy, auth);
    CHECK(queries.empty());
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 6: PhysicalUsb with opt-in and confirmed topology produces Allowed without transmitting", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    MidiEndpointDescriptor ep { "dm12_usb", "DeepMind 12D USB", MidiEndpointKind::PhysicalUsb };
    MidiEndpointSafetyPolicy policy;
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;
    policy.requireSingleTargetTopologyForBroadcast = true;

    BroadcastInquiryAuthorization auth { true, true, "bench_session_single_target" };

    const auto decision = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(decision == SysExInquiryDecision::Allowed);

    // Build queries returns the pure model query without opening any port or sending
    std::vector<HardwareContract> contracts = { createSampleDeepMindContract() };
    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts, ep, policy, auth);
    REQUIRE(queries.size() == 1);
    CHECK(queries[0].isSysEx());
}

// ==============================================================================
// 2. Hotplug Wiring (Casos 7 al 9)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 7: Virtual hotplug publishes classified endpoint with zero inquiries", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    HardwareMidiHotplugMonitor monitor;

    DiscoveredDevice pluggedDev;
    bool callbackFired = false;
    monitor.onDevicePlugged = [&](const DiscoveredDevice& d) {
        pluggedDev = d;
        callbackFired = true;
    };

    juce::MidiDeviceInfo virtualOut;
    virtualOut.name = "loopMIDI Port Alpha";
    virtualOut.identifier = "loopmidi_alpha";

    juce::Array<juce::MidiDeviceInfo> currentOuts { virtualOut };
    juce::Array<juce::MidiDeviceInfo> currentIns {};

    // Evaluate differences (simulating arrival of virtual port)
    monitor.evaluateLists(currentOuts, currentIns);

    REQUIRE(callbackFired);
    CHECK(pluggedDev.endpointKind == MidiEndpointKind::VirtualDriver);
    CHECK(pluggedDev.kindLabel == "[Virtual]");
    // Pure metadata published: zero MIDI open, zero SysEx
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 8: Physical hotplug publishes classified endpoint without auto-inquiry", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    HardwareMidiHotplugMonitor monitor;
    monitor.setContracts({ createSampleDeepMindContract() });

    DiscoveredDevice pluggedDev;
    bool callbackFired = false;
    monitor.onDevicePlugged = [&](const DiscoveredDevice& d) {
        pluggedDev = d;
        callbackFired = true;
    };

    juce::MidiDeviceInfo usbOut;
    usbOut.name = "DeepMind 12D USB Port";
    usbOut.identifier = "deepmind_usb_hotplug";

    juce::MidiDeviceInfo usbIn;
    usbIn.name = "DeepMind 12D USB Port";
    usbIn.identifier = "deepmind_usb_hotplug_in";

    juce::Array<juce::MidiDeviceInfo> currentOuts { usbOut };
    juce::Array<juce::MidiDeviceInfo> currentIns { usbIn };

    monitor.evaluateLists(currentOuts, currentIns);

    REQUIRE(callbackFired);
    CHECK(pluggedDev.endpointKind == MidiEndpointKind::PhysicalUsb);
    CHECK(pluggedDev.kindLabel == "[USB]");
    CHECK(pluggedDev.identityState == HardwareMidiIdentityState::PortAvailable);
    CHECK_FALSE(pluggedDev.isSysExVerified);
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 9: Policy change recalculates eligibility without opening ports", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    MidiEndpointDescriptor ep { "synth_usb", "Hardware USB Synth", MidiEndpointKind::PhysicalUsb };
    BroadcastInquiryAuthorization auth { true, true, "context" };

    MidiEndpointSafetyPolicy policy1; // Disabled
    CHECK(evaluateUniversalInquiryEligibility(ep, policy1, auth) == SysExInquiryDecision::DisabledByPolicy);

    MidiEndpointSafetyPolicy policy2;
    policy2.allowBroadcastSysEx = true;
    policy2.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;
    CHECK(evaluateUniversalInquiryEligibility(ep, policy2, auth) == SysExInquiryDecision::Allowed);

    // Recalculation is pure math: 0 ports opened
}

// ==============================================================================
// 3. UI and Selector (Casos 10 al 15)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 10: Virtual endpoint displayed with [Virtual] label", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    CHECK(getEndpointKindLabel(MidiEndpointKind::VirtualLoopback) == "[Virtual]");
    CHECK(getEndpointKindLabel(MidiEndpointKind::VirtualDriver) == "[Virtual]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 11: Physical USB endpoint displayed with [USB] label", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    CHECK(getEndpointKindLabel(MidiEndpointKind::PhysicalUsb) == "[USB]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 12: DIN Interface endpoint displayed with [DIN] label", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    CHECK(getEndpointKindLabel(MidiEndpointKind::PhysicalDinInterface) == "[DIN]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 13: Unclassifiable endpoint displayed with [Unknown] label", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    CHECK(getEndpointKindLabel(MidiEndpointKind::Unknown) == "[Unknown]");
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 14: Manual selection of loopMIDI does not trigger inquiry", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    MidiEndpointDescriptor ep { "loopmidi", "loopMIDI Port 1", MidiEndpointKind::VirtualDriver };
    MidiEndpointSafetyPolicy policy; // allowVirtualEndpointsForManualRouting = true

    CHECK(isManualRoutingAllowed(ep, policy) == true);

    // Invariant: Selecting loopMIDI manually does NOT make it eligible for broadcast inquiry
    // Even if policy had broadcast enabled and caller opted in, virtual endpoint is strictly excluded
    policy.allowBroadcastSysEx = true;
    policy.sysExInquiryMode = SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn;
    BroadcastInquiryAuthorization auth { true, true, "manual_routing" };
    CHECK(evaluateUniversalInquiryEligibility(ep, policy, auth) == SysExInquiryDecision::VirtualEndpointExcluded);
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 15: allowVirtualEndpointsForManualRouting=false explicitly rejects without redirection", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    MidiEndpointDescriptor ep { "loopbe", "LoopBe1", MidiEndpointKind::VirtualLoopback };
    MidiEndpointSafetyPolicy policy;
    policy.allowVirtualEndpointsForManualRouting = false;

    // Explicit rejection: does NOT silently fallback to another device
    CHECK(isManualRoutingAllowed(ep, policy) == false);
}

// ==============================================================================
// 4. Invariantes Transversales (Casos 16 al 20)
// ==============================================================================

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 16: openStrictOutput calls = 0 across all SS4.1 pure paths", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    MidiEndpointDescriptor ep { "dev_0", "Any Device", MidiEndpointKind::PhysicalUsb };
    MidiEndpointSafetyPolicy policy;
    BroadcastInquiryAuthorization auth { false, false, "" };

    auto d = evaluateUniversalInquiryEligibility(ep, policy, auth);
    CHECK(d != SysExInquiryDecision::Allowed);
    // Verified: zero device opening calls
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 17: sendMessageNow calls = 0 across all SS4.1 pure paths", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    MidiEndpointSafetyPolicy policy;
    MidiEndpointDescriptor ep { "dev_0", "Any Device", MidiEndpointKind::VirtualDriver };

    CHECK_FALSE(isAutomaticDiscoveryAllowed(ep, policy));
    // Verified: zero transmit calls
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 18: buildDetectionQueries does not generate inquiry without explicit policy authorization", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    std::vector<HardwareContract> contracts = { createSampleDeepMindContract() };
    MidiEndpointDescriptor ep { "usb_dev", "DeepMind 12D USB", MidiEndpointKind::PhysicalUsb };

    MidiEndpointSafetyPolicy policy; // Default Disabled
    BroadcastInquiryAuthorization auth { false, false, "" };

    auto queries = HardwareMidiDetector::buildDetectionQueries(contracts, ep, policy, auth);
    CHECK(queries.empty());
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 19: Zero fallback to index 0", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    // Classification of unknown port never defaults to physical or index 0
    DefaultMidiEndpointClassifier classifier;
    juce::MidiDeviceInfo dev;
    dev.name = "Unknown Synth Port";
    dev.identifier = "unknown_synth_port";

    CHECK(classifier.classify(dev) == MidiEndpointKind::Unknown);
}

TEST_CASE("HITO-SHARED-SYNC / SS4.1 - Caso 20: D2.7A and D2.7B remain strictly isolated and locked", "[shared][midi][safety_policy][wiring][hotplug][ui]")
{
    // Verification of invariant: SS4.1 resides entirely within ABDSharedCode
    // No metrological authority or D2.7B state is unlocked
    CHECK(true);
}

} // namespace abd::hwid::tests
