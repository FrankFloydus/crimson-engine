////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/modeling_session_service.hpp"

namespace quader::test_support {

inline void append_overlay_frame(ViewportOverlayFrame &target,
                                 const ViewportOverlayFrame &source) {
  target.lines.reserve(target.lines.size() + source.lines.size());
  target.triangles.reserve(target.triangles.size() + source.triangles.size());
  target.points.reserve(target.points.size() + source.points.size());
  target.icons.reserve(target.icons.size() + source.icons.size());
  target.lines.insert(target.lines.end(), source.lines.begin(),
                      source.lines.end());
  target.triangles.insert(target.triangles.end(), source.triangles.begin(),
                          source.triangles.end());
  target.points.insert(target.points.end(), source.points.begin(),
                       source.points.end());
  target.icons.insert(target.icons.end(), source.icons.begin(),
                      source.icons.end());
  if (source.hover_edge_dimension_context.has_value()) {
    target.hover_edge_dimension_context = source.hover_edge_dimension_context;
  }
  target.hover_target_selected =
      target.hover_target_selected || source.hover_target_selected;
}

/// Composes the persistent and volatile streams exactly as the viewport does.
inline ViewportOverlayFrame
visible_modeling_overlay_frame(const ModelingSessionService &service) {
  ViewportOverlayFrame frame = service.selection_overlay_frame();
  append_overlay_frame(frame, service.volatile_overlay_frame());
  return frame;
}

} // namespace quader::test_support
