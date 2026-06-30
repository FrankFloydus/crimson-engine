////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/imgui_filament_renderer.hpp"

#include "core/logger.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"

#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/Viewport.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <utility>
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
 * Represents an UI Primitive value used by the render extraction and Filament resource layer.
 */
struct UIPrimitive {
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    filament::Texture* texture = nullptr;
    TextureSampleColorSpace sampleColorSpace = TextureSampleColorSpace::DisplaySrgb;
    std::uint32_t scissorLeft = 0;
    std::uint32_t scissorBottom = 0;
    std::uint32_t scissorWidth = 0;
    std::uint32_t scissorHeight = 0;
};

} // namespace

ImGuiFilamentRenderer::~ImGuiFilamentRenderer() {
    shutdown();
}

void ImGuiFilamentRenderer::initialize(filament::Engine& engine, filament::Material& material, TextureRegistry& textures) {
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
    clearView_->setName("ImGui Clear");
    clearView_->setScene(clearScene_);
    clearView_->setCamera(camera_);
    clearView_->setPostProcessingEnabled(false);
    clearView_->setBlendMode(filament::View::BlendMode::OPAQUE);

    LOG_INFO("Creating ImGui Filament scene/view");
    scene_ = engine_->createScene();
    view_ = engine_->createView();
    view_->setName("ImGui Overlay");
    view_->setScene(scene_);
    view_->setPostProcessingEnabled(false);
    view_->setBlendMode(filament::View::BlendMode::TRANSLUCENT);
    view_->setCamera(camera_);

    LOG_INFO("Creating ImGui font texture");
    createFontTexture();
}

void ImGuiFilamentRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    releaseFrameResources();

    if (fontTextureId_ != ImTextureID_Invalid) {
        textures_->unregisterTexture(fontTextureId_);
        fontTextureId_ = ImTextureID_Invalid;
    }
    if (fontTexture_) {
        engine_->destroy(fontTexture_);
        fontTexture_ = nullptr;
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
}

