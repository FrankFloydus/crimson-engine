////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filament/Engine.h>
#include <filament/Material.h>

#include <filesystem>

namespace quader {

/**
 * Owns Filament Material Library behavior within the render extraction and Filament resource layer.
 */
class FilamentMaterialLibrary {
public:
    FilamentMaterialLibrary() = default;
    FilamentMaterialLibrary(const FilamentMaterialLibrary&) = delete;
    FilamentMaterialLibrary& operator=(const FilamentMaterialLibrary&) = delete;
    FilamentMaterialLibrary(FilamentMaterialLibrary&&) = delete;
    FilamentMaterialLibrary& operator=(FilamentMaterialLibrary&&) = delete;
    ~FilamentMaterialLibrary();

    bool initialize(filament::Engine &engine,
                    const std::filesystem::path &executable_dir);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr;
    }

    filament::Material &quader_mesh() { return *quader_mesh_material_; }
    filament::Material &quader_mesh_lit() { return *quader_mesh_lit_material_; }
    filament::Material &vertex_color_overlay() {
      return *vertex_color_overlay_material_;
    }
    filament::Material &overlay_line() { return *overlay_line_material_; }
    filament::Material &selected_object_line() {
      return *selected_object_line_material_;
    }
    filament::Material &edit_wire_line() { return *edit_wire_line_material_; }
    filament::Material &overlay_point() { return *overlay_point_material_; }
    filament::Material &overlay_icon() { return *overlay_icon_material_; }
    filament::Material &imgui() { return *imgui_material_; }
    filament::Material &ground_grid() { return *ground_grid_material_; }

  private:
    filament::Material* load(const std::filesystem::path& path) const;
    void destroy(filament::Material*& material);

    filament::Engine* engine_ = nullptr;
    filament::Material *quader_mesh_material_ = nullptr;
    filament::Material *quader_mesh_lit_material_ = nullptr;
    filament::Material *vertex_color_overlay_material_ = nullptr;
    filament::Material *overlay_line_material_ = nullptr;
    filament::Material *selected_object_line_material_ = nullptr;
    filament::Material *edit_wire_line_material_ = nullptr;
    filament::Material *overlay_point_material_ = nullptr;
    filament::Material *overlay_icon_material_ = nullptr;
    filament::Material *imgui_material_ = nullptr;
    filament::Material *ground_grid_material_ = nullptr;
};

} // namespace quader
