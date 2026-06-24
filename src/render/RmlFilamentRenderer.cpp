////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/RmlFilamentRenderer.h"

#include "core/Logger.h"
#include "render/FilamentBufferDescriptorFactory.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/Viewport.h>
#include <lodepng.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

/**
 * Represents an UI Vertex value used by the render extraction and Filament resource layer.
 */
struct UIVertex {
    filament::math::float3 position;
    filament::math::float2 uv;
    std::uint32_t color;
};

/**
 * Stores RmlUi geometry after conversion to the Filament vertex layout.
 */
struct CompiledGeometry {
    std::vector<UIVertex> vertices;
    std::vector<std::uint32_t> indices;
};

template <typename Color>
std::uint32_t packColor(Color color) {
    return static_cast<std::uint32_t>(color.red) |
            (static_cast<std::uint32_t>(color.green) << 8u) |
            (static_cast<std::uint32_t>(color.blue) << 16u) |
            (static_cast<std::uint32_t>(color.alpha) << 24u);
}

RenderTextureHandle toRenderTextureHandle(Rml::TextureHandle texture) {
    return static_cast<RenderTextureHandle>(texture);
}

Rml::TextureHandle toRmlTextureHandle(RenderTextureHandle texture) {
    return static_cast<Rml::TextureHandle>(texture);
}

void premultiplyRgbaPixels(std::vector<unsigned char>& pixels) {
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        const unsigned alpha = pixels[offset + 3];
        pixels[offset] = static_cast<unsigned char>((static_cast<unsigned>(pixels[offset]) * alpha + 127u) / 255u);
        pixels[offset + 1] = static_cast<unsigned char>((static_cast<unsigned>(pixels[offset + 1]) * alpha + 127u) / 255u);
        pixels[offset + 2] = static_cast<unsigned char>((static_cast<unsigned>(pixels[offset + 2]) * alpha + 127u) / 255u);
    }
}

} // namespace

RmlFilamentRenderer::~RmlFilamentRenderer() {
    shutdown();
}

void RmlFilamentRenderer::initialize(
        filament::Engine& engine,
        filament::Material& material,
        TextureRegistry& textures) {
    engine_ = &engine;
    material_ = &material;
    textures_ = &textures;

    cameraEntity_ = utils::EntityManager::get().create();
    camera_ = engine_->createCamera(cameraEntity_);

    clearScene_ = engine_->createScene();
    clearSkybox_ = filament::Skybox::Builder()
            .color({0.035f, 0.038f, 0.045f, 1.0f})
            .build(*engine_);
    clearScene_->setSkybox(clearSkybox_);
    clearView_ = engine_->createView();
    clearView_->setName("RmlUi Clear");
    clearView_->setScene(clearScene_);
    clearView_->setCamera(camera_);
    clearView_->setPostProcessingEnabled(false);
    clearView_->setBlendMode(filament::View::BlendMode::OPAQUE);

    LOG_INFO("Creating RmlUi Filament scene/view");
    scene_ = engine_->createScene();
    view_ = engine_->createView();
    view_->setName("RmlUi Overlay");
    view_->setScene(scene_);
    view_->setPostProcessingEnabled(false);
    view_->setBlendMode(filament::View::BlendMode::TRANSLUCENT);
    view_->setCamera(camera_);

    createWhiteTexture();
}

void RmlFilamentRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    releaseFrameResources();
    releaseOwnedTextures();
    if (whiteTexture_) {
        engine_->destroy(whiteTexture_);
        whiteTexture_ = nullptr;
    }
    if (view_) {
        view_->setCamera(nullptr);
        view_->setScene(nullptr);
        engine_->destroy(view_);
        view_ = nullptr;
    }
    if (clearView_) {
        clearView_->setCamera(nullptr);
        clearView_->setScene(nullptr);
        engine_->destroy(clearView_);
        clearView_ = nullptr;
    }
    if (scene_) {
        engine_->destroy(scene_);
        scene_ = nullptr;
    }
    if (clearScene_) {
        clearScene_->setSkybox(nullptr);
    }
    if (clearSkybox_) {
        engine_->destroy(clearSkybox_);
        clearSkybox_ = nullptr;
    }
    if (clearScene_) {
        engine_->destroy(clearScene_);
        clearScene_ = nullptr;
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
    material_ = nullptr;
    textures_ = nullptr;
    textureSources_.clear();
}

