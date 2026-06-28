////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_grid_renderer.hpp"

#include "core/color_space.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/render_layers.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"

#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr float kGroundGridFadeStart = 96.0f;
constexpr float kGroundGridFadeEnd = 1536.0f;
constexpr float kParentGridWorldSize = 2.0f;
constexpr float kHalfPi = 1.57079632679f;

/**
 * Represents a Ground Grid Vertex value used by the render extraction and Filament resource layer.
 */
struct GroundGridVertex {
    filament::math::float3 position;
};

filament::Box groundGridBounds() {
    filament::Box box;
    const float half = kViewportGridDefaultPlaneSize * 0.5f;
    box.set({-half, -0.01f, -half}, {half, 0.01f, half});
    return box;
}

filament::math::float4 settingColor(ViewportSettingColor color) {
    return {
            srgbToLinear(color.r),
            srgbToLinear(color.g),
            srgbToLinear(color.b),
            color.a,
    };
}

filament::math::mat4f groundGridTransform(const ViewportGridPlacement& grid) {
    const filament::math::float3 origin = quader::toFilament(grid.origin);
    if (grid.rotation == ViewportGridRotation::NegativeHalfPiAroundZ) {
        return filament::math::mat4f::translation(origin)
                * filament::math::mat4f::rotation(-kHalfPi, filament::math::float3{0.0f, 0.0f, 1.0f});
    }
    if (grid.rotation == ViewportGridRotation::PositiveHalfPiAroundX) {
        return filament::math::mat4f::translation(origin)
                * filament::math::mat4f::rotation(kHalfPi, filament::math::float3{1.0f, 0.0f, 0.0f});
    }
    return filament::math::mat4f::translation(origin);
}

/**
 * Rolls back partially-created ground-grid render resources until the renderer owns them.
 */
struct GroundGridCreationGuard {
    filament::Engine& engine;
    filament::Scene& scene;
    filament::VertexBuffer* vertexBuffer = nullptr;
    filament::IndexBuffer* indexBuffer = nullptr;
    filament::MaterialInstance* materialInstance = nullptr;
    utils::Entity entity;
    bool sceneAdded = false;
    bool renderableCreated = false;
    bool transformCreated = false;
    bool released = false;

    ~GroundGridCreationGuard() {
        if (released) {
            return;
        }
        if (entity) {
            if (sceneAdded) {
                scene.remove(entity);
            }
            if (renderableCreated) {
                engine.getRenderableManager().destroy(entity);
            }
            if (transformCreated) {
                engine.getTransformManager().destroy(entity);
            }
        }
        if (materialInstance) {
            engine.destroy(materialInstance);
        }
        if (vertexBuffer) {
            engine.destroy(vertexBuffer);
        }
        if (indexBuffer) {
            engine.destroy(indexBuffer);
        }
        if (entity) {
            utils::EntityManager::get().destroy(entity);
            entity.clear();
        }
    }

    void release() {
        released = true;
    }
};

} // namespace

ViewportGridRenderer::~ViewportGridRenderer() {
    shutdown();
}

void ViewportGridRenderer::initialize(filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& groundGridMaterial,
        std::uint8_t gridLayerMask) {
    shutdown();
    engine_ = &engine;
    scene_ = &scene;
    groundGridMaterial_ = &groundGridMaterial;
    gridLayerMask_ = gridLayerMask == 0u ? viewportGridLayerMask(0) : gridLayerMask;
    createGroundGrid();
}

void ViewportGridRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    destroyGroundGrid();
    engine_ = nullptr;
    scene_ = nullptr;
    groundGridMaterial_ = nullptr;
}

void ViewportGridRenderer::update(const ViewportCameraController& controller,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        float gridWorldSpacing,
        const ViewportSettings& settings,
        bool visible) {
    if (!engine_ || !scene_ || !groundGridEntity_) {
        return;
    }

    visible_ = visible;
    if (!visible) {
        return;
    }

    const ViewportCameraPose pose = controller.pose();
    const ViewportGridPlacement grid = ViewportGridPlacementPolicy::placement(
            controller,
            std::max(1u, viewportWidth),
            std::max(1u, viewportHeight));
    updateGroundGridMesh(grid.planeSize.x, grid.planeSize.y);

    auto& transformManager = engine_->getTransformManager();
    const filament::TransformManager::Instance transform = transformManager.getInstance(groundGridEntity_);
    if (transform) {
        transformManager.setTransform(transform, groundGridTransform(grid));
    }

    if (!groundGridMaterialInstance_) {
        return;
    }

    groundGridMaterialInstance_->setDepthCulling(controller.state().projection != ViewportProjection::Orthographic);
    const float spacing = std::max(gridWorldSpacing, 0.0001f);
    const float parentSpacing = std::max(kParentGridWorldSize, spacing);
    groundGridMaterialInstance_->setParameter("gridColor", settingColor(settings.color(ViewportColorSettingId::GridMinor)));
    groundGridMaterialInstance_->setParameter("majorGridColor", settingColor(settings.color(ViewportColorSettingId::GridMajor)));
    groundGridMaterialInstance_->setParameter("originUColor", settingColor(settings.color(grid.axes.uColor)));
    groundGridMaterialInstance_->setParameter("originVColor", settingColor(settings.color(grid.axes.vColor)));
    groundGridMaterialInstance_->setParameter("cameraPosition", filament::math::float3{pose.eye.x, pose.eye.y, pose.eye.z});
    groundGridMaterialInstance_->setParameter("gridUAxis", quader::toFilament(grid.axes.uAxis));
    groundGridMaterialInstance_->setParameter("gridVAxis", quader::toFilament(grid.axes.vAxis));
    groundGridMaterialInstance_->setParameter("gridSize", spacing);
    groundGridMaterialInstance_->setParameter("parentGridSize", parentSpacing);
    groundGridMaterialInstance_->setParameter("lineThickness", settings.floatValue(ViewportFloatSettingId::GridMinorLineSize));
    groundGridMaterialInstance_->setParameter("majorLineThickness", settings.floatValue(ViewportFloatSettingId::GridMajorLineSize));
    groundGridMaterialInstance_->setParameter("axisLineThickness", settings.floatValue(ViewportFloatSettingId::GridAxisLineSize));
    groundGridMaterialInstance_->setParameter("fadeStart", kGroundGridFadeStart);
    groundGridMaterialInstance_->setParameter("fadeEnd", kGroundGridFadeEnd);
    groundGridMaterialInstance_->setParameter("planeHalfSize", std::max(grid.planeSize.x, grid.planeSize.y) * 0.5f);
    groundGridMaterialInstance_->setParameter("cameraSize", std::max(0.0001f, controller.state().orthographicSize));
    groundGridMaterialInstance_->setParameter("viewportHeight", static_cast<float>(std::max(1u, viewportHeight)));
    groundGridMaterialInstance_->setParameter("visibleGridSize", spacing);
    groundGridMaterialInstance_->setParameter("minGridWorldSpacing", 0.001f);
}

