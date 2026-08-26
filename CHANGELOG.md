# Changelog — sfizioso

sfizioso is a fork of [sfizz](https://github.com/sfztools/sfizz). This file
records only the changes made **in the fork**. For the history of the upstream
sfizz releases this fork is built on, see the sfizz project's own changelog.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

### Added

- SFZ `lochan` / `hichan` region routing across MIDI channels 1–16.
- Source-channel isolation for channel-restricted CC conditions, keyswitches,
  sequences, note ownership, release triggers, pedals, bend and aftertouch.

### Changed

- MPE-off input now retains the original source channel for SFZ eligibility
  while continuing to collapse expression routing to channel 0. MPE Full still
  honors channel ranges without bypassing Manager-only message filters.
- `loprog` / `hiprog` remain global and unchanged.

## [1.1.0] - 2026-08-26

Forked from sfizz (1.2.3 development series).

### Added

- Host-provided sample reader callbacks in the C and C++ APIs, allowing samples
  to be loaded from memory with transparent fallback to normal disk reads.
- Linux CI coverage for the static engine library.

### Changed

- Raise the default C++ standard from C++14 to C++17 so GCC accepts the engine's
  hexadecimal floating-point literals and Linux builds work out of the box.

## [1.0.0] - 2026-06-06

Forked from sfizz (1.2.3 development series).

### Added

- **MPE support**: per-channel pitch / CC / aftertouch routing; configurable
  master and per-note pitch-bend ranges (summed per MPE 1.0); MPE
  auto-configuration from RPN 6 (MPE Configuration Message) and RPN 0 (Pitch
  Bend Sensitivity); parallel C and C++ API surfaces (`noteOnMPE` etc.).
- **MPE 1.0 conformance filters**:
  - drop Polyphonic Key Pressure on Member Channels (§2.2.7);
  - drop Manager-only CCs (pedals, mode/reset, Bank Select) received on Member
    Channels (§2.3.1 / §2.3.3);
  - route a released voice's PB / CP / CC#74 / Poly Key Pressure reads through
    the Manager Channel, so a release tail stops responding to traffic on a
    now-reused Member Channel (§2.2.6 / §2.2.7 / §2.2.8).
- **Brand surface**: `<sfizioso.hpp>` (namespace `sfizioso`, with a `Sfizioso`
  type alias) and a `sfizioso::sfizioso` CMake alias over the engine's own
  `sfizz::sfizz` target. Engine internals remain in `namespace sfz`.
- `NOTICE` documenting the independent-fork relationship; BSD-2-Clause retained
  from upstream sfizz.
