// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

/**
 * SMPL-93 fixed-channel routing tests.
 *
 * These tests deliberately cover parser/model behavior, MPE None and Full,
 * source-scoped articulation and switch state, voice lifetime, release
 * triggers, pedal-delayed releases, off-groups and legacy omni compatibility.
 */

#include "sfizz/Layer.h"
#include "sfizz/MidiState.h"
#include "sfizz/Region.h"
#include "sfizz/Synth.h"
#include "sfizz/Voice.h"
#include "sfizz/SfzHelpers.h"
#include "sfizz.h"
#include "sfizz.hpp"
#include "TestHelpers.h"
#include "catch2/catch.hpp"
#include <algorithm>
#include <string>
#include <vector>

using namespace sfz::literals;

namespace {

struct RoutingSynth {
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

std::vector<int> playingSourceChannels(const sfz::Synth& synth)
{
    std::vector<int> channels;
    for (const sfz::Voice* voice : getPlayingVoices(synth))
        channels.push_back(voice->getTriggerEvent().sourceChannel);
    std::sort(channels.begin(), channels.end());
    return channels;
}

} // namespace

TEST_CASE("[Channel routing] lochan and hichan parse strictly")
{
    sfz::Region region { 0 };
    REQUIRE(region.channelRange.getStart() == 1);
    REQUIRE(region.channelRange.getEnd() == 16);
    REQUIRE_FALSE(region.isChannelRestricted());

    REQUIRE(region.parseOpcode({ "lochan", "3" }));
    REQUIRE(region.parseOpcode({ "hichan", "7" }));
    REQUIRE(region.channelRange.getStart() == 3);
    REQUIRE(region.channelRange.getEnd() == 7);
    REQUIRE(region.isChannelRestricted());

    SECTION("out-of-range values retain their opcode defaults")
    {
        sfz::Region invalid { 0 };
        REQUIRE(invalid.parseOpcode({ "lochan", "0" }));
        REQUIRE(invalid.parseOpcode({ "hichan", "17" }));
        REQUIRE(invalid.channelRange.getStart() == 1);
        REQUIRE(invalid.channelRange.getEnd() == 16);
        REQUIRE_FALSE(invalid.isChannelRestricted());
    }

    SECTION("malformed values retain their opcode defaults")
    {
        sfz::Region invalid { 0 };
        REQUIRE(invalid.parseOpcode({ "lochan", "two" }));
        REQUIRE(invalid.parseOpcode({ "hichan", "" }));
        REQUIRE(invalid.channelRange.getStart() == 1);
        REQUIRE(invalid.channelRange.getEnd() == 16);
    }

    SECTION("an explicit full range remains omni")
    {
        sfz::Region omni { 0 };
        REQUIRE(omni.parseOpcode({ "lochan", "1" }));
        REQUIRE(omni.parseOpcode({ "hichan", "16" }));
        REQUIRE_FALSE(omni.isChannelRestricted());
    }
}

TEST_CASE("[Channel routing] channel ranges inherit through every SFZ scope")
{
    SECTION("global")
    {
        RoutingSynth f;
        f.load("channel_global.sfz", R"(
            <global> lochan=2 hichan=5
            <region> sample=*sine
        )");
        const auto* region = f.synth.getRegionView(0);
        REQUIRE(region->channelRange.getStart() == 2);
        REQUIRE(region->channelRange.getEnd() == 5);
    }

    SECTION("master overrides global")
    {
        RoutingSynth f;
        f.load("channel_master.sfz", R"(
            <global> lochan=2 hichan=8
            <master> lochan=3 hichan=7
            <region> sample=*sine
        )");
        const auto* region = f.synth.getRegionView(0);
        REQUIRE(region->channelRange.getStart() == 3);
        REQUIRE(region->channelRange.getEnd() == 7);
    }

    SECTION("group overrides master")
    {
        RoutingSynth f;
        f.load("channel_group.sfz", R"(
            <master> lochan=2 hichan=8
            <group> lochan=4 hichan=6
            <region> sample=*sine
        )");
        const auto* region = f.synth.getRegionView(0);
        REQUIRE(region->channelRange.getStart() == 4);
        REQUIRE(region->channelRange.getEnd() == 6);
    }

    SECTION("region overrides group")
    {
        RoutingSynth f;
        f.load("channel_region.sfz", R"(
            <group> lochan=4 hichan=6
            <region> lochan=5 hichan=5 sample=*sine
        )");
        const auto* region = f.synth.getRegionView(0);
        REQUIRE(region->channelRange.getStart() == 5);
        REQUIRE(region->channelRange.getEnd() == 5);
    }
}

TEST_CASE("[Channel routing] exact channels and overlapping ranges activate in MPE None")
{
    RoutingSynth f;
    REQUIRE_FALSE(f.synth.getMPEEnabled());
    f.load("channel_exact_none.sfz", R"(
        <region> lochan=1 hichan=1 sample=*sine
        <region> lochan=2 hichan=4 sample=*saw
        <region> lochan=4 hichan=6 sample=*tri
    )");

    f.synth.noteOn(0, 0, 60, 100); // MIDI channel 1
    f.render();
    REQUIRE(playingSamples(f.synth) == std::vector<std::string> { "*sine" });

    f.synth.noteOn(0, 1, 61, 100); // MIDI channel 2
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 0);

