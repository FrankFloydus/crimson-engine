////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_face_fill_policy.hpp"

namespace filament {
class MaterialInstance;
}

namespace quader {

/// Applies both-face stencil state, leaving color/depth/culling policy intact.
/// Vulkan supports the shared native/custom-target path. Other backends retain
/// the previous alpha composition because public custom targets lack stencil.
void apply_overlay_face_fill_stencil(filament::MaterialInstance &material,
                                     OverlayFaceFillClass fill_class,
                                     OverlayFaceFillPass pass,
                                     bool enabled = true);

/// Object outlines use an independent bit after face composition. Visible
/// color draws claim coverage; later hidden draws cannot blend into it.
void apply_overlay_silhouette_stencil(filament::MaterialInstance &material,
                                     OverlaySilhouettePass pass,
                                     bool enabled = true);

/// Changes only the independent active-wire stencil bit and color writes.
void apply_overlay_edit_wire_stencil(filament::MaterialInstance &material,
                                     OverlayEditWirePass pass);

} // namespace quader
