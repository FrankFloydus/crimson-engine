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
    LOG_INFO("Loading Quader mesh material");
    quader_mesh_material_ =
        load(material_dir / std::string(kQuaderMeshMaterialFile));
    LOG_INFO("Loading Quader lit mesh material");
    quader_mesh_lit_material_ =
        load(material_dir / std::string(kQuaderMeshLitMaterialFile));
    LOG_INFO("Loading vertex color overlay material");
    vertex_color_overlay_material_ =
        load(material_dir / std::string(kVertexColorOverlayMaterialFile));
    LOG_INFO("Loading instance selection fill material");
    instance_selection_fill_material_ =
        load(material_dir / std::string(kInstanceSelectionFillMaterialFile));
    LOG_INFO("Loading overlay line material");
    overlay_line_material_ =
        load(material_dir / std::string(kOverlayLineMaterialFile));
    LOG_INFO("Loading selected object line material");
    selected_object_line_material_ =
        load(material_dir / std::string(kSelectedObjectLineMaterialFile));
    LOG_INFO("Loading edit wire line material");
    edit_wire_line_material_ =
        load(material_dir / std::string(kEditWireLineMaterialFile));
    LOG_INFO("Loading overlay point material");
    overlay_point_material_ =
        load(material_dir / std::string(kOverlayPointMaterialFile));
    LOG_INFO("Loading overlay icon material");
    overlay_icon_material_ =
        load(material_dir / std::string(kOverlayIconMaterialFile));
    LOG_INFO("Loading ground grid material");
    ground_grid_material_ =
        load(material_dir / std::string(kGroundGridMaterialFile));
  } catch (const std::exception &error) {
    LOG_ERROR("Material load failed: {}", error.what());
    shutdown();
    return false;
  }
  LOG_INFO("Filament materials loaded");
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
    destroy(edit_wire_line_material_);
    destroy(overlay_point_material_);
    destroy(overlay_icon_material_);
    destroy(ground_grid_material_);
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
