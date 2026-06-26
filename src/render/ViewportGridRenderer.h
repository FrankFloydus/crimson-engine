////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportSettings.h"

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Scene.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>

#include <cstdint>

namespace quader {

/**
 * Owns Viewport Grid Renderer behavior within the render extraction and Filament resource layer.
 */
class ViewportGridRenderer {
public:
    ViewportGridRenderer() = default;
    ViewportGridRenderer(const ViewportGridRenderer&) = delete;
    ViewportGridRenderer& operator=(const ViewportGridRenderer&) = delete;
    ViewportGridRenderer(ViewportGridRenderer&&) = delete;
    ViewportGridRenderer& operator=(ViewportGridRenderer&&) = delete;
    ~ViewportGridRenderer();

    void initialize(filament::Engine& engine,
            filament::Scene& scene,
            filament::Material& groundGridMaterial,
            std::uint8_t gridLayerMask);
    void shutdown();

    void update(const ViewportCameraController& controller,
            std::uint32_t viewportWidth,
            std::uint32_t viewportHeight,
            float gridWorldSpacing,
            const ViewportSettings& settings,
            bool visible);

    bool visible() const {
        return visible_;
    }

private:
    void createGroundGrid();
    void updateGroundGridMesh(float width, float height);
    void destroyGroundGrid();

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::Material* groundGridMaterial_ = nullptr;
    utils::Entity groundGridEntity_;
    filament::VertexBuffer* groundGridVertexBuffer_ = nullptr;
    filament::IndexBuffer* groundGridIndexBuffer_ = nullptr;
    filament::MaterialInstance* groundGridMaterialInstance_ = nullptr;
    float groundGridMeshWidth_ = 0.0f;
    float groundGridMeshHeight_ = 0.0f;
    bool visible_ = true;
    std::uint8_t gridLayerMask_ = 0x04u;
};

} // namespace quader
