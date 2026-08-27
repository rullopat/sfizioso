// SPDX-License-Identifier: BSD-2-Clause

#include "sfizz/ExpressionContext.h"
#include "sfizz/MidiState.h"
#include "sfizz/MidiIdentity.h"
#include "sfizz/Synth.h"
#include "sfizz/Resources.h"
#include "sfizz/Voice.h"
#include "catch2/catch.hpp"

TEST_CASE("[Expression] Targets retain protocol-neutral identities")
{
    using namespace sfz;

    const SourceAddress source { 9, 13 };
    const ExpressionTarget channel = ExpressionTarget::channel(source);
    REQUIRE(channel.scope == ExpressionScope::Channel);
    REQUIRE(channel.sourceAddress() == source);

    const NoteInstanceId note { 42, 17 };
    const ExpressionTarget noteTarget = ExpressionTarget::note(note);
    REQUIRE(noteTarget.scope == ExpressionScope::Note);
    REQUIRE(noteTarget.noteInstanceId() == note);

    REQUIRE(ExpressionTarget::global() != ExpressionTarget::zone(0));
    REQUIRE(ExpressionControlId::fromSfizzCC(74).nameSpace
        == ExpressionControlNamespace::MidiCC);
    REQUIRE(ExpressionControlId::fromSfizzCC(130).nameSpace
        == ExpressionControlNamespace::SfzExtendedCC);
}

TEST_CASE("[Expression] Dense controller slots retain scalar state for unused controls")
{
    sfz::ExpressionContext context;
    context.configure(/*controllerSlots=*/2, /*polyPressureSlots=*/0,
        /*eventsPerTimeline=*/8);

    std::array<bool, sfz::config::numCCs> used { };
    used[7] = true;
    used[74] = true;
    context.configureSfizzControllers(used);

    REQUIRE(context.controllerSlotCount() == 2);
    REQUIRE(context.configuredControllerCount() == 2);
    REQUIRE(context.controllerEvents(7) != nullptr);
    REQUIRE(context.controllerEvents(74) != nullptr);
    REQUIRE(context.controllerEvents(11) == nullptr);

    REQUIRE(context.controllerEvent(3, 11, 0.75f));
    REQUIRE(context.controllerValue(11) == 0.75f);
    REQUIRE(context.hasController(11));
    REQUIRE(context.controllerEvents(11) == nullptr);
}

TEST_CASE("[Expression] Compatibility slots activate without container growth")
{
    sfz::ExpressionContext context;
    context.configure(/*controllerSlots=*/1, /*polyPressureSlots=*/1,
        /*eventsPerTimeline=*/8, /*retainAllControllerScalars=*/false);
    REQUIRE(context.controllerSlotCount() == 1);
    REQUIRE(context.configuredControllerCount() == 0);

    REQUIRE(context.controllerEvent(4, 74, 0.5f));
    REQUIRE(context.controllerSlotCount() == 1);
    REQUIRE(context.configuredControllerCount() == 1);
    const sfz::EventVector* events = context.controllerEvents(74);
    REQUIRE(events != nullptr);
    REQUIRE(events->capacity() == 8);
    REQUIRE(events->size() == 2);
}

TEST_CASE("[Expression] Timeline overflow is bounded counted and deterministic")
{
    sfz::ExpressionContext context;
    context.configure(/*controllerSlots=*/1, /*polyPressureSlots=*/0,
        /*eventsPerTimeline=*/3);
    std::array<bool, sfz::config::numCCs> used { };
    used[74] = true;
    context.configureSfizzControllers(used);

    const sfz::EventVector* events = context.controllerEvents(74);
    REQUIRE(events != nullptr);
    const size_t capacity = events->capacity();

    REQUIRE(context.controllerEvent(1, 74, 0.1f));
    REQUIRE(context.controllerEvent(2, 74, 0.2f));
    REQUIRE_FALSE(context.controllerEvent(3, 74, 0.3f));
    REQUIRE(context.overflowCount() == 1);
    REQUIRE(context.controllerValue(74) == 0.2f);
    REQUIRE(events->size() == 3);
    REQUIRE(events->capacity() == capacity);

    // Replacing an existing sample offset needs no additional capacity.
    REQUIRE(context.controllerEvent(2, 74, 0.25f));
    REQUIRE(context.overflowCount() == 1);
    REQUIRE(events->back().value == 0.25f);
}

TEST_CASE("[Expression] Compatibility inheritance is explicit after zero writes")
{
    sfz::MidiState state;
    state.ccEvent(0, /*channel=*/0, 74, 0.8f);
    REQUIRE(state.getCCValue(/*channel=*/3, 74) == 0.8f);

    // Presence, not vector emptiness or a non-zero value, selects the member
    // context. A deliberate member zero must override the global value.
    state.ccEvent(0, /*channel=*/3, 74, 0.0f);
    state.advanceTime(64);
    REQUIRE(state.getCCValue(/*channel=*/3, 74) == 0.0f);
    REQUIRE(state.getCCEvents(/*channel=*/3, 74).back().value == 0.0f);
    REQUIRE(state.getCCValue(/*channel=*/0, 74) == 0.8f);
}