void ViewportGridRenderer::createGroundGrid() {
    if (!engine_ || !scene_ || !groundGridMaterial_) {
        return;
    }

    const float half = kViewportGridDefaultPlaneSize * 0.5f;
    const std::vector<GroundGridVertex> vertices = {
            {{-half, 0.0f, -half}},
            {{half, 0.0f, -half}},
            {{half, 0.0f, half}},
            {{-half, 0.0f, half}},
    };
    const std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};

    GroundGridCreationGuard creationGuard{*engine_, *scene_};
    creationGuard.vertexBuffer = filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(vertices.size()))
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(GroundGridVertex, position), sizeof(GroundGridVertex))
            .build(*engine_);
    if (creationGuard.vertexBuffer == nullptr) {
        return;
    }
    creationGuard.vertexBuffer->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));

    creationGuard.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    if (creationGuard.indexBuffer == nullptr) {
        return;
    }
    creationGuard.indexBuffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::vector(indices));

    creationGuard.materialInstance = groundGridMaterial_->createInstance();
    if (creationGuard.materialInstance == nullptr) {
        return;
    }
    creationGuard.entity = utils::EntityManager::get().create();
    filament::RenderableManager::Builder(1)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                    creationGuard.vertexBuffer, creationGuard.indexBuffer)
            .material(0, creationGuard.materialInstance)
            .layerMask(kViewportAllLayerMask, gridLayerMask_)
            .boundingBox(groundGridBounds())
            .castShadows(false)
            .receiveShadows(false)
            .build(*engine_, creationGuard.entity);
    creationGuard.renderableCreated = true;

    scene_->addEntity(creationGuard.entity);
    creationGuard.sceneAdded = true;
    engine_->getTransformManager().create(creationGuard.entity);
    creationGuard.transformCreated = true;
    groundGridVertexBuffer_ = creationGuard.vertexBuffer;
    groundGridIndexBuffer_ = creationGuard.indexBuffer;
    groundGridMaterialInstance_ = creationGuard.materialInstance;
    groundGridEntity_ = creationGuard.entity;
    groundGridMeshWidth_ = kViewportGridDefaultPlaneSize;
    groundGridMeshHeight_ = kViewportGridDefaultPlaneSize;
    visible_ = true;
    creationGuard.release();
}

void ViewportGridRenderer::updateGroundGridMesh(float width, float height) {
    if (!engine_ || !groundGridVertexBuffer_) {
        return;
    }

    width = std::max(0.0001f, width);
    height = std::max(0.0001f, height);
    if (std::fabs(groundGridMeshWidth_ - width) <= 0.0001f &&
            std::fabs(groundGridMeshHeight_ - height) <= 0.0001f) {
        return;
    }

    const float halfX = width * 0.5f;
    const float halfZ = height * 0.5f;
    const std::vector<GroundGridVertex> vertices = {
            {{-halfX, 0.0f, -halfZ}},
            {{halfX, 0.0f, -halfZ}},
            {{halfX, 0.0f, halfZ}},
            {{-halfX, 0.0f, halfZ}},
    };
    groundGridVertexBuffer_->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));
    groundGridMeshWidth_ = width;
    groundGridMeshHeight_ = height;
}

void ViewportGridRenderer::destroyGroundGrid() {
    if (!engine_) {
        return;
    }
    if (groundGridEntity_) {
        if (scene_) {
            scene_->remove(groundGridEntity_);
        }
        engine_->getRenderableManager().destroy(groundGridEntity_);
        engine_->getTransformManager().destroy(groundGridEntity_);
    }
    if (groundGridMaterialInstance_) {
        engine_->destroy(groundGridMaterialInstance_);
        groundGridMaterialInstance_ = nullptr;
    }
    if (groundGridVertexBuffer_) {
        engine_->destroy(groundGridVertexBuffer_);
        groundGridVertexBuffer_ = nullptr;
    }
    groundGridMeshWidth_ = 0.0f;
    groundGridMeshHeight_ = 0.0f;
    if (groundGridIndexBuffer_) {
        engine_->destroy(groundGridIndexBuffer_);
        groundGridIndexBuffer_ = nullptr;
    }
    if (groundGridEntity_) {
        utils::EntityManager::get().destroy(groundGridEntity_);
        groundGridEntity_.clear();
    }
}

} // namespace quader
