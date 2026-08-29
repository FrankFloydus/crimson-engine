////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/curve_asset_payload.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_frame.hpp"

namespace quader {

/** Appends SDK-sampled strokes without inventing polygon geometry or closure. */
inline void append_curve_overlay_stroke(
    ViewportOverlayFrame &frame, const CurveSamplePayload &payload,
    const QMat4 &world_transform, ViewportOverlayLine style) {
  if (!payload.error_message.empty() || payload.samples.size() < 2u) {
    return;
  }
  frame.lines.reserve(frame.lines.size() + payload.samples.size() - 1u);
  for (std::size_t index = 1; index < payload.samples.size(); ++index) {
    const auto &first = payload.samples[index - 1u].position;
    const auto &second = payload.samples[index].position;
    style.start = transform_point(world_transform, {first.x, first.y, first.z});
    style.end = transform_point(world_transform, {second.x, second.y, second.z});
    if (style.start.x == style.end.x && style.start.y == style.end.y &&
        style.start.z == style.end.z) {
      continue;
    }
    frame.lines.push_back(style);
  }
}

} // namespace quader
