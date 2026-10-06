/**
 * @file MidiEndpointSafetyPolicy.h
 * @brief Safety policy, endpoint classification, and inquiry eligibility governance for ABDSharedCode.
 * @details Implements the SS4/SS5 axioms:
 *          "Un puerto que puede seleccionarse manualmente no queda por ello autorizado
 *           para recibir interrogación automática; el discovery automático debe ser
 *           más restrictivo que el routing manual."
 *          "Un endpoint compartido puede ser elegible para discovery, pero no queda por ello
 *           autorizado para una sesión metrológica, para un despacho físico ni para una exportación."
 * @author ABDSynths
 * @date 2026
 */

#pragma once

#include "HardwareContract.h"
#include <array>
#include <cstdint>
#include <juce_audio_devices/juce_audio_devices.h>
#include <optional>
#include <string>

namespace abd::hwid
{

/**
 * @enum MidiEndpointKind
 * @brief Physical vs virtual vs unknown classification of a MIDI endpoint.
 */
enum class MidiEndpointKind
{
    PhysicalUsb = 0,
    PhysicalDinInterface,
    VirtualLoopback,
    VirtualDriver,
    Unknown
};

/**
 * @enum SysExDiscoveryInquiryMode
 * @brief Explicit operational mode for Universal Device Inquiry during discovery.
 */
enum class SysExDiscoveryInquiryMode
{
    Disabled = 0,
    ManualExplicitOnly,
    PhysicalEndpointsWithExplicitOptIn
};

/**
 * @struct MidiEndpointSafetyPolicy
 * @brief Policy rules governing virtual ports, SysEx broadcast, and discovery.
 */
struct MidiEndpointSafetyPolicy
{
    /** Allow virtual endpoints for explicit user/manual routing. */
    bool allowVirtualEndpointsForManualRouting{true};

    /** Allow virtual endpoints in automatic scanning / discovery sweeps. */
    bool allowVirtualEndpointsForAutomaticDiscovery{false};

    /** Global gate for broadcast SysEx transmission. */
    bool allowBroadcastSysEx{false};

    /** Operational mode for SysEx inquiries. */
    SysExDiscoveryInquiryMode sysExInquiryMode{SysExDiscoveryInquiryMode::Disabled};

    /** Requires affirmative single-target topology confirmation from caller. */
    bool requireSingleTargetTopologyForBroadcast{true};
};

/**
 * @struct BroadcastInquiryAuthorization
 * @brief Caller-provided explicit affirmation for broadcast inquiry eligibility.
 * ABDSharedCode cannot verify external cabling or daisy-chains; the host application
 * must explicitly certify topology and opt-in.
 */
struct BroadcastInquiryAuthorization
{
    bool callerExplicitlyOptedIn{false};
    bool callerConfirmedSingleTargetTopology{false};
    std::string callerContext;
};

/**
 * @enum SysExInquiryDecision
 * @brief Typed result of evaluating whether an endpoint is eligible for Universal Inquiry.
 */
enum class SysExInquiryDecision
{
    Allowed = 0,
    DisabledByPolicy,
    VirtualEndpointExcluded,
    UnknownEndpointExcluded,
    SingleTargetTopologyUnconfirmed,
    CallerOptInMissing
};

/**
 * @struct MidiEndpointDescriptor
 * @brief Classified descriptor of a MIDI endpoint.
 */
struct MidiEndpointDescriptor
{
    std::string stableDeviceId;
    std::string displayName;
    MidiEndpointKind kind{MidiEndpointKind::Unknown};
};

struct UniversalInquiryEligibilityResult;

/**
 * @class UniversalInquiryRequest
 * @brief Strong non-executable intention to issue a Universal Identity Inquiry.
 * INVARIANT: Has NO public constructor. Can ONLY be instantiated when
 * evaluateUniversalInquiryEligibilityDetailed yields SysExInquiryDecision::Allowed.
 */
class UniversalInquiryRequest
{
public:
    [[nodiscard]] const MidiEndpointDescriptor& endpoint() const noexcept { return endpoint_; }
    [[nodiscard]] const std::array<uint8_t, 6>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] const MidiEndpointSafetyPolicy& policySnapshot() const noexcept { return policySnapshot_; }
    [[nodiscard]] const BroadcastInquiryAuthorization& authorizationSnapshot() const noexcept { return authorizationSnapshot_; }

private:
    friend UniversalInquiryEligibilityResult evaluateUniversalInquiryEligibilityDetailed(
        const MidiEndpointDescriptor& endpoint,
        const MidiEndpointSafetyPolicy& policy,
        const BroadcastInquiryAuthorization& authorization) noexcept;

