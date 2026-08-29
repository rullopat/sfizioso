// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "ExpressionEvent.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace sfz {

/**
 * @brief Result of resolving a MIDI 1.0 expression message.
 *
 * Member-channel MPE expression is broadcast to every active logical note on
 * the source channel. The channel target retains compact profile state and
 * supports compatibility diagnostics, but voices consume the Note targets.
 */
struct MidiExpressionRoute {
    ResolvedExpressionEvent event;
    bool broadcastToActiveNotes { false };
};

/**
 * @brief MIDI 1.0 transport and Lower-Zone MPE profile resolver.
 *
 * This object owns all MPE-specific policy: Manager/Member classification,
 * filters, RPN/MCM parsing, pitch-range conversion and pre-note standardized
 * member expression. It performs no allocation and knows nothing about
 * voices or SFZ regions.
 */
class MidiInputAdapter {
public:
    MidiExpressionRoute resolvePitch(
        int delay, SourceAddress source, float normalizedValue) noexcept;
    MidiExpressionRoute resolvePressure(
        int delay, SourceAddress source, float normalizedValue) noexcept;
    MidiExpressionRoute resolveControl(
        int delay, SourceAddress source, int ccNumber, float normalizedValue) noexcept;
    MidiExpressionRoute resolvePolyPressure(int delay, SourceAddress source,
        int noteNumber, float normalizedValue) noexcept;

    ExpressionTarget noteBroadTarget(SourceAddress source) const noexcept;

    /** Resolve accepted Program Change to routing state, never expression. */
    bool acceptProgramChange(SourceAddress source) const noexcept;
    RoutingTarget programTarget(SourceAddress source) const noexcept;

    bool acceptControl(SourceAddress source, int ccNumber) noexcept;
    bool acceptPolyPressure(SourceAddress source) noexcept;
    void observeRpnControl(
        SourceAddress source, int ccNumber, float normalizedValue) noexcept;

    void setMpeEnabled(bool enabled) noexcept { mpeEnabled_ = enabled; }
    bool mpeEnabled() const noexcept { return mpeEnabled_; }

    void setPitchBendRange(float managerSemitones, float memberSemitones) noexcept;
    float managerPitchBendRange() const noexcept { return managerPitchBendRange_; }
    float memberPitchBendRange() const noexcept { return memberPitchBendRange_; }

    void setManagerBendAutoConfigEnabled(bool enabled) noexcept
    {
        managerBendAutoConfigEnabled_ = enabled;
    }
    bool managerBendAutoConfigEnabled() const noexcept
    {
        return managerBendAutoConfigEnabled_;
    }
    void setMemberBendAutoConfigEnabled(bool enabled) noexcept
    {
        memberBendAutoConfigEnabled_ = enabled;
    }
    bool memberBendAutoConfigEnabled() const noexcept
    {
        return memberBendAutoConfigEnabled_;
    }

    int droppedPolyPressureCount() const noexcept { return droppedPolyPressure_; }
    int droppedManagerOnlyControlCount() const noexcept
    {
        return droppedManagerOnlyControls_;
    }

    struct MemberSeed {
        float pitchSemitones { 0.0f };
        float normalizedPitch { 0.0f };
        float pressure { 0.0f };
        float timbre { 0.0f };
        bool hasPitch { false };
        bool hasPressure { false };
        bool hasTimbre { false };
    };
    MemberSeed memberSeed(SourceAddress source) const noexcept;
    bool currentPitch(SourceAddress source, float& normalizedValue) const noexcept;
    void resetExpressionState() noexcept;

    static bool isManagerOnlyControl(int ccNumber) noexcept;

private:
    bool isMember(SourceAddress source) const noexcept
    {
        return mpeEnabled_ && source.group == 0 && source.channel > 0
            && source.channel < memberSeeds_.size();
    }

    ExpressionTarget profileTarget(SourceAddress source) const noexcept;

    struct RpnParserState {
        static constexpr uint16_t nullRpn = 0x3fff;
        uint16_t selectedRpn { nullRpn };
        bool nrpnMode { false };
    };

    std::array<RpnParserState, 16> rpnParsers_ { };
    std::array<MemberSeed, 16> memberSeeds_ { };
    bool mpeEnabled_ { false };
    float managerPitchBendRange_ { 2.0f };
    float memberPitchBendRange_ { 48.0f };
    bool managerBendAutoConfigEnabled_ { true };
    bool memberBendAutoConfigEnabled_ { true };
    int droppedPolyPressure_ { 0 };
    int droppedManagerOnlyControls_ { 0 };
};

} // namespace sfz
