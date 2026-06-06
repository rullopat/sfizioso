# Changelog — sfizioso

sfizioso is a fork of [sfizz](https://github.com/sfztools/sfizz). This file
records only the changes made **in the fork**. For the history of the upstream
sfizz releases this fork is built on, see the sfizz project's own changelog.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

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