    UniversalInquiryRequest(
        MidiEndpointDescriptor endpoint,
        MidiEndpointSafetyPolicy policy,
        BroadcastInquiryAuthorization authorization)
        : endpoint_(std::move(endpoint)),
          policySnapshot_(policy),
          authorizationSnapshot_(std::move(authorization))
    {
    }

    MidiEndpointDescriptor endpoint_;
    std::array<uint8_t, 6> bytes_{0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    MidiEndpointSafetyPolicy policySnapshot_;
    BroadcastInquiryAuthorization authorizationSnapshot_;
};

/**
 * @struct UniversalInquiryEligibilityResult
 * @brief Invariant-enforced result: request has value IF AND ONLY IF decision == Allowed.
 */
struct UniversalInquiryEligibilityResult
{
    SysExInquiryDecision decision{SysExInquiryDecision::DisabledByPolicy};
    std::optional<UniversalInquiryRequest> request;

    [[nodiscard]] bool isAllowed() const noexcept { return decision == SysExInquiryDecision::Allowed && request.has_value(); }
};

/**
 * @struct SharedMidiEndpointSnapshot
 * @brief Read-only boundary snapshot consumed by ABDAudioLab from ABDSharedCode.
 */
struct SharedMidiEndpointSnapshot
{
    std::string stableDeviceId;
    std::string displayName;
    MidiEndpointKind endpointKind{MidiEndpointKind::Unknown};
    HardwareMidiIdentityState identityState{HardwareMidiIdentityState::PortAvailable};
    bool isSysExVerified{false};
    bool eligibleForAutomaticDiscovery{false};
    SysExInquiryDecision inquiryDecision{SysExInquiryDecision::DisabledByPolicy};
    std::string diagnosticCode;
    std::string diagnosticMessage;
};

/**
 * @class IMidiEndpointClassifier
 * @brief Abstract interface for classifying endpoints without hardcoding a single heuristic.
 */
class IMidiEndpointClassifier
{
public:
    virtual ~IMidiEndpointClassifier()                                                                            = default;
    [[nodiscard]] virtual MidiEndpointKind classify(const juce::MidiDeviceInfo& device) const                     = 0;
    [[nodiscard]] virtual MidiEndpointKind classify(const std::string& identifier, const std::string& name) const = 0;
};

/**
 * @class DefaultMidiEndpointClassifier
 * @brief Conservative default classifier for MIDI hardware and virtual drivers.
 */
class DefaultMidiEndpointClassifier final : public IMidiEndpointClassifier
{
public:
    [[nodiscard]] MidiEndpointKind classify(const juce::MidiDeviceInfo& device) const override;
    [[nodiscard]] MidiEndpointKind classify(const std::string& identifier, const std::string& name) const override;
};

/**
 * @brief Detailed pure evaluation returning decision and optional non-executable request.
 * Invariant: request.has_value() == (decision == Allowed).
 */
[[nodiscard]] UniversalInquiryEligibilityResult evaluateUniversalInquiryEligibilityDetailed(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy,
    const BroadcastInquiryAuthorization& authorization) noexcept;

/**
 * @brief Pure evaluation of inquiry eligibility decision.
 * ZERO side-effects: does NOT open devices, does NOT send messages, does NOT allocate audio buffers.
 */
[[nodiscard]] SysExInquiryDecision evaluateUniversalInquiryEligibility(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy,
    const BroadcastInquiryAuthorization& authorization) noexcept;

/**
 * @brief Pure check if an endpoint is eligible for manual routing under policy.
 */
[[nodiscard]] bool isManualRoutingAllowed(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy) noexcept;

/**
 * @brief Pure check if an endpoint is eligible for automatic discovery sweeps under policy.
 */
[[nodiscard]] bool isAutomaticDiscoveryAllowed(
    const MidiEndpointDescriptor& endpoint,
    const MidiEndpointSafetyPolicy& policy) noexcept;

/**
 * @brief Human-readable classification badge for UI display without hiding endpoints.
 */
[[nodiscard]] std::string getEndpointKindLabel(MidiEndpointKind kind) noexcept;

} // namespace abd::hwid
