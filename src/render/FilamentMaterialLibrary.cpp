////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/FilamentMaterialLibrary.h"

#include "core/Logger.h"
#include "render/MaterialCompilerService.h"

#include <stdexcept>
#include <vector>

namespace quader {

FilamentMaterialLibrary::~FilamentMaterialLibrary() {
    shutdown();
}

bool FilamentMaterialLibrary::initialize(filament::Engine& engine, const std::filesystem::path& executableDir) {
    engine_ = &engine;
    try {
        LOG_INFO("Loading Quader mesh material");
        quaderMeshMaterial_ = load(executableDir / "materials" / "quader_mesh.filamat");
        LOG_INFO("Loading vertex color overlay material");
        vertexColorOverlayMaterial_ = load(executableDir / "materials" / "vertex_color_overlay.filamat");
        LOG_INFO("Loading overlay line material");
        overlayLineMaterial_ = load(executableDir / "materials" / "quader_overlay_line.filamat");
        LOG_INFO("Loading overlay point material");
        overlayPointMaterial_ = load(executableDir / "materials" / "quader_overlay_point.filamat");
        LOG_INFO("Loading ImGui material");
        imguiMaterial_ = load(executableDir / "materials" / "imgui.filamat");
        LOG_INFO("Loading ground grid material");
        groundGridMaterial_ = load(executableDir / "materials" / "ground_grid.filamat");
    } catch (const std::exception& error) {
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

    destroy(quaderMeshMaterial_);
    destroy(vertexColorOverlayMaterial_);
    destroy(overlayLineMaterial_);
    destroy(overlayPointMaterial_);
    destroy(imguiMaterial_);
    destroy(groundGridMaterial_);
    engine_ = nullptr;
}

filament::Material* FilamentMaterialLibrary::load(const std::filesystem::path& path) const {
    const std::vector<std::byte> package = MaterialCompilerService::loadPackage(path);
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
