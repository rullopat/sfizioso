// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once
#include <array>
#include <bitset>
#include <vector>
#include "CCMap.h"
#include "ExpressionContext.h"
#include "ExpressionEvent.h"
#include "Range.h"

namespace sfz {
/**
 * @brief Holds the current "MIDI state", meaning the known state of all CCs
 * currently, as well as the note velocities that triggered the currently
 * pressed notes.
 *
 */
class MidiState {
public:
    MidiState();
    /** Bounded note history; voices reserve the same capacity for release. */
    static constexpr size_t noteTimelineEvents = 65;

    /**
     * @brief Update the state after a note on event
     *
     * @param delay
     * @param noteNumber
     * @param velocity
     */
    void noteOnEvent(int delay, int noteNumber, float velocity) noexcept;

    /**
     * @brief Update the state after a note off event
     *
     * @param delay
     * @param noteNumber
     * @param velocity
     */
    void noteOffEvent(int delay, int noteNumber, float velocity) noexcept;

    /**
     * @brief Track note eligibility state on the original source channel.
     *
     * This state is separate from the legacy/global note state above. It is
     * used only by channel-restricted SFZ regions, so MPE-off expression
     * routing can still collapse to channel 0 without losing lochan/hichan
     * note ownership, velocity and legato state.
     */
    void sourceNoteOnEvent(int channel, int noteNumber, float velocity) noexcept;
    void sourceNoteOffEvent(int channel, int noteNumber) noexcept;
    int getSourceActiveNotes(int channel) const noexcept;
    float getSourceNoteVelocity(int channel, int noteNumber) const noexcept;
    float getSourceVelocityOverride(int channel) const noexcept;
    bool isSourceNotePressed(int channel, int noteNumber) const noexcept;

    /**
     * @brief Track/read Polyphonic Key Pressure on its accepted source channel.
     * Kept separate from expression state for MPE-off fixed-channel routing.
     */
    void sourcePolyAftertouchEvent(int channel, int noteNumber, float aftertouch) noexcept;
    float getSourcePolyAftertouch(int channel, int noteNumber) const noexcept;
    void sourcePitchBendEvent(int channel, float pitch) noexcept;
    float getSourcePitchBend(int channel) const noexcept;
    void sourceChannelAftertouchEvent(int channel, float aftertouch) noexcept;
    float getSourceChannelAftertouch(int channel) const noexcept;

    /**
     * @brief Set all notes off
     *
     * @param delay
     */
    void allNotesOff(int delay) noexcept;

    /**
     * @brief Get the number of active notes
     */
    int getActiveNotes() const noexcept { return activeNotes; }

    /**
     * @brief Get the note duration since note on
     *
     * @param noteNumber
     * @param delay
     * @return float
     */
    float getNoteDuration(int noteNumber, int delay = 0) const;

    /**
     * @brief Set the maximum size of the blocks for the callback. The actual
     * size can be lower in each callback but should not be larger
     * than this value.
     *
     * @param samplesPerBlock
     */
    void setSamplesPerBlock(int samplesPerBlock) noexcept;

    /**
     * @brief Densify sample-accurate controller slots for the loaded SFZ.
     * Control-thread only; scalar state for all controller numbers remains.
     */
    void configureExpressionControls(
        const std::array<bool, config::numCCs>& usedControllers);

    /**
     * @brief Preallocate/recycle note-scoped expression contexts alongside
     * the logical-note registry. Configuration is control-thread only.
     */
    void configureNoteExpressionContexts(size_t capacity);
    void beginNoteExpression(NoteInstanceId noteId) noexcept;
    void endNoteExpression(NoteInstanceId noteId) noexcept;
    void clearNoteExpressionContexts() noexcept;
    void resetScopedExpressionContexts() noexcept;
    ExpressionContext* getExpressionContext(ExpressionTarget target) noexcept;
    const ExpressionContext* getExpressionContext(ExpressionTarget target) const noexcept;

    /** Apply one transport-neutral event to its explicit expression target. */
    bool expressionEvent(const ResolvedExpressionEvent& event) noexcept;

