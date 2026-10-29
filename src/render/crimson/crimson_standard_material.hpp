////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "render/material_evaluation_snapshot.hpp"
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include <memory>

namespace quader {
// Immutable render transport. Geometry/occurrences may copy the surface to
// apply effective sidedness while retaining these exact sampled resources.
struct CrimsonStandardMaterial final {
  crimson::ModelMaterial surface;
  crimson::ModelMaterialTextures bindings;
  std::shared_ptr<const void> lease;
  RenderStandardMaterialEvaluationSnapshot source;
};

// Called by the renderer-owning GUI thread during resource preparation. Invalid
// decoded/encoded images or failed allocations reject the candidate atomically.
std::shared_ptr<const CrimsonStandardMaterial> prepare_crimson_standard_material(
    const RenderStandardMaterialEvaluationSnapshot&,
    const std::shared_ptr<const CrimsonStandardMaterial>& previous={});
}
