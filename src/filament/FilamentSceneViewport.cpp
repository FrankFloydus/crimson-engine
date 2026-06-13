module;

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <tracy/Tracy.hpp>

#include <memory>
#include <optional>
#include <variant>

module quader.filament.scene_viewport;

import quader.filament.buffer_upload;
import quader.filament.debug_draw_renderer;
import quader.filament.environment_resources;
import quader.filament.post_processing_options;
import quader.filament.scene_viewport_camera;
import quader.filament.scene_viewport_materials;
import quader.filament.scene_viewport_render_targets;
import quader.filament.scene_viewport_scene_sync;

namespace quader {

struct FilamentSceneViewport::Resources {
    Resources(filament::Engine& engine_ref, FilamentViewportVulkanPlatform& platform_ref)
        : engine(engine_ref)
        , render_target_resources(create_vulkan_viewport_render_targets(
              engine_ref,
              platform_ref))
        , camera_resources(engine_ref)
        , material_resources(engine_ref)
    {
    }

    Resources(filament::Engine& engine_ref, IOpenGlTexture2DAllocator& texture_allocator)
        : engine(engine_ref)
        , render_target_resources(create_opengl_viewport_render_targets(
              engine_ref,
              texture_allocator))
        , camera_resources(engine_ref)
        , material_resources(engine_ref)
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        engine.flushAndWait();

        environment_resources.reset();
        debug_draw_renderer.reset();
        scene_sync_resources.reset();
        camera_resources.destroy();
        material_resources.destroy();
        render_target_resources.reset();

        if (view != nullptr) {
            engine.destroy(view);
            view = nullptr;
        }

        if (scene != nullptr) {
            engine.destroy(scene);
            scene = nullptr;
        }

        if (renderer != nullptr) {
            engine.destroy(renderer);
            renderer = nullptr;
        }
    }

    void initialize()
    {
        renderer = require_filament_allocation(engine.createRenderer(), "viewport renderer");
        scene = require_filament_allocation(engine.createScene(), "viewport scene");
        view = require_filament_allocation(engine.createView(), "viewport view");
        environment_resources = std::make_unique<FilamentEnvironmentResources>(engine, *scene);
        debug_draw_renderer = std::make_unique<FilamentDebugDrawRenderer>(engine, *scene);
        scene_sync_resources = std::make_unique<FilamentSceneSyncResources>(
            engine,
            *scene,
            *view,
            material_resources);

        view->setScene(scene);
        view->setPostProcessingEnabled(true);
        view->setAntiAliasing(filament::View::AntiAliasing::FXAA);
        view->setShadowingEnabled(true);
        view->setShadowType(filament::View::ShadowType::PCF);

        filament::Renderer::ClearOptions clear_options;
        clear_options.clear = true;
        clear_options.clearColor = { 0.055, 0.060, 0.070, 1.0 };
        renderer->setClearOptions(clear_options);

        camera_resources.create(*view);
    }

    void render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame)
    {
        ZoneScopedN("Filament scene viewport render");

        const SceneRenderSnapshot& snapshot = std::get<SceneViewportSubject>(request.subject).snapshot;
        view->setBloomOptions(filament_bloom_options(snapshot.post_processing.bloom));
        view->setAmbientOcclusionOptions(
            filament_ambient_occlusion_options(snapshot.post_processing.ambient_occlusion));
        if (environment_resources != nullptr) {
            environment_resources->apply(snapshot.environment);
        }
        if (scene_sync_resources != nullptr) {
            scene_sync_resources->apply_snapshot(snapshot);
        }
        if (debug_draw_renderer != nullptr) {
            debug_draw_renderer->render(request.debug_draw);
        }

        const bool resized_target = render_target_resources->ensure_size(
            request.viewport_id,
            request.generation,
            request.extent,
            *view);
        camera_resources.apply(
            request.camera,
            request.projection,
            filament_dynamic_lighting_far_for(snapshot, request.camera, request.projection),
            render_target_resources->current_extent(),
            *view);
        if (resized_target) {
            camera_resources.apply_projection(render_target_resources->current_extent(), *view);
        }
        if (!render_target_resources->has_swapchain()) {
            return;
        }

        frame.extent = request.extent;

        filament::SwapChain* swap_chain = render_target_resources->current_swapchain();
        if (renderer->beginFrame(swap_chain)) {
            renderer->render(view);
            renderer->endFrame();
            render_target_resources->finish_rendered_frame(request.viewport_id, frame);
        }
    }

    [[nodiscard]] std::optional<ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const ViewportGpuImageHandle& handle)
    {
        return render_target_resources != nullptr
            ? render_target_resources->resolve_viewport_gpu_image(handle)
            : std::nullopt;
    }

    [[nodiscard]] bool has_retired_viewport_gpu_resources() const noexcept
    {
        return render_target_resources != nullptr &&
            render_target_resources->has_retired_viewport_gpu_resources();
    }

    void collect_retired_viewport_gpu_resources()
    {
        if (render_target_resources != nullptr) {
            render_target_resources->collect_retired_viewport_gpu_resources();
        }
    }

    filament::Engine& engine;
    std::unique_ptr<FilamentViewportRenderTargetResources> render_target_resources;
    FilamentViewportCameraResources camera_resources;
    FilamentPbrMaterialResources material_resources;
    filament::Renderer* renderer = nullptr;
    filament::Scene* scene = nullptr;
    filament::View* view = nullptr;
    std::unique_ptr<FilamentEnvironmentResources> environment_resources;
    std::unique_ptr<FilamentDebugDrawRenderer> debug_draw_renderer;
    std::unique_ptr<FilamentSceneSyncResources> scene_sync_resources;
};

FilamentSceneViewport::FilamentSceneViewport(
    filament::Engine& engine,
    FilamentViewportVulkanPlatform& platform)
    : resources_(std::make_unique<Resources>(engine, platform))
{
    resources_->initialize();
}

FilamentSceneViewport::FilamentSceneViewport(
    filament::Engine& engine,
    IOpenGlTexture2DAllocator& texture_allocator)
    : resources_(std::make_unique<Resources>(engine, texture_allocator))
{
    resources_->initialize();
}

FilamentSceneViewport::~FilamentSceneViewport() = default;

void FilamentSceneViewport::render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame)
{
    resources_->render_into(request, frame);
}

std::optional<ViewportSampledGpuImage> FilamentSceneViewport::resolve_viewport_gpu_image(
    const ViewportGpuImageHandle& handle)
{
    return resources_ != nullptr ? resources_->resolve_viewport_gpu_image(handle) : std::nullopt;
}

bool FilamentSceneViewport::has_retired_viewport_gpu_resources() const noexcept
{
    return resources_ != nullptr && resources_->has_retired_viewport_gpu_resources();
}

void FilamentSceneViewport::collect_retired_viewport_gpu_resources()
{
    if (resources_ != nullptr) {
        resources_->collect_retired_viewport_gpu_resources();
    }
}

} // namespace quader
