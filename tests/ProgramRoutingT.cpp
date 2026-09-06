// SPDX-License-Identifier: BSD-2-Clause

#include "sfizz/Layer.h"
#include "sfizz/MidiState.h"
#include "sfizz/Region.h"
#include "sfizz/Synth.h"
#include "sfizz/Voice.h"
#include "sfizz.h"
#include "sfizz.hpp"
#include "TestHelpers.h"
#include "catch2/catch.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ProgramSynth {
    sfz::Synth synth;
    sfz::AudioBuffer<float> buffer {
        2, static_cast<unsigned>(synth.getSamplesPerBlock())
    };

    void load(const char* name, const char* text)
    {
        REQUIRE(synth.loadSfzString(fs::current_path() / name, text));
    }

    void render()
    {
        buffer.clear();
        synth.renderBlock(buffer);
    }
};

int countPlayingSample(const sfz::Synth& synth, const std::string& sample)
{
    const auto samples = playingSamples(synth);
    return static_cast<int>(std::count(samples.begin(), samples.end(), sample));
}

constexpr const char* scopedProgramSfz = R"(
    <region> lochan=1 hichan=1 loprog=5 hiprog=5 sample=*sine
    <region> lochan=2 hichan=2 loprog=7 hiprog=7 sample=*saw
    <region> loprog=7 hiprog=7 sample=*tri
)";

} // namespace

TEST_CASE("[Program routing] source channels retain independent program conditions")
{
    ProgramSynth f;
    f.load("program_source_independent.sfz", scopedProgramSfz);

    f.synth.programChange(0, 0, 5);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 0);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 0);

    f.synth.programChange(0, 1, 7);
    f.synth.noteOn(0, 0, 62, 100);
    f.synth.noteOn(0, 1, 63, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    // Omni regions intentionally observe the latest legacy global view.
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2);
}

TEST_CASE("[Program routing] channel-less global Program Change updates every source")
{
    ProgramSynth f;
    f.load("program_global_broadcast.sfz", scopedProgramSfz);

    f.synth.programChange(0, 0, 5);
    f.synth.programChange(0, 1, 7);
    f.synth.programChange(0, 7);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();

    REQUIRE(countPlayingSample(f.synth, "*sine") == 0);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2);
}

TEST_CASE("[Program routing] MPE Manager updates the zone and Mode-3 Member is dropped")
{
    ProgramSynth f;
    f.synth.setMPEEnabled(true);
    f.load("program_mpe_manager.sfz", scopedProgramSfz);

    f.synth.programChange(0, 0, 7);
    f.synth.programChange(1, 1, 5); // dropped Member Program Change
    f.synth.noteOn(2, 0, 60, 100);
    f.synth.noteOn(2, 1, 61, 100);
    f.render();

    REQUIRE(countPlayingSample(f.synth, "*sine") == 0);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2);
}

TEST_CASE("[Program routing] scoped state survives reload reset and MPE transitions")
{
    ProgramSynth f;
    f.synth.programChange(0, 0, 5);
    f.synth.programChange(0, 1, 7);
    f.load("program_reload.sfz", scopedProgramSfz);

    f.synth.allSoundOff();
    f.synth.setMPEEnabled(true);
    f.synth.setMPEEnabled(false);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();

    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2);
}

TEST_CASE("[Program routing] group and channel form the bounded source key")
{
    sfz::MidiState state;
    state.programChangeEvent(0,
        sfz::RoutingTarget::channel({ 1, 1 }), 7);

    REQUIRE(state.getProgram(sfz::SourceAddress { 1, 1 }) == 7);
    REQUIRE(state.getProgram(sfz::SourceAddress { 2, 1 }) == 0);
    REQUIRE(state.getProgram() == 7);

    sfz::Region region { 0 };
    REQUIRE(region.parseOpcode({ "lochan", "2" }));
    REQUIRE(region.parseOpcode({ "hichan", "2" }));
    REQUIRE(region.parseOpcode({ "loprog", "7" }));
    REQUIRE(region.parseOpcode({ "hiprog", "7" }));
    sfz::Layer layer { region, state };
    REQUIRE(layer.registerNoteOn(60, 1.0f, 0.5f,
        sfz::SourceAddress { 1, 1 }));
    REQUIRE_FALSE(layer.registerNoteOn(60, 1.0f, 0.5f,
        sfz::SourceAddress { 2, 1 }));
}

