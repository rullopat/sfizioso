// SPDX-License-Identifier: BSD-2-Clause

#include "MidiInputAdapter.h"
#include "Config.h"
#include <algorithm>
#include <cmath>

namespace sfz {

ExpressionTarget MidiInputAdapter::profileTarget(SourceAddress source) const noexcept
{
    if (!mpeEnabled_)
        return ExpressionTarget::global();
    if (source.channel == 0)
        return ExpressionTarget::zone(0);
    return ExpressionTarget::channel(source);
}

ExpressionTarget MidiInputAdapter::noteBroadTarget(SourceAddress source) const noexcept
{
    return mpeEnabled_ && source.group == 0
        ? ExpressionTarget::zone(0)
        : ExpressionTarget::global();
}

MidiExpressionRoute MidiInputAdapter::resolvePitch(
    int delay, SourceAddress source, float normalizedValue) noexcept
{
    MidiExpressionRoute route;
    route.event.delay = delay;
    route.event.target = profileTarget(source);
    route.event.kind = mpeEnabled_
        ? ExpressionEventKind::Pitch
        : ExpressionEventKind::LegacyPitch;
    route.event.value = normalizedValue;

    if (mpeEnabled_) {
        const float range = source.channel == 0
            ? managerPitchBendRange_
            : memberPitchBendRange_;
        route.event.value *= range;
        route.broadcastToActiveNotes = isMember(source);
        if (source.group == 0 && source.channel < memberSeeds_.size()) {
            MemberSeed& seed = memberSeeds_[source.channel];
            seed.pitchSemitones = route.event.value;
            seed.normalizedPitch = normalizedValue;
            seed.hasPitch = true;
        }
    }
    return route;
}

MidiExpressionRoute MidiInputAdapter::resolvePressure(
    int delay, SourceAddress source, float normalizedValue) noexcept
{
    MidiExpressionRoute route;
    route.event = { profileTarget(source), ExpressionEventKind::Pressure,
        { }, delay, -1, normalizedValue };
    route.broadcastToActiveNotes = isMember(source);
    if (route.broadcastToActiveNotes) {
        MemberSeed& seed = memberSeeds_[source.channel];
        seed.pressure = normalizedValue;
        seed.hasPressure = true;
    }
    return route;
}

MidiExpressionRoute MidiInputAdapter::resolveControl(
    int delay, SourceAddress source, int ccNumber, float normalizedValue) noexcept
{
    MidiExpressionRoute route;
    route.event = { profileTarget(source), ExpressionEventKind::Control,
        ExpressionControlId::fromSfizzCC(ccNumber), delay, -1,
        normalizedValue };
    route.broadcastToActiveNotes = isMember(source);
    if (route.broadcastToActiveNotes && ccNumber == 74) {
        MemberSeed& seed = memberSeeds_[source.channel];
        seed.timbre = normalizedValue;
        seed.hasTimbre = true;
    }
    return route;
}

MidiExpressionRoute MidiInputAdapter::resolvePolyPressure(int delay,
    SourceAddress source, int noteNumber, float normalizedValue) noexcept
{
    MidiExpressionRoute route;
    route.event = { profileTarget(source), ExpressionEventKind::PolyPressure,
        { }, delay, noteNumber, normalizedValue };
    return route;
}

bool MidiInputAdapter::isManagerOnlyControl(int ccNumber) noexcept
{
    switch (ccNumber) {
    case 0:
    case 32:
    case 64:
    case 65:
    case 66:
    case 67:
    case 68:
    case 69:
    case 120:
    case 121:
    case 123:
    case 124:
    case 125:
        return true;
    default:
        return false;
    }
}

bool MidiInputAdapter::acceptControl(SourceAddress source, int ccNumber) noexcept
{
    if (isMember(source) && isManagerOnlyControl(ccNumber)) {
        ++droppedManagerOnlyControls_;
        return false;
    }
    return true;
}

bool MidiInputAdapter::acceptPolyPressure(SourceAddress source) noexcept
{
    if (isMember(source)) {
        ++droppedPolyPressure_;
        return false;
    }
    return true;
}

void MidiInputAdapter::observeRpnControl(
    SourceAddress source, int ccNumber, float normalizedValue) noexcept
{
    if (source.group != 0 || source.channel >= rpnParsers_.size())
        return;

    RpnParserState& state = rpnParsers_[source.channel];
    const int data7 = static_cast<int>(std::lround(
        std::min(std::max(normalizedValue, 0.0f), 1.0f) * 127.0f));

    switch (ccNumber) {
    case 99:
        state.selectedRpn = static_cast<uint16_t>(
            (state.selectedRpn & 0x007f) | ((data7 & 0x7f) << 7));
        state.nrpnMode = true;
        return;
    case 98:
        state.selectedRpn = static_cast<uint16_t>(
            (state.selectedRpn & 0x3f80) | (data7 & 0x7f));
        state.nrpnMode = true;
        return;
    case 101:
        state.selectedRpn = static_cast<uint16_t>(
            (state.selectedRpn & 0x007f) | ((data7 & 0x7f) << 7));
        state.nrpnMode = false;
        return;
    case 100:
        state.selectedRpn = static_cast<uint16_t>(
            (state.selectedRpn & 0x3f80) | (data7 & 0x7f));
        state.nrpnMode = false;
        return;
    case 6:
        if (state.nrpnMode || state.selectedRpn == RpnParserState::nullRpn)
            return;
        if (state.selectedRpn == 6) {
            if (source.channel == 0)
                mpeEnabled_ = data7 >= 1 && data7 <= 15;
        } else if (state.selectedRpn == 0) {
            if (source.channel == 0) {
                if (managerBendAutoConfigEnabled_)
                    managerPitchBendRange_ = static_cast<float>(data7);
            } else if (memberBendAutoConfigEnabled_) {
                memberPitchBendRange_ = static_cast<float>(data7);
            }
        }
        return;
    default:
        return;
    }
}

void MidiInputAdapter::setPitchBendRange(
    float managerSemitones, float memberSemitones) noexcept
{
    managerPitchBendRange_ = managerSemitones;
    memberPitchBendRange_ = memberSemitones;
    for (size_t channel = 0; channel < memberSeeds_.size(); ++channel) {
        MemberSeed& seed = memberSeeds_[channel];
        if (!seed.hasPitch)
            continue;
        const float range = channel == 0
            ? managerPitchBendRange_ : memberPitchBendRange_;
        seed.pitchSemitones = seed.normalizedPitch * range;
    }
}

void MidiInputAdapter::resetExpressionState() noexcept
{
    memberSeeds_.fill({ });
    rpnParsers_.fill({ });
}

bool MidiInputAdapter::currentPitch(
    SourceAddress source, float& normalizedValue) const noexcept
{
    if (source.group != 0 || source.channel >= memberSeeds_.size())
        return false;
    const MemberSeed& seed = memberSeeds_[source.channel];
    if (!seed.hasPitch)
        return false;
    normalizedValue = seed.normalizedPitch;
    return true;
}

MidiInputAdapter::MemberSeed MidiInputAdapter::memberSeed(
    SourceAddress source) const noexcept
{
    if (source.group != 0 || source.channel == 0
        || source.channel >= memberSeeds_.size())
        return { };
    return memberSeeds_[source.channel];
}

} // namespace sfz