    f.synth.noteOn(0, 3, 62, 100); // MIDI channel 4 overlaps both ranges
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*saw") == 2);
    REQUIRE(countPlayingSample(f.synth, "*tri") == 1);

    for (const sfz::Voice* voice : getPlayingVoices(f.synth))
        REQUIRE(voice->getTriggerEvent().channel == 0); // expression collapsed
    REQUIRE(playingSourceChannels(f.synth)
        == std::vector<int> { 0, 1, 3, 3 });
}

TEST_CASE("[Channel routing] every MIDI channel maps to SFZ channel 1 through 16")
{
    RoutingSynth f;
    std::string sfz;
    for (int channel = 1; channel <= 16; ++channel) {
        sfz += "<region> key=" + std::to_string(35 + channel)
            + " lochan=" + std::to_string(channel)
            + " hichan=" + std::to_string(channel)
            + " sample=*sine\n";
    }
    REQUIRE(f.synth.loadSfzString(
        fs::current_path() / "all_channels.sfz", sfz));

    for (int source = 0; source < 16; ++source) {
        f.synth.noteOn(0, source, 36 + source, 100);
        f.render();
    }
    REQUIRE(numPlayingVoices(f.synth) == 16);
    REQUIRE(playingSourceChannels(f.synth)
        == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15 });
}

TEST_CASE("[Channel routing] legacy API is MIDI channel 1")
{
    RoutingSynth f;
    f.load("channel_legacy.sfz", R"(
        <region> lochan=1 hichan=1 sample=*sine
        <region> lochan=2 hichan=2 sample=*saw
    )");

    f.synth.noteOn(0, 60, 100);
    f.render();
    REQUIRE(playingSamples(f.synth) == std::vector<std::string> { "*sine" });
    const auto& event = getPlayingVoices(f.synth).front()->getTriggerEvent();
    REQUIRE(event.channel == 0);
    REQUIRE(event.sourceChannel == 0);
}

TEST_CASE("[Channel routing] reversed channel range matches no source")
{
    RoutingSynth f;
    f.load("channel_reversed.sfz", R"(
        <region> lochan=8 hichan=3 sample=*sine
    )");
    for (int channel = 0; channel < 16; ++channel)
        f.synth.noteOn(0, channel, 60, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 0);
}

