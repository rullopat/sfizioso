// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "MidiState.h"
#include "utility/Macros.h"
#include "utility/Debug.h"
#include <algorithm>
#include <limits>

sfz::MidiState::MidiState()
{
    const size_t eventCapacity = static_cast<size_t>(samplesPerBlock) + 1;
    globalExpressionContext_.configure(
        config::numCCs, 128, eventCapacity);
    lowerZoneExpressionContext_.configure(
        compatibilityControllerSlots, compatibilityPolyPressureSlots,
        eventCapacity, /*retainAllControllerScalars=*/false);
    for (ExpressionContext& context : channelExpressionContexts_) {
        context.configure(
            compatibilityControllerSlots, compatibilityPolyPressureSlots,
            eventCapacity, /*retainAllControllerScalars=*/false);
    }
    resetEventStates();
    resetNoteStates();
}

void sfz::MidiState::noteOnEvent(int delay, int noteNumber, float velocity) noexcept
{
    ASSERT(noteNumber >= 0 && noteNumber <= 127);
    ASSERT(velocity >= 0 && velocity <= 1.0);

    if (noteNumber >= 0 && noteNumber < 128) {
        float keydelta { 0 };

        if (lastNotePlayed >= 0) {
            keydelta = static_cast<float>(noteNumber - lastNotePlayed);
            velocityOverride = lastNoteVelocities[lastNotePlayed];
        }

        lastNoteVelocities[noteNumber] = velocity;
        noteOnTimes[noteNumber] = internalClock + static_cast<unsigned>(delay);
        lastNotePlayed = noteNumber;
        noteStates[noteNumber] = true;
        ccEvent(delay, ExtendedCCs::noteOnVelocity, velocity);
        ccEvent(delay, ExtendedCCs::keyboardNoteNumber, normalize7Bits(noteNumber));
        ccEvent(delay, ExtendedCCs::unipolarRandom, unipolarDist(Random::randomGenerator));
        ccEvent(delay, ExtendedCCs::bipolarRandom, bipolarDist(Random::randomGenerator));
        ccEvent(delay, ExtendedCCs::keyboardNoteGate, activeNotes > 0 ? 1.0f : 0.0f);
        ccEvent(delay, AriaExtendedCCs::keydelta, keydelta);
        ccEvent(delay, AriaExtendedCCs::absoluteKeydelta, std::abs(keydelta));
        activeNotes++;

        ccEvent(delay, ExtendedCCs::alternate, alternate);
        alternate = alternate == 0.0f ? 1.0f : 0.0f;
    }
}

void sfz::MidiState::noteOffEvent(int delay, int noteNumber, float velocity) noexcept
{
    ASSERT(delay >= 0);
    ASSERT(noteNumber >= 0 && noteNumber <= 127);
    ASSERT(velocity >= 0.0 && velocity <= 1.0);
    UNUSED(velocity);
    if (noteNumber >= 0 && noteNumber < 128) {
        noteOffTimes[noteNumber] = internalClock + static_cast<unsigned>(delay);
        ccEvent(delay, ExtendedCCs::noteOffVelocity, velocity);
        ccEvent(delay, ExtendedCCs::keyboardNoteNumber, normalize7Bits(noteNumber));
        ccEvent(delay, ExtendedCCs::unipolarRandom, unipolarDist(Random::randomGenerator));
        ccEvent(delay, ExtendedCCs::bipolarRandom, bipolarDist(Random::randomGenerator));
        if (activeNotes > 0)
            activeNotes--;
        noteStates[noteNumber] = false;
    }
}

void sfz::MidiState::sourceNoteOnEvent(int channel, int noteNumber, float velocity) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return;
    if (noteNumber < 0 || noteNumber >= 128)
        return;

    SourceNoteState& state = sourceNoteStates[channel];
    if (state.lastNotePlayed >= 0)
        state.velocityOverride = state.velocities[state.lastNotePlayed];
    state.velocities[noteNumber] = velocity;
    state.lastNotePlayed = noteNumber;
    if (state.noteCounts[noteNumber] < std::numeric_limits<uint16_t>::max()) {
        ++state.noteCounts[noteNumber];
        ++state.activeNotes;
    }
    state.pressed.set(noteNumber);
}

