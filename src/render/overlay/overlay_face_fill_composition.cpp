////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_face_fill_composition.hpp"

#include <filament/MaterialInstance.h>

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

void apply_overlay_face_fill_stencil(filament::MaterialInstance &material,
                                     OverlayFaceFillClass fill_class,
                                     OverlayFaceFillPass pass,
                                     bool enabled) {
  using Compare = filament::MaterialInstance::StencilCompareFunc;
  using Operation = filament::MaterialInstance::StencilOperation;
  const bool uses_stencil = enabled && fill_class != OverlayFaceFillClass::None;
  const bool mask_pass = pass == OverlayFaceFillPass::Mask;
  material.setStencilWrite(uses_stencil);
  material.setStencilCompareFunction(
      !uses_stencil ? Compare::A : mask_pass ? Compare::GE : Compare::E);
  material.setStencilReferenceValue(
      static_cast<std::uint8_t>(uses_stencil ? fill_class
                                           : OverlayFaceFillClass::None));
  const std::uint8_t read_mask =
      !uses_stencil ? std::uint8_t{0u}
                   : mask_pass ? kOverlayFaceFillPriorityMask
                               : kOverlayFaceFillStencilMask;
  const std::uint8_t write_mask =
      !uses_stencil ? std::uint8_t{0u}
                   : mask_pass ? kOverlayFaceFillPriorityMask
                               : kOverlayFaceFillConsumedMask;
  material.setStencilReadMask(read_mask);
  material.setStencilWriteMask(write_mask);
  material.setStencilOpStencilFail(Operation::KEEP);
  material.setStencilOpDepthFail(Operation::KEEP);
  // Reference >= stored priority selects the strongest semantic tint. The
  // later color pass flips only the consumed bit, preventing a second blend.
  material.setStencilOpDepthStencilPass(
      !uses_stencil ? Operation::KEEP
                   : mask_pass ? Operation::REPLACE : Operation::INVERT);
}

void apply_overlay_silhouette_stencil(filament::MaterialInstance &material,
                                     OverlaySilhouettePass pass,
                                     bool enabled) {
  using Compare = filament::MaterialInstance::StencilCompareFunc;
  using Operation = filament::MaterialInstance::StencilOperation;
  const bool uses_stencil = enabled && pass != OverlaySilhouettePass::None;
  const bool visible = uses_stencil && pass == OverlaySilhouettePass::Visible;
  material.setStencilWrite(visible);
  material.setStencilCompareFunction(
      !uses_stencil || visible ? Compare::A : Compare::E);
  material.setStencilReferenceValue(
      visible ? kOverlaySilhouetteVisibleStencilMask : 0u);
  material.setStencilReadMask(
      uses_stencil ? kOverlaySilhouetteVisibleStencilMask : 0u);
  material.setStencilWriteMask(
      visible ? kOverlaySilhouetteVisibleStencilMask : 0u);
  material.setStencilOpStencilFail(Operation::KEEP);
  material.setStencilOpDepthFail(Operation::KEEP);
  material.setStencilOpDepthStencilPass(visible ? Operation::REPLACE
                                               : Operation::KEEP);
}

} // namespace quader