TEST_CASE("[Program routing] scoped Program Change composes with source CC conditions")
{
    ProgramSynth f;
    f.load("program_cc_composition.sfz", R"(
        <region> lochan=2 hichan=2 loprog=7 hiprog=7 locc32=4 hicc32=4 sample=*sine
    )");

    f.synth.programChange(0, 1, 7);
    f.synth.cc(0, 1, 32, 4);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);

    f.synth.cc(0, 1, 32, 3);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
}

TEST_CASE("[Program routing] public wrappers expose exact scoped boundary values")
{
    SECTION("C++")
    {
        sfz::Sfizz synth;
        REQUIRE(synth.loadSfzString("program_cpp_wrapper.sfz", R"(
            <region> lochan=2 hichan=2 loprog=127 hiprog=127 sample=*sine
        )"));
        synth.programChange(0, 1, 127);
        synth.noteOn(0, 1, 60, 100);
        std::vector<float> left(256), right(256);
        float* outputs[] { left.data(), right.data() };
        synth.renderBlock(outputs, left.size());
        REQUIRE(synth.getNumActiveVoices() == 1);
    }

    SECTION("C")
    {
        sfizz_synth_t* synth = sfizz_create_synth();
        REQUIRE(synth != nullptr);
        REQUIRE(sfizz_load_string(synth, "program_c_wrapper.sfz", R"(
            <region> lochan=3 hichan=3 loprog=0 hiprog=0 sample=*sine
        )"));
        sfizz_send_program_change_channel(synth, 0, 2, 0);
        sfizz_send_note_on_channel(synth, 0, 2, 60, 100);
        std::vector<float> left(256), right(256);
        float* outputs[] { left.data(), right.data() };
        sfizz_render_block(synth, outputs, 2, static_cast<int>(left.size()));
        REQUIRE(sfizz_get_num_active_voices(synth) == 1);
        sfizz_free(synth);
    }
}

TEST_CASE("[Program routing] MPE program switches preserve held note ownership and release eligibility")
{
    const bool sustained = GENERATE(false, true);
    const char* releaseTrigger = GENERATE("release_key", "release");
    CAPTURE(sustained, releaseTrigger);
    ProgramSynth f;
    f.synth.setMPEEnabled(true);
    const std::string sfz = std::string(R"(
        <global> lochan=2 hichan=3 ampeg_release=0.02
        <region> loprog=5 hiprog=5 sample=*sine
        <region> loprog=7 hiprog=7 sample=*saw
        <group> trigger=)") + releaseTrigger + R"( rt_dead=on ampeg_sustain=0 ampeg_decay=0.01
        <region> loprog=5 hiprog=5 sample=*tri
        <region> loprog=7 hiprog=7 sample=*square
    )";
    f.load("program_mpe_held.sfz", sfz.c_str());
    f.synth.programChange(0, 0, 5);
    if (sustained)
        f.synth.cc(0, 0, 64, 127);
    f.synth.noteOn(0, 1, 60, 100);
    f.synth.noteOn(0, 2, 60, 100);
    f.render();
    const auto held = getPlayingVoices(f.synth);
    REQUIRE(held.size() == 2);
    const auto first = held[0]->getTriggerEvent();
    const auto second = held[1]->getTriggerEvent();
    REQUIRE(first.source.channel == 1);
    REQUIRE(second.source.channel == 2);
    REQUIRE(first.noteId != second.noteId);

    f.synth.programChange(0, 0, 7);
    f.synth.programChange(1, 1, 5); // Must not undo the Manager's selection.
    f.synth.noteOn(2, 1, 62, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    REQUIRE(held[0]->getTriggerEvent().noteId == first.noteId);
    REQUIRE(held[1]->getTriggerEvent().noteId == second.noteId);

    f.synth.noteOff(0, 1, 60, 0);
    REQUIRE_FALSE(held[1]->released());
    f.synth.noteOff(1, 2, 60, 0);
    // Selection is evaluated at Note Off, using the current program (7),
    // even though the attack started under program 5.
    REQUIRE(countPlayingSample(f.synth, "*tri") == 0);
    const bool delayed = sustained && std::string(releaseTrigger) == "release";
    REQUIRE(countPlayingSample(f.synth, "*square") == (delayed ? 0 : 2));
    if (sustained)
        f.synth.cc(2, 0, 64, 0);
    REQUIRE(held[0]->released());
    REQUIRE(held[1]->released());
    REQUIRE(countPlayingSample(f.synth, "*square") == 2);
    for (const auto* voice : getPlayingVoices(f.synth)) {
        if (voice->getRegion()->sampleId->filename() != "*square")
            continue;
        const auto& release = voice->getTriggerEvent();
        REQUIRE(release.number == 60);
        REQUIRE(release.channel == release.source.channel);
        REQUIRE(release.noteId == (release.source.channel == 1 ? first.noteId : second.noteId));
    }
    f.render();
    f.synth.noteOff(0, 1, 62, 0);
    for (int i = 0; i < 200; ++i)
        f.render();
    REQUIRE(f.synth.getNumActiveVoices() == 0);
}

TEST_CASE("[Program routing] MPE queued sustain releases survive a subsequent program switch")
{
    ProgramSynth f;
    f.synth.setMPEEnabled(true);
    f.load("program_mpe_queued_release.sfz", R"(
        <global> lochan=2 hichan=3 ampeg_release=0.02
        <region> sample=*sine
        <group> trigger=release rt_dead=on ampeg_sustain=0 ampeg_decay=0.01
        <region> loprog=5 hiprog=5 sample=*tri
        <region> loprog=7 hiprog=7 sample=*saw
    )");
    f.synth.programChange(0, 0, 5);
    f.synth.cc(0, 0, 64, 127);
    f.synth.noteOn(0, 1, 60, 100);
    f.synth.noteOn(0, 2, 60, 100);
    f.render();
    f.synth.noteOff(0, 1, 60, 0);
    f.synth.noteOff(1, 2, 60, 0);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 0);
    f.synth.programChange(2, 0, 7);
    f.synth.cc(3, 0, 64, 0);
    // Once queued at Note Off, program 5 releases are not reselected at pedal-up.
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 0);
    f.render();
    f.synth.cc(0, 0, 64, 0);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2); // No duplicate queue drain.
    for (int i = 0; i < 200; ++i)
        f.render();
    REQUIRE(f.synth.getNumActiveVoices() == 0);
}