TEST_CASE("[Channel routing] simple ranges remain active in MPE Full")
{
    RoutingSynth f;
    f.synth.setMPEEnabled(true);
    f.load("channel_full.sfz", R"(
        <region> lochan=2 hichan=2 sample=*sine
        <region> lochan=3 hichan=3 sample=*saw
    )");

    f.synth.noteOn(0, 1, 60, 100);
    f.synth.noteOn(0, 2, 61, 100);
    f.render();
    REQUIRE(playingSamples(f.synth)
        == std::vector<std::string> { "*sine", "*saw" });

    const auto voices = getPlayingVoices(f.synth);
    REQUIRE(voices[0]->getTriggerEvent().channel
        == voices[0]->getTriggerEvent().sourceChannel);
    REQUIRE(voices[1]->getTriggerEvent().channel
        == voices[1]->getTriggerEvent().sourceChannel);
}

TEST_CASE("[Channel routing] motivating CC32 articulations are isolated in MPE None")
{
    RoutingSynth f;
    f.load("channel_cc32_none.sfz", R"(
        <region> lochan=1 hichan=1 locc32=0  hicc32=5  sample=*sine
        <region> lochan=2 hichan=2 locc32=73 hicc32=78 sample=*saw
    )");

    f.synth.cc(0, 0, 32, 0);
    f.synth.cc(0, 1, 32, 73);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 64, 100);
    f.render();
    REQUIRE(playingSamples(f.synth)
        == std::vector<std::string> { "*sine", "*saw" });

    // Changing channel 2 must not pollute channel 1's articulation state.
    f.synth.cc(0, 1, 32, 78);
    f.synth.noteOn(0, 0, 67, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
}

TEST_CASE("[Channel routing] overlapping region ranges keep separate CC state per source")
{
    RoutingSynth f;
    f.load("channel_overlap_cc.sfz", R"(
        <region> lochan=1 hichan=2 locc20=10 hicc20=20 sample=*sine
    )");

    f.synth.cc(0, 0, 20, 15);
    f.synth.cc(0, 1, 20, 80);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 0 });

    f.synth.cc(0, 1, 20, 15);
    f.synth.noteOn(0, 1, 62, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 2);
}

TEST_CASE("[Channel routing] set_cc defaults initialize every restricted source")
{
    RoutingSynth f;
    f.load("channel_default_cc.sfz", R"(
        <control> set_cc20=15
        <region> lochan=2 hichan=2 locc20=10 hicc20=20 sample=*sine
    )");
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
}

TEST_CASE("[Channel routing] pre-load source conditions initialize restricted layers")
{
    RoutingSynth f;
    f.synth.cc(0, 0, 20, 15);
    f.synth.cc(0, 1, 20, 80);
    f.synth.pitchWheel(0, 0, 4096);
    f.synth.pitchWheel(0, 1, -4096);
    f.synth.channelAftertouch(0, 0, 100);
    f.synth.channelAftertouch(0, 1, 20);
    f.load("channel_preload_state.sfz", R"(
        <region> lochan=1 hichan=2 locc20=10 hicc20=20
                 lobend=1000 hibend=8191 lochanaft=64 hichanaft=127
                 sample=*sine
    )");

    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 0 });
}

TEST_CASE("[Channel routing] MPE rejects Member-channel CC32 before condition state")
{
    RoutingSynth f;
    f.synth.setMPEEnabled(true);
    f.load("channel_cc32_full.sfz", R"(
        <region> lochan=2 hichan=2 locc32=73 hicc32=78 sample=*sine
    )");

    f.synth.cc(0, 0, 32, 0); // accepted Manager baseline, zone-wide
    f.synth.cc(0, 1, 32, 73); // prohibited Member Bank Select LSB
    REQUIRE(f.synth.getDroppedManagerOnlyMessageCount() == 1);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 0);
}

TEST_CASE("[Channel routing] accepted MPE Member CC conditions stay isolated")
{
    RoutingSynth f;
    f.synth.setMPEEnabled(true);
    f.load("channel_cc74_full.sfz", R"(
        <region> lochan=2 hichan=3 locc74=80 hicc74=127 sample=*sine
    )");

    f.synth.cc(0, 1, 74, 100);
    f.synth.cc(0, 2, 74, 20);
    f.synth.noteOn(0, 1, 60, 100);
    f.synth.noteOn(0, 2, 61, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 1 });
}