void ImGuiFilamentRenderer::prepare(ImDrawData* drawData, std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    if (!engine_ || !drawData || drawData->CmdListsCount == 0 || framebufferWidth == 0 || framebufferHeight == 0) {
        return;
    }

    clearView_->setViewport({0, 0, framebufferWidth, framebufferHeight});
    view_->setViewport({0, 0, framebufferWidth, framebufferHeight});
    camera_->setProjection(filament::Camera::Projection::ORTHO,
            0.0, static_cast<double>(framebufferWidth),
            static_cast<double>(framebufferHeight), 0.0,
            0.001, 10.0);
    camera_->lookAt({0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});

    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    const filament::TextureSampler sampler(
            filament::TextureSampler::MinFilter::LINEAR,
            filament::TextureSampler::MagFilter::LINEAR,
            filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

    auto& entityManager = utils::EntityManager::get();
    std::uint16_t drawOrder = 0;

    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* commandList = drawData->CmdLists[listIndex];
        if (commandList->VtxBuffer.Size == 0 || commandList->IdxBuffer.Size == 0) {
            continue;
        }

        std::vector<UIVertex> vertices;
        vertices.reserve(commandList->VtxBuffer.Size);
        for (const ImDrawVert& vertex : commandList->VtxBuffer) {
            vertices.push_back({
                    {vertex.pos.x, vertex.pos.y, 0.0f},
                    {vertex.uv.x, vertex.uv.y},
                    vertex.col,
            });
        }

        std::vector<std::uint32_t> indices;
        std::vector<UIPrimitive> primitives;
        indices.reserve(commandList->IdxBuffer.Size);
        primitives.reserve(commandList->CmdBuffer.Size);

        for (const ImDrawCmd& command : commandList->CmdBuffer) {
            if (command.UserCallback) {
                command.UserCallback(commandList, &command);
                continue;
            }
            if (command.ElemCount == 0 ||
                    command.IdxOffset + command.ElemCount > static_cast<unsigned int>(commandList->IdxBuffer.Size) ||
                    command.VtxOffset >= static_cast<unsigned int>(commandList->VtxBuffer.Size)) {
                LOG_WARN("Skipping invalid ImGui draw command: elem={} idxOffset={} vtxOffset={} idxCount={} vtxCount={}",
                        command.ElemCount, command.IdxOffset, command.VtxOffset,
                        commandList->IdxBuffer.Size, commandList->VtxBuffer.Size);
                continue;
            }

            ImVec2 clipMin{
                    (command.ClipRect.x - clipOffset.x) * clipScale.x,
                    (command.ClipRect.y - clipOffset.y) * clipScale.y,
            };
            ImVec2 clipMax{
                    (command.ClipRect.z - clipOffset.x) * clipScale.x,
                    (command.ClipRect.w - clipOffset.y) * clipScale.y,
            };
            clipMin.x = std::clamp(clipMin.x, 0.0f, static_cast<float>(framebufferWidth));
            clipMin.y = std::clamp(clipMin.y, 0.0f, static_cast<float>(framebufferHeight));
            clipMax.x = std::clamp(clipMax.x, 0.0f, static_cast<float>(framebufferWidth));
            clipMax.y = std::clamp(clipMax.y, 0.0f, static_cast<float>(framebufferHeight));
            const int scissorLeft = static_cast<int>(std::floor(clipMin.x));
            const int scissorTop = static_cast<int>(std::floor(clipMin.y));
            const int scissorRight = static_cast<int>(std::ceil(clipMax.x));
            const int scissorBottom = static_cast<int>(std::ceil(clipMax.y));
            const int scissorWidth = scissorRight - scissorLeft;
            const int scissorHeight = scissorBottom - scissorTop;
            if (scissorWidth <= 0 || scissorHeight <= 0) {
                continue;
            }

            filament::Texture* texture = textures_->texture(command.GetTexID());
            if (!texture) {
                LOG_WARN("Skipping ImGui draw command with unregistered texture id {}",
                        static_cast<std::uint64_t>(command.GetTexID()));
                continue;
            }

            const std::uint32_t primitiveOffset = static_cast<std::uint32_t>(indices.size());
            bool validIndices = true;
            for (unsigned int n = 0; n < command.ElemCount; ++n) {
                const auto sourceIndex = static_cast<std::uint32_t>(commandList->IdxBuffer[command.IdxOffset + n]);
                const std::uint32_t rebasedIndex = static_cast<std::uint32_t>(command.VtxOffset) + sourceIndex;
                if (rebasedIndex >= static_cast<std::uint32_t>(vertices.size())) {
                    validIndices = false;
                    break;
                }
                indices.push_back(rebasedIndex);
            }
            if (!validIndices) {
                indices.resize(primitiveOffset);
                LOG_WARN("Skipping ImGui draw command with out-of-range rebased indices");
                continue;
            }

            primitives.push_back({
                    .indexOffset = primitiveOffset,
                    .indexCount = command.ElemCount,
                    .texture = texture,
                    .sampleColorSpace = textures_->sampleColorSpace(command.GetTexID()),
                    .scissorLeft = static_cast<std::uint32_t>(scissorLeft),
                    .scissorBottom = framebufferHeight - static_cast<std::uint32_t>(scissorBottom),
                    .scissorWidth = static_cast<std::uint32_t>(scissorWidth),
                    .scissorHeight = static_cast<std::uint32_t>(scissorHeight),
            });
        }

        if (primitives.empty() || indices.empty()) {
            continue;
        }

        DrawResource resource;
        resource.entity = entityManager.create();
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
                    .indexCount(static_cast<std::uint32_t>(indices.size()))
                    .bufferType(filament::IndexBuffer::IndexType::UINT)
                    .build(*engine_);
        resource.indexBuffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::vector(indices));

        filament::RenderableManager::Builder builder(primitives.size());
        builder.boundingBox({{static_cast<float>(framebufferWidth) * 0.5f, static_cast<float>(framebufferHeight) * 0.5f, 0.0f},
                            {static_cast<float>(framebufferWidth) * 0.5f, static_cast<float>(framebufferHeight) * 0.5f, 1.0f}})
                .culling(false)
                .castShadows(false)
                .receiveShadows(false)
                .priority(0);

        resource.materialInstances.reserve(primitives.size());
        for (std::size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
            const UIPrimitive& primitive = primitives[primitiveIndex];
            filament::MaterialInstance* materialInstance = material_->createInstance();
            materialInstance->setParameter("albedo", primitive.texture, sampler);
            const float encodeToSrgb = primitive.sampleColorSpace == TextureSampleColorSpace::LinearSrgb ? 1.0f : 0.0f;
            materialInstance->setParameter("encodeToSrgb", encodeToSrgb);
            const float premultipliedAlpha =
                    primitive.sampleColorSpace == TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha
                    ? 1.0f
                    : 0.0f;
            materialInstance->setParameter("premultipliedAlpha", premultipliedAlpha);
            materialInstance->setScissor(primitive.scissorLeft, primitive.scissorBottom,
                    primitive.scissorWidth, primitive.scissorHeight);
            resource.materialInstances.push_back(materialInstance);

            builder.geometry(primitiveIndex, filament::RenderableManager::PrimitiveType::TRIANGLES,
                            resource.vertexBuffer, resource.indexBuffer, primitive.indexOffset, primitive.indexCount)
                    .material(primitiveIndex, materialInstance)
                    .blendOrder(primitiveIndex, drawOrder++)
                    .globalBlendOrderEnabled(primitiveIndex, true);
        }

        builder.build(*engine_, resource.entity);
        scene_->addEntity(resource.entity);
        frameResources_.push_back(std::move(resource));
    }
}

