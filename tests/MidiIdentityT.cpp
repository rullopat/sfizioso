// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "sfizz/MidiIdentity.h"
#include "sfizz/NoteRegistry.h"
#include "sfizz/Synth.h"
#include "sfizz/Voice.h"
#include "TestHelpers.h"
#include "catch2/catch.hpp"
#include <algorithm>
#include <vector>

TEST_CASE("[MIDI identity] source address includes group and channel")
{
    constexpr sfz::SourceAddress midi1 = sfz::SourceAddress::fromMidi1(7);
    constexpr sfz::SourceAddress midi2 { 3, 7 };

    STATIC_REQUIRE(midi1.group == 0);
    STATIC_REQUIRE(midi1.channel == 7);
    STATIC_REQUIRE(midi1 != midi2);
    STATIC_REQUIRE((midi2 == sfz::SourceAddress { 3, 7 }));
}

TEST_CASE("[MIDI identity] registry pairs repeated notes FIFO")
{
    sfz::NoteRegistry registry;
    registry.configure(4);
    const sfz::SourceAddress source { 0, 2 };

    const sfz::NoteInstanceId first = registry.beginNote(source, 60);
    const sfz::NoteInstanceId second = registry.beginNote(source, 60);
    REQUIRE(first.valid());
    REQUIRE(second.valid());
    REQUIRE(first != second);
    REQUIRE(registry.activeCount() == 2);

    REQUIRE(registry.endNote(source, 60) == first);
    REQUIRE_FALSE(registry.contains(first));
    REQUIRE(registry.contains(second));
    REQUIRE(registry.endNote(source, 60) == second);
    REQUIRE(registry.activeCount() == 0);
    REQUIRE_FALSE(registry.endNote(source, 60).valid());
}

TEST_CASE("[MIDI identity] registry separates group channel and pitch")
{
    sfz::NoteRegistry registry;
    registry.configure(4);

    const auto group0 = registry.beginNote({ 0, 1 }, 60);
    const auto group1 = registry.beginNote({ 1, 1 }, 60);
    const auto note61 = registry.beginNote({ 0, 1 }, 61);

    REQUIRE(registry.endNote({ 1, 1 }, 60) == group1);
    REQUIRE(registry.endNote({ 0, 1 }, 61) == note61);
    REQUIRE(registry.endNote({ 0, 1 }, 60) == group0);
}

TEST_CASE("[MIDI identity] registry is bounded and generation safe")
{
    sfz::NoteRegistry registry;
    registry.configure(1);

    const auto first = registry.beginNote({ 0, 0 }, 60);
    REQUIRE(first.valid());
    REQUIRE_FALSE(registry.beginNote({ 0, 0 }, 61).valid());
    REQUIRE(registry.overflowCount() == 1);

    REQUIRE(registry.endNote({ 0, 0 }, 60) == first);
    const auto recycled = registry.beginNote({ 0, 0 }, 61);
    REQUIRE(recycled.valid());
    REQUIRE(recycled.index == first.index);
    REQUIRE(recycled.generation != first.generation);
    REQUIRE_FALSE(registry.contains(first));
    REQUIRE(registry.contains(recycled));

    registry.clear();
    REQUIRE(registry.activeCount() == 0);
    REQUIRE_FALSE(registry.contains(recycled));
}

TEST_CASE("[MIDI identity] layered voices share one logical Note On")
{
    sfz::Synth synth;
    REQUIRE(synth.loadSfzString(fs::current_path() / "logical_note.sfz", R"(
        <region> lochan=3 hichan=3 key=60 sample=*sine
        <region> lochan=3 hichan=3 key=60 sample=*saw
    )"));

    synth.noteOn(0, 2, 60, 100);
    const auto firstVoices = getPlayingVoices(synth);
    REQUIRE(firstVoices.size() == 2);
    const sfz::NoteInstanceId first = firstVoices.front()->getTriggerEvent().noteId;
    REQUIRE(first.valid());
    REQUIRE(firstVoices.back()->getTriggerEvent().noteId == first);
    REQUIRE((firstVoices.front()->getTriggerEvent().source
        == sfz::SourceAddress { 0, 2 }));

    synth.noteOn(0, 2, 60, 110);
    const auto allVoices = getPlayingVoices(synth);
    REQUIRE(allVoices.size() == 4);
    const sfz::NoteInstanceId second = allVoices[2]->getTriggerEvent().noteId;
    REQUIRE(second.valid());
    REQUIRE(second != first);
    REQUIRE(allVoices[3]->getTriggerEvent().noteId == second);
}

TEST_CASE("[MIDI identity] delayed release voices retain logical note identity")
{
    sfz::Synth synth;
    REQUIRE(synth.loadSfzString(fs::current_path() / "delayed_note_id.sfz", R"(
        <region> lochan=3 hichan=3 key=60 trigger=attack  sample=*sine
        <region> lochan=3 hichan=3 key=60 trigger=release sample=*saw
    )"));

    synth.cc(0, 2, 64, 127);
    synth.noteOn(0, 2, 60, 100);
    const auto attackVoices = getPlayingVoices(synth);
    REQUIRE(attackVoices.size() == 1);
    const sfz::NoteInstanceId noteId = attackVoices.front()->getTriggerEvent().noteId;
    REQUIRE(noteId.valid());

    synth.noteOff(0, 2, 60, 0);
    const auto beforePedalUp = getPlayingVoices(synth);
    REQUIRE(std::none_of(beforePedalUp.begin(), beforePedalUp.end(),
        [](const sfz::Voice* voice) {
            return voice->getTriggerEvent().type == sfz::TriggerEventType::NoteOff;
        }));
    synth.cc(0, 2, 64, 0);

    const auto released = getPlayingVoices(synth);
    const auto it = std::find_if(released.begin(), released.end(),
        [](const sfz::Voice* voice) {
            return voice->getTriggerEvent().type == sfz::TriggerEventType::NoteOff;
        });
    REQUIRE(it != released.end());
    REQUIRE((*it)->getTriggerEvent().noteId == noteId);
    REQUIRE((*it)->getTriggerEvent().source == sfz::SourceAddress::fromMidi1(2));
}

TEST_CASE("[MIDI identity] repeated Note Off releases the oldest instance only")
{
    sfz::Synth synth;
    REQUIRE(synth.loadSfzString(fs::current_path() / "repeated_note.sfz", R"(
        <region> key=60 sample=*sine ampeg_release=0.001
        <region> key=60 sample=*saw  ampeg_release=0.001
    )"));
    sfz::AudioBuffer<float> buffer {
        2, static_cast<unsigned>(synth.getSamplesPerBlock())
    };

    synth.noteOn(0, 60, 100);
    const auto firstVoices = getPlayingVoices(synth);
    REQUIRE(firstVoices.size() == 2);
    const sfz::NoteInstanceId first = firstVoices.front()->getTriggerEvent().noteId;

    synth.noteOn(0, 60, 110);
    const auto allVoices = getPlayingVoices(synth);
    REQUIRE(allVoices.size() == 4);
    const sfz::NoteInstanceId second = allVoices[2]->getTriggerEvent().noteId;
    REQUIRE(second != first);

    synth.noteOff(0, 60, 0);
    synth.renderBlock(buffer);
    const auto remaining = getPlayingVoices(synth);
    REQUIRE(remaining.size() == 2);
    for (const sfz::Voice* voice : remaining)
        REQUIRE(voice->getTriggerEvent().noteId == second);
}
