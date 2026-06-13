module;

#include <filament/Engine.h>
#include <filament/RenderTarget.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <memory>
#include <optional>
#include <cstdint>
#include <stdexcept>

module quader.filament.scene_viewport_render_targets;

import quader.filament.buffer_upload;
import quader.render.opengl_texture;
import quader.render.viewport_types;

namespace quader {
namespace {

constexpr ViewportTexturePayloadKind kOpenGlViewportTexturePayloadKind =
    viewport_texture_payload_kind("quader.viewport_texture.opengl.texture2d.v1");

[[nodiscard]] ViewportTextureToken make_opengl_viewport_texture_token(
    ViewportGpuImageHandle handle,
    OpenGlTexture2D texture) noexcept
{
    return ViewportTextureToken {
        .handle = handle,
        .payload_kind = kOpenGlViewportTexturePayloadKind,
        .payload = {
            static_cast<uint64_t>(texture.id),
            0,
            0,
        },
        .uv_origin_bottom_left = true,
    };
}

class VulkanViewportRenderTargetResources final : public FilamentViewportRenderTargetResources {
public:
    VulkanViewportRenderTargetResources(
        filament::Engine& engine_ref,
        FilamentViewportVulkanPlatform& platform_ref)
        : engine(engine_ref)
        , platform(platform_ref)
    {
    }

    VulkanViewportRenderTargetResources(const VulkanViewportRenderTargetResources&) = delete;
    VulkanViewportRenderTargetResources& operator=(const VulkanViewportRenderTargetResources&) = delete;

    ~VulkanViewportRenderTargetResources() override
    {
        destroy();
    }

    [[nodiscard]] bool ensure_size(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent requested_extent,
        filament::View& view) override
    {
        if (!viewport_id.is_valid() || !generation.is_valid() || !requested_extent.is_valid()) {
            return false;
        }

        if (swap_chain != nullptr &&
            extent.width == requested_extent.width &&
            extent.height == requested_extent.height) {
            view.setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
            view.setRenderTarget(nullptr);
            return false;
        }

        engine.flushAndWait();
        destroy();

        swap_chain = platform.create_viewport_swapchain(
            engine,
            viewport_id,
            generation,
            requested_extent);
        swap_chain = require_filament_allocation(swap_chain, "viewport swapchain");

        extent = requested_extent;
        attached_view = &view;
        attached_view->setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
        attached_view->setRenderTarget(nullptr);
        return true;
    }

    [[nodiscard]] bool has_swapchain() const override
    {
        return swap_chain != nullptr;
    }

    [[nodiscard]] RenderExtent current_extent() const override
    {
        return extent;
    }

    [[nodiscard]] filament::SwapChain* current_swapchain() const override
    {
        return swap_chain;
    }

    void finish_rendered_frame(ViewportId viewport_id, ViewportRenderFrame& frame) override
    {
        frame.payload.gpu_image = platform.take_latest_viewport_gpu_image(viewport_id);
        frame.payload.gpu_image_pending = !frame.payload.gpu_image.has_value();
    }

    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const ViewportGpuImageHandle& handle) override
    {
        return platform.resolve_viewport_gpu_image(handle, ViewportGpuResolveWait::poll);
    }

    [[nodiscard]] bool has_retired_viewport_gpu_resources() const noexcept override
    {
        return platform.has_retired_viewport_gpu_resources();
    }

    void collect_retired_viewport_gpu_resources() override
    {
        platform.collect_retired_viewport_gpu_resources();
    }

private:
    void destroy()
    {
        if (attached_view != nullptr) {
            attached_view->setRenderTarget(nullptr);
            attached_view = nullptr;
        }

        if (swap_chain != nullptr) {
            engine.destroy(swap_chain);
            swap_chain = nullptr;
        }

        extent = {};
    }

    filament::Engine& engine;
    FilamentViewportVulkanPlatform& platform;
    filament::SwapChain* swap_chain = nullptr;
    filament::View* attached_view = nullptr;
    RenderExtent extent;
};

class OpenGlViewportRenderTargetResources final : public FilamentViewportRenderTargetResources {
public:
    OpenGlViewportRenderTargetResources(
        filament::Engine& engine_ref,
        IOpenGlTexture2DAllocator& texture_allocator_ref)
        : engine(engine_ref)
        , texture_allocator(texture_allocator_ref)
    {
    }

    OpenGlViewportRenderTargetResources(const OpenGlViewportRenderTargetResources&) = delete;
    OpenGlViewportRenderTargetResources& operator=(const OpenGlViewportRenderTargetResources&) = delete;

    ~OpenGlViewportRenderTargetResources() override
    {
        destroy();
    }