TEST_CASE("[Channel routing] equal CC triggers fire independently on each source")
{
    RoutingSynth f;
    f.load("channel_cc_trigger.sfz", R"(
        <region> lochan=1 hichan=2 on_locc20=64 on_hicc20=64 sample=*sine
    )");

    f.synth.cc(0, 0, 20, 64);
    f.synth.cc(0, 1, 20, 64);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 2);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 0, 1 });
}

TEST_CASE("[Channel routing] same-pitch Note Off is source-scoped in MPE None")
{
    RoutingSynth f;
    f.load("channel_noteoff_none.sfz", R"(
        <region> lochan=1 hichan=2 sample=*sine ampeg_release=1
    )");

    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 2);

    f.synth.noteOff(0, 0, 60, 0);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 1 });
}

TEST_CASE("[Channel routing] omni MPE-off Note Off keeps collapsed legacy ownership")
{
    RoutingSynth f;
    f.load("channel_omni_noteoff.sfz", R"(
        <region> lochan=1 hichan=16 sample=*sine ampeg_release=1
    )");
    REQUIRE_FALSE(f.synth.getRegionView(0)->isChannelRestricted());

    f.synth.noteOn(0, 5, 60, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(getPlayingVoices(f.synth).front()->getTriggerEvent().channel == 0);

    // A different incoming source still maps to expression channel 0 for an
    // omni region, exactly matching the pre-SMPL-93 contract.
    f.synth.noteOff(0, 9, 60, 0);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 0);
}

TEST_CASE("[Channel routing] release triggers inherit source and expression channels")
{
    RoutingSynth f;
    f.load("channel_release.sfz", R"(
        <region> lochan=1 hichan=2 trigger=attack sample=*saw ampeg_release=1
        <region> lochan=1 hichan=2 trigger=release_key sample=*sine
    )");

    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    f.synth.noteOff(0, 0, 60, 0);
    f.render();

    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
    for (const sfz::Voice* voice : getPlayingVoices(f.synth))
        REQUIRE(voice->getTriggerEvent().channel == 0);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 0, 1 });
}

TEST_CASE("[Channel routing] sustain-delayed releases are source-scoped and preallocated")
{
    RoutingSynth f;
    f.load("channel_sustain_release.sfz", R"(
        <region> lochan=1 hichan=2 trigger=attack sample=*saw ampeg_release=1
        <region> lochan=1 hichan=2 trigger=release sample=*sine
    )");
    const sfz::Layer* releaseLayer = f.synth.getLayerView(1);
    const size_t initialCapacity = releaseLayer->sourceDelayedSustainReleases_.capacity();
    REQUIRE(initialCapacity >= 32);

    f.synth.cc(0, 0, 64, 127);
    f.synth.cc(0, 1, 64, 127);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    f.synth.noteOff(0, 0, 60, 0);
    f.synth.noteOff(0, 1, 60, 0);
    f.render();
    REQUIRE(releaseLayer->sourceDelayedSustainReleases_.size() == 2);

    f.synth.cc(0, 0, 64, 0);
    f.render();
    REQUIRE(releaseLayer->sourceDelayedSustainReleases_.size() == 1);
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);

    f.synth.cc(0, 1, 64, 0);
    f.render();
    REQUIRE(releaseLayer->sourceDelayedSustainReleases_.empty());
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
    REQUIRE(releaseLayer->sourceDelayedSustainReleases_.capacity()
        == initialCapacity); // no audio-dispatch allocation
}

