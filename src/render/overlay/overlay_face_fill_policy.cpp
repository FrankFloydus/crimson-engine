////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_face_fill_policy.hpp"

namespace quader {

OverlayFaceFillClass
overlay_face_fill_class(const ViewportOverlayTriangle &triangle) {
  if (visible_overlay_face_fill_class(OverlayFaceFillClass::Selection,
                                      triangle.color) ==
      OverlayFaceFillClass::None) {
    return OverlayFaceFillClass::None;
  }
  switch (triangle.layer) {
  case ViewportOverlayLayer::SelectedVisibleFace:
  case ViewportOverlayLayer::SelectedXrayFace:
  case ViewportOverlayLayer::SelectedFaceMask:
  case ViewportOverlayLayer::HoverFace:
    break;
  default:
    return OverlayFaceFillClass::None;
  }
  switch (triangle.style_role) {
  case ViewportOverlayStyleRole::SelectionFace:
    return OverlayFaceFillClass::Selection;
  case ViewportOverlayStyleRole::ActiveSelectionFace:
    return OverlayFaceFillClass::ActiveSelection;
  case ViewportOverlayStyleRole::HoverFace:
    return OverlayFaceFillClass::Hover;
  case ViewportOverlayStyleRole::FacePlaneHoverFace:
    return OverlayFaceFillClass::FacePlaneHover;
  case ViewportOverlayStyleRole::RemoveSelectionFace:
    return OverlayFaceFillClass::RemoveSelection;
  default:
    return triangle.layer == ViewportOverlayLayer::HoverFace
               ? OverlayFaceFillClass::Hover
               : OverlayFaceFillClass::Selection;
  }
}

} // namespace quader
