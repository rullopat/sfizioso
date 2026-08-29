// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "ExpressionEvent.h"
#include <cstddef>

namespace sfz {

class MidiState;
class NoteRegistry;

/**
 * @brief Address supplied by a per-note transport/profile adapter.
 *
 * MIDI 2.0 group/channel/note messages generally leave noteId invalid and
 * therefore address every matching active generation. A protocol mechanism
 * with a more specific identity may supply noteId to select one generation.
 */
struct AddressedNoteExpressionEvent {
    SourceAddress source { };
    int noteNumber { -1 };
    NoteInstanceId noteId { };
    ResolvedExpressionEvent expression { };
};

/**
 * Dispatch a synthetic/resolved per-note event without allocation or locks.
 * Returns the number of logical-note contexts to which the event was offered.
 */
size_t dispatchAddressedNoteExpression(MidiState& midiState,
    const NoteRegistry& registry,
    const AddressedNoteExpressionEvent& event) noexcept;

} // namespace sfz
