# Protocol-neutral MIDI routing and expression architecture

Status: SMPL-97 complete; scoped Program Change implemented for SMPL-99.

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
- MPE pressure/timbre renders the maximum of Member and Manager values; raw contexts retain separate values and presence flags;
- pedals retain zone/global scope;
- ordinary controller inheritance has one explicit policy;
- MPE Note/Member expression detaches at Note Off where required, while permitted Manager/Zone expression remains. Each voice snapshots bounded note-pitch, pressure and CC74 timelines before the logical-note slot is recycled, retaining the final Member values throughout release and pedal sustain. Pitch adds live Manager bend; pressure and timbre use the maximum of live Manager and live/frozen Member values before SFZ mapping. This block's timeline collapses to its final value after rendering; no audio-thread allocation is needed.

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

The implementation uses a compact `RoutingTarget` restricted to Global, Zone
and Channel scopes. `MidiState` retains one global value, 16 bounded zone
values and 256 explicit group/channel values. The original channel-less API
broadcasts to every routing context. A channel target updates that source and
the legacy global view; a zone target updates the zone, global view and all 16
sources in the corresponding group. Effectively channel-restricted layers read
the triggering `SourceAddress`, while omni layers retain their existing global
activation flag. Lower-Zone MPE Member Program Change is rejected by
`MidiInputAdapter` before any routing state changes.

## Implementation status

The first M1 milestone now establishes identity without moving expression storage:

- `SourceAddress` carries protocol group and channel; current MIDI 1.0 adapters emit group zero.
- `NoteInstanceId` uses a registry index plus generation and is shared by all attack, layered, sister, direct-release and pedal-delayed release voices from one Note On.
- `NoteRegistry` preallocates `max(256, 2 × configured polyphony)` bounded slots on the control thread. Note On/Off performs no allocation or lock; legacy repeated notes pair FIFO.
- Note identities are cleared with SFZ reload, voice reconfiguration, All Notes/Sound Off and explicit sound reset.
- Registry overflow is deterministic and counted; invalid IDs retain the old channel/note matching fallback rather than leaving voices stuck.
- The identity fields are compact (`SourceAddress` 2 bytes, `NoteInstanceId` 4 bytes); `TriggerEvent` is now 24 bytes on the measured x86-64 ABI.

Focused tests cover group/channel separation, FIFO pairing, generation-safe slot reuse, bounded overflow, layered voice sharing, delayed release propagation and repeated-note voice release. The complete Debug, Release and sanitizer suites remain the compatibility gate.

M2 now replaces the expression-vector topology while preserving the M1 identity contract:

- `ExpressionTarget` explicitly addresses Global, lower Zone, group/channel or generation-safe Note scopes. Controller IDs distinguish MIDI CC, SFZ ExtendedCC and future MIDI 2.0 registered/assignable namespaces.
- `ExpressionContext` stores canonical pitch, pressure and timbre plus dense SFZ-controller and Poly Pressure slots. SFZ load densifies controller timelines from the completed modulation matrix; CC74 is retained as the MPE profile timbre control.
- Every timeline is allocated/reserved on the control thread. Realtime insertion is sorted and bounded, never grows a container, and deterministically drops/counts an event when the timeline is full. A same-offset replacement remains accepted at capacity.
- Global compatibility retains scalar values for every existing SFZ controller number. Zone/channel contexts retain only configured dense slots plus small preallocated compatibility slots before an SFZ is loaded. Raw inheritance uses explicit presence flags, preserving intentional Member zero. Voice rendering combines MPE pressure and CC74 with the Manager using max, including for an explicit Member zero.
- The note registry has a parallel preallocated context slot per logical-note slot. Layered voices resolve the same context; Note Off detaches it; generation checks prevent stale access after reuse. Note timelines allow 64 within-block changes plus their delay-zero sentinel, and retired-slot overflow counts remain monotonic.
- Current `MidiState` C++ compatibility accessors continue to route through these contexts for source compatibility.

M3 now owns transport/profile policy at the input boundary:

- `MidiInputAdapter` owns Lower-Zone MPE Manager/Member classification, Manager-only and Poly Pressure filters, RPN/MCM parsing, bend ranges and conversion to canonical semitones, plus compact pre-note pitch/pressure/timbre seeds.
- Existing MIDI 1.0 entry points emit `ResolvedExpressionEvent` values with explicit Global, Zone, Channel or Note targets. MPE Member expression broadcasts to every active logical note on that Member Channel rather than selecting an arbitrary voice.
- `TriggerEvent::expressionTarget` records the broad expression scope selected at Note On. Generation-safe detachment on Note Off releases the logical slot; voice-owned snapshots retain Member pitch, pressure and CC74 while the Zone remains live.
- Voice pitch, crossfades, controller modulation, Channel Pressure and Poly Pressure consume explicit broad/note contexts. They no longer branch on MPE state or select expression through `triggerChannel_`; `Voice::expressionChannel()` remains only as a source-compatible diagnostic.
- `AddressedNoteExpressionEvent` is the future UMP adapter seam. Group/channel/note addressing broadcasts deterministically to every active match unless a valid `NoteInstanceId` selects one generation.
- All adapter and addressed-note dispatch paths are bounded, allocation-free and lock-free.

On the same x86-64 GCC/libstdc++ ABI, `sizeof(MidiState)` fell from 302,064 B at SMPL-93 to 58,592 B in M2. M3 is 58,656 B after adding the 512-bit note-controller reconfiguration mask (80.6% below SMPL-93); `TriggerEvent` is 32 B after adding its explicit broad target. `ExpressionContext` remains 168 B. Controller scalar banks and event payload are control-thread heap allocations only where required. The fixed `16 × 642 EventVector` topology and Member first-write allocation no longer exist.

The committed standalone dispatch benchmark compares SMPL-93 `a713070d` with
scoped-routing `f8557e04` using the same 256-frame workloads over 1, 100 and
1,000 regions and 1, 16 and 128 used controllers. First-block MPE allocations
fell from 300 to zero, malformed-overlap broadcast from 120 to zero, and dense
1,536-event traffic from 1,340 to zero. Representative 100-region active
traffic measured 0.94x baseline for legacy, 1.03x for ordinary MPE, 1.05x for
overlapping MPE and 1.14x for dense MPE. Full raw data, environment metadata
and reproduction instructions are in
[`design/benchmarks/smpl97-midi-dispatch.md`](benchmarks/smpl97-midi-dispatch.md).

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