void ImGuiFilamentRenderer::render(filament::Renderer& renderer) {
    if (!view_) {
        return;
    }
    if (clearView_) {
        renderer.render(clearView_);
    }
    renderer.render(view_);
}

void ImGuiFilamentRenderer::createFontTexture() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* sourcePixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&sourcePixels, &width, &height);

    const std::size_t rowByteCount = static_cast<std::size_t>(width) * 4u;
    const std::size_t byteCount = rowByteCount * static_cast<std::size_t>(height);
    std::vector<unsigned char> pixels(byteCount);
    for (int y = 0; y < height; ++y) {
        const auto sourceOffset = static_cast<std::size_t>(y) * rowByteCount;
        const auto targetOffset = static_cast<std::size_t>(height - 1 - y) * rowByteCount;
        std::memcpy(pixels.data() + targetOffset, sourcePixels + sourceOffset, rowByteCount);
    }
    const ImVec2 whiteUv = io.Fonts->TexUvWhitePixel;
    const int whiteX = std::clamp(static_cast<int>(whiteUv.x * static_cast<float>(width)), 0, width - 1);
    const int whiteY = height - 1 - std::clamp(static_cast<int>(whiteUv.y * static_cast<float>(height)), 0, height - 1);
    for (int y = std::max(0, whiteY - 1); y <= std::min(height - 1, whiteY + 1); ++y) {
        for (int x = std::max(0, whiteX - 1); x <= std::min(width - 1, whiteX + 1); ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
            pixels[offset + 0u] = 255u;
            pixels[offset + 1u] = 255u;
            pixels[offset + 2u] = 255u;
            pixels[offset + 3u] = 255u;
        }
    }

    fontTexture_ = filament::Texture::Builder()
            .width(static_cast<std::uint32_t>(width))
            .height(static_cast<std::uint32_t>(height))
            .levels(1)
            .format(filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::UPLOADABLE | filament::Texture::Usage::SAMPLEABLE)
            .build(*engine_);

    fontTexture_->setImage(*engine_, 0, FilamentBufferDescriptorFactory::rgbaPixels(pixels.data(), pixels.size()));
    fontTextureId_ = textures_->registerTexture(fontTexture_);
    io.Fonts->SetTexID(fontTextureId_);
    LOG_INFO("ImGui font atlas uploaded as Filament texture {}x{}", width, height);
}

void ImGuiFilamentRenderer::releaseFrameResources() {
    if (!engine_) {
        return;
    }

    for (DrawResource& resource : frameResources_) {
        if (resource.entity) {
            scene_->remove(resource.entity);
            engine_->getRenderableManager().destroy(resource.entity);
        }
        for (filament::MaterialInstance* materialInstance : resource.materialInstances) {
            if (materialInstance) {
                engine_->destroy(materialInstance);
            }
        }
        resource.materialInstances.clear();
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

} // namespace quader