void RmlFilamentRenderer::beginFrame(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    releaseFrameResources();
    framebufferWidth_ = std::max(1u, framebufferWidth);
    framebufferHeight_ = std::max(1u, framebufferHeight);
    drawOrder_ = 0u;

    if (!camera_) {
        return;
    }

    if (clearView_) {
        clearView_->setViewport({0, 0, framebufferWidth_, framebufferHeight_});
    }
    if (view_) {
        view_->setViewport({0, 0, framebufferWidth_, framebufferHeight_});
    }
    camera_->setProjection(filament::Camera::Projection::ORTHO,
            0.0,
            static_cast<double>(framebufferWidth_),
            static_cast<double>(framebufferHeight_),
            0.0,
            0.001,
            10.0);
    camera_->lookAt({0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
}

void RmlFilamentRenderer::render(filament::Renderer& renderer) {
    if (!view_) {
        return;
    }
    if (clearView_) {
        renderer.render(clearView_);
    }
    renderer.render(view_);
}

void RmlFilamentRenderer::registerTextureSource(
        std::string source,
        RenderTextureHandle textureHandle,
        std::uint32_t width,
        std::uint32_t height) {
    if (source.empty() || textureHandle == InvalidRenderTextureHandle) {
        return;
    }

    textureSources_[std::move(source)] = {
            .textureHandle = textureHandle,
            .width = std::max(1u, width),
            .height = std::max(1u, height),
    };
}

void RmlFilamentRenderer::clearTextureSources() {
    textureSources_.clear();
}

Rml::CompiledGeometryHandle RmlFilamentRenderer::CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty()) {
        return 0;
    }

    auto* geometry = new CompiledGeometry;
    geometry->vertices.reserve(vertices.size());
    for (const Rml::Vertex& vertex : vertices) {
        geometry->vertices.push_back({
                {vertex.position.x, vertex.position.y, 0.0f},
                {vertex.tex_coord.x, vertex.tex_coord.y},
                packColor(vertex.colour),
        });
    }
    geometry->indices.reserve(indices.size());
    for (const int index : indices) {
        if (index < 0 || static_cast<std::size_t>(index) >= geometry->vertices.size()) {
            delete geometry;
            return 0;
        }
        geometry->indices.push_back(static_cast<std::uint32_t>(index));
    }
    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void RmlFilamentRenderer::RenderGeometry(
        Rml::CompiledGeometryHandle geometryHandle,
        Rml::Vector2f translation,
        Rml::TextureHandle textureHandle) {
    if (!engine_ || !scene_ || !material_ || geometryHandle == 0) {
        return;
    }

    const auto* geometry = reinterpret_cast<const CompiledGeometry*>(geometryHandle);
    if (geometry->vertices.empty() || geometry->indices.empty()) {
        return;
    }

    filament::Texture* texture = whiteTexture_;
    TextureSampleColorSpace sampleColorSpace = TextureSampleColorSpace::DisplaySrgb;
    const RenderTextureHandle renderTextureHandle = toRenderTextureHandle(textureHandle);
    if (renderTextureHandle != InvalidRenderTextureHandle && textures_ != nullptr) {
        if (filament::Texture* registeredTexture = textures_->texture(renderTextureHandle)) {
            texture = registeredTexture;
            sampleColorSpace = textures_->sampleColorSpace(renderTextureHandle);
        }
    }
    if (!texture) {
        return;
    }

    std::vector<UIVertex> vertices;
    vertices.reserve(geometry->vertices.size());
    for (const UIVertex& vertex : geometry->vertices) {
        UIVertex translated = vertex;
        translated.position.x += translation.x;
        translated.position.y += translation.y;
        vertices.push_back(translated);
    }

    DrawResource resource;
    resource.entity = utils::EntityManager::get().create();
    resource.vertexBuffer = filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(vertices.size()))
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(UIVertex, position), sizeof(UIVertex))
            .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(UIVertex, uv), sizeof(UIVertex))
            .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(UIVertex, color), sizeof(UIVertex))
            .normalized(filament::VertexAttribute::COLOR)
            .build(*engine_);
    resource.vertexBuffer->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));

    resource.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(geometry->indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    resource.indexBuffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::vector(geometry->indices));

    resource.materialInstance = material_->createInstance();
    const filament::TextureSampler sampler(
            filament::TextureSampler::MinFilter::LINEAR,
            filament::TextureSampler::MagFilter::LINEAR,
            filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);
    resource.materialInstance->setParameter("albedo", texture, sampler);
    const float encodeToSrgb = sampleColorSpace == TextureSampleColorSpace::LinearSrgb ? 1.0f : 0.0f;
    resource.materialInstance->setParameter("encodeToSrgb", encodeToSrgb);

    if (scissorEnabled_) {
        const int left = std::clamp(scissorRegion_.Left(), 0, static_cast<int>(framebufferWidth_));
        const int top = std::clamp(scissorRegion_.Top(), 0, static_cast<int>(framebufferHeight_));
        const int right = std::clamp(scissorRegion_.Right(), 0, static_cast<int>(framebufferWidth_));
        const int bottom = std::clamp(scissorRegion_.Bottom(), 0, static_cast<int>(framebufferHeight_));
        const int width = right - left;
        const int height = bottom - top;
        if (width <= 0 || height <= 0) {
            if (resource.materialInstance) {
                engine_->destroy(resource.materialInstance);
            }
            if (resource.vertexBuffer) {
                engine_->destroy(resource.vertexBuffer);
            }
            if (resource.indexBuffer) {
                engine_->destroy(resource.indexBuffer);
            }
            if (resource.entity) {
                utils::EntityManager::get().destroy(resource.entity);
            }
            return;
        }
        resource.materialInstance->setScissor(
                static_cast<std::uint32_t>(left),
                framebufferHeight_ - static_cast<std::uint32_t>(bottom),
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height));
    }

    filament::RenderableManager::Builder(1)
            .boundingBox({{static_cast<float>(framebufferWidth_) * 0.5f, static_cast<float>(framebufferHeight_) * 0.5f, 0.0f},
                    {static_cast<float>(framebufferWidth_) * 0.5f, static_cast<float>(framebufferHeight_) * 0.5f, 1.0f}})
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .priority(0)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                    resource.vertexBuffer,
                    resource.indexBuffer,
                    0,
                    static_cast<std::uint32_t>(geometry->indices.size()))
            .material(0, resource.materialInstance)
            .blendOrder(0, drawOrder_++)
            .globalBlendOrderEnabled(0, true)
            .build(*engine_, resource.entity);
    scene_->addEntity(resource.entity);
    frameResources_.push_back(resource);
}

void RmlFilamentRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    delete reinterpret_cast<CompiledGeometry*>(geometry);
}

Rml::TextureHandle RmlFilamentRenderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) {
    const auto iterator = textureSources_.find(source);
    if (iterator != textureSources_.end() &&
            iterator->second.textureHandle != InvalidRenderTextureHandle) {
        textureDimensions = {
                static_cast<int>(iterator->second.width),
                static_cast<int>(iterator->second.height),
        };
        return toRmlTextureHandle(iterator->second.textureHandle);
    }

    Rml::FileInterface* fileInterface = Rml::GetFileInterface();
    if (!fileInterface) {
        return 0;
    }

    Rml::String encodedTexture;
    if (!fileInterface->LoadFile(source, encodedTexture)) {
        LOG_WARN("Unable to load UI texture '{}'", source);
        return 0;
    }

    std::vector<unsigned char> pixels;
    unsigned width = 0;
    unsigned height = 0;
    const unsigned error = lodepng::decode(
            pixels,
            width,
            height,
            reinterpret_cast<const unsigned char*>(encodedTexture.data()),
            encodedTexture.size());
    if (error != 0u) {
        LOG_ERROR("Failed to decode UI texture '{}': {}", source, lodepng_error_text(error));
        return 0;
    }
    premultiplyRgbaPixels(pixels);

    const Rml::TextureHandle texture = createOwnedTextureFromRgba(
            pixels.data(),
            pixels.size(),
            width,
            height);
    if (texture == 0) {
        return 0;
    }
    textureDimensions = {
            static_cast<int>(width),
            static_cast<int>(height),
    };
    return texture;
}

Rml::TextureHandle RmlFilamentRenderer::GenerateTexture(
        Rml::Span<const Rml::byte> source,
        Rml::Vector2i sourceDimensions) {
    return createOwnedTextureFromRgba(
            reinterpret_cast<const unsigned char*>(source.data()),
            source.size(),
            static_cast<std::uint32_t>(std::max(0, sourceDimensions.x)),
            static_cast<std::uint32_t>(std::max(0, sourceDimensions.y)));
}

