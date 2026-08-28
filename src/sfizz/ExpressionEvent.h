// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "MidiIdentity.h"

namespace sfz {

/**
 * @brief Canonical expression dimensions accepted by the engine core.
 *
 * LegacyPitch is the one compatibility exception: its normalized value is
 * interpreted through each SFZ region's bend_up/bend_down. Pitch values are
 * already resolved to semitones by the transport/profile adapter.
 */
enum class ExpressionEventKind : uint8_t {
    LegacyPitch,
    Pitch,
    Pressure,
    Timbre,
    Control,
    PolyPressure,
};

/**
 * @brief Protocol-neutral expression event produced at an input boundary.
 *
 * MIDI 1.0, MPE and future UMP decoders normalize protocol widths before
 * constructing this value. Core dispatch therefore has no transport branch.
 */
struct ResolvedExpressionEvent {
    ExpressionTarget target { ExpressionTarget::global() };
    ExpressionEventKind kind { ExpressionEventKind::Control };
    ExpressionControlId control { };
    int delay { 0 };
    int noteNumber { -1 };
    float value { 0.0f };
};

} // namespace sfz
