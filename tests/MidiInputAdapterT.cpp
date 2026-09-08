// SPDX-License-Identifier: BSD-2-Clause

#include "sfizz/ExpressionEvent.h"
#include "sfizz/ExpressionEventDispatcher.h"
#include "sfizz/MidiInputAdapter.h"
#include "sfizz/MidiState.h"
#include "sfizz/NoteRegistry.h"
#include "sfizz/Synth.h"
#include "sfizz/Voice.h"
#include "catch2/catch.hpp"

TEST_CASE("[Expression adapter] legacy and MPE pitch resolve at the boundary")
{
    sfz::MidiInputAdapter adapter;
    const sfz::SourceAddress member = sfz::SourceAddress::fromMidi1(3);

    auto route = adapter.resolvePitch(7, member, 0.5f);
    REQUIRE(route.event.target == sfz::ExpressionTarget::global());
    REQUIRE(route.event.kind == sfz::ExpressionEventKind::LegacyPitch);
    REQUIRE(route.event.value == 0.5f);
    REQUIRE_FALSE(route.broadcastToActiveNotes);

    adapter.setMpeEnabled(true);
    adapter.setPitchBendRange(2.0f, 24.0f);

    route = adapter.resolvePitch(9, sfz::SourceAddress::fromMidi1(0), -0.5f);
    REQUIRE(route.event.target == sfz::ExpressionTarget::zone(0));
    REQUIRE(route.event.kind == sfz::ExpressionEventKind::Pitch);
    REQUIRE(route.event.value == -1.0f);
    REQUIRE_FALSE(route.broadcastToActiveNotes);

    route = adapter.resolvePitch(11, member, 0.5f);
    REQUIRE(route.event.target == sfz::ExpressionTarget::channel(member));
    REQUIRE(route.event.kind == sfz::ExpressionEventKind::Pitch);
    REQUIRE(route.event.value == 12.0f);
    REQUIRE(route.broadcastToActiveNotes);

    const auto seed = adapter.memberSeed(member);
    REQUIRE(seed.hasPitch);
    REQUIRE(seed.pitchSemitones == 12.0f);
}

TEST_CASE("[Expression adapter] MPE policy owns filters and RPN state")
{
    sfz::MidiInputAdapter adapter;
    const sfz::SourceAddress manager = sfz::SourceAddress::fromMidi1(0);
    const sfz::SourceAddress member = sfz::SourceAddress::fromMidi1(2);
    adapter.setMpeEnabled(true);

    REQUIRE_FALSE(adapter.acceptControl(member, 64));
    REQUIRE(adapter.droppedManagerOnlyControlCount() == 1);
    REQUIRE(adapter.acceptControl(manager, 64));

    REQUIRE_FALSE(adapter.acceptPolyPressure(member));
    REQUIRE(adapter.droppedPolyPressureCount() == 1);
    REQUIRE(adapter.acceptPolyPressure(manager));

    adapter.observeRpnControl(member, 101, 0.0f);
    adapter.observeRpnControl(member, 100, 0.0f);
    adapter.observeRpnControl(member, 6, 0.5f);
    REQUIRE(adapter.memberPitchBendRange() == 64.0f);

    adapter.observeRpnControl(manager, 101, 0.0f);
    adapter.observeRpnControl(manager, 100, 6.0f / 127.0f);
    adapter.observeRpnControl(manager, 6, 0.0f);
    REQUIRE_FALSE(adapter.mpeEnabled());
}

