// -----------------------------------------------------------------------------
// sfizioso.hpp — brand surface for the sfizioso SFZ engine.
//
// sfizioso is an independent fork of sfizz (https://github.com/sfztools/sfizz),
// not endorsed by or affiliated with the sfizz authors. The engine internals
// remain in namespace `sfz`; this header exposes them under the `sfizioso`
// brand so consumers can write `sfizioso::Sfizz` / `sfizioso::Sfizioso` without
// the engine's internal symbols being renamed (which would poison merges and
// buys nothing user-visible). See NOTICE for fork attribution; the original
// BSD-2-Clause license and copyright notices are retained in LICENSE.md.
// -----------------------------------------------------------------------------
#pragma once

#include "sfizz.hpp"

namespace sfizioso {

// Everything in the engine namespace is reachable as `sfizioso::<name>`.
using namespace sfz;

// Brand-named alias of the primary engine class (`sfz::Sfizz`).
using Sfizioso = sfz::Sfizz;

} // namespace sfizioso
