// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once

#include "MidiIdentity.h"

namespace sfz
{
enum class TriggerEventType { NoteOn, NoteOff, CC };

/**
 * @brief Encapsulate a midi event with normalized values
 *
 */
struct TriggerEvent
{
    TriggerEventType type;
    int number;
    float value;
    /**
     * @brief Compatibility MIDI expression channel (0..15). It remains for
     * Note Off matching, voice-steal preference and diagnostics; expression
     * consumers use expressionTarget plus noteId.
     */
    int channel { 0 };
    /**
     * @brief Original protocol-neutral source address, before MPE-off
     * expression normalization. Channel-restricted regions use its channel
     * for lochan/hichan, source-scoped Note Off and articulation state. MIDI
     * 1.0 events use group zero.
     */
    SourceAddress source {};
    /**
     * @brief Logical Note On identity shared by every layered voice created
     * from that event. CC-triggered voices and compatibility paths which do
     * not correspond to an accepted Note On retain an invalid identity.
     */
    NoteInstanceId noteId {};
    /**
     * @brief Broad expression context selected by the input adapter.
     *
     * Logical-note expression composes above this target while the note is
     * active. MPE member notes use the Zone target; legacy MIDI uses Global.
     * Release tails naturally detach Note state and retain this broad scope.
     */
    ExpressionTarget expressionTarget { ExpressionTarget::global() };
};

}