    /**
     * Resolve the explicit Note → broad target → Global inheritance policy
     * for voice and modulation consumers. Note expression disappears
     * automatically once its generation-safe context is detached.
     */
    const EventVector& getVoiceCCEvents(ExpressionTarget broadTarget,
        NoteInstanceId noteId, int ccNumber) const noexcept;
    const EventVector& getVoicePressureEvents(ExpressionTarget broadTarget,
        NoteInstanceId noteId) const noexcept;
    const EventVector& getVoicePolyPressureEvents(ExpressionTarget broadTarget,
        NoteInstanceId noteId, int noteNumber) const noexcept;
    const EventVector& getVoiceBroadPitchEvents(ExpressionTarget broadTarget) const noexcept;
    const EventVector& getVoiceNotePitchEvents(NoteInstanceId noteId) const noexcept;

    uint64_t getExpressionOverflowCount() const noexcept;
    /**
     * @brief Set the sample rate. If you do not call it it is initialized
     * to sfz::config::defaultSampleRate.
     *
     * @param sampleRate
     */
    void setSampleRate(float sampleRate) noexcept;
    /**
     * @brief Get the note on velocity for a given note
     *
     * @param noteNumber
     * @return float
     */
    float getNoteVelocity(int noteNumber) const noexcept;

    /**
     * @brief Get the velocity override value (sw_vel in SFZ)
     *
     * @return float
     */
    float getVelocityOverride() const noexcept;

    /**
     * @brief Register a pitch bend event
     *
     * @param pitchBendValue
     */
    void pitchBendEvent(int delay, float pitchBendValue) noexcept;

    /**
     * @brief Register a pitch bend event on a specific MIDI channel.
     * Out-of-range channels are silently ignored. The single-arg
     * overload forwards to this with channel = masterChannel.
     */
    void pitchBendEvent(int delay, int channel, float pitchBendValue) noexcept;

    /**
     * @brief Get the pitch bend status

     * @return int
     */
    float getPitchBend() const noexcept;

    /**
     * @brief Get the pitch bend status for a specific MIDI channel.
     * Out-of-range channels return 0.0f. Used by per-voice consumers
     * (Voice etc.) to read modulation events scoped to the voice's
     * originating channel; master-channel reads via the no-arg overload
     * are unchanged.
     */
    float getPitchBend(int channel) const noexcept;

    /**
     * @brief Register a channel aftertouch event
     *
     * @param aftertouch
     */
    void channelAftertouchEvent(int delay, float aftertouch) noexcept;

    /**
     * @brief Register a channel aftertouch event on a specific MIDI
     * channel. Out-of-range channels are silently ignored.
     */
    void channelAftertouchEvent(int delay, int channel, float aftertouch) noexcept;

    /**
     * @brief Register a channel aftertouch event
     *
     * @param aftertouch
     */
    void polyAftertouchEvent(int delay, int noteNumber, float aftertouch) noexcept;

    /**
     * @brief Register a polyphonic aftertouch event on a specific MIDI
     * channel. Out-of-range channels or notes are silently ignored.
     */
    void polyAftertouchEvent(int delay, int channel, int noteNumber, float aftertouch) noexcept;

    /**
     * @brief Get the channel aftertouch status

     * @return int
     */
    float getChannelAftertouch() const noexcept;

    /**
     * @brief Get the channel aftertouch status for a specific MIDI
     * channel. Out-of-range channels return 0.0f.
     */
    float getChannelAftertouch(int channel) const noexcept;

    /**
     * @brief Get the polyphonic aftertouch status

     * @return int
     */
    float getPolyAftertouch(int noteNumber) const noexcept;

    /**
     * @brief Get the polyphonic aftertouch status for a specific MIDI
     * channel and note. Out-of-range channels or notes return 0.0f.
     */
    float getPolyAftertouch(int channel, int noteNumber) const noexcept;

    /**
     * @brief Get the current midi program
     *
     * @return int
     */
    int getProgram() const noexcept;
    int getProgram(SourceAddress source) const noexcept;
    int getProgram(RoutingTarget target) const noexcept;
    /**
     * @brief Register a global compatibility Program Change.
     *
     * Global changes update every bounded source/zone routing context.
     */
    void programChangeEvent(int delay, int program) noexcept;
    /**
     * @brief Register Program Change on an explicit non-note routing target.
     *
     * Channel targets also update the legacy global view while retaining
     * independent source state. Zone targets update every source in the zone.
     */
    void programChangeEvent(int delay, RoutingTarget target, int program) noexcept;

