////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/curve_tool_preview.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>
#include <memory>

namespace quader {

struct CurveSamplePayload;

/// Retains draft strokes and editable controls until their neutral state changes.
class CurveToolOverlayCache final {
public:
  /// Borrows the retained frame until the next changed preview or reset.
  [[nodiscard]] const ViewportOverlayFrame &frame(
      const CurveToolPreviewState &preview);
  void reset();
  [[nodiscard]] std::uint64_t build_count() const noexcept { return build_count_; }

private:
  ViewportOverlayFrame frame_;
  CurveToolPreviewState previous_;
  std::shared_ptr<const CurveSamplePayload> samples_;
  std::uint64_t build_count_ = 0;
  bool valid_ = false;
};

} // namespace quader
