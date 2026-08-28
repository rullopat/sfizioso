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

/**
 * @brief Scope selected by a transport/profile adapter for expression.
 */
enum class ExpressionScope : uint8_t {
    Global,
    Zone,
    Channel,
    Note,
};

/**
 * @brief Protocol-neutral target of one resolved expression event.
 *
 * The packed id is deliberately transport-independent. Channel ids retain
 * group + channel, while note ids retain registry index + generation.
 */
struct ExpressionTarget {
    ExpressionScope scope { ExpressionScope::Global };
    uint32_t id { 0 };

    static constexpr ExpressionTarget global() noexcept
    {
        return { ExpressionScope::Global, 0 };
    }

    static constexpr ExpressionTarget zone(uint16_t zoneId) noexcept
    {
        return { ExpressionScope::Zone, zoneId };
    }

    static constexpr ExpressionTarget channel(SourceAddress source) noexcept
    {
        return { ExpressionScope::Channel,
            static_cast<uint32_t>((source.group << 4) | source.channel) };
    }

    static constexpr ExpressionTarget note(NoteInstanceId noteId) noexcept
    {
        return { ExpressionScope::Note,
            (static_cast<uint32_t>(noteId.generation) << 16) | noteId.index };
    }

    constexpr SourceAddress sourceAddress() const noexcept
    {
        return { static_cast<uint8_t>((id >> 4) & 0x0f),
            static_cast<uint8_t>(id & 0x0f) };
    }

    constexpr NoteInstanceId noteInstanceId() const noexcept
    {
        return { static_cast<uint16_t>(id & 0xffff),
            static_cast<uint16_t>(id >> 16) };
    }
};

constexpr bool operator==(ExpressionTarget lhs, ExpressionTarget rhs) noexcept
{
    return lhs.scope == rhs.scope && lhs.id == rhs.id;
}

constexpr bool operator!=(ExpressionTarget lhs, ExpressionTarget rhs) noexcept
{
    return !(lhs == rhs);
}

/**
 * Scope of a non-continuous source-routing event such as Program Change.
 * Routing never targets a logical note.
 */
enum class RoutingScope : uint8_t {
    Global,
    Zone,
    Channel,
};

struct RoutingTarget {
    RoutingScope scope { RoutingScope::Global };
    uint16_t id { 0 };

    static constexpr RoutingTarget global() noexcept
    {
        return { RoutingScope::Global, 0 };
    }

    static constexpr RoutingTarget zone(uint8_t zoneId) noexcept
    {
        return { RoutingScope::Zone, zoneId };
    }

    static constexpr RoutingTarget channel(SourceAddress source) noexcept
    {
        return { RoutingScope::Channel,
            static_cast<uint16_t>((source.group << 4) | source.channel) };
    }

    constexpr SourceAddress sourceAddress() const noexcept
    {
        return { static_cast<uint8_t>((id >> 4) & 0x0f),
            static_cast<uint8_t>(id & 0x0f) };
    }
};

constexpr bool operator==(RoutingTarget lhs, RoutingTarget rhs) noexcept
{
    return lhs.scope == rhs.scope && lhs.id == rhs.id;
}

constexpr bool operator!=(RoutingTarget lhs, RoutingTarget rhs) noexcept
{
    return !(lhs == rhs);
}

/**
 * Controller namespaces must remain distinct when protocols are normalized.
 */
enum class ExpressionControlNamespace : uint8_t {
    MidiCC,
    SfzExtendedCC,
    Midi2Registered,
    Midi2Assignable,
};

struct ExpressionControlId {
    ExpressionControlNamespace nameSpace { ExpressionControlNamespace::MidiCC };
    uint16_t number { 0 };

    static constexpr ExpressionControlId fromSfizzCC(int cc) noexcept
    {
        return cc < 128
            ? ExpressionControlId { ExpressionControlNamespace::MidiCC,
                  static_cast<uint16_t>(cc) }
            : ExpressionControlId { ExpressionControlNamespace::SfzExtendedCC,
                  static_cast<uint16_t>(cc) };
    }
};

constexpr bool operator==(ExpressionControlId lhs, ExpressionControlId rhs) noexcept
{
    return lhs.nameSpace == rhs.nameSpace && lhs.number == rhs.number;
}

constexpr bool operator!=(ExpressionControlId lhs, ExpressionControlId rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace sfz
