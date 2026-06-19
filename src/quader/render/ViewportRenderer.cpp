#include "quader/render/ViewportRenderer.h"

#include "quader/core/ColorSpace.h"
#include "quader/core/Logger.h"
#include "quader/render/FilamentUtils.h"

#include <backend/BufferDescriptor.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/Viewport.h>
#include <math/mat4.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr float kGroundPlaneSize = 8192.0f;
constexpr float kGroundGridYOffset = -0.002f;
constexpr float kGroundGridFadeStart = 96.0f;
constexpr float kGroundGridFadeEnd = 1536.0f;
constexpr float kParentGridWorldSize = 2.0f;
constexpr float kMinOrthographicSize = 0.01f;
constexpr float kOrthoGridViewportMargin = 1.25f;
constexpr float kHalfPi = 1.57079632679f;

struct GroundGridVertex {
    filament::math::float3 position;
};

enum class GroundGridPlane {
    XZ,
    YZ,
    XY,
};

struct GroundGridAxes {
    filament::math::float3 uAxis;
    filament::math::float3 vAxis;
    filament::math::float4 uColor;
    filament::math::float4 vColor;
};

struct GroundGridPlacement {
    GroundGridPlane plane = GroundGridPlane::XZ;
    filament::math::mat4f transform;
    filament::math::float2 planeSize = {kGroundPlaneSize, kGroundPlaneSize};
    GroundGridAxes axes = {};
};

template <typename T>
filament::backend::BufferDescriptor makeBufferDescriptor(const std::vector<T>& values) {
    const std::size_t byteCount = sizeof(T) * values.size();
    auto* bytes = new std::byte[byteCount];
    std::memcpy(bytes, values.data(), byteCount);
    return {bytes, byteCount, [](void* buffer, std::size_t, void*) {
                delete[] static_cast<std::byte*>(buffer);
            }};
}

filament::Box groundGridBounds() {
    filament::Box box;
    const float half = kGroundPlaneSize * 0.5f;
    box.set({-half, -0.01f, -half}, {half, 0.01f, half});
    return box;
}

GroundGridPlane groundGridPlane(const ViewportCameraController& controller, const ViewportCameraPose& pose) {
    if (controller.state().projection != ViewportProjection::Orthographic) {
        return GroundGridPlane::XZ;
    }

    Vec3 forward = normalize(pose.target - pose.eye);
    if (length(forward) <= 0.000001f) {
        forward = {0.0f, 0.0f, 1.0f};
    }

    const float absX = std::fabs(forward.x);
    const float absY = std::fabs(forward.y);
    const float absZ = std::fabs(forward.z);
    if (absX >= absY && absX >= absZ) {
        return GroundGridPlane::YZ;
    }
    if (absZ >= absX && absZ >= absY) {
        return GroundGridPlane::XY;
    }
    return GroundGridPlane::XZ;
}

filament::math::float2 groundGridPlaneSize(GroundGridPlane plane, const ViewportCameraController& controller, std::uint32_t width, std::uint32_t height) {
    if (controller.state().projection != ViewportProjection::Orthographic) {
        return {kGroundPlaneSize, kGroundPlaneSize};
    }

    const float aspect = std::max(0.0001f, static_cast<float>(std::max(1u, width)) / static_cast<float>(std::max(1u, height)));
    const float planeHeight = std::max(kMinOrthographicSize, controller.state().orthographicSize) * kOrthoGridViewportMargin;
    if (plane == GroundGridPlane::YZ) {
        return {planeHeight, planeHeight * aspect};
    }
    return {planeHeight * aspect, planeHeight};
}

