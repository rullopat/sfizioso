// SPDX-License-Identifier: BSD-2-Clause

#include "sfizz/Layer.h"
#include "sfizz/MidiState.h"
#include "sfizz/Region.h"
#include "sfizz/Synth.h"
#include "sfizz.h"
#include "sfizz.hpp"
#include "TestHelpers.h"
#include "catch2/catch.hpp"
#include <algorithm>
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
