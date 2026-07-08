////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/input/quader_input_binding.hpp"
#include "viewport/viewport_input_types.hpp"

#include <quader/modeling/input/viewport_host_input_snapshot.hpp>

#include <cstdint>

namespace quader {

/**
 * Describes the native viewport origin used for SDK host input snapshots.
 */
struct ModelingSdkViewportInputOrigin {
  float x = 0.0F;
  float y = 0.0F;
};

/**
 * Encodes one native input chord into a stable SDK-neutral integer token.
 */
[[nodiscard]] std::uint32_t
modeling_sdk_encode_input_chord(const QuaderInputChord &chord);

/**
 * Converts a native semantic viewport frame into an SDK host input snapshot.
 */
[[nodiscard]] quader::modeling::ViewportHostInputSnapshot
modeling_sdk_viewport_input_snapshot(
    const ViewportInputFrame &frame,
    ModelingSdkViewportInputOrigin origin = {});

} // namespace quader