void sfz::MidiState::sourceNoteOffEvent(int channel, int noteNumber) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return;
    if (noteNumber < 0 || noteNumber >= 128)
        return;

    SourceNoteState& state = sourceNoteStates[channel];
    if (state.noteCounts[noteNumber] > 0) {
        --state.noteCounts[noteNumber];
        if (state.noteCounts[noteNumber] == 0)
            state.pressed.reset(noteNumber);
        if (state.activeNotes > 0)
            --state.activeNotes;
    }
}

int sfz::MidiState::getSourceActiveNotes(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return 0;
    return sourceNoteStates[channel].activeNotes;
}

float sfz::MidiState::getSourceNoteVelocity(int channel, int noteNumber) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return 0.0f;
    if (noteNumber < 0 || noteNumber >= 128)
        return 0.0f;
    return sourceNoteStates[channel].velocities[noteNumber];
}

float sfz::MidiState::getSourceVelocityOverride(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return 0.0f;
    return sourceNoteStates[channel].velocityOverride;
}

bool sfz::MidiState::isSourceNotePressed(int channel, int noteNumber) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return false;
    if (noteNumber < 0 || noteNumber >= 128)
        return false;
    return sourceNoteStates[channel].pressed.test(noteNumber);
}

void sfz::MidiState::sourcePolyAftertouchEvent(int channel, int noteNumber, float aftertouch) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return;
    if (noteNumber < 0 || noteNumber >= 128)
        return;
    sourceNoteStates[channel].polyAftertouch[noteNumber] = aftertouch;
}

float sfz::MidiState::getSourcePolyAftertouch(int channel, int noteNumber) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceNoteStates.size()))
        return 0.0f;
    if (noteNumber < 0 || noteNumber >= 128)
        return 0.0f;
    return sourceNoteStates[channel].polyAftertouch[noteNumber];
}

void sfz::MidiState::sourcePitchBendEvent(int channel, float pitch) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourcePitchBends.size()))
        return;
    sourcePitchBends[channel] = pitch;
}

float sfz::MidiState::getSourcePitchBend(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourcePitchBends.size()))
        return 0.0f;
    return sourcePitchBends[channel];
}

void sfz::MidiState::sourceChannelAftertouchEvent(int channel, float aftertouch) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceChannelAftertouch.size()))
        return;
    sourceChannelAftertouch[channel] = aftertouch;
}

float sfz::MidiState::getSourceChannelAftertouch(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceChannelAftertouch.size()))
        return 0.0f;
    return sourceChannelAftertouch[channel];
}

void sfz::MidiState::allNotesOff(int delay) noexcept
{
    for (int note = 0; note < 128; note++)
        noteOffEvent(delay, note, 0.0f);
    for (SourceNoteState& state : sourceNoteStates) {
        state.pressed.reset();
        state.noteCounts.fill(0);
        state.activeNotes = 0;
    }
}

void sfz::MidiState::setSampleRate(float sampleRate) noexcept
{
    this->sampleRate = sampleRate;
    internalClock = 0;
    absl::c_fill(noteOnTimes, 0);
    absl::c_fill(noteOffTimes, 0);
}

void sfz::MidiState::advanceTime(int numSamples) noexcept
{
    internalClock += numSamples;
    flushEvents();
}

void sfz::MidiState::flushEvents() noexcept
{
    globalExpressionContext_.flushEvents();
    lowerZoneExpressionContext_.flushEvents();
    for (ExpressionContext& context : channelExpressionContexts_)
        context.flushEvents();
    for (NoteExpressionSlot& slot : noteExpressionSlots_)
        slot.context.flushEvents();
}

void sfz::MidiState::setSamplesPerBlock(int samplesPerBlock) noexcept
{
    this->samplesPerBlock = samplesPerBlock;
    const size_t eventCapacity = static_cast<size_t>(samplesPerBlock) + 1;
    globalExpressionContext_.setTimelineEventCapacity(eventCapacity);
    lowerZoneExpressionContext_.setTimelineEventCapacity(eventCapacity);
    for (ExpressionContext& context : channelExpressionContexts_)
        context.setTimelineEventCapacity(eventCapacity);
}

