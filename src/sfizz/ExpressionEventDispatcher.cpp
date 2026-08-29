// SPDX-License-Identifier: BSD-2-Clause

#include "ExpressionEventDispatcher.h"
#include "MidiState.h"
#include "NoteRegistry.h"

namespace sfz {

size_t dispatchAddressedNoteExpression(MidiState& midiState,
    const NoteRegistry& registry,
    const AddressedNoteExpressionEvent& event) noexcept
{
    size_t dispatched = 0;
    auto dispatch = [&](NoteInstanceId noteId) {
        ResolvedExpressionEvent resolved = event.expression;
        resolved.target = ExpressionTarget::note(noteId);
        midiState.expressionEvent(resolved);
        ++dispatched;
    };

    if (event.noteId.valid()) {
        if (registry.matches(
                event.noteId, event.source, event.noteNumber)) {
            dispatch(event.noteId);
        }
        return dispatched;
    }

    registry.forEachActive(event.source, event.noteNumber, dispatch);
    return dispatched;
}

} // namespace sfz