    /**
     * @brief Register a CC event
     *
     * @param ccNumber
     * @param ccValue
     */
    void ccEvent(int delay, int ccNumber, float ccValue) noexcept;

    /**
     * @brief Register a CC event on a specific MIDI channel.
     * Out-of-range channels are silently ignored.
     */
    void ccEvent(int delay, int channel, int ccNumber, float ccValue) noexcept;

    /**
     * @brief Track/read accepted CC state on the original source channel.
     * This scalar state is separate from expression event vectors so MPE-off
     * lochan/hichan regions can compare CC triggers without changing the
     * legacy channel-0 modulation contract. Defaults are copied to all source
     * channels so a later channel-1 event cannot pollute another channel.
     */
    void sourceCCEvent(int channel, int ccNumber, float ccValue) noexcept;
    float getSourceCCValue(int channel, int ccNumber) const noexcept;
    void resetSourceCCStates() noexcept;

    /**
     * @brief Advances the internal clock of a given amount of samples.
     * You should call this at each callback. This will flush the events
     * in the midistate memory by calling flushEvents().
     *
     * @param numSamples the number of samples of clock advance
     */
    void advanceTime(int numSamples) noexcept;

    /**
     * @brief Returns current internal sample clock
     *
     */
    unsigned getInternalClock() const noexcept { return internalClock; }

    /**
     * @brief Flush events in all states, keeping only the last one as the "base" state
     *
     */
    void flushEvents() noexcept;

    /**
     * @brief Check if a note is currently depressed
     *
     * @param noteNumber
     * @return true
     * @return false
     */
    bool isNotePressed(int noteNumber) const noexcept { return noteStates[noteNumber]; }

    /**
     * @brief Get the last CC value for CC number
     *
     * @param ccNumber
     * @return float
     */
    float getCCValue(int ccNumber) const noexcept;

    /**
     * @brief Get the last CC value for CC number on a specific MIDI
     * channel. Out-of-range channels return 0.0f.
     */
    float getCCValue(int channel, int ccNumber) const noexcept;

    /**
     * @brief Get the CC value for CC number
     *
     * @param ccNumber
     * @param delay
     * @return float
     */
    float getCCValueAt(int ccNumber, int delay) const noexcept;

    /**
     * @brief Get the CC value for CC number on a specific MIDI channel
     * at a given delay. Out-of-range channels return 0.0f.
     */
    float getCCValueAt(int channel, int ccNumber, int delay) const noexcept;

    /**
     * @brief Reset the midi note states
     *
     */
    void resetNoteStates() noexcept;

    const EventVector& getCCEvents(int ccIdx) const noexcept;
    const EventVector& getCCEvents(int channel, int ccIdx) const noexcept;
    const EventVector& getPolyAftertouchEvents(int noteNumber) const noexcept;
    const EventVector& getPolyAftertouchEvents(int channel, int noteNumber) const noexcept;
    const EventVector& getPitchEvents() const noexcept;
    const EventVector& getPitchEvents(int channel) const noexcept;
    /**
     * @brief Return the pitch-bend events for the given channel, with no
     *        master fallback. The vector may be empty if that channel has
     *        never received its own bend events. Use this when combining
     *        member and master bend contributions separately for MPE.
     */
    const EventVector& getPitchEventsRaw(int channel) const noexcept;

    /**
     * @brief Return the latest pitch bend value for the given channel, with
     *        no master fallback. Returns 0 if the channel has never received
     *        its own bend. Pair with getMPEBendRangeForChannel to compute a
     *        per-channel bend contribution.
     */
    float getPitchBendRaw(int channel) const noexcept;
    const EventVector& getChannelAftertouchEvents() const noexcept;
    const EventVector& getChannelAftertouchEvents(int channel) const noexcept;
    /**
     * @brief Reset the midi event states (CC, AT, and pitch bend)
     *
     */
    void resetEventStates() noexcept;

