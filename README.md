# sfizioso

[![build](https://github.com/rullopat/sfizioso/actions/workflows/build.yml/badge.svg)](https://github.com/rullopat/sfizioso/actions/workflows/build.yml)

An independent, **MPE-capable fork** of the [sfizz] SFZ parser + synthesis C++ library.

> sfizioso is **not** endorsed by, nor affiliated with, the sfizz authors or the
> SFZTools project. It is licensed under the BSD 2-Clause License like sfizz; the
> original copyright notices are retained in [LICENSE.md](LICENSE.md), and the
> fork relationship is documented in [NOTICE](NOTICE).

## What sfizioso adds over sfizz

- **MPE support** — per-channel pitch / CC / aftertouch routing; configurable
  master and per-note pitch-bend ranges (summed per MPE 1.0); MPE
  auto-configuration from RPN 6 (MPE Configuration Message) and RPN 0 (Pitch
  Bend Sensitivity).
- **MPE 1.0 conformance filters** — drop Polyphonic Key Pressure on Member
  Channels (§2.2.7); drop Manager-only CCs (pedals, mode/reset, Bank Select)
  received on Member Channels (§2.3.1 / §2.3.3); route a released voice's
  PB / CP / CC#74 / Poly Key Pressure reads through the Manager Channel so a
  release tail stops responding to traffic on a now-reused Member Channel
  (§2.2.6 / §2.2.7 / §2.2.8).
- Parallel **C and C++ API** surfaces for the MPE additions.
- A small **brand surface**: `#include <sfizioso.hpp>` exposes the engine under
  `namespace sfizioso` (with a `sfizioso::Sfizioso` alias of the engine class),
  and CMake exposes a `sfizioso::sfizioso` library alias. The engine internals
  stay in `namespace sfz`, so the fork tracks closely against upstream sfizz.

## Using sfizioso

sfizioso is a C++17 library. Add it as a subdirectory / git submodule and link
the CMake target:

```cmake
add_subdirectory(sfizioso)
target_link_libraries(your_target PRIVATE sfizioso::sfizioso)
```

```cpp
#include <sfizioso.hpp>

sfizioso::Sfizioso synth;   // alias of sfz::Sfizz
```

The C and C++ APIs match sfizz's (`src/sfizz.h` / `src/sfizz.hpp`) plus the MPE
additions. Build options follow sfizz's `SFIZZ_*` CMake cache variables (the
static library is the default; JACK / render / tests / demos are off by default).

## Dependencies and licenses

The library can be compiled against either the `dr_libs` audio libraries (the
default) or `libsndfile`.

- [dr_libs] — MIT No Attribution license
- [libsndfile] — GNU Lesser General Public License v2.1

Primary dependencies:

- [Abseil] — Apache License 2.0
- [atomic_queue] by Maxim Egorushkin — MIT license
- [filesystem] by Steffen Schümann — BSD 3-Clause license
- [hiir] by Laurent de Soras — WTFPL v2 license
- [KISS FFT] by Mark Borgerding — BSD 3-Clause license
- [Surge tuning] by Paul Walker — MIT license
- [pugixml] by Arseny Kapoulkine — MIT license
- [cephes] by Stephen Moshier — BSD 3-Clause license
- [cpuid] by Steinwurf ApS — BSD 3-Clause license
- [faust-libraries] by GRAME, Julius O. Smith III and Eric Tarr — STK-4.3 and a
  permissive LGPL variant

Used by some subprojects:

- [Catch2] — Boost Software License 1.0
- [cxxopts] by Jarryd Beck — MIT license
- [fmidi] by Jean Pierre Cimalando — Boost Software License 1.0
- [libsamplerate] — BSD 2-Clause license

## License

sfizioso is **BSD 2-Clause** (see [LICENSE.md](LICENSE.md)), inherited from sfizz;
the changes in this fork are released under the same license. See [NOTICE](NOTICE)
for fork attribution and [AUTHORS.md](AUTHORS.md) for contributors.

[sfizz]:           https://github.com/sfztools/sfizz
[Abseil]:          https://abseil.io/
[atomic_queue]:    https://github.com/max0x7ba/atomic_queue/
[Catch2]:          https://github.com/catchorg/Catch2/
[filesystem]:      https://github.com/gulrak/filesystem/
[Surge tuning]:    https://surge-synth-team.org/tuning-library/
[pugixml]:         https://pugixml.org/
[cephes]:          https://www.netlib.org/cephes/
[cpuid]:           https://github.com/steinwurf/cpuid/
[dr_libs]:         https://github.com/mackron/dr_libs/
[faust-libraries]: https://github.com/grame-cncm/faustlibraries/
[hiir]:            http://ldesoras.free.fr/prod.html#src_hiir
[KISS FFT]:        http://kissfft.sourceforge.net/
[cxxopts]:         https://github.com/jarro2783/cxxopts/
[fmidi]:           https://github.com/jpcima/fmidi/
[libsamplerate]:   http://www.mega-nerd.com/SRC/
[libsndfile]:      http://www.mega-nerd.com/libsndfile/
