////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_material_library.hpp"

#include "core/logger.hpp"
#include "render/filament_material_catalog.hpp"
#include "render/material_compiler_service.hpp"

#include <string>
#include <stdexcept>
#include <vector>

namespace quader {

FilamentMaterialLibrary::~FilamentMaterialLibrary() {
    shutdown();
}

bool FilamentMaterialLibrary::initialize(
    filament::Engine &engine, const std::filesystem::path &executable_dir) {
  engine_ = &engine;
  try {
    const std::filesystem::path material_dir =
        executable_dir / std::string(kFilamentMaterialDirectory);
    quader_mesh_material_ =
        load(material_dir / std::string(kQuaderMeshMaterialFile));
    quader_mesh_lit_material_ =
        load(material_dir / std::string(kQuaderMeshLitMaterialFile));
    vertex_color_overlay_material_ =
        load(material_dir / std::string(kVertexColorOverlayMaterialFile));
    instance_selection_fill_material_ =
        load(material_dir / std::string(kInstanceSelectionFillMaterialFile));
    overlay_line_material_ =
        load(material_dir / std::string(kOverlayLineMaterialFile));
    selected_object_line_material_ =
        load(material_dir / std::string(kSelectedObjectLineMaterialFile));
    selected_mesh_outline_mask_material_ =
        load(material_dir / std::string(kSelectedMeshOutlineMaskMaterialFile));
    selected_mesh_outline_composite_material_ =
        load(material_dir / std::string(kSelectedMeshOutlineCompositeMaterialFile));
    edit_wire_line_material_ =
        load(material_dir / std::string(kEditWireLineMaterialFile));
    overlay_point_material_ =
        load(material_dir / std::string(kOverlayPointMaterialFile));
    overlay_icon_material_ =
        load(material_dir / std::string(kOverlayIconMaterialFile));
    ground_grid_material_ =
        load(material_dir / std::string(kGroundGridMaterialFile));
    overlay_stencil_reset_material_ =
        load(material_dir / std::string(kOverlayStencilResetMaterialFile));
    component_selection_material_ =
        load(material_dir / std::string(kComponentSelectionMaterialFile));
  } catch (const std::exception &error) {
    LOG_ERROR("Material load failed: {}", error.what());
    shutdown();
    return false;
  }
  return true;
}

void FilamentMaterialLibrary::shutdown() {
    if (!engine_) {
        return;
    }

    destroy(quader_mesh_material_);
    destroy(quader_mesh_lit_material_);
    destroy(vertex_color_overlay_material_);
    destroy(instance_selection_fill_material_);
    destroy(overlay_line_material_);
    destroy(selected_object_line_material_);
    destroy(selected_mesh_outline_mask_material_);
    destroy(selected_mesh_outline_composite_material_);
    destroy(edit_wire_line_material_);
    destroy(overlay_point_material_);
    destroy(overlay_icon_material_);
    destroy(ground_grid_material_);
    destroy(overlay_stencil_reset_material_);
    destroy(component_selection_material_);
    engine_ = nullptr;
}

filament::Material* FilamentMaterialLibrary::load(const std::filesystem::path& path) const {
  const std::vector<std::byte> package =
      MaterialCompilerService::load_package(path);
  return filament::Material::Builder()
      .package(package.data(), package.size())
      .build(*engine_);
}

void FilamentMaterialLibrary::destroy(filament::Material*& material) {
    if (material) {
        engine_->destroy(material);
        material = nullptr;
    }
}

} // namespace quader
