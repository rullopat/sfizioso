// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once
#include "Region.h"
#include "Config.h"
#include "MidiIdentity.h"
#include "utility/NumericId.h"
#include "utility/LeakDetector.h"
#include <absl/strings/string_view.h>
#include <utility>
#include <string>
#include <array>
#include <vector>
#include <bitset>
#include <memory>

namespace sfz {
struct Region;
class MidiState;

struct Layer {
public:
    /**
     * @brief Initialize a layer based on a new default region.
     */
    Layer(int regionNumber, absl::string_view defaultPath, const MidiState& midiState);

    /**
     * @brief Initialize a layer based on a copy of the contents of a region.
     */
    Layer(const Region& region, const MidiState& midiState);

    ~Layer();

    /**
     * @brief Get the region that this layer operates on.
     */
    const Region& getRegion() const noexcept { return region_; }
    /**
     * @brief Get the region that this layer operates on.
     */
    Region& getRegion() noexcept { return region_; }

    /**
     * @brief Reset the activations to their initial states.
     */
    void initializeActivations();

    /**
     * @brief Given the current midi state, is the region switched on?
     *
     * @return true
     * @return false
     */
    bool isSwitchedOn(SourceAddress source) const noexcept;
    bool isSwitchedOn(int sourceChannel = 0) const noexcept
    {
        return isSwitchedOn(SourceAddress::fromMidi1(sourceChannel));
    }
    bool isSourceChannelEligible(int sourceChannel) const noexcept;
    /**
     * @brief Register a new note on event. The region may be switched on or off using keys so
     * this function updates the keyswitches state.
     *
     * @param noteNumber
     * @param velocity
     * @param randValue a random value between 0 and 1 used to randomize a bit the region activations
     *                  and vary the samples
     * @return true if the region should trigger on this event.
     * @return false
     */
    bool registerNoteOn(int noteNumber, float velocity, float randValue,
        SourceAddress source) noexcept;
    bool registerNoteOn(int noteNumber, float velocity, float randValue,
        int sourceChannel = 0) noexcept
    {
        return registerNoteOn(noteNumber, velocity, randValue,
            SourceAddress::fromMidi1(sourceChannel));
    }
    /**
     * @brief Register a new note off event. The region may be switched on or off using keys so
     * this function updates the keyswitches state.
     *
     * @param noteNumber
     * @param velocity
     * @param randValue a random value between 0 and 1 used to randomize a bit the region activations
     *                  and vary the samples
     * @return true if the region should trigger on this event.
     * @return false
     */
    bool registerNoteOff(int noteNumber, float velocity, float randValue,
        SourceAddress source, int expressionChannel = 0,
        NoteInstanceId noteId = { }) noexcept;
    bool registerNoteOff(int noteNumber, float velocity, float randValue,
        int sourceChannel = 0, int expressionChannel = 0,
        NoteInstanceId noteId = { }) noexcept
    {
        return registerNoteOff(noteNumber, velocity, randValue,
            SourceAddress::fromMidi1(sourceChannel), expressionChannel, noteId);
    }
    /**
     * @brief Update the internal state of the layer with respect to CC events (sustain, CC
     *  switch, etc).
     *
     * @param ccNumber
     * @param ccValue
     * @return false
     */
    void updateCCState(int ccNumber, float ccValue, int sourceChannel = 0,
        int expressionChannel = 0) noexcept;
    /**
     * @brief Register a new CC event,. This method updates the internal CC state with respect
     * to CC events (sustain, CC switch, etc) and checks if the region should trigger on this
     * event.
     *
     * @param ccNumber
     * @param ccValue
     * @param randValue
     * @param extendedArg is used for special extendedCCs (eg. polyaftertouch to represent note num, etc)
     * @return true if the region should trigger on this event
     * @return false otherwise
     */
    bool registerCC(int ccNumber, float ccValue, float randValue,
        int extendedArg, SourceAddress source,
        int expressionChannel = 0) noexcept;
    bool registerCC(int ccNumber, float ccValue, float randValue,
        int extendedArg = -1, int sourceChannel = 0,
        int expressionChannel = 0) noexcept
    {
        return registerCC(ccNumber, ccValue, randValue, extendedArg,
            SourceAddress::fromMidi1(sourceChannel), expressionChannel);
    }
    /**
     * @brief Register a new pitch wheel event.
     *
     * @param pitch
     */
    void registerPitchWheel(float pitch, int sourceChannel = 0) noexcept;
    /**
     * @brief Register a new aftertouch event.
     *
     * @param aftertouch
     */
    void registerAftertouch(float aftertouch, int sourceChannel = 0) noexcept;
    /**
     * @brief Register tempo
     *
     * @param secondsPerQuarter
     */
    void registerTempo(float secondsPerQuarter) noexcept;
    /**
     * @brief Register program change
     *
     * @param secondsPerQuarter
     */
    void registerProgramChange(int program) noexcept;

