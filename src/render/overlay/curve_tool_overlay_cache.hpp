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

/** Fixed-size Curve owner facts used before retained geometry is compiled. */
struct CurveToolOverlaySourceStamp final {
  std::uint64_t preview_revision = 0u;
  std::uint64_t document_identity = 0u;
  std::uint64_t document_revision = 0u;
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = -1;
  bool active = false;
  bool drawing = false;
  bool dragging = false;
  bool document_available = false;

  friend bool operator==(const CurveToolOverlaySourceStamp &,
                         const CurveToolOverlaySourceStamp &) = default;
};

/// Retains draft strokes and editable controls until their neutral state changes.
class CurveToolOverlayCache final {
public:
  [[nodiscard]] static CurveToolOverlaySourceStamp
  source_stamp(const CurveToolPreviewState &preview) noexcept;
  /// Borrows the retained frame until the next changed preview or reset.
  [[nodiscard]] const ViewportOverlayFrame &frame(
      const CurveToolPreviewState &preview);
  void reset();
  [[nodiscard]] std::uint64_t build_count() const noexcept { return build_count_; }
  [[nodiscard]] std::uint64_t publication_revision() const noexcept {
    return publication_revision_;
  }

private:
  ViewportOverlayFrame frame_;
  CurveToolOverlaySourceStamp previous_stamp_;
  std::shared_ptr<const CurveSamplePayload> samples_;
  std::uint64_t build_count_ = 0;
  std::uint64_t publication_revision_ = 0;
  bool valid_ = false;
};

} // namespace quader