void sfz::MidiState::configureExpressionControls(
    const std::array<bool, config::numCCs>& usedControllers)
{
    globalExpressionContext_.configureSfizzControllers(usedControllers);

    // CC74 is the MPE profile's standardized per-note timbre control even
    // when the loaded instrument does not currently connect it. Other
    // channel timelines are exactly the controls consumed by the SFZ.
    std::array<bool, config::numCCs> profileControllers = usedControllers;
    profileControllers[74] = true;
    for (int cc = 0; cc < config::numCCs; ++cc)
        noteExpressionControllers_.set(cc, profileControllers[cc]);
    lowerZoneExpressionContext_.configureSfizzControllers(profileControllers);
    for (ExpressionContext& context : channelExpressionContexts_)
        context.configureSfizzControllers(profileControllers);
    for (NoteExpressionSlot& slot : noteExpressionSlots_)
        slot.context.configureSfizzControllers(profileControllers);
}

void sfz::MidiState::configureNoteExpressionContexts(size_t capacity)
{
    noteExpressionSlots_.clear();
    retiredNoteExpressionOverflowCount_ = 0;
    noteExpressionSlots_.resize(capacity);
    std::array<bool, config::numCCs> noteControllers { };
    for (int cc = 0; cc < config::numCCs; ++cc)
        noteControllers[cc] = noteExpressionControllers_.test(cc);
    for (NoteExpressionSlot& slot : noteExpressionSlots_) {
        slot.context.configure(
            0, 0, noteTimelineEvents, /*retainAllControllerScalars=*/false);
        slot.context.configureSfizzControllers(noteControllers);
        slot.generation = 0;
        slot.active = false;
    }
}

void sfz::MidiState::beginNoteExpression(NoteInstanceId noteId) noexcept
{
    if (!noteId.valid() || noteId.index >= noteExpressionSlots_.size())
        return;
    NoteExpressionSlot& slot = noteExpressionSlots_[noteId.index];
    retiredNoteExpressionOverflowCount_ += slot.context.overflowCount();
    slot.context.reset();
    slot.generation = noteId.generation;
    slot.active = true;
}

void sfz::MidiState::endNoteExpression(NoteInstanceId noteId) noexcept
{
    if (!noteId.valid() || noteId.index >= noteExpressionSlots_.size())
        return;
    NoteExpressionSlot& slot = noteExpressionSlots_[noteId.index];
    if (slot.generation == noteId.generation)
        slot.active = false;
}

void sfz::MidiState::clearNoteExpressionContexts() noexcept
{
    for (NoteExpressionSlot& slot : noteExpressionSlots_)
        slot.active = false;
}

void sfz::MidiState::resetScopedExpressionContexts() noexcept
{
    lowerZoneExpressionContext_.reset();
    for (ExpressionContext& context : channelExpressionContexts_)
        context.reset();
    for (NoteExpressionSlot& slot : noteExpressionSlots_)
        slot.context.reset();
}

sfz::ExpressionContext& sfz::MidiState::compatibilityContext(int channel) noexcept
{
    return channel == masterChannel
        ? globalExpressionContext_
        : channelExpressionContexts_[channel];
}

const sfz::ExpressionContext& sfz::MidiState::compatibilityContext(int channel) const noexcept
{
    return channel == masterChannel
        ? globalExpressionContext_
        : channelExpressionContexts_[channel];
}

sfz::ExpressionContext* sfz::MidiState::getExpressionContext(
    ExpressionTarget target) noexcept
{
    switch (target.scope) {
    case ExpressionScope::Global:
        return &globalExpressionContext_;
    case ExpressionScope::Zone:
        return target.id == 0 ? &lowerZoneExpressionContext_ : nullptr;
    case ExpressionScope::Channel: {
        const SourceAddress source = target.sourceAddress();
        return source.group == 0 && source.channel < channelExpressionContexts_.size()
            ? &channelExpressionContexts_[source.channel]
            : nullptr;
    }
    case ExpressionScope::Note: {
        const NoteInstanceId noteId = target.noteInstanceId();
        if (!noteId.valid() || noteId.index >= noteExpressionSlots_.size())
            return nullptr;
        NoteExpressionSlot& slot = noteExpressionSlots_[noteId.index];
        return slot.active && slot.generation == noteId.generation
            ? &slot.context
            : nullptr;
    }
    }
    return nullptr;
}

const sfz::ExpressionContext* sfz::MidiState::getExpressionContext(
    ExpressionTarget target) const noexcept
{
    return const_cast<MidiState*>(this)->getExpressionContext(target);
}