GroundGridAxes groundGridAxes(GroundGridPlane plane) {
    const filament::math::float3 quaderX{1.0f, 0.0f, 0.0f};
    const filament::math::float3 quaderY{0.0f, 1.0f, 0.0f};
    const filament::math::float3 quaderZ{0.0f, 0.0f, -1.0f};
    const filament::math::float4 xColor{srgbToLinear(1.0f), srgbToLinear(0.239f), srgbToLinear(0.0f), 0.72f};
    const filament::math::float4 yColor{srgbToLinear(0.29f), srgbToLinear(0.58f), srgbToLinear(0.0f), 0.0f};
    const filament::math::float4 zColor{srgbToLinear(0.059f), srgbToLinear(0.612f), srgbToLinear(1.0f), 0.72f};

    if (plane == GroundGridPlane::YZ) {
        return {quaderZ, quaderY, zColor, yColor};
    }
    if (plane == GroundGridPlane::XY) {
        return {quaderX, quaderY, xColor, yColor};
    }
    return {quaderX, quaderZ, xColor, zColor};
}

filament::math::mat4f groundGridTransform(GroundGridPlane plane, const ViewportCameraPose& pose) {
    if (plane == GroundGridPlane::YZ) {
        const filament::math::float3 origin{kGroundGridYOffset, pose.eye.y, pose.eye.z};
        return filament::math::mat4f::translation(origin)
                * filament::math::mat4f::rotation(-kHalfPi, filament::math::float3{0.0f, 0.0f, 1.0f});
    }
    if (plane == GroundGridPlane::XY) {
        const filament::math::float3 origin{pose.eye.x, pose.eye.y, kGroundGridYOffset};
        return filament::math::mat4f::translation(origin)
                * filament::math::mat4f::rotation(kHalfPi, filament::math::float3{1.0f, 0.0f, 0.0f});
    }
    const filament::math::float3 origin{pose.eye.x, kGroundGridYOffset, pose.eye.z};
    return filament::math::mat4f::translation(origin);
}

GroundGridPlacement groundGridPlacement(const ViewportCameraController& controller,
        const ViewportCameraPose& pose,
        std::uint32_t width,
        std::uint32_t height) {
    GroundGridPlacement placement;
    placement.plane = groundGridPlane(controller, pose);
    placement.transform = groundGridTransform(placement.plane, pose);
    placement.planeSize = groundGridPlaneSize(placement.plane, controller, width, height);
    placement.axes = groundGridAxes(placement.plane);
    return placement;
}

} // namespace

ViewportRenderer::~ViewportRenderer() {
    shutdown();
}

void ViewportRenderer::initialize(filament::Engine& engine,
        filament::Scene& scene,
        TextureRegistry& textureRegistry,
        filament::Material& groundGridMaterial) {
    engine_ = &engine;
    scene_ = &scene;
    textureRegistry_ = &textureRegistry;
    groundGridMaterial_ = &groundGridMaterial;
    LOG_INFO("Creating viewport Filament view");
    view_ = engine_->createView();
    view_->setName("Main Viewport");
    view_->setScene(scene_);
    view_->setPostProcessingEnabled(false);

    LOG_INFO("Creating viewport camera");
    cameraEntity_ = utils::EntityManager::get().create();
    camera_ = engine_->createCamera(cameraEntity_);
    view_->setCamera(camera_);
    createGroundGrid();
    LOG_INFO("Creating initial viewport render target");
    resize(640, 480);
}

void ViewportRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    destroyRenderTarget();
    destroyGroundGrid();
    if (view_) {
        view_->setCamera(nullptr);
        view_->setScene(nullptr);
        engine_->destroy(view_);
        view_ = nullptr;
    }
    if (camera_) {
        engine_->destroyCameraComponent(cameraEntity_);
        camera_ = nullptr;
    }
    if (cameraEntity_) {
        utils::EntityManager::get().destroy(cameraEntity_);
        cameraEntity_.clear();
    }

    engine_ = nullptr;
    scene_ = nullptr;
    textureRegistry_ = nullptr;
    groundGridMaterial_ = nullptr;
}

