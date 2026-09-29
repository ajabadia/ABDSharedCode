/**
 * @file MidiEndpointSafetyPolicy.cpp
 * @brief Implementation of safety policy and conservative endpoint classifier for ABDSharedCode.
 * @author ABDSynths
 * @date 2026
 */

#include "MidiEndpointSafetyPolicy.h"
#include <algorithm>

namespace abd::hwid
{

static bool containsIgnoreCase(const std::string& text, const std::string& substring)
{
    if (substring.empty()) return false;
    auto it = std::search(
        text.begin(), text.end(),
        substring.begin(), substring.end(),
        [](char ch1, char ch2) {
            return std::tolower(static_cast<unsigned char>(ch1)) ==
                   std::tolower(static_cast<unsigned char>(ch2));
        }
    );
    return it != text.end();
}

MidiEndpointKind DefaultMidiEndpointClassifier::classify(const juce::MidiDeviceInfo& device) const
{
    return classify(device.identifier.toStdString(), device.name.toStdString());
}

MidiEndpointKind DefaultMidiEndpointClassifier::classify(const std::string& identifier, const std::string& name) const
{
    const std::string combined = identifier + " " + name;

    // 1. Virtual Loopback check (known virtual loopback cables)
    if (containsIgnoreCase(combined, "LoopBe") ||
        containsIgnoreCase(combined, "Midi Yoke") ||
        containsIgnoreCase(combined, "LoopBe1") ||
        containsIgnoreCase(combined, "LoopBe30"))
    {
        return MidiEndpointKind::VirtualLoopback;
    }

    // 2. Virtual Driver check (known virtual software drivers & network ports)
    if (containsIgnoreCase(combined, "loopMIDI") ||
        containsIgnoreCase(combined, "teVirtualMIDI") ||
        containsIgnoreCase(combined, "springbeats") ||
        containsIgnoreCase(combined, "rtpMIDI") ||
        containsIgnoreCase(combined, "MIDI Through") ||
        containsIgnoreCase(combined, "MIDI Thru") ||
        containsIgnoreCase(combined, "Virtual MIDI") ||
        containsIgnoreCase(combined, "IAC Driver") ||
        containsIgnoreCase(combined, "Network Session"))
    {
        return MidiEndpointKind::VirtualDriver;
    }

    // 3. Known DIN Interfaces (USB-to-DIN cables, audio interfaces with DIN)
    if (containsIgnoreCase(combined, "UM-ONE") ||
        containsIgnoreCase(combined, "MidiSport") ||
        containsIgnoreCase(combined, "DIN Interface") ||
        containsIgnoreCase(combined, "DIN Port") ||
        containsIgnoreCase(combined, "Scarlett") ||
        containsIgnoreCase(combined, "AudioBox") ||
        containsIgnoreCase(combined, "Fast Track") ||
        containsIgnoreCase(combined, "Komplete Audio"))
    {
        return MidiEndpointKind::PhysicalDinInterface;
    }

    // 4. Physical USB Synths / USB MIDI Ports
    if (containsIgnoreCase(combined, "USB") ||
        containsIgnoreCase(combined, "DeepMind") ||
        containsIgnoreCase(combined, "PRO-800") ||
        containsIgnoreCase(combined, "Boutique") ||
        containsIgnoreCase(combined, "Minilogue") ||
        containsIgnoreCase(combined, "Hydrasynth") ||
        containsIgnoreCase(combined, "Blofeld"))
    {
        return MidiEndpointKind::PhysicalUsb;
    }

    // Conservative default: Unknown. NEVER default to PhysicalUsb!
    return MidiEndpointKind::Unknown;
}

UniversalInquiryEligibilityResult evaluateUniversalInquiryEligibilityDetailed(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy,
    const BroadcastInquiryAuthorization& authorization) noexcept
{
    UniversalInquiryEligibilityResult result;

    // Global gate: disabled by policy
    if (!policy.allowBroadcastSysEx || policy.sysExInquiryMode == SysExDiscoveryInquiryMode::Disabled)
    {
        result.decision = SysExInquiryDecision::DisabledByPolicy;
        result.request = std::nullopt;
        return result;
    }

    // Virtual endpoints are strictly excluded from broadcast inquiries
    if (endpoint.kind == MidiEndpointKind::VirtualLoopback ||
        endpoint.kind == MidiEndpointKind::VirtualDriver)
    {
        result.decision = SysExInquiryDecision::VirtualEndpointExcluded;
        result.request = std::nullopt;
        return result;
    }

    // Unknown endpoints cannot receive broadcast inquiries (conservative fail-closed)
    if (endpoint.kind == MidiEndpointKind::Unknown)
    {
        result.decision = SysExInquiryDecision::UnknownEndpointExcluded;
        result.request = std::nullopt;
        return result;
    }

    // Single-target topology requirement
    if (policy.requireSingleTargetTopologyForBroadcast &&
        !authorization.callerConfirmedSingleTargetTopology)
    {
        result.decision = SysExInquiryDecision::SingleTargetTopologyUnconfirmed;
        result.request = std::nullopt;
        return result;
    }

    // Caller explicit opt-in requirement
    if (policy.sysExInquiryMode == SysExDiscoveryInquiryMode::PhysicalEndpointsWithExplicitOptIn ||
        policy.sysExInquiryMode == SysExDiscoveryInquiryMode::ManualExplicitOnly)
    {
        if (!authorization.callerExplicitlyOptedIn)
        {
            result.decision = SysExInquiryDecision::CallerOptInMissing;
            result.request = std::nullopt;
            return result;
        }
    }

    // All safety preconditions satisfied on physical endpoint: build strong non-executable request
    result.decision = SysExInquiryDecision::Allowed;
    result.request.emplace(UniversalInquiryRequest(endpoint, policy, authorization));
    return result;
}

SysExInquiryDecision evaluateUniversalInquiryEligibility(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy,
    const BroadcastInquiryAuthorization& authorization) noexcept
{
    return evaluateUniversalInquiryEligibilityDetailed(endpoint, policy, authorization).decision;
}

bool isManualRoutingAllowed(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy) noexcept
{
    if ((endpoint.kind == MidiEndpointKind::VirtualLoopback ||
         endpoint.kind == MidiEndpointKind::VirtualDriver) &&
        !policy.allowVirtualEndpointsForManualRouting)
    {
        return false;
    }
    return true;
}

bool isAutomaticDiscoveryAllowed(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy) noexcept
{
    if ((endpoint.kind == MidiEndpointKind::VirtualLoopback ||
         endpoint.kind == MidiEndpointKind::VirtualDriver) &&
        !policy.allowVirtualEndpointsForAutomaticDiscovery)
    {
        return false;
    }
    return true;
}

std::string getEndpointKindLabel(MidiEndpointKind kind) noexcept
{
    switch (kind)
    {
        case MidiEndpointKind::PhysicalUsb:          return "[USB]";
        case MidiEndpointKind::PhysicalDinInterface: return "[DIN]";
        case MidiEndpointKind::VirtualLoopback:      return "[Virtual]";
        case MidiEndpointKind::VirtualDriver:        return "[Virtual]";
        case MidiEndpointKind::Unknown:              return "[Unknown]";
    }
    return "[Unknown]";
}

} // namespace abd::hwid
