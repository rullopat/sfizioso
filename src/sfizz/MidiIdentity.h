// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#pragma once

#include <cstdint>
#include <limits>

namespace sfz {

/**
 * @brief Protocol-neutral address of the source which produced an event.
 *
 * MIDI 1.0 adapters always use group zero. Keeping group in the core identity
 * avoids having to add another channel dimension when a MIDI 2.0 adapter is
 * introduced.
 */
struct SourceAddress {
    uint8_t group { 0 };
    uint8_t channel { 0 };

    constexpr SourceAddress() noexcept = default;
    constexpr SourceAddress(uint8_t group, uint8_t channel) noexcept
        : group(group)
        , channel(channel)
    {
    }

    static constexpr SourceAddress fromMidi1(int channel) noexcept
    {
        return SourceAddress { 0, static_cast<uint8_t>(channel) };
    }
};

constexpr bool operator==(SourceAddress lhs, SourceAddress rhs) noexcept
{
    return lhs.group == rhs.group && lhs.channel == rhs.channel;
}

constexpr bool operator!=(SourceAddress lhs, SourceAddress rhs) noexcept
{
    return !(lhs == rhs);
}

/**
 * @brief Generation-safe identity of one accepted logical Note On.
 *
 * Every voice layered from one Note On shares this identity. The generation
 * prevents a voice retaining a recycled registry index from accidentally
 * observing a later note's state.
 */
struct NoteInstanceId {
    static constexpr uint16_t invalidIndex = std::numeric_limits<uint16_t>::max();

    uint16_t index { invalidIndex };
    uint16_t generation { 0 };

    constexpr bool valid() const noexcept
    {
        return index != invalidIndex && generation != 0;
    }
};

constexpr bool operator==(NoteInstanceId lhs, NoteInstanceId rhs) noexcept
{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

constexpr bool operator!=(NoteInstanceId lhs, NoteInstanceId rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace sfz
