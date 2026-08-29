// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once

#include "Config.h"
#include "MidiIdentity.h"
#include "SfzHelpers.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sfz {

/**
 * @brief One protocol-neutral expression scope with bounded event timelines.
 *
 * configure() and configureSfizzControllers() are control-thread operations.
 * Event insertion, flushing and scalar reads neither allocate nor lock. SFZ
 * controllers use a fixed lookup table into a dense set of timeline slots;
 * controls which are not consumed by the loaded instrument retain scalar
 * state but do not consume a sample-accurate timeline.
 */
class ExpressionContext {
public:
    static constexpr int unassignedControl = -1;

    void configure(size_t controllerSlots, size_t polyPressureSlots,
        size_t eventsPerTimeline, bool retainAllControllerScalars = true);
    void setTimelineEventCapacity(size_t eventsPerTimeline);
    void configureSfizzControllers(const std::array<bool, config::numCCs>& used,
        size_t compatibilitySlots = 0);

    bool controllerEvent(int delay, int sfizzCC, float value) noexcept;
    bool pitchEvent(int delay, float semitones) noexcept;
    bool pressureEvent(int delay, float value) noexcept;
    bool timbreEvent(int delay, float value) noexcept;
    bool polyPressureEvent(int delay, int noteNumber, float value) noexcept;

    float controllerValue(int sfizzCC) const noexcept;
    float pitchValue() const noexcept { return pitchValue_; }
    float pressureValue() const noexcept { return pressureValue_; }
    float timbreValue() const noexcept { return timbreValue_; }
    float polyPressureValue(int noteNumber) const noexcept;

    bool hasController(int sfizzCC) const noexcept;
    bool hasPitch() const noexcept { return pitchPresent_; }
    bool hasPressure() const noexcept { return pressurePresent_; }
    bool hasTimbre() const noexcept { return timbrePresent_; }
    bool hasPolyPressure(int noteNumber) const noexcept;

    const EventVector* controllerEvents(int sfizzCC) const noexcept;
    const EventVector& pitchEvents() const noexcept { return pitchEvents_; }
    const EventVector& pressureEvents() const noexcept { return pressureEvents_; }
    const EventVector& timbreEvents() const noexcept { return timbreEvents_; }
    const EventVector* polyPressureEvents(int noteNumber) const noexcept;

    void flushEvents() noexcept;
    void reset() noexcept;

    uint64_t overflowCount() const noexcept { return overflowCount_; }
    size_t configuredControllerCount() const noexcept;
    size_t controllerSlotCount() const noexcept { return controllerSlots_.size(); }
    size_t timelineEventCapacity() const noexcept { return eventsPerTimeline_; }

private:
    struct ControllerSlot {
        ExpressionControlId id;
        EventVector events;
        float value { 0.0f };
        bool present { false };
        bool assigned { false };
    };

    struct PolyPressureSlot {
        int noteNumber { -1 };
        EventVector events;
        float value { 0.0f };
        bool present { false };
    };

    struct ControllerScalarState {
        std::array<float, config::numCCs> values { };
        std::array<bool, config::numCCs> present { };
    };

    static void seedTimeline(EventVector& events, size_t capacity, float value);
    static void resetTimeline(EventVector& events, float value) noexcept;
    bool insertEvent(EventVector& events, int delay, float value) noexcept;
    ControllerSlot* assignControllerSlot(int sfizzCC) noexcept;
    PolyPressureSlot* assignPolyPressureSlot(int noteNumber) noexcept;

    std::vector<ControllerSlot> controllerSlots_;
    std::vector<PolyPressureSlot> polyPressureSlots_;
    std::unique_ptr<std::array<int16_t, config::numCCs>> controllerLookup_;
    std::unique_ptr<ControllerScalarState> controllerScalarState_;

    EventVector pitchEvents_;
    EventVector pressureEvents_;
    EventVector timbreEvents_;
    float pitchValue_ { 0.0f };
    float pressureValue_ { 0.0f };
    float timbreValue_ { 0.0f };
    bool pitchPresent_ { false };
    bool pressurePresent_ { false };
    bool timbrePresent_ { false };

    size_t eventsPerTimeline_ { 1 };
    uint64_t overflowCount_ { 0 };
};

} // namespace sfz