TEST_CASE("[Expression adapter] synthetic MIDI 2 note event matches resolved MPE note event")
{
    sfz::MidiState mpeState;
    sfz::MidiState midi2State;
    mpeState.configureNoteExpressionContexts(1);
    midi2State.configureNoteExpressionContexts(1);
    const sfz::NoteInstanceId noteId { 0, 1 };
    mpeState.beginNoteExpression(noteId);
    midi2State.beginNoteExpression(noteId);

    sfz::MidiInputAdapter adapter;
    adapter.setMpeEnabled(true);
    adapter.setPitchBendRange(2.0f, 24.0f);
    auto mpeRoute = adapter.resolvePitch(
        13, sfz::SourceAddress::fromMidi1(4), 0.25f);
    mpeRoute.event.target = sfz::ExpressionTarget::note(noteId);
    REQUIRE(mpeState.expressionEvent(mpeRoute.event));

    // A future UMP decoder emits this protocol-neutral event after resolving
    // its native per-note pitch width and group/channel/note address.
    const sfz::ResolvedExpressionEvent midi2Event {
        sfz::ExpressionTarget::note(noteId),
        sfz::ExpressionEventKind::Pitch,
        { }, 13, -1, 6.0f
    };
    REQUIRE(midi2State.expressionEvent(midi2Event));

    const auto* mpeContext = mpeState.getExpressionContext(
        sfz::ExpressionTarget::note(noteId));
    const auto* midi2Context = midi2State.getExpressionContext(
        sfz::ExpressionTarget::note(noteId));
    REQUIRE(mpeContext != nullptr);
    REQUIRE(midi2Context != nullptr);
    REQUIRE(mpeContext->pitchValue() == midi2Context->pitchValue());
    REQUIRE(mpeContext->pitchEvents().size()
        == midi2Context->pitchEvents().size());
    for (size_t i = 0; i < mpeContext->pitchEvents().size(); ++i) {
        REQUIRE(mpeContext->pitchEvents()[i].delay
            == midi2Context->pitchEvents()[i].delay);
        REQUIRE(mpeContext->pitchEvents()[i].value
            == midi2Context->pitchEvents()[i].value);
    }
}

TEST_CASE("[Expression adapter] MIDI 2 address broadcasts ambiguous generations deterministically")
{
    sfz::NoteRegistry registry;
    registry.configure(3);
    const sfz::SourceAddress source { 2, 5 };
    const sfz::NoteInstanceId first = registry.beginNote(source, 60);
    const sfz::NoteInstanceId second = registry.beginNote(source, 60);

    sfz::MidiState state;
    state.configureNoteExpressionContexts(registry.capacity());
    state.beginNoteExpression(first);
    state.beginNoteExpression(second);

    sfz::AddressedNoteExpressionEvent event {
        source, 60, { }, { sfz::ExpressionTarget::global(), sfz::ExpressionEventKind::Pressure, { }, 4, -1, 0.6f }
    };
    REQUIRE(sfz::dispatchAddressedNoteExpression(state, registry, event) == 2);
    REQUIRE(state.getExpressionContext(
                     sfz::ExpressionTarget::note(first))
                ->pressureValue()
        == 0.6f);
    REQUIRE(state.getExpressionContext(
                     sfz::ExpressionTarget::note(second))
                ->pressureValue()
        == 0.6f);

    event.noteId = second;
    event.expression.value = 0.9f;
    REQUIRE(sfz::dispatchAddressedNoteExpression(state, registry, event) == 1);
    REQUIRE(state.getExpressionContext(
                     sfz::ExpressionTarget::note(first))
                ->pressureValue()
        == 0.6f);
    REQUIRE(state.getExpressionContext(
                     sfz::ExpressionTarget::note(second))
                ->pressureValue()
        == 0.9f);

    event.source.channel = 6;
    REQUIRE(sfz::dispatchAddressedNoteExpression(state, registry, event) == 0);
}