TEST_CASE("[Channel routing] sostenuto-delayed releases are source-scoped")
{
    RoutingSynth f;
    f.load("channel_sostenuto_release.sfz", R"(
        <region> lochan=1 hichan=2 trigger=attack sample=*saw ampeg_release=1
        <region> lochan=1 hichan=2 trigger=release sample=*sine
    )");
    const sfz::Layer* releaseLayer = f.synth.getLayerView(1);

    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();
    f.synth.cc(0, 0, 66, 127);
    f.synth.cc(0, 1, 66, 127);
    f.synth.noteOff(0, 0, 60, 0);
    f.synth.noteOff(0, 1, 61, 0);
    f.render();
    REQUIRE(releaseLayer->sourceDelayedSostenutoReleases_.size() == 2);

    f.synth.cc(0, 0, 66, 0);
    f.render();
    REQUIRE(releaseLayer->sourceDelayedSostenutoReleases_.size() == 1);
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
    REQUIRE(playingSourceChannels(f.synth)
        == std::vector<int> { 0, 1 }); // source-1 attack plus source-0 release

    f.synth.cc(0, 1, 66, 0);
    f.render();
    REQUIRE(releaseLayer->sourceDelayedSostenutoReleases_.empty());
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
}

TEST_CASE("[Channel routing] source note-on velocity selects matching release region")
{
    RoutingSynth f;
    f.load("channel_release_velocity.sfz", R"(
        <region> lochan=1 hichan=2 trigger=attack sample=*tri ampeg_release=1
        <region> lochan=1 hichan=2 trigger=release_key lovel=0  hivel=63  sample=*sine
        <region> lochan=1 hichan=2 trigger=release_key lovel=64 hivel=127 sample=*saw
    )");

    f.synth.noteOn(0, 0, 60, 40);
    f.synth.noteOn(0, 1, 60, 110);
    f.render();
    f.synth.noteOff(0, 0, 60, 0);
    f.synth.noteOff(0, 1, 60, 0);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
}

TEST_CASE("[Channel routing] MPE Manager pedal releases retain member expression channels")
{
    RoutingSynth f;
    f.synth.setMPEEnabled(true);
    f.load("channel_mpe_manager_sustain.sfz", R"(
        <region> lochan=2 hichan=3 trigger=attack sample=*saw ampeg_release=1
        <region> lochan=2 hichan=3 trigger=release sample=*sine
    )");

    f.synth.cc(0, 0, 64, 127); // Manager sustain is zone-wide
    f.synth.noteOn(0, 1, 60, 100);
    f.synth.noteOn(0, 2, 61, 100);
    f.render();
    f.synth.noteOff(0, 1, 60, 0);
    f.synth.noteOff(0, 2, 61, 0);
    f.render();
    f.synth.cc(0, 0, 64, 0);
    f.render();

    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
    for (const sfz::Voice* voice : getPlayingVoices(f.synth)) {
        REQUIRE(voice->getTriggerEvent().channel
            == voice->getTriggerEvent().sourceChannel);
    }
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 1, 2 });
}

TEST_CASE("[Channel routing] last keyswitch state is source-scoped")
{
    RoutingSynth f;
    f.load("channel_keyswitch.sfz", R"(
        <region> lochan=1 hichan=1 sw_last=24 key=60 sample=*sine
        <region> lochan=2 hichan=2 sw_last=25 key=60 sample=*saw
    )");

    f.synth.noteOn(0, 0, 24, 100);
    f.synth.noteOn(0, 1, 25, 100);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    REQUIRE(playingSamples(f.synth)
        == std::vector<std::string> { "*sine", "*saw" });

    // Channel 2 changing its switch does not unset channel 1's selection.
    f.synth.noteOn(0, 1, 24, 100);
    f.synth.noteOn(0, 0, 60, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
}

TEST_CASE("[Channel routing] restricted keyswitch does not consume another source's note")
{
    RoutingSynth f;
    f.load("channel_keyswitch_overlap.sfz", R"(
        <region> lochan=1 hichan=1 sw_last=24 key=60 sample=*sine
        <region> lochan=2 hichan=2 key=24 sample=*saw ampeg_release=1
    )");

    f.synth.noteOn(0, 1, 24, 90);
    f.render();
    REQUIRE(playingSamples(f.synth) == std::vector<std::string> { "*saw" });
    REQUIRE(f.synth.getResources().getMidiState().getSourceActiveNotes(1) == 1);
    REQUIRE(f.synth.getResources().getMidiState().getSourceNoteVelocity(1, 24)
        == 90_norm);
    f.synth.noteOff(0, 1, 24, 0);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 0);
}