void RmlFilamentRenderer::ReleaseTexture(Rml::TextureHandle texture) {
    const RenderTextureHandle handle = toRenderTextureHandle(texture);
    if (handle == InvalidRenderTextureHandle || isRegisteredTextureSourceHandle(handle)) {
        return;
    }

    const auto it = ownedTextureHandles_.find(handle);
    if (it == ownedTextureHandles_.end() || textures_ == nullptr || engine_ == nullptr) {
        return;
    }

    filament::Texture* filamentTexture = textures_->texture(handle);
    textures_->unregisterTexture(handle);
    ownedTextureHandles_.erase(it);
    if (filamentTexture) {
        engine_->destroy(filamentTexture);
    }
}

void RmlFilamentRenderer::EnableScissorRegion(bool enable) {
    scissorEnabled_ = enable;
}

void RmlFilamentRenderer::SetScissorRegion(Rml::Rectanglei region) {
    scissorRegion_ = region;
}

void RmlFilamentRenderer::releaseFrameResources() {
    if (!engine_) {
        return;
    }

    for (DrawResource& resource : frameResources_) {
        if (resource.entity) {
            scene_->remove(resource.entity);
            engine_->getRenderableManager().destroy(resource.entity);
        }
        if (resource.materialInstance) {
            engine_->destroy(resource.materialInstance);
            resource.materialInstance = nullptr;
        }
        if (resource.vertexBuffer) {
            engine_->destroy(resource.vertexBuffer);
            resource.vertexBuffer = nullptr;
        }
        if (resource.indexBuffer) {
            engine_->destroy(resource.indexBuffer);
            resource.indexBuffer = nullptr;
        }
        if (resource.entity) {
            utils::EntityManager::get().destroy(resource.entity);
            resource.entity.clear();
        }
    }
    frameResources_.clear();
}

void RmlFilamentRenderer::createWhiteTexture() {
    if (!engine_) {
        return;
    }

    const unsigned char whitePixel[4] = {255u, 255u, 255u, 255u};
    whiteTexture_ = filament::Texture::Builder()
            .width(1u)
            .height(1u)
            .levels(1)
            .format(filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::UPLOADABLE | filament::Texture::Usage::SAMPLEABLE)
            .build(*engine_);
    whiteTexture_->setImage(*engine_, 0, FilamentBufferDescriptorFactory::rgbaPixels(whitePixel, sizeof(whitePixel)));
}

Rml::TextureHandle RmlFilamentRenderer::createOwnedTextureFromRgba(
        const unsigned char* pixels,
        std::size_t byteCount,
        std::uint32_t width,
        std::uint32_t height) {
    if (!engine_ || !textures_ || !pixels || byteCount == 0u || width == 0u || height == 0u) {
        return 0;
    }

    const std::size_t expectedByteCount =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (byteCount < expectedByteCount) {
        return 0;
    }

    filament::Texture* texture = filament::Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .format(filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::UPLOADABLE | filament::Texture::Usage::SAMPLEABLE)
            .build(*engine_);
    texture->setImage(*engine_, 0, FilamentBufferDescriptorFactory::rgbaPixels(pixels, expectedByteCount));

    const RenderTextureHandle handle = textures_->registerTexture(texture, TextureSampleColorSpace::DisplaySrgb);
    ownedTextureHandles_.insert(handle);
    return toRmlTextureHandle(handle);
}

bool RmlFilamentRenderer::isRegisteredTextureSourceHandle(RenderTextureHandle textureHandle) const {
    return std::any_of(textureSources_.begin(), textureSources_.end(), [textureHandle](const auto& entry) {
        return entry.second.textureHandle == textureHandle;
    });
}

void RmlFilamentRenderer::releaseOwnedTextures() {
    if (!engine_ || !textures_) {
        ownedTextureHandles_.clear();
        return;
    }

    std::vector<RenderTextureHandle> handles(ownedTextureHandles_.begin(), ownedTextureHandles_.end());
    for (const RenderTextureHandle handle : handles) {
        filament::Texture* texture = textures_->texture(handle);
        textures_->unregisterTexture(handle);
        if (texture) {
            engine_->destroy(texture);
        }
    }
    ownedTextureHandles_.clear();
}

} // namespace quader
