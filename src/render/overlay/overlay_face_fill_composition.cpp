////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_face_fill_composition.hpp"

#include <filament/MaterialInstance.h>

namespace quader {

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

void apply_overlay_edit_wire_stencil(filament::MaterialInstance &material,
                                     OverlayEditWirePass pass) {
  using Compare = filament::MaterialInstance::StencilCompareFunc;
  using Operation = filament::MaterialInstance::StencilOperation;
  const bool mask = pass == OverlayEditWirePass::ActiveMask;
  const bool ordinary = pass == OverlayEditWirePass::Ordinary;
  const bool active = pass == OverlayEditWirePass::Active;
  const bool supplement = pass == OverlayEditWirePass::ActiveSupplement;
  material.setColorWrite(!mask);
  material.setStencilWrite(mask || active || supplement);
  material.setStencilCompareFunction(ordinary || supplement ? Compare::E : Compare::A);
  material.setStencilReferenceValue(mask ? kOverlayActiveWireStencilMask :
      active ? kOverlayActiveWireConsumedStencilMask : 0u);
  material.setStencilReadMask(ordinary ? kOverlayActiveWireStencilMask :
      supplement ? kOverlayActiveWireConsumedStencilMask : 0u);
  material.setStencilWriteMask(mask ? kOverlayActiveWireStencilMask :
      active || supplement ? kOverlayActiveWireConsumedStencilMask : 0u);
  material.setStencilOpStencilFail(Operation::KEEP);
  material.setStencilOpDepthFail(Operation::KEEP);
  material.setStencilOpDepthStencilPass(mask || active ? Operation::REPLACE :
      supplement ? Operation::INVERT : Operation::KEEP);
}

} // namespace quader
