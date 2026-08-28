// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once

#include "MidiIdentity.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sfz {

/**
 * @brief Bounded, preallocated registry for logical Note On instances.
 *
 * configure() is a control-thread operation. beginNote(), endNote() and
 * clear() neither allocate nor lock. Legacy Note Off ambiguity is resolved in
 * FIFO order for repeated notes with the same source address and pitch.
 */
class NoteRegistry {
public:
    void configure(size_t capacity);

    NoteInstanceId beginNote(SourceAddress source, int noteNumber) noexcept;
    NoteInstanceId endNote(SourceAddress source, int noteNumber) noexcept;
    void clear() noexcept;

    size_t capacity() const noexcept { return slots_.size(); }
    size_t activeCount() const noexcept { return activeCount_; }
    uint64_t overflowCount() const noexcept { return overflowCount_; }

    bool contains(NoteInstanceId id) const noexcept;
    bool matches(NoteInstanceId id, SourceAddress source,
        int noteNumber = -1) const noexcept;

    /** Visit active matches without allocating or exposing registry storage. */
    template <class Visitor>
    void forEachActive(SourceAddress source, int noteNumber,
        Visitor&& visitor) const noexcept;

private:
    struct Slot {
        SourceAddress source;
        uint64_t sequence { 0 };
        uint16_t generation { 0 };
        uint8_t noteNumber { 0 };
        bool active { false };
    };

    std::vector<Slot> slots_;
    uint64_t nextSequence_ { 1 };
    size_t activeCount_ { 0 };
    uint64_t overflowCount_ { 0 };
};

template <class Visitor>
void NoteRegistry::forEachActive(SourceAddress source, int noteNumber,
    Visitor&& visitor) const noexcept
{
    for (size_t i = 0; i < slots_.size(); ++i) {
        const Slot& slot = slots_[i];
        if (!slot.active || slot.source != source)
            continue;
        if (noteNumber >= 0 && slot.noteNumber != noteNumber)
            continue;
        visitor(NoteInstanceId {
            static_cast<uint16_t>(i), slot.generation });
    }
}

} // namespace sfz