void ViewportRenderer::resize(std::uint32_t width, std::uint32_t height) {
    width = std::max(1u, width);
    height = std::max(1u, height);
    if (renderTarget_ && width == width_ && height == height_) {
        return;
    }

    destroyRenderTarget();
    width_ = width;
    height_ = height;

    using Texture = filament::Texture;
    LOG_INFO("Allocating viewport render target {}x{}", width_, height_);
    colorTexture_ = Texture::Builder()
            .width(width_)
            .height(height_)
            .levels(1)
            .usage(Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE)
            .format(Texture::InternalFormat::RGBA8)
            .build(*engine_);

    depthTexture_ = Texture::Builder()
            .width(width_)
            .height(height_)
            .levels(1)
            .usage(Texture::Usage::DEPTH_ATTACHMENT)
            .format(Texture::InternalFormat::DEPTH24)
            .build(*engine_);

    renderTarget_ = filament::RenderTarget::Builder()
            .texture(filament::RenderTarget::AttachmentPoint::COLOR, colorTexture_)
            .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depthTexture_)
            .build(*engine_);

    textureId_ = textureRegistry_->registerTexture(colorTexture_, TextureSampleColorSpace::LinearSrgb);
    view_->setRenderTarget(renderTarget_);
    view_->setViewport({0, 0, width_, height_});
    LOG_INFO("Viewport render target ready {}x{} as texture id {}", width_, height_, textureId_);
}

void ViewportRenderer::updateCamera(const ViewportCameraController& controller) {
    const auto pose = controller.pose();
    camera_->lookAt(
            {pose.eye.x, pose.eye.y, pose.eye.z},
            {pose.target.x, pose.target.y, pose.target.z},
            {pose.up.x, pose.up.y, pose.up.z});
    const double aspect = static_cast<double>(width_) / static_cast<double>(height_);
    if (controller.state().projection == ViewportProjection::Orthographic) {
        const double halfHeight = std::max(0.0001f, controller.state().orthographicSize * 0.5f);
        const double halfWidth = halfHeight * aspect;
        camera_->setProjection(filament::Camera::Projection::ORTHO,
                -halfWidth, halfWidth,
                -halfHeight, halfHeight,
                controller.state().nearPlane, controller.state().farPlane);
    } else {
        camera_->setProjection(controller.state().fovDegrees, aspect,
                controller.state().nearPlane, controller.state().farPlane);
    }
}

void ViewportRenderer::updateGroundGrid(const ViewportCameraController& controller, float gridWorldSpacing, bool visible) {
    if (!engine_ || !scene_ || !groundGridEntity_) {
        return;
    }

    if (visible && !groundGridInScene_) {
        scene_->addEntity(groundGridEntity_);
        groundGridInScene_ = true;
    } else if (!visible && groundGridInScene_) {
        scene_->remove(groundGridEntity_);
        groundGridInScene_ = false;
    }
    if (!visible) {
        return;
    }

    const ViewportCameraPose pose = controller.pose();
    const GroundGridPlacement grid = groundGridPlacement(controller, pose, width_, height_);
    updateGroundGridMesh(grid.planeSize.x, grid.planeSize.y);
    auto& transformManager = engine_->getTransformManager();
    const filament::TransformManager::Instance transform = transformManager.getInstance(groundGridEntity_);
    if (transform) {
        transformManager.setTransform(transform, grid.transform);
    }

    if (!groundGridMaterialInstance_) {
        return;
    }

    const float spacing = std::max(gridWorldSpacing, 0.0001f);
    const float parentSpacing = std::max(kParentGridWorldSize, spacing);
    groundGridMaterialInstance_->setParameter("gridColor", filament::math::float4{srgbToLinear(0.8f), srgbToLinear(0.8f), srgbToLinear(0.8f), 0.05f});
    groundGridMaterialInstance_->setParameter("majorGridColor", filament::math::float4{srgbToLinear(0.298f), srgbToLinear(0.298f), srgbToLinear(0.298f), 0.459f});
    groundGridMaterialInstance_->setParameter("originUColor", grid.axes.uColor);
    groundGridMaterialInstance_->setParameter("originVColor", grid.axes.vColor);
    groundGridMaterialInstance_->setParameter("cameraPosition", filament::math::float3{pose.eye.x, pose.eye.y, pose.eye.z});
    groundGridMaterialInstance_->setParameter("gridUAxis", grid.axes.uAxis);
    groundGridMaterialInstance_->setParameter("gridVAxis", grid.axes.vAxis);
    groundGridMaterialInstance_->setParameter("gridSize", spacing);
    groundGridMaterialInstance_->setParameter("parentGridSize", parentSpacing);
    groundGridMaterialInstance_->setParameter("lineThickness", 0.32f);
    groundGridMaterialInstance_->setParameter("majorLineThickness", 0.36f);
    groundGridMaterialInstance_->setParameter("axisLineThickness", 0.65f);
    groundGridMaterialInstance_->setParameter("fadeStart", kGroundGridFadeStart);
    groundGridMaterialInstance_->setParameter("fadeEnd", kGroundGridFadeEnd);
    groundGridMaterialInstance_->setParameter("planeHalfSize", std::max(grid.planeSize.x, grid.planeSize.y) * 0.5f);
    groundGridMaterialInstance_->setParameter("cameraSize", std::max(0.0001f, controller.state().orthographicSize));
    groundGridMaterialInstance_->setParameter("viewportHeight", static_cast<float>(std::max(1u, height_)));
    groundGridMaterialInstance_->setParameter("visibleGridSize", spacing);
    groundGridMaterialInstance_->setParameter("minGridWorldSpacing", 0.001f);
}