    void setKeySwitched(int sourceChannel, bool value) noexcept;
    void setPreviousKeySwitched(int sourceChannel, bool value) noexcept;
    bool isCcSwitchedOn(int sourceChannel) const noexcept;

    struct DelayedRelease {
        int noteNumber;
        float velocity;
        int sourceChannel { 0 };
        int expressionChannel { 0 };
        NoteInstanceId noteId { };

        bool operator==(const DelayedRelease& other) const noexcept
        {
            return noteNumber == other.noteNumber
                && velocity == other.velocity
                && sourceChannel == other.sourceChannel
                && expressionChannel == other.expressionChannel
                && noteId == other.noteId;
        }
    };

    struct SourceActivationState {
        bool keySwitched { false };
        bool previousKeySwitched { false };
        bool sequenceSwitched { false };
        bool pitchSwitched { true };
        bool aftertouchSwitched { true };
        bool sustainPressed { false };
        bool sostenutoPressed { false };
        std::bitset<config::numCCs> ccSwitched;
        int sequenceCounter { 0 };
    };

    // Started notes. The legacy fields remain the storage for omni regions;
    // sourceStates_/sourceDelayed* are used only by effective lochan/hichan
    // restrictions, preserving ordinary SFZ behavior and memory cost.
    bool sustainPressed_ { false };
    bool sostenutoPressed_ { false };
    std::vector<DelayedRelease> delayedSustainReleases_;
    std::vector<DelayedRelease> delayedSostenutoReleases_;
    std::unique_ptr<std::array<SourceActivationState, 16>> sourceStates_;
    std::vector<DelayedRelease> sourceDelayedSustainReleases_;
    std::vector<DelayedRelease> sourceDelayedSostenutoReleases_;

    void reserveDelayedReleaseCapacity(size_t capacity);
    void delaySustainRelease(int noteNumber, float velocity,
        int sourceChannel = 0, int expressionChannel = 0,
        NoteInstanceId noteId = { }) noexcept;
    void delaySostenutoRelease(int noteNumber, float velocity,
        int sourceChannel = 0, int expressionChannel = 0,
        NoteInstanceId noteId = { }) noexcept;
    void storeSostenutoNotes(int sourceChannel = 0, int expressionChannel = 0) noexcept;
    void removeFromSostenutoReleases(int noteNumber, int sourceChannel = 0) noexcept;
    bool isNoteSustained(int noteNumber, int sourceChannel = 0) const noexcept;
    bool isNoteSostenutoed(int noteNumber, int sourceChannel = 0) const noexcept;
    bool isSustainPressed(int sourceChannel = 0) const noexcept;
    bool isSostenutoPressed(int sourceChannel = 0) const noexcept;

    const MidiState& midiState_;
    bool keySwitched_ { };
    bool previousKeySwitched_ { };
    bool sequenceSwitched_ { };
    bool pitchSwitched_ { };
    bool programSwitched_ { };
    bool bpmSwitched_ { };
    bool aftertouchSwitched_ { };
    std::bitset<config::numCCs> ccSwitched_;

    int sequenceCounter_ { 0 };

    Region region_;

    LEAK_DETECTOR(Layer);
};

} // namespace sfz