    /**
     * @brief Configure the MPE pitch bend ranges used when computing the
     *        bend amount applied to a voice. Master and member channels
     *        carry independent ranges per MPE 1.0; SFZ regions only have
     *        a single bend_up / bend_down pair, so member channels can't
     *        rely on the region opcodes and need this synth-level setting.
     */
    void setMPEPitchBendRange(float masterSemitones, float perNoteSemitones) noexcept;

    /**
     * @brief Return the bend range (in semitones) the given channel should
     *        use to scale a normalized [-1, +1] pitch bend value. Master
     *        channel returns the master range, member channels return the
     *        per-note range.
     */
    float getMPEBendRangeForChannel(int channel) const noexcept;

private:
    int activeNotes { 0 };

    /**
     * @brief Stores the note on times.
     *
     */
    MidiNoteArray<unsigned> noteOnTimes { { } };

    /**
     * @brief Stores the note off times.
     *
     */

    MidiNoteArray<unsigned> noteOffTimes { { } };

    /**
     * @brief Store the note states
     *
     */
    std::bitset<128> noteStates;

    /**
     * @brief Stores the velocity of the note ons for currently
     * depressed notes.
     *
     */
    MidiNoteArray<float> lastNoteVelocities;

    /**
     * @brief Velocity override value (sw_vel in SFZ)
     */
    float velocityOverride;

    /**
     * @brief Last note played
     */
    int lastNotePlayed { -1 };

    static constexpr int masterChannel = 0;
    static constexpr size_t compatibilityControllerSlots = 8;
    static constexpr size_t compatibilityPolyPressureSlots = 4;

    ExpressionContext& compatibilityContext(int channel) noexcept;
    const ExpressionContext& compatibilityContext(int channel) const noexcept;

    // The compatibility channel-0 API resolves to Global. Zone and Channel
    // contexts remain explicit for adapter/profile milestones; member-channel
    // compatibility calls currently use channelExpressionContexts_[1..15].
    ExpressionContext globalExpressionContext_;
    ExpressionContext lowerZoneExpressionContext_;
    std::array<ExpressionContext, 16> channelExpressionContexts_;

    struct NoteExpressionSlot {
        ExpressionContext context;
        uint16_t generation { 0 };
        bool active { false };
    };
    std::vector<NoteExpressionSlot> noteExpressionSlots_;
    std::bitset<config::numCCs> noteExpressionControllers_;
    uint64_t retiredNoteExpressionOverflowCount_ { 0 };

    struct SourceNoteState {
        std::bitset<128> pressed;
        MidiNoteArray<uint16_t> noteCounts { { } };
        MidiNoteArray<float> velocities { { } };
        MidiNoteArray<float> polyAftertouch { { } };
        int activeNotes { 0 };
        int lastNotePlayed { -1 };
        float velocityOverride { 0.0f };
    };
    std::array<SourceNoteState, 16> sourceNoteStates;
    std::array<std::array<float, config::numCCs>, 16> sourceCCValues { { } };
    std::array<float, 16> sourcePitchBends { { } };
    std::array<float, 16> sourceChannelAftertouch { { } };

    // MPE 1.0 defaults: master = 2 st, per-note = 48 st.
    float mpeMasterPitchBendRange_ { 2.0f };
    float mpePerNotePitchBendRange_ { 48.0f };

    /**
     * @brief Null event
     *
     */
    const EventVector nullEvent { { 0, 0.0f } };

    /**
     * @brief Bounded Program Change routing state.
     *
     * The legacy global scalar is retained. Sixteen explicit groups each own
     * sixteen source-channel slots; this is routing state, not expression
     * controller storage. Zone ids currently map to their protocol group.
     */
    int currentProgram { 0 };
    std::array<int, 16> zonePrograms_ { { } };
    std::array<int, 256> sourcePrograms_ { { } };

    float sampleRate { config::defaultSampleRate };
    int samplesPerBlock { config::defaultSamplesPerBlock };
    float alternate { 0.0f };
    unsigned internalClock { 0 };
    fast_real_distribution<float> unipolarDist { 0.0f, 1.0f };
    fast_real_distribution<float> bipolarDist { -1.0f, 1.0f };
};
}