void ViewportRenderer::render(filament::Renderer& renderer) {
    if (view_ && renderTarget_) {
        renderer.render(view_);
    }
}

void ViewportRenderer::createGroundGrid() {
    if (!engine_ || !scene_ || !groundGridMaterial_) {
        return;
    }

    const float half = kGroundPlaneSize * 0.5f;
    const std::vector<GroundGridVertex> vertices = {
            {{-half, 0.0f, -half}},
            {{half, 0.0f, -half}},
            {{half, 0.0f, half}},
            {{-half, 0.0f, half}},
    };
    const std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};

    groundGridVertexBuffer_ = filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(vertices.size()))
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(GroundGridVertex, position), sizeof(GroundGridVertex))
            .build(*engine_);
    groundGridVertexBuffer_->setBufferAt(*engine_, 0, makeBufferDescriptor(vertices));
    groundGridMeshWidth_ = kGroundPlaneSize;
    groundGridMeshHeight_ = kGroundPlaneSize;

    groundGridIndexBuffer_ = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    groundGridIndexBuffer_->setBuffer(*engine_, makeBufferDescriptor(indices));

    groundGridMaterialInstance_ = groundGridMaterial_->createInstance();
    groundGridEntity_ = utils::EntityManager::get().create();
    filament::RenderableManager::Builder(1)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                    groundGridVertexBuffer_, groundGridIndexBuffer_)
            .material(0, groundGridMaterialInstance_)
            .boundingBox(groundGridBounds())
            .castShadows(false)
            .receiveShadows(false)
            .build(*engine_, groundGridEntity_);

    engine_->getTransformManager().create(groundGridEntity_);
    scene_->addEntity(groundGridEntity_);
    groundGridInScene_ = true;
}

void ViewportRenderer::updateGroundGridMesh(float width, float height) {
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
    groundGridVertexBuffer_->setBufferAt(*engine_, 0, makeBufferDescriptor(vertices));
    groundGridMeshWidth_ = width;
    groundGridMeshHeight_ = height;
}

void ViewportRenderer::destroyGroundGrid() {
    if (!engine_) {
        return;
    }
    if (groundGridEntity_) {
        if (scene_ && groundGridInScene_) {
            scene_->remove(groundGridEntity_);
        }
        engine_->getRenderableManager().destroy(groundGridEntity_);
        engine_->getTransformManager().destroy(groundGridEntity_);
        groundGridInScene_ = false;
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

void ViewportRenderer::destroyRenderTarget() {
    if (textureRegistry_ && textureId_ != ImTextureID_Invalid) {
        textureRegistry_->unregisterTexture(textureId_);
        textureId_ = ImTextureID_Invalid;
    }
    if (renderTarget_) {
        engine_->destroy(renderTarget_);
        renderTarget_ = nullptr;
    }
    if (colorTexture_) {
        engine_->destroy(colorTexture_);
        colorTexture_ = nullptr;
    }
    if (depthTexture_) {
        engine_->destroy(depthTexture_);
        depthTexture_ = nullptr;
    }
}

} // namespace quader
