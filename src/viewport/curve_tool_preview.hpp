////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"

#include <quader/modeling/curves/curve_document.hpp>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace quader {

/// Identifies an anchor or one of its two local Bezier handle offsets.
enum class CurveControlKind { Anchor, IncomingHandle, OutgoingHandle };

/// Identifies a transient edit target without introducing another selection mode.
struct CurveControlTarget {
  modeling::CurvePointId point;
  CurveControlKind kind = CurveControlKind::Anchor;

  [[nodiscard]] auto operator<=>(const CurveControlTarget &) const = default;
};

/// Stores a canonical ascending control set separately from its active member.
struct CurveControlSelection {
  std::vector<CurveControlTarget> controls;
  std::optional<CurveControlTarget> active;

  [[nodiscard]] bool empty() const { return controls.empty(); }
  [[nodiscard]] bool contains(CurveControlTarget target) const {
    return std::binary_search(controls.begin(), controls.end(), target);
  }
  [[nodiscard]] bool operator==(const CurveControlSelection &) const = default;
};

/**
 * Pins toolkit-neutral authored controls for the current draw/edit session.
 * The service owns this cached view until its next mutation or refresh. Existing
 * curves pin the portable session's immutable document; only an uncommitted new
 * path is a tool-owned draft. Tessellation is left to the render owner.
 */
struct CurveToolPreviewState {
  bool active = false;
  bool drawing = false;
  bool dragging = false;
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = -1;
  std::optional<modeling::CurveDocument> document;
  QMat4 world_transform;
  CurveControlSelection control_selection;
  std::optional<QVec3> hover_world;
  std::uint64_t revision = 0;
};

/// Shares the exact editable-control visibility policy between owners and draw.
[[nodiscard]] inline bool curve_control_is_visible(
    const modeling::CurveDocument &document, bool drawing,
    std::size_t point_index, CurveControlKind kind) {
  if (point_index >= document.point_count()) {
    return false;
  }
  if (kind == CurveControlKind::Anchor) {
    return true;
  }
  const auto &point = document.points()[point_index];
  const bool incoming = kind == CurveControlKind::IncomingHandle;
  if (incoming && point_index == 0u && !document.closed()) {
    return false;
  }
  if (!incoming && point_index + 1u == document.point_count() &&
      !document.closed() && !drawing) {
    return false;
  }
  const modeling::Vec3 offset = incoming ? point.incoming_handle
                                        : point.outgoing_handle;
  return offset.x * offset.x + offset.y * offset.y + offset.z * offset.z >
         1.0e-12f;
}

[[nodiscard]] inline bool curve_control_is_visible(
    const CurveToolPreviewState &preview, std::size_t point_index,
    CurveControlKind kind) {
  return preview.active && preview.document &&
         curve_control_is_visible(*preview.document, preview.drawing,
                                  point_index, kind);
}

/// Converts authored local offsets to the control's local-space position.
[[nodiscard]] inline QVec3 curve_control_local_position(
    const modeling::CurveControlPoint &point, CurveControlKind kind) {
  const QVec3 anchor{point.position.x, point.position.y, point.position.z};
  if (kind == CurveControlKind::Anchor) {
    return anchor;
  }
  const modeling::Vec3 offset = kind == CurveControlKind::IncomingHandle
                                   ? point.incoming_handle
                                   : point.outgoing_handle;
  return anchor + QVec3{offset.x, offset.y, offset.z};
}

} // namespace quader
