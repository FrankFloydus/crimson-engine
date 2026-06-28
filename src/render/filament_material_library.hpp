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

    bool initialize(filament::Engine& engine, const std::filesystem::path& executableDir);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr;
    }

    filament::Material& quaderMesh() { return *quaderMeshMaterial_; }
    filament::Material& vertexColorOverlay() { return *vertexColorOverlayMaterial_; }
    filament::Material& overlayLine() { return *overlayLineMaterial_; }
    filament::Material& overlayPoint() { return *overlayPointMaterial_; }
    filament::Material& imgui() { return *imguiMaterial_; }
    filament::Material& groundGrid() { return *groundGridMaterial_; }

private:
    filament::Material* load(const std::filesystem::path& path) const;
    void destroy(filament::Material*& material);

    filament::Engine* engine_ = nullptr;
    filament::Material* quaderMeshMaterial_ = nullptr;
    filament::Material* vertexColorOverlayMaterial_ = nullptr;
    filament::Material* overlayLineMaterial_ = nullptr;
    filament::Material* overlayPointMaterial_ = nullptr;
    filament::Material* imguiMaterial_ = nullptr;
    filament::Material* groundGridMaterial_ = nullptr;
};

} // namespace quader
