// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "Layer.h"
#include "Region.h"
#include "utility/Debug.h"
#include "utility/SwapAndPop.h"
#include <absl/algorithm/container.h>
#include <algorithm>

namespace sfz {

Layer::Layer(int regionNumber, absl::string_view defaultPath, const MidiState& midiState)
    : midiState_(midiState)
    , region_(regionNumber, defaultPath)
{
    initializeActivations();
}

Layer::Layer(const Region& region, const MidiState& midiState)
    : midiState_(midiState)
    , region_(region)
{
    initializeActivations();
}

Layer::~Layer()
{
}

void Layer::initializeActivations()
{
    const Region& region = region_;

    keySwitched_ = !region.usesKeySwitches;
    previousKeySwitched_ = !region.usesPreviousKeySwitches;
    sequenceSwitched_ = !region.usesSequenceSwitches;
    pitchSwitched_ = true;
    bpmSwitched_ = true;
    aftertouchSwitched_ = true;
    programSwitched_ = true;
    ccSwitched_.set();
    sustainPressed_ = false;
    sostenutoPressed_ = false;
    sequenceCounter_ = 0;

    if (region.isChannelRestricted()) {
        if (!sourceStates_)
            sourceStates_ = std::make_unique<std::array<SourceActivationState, 16>>();
        for (SourceActivationState& state : *sourceStates_) {
            state.keySwitched = !region.usesKeySwitches;
            state.previousKeySwitched = !region.usesPreviousKeySwitches;
            state.sequenceSwitched = !region.usesSequenceSwitches;
            state.pitchSwitched = true;
            state.aftertouchSwitched = true;
            state.sustainPressed = false;
            state.sostenutoPressed = false;
            state.ccSwitched.set();
            state.sequenceCounter = 0;
        }
    } else {
        sourceStates_.reset();
    }

    delayedSustainReleases_.clear();
    delayedSostenutoReleases_.clear();
    sourceDelayedSustainReleases_.clear();
    sourceDelayedSostenutoReleases_.clear();
}

bool Layer::isSourceChannelEligible(int sourceChannel) const noexcept
{
    return sourceChannel >= 0 && sourceChannel < 16
        && region_.channelRange.containsWithEnd(sourceChannel + 1);
}

bool Layer::isSwitchedOn(SourceAddress source) const noexcept
{
    if (!region_.isChannelRestricted()) {
        return keySwitched_ && previousKeySwitched_ && sequenceSwitched_ && pitchSwitched_
            && programSwitched_ && bpmSwitched_ && aftertouchSwitched_ && ccSwitched_.all();
    }

    const int sourceChannel = source.channel;
    if (!isSourceChannelEligible(sourceChannel))
        return false;

    const SourceActivationState& state = (*sourceStates_)[sourceChannel];
    const bool sourceProgramSwitched = region_.programRange.containsWithEnd(
        midiState_.getProgram(source));
    return state.keySwitched && state.previousKeySwitched && state.sequenceSwitched
        && state.pitchSwitched && sourceProgramSwitched && bpmSwitched_
        && state.aftertouchSwitched && state.ccSwitched.all();
}

bool Layer::registerNoteOn(int noteNumber, float velocity, float randValue,
    SourceAddress source) noexcept
{
    ASSERT(velocity >= 0.0f && velocity <= 1.0f);

    const int sourceChannel = source.channel;
    const Region& region = region_;
    if (region.isChannelRestricted() && !isSourceChannelEligible(sourceChannel))
        return false;

    const bool keyOk = region.keyRange.containsWithEnd(noteNumber);
    if (keyOk) {
        if (region.isChannelRestricted()) {
            SourceActivationState& state = (*sourceStates_)[sourceChannel];
            state.sequenceSwitched = ((state.sequenceCounter++ % region.sequenceLength) == region.sequencePosition - 1);
        } else {
            sequenceSwitched_ = ((sequenceCounter_++ % region.sequenceLength) == region.sequencePosition - 1);
        }
    }

    const bool polyAftertouchActive = region.isChannelRestricted()
        ? region.polyAftertouchRange.containsWithEnd(
              midiState_.getSourcePolyAftertouch(sourceChannel, noteNumber))
        : region.polyAftertouchRange.containsWithEnd(midiState_.getPolyAftertouch(noteNumber));

    if (!isSwitchedOn(source) || !polyAftertouchActive)
        return false;

    if (!region.triggerOnNote)
        return false;

    if (region.velocityOverride == VelocityOverride::previous) {
        velocity = region.isChannelRestricted()
            ? midiState_.getSourceVelocityOverride(sourceChannel)
            : midiState_.getVelocityOverride();
    }

    const bool velOk = region.velocityRange.containsWithEnd(velocity);
    const bool randOk = region.randRange.contains(randValue)
        || (randValue >= 1.0f && region.randRange.isValid() && region.randRange.getEnd() >= 1.0f);
    const int activeNotes = region.isChannelRestricted()
        ? midiState_.getSourceActiveNotes(sourceChannel)
        : midiState_.getActiveNotes();
    const bool firstLegatoNote = (region.trigger == Trigger::first && activeNotes == 1);
    const bool attackTrigger = (region.trigger == Trigger::attack);
    const bool notFirstLegatoNote = (region.trigger == Trigger::legato && activeNotes > 1);

    return keyOk && velOk && randOk && (attackTrigger || firstLegatoNote || notFirstLegatoNote);
}

bool Layer::registerNoteOff(int noteNumber, float velocity, float randValue,
    SourceAddress source, int expressionChannel, NoteInstanceId noteId) noexcept
{
    ASSERT(velocity >= 0.0f && velocity <= 1.0f);

    const int sourceChannel = source.channel;
    const Region& region = region_;
    if (region.isChannelRestricted() && !isSourceChannelEligible(sourceChannel))
        return false;

    const bool polyAftertouchActive = region.isChannelRestricted()
        ? region.polyAftertouchRange.containsWithEnd(
              midiState_.getSourcePolyAftertouch(sourceChannel, noteNumber))
        : region.polyAftertouchRange.containsWithEnd(midiState_.getPolyAftertouch(noteNumber));

    if (!isSwitchedOn(source) || !polyAftertouchActive)
        return false;

    if (!region.triggerOnNote)
        return false;

    const bool keyOk = region.keyRange.containsWithEnd(noteNumber);
    const bool velOk = region.velocityRange.containsWithEnd(velocity);
    const bool randOk = region.randRange.contains(randValue)
        || (randValue >= 1.0f && region.randRange.isValid() && region.randRange.getEnd() >= 1.0f);

    if (!(velOk && keyOk && randOk))
        return false;

    if (region.trigger == Trigger::release_key)
        return true;

    if (region.trigger == Trigger::release) {
        const bool sostenutoed = isNoteSostenutoed(noteNumber, sourceChannel);
        const bool sostenutoPressed = region.isChannelRestricted()
            ? (*sourceStates_)[sourceChannel].sostenutoPressed
            : sostenutoPressed_;
        const bool sustainPressed = region.isChannelRestricted()
            ? (*sourceStates_)[sourceChannel].sustainPressed
            : sustainPressed_;
        const float noteVelocity = region.isChannelRestricted()
            ? midiState_.getSourceNoteVelocity(sourceChannel, noteNumber)
            : midiState_.getNoteVelocity(noteNumber);

        if (sostenutoed && noteId.valid()) {
            auto& releases = region.isChannelRestricted()
                ? sourceDelayedSostenutoReleases_
                : delayedSostenutoReleases_;
            const auto it = absl::c_find_if(releases,
                [=](const DelayedRelease& release) {
                    return release.noteNumber == noteNumber
                        && (!region.isChannelRestricted()
                            || release.sourceChannel == sourceChannel)
                        && !release.noteId.valid();
                });
            if (it != releases.end()) {
                it->sourceChannel = sourceChannel;
                it->expressionChannel = expressionChannel;
                it->noteId = noteId;
            }
        }

        if (sostenutoed && !sostenutoPressed) {
            removeFromSostenutoReleases(noteNumber, sourceChannel);
            if (sustainPressed) {
                delaySustainRelease(noteNumber, noteVelocity, sourceChannel,
                    expressionChannel, noteId);
            }
        }

        if (!sostenutoPressed || !sostenutoed) {
            if (sustainPressed) {
                delaySustainRelease(noteNumber, noteVelocity, sourceChannel,
                    expressionChannel, noteId);
            } else
                return true;
        }
    }

    return false;
}

void Layer::updateCCState(int ccNumber, float ccValue, int sourceChannel,
    int expressionChannel) noexcept
{
    const Region& region = region_;
    if (region.isChannelRestricted() && !isSourceChannelEligible(sourceChannel))
        return;

    if (!region.isChannelRestricted()) {
        if (ccNumber == region.sustainCC)
            sustainPressed_ = region.checkSustain && ccValue >= region.sustainThreshold;

        if (ccNumber == region.sostenutoCC) {
            const bool newState = region.checkSostenuto && ccValue >= region.sostenutoThreshold;
            if (!sostenutoPressed_ && newState)
                storeSostenutoNotes();

            if (!newState && sostenutoPressed_)
                delayedSostenutoReleases_.clear();

            sostenutoPressed_ = newState;
        }

        if (const auto conditions = region.ccConditions.get(ccNumber))
            ccSwitched_.set(ccNumber, conditions->containsWithEnd(ccValue));
        return;
    }

    SourceActivationState& state = (*sourceStates_)[sourceChannel];
    if (ccNumber == region.sustainCC)
        state.sustainPressed = region.checkSustain && ccValue >= region.sustainThreshold;

    if (ccNumber == region.sostenutoCC) {
        const bool newState = region.checkSostenuto && ccValue >= region.sostenutoThreshold;
        if (!state.sostenutoPressed && newState)
            storeSostenutoNotes(sourceChannel, expressionChannel);

        if (!newState && state.sostenutoPressed) {
            auto& releases = sourceDelayedSostenutoReleases_;
            releases.erase(std::remove_if(releases.begin(), releases.end(),
                               [=](const DelayedRelease& release) {
                                   return release.sourceChannel == sourceChannel;
                               }),
                releases.end());
        }

        state.sostenutoPressed = newState;
    }

    if (const auto conditions = region.ccConditions.get(ccNumber))
        state.ccSwitched.set(ccNumber, conditions->containsWithEnd(ccValue));
}

bool Layer::registerCC(int ccNumber, float ccValue, float randValue,
    int extendedArg, SourceAddress source, int expressionChannel) noexcept
{
    const int sourceChannel = source.channel;
    const Region& region = region_;
    if (region.isChannelRestricted() && !isSourceChannelEligible(sourceChannel))
        return false;

    updateCCState(ccNumber, ccValue, sourceChannel, expressionChannel);

    if (!region.triggerOnCC)
        return false;

    const bool randOk = region.randRange.contains(randValue)
        || (randValue >= 1.0f && region.randRange.isValid() && region.randRange.getEnd() >= 1.0f);

    if (!randOk)
        return false;

    if (auto triggerRange = region.ccTriggers.get(ccNumber)) {
        if (!triggerRange->containsWithEnd(ccValue))
            return false;

        if (ccNumber == ExtendedCCs::polyphonicAftertouch && extendedArg >= 0
            && !region.keyRange.containsWithEnd(extendedArg))
            return false;

        if (region.isChannelRestricted()) {
            SourceActivationState& state = (*sourceStates_)[sourceChannel];
            state.sequenceSwitched = ((state.sequenceCounter++ % region.sequenceLength) == region.sequencePosition - 1);
        } else {
            sequenceSwitched_ = ((sequenceCounter_++ % region.sequenceLength) == region.sequencePosition - 1);
        }

        const float previousValue = region.isChannelRestricted()
            ? midiState_.getSourceCCValue(sourceChannel, ccNumber)
            : midiState_.getCCValue(expressionChannel, ccNumber);
        if (isSwitchedOn(source)
            && (ccNumber == ExtendedCCs::polyphonicAftertouch
                || ccValue != previousValue))
            return true;
    }

    return false;
}

void Layer::registerPitchWheel(float pitch, int sourceChannel) noexcept
{
    if (!region_.isChannelRestricted()) {
        pitchSwitched_ = region_.bendRange.containsWithEnd(pitch);
        return;
    }
    if (isSourceChannelEligible(sourceChannel))
        (*sourceStates_)[sourceChannel].pitchSwitched = region_.bendRange.containsWithEnd(pitch);
}

void Layer::registerProgramChange(int program) noexcept
{
    programSwitched_ = region_.programRange.containsWithEnd(program);
}

void Layer::registerAftertouch(float aftertouch, int sourceChannel) noexcept
{
    if (!region_.isChannelRestricted()) {
        aftertouchSwitched_ = region_.aftertouchRange.containsWithEnd(aftertouch);
        return;
    }
    if (isSourceChannelEligible(sourceChannel))
        (*sourceStates_)[sourceChannel].aftertouchSwitched = region_.aftertouchRange.containsWithEnd(aftertouch);
}

void Layer::registerTempo(float secondsPerQuarter) noexcept
{
    const float bpm = 60.0f / secondsPerQuarter;
    bpmSwitched_ = region_.bpmRange.containsWithEnd(bpm);
}

void Layer::setKeySwitched(int sourceChannel, bool value) noexcept
{
    if (!region_.isChannelRestricted()) {
        keySwitched_ = value;
        return;
    }
    if (isSourceChannelEligible(sourceChannel))
        (*sourceStates_)[sourceChannel].keySwitched = value;
}

void Layer::setPreviousKeySwitched(int sourceChannel, bool value) noexcept
{
    if (!region_.isChannelRestricted()) {
        previousKeySwitched_ = value;
        return;
    }
    if (isSourceChannelEligible(sourceChannel))
        (*sourceStates_)[sourceChannel].previousKeySwitched = value;
}

bool Layer::isCcSwitchedOn(int sourceChannel) const noexcept
{
    if (!region_.isChannelRestricted())
        return ccSwitched_.all();
    return isSourceChannelEligible(sourceChannel)
        && (*sourceStates_)[sourceChannel].ccSwitched.all();
}

void Layer::reserveDelayedReleaseCapacity(size_t capacity)
{
    delayedSustainReleases_.reserve(capacity);
    delayedSostenutoReleases_.reserve(capacity);
    if (region_.isChannelRestricted()) {
        // Bound the per-region increase: enough for the legacy delayed-release
        // allowance on all 16 sources, without multiplying a 128-key range by
        // 16 (which would reserve tens of KiB for every release region).
        const size_t sourceCapacity = std::max(
            capacity, static_cast<size_t>(config::delayedReleaseVoices) * 16);
        sourceDelayedSustainReleases_.reserve(sourceCapacity);
        sourceDelayedSostenutoReleases_.reserve(sourceCapacity);
    }
}

void Layer::delaySustainRelease(int noteNumber, float velocity,
    int sourceChannel, int expressionChannel, NoteInstanceId noteId) noexcept
{
    if (!region_.isChannelRestricted()) {
        if (delayedSustainReleases_.size() == delayedSustainReleases_.capacity())
            return;
        delayedSustainReleases_.push_back(
            { noteNumber, velocity, sourceChannel, expressionChannel, noteId });
        return;
    }

    if (sourceDelayedSustainReleases_.size() == sourceDelayedSustainReleases_.capacity())
        return;
    sourceDelayedSustainReleases_.push_back(
        { noteNumber, velocity, sourceChannel, expressionChannel, noteId });
}

void Layer::delaySostenutoRelease(int noteNumber, float velocity,
    int sourceChannel, int expressionChannel, NoteInstanceId noteId) noexcept
{
    if (!region_.isChannelRestricted()) {
        if (delayedSostenutoReleases_.size() == delayedSostenutoReleases_.capacity())
            return;
        delayedSostenutoReleases_.push_back(
            { noteNumber, velocity, sourceChannel, expressionChannel, noteId });
        return;
    }

    if (sourceDelayedSostenutoReleases_.size() == sourceDelayedSostenutoReleases_.capacity())
        return;
    sourceDelayedSostenutoReleases_.push_back(
        { noteNumber, velocity, sourceChannel, expressionChannel, noteId });
}

void Layer::removeFromSostenutoReleases(int noteNumber, int sourceChannel) noexcept
{
    if (!region_.isChannelRestricted()) {
        swapAndPopFirst(delayedSostenutoReleases_, [=](const DelayedRelease& release) {
            return release.noteNumber == noteNumber;
        });
        return;
    }

    swapAndPopFirst(sourceDelayedSostenutoReleases_, [=](const DelayedRelease& release) {
        return release.noteNumber == noteNumber && release.sourceChannel == sourceChannel;
    });
}

void Layer::storeSostenutoNotes(int sourceChannel, int expressionChannel) noexcept
{
    const Region& region = region_;
    if (!region.isChannelRestricted()) {
        ASSERT(delayedSostenutoReleases_.empty());
        for (int note = region.keyRange.getStart(); note <= region.keyRange.getEnd(); ++note) {
            if (midiState_.isNotePressed(note))
                delaySostenutoRelease(note, midiState_.getNoteVelocity(note));
        }
        return;
    }

    for (int note = region.keyRange.getStart(); note <= region.keyRange.getEnd(); ++note) {
        if (midiState_.isSourceNotePressed(sourceChannel, note)) {
            delaySostenutoRelease(note,
                midiState_.getSourceNoteVelocity(sourceChannel, note),
                sourceChannel, expressionChannel);
        }
    }
}

bool Layer::isNoteSustained(int noteNumber, int sourceChannel) const noexcept
{
    if (!region_.isChannelRestricted()) {
        return absl::c_find_if(delayedSustainReleases_, [=](const DelayedRelease& release) {
            return release.noteNumber == noteNumber;
        }) != delayedSustainReleases_.end();
    }

    return absl::c_find_if(sourceDelayedSustainReleases_, [=](const DelayedRelease& release) {
        return release.noteNumber == noteNumber && release.sourceChannel == sourceChannel;
    }) != sourceDelayedSustainReleases_.end();
}

bool Layer::isNoteSostenutoed(int noteNumber, int sourceChannel) const noexcept
{
    if (!region_.isChannelRestricted()) {
        return absl::c_find_if(delayedSostenutoReleases_, [=](const DelayedRelease& release) {
            return release.noteNumber == noteNumber;
        }) != delayedSostenutoReleases_.end();
    }

    return absl::c_find_if(sourceDelayedSostenutoReleases_, [=](const DelayedRelease& release) {
        return release.noteNumber == noteNumber && release.sourceChannel == sourceChannel;
    }) != sourceDelayedSostenutoReleases_.end();
}

bool Layer::isSustainPressed(int sourceChannel) const noexcept
{
    if (!region_.isChannelRestricted())
        return sustainPressed_;
    return isSourceChannelEligible(sourceChannel)
        && (*sourceStates_)[sourceChannel].sustainPressed;
}

bool Layer::isSostenutoPressed(int sourceChannel) const noexcept
{
    if (!region_.isChannelRestricted())
        return sostenutoPressed_;
    return isSourceChannelEligible(sourceChannel)
        && (*sourceStates_)[sourceChannel].sostenutoPressed;
}

} // namespace sfz