bool sfz::MidiState::expressionEvent(
    const ResolvedExpressionEvent& event) noexcept
{
    ExpressionContext* context = getExpressionContext(event.target);
    if (context == nullptr)
        return false;

    switch (event.kind) {
    case ExpressionEventKind::LegacyPitch:
        if (event.target.scope != ExpressionScope::Global)
            return false;
        return context->pitchEvent(event.delay, event.value);
    case ExpressionEventKind::Pitch:
        if (event.target.scope == ExpressionScope::Global)
            return false;
        return context->pitchEvent(event.delay, event.value);
    case ExpressionEventKind::Pressure:
        return context->pressureEvent(event.delay, event.value);
    case ExpressionEventKind::Timbre:
        return context->timbreEvent(event.delay, event.value);
    case ExpressionEventKind::Control: {
        int sfizzCC = -1;
        switch (event.control.nameSpace) {
        case ExpressionControlNamespace::MidiCC:
            sfizzCC = event.control.number < 128
                ? static_cast<int>(event.control.number)
                : -1;
            break;
        case ExpressionControlNamespace::SfzExtendedCC:
            sfizzCC = event.control.number;
            break;
        case ExpressionControlNamespace::Midi2Registered:
        case ExpressionControlNamespace::Midi2Assignable:
            return false;
        }
        return context->controllerEvent(event.delay, sfizzCC, event.value);
    }
    case ExpressionEventKind::PolyPressure:
        return context->polyPressureEvent(
            event.delay, event.noteNumber, event.value);
    }
    return false;
}

const sfz::EventVector& sfz::MidiState::getVoiceCCEvents(
    ExpressionTarget broadTarget, NoteInstanceId noteId, int ccNumber) const noexcept
{
    if (const ExpressionContext* note = getExpressionContext(
            ExpressionTarget::note(noteId))) {
        if (note->hasController(ccNumber)) {
            if (const EventVector* events = note->controllerEvents(ccNumber))
                return *events;
        }
    }
    if (const ExpressionContext* broad = getExpressionContext(broadTarget)) {
        if (broad->hasController(ccNumber)) {
            if (const EventVector* events = broad->controllerEvents(ccNumber))
                return *events;
        }
    }
    return getCCEvents(ccNumber);
}

const sfz::EventVector& sfz::MidiState::getVoicePressureEvents(
    ExpressionTarget broadTarget, NoteInstanceId noteId) const noexcept
{
    if (const ExpressionContext* note = getExpressionContext(
            ExpressionTarget::note(noteId))) {
        if (note->hasPressure())
            return note->pressureEvents();
    }
    if (const ExpressionContext* broad = getExpressionContext(broadTarget)) {
        if (broad->hasPressure())
            return broad->pressureEvents();
    }
    return getChannelAftertouchEvents();
}

const sfz::EventVector& sfz::MidiState::getVoicePolyPressureEvents(
    ExpressionTarget broadTarget, NoteInstanceId noteId,
    int noteNumber) const noexcept
{
    if (const ExpressionContext* note = getExpressionContext(
            ExpressionTarget::note(noteId))) {
        if (note->hasPolyPressure(noteNumber)) {
            if (const EventVector* events = note->polyPressureEvents(noteNumber))
                return *events;
        }
    }
    if (const ExpressionContext* broad = getExpressionContext(broadTarget)) {
        if (broad->hasPolyPressure(noteNumber)) {
            if (const EventVector* events = broad->polyPressureEvents(noteNumber))
                return *events;
        }
    }
    return getPolyAftertouchEvents(noteNumber);
}

const sfz::EventVector& sfz::MidiState::getVoiceBroadPitchEvents(
    ExpressionTarget broadTarget) const noexcept
{
    const ExpressionContext* context = getExpressionContext(broadTarget);
    return context != nullptr ? context->pitchEvents() : nullEvent;
}

const sfz::EventVector& sfz::MidiState::getVoiceNotePitchEvents(
    NoteInstanceId noteId) const noexcept
{
    const ExpressionContext* context = getExpressionContext(
        ExpressionTarget::note(noteId));
    return context != nullptr ? context->pitchEvents() : nullEvent;
}

uint64_t sfz::MidiState::getExpressionOverflowCount() const noexcept
{
    uint64_t count = retiredNoteExpressionOverflowCount_
        + globalExpressionContext_.overflowCount()
        + lowerZoneExpressionContext_.overflowCount();
    for (const ExpressionContext& context : channelExpressionContexts_)
        count += context.overflowCount();
    for (const NoteExpressionSlot& slot : noteExpressionSlots_)
        count += slot.context.overflowCount();
    return count;
}

