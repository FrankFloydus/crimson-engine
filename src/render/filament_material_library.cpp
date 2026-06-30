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

bool FilamentMaterialLibrary::initialize(filament::Engine& engine, const std::filesystem::path& executableDir) {
    engine_ = &engine;
    try {
        const std::filesystem::path materialDir = executableDir / std::string(kFilamentMaterialDirectory);
        LOG_INFO("Loading Quader mesh material");
        quaderMeshMaterial_ = load(materialDir / std::string(kQuaderMeshMaterialFile));
        LOG_INFO("Loading Quader lit mesh material");
        quaderMeshLitMaterial_ = load(materialDir / std::string(kQuaderMeshLitMaterialFile));
        LOG_INFO("Loading vertex color overlay material");
        vertexColorOverlayMaterial_ = load(materialDir / std::string(kVertexColorOverlayMaterialFile));
        LOG_INFO("Loading overlay line material");
        overlayLineMaterial_ = load(materialDir / std::string(kOverlayLineMaterialFile));
        LOG_INFO("Loading overlay point material");
        overlayPointMaterial_ = load(materialDir / std::string(kOverlayPointMaterialFile));
        LOG_INFO("Loading ImGui material");
        imguiMaterial_ = load(materialDir / std::string(kImguiMaterialFile));
        LOG_INFO("Loading ground grid material");
        groundGridMaterial_ = load(materialDir / std::string(kGroundGridMaterialFile));
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
    destroy(quaderMeshLitMaterial_);
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