TEST_CASE("[Expression adapter] malformed overlapping MPE notes share Member expression")
{
    sfz::Synth synth;
    synth.setMPEEnabled(true);
    synth.setMPEPitchBendRange(2.0f, 24.0f);
    REQUIRE(synth.loadSfzString(fs::current_path() / "tests/mpe-adapter.sfz", R"(
        <region> sample=*sine ampeg_release=1
    )"));

    synth.noteOn(0, 3, 60, 100);
    synth.noteOn(0, 3, 64, 100);
    auto voices = synth.getActiveVoices();
    REQUIRE(voices.size() == 2);
    REQUIRE(voices[0]->getTriggerEvent().expressionTarget
        == sfz::ExpressionTarget::zone(0));
    REQUIRE(voices[1]->getTriggerEvent().expressionTarget
        == sfz::ExpressionTarget::zone(0));

    synth.hdPitchWheel(17, 3, 0.5f);
    const sfz::MidiState& state = synth.getResources().getMidiState();
    for (const sfz::Voice* voice : voices) {
        const sfz::NoteInstanceId id = voice->getTriggerEvent().noteId;
        const sfz::ExpressionContext* context = state.getExpressionContext(
            sfz::ExpressionTarget::note(id));
        REQUIRE(context != nullptr);
        REQUIRE(context->pitchValue() == 12.0f);
        REQUIRE(context->pitchEvents().back().delay == 17);
    }

    sfz::AudioBuffer<float> buffer {
        2, static_cast<unsigned>(synth.getSamplesPerBlock())
    };
    synth.renderBlock(buffer);
    synth.setMPEPitchBendRange(2.0f, 12.0f);
    for (const sfz::Voice* voice : voices) {
        const sfz::ExpressionContext* context = state.getExpressionContext(
            sfz::ExpressionTarget::note(voice->getTriggerEvent().noteId));
        REQUIRE(context->pitchValue() == 6.0f);
    }
}

TEST_CASE("[Expression adapter] note controller density survives voice reconfiguration")
{
    sfz::Synth synth;
    synth.setMPEEnabled(true);
    REQUIRE(synth.loadSfzString(fs::current_path() / "tests/mpe-reconfigure.sfz", R"(
        <region> sample=*sine cutoff_oncc21=1000
    )"));
    synth.setNumVoices(8);

    synth.noteOn(0, 2, 60, 100);
    const sfz::NoteInstanceId noteId =
        synth.getActiveVoices().front()->getTriggerEvent().noteId;
    synth.hdcc(3, 2, 21, 0.5f);

    const sfz::ExpressionContext* note =
        synth.getResources().getMidiState().getExpressionContext(
            sfz::ExpressionTarget::note(noteId));
    REQUIRE(note != nullptr);
    REQUIRE(note->hasController(21));
    REQUIRE(note->controllerEvents(21) != nullptr);
    REQUIRE(note->controllerEvents(21)->back().delay == 3);
    REQUIRE(note->controllerValue(21) == 0.5f);
}

TEST_CASE("[Expression adapter] retired note context detaches while the voice retains pressure")
{
    sfz::Synth synth;
    synth.setMPEEnabled(true);
    REQUIRE(synth.loadSfzString(fs::current_path() / "tests/mpe-release-target.sfz", R"(
        <region> sample=*sine ampeg_release=1
    )"));

    synth.noteOn(0, 2, 60, 100);
    const sfz::TriggerEvent trigger = synth.getActiveVoices().front()->getTriggerEvent();
    synth.hdChannelAftertouch(0, 0, 0.25f);
    synth.hdChannelAftertouch(0, 2, 0.75f);

    const sfz::MidiState& state = synth.getResources().getMidiState();
    REQUIRE(state.getVoicePressureEvents(
                     trigger.expressionTarget, trigger.noteId)
                .back()
                .value
        == 0.75f);

    const auto* voice = synth.getActiveVoices().front();
    synth.noteOff(0, 2, 60, 0);
    synth.hdChannelAftertouch(0, 2, 1.0f);
    REQUIRE(voice->getPressureEvents().back().value == 0.75f);
    REQUIRE(state.getExpressionContext(
                sfz::ExpressionTarget::note(trigger.noteId))
        == nullptr);
    REQUIRE(state.getVoicePressureEvents(
                     trigger.expressionTarget, trigger.noteId)
                .back()
                .value
        == 0.25f);
}

TEST_CASE("[Expression adapter] explicit zero Member timbre is stored separately from Zone")
{
    sfz::Synth synth;
    synth.setMPEEnabled(true);
    REQUIRE(synth.loadSfzString(fs::current_path() / "tests/mpe-timbre.sfz", R"(
        <region> sample=*sine cutoff_oncc74=1000
    )"));

    synth.noteOn(0, 2, 60, 100);
    const sfz::Voice* voice = synth.getActiveVoices().front();
    const sfz::TriggerEvent trigger = voice->getTriggerEvent();

    synth.hdcc(0, 0, 74, 0.75f);
    synth.hdcc(5, 2, 74, 0.0f);

    const sfz::MidiState& state = synth.getResources().getMidiState();
    const sfz::ExpressionContext* zone = state.getExpressionContext(
        sfz::ExpressionTarget::zone(0));
    const sfz::ExpressionContext* note = state.getExpressionContext(
        sfz::ExpressionTarget::note(trigger.noteId));
    REQUIRE(zone != nullptr);
    REQUIRE(note != nullptr);
    REQUIRE(zone->controllerValue(74) == 0.75f);
    REQUIRE(note->hasController(74));
    REQUIRE(note->controllerValue(74) == 0.0f);
    REQUIRE(state.getVoiceCCEvents(
                     trigger.expressionTarget, trigger.noteId, 74)
                .back()
                .value
        == 0.0f);
    // Raw contexts keep explicit zero; rendering combines the live Manager
    // and Member values rather than changing the stored MIDI state.
    REQUIRE(voice->getControllerEvents(74).back().value == 0.75f);
}