float sfz::MidiState::getNoteDuration(int noteNumber, int delay) const
{
    ASSERT(noteNumber >= 0 && noteNumber < 128);
    if (noteNumber < 0 || noteNumber >= 128)
        return 0.0f;

#if 0
    if (!noteStates[noteNumber])
        return 0.0f;
#endif

    const unsigned timeInSamples = internalClock + static_cast<unsigned>(delay) - noteOnTimes[noteNumber];
    return static_cast<float>(timeInSamples) / sampleRate;
}

float sfz::MidiState::getNoteVelocity(int noteNumber) const noexcept
{
    ASSERT(noteNumber >= 0 && noteNumber <= 127);

    return lastNoteVelocities[noteNumber];
}

float sfz::MidiState::getVelocityOverride() const noexcept
{
    return velocityOverride;
}

void sfz::MidiState::pitchBendEvent(int delay, float pitchBendValue) noexcept
{
    pitchBendEvent(delay, masterChannel, pitchBendValue);
}

void sfz::MidiState::pitchBendEvent(int delay, int channel, float pitchBendValue) noexcept
{
    ASSERT(pitchBendValue >= -1.0f && pitchBendValue <= 1.0f);
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return;
    compatibilityContext(channel).pitchEvent(delay, pitchBendValue);
}

float sfz::MidiState::getPitchBend() const noexcept
{
    return getPitchBend(masterChannel);
}

float sfz::MidiState::getPitchBend(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return 0.0f;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasPitch() && channel != masterChannel)
        return getPitchBend(masterChannel);
    return context.pitchValue();
}

void sfz::MidiState::channelAftertouchEvent(int delay, float aftertouch) noexcept
{
    channelAftertouchEvent(delay, masterChannel, aftertouch);
}

void sfz::MidiState::channelAftertouchEvent(int delay, int channel, float aftertouch) noexcept
{
    ASSERT(aftertouch >= -1.0f && aftertouch <= 1.0f);
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return;
    compatibilityContext(channel).pressureEvent(delay, aftertouch);
}

void sfz::MidiState::polyAftertouchEvent(int delay, int noteNumber, float aftertouch) noexcept
{
    polyAftertouchEvent(delay, masterChannel, noteNumber, aftertouch);
}

void sfz::MidiState::polyAftertouchEvent(int delay, int channel, int noteNumber, float aftertouch) noexcept
{
    ASSERT(aftertouch >= 0.0f && aftertouch <= 1.0f);
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return;
    compatibilityContext(channel).polyPressureEvent(
        delay, noteNumber, aftertouch);
}

float sfz::MidiState::getChannelAftertouch() const noexcept
{
    return getChannelAftertouch(masterChannel);
}

float sfz::MidiState::getChannelAftertouch(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return 0.0f;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasPressure() && channel != masterChannel)
        return getChannelAftertouch(masterChannel);
    return context.pressureValue();
}

float sfz::MidiState::getPolyAftertouch(int noteNumber) const noexcept
{
    return getPolyAftertouch(masterChannel, noteNumber);
}

float sfz::MidiState::getPolyAftertouch(int channel, int noteNumber) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return 0.0f;
    if (noteNumber < 0 || noteNumber > 127)
        return 0.0f;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasPolyPressure(noteNumber) && channel != masterChannel)
        return getPolyAftertouch(masterChannel, noteNumber);
    return context.polyPressureValue(noteNumber);
}

void sfz::MidiState::ccEvent(int delay, int ccNumber, float ccValue) noexcept
{
    ccEvent(delay, masterChannel, ccNumber, ccValue);
}

void sfz::MidiState::ccEvent(int delay, int channel, int ccNumber, float ccValue) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return;
    if (ccNumber < 0 || ccNumber >= config::numCCs)
        return;
    compatibilityContext(channel).controllerEvent(delay, ccNumber, ccValue);
}

void sfz::MidiState::sourceCCEvent(int channel, int ccNumber, float ccValue) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceCCValues.size()))
        return;
    if (ccNumber < 0 || ccNumber >= config::numCCs)
        return;
    sourceCCValues[channel][ccNumber] = ccValue;
}