    [[nodiscard]] bool ensure_size(
        ViewportId viewport_id,
        ViewportGeneration generation,
        RenderExtent requested_extent,
        filament::View& view) override
    {
        if (!viewport_id.is_valid() || !generation.is_valid() || !requested_extent.is_valid()) {
            return false;
        }

        if (swap_chain != nullptr &&
            viewport_generation == generation &&
            extent.width == requested_extent.width &&
            extent.height == requested_extent.height) {
            view.setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
            view.setRenderTarget(render_target);
            return false;
        }

        engine.flushAndWait();
        destroy();

        viewport_generation = generation;
        extent = requested_extent;
        color_texture_handle = texture_allocator.create_color_texture(extent);
        if (!color_texture_handle.is_valid()) {
            throw std::runtime_error("OpenGL viewport texture allocator returned an invalid color texture.");
        }

        const filament::Texture::Usage color_usage =
            filament::Texture::Usage::COLOR_ATTACHMENT |
            filament::Texture::Usage::SAMPLEABLE |
            filament::Texture::Usage::BLIT_SRC;
        color_texture = filament::Texture::Builder()
            .width(extent.width)
            .height(extent.height)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(filament::Texture::InternalFormat::RGBA8)
            .usage(color_usage)
            .import(static_cast<intptr_t>(color_texture_handle.id))
            .build(engine);
        color_texture = require_filament_allocation(color_texture, "OpenGL imported viewport color texture");

        depth_texture = filament::Texture::Builder()
            .width(extent.width)
            .height(extent.height)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(filament::Texture::InternalFormat::DEPTH24)
            .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
            .build(engine);
        depth_texture = require_filament_allocation(depth_texture, "OpenGL viewport depth texture");

        render_target = filament::RenderTarget::Builder()
            .texture(filament::RenderTarget::AttachmentPoint::COLOR, color_texture)
            .texture(filament::RenderTarget::AttachmentPoint::DEPTH, depth_texture)
            .build(engine);
        render_target = require_filament_allocation(render_target, "OpenGL viewport render target");

        swap_chain = engine.createSwapChain(extent.width, extent.height);
        swap_chain = require_filament_allocation(swap_chain, "OpenGL viewport headless swapchain");

        attached_view = &view;
        attached_view->setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
        attached_view->setRenderTarget(render_target);
        return true;
    }

    [[nodiscard]] bool has_swapchain() const override
    {
        return swap_chain != nullptr && render_target != nullptr && color_texture_handle.is_valid();
    }

    [[nodiscard]] RenderExtent current_extent() const override
    {
        return extent;
    }

    [[nodiscard]] filament::SwapChain* current_swapchain() const override
    {
        return swap_chain;
    }

    void finish_rendered_frame(ViewportId viewport_id, ViewportRenderFrame& frame) override
    {
        if (!viewport_id.is_valid() || !viewport_generation.is_valid() || !color_texture_handle.is_valid()) {
            frame.payload.gpu_image_pending = false;
            return;
        }

        frame.payload.gpu_image = ViewportGpuImageHandle {
            .viewport_id = viewport_id,
            .generation = viewport_generation,
            .slot_index = 0,
            .image_generation = color_texture_handle.generation,
            .resource_id = color_texture_handle.resource_id,
            .extent = color_texture_handle.extent,
            .readiness = ViewportGpuImageReadiness::ready,
        };
        frame.payload.gpu_image_pending = false;
    }

    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const ViewportGpuImageHandle& handle) override
    {
        if (!handle.is_ready() ||
            !color_texture_handle.is_valid() ||
            handle.generation != viewport_generation ||
            handle.resource_id != color_texture_handle.resource_id ||
            handle.image_generation != color_texture_handle.generation ||
            handle.extent.width != color_texture_handle.extent.width ||
            handle.extent.height != color_texture_handle.extent.height) {
            return std::nullopt;
        }

        return ViewportSampledGpuImage {
            .texture = make_opengl_viewport_texture_token(handle, color_texture_handle),
        };
    }

    [[nodiscard]] bool has_retired_viewport_gpu_resources() const noexcept override
    {
        return false;
    }

    void collect_retired_viewport_gpu_resources() override
    {
    }

private:
    void destroy()
    {
        if (attached_view != nullptr) {
            attached_view->setRenderTarget(nullptr);
            attached_view = nullptr;
        }

        if (render_target != nullptr) {
            engine.destroy(render_target);
            render_target = nullptr;
        }

        if (color_texture != nullptr) {
            engine.destroy(color_texture);
            color_texture = nullptr;
        }

        if (depth_texture != nullptr) {
            engine.destroy(depth_texture);
            depth_texture = nullptr;
        }

        if (swap_chain != nullptr) {
            engine.destroy(swap_chain);
            swap_chain = nullptr;
        }

        if (color_texture_handle.is_valid()) {
            texture_allocator.destroy_texture(color_texture_handle);
            color_texture_handle = {};
        }

        viewport_generation = {};
        extent = {};
    }

    filament::Engine& engine;
    IOpenGlTexture2DAllocator& texture_allocator;
    filament::SwapChain* swap_chain = nullptr;
    filament::RenderTarget* render_target = nullptr;
    filament::Texture* color_texture = nullptr;
    filament::Texture* depth_texture = nullptr;
    filament::View* attached_view = nullptr;
    ViewportGeneration viewport_generation;
    RenderExtent extent;
    OpenGlTexture2D color_texture_handle;
};

} // namespace

std::unique_ptr<FilamentViewportRenderTargetResources> create_vulkan_viewport_render_targets(
    filament::Engine& engine,
    FilamentViewportVulkanPlatform& platform)
{
    return std::make_unique<VulkanViewportRenderTargetResources>(engine, platform);
}

std::unique_ptr<FilamentViewportRenderTargetResources> create_opengl_viewport_render_targets(
    filament::Engine& engine,
    IOpenGlTexture2DAllocator& texture_allocator)
{
    return std::make_unique<OpenGlViewportRenderTargetResources>(engine, texture_allocator);
}

} // namespace quader
