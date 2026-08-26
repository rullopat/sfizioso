# Protocol-neutral MIDI routing and expression architecture

Status: design draft for SMPL-97 and SMPL-99.

## Purpose

Sfizioso currently implements MPE expression with a complete `ChannelState` for each of 16 MIDI channels, and implements fixed-channel SFZ activation with a second source-channel state path. This is functional, but it treats a MIDI channel as expression identity, note identity and source routing at different points in the engine.

The replacement architecture must support these inputs through one core model:

- legacy MIDI 1.0 with global expression compatibility;
- fixed-channel MIDI 1.0 SFZ routing;
- MPE Manager/Member profile semantics;
- future MIDI 2.0 channel and per-note messages.

A complete MIDI 2.0 UMP decoder is not part of this refactor. The core boundary must nevertheless represent MIDI 2.0 without another storage redesign.

## Current cost and real-time issue

Measured on x86-64 GCC/libstdc++:

| Revision | `sizeof(MidiState)` | `sizeof(Layer)` | `sizeof(TriggerEvent)` |
|---|---:|---:|---:|
| pre-MPE `f5c6e29f` | 17,048 | 3,872 | 12 |
| MPE base `9fd8ccf8` | 248,176 | 3,872 | 16 |
| channel routing `a713070d` | 302,064 | 3,928 | 20 |

`EventVector` is 24 bytes. One current `ChannelState` embeds 642 vectors: 512 CC, 128 Polyphonic Key Pressure, Pitch and Channel Pressure. Sixteen states therefore cost about 246 KiB before payload allocation.

`MidiState::setSamplesPerBlock()` reserves payload for all 642 Manager vectors and none of the Member vectors. The first Member expression event can consequently allocate during real-time dispatch. Blindly reserving every vector for every channel would cost approximately 20 MiB at a 256-frame block or 80 MiB at a 1,024-frame block, before allocator overhead.

The new model must remove the fixed full-controller topology and the Member first-write allocation. Moving equivalent duplication behind pointers is not sufficient.

## Separate identities

### Source address

```cpp
struct SourceAddress {
    uint8_t group;   // zero for MIDI 1.0
    uint8_t channel; // zero-based 0..15
};
```

Source address is used for SFZ `lochan` / `hichan`, source-scoped articulation and diagnostics. MIDI 2.0 group handling is explicit; `lochan` / `hichan` remains a 1..16 channel condition.

### Logical note identity

```cpp
struct NoteInstanceId {
    uint16_t index;
    uint16_t generation;
};
```

One accepted Note On creates one logical note instance. Every layered, sister, attack and applicable release voice created by that event shares its ID. A preallocated active-note registry owns the context; stale generation values cannot address a recycled slot.

Legacy same-address/same-pitch Note Off pairs with the oldest outstanding Note On first. One Note Off releases all layered voices for that instance, not every overlapping same-pitch instance.

### Event target

```cpp
enum class TargetScope : uint8_t {
    Global,
    Zone,
    Channel,
    Note,
};

struct EventTarget {
    TargetScope scope;
    uint32_t id;
};
```

Continuous expression may target all four scopes. Non-continuous routing state such as Program Change uses Global, Zone or Channel, never Note.

Transport/profile adapters resolve raw messages into source addresses and targets before core dispatch.

## Canonical expression model

The core recognizes protocol-neutral dimensions:

- pitch, represented in resolved semitones;
- pressure and timbre, normalized to `[0, 1]`;
- bipolar controls, normalized to `[-1, 1]`;
- generic controller IDs with an explicit namespace.

MIDI CC, SFZ ExtendedCC and future MIDI 2.0 registered/assignable controllers are not silently collapsed into one numeric namespace.

A voice reads a composition of Global, Zone, Channel and Note contexts. Composition is semantic rather than inferred from empty vectors:

- independent pitch contributions are additive;
- note pressure/timbre overrides broader inherited values when present;
- pedals retain zone/global scope;
- ordinary controller inheritance has one explicit policy;
- MPE Note/Member expression detaches at release where required, while permitted Manager/Zone expression remains.

MPE bend ranges, MCM/RPN interpretation, Manager/Member filtering and release rules belong to the MPE adapter/profile resolver. Voice and modulation-source code consumes resolved contexts without branching on transport protocol.

## Storage

- The active-note registry is allocated on the control thread from configured polyphony plus documented release headroom.
- One note-expression context is shared by all voices belonging to a logical Note On.
- Global, configured Zone and configured Channel contexts are fixed and small.
- Controller slots are densified on SFZ load from controls used by the instrument/profile.
- Sample-offset events use a preallocated per-block timeline pool with deterministic overflow counters.
- MIDI/audio dispatch performs no allocation, lock, sparse insertion or unbounded search.

Malformed MPE that overlaps multiple notes on one Member Channel broadcasts later Member expression to all still-active logical notes on that member. A MIDI 2.0 per-note message addressed only by group/channel/note similarly targets all active matching instances unless a supported protocol mechanism provides a more specific identity.

## Program Change and `loprog` / `hiprog`

The existing `programChange(delay, program)` remains the global compatibility API.

After the shared source/target model exists, a source-aware API can implement:

- channel-less Program Change: update global and every source program context;
- channel-aware MIDI 1.0 with MPE off: update the source context and legacy global/omni view;
- MPE Mode 3 Manager Program Change: update Zone/global and every source context in the zone;
- MPE Mode 3 Member Program Change: reject before mutation;
- future MIDI 2.0 Program Change: target the explicit group/channel context.

Omni regions evaluate the global program view. Effectively channel-restricted regions evaluate the triggering event's source context. This preserves SMPL-98 behavior for ordinary instruments while allowing independent program conditions in fixed-channel instruments.

Program state is routing state, not note expression, and must not be inserted into expression controller banks.

## Migration sequence

1. Freeze current legacy, channel-routing and MPE behavior with regression tests and benchmark allocation/timing.
2. Introduce `SourceAddress`, `NoteInstanceId`, `EventTarget` and compatibility accessors.
3. Add the preallocated logical-note registry and migrate Note On/Off ownership.
4. Add expression contexts and dense used-control timelines behind current `MidiState` APIs.
5. Move MPE policy to a profile adapter and migrate Voice/modulation consumers.
6. Remove redundant `channelStates` expression topology and compatibility accessors.
7. Add scoped Program Change and source-aware `loprog` / `hiprog` state.
8. Add a synthetic MIDI 2.0 adapter seam before implementing byte-level UMP input.

Every migration commit must preserve public C/C++ source compatibility and pass Debug, Release and sanitizer suites. Runtime behavior changes, especially repeated-note ownership and scoped Program Change, land with focused tests and release notes.