float sfz::MidiState::getSourceCCValue(int channel, int ccNumber) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(sourceCCValues.size()))
        return 0.0f;
    if (ccNumber < 0 || ccNumber >= config::numCCs)
        return 0.0f;
    return sourceCCValues[channel][ccNumber];
}

void sfz::MidiState::resetSourceCCStates() noexcept
{
    for (auto& values : sourceCCValues)
        values.fill(0.0f);
}

float sfz::MidiState::getCCValue(int ccNumber) const noexcept
{
    return getCCValue(masterChannel, ccNumber);
}

float sfz::MidiState::getCCValue(int channel, int ccNumber) const noexcept
{
    ASSERT(ccNumber >= 0 && ccNumber < config::numCCs);
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return 0.0f;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasController(ccNumber) && channel != masterChannel)
        return getCCValue(masterChannel, ccNumber);
    return context.controllerValue(ccNumber);
}

float sfz::MidiState::getCCValueAt(int ccNumber, int delay) const noexcept
{
    return getCCValueAt(masterChannel, ccNumber, delay);
}

float sfz::MidiState::getCCValueAt(int channel, int ccNumber, int delay) const noexcept
{
    ASSERT(ccNumber >= 0 && ccNumber < config::numCCs);
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return 0.0f;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasController(ccNumber) && channel != masterChannel)
        return getCCValueAt(masterChannel, ccNumber, delay);
    const EventVector* events = context.controllerEvents(ccNumber);
    if (events == nullptr)
        return context.controllerValue(ccNumber);
    const auto event = absl::c_lower_bound(
        *events, delay, MidiEventDelayComparator { });
    return event != events->end() ? event->value : events->back().value;
}

void sfz::MidiState::resetNoteStates() noexcept
{
    for (auto& velocity : lastNoteVelocities)
        velocity = 0.0f;

    velocityOverride = 0.0f;
    activeNotes = 0;
    internalClock = 0;
    lastNotePlayed = -1;
    alternate = 0.0f;

    globalExpressionContext_.controllerEvent(
        0, ExtendedCCs::noteOnVelocity, 0.0f);
    globalExpressionContext_.controllerEvent(
        0, ExtendedCCs::keyboardNoteNumber, 0.0f);
    globalExpressionContext_.controllerEvent(
        0, ExtendedCCs::unipolarRandom, 0.0f);
    globalExpressionContext_.controllerEvent(
        0, ExtendedCCs::bipolarRandom, 0.0f);
    globalExpressionContext_.controllerEvent(
        0, ExtendedCCs::keyboardNoteGate, 0.0f);
    globalExpressionContext_.controllerEvent(
        0, ExtendedCCs::alternate, 0.0f);

    noteStates.reset();
    absl::c_fill(noteOnTimes, 0);
    absl::c_fill(noteOffTimes, 0);

    for (SourceNoteState& state : sourceNoteStates) {
        state.pressed.reset();
        state.noteCounts.fill(0);
        state.velocities.fill(0.0f);
        state.polyAftertouch.fill(0.0f);
        state.activeNotes = 0;
        state.lastNotePlayed = -1;
        state.velocityOverride = 0.0f;
    }
}

const sfz::EventVector& sfz::MidiState::getPitchEventsRaw(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return nullEvent;
    return compatibilityContext(channel).pitchEvents();
}

float sfz::MidiState::getPitchBendRaw(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return 0.0f;
    const ExpressionContext& context = compatibilityContext(channel);
    return context.hasPitch() ? context.pitchValue() : 0.0f;
}

void sfz::MidiState::setMPEPitchBendRange(float masterSemitones, float perNoteSemitones) noexcept
{
    mpeMasterPitchBendRange_ = masterSemitones;
    mpePerNotePitchBendRange_ = perNoteSemitones;
}

float sfz::MidiState::getMPEBendRangeForChannel(int channel) const noexcept
{
    return (channel == masterChannel) ? mpeMasterPitchBendRange_ : mpePerNotePitchBendRange_;
}

void sfz::MidiState::resetEventStates() noexcept
{
    resetSourceCCStates();
    sourcePitchBends.fill(0.0f);
    sourceChannelAftertouch.fill(0.0f);

    globalExpressionContext_.reset();
    lowerZoneExpressionContext_.reset();
    retiredNoteExpressionOverflowCount_ = 0;
    for (ExpressionContext& context : channelExpressionContexts_)
        context.reset();
    for (NoteExpressionSlot& slot : noteExpressionSlots_) {
        slot.context.reset();
        slot.active = false;
    }
}

