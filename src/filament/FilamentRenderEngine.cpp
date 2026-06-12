module;

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

} // namespace

struct FilamentRenderEngine::Resources {
    Resources() = default;

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        viewports.clear();
        if (engine != nullptr) {
            filament::Engine::destroy(&engine);
        }
    }

    filament::Engine* engine = nullptr;
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
        auto viewport = std::make_unique<FilamentSceneViewport>(engine_ref);

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

FilamentRenderEngine::FilamentRenderEngine()
    : resources_(std::make_unique<Resources>())
{
    try {
        resources_->engine = filament::Engine::create(filament::backend::Backend::VULKAN);
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
    slot->frame_available = slot->frame.has_pixels();
    if (!slot->frame_available && slot->frame.payload.has_unsupported_texture_payload()) {
        status_.message = "Viewport frame produced a render texture handle, but only temporary RGBA8 readback is currently presentable.";
    }
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

    entry->second.frame_available = false;
    return std::move(entry->second.frame);
}

} // namespace quader