TEST_CASE("[Program routing] MPE overlapping channel ranges compose with note and Manager expression")
{
    ProgramSynth f;
    f.synth.setMPEEnabled(true);
    f.synth.setMPEPitchBendRange(2, 24);
    f.load("program_mpe_expression.sfz", R"(
        <global> cutoff_oncc74=1000
        <region> lochan=2 hichan=3 loprog=7 hiprog=7 sample=*sine
        <region> lochan=3 hichan=4 loprog=7 hiprog=7 sample=*saw
        <region> lochan=2 hichan=4 loprog=5 hiprog=5 sample=*tri
    )");
    f.synth.programChange(0, 0, 7);
    for (int channel = 0; channel <= 4; ++channel)
        f.synth.noteOn(0, channel, 60, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 2);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 0);

    f.synth.hdPitchWheel(3, 0, 0.5f);
    f.synth.hdChannelAftertouch(4, 0, 0.25f);
    f.synth.hdcc(5, 0, 74, 0.25f);
    f.synth.hdPitchWheel(7, 2, 0.5f);
    f.synth.hdChannelAftertouch(8, 2, 0.75f);
    f.synth.hdcc(9, 2, 74, 0.75f);
    const auto& state = f.synth.getResources().getMidiState();
    for (const auto* voice : getPlayingVoices(f.synth)) {
        const auto& trigger = voice->getTriggerEvent();
        const bool expressed = trigger.source.channel == 2;
        REQUIRE(trigger.expressionTarget == sfz::ExpressionTarget::zone(0));
        REQUIRE(state.getVoiceBroadPitchEvents(trigger.expressionTarget).back().value == 1.0f);
        REQUIRE(state.getVoiceNotePitchEvents(trigger.noteId).back().value == (expressed ? 12.0f : 0.0f));
        REQUIRE(state.getVoicePressureEvents(trigger.expressionTarget, trigger.noteId).back().value
            == (expressed ? 0.75f : 0.25f));
        REQUIRE(state.getVoiceCCEvents(trigger.expressionTarget, trigger.noteId, 74).back().value
            == (expressed ? 0.75f : 0.25f));
    }
}