const sfz::EventVector& sfz::MidiState::getCCEvents(int ccIdx) const noexcept
{
    return getCCEvents(masterChannel, ccIdx);
}

const sfz::EventVector& sfz::MidiState::getCCEvents(int channel, int ccIdx) const noexcept
{
    if (ccIdx < 0 || ccIdx >= config::numCCs)
        return nullEvent;
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return nullEvent;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasController(ccIdx) && channel != masterChannel)
        return getCCEvents(masterChannel, ccIdx);
    const EventVector* events = context.controllerEvents(ccIdx);
    return events != nullptr ? *events : nullEvent;
}

const sfz::EventVector& sfz::MidiState::getPitchEvents() const noexcept
{
    return getPitchEvents(masterChannel);
}

const sfz::EventVector& sfz::MidiState::getPitchEvents(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return nullEvent;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasPitch() && channel != masterChannel)
        return getPitchEvents(masterChannel);
    return context.pitchEvents();
}

const sfz::EventVector& sfz::MidiState::getChannelAftertouchEvents() const noexcept
{
    return getChannelAftertouchEvents(masterChannel);
}

const sfz::EventVector& sfz::MidiState::getChannelAftertouchEvents(int channel) const noexcept
{
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return nullEvent;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasPressure() && channel != masterChannel)
        return getChannelAftertouchEvents(masterChannel);
    return context.pressureEvents();
}

const sfz::EventVector& sfz::MidiState::getPolyAftertouchEvents(int noteNumber) const noexcept
{
    return getPolyAftertouchEvents(masterChannel, noteNumber);
}

const sfz::EventVector& sfz::MidiState::getPolyAftertouchEvents(int channel, int noteNumber) const noexcept
{
    if (noteNumber < 0 || noteNumber > 127)
        return nullEvent;
    if (channel < 0 || channel >= static_cast<int>(channelExpressionContexts_.size()))
        return nullEvent;
    const ExpressionContext& context = compatibilityContext(channel);
    if (!context.hasPolyPressure(noteNumber) && channel != masterChannel)
        return getPolyAftertouchEvents(masterChannel, noteNumber);
    const EventVector* events = context.polyPressureEvents(noteNumber);
    return events != nullptr ? *events : nullEvent;
}

int sfz::MidiState::getProgram() const noexcept
{
    return currentProgram;
}

int sfz::MidiState::getProgram(SourceAddress source) const noexcept
{
    if (source.group >= 16 || source.channel >= 16)
        return currentProgram;
    return sourcePrograms_[static_cast<size_t>(source.group) * 16 + source.channel];
}

int sfz::MidiState::getProgram(RoutingTarget target) const noexcept
{
    switch (target.scope) {
    case RoutingScope::Global:
        return currentProgram;
    case RoutingScope::Zone:
        return target.id < zonePrograms_.size()
            ? zonePrograms_[target.id]
            : currentProgram;
    case RoutingScope::Channel:
        return getProgram(target.sourceAddress());
    }
    return currentProgram;
}

void sfz::MidiState::programChangeEvent(int delay, int program) noexcept
{
    UNUSED(delay);
    ASSERT(program >= 0 && program <= 127);
    currentProgram = program;
    zonePrograms_.fill(program);
    sourcePrograms_.fill(program);
}

void sfz::MidiState::programChangeEvent(
    int delay, RoutingTarget target, int program) noexcept
{
    UNUSED(delay);
    ASSERT(program >= 0 && program <= 127);

    switch (target.scope) {
    case RoutingScope::Global:
        programChangeEvent(delay, program);
        return;
    case RoutingScope::Zone: {
        if (target.id >= zonePrograms_.size())
            return;
        currentProgram = program;
        zonePrograms_[target.id] = program;
        const size_t first = static_cast<size_t>(target.id) * 16;
        std::fill_n(sourcePrograms_.begin() + first, 16, program);
        return;
    }
    case RoutingScope::Channel: {
        const SourceAddress source = target.sourceAddress();
        if (source.group >= 16 || source.channel >= 16)
            return;
        currentProgram = program;
        sourcePrograms_[static_cast<size_t>(source.group) * 16 + source.channel] = program;
        return;
    }
    }
}
