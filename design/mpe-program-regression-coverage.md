# MPE, Program Change and channel routing regression coverage

Assessment date: 2026-09-06. Engine baseline: `c31bc48f` (1.2.0).

## Engine regressions

`tests/ProgramRoutingT.cpp` adds six cases covering the interactions between
MPE, `loprog` / `hiprog`, and `lochan` / `hichan`:

| Case | Contract protected |
| --- | --- |
| Held notes and release eligibility | Manager Program Change selects new attacks without replacing existing voices or their note identities. Member Program Change cannot undo it. Same-pitch notes on two Members release independently. Both `release_key` and `release` are checked, with and without Manager sustain. Voices eventually finish. |
| Queued sustain releases | Release-region eligibility is evaluated at Note Off. A release already queued under one program retains that selection when the Manager changes programs before pedal-up. The queue drains once. |
| Overlapping ranges and expression | Only channel/program-eligible layers start. Both layers of an overlapping Member receive its pitch, pressure and CC74; other Members remain isolated. Raw contexts retain separate Member and Manager values. Voice rendering adds pitch and, since 1.2.2, takes the maximum for pressure/CC74. |
| Explicit and RPN transitions | Manager changes replace earlier independent source selections; rejected Member changes do not mutate them. After disabling MPE, source changes are accepted independently again. Re-enabling restores the Manager-only policy. |
| Reused Member and release tail | A same-channel/same-pitch note under the new program receives a new logical identity. Its pitch, pressure and CC74 cannot modulate the old tail; the tail retains all three final Member values with live Manager expression. `MPEReleasePitchT.cpp` and `MPEReleaseExpressionT.cpp` verify rendered audio, both pedals, Manager changes and within-block timing. Both voices eventually finish. |
| Public C and C++ wrappers | Manager Program Change reaches restricted regions at MIDI channels 2 and 16; excluded channels do not play. Member changes are rejected. Program values 0 and 127 survive both public interfaces. |

The transition case clears sound between probes so program-state persistence
is tested independently of voice flushing. The explicit disable API flushes
voices; the RPN adapter changes mode without invoking that explicit API. This
case does not assert equivalence of their in-flight voice behavior.

The older `ChannelRoutingT.cpp` test is renamed to identify the channel-less
Program Change broadcast API it exercises, rather than implying that all
program routing is global.

## Validation

The full 599-case engine suite passes in Debug, Release, AddressSanitizer
(with leak detection enabled), and UndefinedBehaviorSanitizer (halt on error).
The focused Program routing selection passes 204 assertions in 13 cases,
including the seven pre-existing cases. The new tests use generated oscillator
samples and require no external instrument or MIDI hardware.

Run the focused tests from the repository root after building `sfizz_tests`:

```sh
./build-tests/library/bin/sfizz_tests '*Program routing*'
```

The existing Linux CI job runs the complete engine suite, so these additions
are automatically included. No runtime code or public API changes are needed.

## Consumer assessment

The following are recommended follow-up scopes, not changes implemented by
this engine regression work. Review baselines: sfizioso-player `ef6b05c`
(0.12.1), sample-machine `ac5540f`. Both consume engine 1.2.0.
The existing sfizioso-player target was rebuilt and all 24 CTest cases pass.
The sample-machine suite was reviewed but not executed on this Linux host.

### sfizioso-player: add focused integration coverage

`tests/PlayerEngineProgramChangeTests.cpp` already covers Manager acceptance,
Member rejection, program boundary values, and RPN enabling the filter earlier
in the same JUCE block. `tests/PlayerEngineChannelRoutingTests.cpp` separately
covers MPE channel ranges, Member CC32 rejection, and scoped programs with MPE
disabled.

The valuable additions are combinations at the wrapper boundary:

- A JUCE block containing Manager Program Change, a rejected Member change,
  and notes for restricted channels 2 and 16. Include notes before/after the
  program change and verify rendered onset offsets, region selection and
  excluded-channel silence.
- RPN enable and disable followed by channel-aware Program Change in the same
  block; verify that the live engine policy controls dispatch even when the
  stored `MpeMode` has not changed.
- Sustained notes and same-pitch Member reuse around a program switch through
  `PlayerEngine::processBlock`, with rendered-audio and final-silence checks.
- Processor-level MPE setting and instrument restoration through
  `PlayerProcessor::setStateInformation`; existing engine-only tests do not
  exercise the APVTS/preset restoration boundary.

Keep detailed expression-context and note-generation assertions in sfizioso.
The player tests should detect conversion, event-order, timing and state-restore
errors in the JUCE integration.

### sample-machine: cover the branded bundle and processor boundaries

`tests/player_core/ChannelRoutedBundleTests.cpp` exercises the real encrypted
WAV-to-FLAC bundler, `BundleLoader`, and the shared `PlayerEngine`, but its
fixture uses `MpeMode::None` and CC32 conditions. Add a separate MPE Full
fixture with channel/program-gated regions: assert that bundling preserves
all four range opcodes, Manager Program Change selects both Members, rejected
Member changes do not alter selection, and same-pitch/pedal releases end
without stuck audio.

`tests/player_core/PlayerEngineMpeTests.cpp` provides basic rendering and
Program Change smoke coverage. The additional consumer-owned gap is
`RomplerProcessor` state restoration: save/restore the brand preset and MPE
mode, then verify routing and release behavior. Duplicating the engine's full
MPE matrix here would not exercise a new boundary.

No new autosampler MPE regression suite is indicated: these features belong to
instrument playback. The current exporter does not expose channel/program
routing controls, and autosampler Program Change automation was removed. Its
`SfizzTestPlayer` audition path uses channel-less Note On/Off with MPE disabled.

The branded bundle suite requires macOS because the current AES implementation
uses CommonCrypto. Its CI already runs CTest on macOS. Real-host AU/VST3
validation remains a separate macOS desktop step; the existing host-routing
harness records the VST3 Program Change input limitation, so raw Program Change
must not become an assumed VST3 transport guarantee.
