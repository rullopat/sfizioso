// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "ExpressionContext.h"
#include <absl/algorithm/container.h>
#include <algorithm>

namespace sfz {

void ExpressionContext::seedTimeline(EventVector& events, size_t capacity, float value)
{
    events.clear();
    events.shrink_to_fit();
    events.reserve(capacity);
    events.push_back({ 0, value });
}

void ExpressionContext::resetTimeline(EventVector& events, float value) noexcept
{
    events.clear();
    events.push_back({ 0, value });
}

void ExpressionContext::configure(size_t controllerSlots,
    size_t polyPressureSlots, size_t eventsPerTimeline,
    bool retainAllControllerScalars)
{
    eventsPerTimeline_ = std::max<size_t>(2, eventsPerTimeline);

    if (controllerSlots > 0 && !controllerLookup_)
        controllerLookup_ = std::make_unique<std::array<int16_t, config::numCCs>>();
    if (retainAllControllerScalars && !controllerScalarState_)
        controllerScalarState_ = std::make_unique<ControllerScalarState>();

    controllerSlots_.clear();
    controllerSlots_.resize(controllerSlots);
    if (controllerLookup_)
        controllerLookup_->fill(unassignedControl);
    for (ControllerSlot& slot : controllerSlots_)
        seedTimeline(slot.events, eventsPerTimeline_, 0.0f);

    polyPressureSlots_.clear();
    polyPressureSlots_.resize(polyPressureSlots);
    for (PolyPressureSlot& slot : polyPressureSlots_)
        seedTimeline(slot.events, eventsPerTimeline_, 0.0f);

    seedTimeline(pitchEvents_, eventsPerTimeline_, pitchValue_);
    seedTimeline(pressureEvents_, eventsPerTimeline_, pressureValue_);
    seedTimeline(timbreEvents_, eventsPerTimeline_, timbreValue_);
    overflowCount_ = 0;
}

void ExpressionContext::setTimelineEventCapacity(size_t eventsPerTimeline)
{
    eventsPerTimeline_ = std::max<size_t>(2, eventsPerTimeline);
    for (ControllerSlot& slot : controllerSlots_)
        seedTimeline(slot.events, eventsPerTimeline_, slot.value);
    for (PolyPressureSlot& slot : polyPressureSlots_)
        seedTimeline(slot.events, eventsPerTimeline_, slot.value);
    seedTimeline(pitchEvents_, eventsPerTimeline_, pitchValue_);
    seedTimeline(pressureEvents_, eventsPerTimeline_, pressureValue_);
    seedTimeline(timbreEvents_, eventsPerTimeline_, timbreValue_);
}

void ExpressionContext::configureSfizzControllers(
    const std::array<bool, config::numCCs>& used, size_t compatibilitySlots)
{
    // Preserve current scalar state while the dense slot set is rebuilt.
    std::array<float, config::numCCs> values { };
    std::array<bool, config::numCCs> present { };
    for (int cc = 0; cc < config::numCCs; ++cc) {
        values[cc] = controllerValue(cc);
        present[cc] = hasController(cc);
    }

    const size_t usedCount = static_cast<size_t>(
        std::count(used.begin(), used.end(), true));
    const size_t requiredSlots = usedCount + compatibilitySlots;
    if (requiredSlots > 0 && !controllerLookup_)
        controllerLookup_ = std::make_unique<std::array<int16_t, config::numCCs>>();

    controllerSlots_.clear();
    controllerSlots_.resize(requiredSlots);
    if (controllerLookup_)
        controllerLookup_->fill(unassignedControl);
    for (ControllerSlot& slot : controllerSlots_)
        seedTimeline(slot.events, eventsPerTimeline_, 0.0f);

    size_t slotIndex = 0;
    for (int cc = 0; cc < config::numCCs; ++cc) {
        if (!used[cc])
            continue;
        ControllerSlot& slot = controllerSlots_[slotIndex];
        slot.assigned = true;
        slot.id = ExpressionControlId::fromSfizzCC(cc);
        slot.value = values[cc];
        slot.present = present[cc];
        (*controllerLookup_)[cc] = static_cast<int16_t>(slotIndex);
        seedTimeline(slot.events, eventsPerTimeline_, slot.value);
        ++slotIndex;
    }
}

bool ExpressionContext::insertEvent(EventVector& events, int delay, float value) noexcept
{
    const auto insertionPoint = absl::c_lower_bound(
        events, delay, MidiEventDelayComparator { });
    if (insertionPoint != events.end() && insertionPoint->delay == delay) {
        insertionPoint->value = value;
        return true;
    }
    if (events.size() >= events.capacity()) {
        ++overflowCount_;
        return false;
    }
    events.insert(insertionPoint, { delay, value });
    return true;
}

ExpressionContext::ControllerSlot*
ExpressionContext::assignControllerSlot(int sfizzCC) noexcept
{
    if (sfizzCC < 0 || sfizzCC >= config::numCCs || !controllerLookup_)
        return nullptr;
    const int existing = (*controllerLookup_)[sfizzCC];
    if (existing != unassignedControl)
        return &controllerSlots_[static_cast<size_t>(existing)];

    const auto it = std::find_if(controllerSlots_.begin(), controllerSlots_.end(),
        [](const ControllerSlot& slot) { return !slot.assigned; });
    if (it == controllerSlots_.end())
        return nullptr;

    it->assigned = true;
    it->id = ExpressionControlId::fromSfizzCC(sfizzCC);
    if (controllerScalarState_) {
        it->value = controllerScalarState_->values[sfizzCC];
        it->present = controllerScalarState_->present[sfizzCC];
    }
    resetTimeline(it->events, it->value);
    const size_t index = static_cast<size_t>(
        std::distance(controllerSlots_.begin(), it));
    (*controllerLookup_)[sfizzCC] = static_cast<int16_t>(index);
    return &*it;
}

ExpressionContext::PolyPressureSlot*
ExpressionContext::assignPolyPressureSlot(int noteNumber) noexcept
{
    const auto existing = std::find_if(
        polyPressureSlots_.begin(), polyPressureSlots_.end(),
        [=](const PolyPressureSlot& slot) { return slot.noteNumber == noteNumber; });
    if (existing != polyPressureSlots_.end())
        return &*existing;

    const auto available = std::find_if(
        polyPressureSlots_.begin(), polyPressureSlots_.end(),
        [](const PolyPressureSlot& slot) { return slot.noteNumber < 0; });
    if (available == polyPressureSlots_.end())
        return nullptr;

    available->noteNumber = noteNumber;
    available->value = 0.0f;
    available->present = false;
    resetTimeline(available->events, available->value);
    return &*available;
}

bool ExpressionContext::controllerEvent(int delay, int sfizzCC, float value) noexcept
{
    if (sfizzCC < 0 || sfizzCC >= config::numCCs)
        return false;
    ControllerSlot* slot = assignControllerSlot(sfizzCC);
    if (slot != nullptr) {
        if (!insertEvent(slot->events, delay, value))
            return false;
        slot->value = slot->events.back().value;
        slot->present = true;
    } else if (!controllerScalarState_) {
        // No timeline exists because the loaded instrument/profile does not
        // consume this control. Source-routing scalar state is maintained by
        // MidiState separately, so there is no expression overflow to count.
        return true;
    }

    if (controllerScalarState_) {
        controllerScalarState_->values[sfizzCC] = slot != nullptr
            ? slot->value
            : value;
        controllerScalarState_->present[sfizzCC] = true;
    }
    return true;
}

bool ExpressionContext::pitchEvent(int delay, float semitones) noexcept
{
    if (!insertEvent(pitchEvents_, delay, semitones))
        return false;
    pitchValue_ = pitchEvents_.back().value;
    pitchPresent_ = true;
    return true;
}

bool ExpressionContext::pressureEvent(int delay, float value) noexcept
{
    if (!insertEvent(pressureEvents_, delay, value))
        return false;
    pressureValue_ = pressureEvents_.back().value;
    pressurePresent_ = true;
    return true;
}

bool ExpressionContext::timbreEvent(int delay, float value) noexcept
{
    if (!insertEvent(timbreEvents_, delay, value))
        return false;
    timbreValue_ = timbreEvents_.back().value;
    timbrePresent_ = true;
    return true;
}

bool ExpressionContext::polyPressureEvent(
    int delay, int noteNumber, float value) noexcept
{
    if (noteNumber < 0 || noteNumber >= 128)
        return false;
    PolyPressureSlot* slot = assignPolyPressureSlot(noteNumber);
    if (slot == nullptr) {
        ++overflowCount_;
        return false;
    }
    if (!insertEvent(slot->events, delay, value))
        return false;
    slot->value = slot->events.back().value;
    slot->present = true;
    return true;
}

float ExpressionContext::controllerValue(int sfizzCC) const noexcept
{
    if (sfizzCC < 0 || sfizzCC >= config::numCCs)
        return 0.0f;
    if (controllerLookup_) {
        const int index = (*controllerLookup_)[sfizzCC];
        if (index != unassignedControl)
            return controllerSlots_[static_cast<size_t>(index)].value;
    }
    return controllerScalarState_
        ? controllerScalarState_->values[sfizzCC]
        : 0.0f;
}

float ExpressionContext::polyPressureValue(int noteNumber) const noexcept
{
    if (noteNumber < 0 || noteNumber >= 128)
        return 0.0f;
    const auto it = std::find_if(
        polyPressureSlots_.begin(), polyPressureSlots_.end(),
        [=](const PolyPressureSlot& slot) { return slot.noteNumber == noteNumber; });
    return it == polyPressureSlots_.end() ? 0.0f : it->value;
}

bool ExpressionContext::hasController(int sfizzCC) const noexcept
{
    if (sfizzCC < 0 || sfizzCC >= config::numCCs)
        return false;
    if (controllerLookup_) {
        const int index = (*controllerLookup_)[sfizzCC];
        if (index != unassignedControl)
            return controllerSlots_[static_cast<size_t>(index)].present;
    }
    return controllerScalarState_
        && controllerScalarState_->present[sfizzCC];
}

bool ExpressionContext::hasPolyPressure(int noteNumber) const noexcept
{
    if (noteNumber < 0 || noteNumber >= 128)
        return false;
    const auto it = std::find_if(
        polyPressureSlots_.begin(), polyPressureSlots_.end(),
        [=](const PolyPressureSlot& slot) { return slot.noteNumber == noteNumber; });
    return it != polyPressureSlots_.end() && it->present;
}

const EventVector* ExpressionContext::controllerEvents(int sfizzCC) const noexcept
{
    if (sfizzCC < 0 || sfizzCC >= config::numCCs || !controllerLookup_)
        return nullptr;
    const int slot = (*controllerLookup_)[sfizzCC];
    return slot == unassignedControl
        ? nullptr
        : &controllerSlots_[static_cast<size_t>(slot)].events;
}

const EventVector* ExpressionContext::polyPressureEvents(
    int noteNumber) const noexcept
{
    const auto it = std::find_if(
        polyPressureSlots_.begin(), polyPressureSlots_.end(),
        [=](const PolyPressureSlot& slot) { return slot.noteNumber == noteNumber; });
    return it == polyPressureSlots_.end() ? nullptr : &it->events;
}

void ExpressionContext::flushEvents() noexcept
{
    auto flush = [](EventVector& events) {
        events.front() = { 0, events.back().value };
        events.resize(1);
    };
    for (ControllerSlot& slot : controllerSlots_) {
        if (slot.assigned)
            flush(slot.events);
    }
    for (PolyPressureSlot& slot : polyPressureSlots_) {
        if (slot.noteNumber >= 0)
            flush(slot.events);
    }
    flush(pitchEvents_);
    flush(pressureEvents_);
    flush(timbreEvents_);
}

void ExpressionContext::reset() noexcept
{
    if (controllerScalarState_) {
        controllerScalarState_->values.fill(0.0f);
        controllerScalarState_->present.fill(false);
    }
    pitchValue_ = 0.0f;
    pressureValue_ = 0.0f;
    timbreValue_ = 0.0f;
    pitchPresent_ = false;
    pressurePresent_ = false;
    timbrePresent_ = false;
    overflowCount_ = 0;

    for (ControllerSlot& slot : controllerSlots_) {
        slot.value = 0.0f;
        slot.present = false;
        resetTimeline(slot.events, 0.0f);
    }
    for (PolyPressureSlot& slot : polyPressureSlots_) {
        slot.noteNumber = -1;
        slot.value = 0.0f;
        slot.present = false;
        resetTimeline(slot.events, 0.0f);
    }
    resetTimeline(pitchEvents_, 0.0f);
    resetTimeline(pressureEvents_, 0.0f);
    resetTimeline(timbreEvents_, 0.0f);
}

size_t ExpressionContext::configuredControllerCount() const noexcept
{
    return static_cast<size_t>(std::count_if(
        controllerSlots_.begin(), controllerSlots_.end(),
        [](const ControllerSlot& slot) { return slot.assigned; }));
}

} // namespace sfz
