// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "NoteRegistry.h"
#include <algorithm>

namespace sfz {

void NoteRegistry::configure(size_t capacity)
{
    const size_t maximumCapacity = NoteInstanceId::invalidIndex;
    slots_.clear();
    slots_.resize(std::min(capacity, maximumCapacity));
    nextSequence_ = 1;
    activeCount_ = 0;
    overflowCount_ = 0;
}

NoteInstanceId NoteRegistry::beginNote(SourceAddress source, int noteNumber) noexcept
{
    if (source.group >= 16 || source.channel >= 16
        || noteNumber < 0 || noteNumber >= 128) {
        ++overflowCount_;
        return { };
    }

    const auto it = std::find_if(slots_.begin(), slots_.end(),
        [](const Slot& slot) { return !slot.active; });
    if (it == slots_.end()) {
        ++overflowCount_;
        return { };
    }

    Slot& slot = *it;
    ++slot.generation;
    if (slot.generation == 0)
        ++slot.generation;
    slot.source = source;
    slot.noteNumber = static_cast<uint8_t>(noteNumber);
    slot.sequence = nextSequence_++;
    slot.active = true;
    ++activeCount_;

    return {
        static_cast<uint16_t>(std::distance(slots_.begin(), it)),
        slot.generation
    };
}

NoteInstanceId NoteRegistry::endNote(SourceAddress source, int noteNumber) noexcept
{
    Slot* oldest = nullptr;
    size_t oldestIndex = 0;

    for (size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (!slot.active || slot.source != source || slot.noteNumber != noteNumber)
            continue;
        if (oldest == nullptr || slot.sequence < oldest->sequence) {
            oldest = &slot;
            oldestIndex = i;
        }
    }

    if (oldest == nullptr)
        return { };

    oldest->active = false;
    --activeCount_;
    return { static_cast<uint16_t>(oldestIndex), oldest->generation };
}

void NoteRegistry::clear() noexcept
{
    for (Slot& slot : slots_)
        slot.active = false;
    activeCount_ = 0;
}

bool NoteRegistry::contains(NoteInstanceId id) const noexcept
{
    return id.valid() && id.index < slots_.size()
        && slots_[id.index].active
        && slots_[id.index].generation == id.generation;
}

} // namespace sfz