TEST_CASE("[Channel routing] previous keyswitch state is source-scoped")
{
    RoutingSynth f;
    f.load("channel_previous_keyswitch.sfz", R"(
        <region> lochan=1 hichan=2 sw_previous=24 key=60 sample=*sine
    )");

    f.synth.noteOn(0, 0, 24, 100);
    f.synth.noteOn(0, 1, 25, 100);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 0 });
}

TEST_CASE("[Channel routing] sequence counters are independent per source")
{
    RoutingSynth f;
    f.load("channel_sequence.sfz", R"(
        <region> lochan=1 hichan=2 seq_length=2 seq_position=1 sample=*sine
    )");

    f.synth.noteOn(0, 0, 60, 100); // source 1, position 1
    f.synth.noteOn(0, 1, 61, 100); // source 2, position 1
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 2);

    f.synth.noteOn(0, 0, 62, 100); // source 1, position 2
    f.synth.noteOn(0, 1, 63, 100); // source 2, position 2
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 2);

    f.synth.noteOn(0, 0, 64, 100); // source 1 wraps
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 3);
}

TEST_CASE("[Channel routing] first and legato note counts are source-scoped")
{
    RoutingSynth f;
    f.load("channel_legato.sfz", R"(
        <region> lochan=1 hichan=2 trigger=first  sample=*sine
        <region> lochan=1 hichan=2 trigger=legato sample=*saw
    )");

    f.synth.noteOn(0, 0, 60, 100); // first on source 1
    f.synth.noteOn(0, 1, 64, 100); // independently first on source 2
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 2);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 0);

    f.synth.noteOn(0, 0, 67, 100); // legato only on source 1
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);
}

TEST_CASE("[Channel routing] MPE Full same-pitch Note Off remains channel-specific")
{
    RoutingSynth f;
    f.synth.setMPEEnabled(true);
    f.load("channel_noteoff_full.sfz", R"(
        <region> lochan=2 hichan=3 sample=*sine ampeg_release=1
    )");

    f.synth.noteOn(0, 1, 60, 100);
    f.synth.noteOn(0, 2, 60, 100);
    f.render();
    f.synth.noteOff(0, 1, 60, 0);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 2 });
}

TEST_CASE("[Channel routing] bend and channel-aftertouch conditions are source-scoped")
{
    RoutingSynth f;
    f.load("channel_expression_conditions.sfz", R"(
        <region> lochan=1 hichan=2 lobend=1000 hibend=8191
                 lochanaft=64 hichanaft=127 sample=*sine
    )");

    f.synth.pitchWheel(0, 0, 4096);
    f.synth.channelAftertouch(0, 0, 100);
    f.synth.pitchWheel(0, 1, -4096);
    f.synth.channelAftertouch(0, 1, 20);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 61, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 0 });
}

TEST_CASE("[Channel routing] poly-aftertouch eligibility is source-scoped in MPE None")
{
    RoutingSynth f;
    f.load("channel_poly_aftertouch.sfz", R"(
        <region> lochan=1 hichan=2 lopolyaft=64 hipolyaft=127 sample=*sine
    )");

    f.synth.polyAftertouch(0, 0, 60, 100);
    f.synth.polyAftertouch(0, 1, 60, 20);
    f.synth.noteOn(0, 0, 60, 100);
    f.synth.noteOn(0, 1, 60, 100);
    f.render();
    REQUIRE(numPlayingVoices(f.synth) == 1);
    REQUIRE(playingSourceChannels(f.synth) == std::vector<int> { 0 });
}