TEST_CASE("[Program routing] MPE program state survives explicit and RPN transitions")
{
    const bool rpn = GENERATE(false, true);
    CAPTURE(rpn);
    ProgramSynth f;
    f.load("program_mpe_transitions.sfz", R"(
        <region> lochan=2 hichan=2 loprog=5 hiprog=5 sample=*sine
        <region> lochan=3 hichan=3 loprog=7 hiprog=7 sample=*saw
        <region> lochan=2 hichan=3 loprog=9 hiprog=9 sample=*tri
    )");
    auto setMpe = [&](bool enabled) {
        if (rpn) {
            f.synth.cc(0, 0, 101, 0);
            f.synth.cc(1, 0, 100, 6);
            f.synth.cc(2, 0, 6, enabled ? 15 : 0);
        } else {
            f.synth.setMPEEnabled(enabled);
        }
        REQUIRE(f.synth.getMPEEnabled() == enabled);
    };
    f.synth.programChange(0, 1, 5);
    f.synth.programChange(0, 2, 7);
    setMpe(true);
    f.synth.programChange(3, 0, 9);
    f.synth.programChange(4, 1, 5);
    f.synth.noteOn(5, 1, 60, 100);
    f.synth.noteOn(5, 2, 60, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2);
    REQUIRE(numPlayingVoices(f.synth) == 2);

    // Probe program persistence independently of the two APIs' voice-flush policy.
    f.synth.allSoundOff();
    setMpe(false);
    f.synth.noteOn(5, 1, 62, 100);
    f.synth.noteOn(5, 2, 62, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*tri") == 2);
    f.synth.allSoundOff();
    f.synth.programChange(0, 1, 5); // Accepted again and scoped to this source.
    f.synth.noteOn(1, 1, 64, 100);
    f.synth.noteOn(1, 2, 64, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 1);
    REQUIRE(numPlayingVoices(f.synth) == 2);

    f.synth.allSoundOff();
    setMpe(true);
    f.synth.programChange(3, 0, 7);
    f.synth.programChange(4, 2, 9);
    f.synth.noteOn(5, 1, 65, 100);
    f.synth.noteOn(5, 2, 65, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    REQUIRE(numPlayingVoices(f.synth) == 1);
}

TEST_CASE("[Program routing] reused MPE Member after program switch cannot modulate the old tail")
{
    ProgramSynth f;
    f.synth.setMPEEnabled(true);
    f.synth.setMPEPitchBendRange(2, 24);
    f.load("program_mpe_reuse.sfz", R"(
        <global> lochan=2 hichan=2 ampeg_release=1 cutoff_oncc74=1000
        <region> loprog=5 hiprog=5 sample=*sine
        <region> loprog=7 hiprog=7 sample=*saw
    )");
    f.synth.programChange(0, 0, 5);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    const auto* oldVoice = getPlayingVoices(f.synth).front();
    const auto old = oldVoice->getTriggerEvent();
    f.synth.hdPitchWheel(0, 1, 0.25f);
    f.synth.hdChannelAftertouch(0, 1, 0.5f);
    f.synth.hdcc(0, 1, 74, 0.5f);
    f.synth.noteOff(1, 1, 60, 0);
    f.synth.programChange(2, 0, 7);
    f.synth.noteOn(3, 1, 60, 100);
    f.render();
    REQUIRE(oldVoice->released());
    REQUIRE(oldVoice->getRegion()->sampleId->filename() == "*sine");
    REQUIRE(f.synth.getNumActiveVoices() == 2);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    const auto current = getPlayingVoices(f.synth).front()->getTriggerEvent();
    REQUIRE(current.noteId != old.noteId);
    REQUIRE(current.source == old.source);
    REQUIRE(current.number == old.number);

    f.synth.hdPitchWheel(4, 0, 0.5f);
    f.synth.hdChannelAftertouch(5, 0, 0.25f);
    f.synth.hdcc(6, 0, 74, 0.25f);
    f.synth.hdPitchWheel(7, 1, 0.75f);
    f.synth.hdChannelAftertouch(8, 1, 0.75f);
    f.synth.hdcc(9, 1, 74, 0.75f);
    const auto& state = f.synth.getResources().getMidiState();
    REQUIRE(state.getExpressionContext(sfz::ExpressionTarget::note(old.noteId)) == nullptr);
    REQUIRE(state.getVoiceNotePitchEvents(old.noteId).back().value == 0.0f);
    REQUIRE(state.getVoiceBroadPitchEvents(old.expressionTarget).back().value == 1.0f);
    REQUIRE(state.getVoicePressureEvents(old.expressionTarget, old.noteId).back().value == 0.25f);
    REQUIRE(state.getVoiceCCEvents(old.expressionTarget, old.noteId, 74).back().value == 0.25f);
    REQUIRE(state.getVoiceNotePitchEvents(current.noteId).back().value == 18.0f);
    REQUIRE(state.getVoiceBroadPitchEvents(current.expressionTarget).back().value == 1.0f);
    REQUIRE(state.getVoicePressureEvents(current.expressionTarget, current.noteId).back().value == 0.75f);
    REQUIRE(state.getVoiceCCEvents(current.expressionTarget, current.noteId, 74).back().value == 0.75f);
    f.render();
    f.synth.noteOff(0, 1, 60, 0);
    for (int i = 0; i < 500; ++i)
        f.render();
    REQUIRE(f.synth.getNumActiveVoices() == 0);
}

TEST_CASE("[Program routing] C and C++ MPE wrappers preserve Manager scope and channel boundaries")
{
    const bool cApi = GENERATE(false, true);
    CAPTURE(cApi);
    sfz::Sfizz cpp;
    const auto c = std::unique_ptr<sfizz_synth_t, decltype(&sfizz_free)>(sfizz_create_synth(), sfizz_free);
    REQUIRE(c != nullptr);
    const char* sfz = R"(
        <region> lochan=2 hichan=2 loprog=127 hiprog=127 sample=*sine
        <region> lochan=16 hichan=16 loprog=127 hiprog=127 sample=*saw
        <region> lochan=2 hichan=2 loprog=0 hiprog=0 sample=*tri
    )";
    if (cApi) {
        sfizz_set_mpe_enabled(c.get(), true);
        REQUIRE(sfizz_load_string(c.get(), "program_c_mpe.sfz", sfz));
    } else {
        cpp.setMPEEnabled(true);
        REQUIRE(cpp.loadSfzString("program_cpp_mpe.sfz", sfz));
    }
    auto program = [&](int delay, int channel, int value) {
        if (cApi) sfizz_send_program_change_channel(c.get(), delay, channel, value);
        else cpp.programChange(delay, channel, value);
    };
    auto note = [&](int delay, int channel) {
        if (cApi) sfizz_send_note_on_channel(c.get(), delay, channel, 60, 100);
        else cpp.noteOn(delay, channel, 60, 100);
    };
    auto renderAndCount = [&]() {
        std::vector<float> left(256), right(256);
        float* outputs[] { left.data(), right.data() };
        if (cApi) {
            sfizz_render_block(c.get(), outputs, 2, 256);
            return sfizz_get_num_active_voices(c.get());
        }
        cpp.renderBlock(outputs, 256);
        return cpp.getNumActiveVoices();
    };
    program(0, 0, 127);
    program(1, 15, 0); // Even the highest Member cannot change the program.
    note(2, 0); // Manager and excluded Member channels match neither range.
    note(2, 2);
    note(2, 1);
    note(2, 15);
    REQUIRE(renderAndCount() == 2);
    if (cApi) sfizz_all_sound_off(c.get());
    else cpp.allSoundOff();
    program(0, 0, 0);
    program(1, 1, 127);
    note(2, 1);
    note(2, 15);
    REQUIRE(renderAndCount() == 1);
}