TEST_CASE("[Expression] Global zone and channel contexts are distinct")
{
    sfz::MidiState state;
    sfz::ExpressionContext* global = state.getExpressionContext(
        sfz::ExpressionTarget::global());
    sfz::ExpressionContext* zone = state.getExpressionContext(
        sfz::ExpressionTarget::zone(0));
    sfz::ExpressionContext* channel = state.getExpressionContext(
        sfz::ExpressionTarget::channel({ 0, 4 }));

    REQUIRE(global != nullptr);
    REQUIRE(zone != nullptr);
    REQUIRE(channel != nullptr);
    REQUIRE(global != zone);
    REQUIRE(global != channel);
    REQUIRE(zone != channel);
    REQUIRE(state.getExpressionContext(sfz::ExpressionTarget::zone(1)) == nullptr);
    REQUIRE(state.getExpressionContext(
                sfz::ExpressionTarget::channel({ 1, 4 }))
        == nullptr);

    REQUIRE(global->pressureEvent(0, 0.2f));
    REQUIRE(zone->pressureEvent(0, 0.4f));
    REQUIRE(channel->pressureEvent(0, 0.6f));
    REQUIRE(global->pressureValue() == 0.2f);
    REQUIRE(zone->pressureValue() == 0.4f);
    REQUIRE(channel->pressureValue() == 0.6f);
}

TEST_CASE("[Expression] Note contexts are shared and generation safe")
{
    sfz::MidiState state;
    state.configureNoteExpressionContexts(1);

    const sfz::NoteInstanceId first { 0, 1 };
    state.beginNoteExpression(first);
    sfz::ExpressionContext* firstContext = state.getExpressionContext(
        sfz::ExpressionTarget::note(first));
    REQUIRE(firstContext != nullptr);
    REQUIRE(firstContext == state.getExpressionContext(sfz::ExpressionTarget::note(first)));
    REQUIRE(firstContext->pressureEvent(12, 0.7f));
    REQUIRE(firstContext->pressureValue() == 0.7f);

    state.endNoteExpression(first);
    REQUIRE(state.getExpressionContext(
                sfz::ExpressionTarget::note(first))
        == nullptr);

    const sfz::NoteInstanceId second { 0, 2 };
    state.beginNoteExpression(second);
    sfz::ExpressionContext* secondContext = state.getExpressionContext(
        sfz::ExpressionTarget::note(second));
    REQUIRE(secondContext != nullptr);
    REQUIRE(secondContext->pressureValue() == 0.0f);
    REQUIRE(state.getExpressionContext(
                sfz::ExpressionTarget::note(first))
        == nullptr);
}

TEST_CASE("[Expression] SFZ load densifies used controls across scopes")
{
    sfz::Synth synth;
    synth.loadSfzString("expression-controls.sfz", R"SFZ(
        <region> sample=*sine cutoff=1000 cutoff_oncc21=500
    )SFZ");

    sfz::MidiState& state = synth.getResources().getMidiState();
    const sfz::ExpressionContext* global = state.getExpressionContext(
        sfz::ExpressionTarget::global());
    const sfz::ExpressionContext* channel = state.getExpressionContext(
        sfz::ExpressionTarget::channel({ 0, 3 }));
    REQUIRE(global != nullptr);
    REQUIRE(channel != nullptr);
    REQUIRE(global->controllerEvents(21) != nullptr);
    REQUIRE(channel->controllerEvents(21) != nullptr);
    REQUIRE(channel->controllerEvents(74) != nullptr); // MPE profile timbre
    REQUIRE(global->controllerEvents(22) == nullptr);
    REQUIRE(channel->controllerEvents(22) == nullptr);
}

TEST_CASE("[Expression] Layered voices resolve one shared note context")
{
    sfz::Synth synth;
    synth.loadSfzString("expression-context.sfz", R"SFZ(
        <group> key=60
        <region> sample=*sine
        <region> sample=*sine transpose=12
    )SFZ");

    synth.noteOn(0, /*channel=*/2, 60, 100);
    const auto voices = synth.getActiveVoices();
    REQUIRE(voices.size() == 2);
    const sfz::NoteInstanceId noteId = voices.front()->getTriggerEvent().noteId;
    REQUIRE(noteId.valid());
    REQUIRE(voices.back()->getTriggerEvent().noteId == noteId);

    sfz::MidiState& state = synth.getResources().getMidiState();
    sfz::ExpressionContext* first = state.getExpressionContext(
        sfz::ExpressionTarget::note(voices.front()->getTriggerEvent().noteId));
    sfz::ExpressionContext* second = state.getExpressionContext(
        sfz::ExpressionTarget::note(voices.back()->getTriggerEvent().noteId));
    REQUIRE(first != nullptr);
    REQUIRE(first == second);

    synth.noteOff(0, /*channel=*/2, 60, 0);
    REQUIRE(state.getExpressionContext(
                sfz::ExpressionTarget::note(noteId))
        == nullptr);
}

TEST_CASE("[Expression] Note timeline overflow contributes to aggregate diagnostics")
{
    sfz::MidiState state;
    state.configureNoteExpressionContexts(1);
    const sfz::NoteInstanceId note { 0, 1 };
    state.beginNoteExpression(note);
    sfz::ExpressionContext* context = state.getExpressionContext(
        sfz::ExpressionTarget::note(note));
    REQUIRE(context != nullptr);

    for (int delay = 1; delay < 65; ++delay)
        REQUIRE(context->pressureEvent(delay, delay / 64.0f));
    REQUIRE_FALSE(context->pressureEvent(65, 1.0f));
    REQUIRE(context->overflowCount() == 1);
    REQUIRE(state.getExpressionOverflowCount() == 1);

    state.endNoteExpression(note);
    state.beginNoteExpression({ 0, 2 });
    REQUIRE(state.getExpressionOverflowCount() == 1);
}
