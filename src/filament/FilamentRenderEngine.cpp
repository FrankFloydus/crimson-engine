module;

#include <backend/platforms/VulkanPlatform.h>
#include <backend/DriverEnums.h>
#include <filament/Engine.h>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

module quader.filament.render_engine;

import quader.filament.scene_viewport;
import quader.filament.viewport_vulkan_platform;

namespace quader {
namespace {

std::string backend_to_string(filament::backend::Backend backend)
{
    switch (backend) {
    case filament::backend::Backend::DEFAULT:
        return "Default";
    case filament::backend::Backend::OPENGL:
        return "OpenGL";
    case filament::backend::Backend::VULKAN:
        return "Vulkan";
    case filament::backend::Backend::METAL:
        return "Metal";
    case filament::backend::Backend::WEBGPU:
        return "WebGPU";
    case filament::backend::Backend::NOOP:
        return "Noop";
    }

    return "Unknown";
}

bool frame_has_pending_or_ready_output(const ViewportRenderFrame& frame)
{
    return frame.has_render_output() ||
        frame.payload.has_pending_gpu_image() ||
        (frame.payload.gpu_image.has_value() && frame.payload.gpu_image->is_valid());
}

filament::backend::VulkanPlatform::VulkanSharedContext filament_shared_context_from(
    const VulkanSharedDeviceContext& context)
{
    filament::backend::VulkanPlatform::VulkanSharedContext filament_context;
    filament_context.instance = context.instance;
    filament_context.physicalDevice = context.physical_device;
    filament_context.logicalDevice = context.device;
    filament_context.graphicsQueueFamilyIndex = context.renderer_graphics_queue.family_index;
    filament_context.graphicsQueueIndex = context.renderer_graphics_queue.queue_index;
    filament_context.debugUtilsSupported = context.debug_utils_supported;
    filament_context.debugMarkersSupported = context.debug_markers_supported;
    filament_context.multiviewSupported = context.multiview_supported;
    return filament_context;
}

} // namespace

struct FilamentRenderEngine::Resources {
    explicit Resources(VulkanSharedDeviceContext shared_device_context)
        : shared_device_context(std::move(shared_device_context))
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        viewports.clear();
        if (engine != nullptr) {
            filament::Engine::destroy(&engine);
        }
        platform.reset();
    }

    filament::Engine* engine = nullptr;
    VulkanSharedDeviceContext shared_device_context;
    filament::backend::VulkanPlatform::VulkanSharedContext filament_shared_context;
    std::unique_ptr<FilamentViewportVulkanPlatform> platform;
    struct ViewportSlot {
        std::unique_ptr<FilamentSceneViewport> viewport;
        ViewportRenderFrame frame;
        bool frame_available = false;
        uint64_t next_frame_id = 1;
    };

    std::unordered_map<ViewportId, ViewportSlot, ViewportIdHash> viewports;
    uint64_t next_viewport_id = 1;

    [[nodiscard]] std::optional<ViewportId> create_viewport(filament::Engine& engine_ref)
    {
        if (next_viewport_id == 0) {
            return std::nullopt;
        }

        const ViewportId viewport_id { next_viewport_id };
        auto viewport = std::make_unique<FilamentSceneViewport>(engine_ref, *platform);

        auto [entry, inserted] = viewports.try_emplace(viewport_id);
        if (!inserted) {
            return std::nullopt;
        }

        entry->second.viewport = std::move(viewport);
        ++next_viewport_id;
        return viewport_id;
    }

    [[nodiscard]] ViewportSlot* find_slot(ViewportId viewport_id)
    {
        auto entry = viewports.find(viewport_id);
        if (entry == viewports.end() || entry->second.viewport == nullptr) {
            return nullptr;
        }

        return &entry->second;
    }
};

FilamentRenderEngine::FilamentRenderEngine(VulkanSharedDeviceContext shared_device_context)
    : resources_(std::make_unique<Resources>(std::move(shared_device_context)))
{
    try {
        if (!resources_->shared_device_context.is_valid()) {
            status_.message = "Filament viewport renderer received an invalid app Vulkan shared device context.";
            return;
        }

        resources_->filament_shared_context = filament_shared_context_from(resources_->shared_device_context);
        resources_->platform = std::make_unique<FilamentViewportVulkanPlatform>(
            resources_->shared_device_context);
        resources_->engine = filament::Engine::Builder()
            .backend(filament::backend::Backend::VULKAN)
            .platform(resources_->platform.get())
            .sharedContext(&resources_->filament_shared_context)
            .build();
        if (resources_->engine == nullptr) {
            status_.message = "Filament Engine::create returned null.";
            return;
        }

        status_.initialized = true;
        status_.backend_name = backend_to_string(resources_->engine->getBackend());
        status_.message = "Filament viewport renderer is initialized.";
    } catch (const std::exception& error) {
        status_.message = error.what();
    } catch (...) {
        status_.message = "Filament threw an unknown exception during scene viewport initialization.";
    }
}