TEST_CASE("[Channel routing] off-groups do not cross restricted source ownership")
{
    RoutingSynth f;
    f.load("channel_offgroup.sfz", R"(
        <region> lochan=1 hichan=2 key=60 group=1 off_by=2 sample=*sine
        <region> lochan=1 hichan=2 key=61 group=2 sample=*saw
    )");

    f.synth.noteOn(0, 0, 60, 100);
    f.render();
    f.synth.noteOn(0, 1, 61, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 1);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 1);

    f.synth.noteOn(0, 0, 61, 100);
    f.render();
    REQUIRE(countPlayingSample(f.synth, "*sine") == 0);
    REQUIRE(countPlayingSample(f.synth, "*saw") == 2);
}

TEST_CASE("[Channel routing] loprog and hiprog remain global in both MPE modes")
{
    for (bool mpe : { false, true }) {
        CAPTURE(mpe);
        RoutingSynth f;
        f.synth.setMPEEnabled(mpe);
        f.load("channel_program_global.sfz", R"(
            <region> lochan=1 hichan=2 loprog=10 hiprog=10 sample=*sine
        )");

        f.synth.programChange(0, 9);
        f.synth.noteOn(0, 0, 60, 100);
        f.synth.noteOn(0, 1, 61, 100);
        f.render();
        REQUIRE(numPlayingVoices(f.synth) == 0);

        f.synth.programChange(0, 10);
        f.synth.noteOn(0, 0, 62, 100);
        f.synth.noteOn(0, 1, 63, 100);
        f.render();
        REQUIRE(numPlayingVoices(f.synth) == 2);
    }
}

TEST_CASE("[Channel routing] public C++ wrapper preserves source routing with MPE off")
{
    sfz::Sfizz synth;
    REQUIRE(synth.loadSfzString("wrapper_channel.sfz", R"(
        <region> lochan=2 hichan=2 sample=*sine
    )"));
    synth.noteOn(0, 1, 60, 100);
    std::vector<float> left(256);
    std::vector<float> right(256);
    float* outputs[] { left.data(), right.data() };
    synth.renderBlock(outputs, left.size());
    REQUIRE(synth.getNumActiveVoices() == 1);
}

TEST_CASE("[Channel routing] public C API preserves source routing with MPE off")
{
    sfizz_synth_t* synth = sfizz_create_synth();
    REQUIRE(synth != nullptr);
    REQUIRE(sfizz_load_string(synth, "c_api_channel.sfz", R"(
        <region> lochan=3 hichan=3 sample=*sine
    )"));
    sfizz_send_note_on_channel(synth, 0, 2, 60, 100);
    std::vector<float> left(256);
    std::vector<float> right(256);
    float* outputs[] { left.data(), right.data() };
    sfizz_render_block(synth, outputs, 2, static_cast<int>(left.size()));
    REQUIRE(sfizz_get_num_active_voices(synth) == 1);
    sfizz_free(synth);
}

TEST_CASE("[Channel routing] source CC bookkeeping is independent")
{
    sfz::MidiState state;
    state.sourceCCEvent(0, 74, 0.2f);
    state.sourceCCEvent(1, 74, 0.8f);
    REQUIRE(state.getSourceCCValue(0, 74) == Approx(0.2f));
    REQUIRE(state.getSourceCCValue(1, 74) == Approx(0.8f));
    REQUIRE(state.getSourceCCValue(2, 74) == Approx(0.0f));
    state.resetSourceCCStates();
    REQUIRE(state.getSourceCCValue(0, 74) == Approx(0.0f));
    REQUIRE(state.getSourceCCValue(1, 74) == Approx(0.0f));
}

TEST_CASE("[Channel routing] source note bookkeeping resets on all-notes-off")
{
    sfz::MidiState state;
    state.sourceNoteOnEvent(4, 60, 0.5f);
    state.sourceNoteOnEvent(4, 64, 0.8f);
    REQUIRE(state.getSourceActiveNotes(4) == 2);
    REQUIRE(state.isSourceNotePressed(4, 60));
    REQUIRE(state.getSourceNoteVelocity(4, 64) == Approx(0.8f));

    state.allNotesOff(0);
    REQUIRE(state.getSourceActiveNotes(4) == 0);
    REQUIRE_FALSE(state.isSourceNotePressed(4, 60));
}