FilamentRenderEngine::~FilamentRenderEngine() = default;

RendererStatus FilamentRenderEngine::status() const
{
    return status_;
}

std::optional<ViewportId> FilamentRenderEngine::try_create_viewport()
{
    if (!status_.initialized || resources_ == nullptr || resources_->engine == nullptr) {
        return std::nullopt;
    }

    try {
        std::optional<ViewportId> viewport_id = resources_->create_viewport(*resources_->engine);
        if (!viewport_id.has_value()) {
            status_.message = "Filament viewport renderer could not allocate a unique viewport id.";
        }
        return viewport_id;
    } catch (const std::exception& error) {
        status_.message = "Filament failed to create viewport: " + std::string(error.what());
    } catch (...) {
        status_.message = "Filament failed to create viewport due to an unknown exception.";
    }

    return std::nullopt;
}

void FilamentRenderEngine::release_viewport(ViewportId viewport_id)
{
    if (resources_ == nullptr || !viewport_id.is_valid()) {
        return;
    }

    resources_->viewports.erase(viewport_id);
    if (resources_->platform != nullptr) {
        resources_->platform->release_viewport(viewport_id);
    }
}

bool FilamentRenderEngine::request_viewport_frame(const ViewportRenderRequest& request)
{
    if (!status_.initialized || resources_ == nullptr || resources_->engine == nullptr || !request.is_valid()) {
        return false;
    }

    Resources::ViewportSlot* slot = resources_->find_slot(request.viewport_id);
    if (slot == nullptr) {
        return false;
    }

    slot->frame = {};
    slot->frame.viewport_id = request.viewport_id;
    slot->frame.generation = request.generation;
    slot->frame.request_serial = request.request_serial;
    slot->frame.frame_id = ViewportFrameId { slot->next_frame_id++ };
    slot->frame.extent = request.extent;

    slot->viewport->render_into(request, slot->frame);
    slot->frame_available = frame_has_pending_or_ready_output(slot->frame);
    return slot->frame_available;
}

std::optional<ViewportRenderFrame> FilamentRenderEngine::try_consume_viewport_frame(ViewportId viewport_id)
{
    if (resources_ == nullptr || !viewport_id.is_valid()) {
        return std::nullopt;
    }

    auto entry = resources_->viewports.find(viewport_id);
    if (entry == resources_->viewports.end() || !entry->second.frame_available) {
        return std::nullopt;
    }

    if (resources_->platform == nullptr) {
        return std::nullopt;
    }

    bool received_submitted_image = false;
    while (std::optional<ViewportGpuImageHandle> submitted_image =
               resources_->platform->take_latest_viewport_gpu_image(viewport_id)) {
        entry->second.frame.payload.gpu_image = *submitted_image;
        received_submitted_image = true;
    }
    if (received_submitted_image) {
        entry->second.frame.payload.gpu_image_pending = false;
    }

    if (!entry->second.frame.payload.gpu_image.has_value()) {
        if (entry->second.frame.payload.gpu_image_pending) {
            return std::nullopt;
        }

        entry->second.frame_available = false;
        return std::nullopt;
    }

    if (entry->second.frame.payload.gpu_image.has_value() &&
        !entry->second.frame.payload.gpu_image->is_ready()) {
        std::optional<ViewportSampledGpuImage> image =
            resources_->platform->resolve_viewport_gpu_image(
                *entry->second.frame.payload.gpu_image,
                ViewportGpuResolveWait::poll);
        if (!image.has_value()) {
            return std::nullopt;
        }

        entry->second.frame.payload.gpu_image = image->handle;
    }

    entry->second.frame_available = false;
    return std::move(entry->second.frame);
}

std::optional<ViewportSampledGpuImage> FilamentRenderEngine::resolve_viewport_gpu_image(
    const ViewportGpuImageHandle& handle)
{
    if (!handle.is_ready() || resources_ == nullptr || resources_->platform == nullptr) {
        return std::nullopt;
    }

    return resources_->platform->resolve_viewport_gpu_image(
        handle,
        ViewportGpuResolveWait::poll);
}

bool FilamentRenderEngine::has_retired_viewport_gpu_resources() const noexcept
{
    return resources_ != nullptr &&
        resources_->platform != nullptr &&
        resources_->platform->has_retired_viewport_gpu_resources();
}

void FilamentRenderEngine::collect_retired_viewport_gpu_resources()
{
    if (resources_ != nullptr && resources_->platform != nullptr) {
        resources_->platform->collect_retired_viewport_gpu_resources();
    }
}

} // namespace quader
