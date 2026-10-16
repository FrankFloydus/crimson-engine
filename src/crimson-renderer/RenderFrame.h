#pragma once

#include "Renderer.h"
#include "RenderPassPlan.h"

namespace crimson {

// One presented world generation, independent of any camera. Published through
// shared_ptr<const ...>; producers finish all values before submission. The pins
// own immutable model/material versions referenced by the prepared caster set.
struct RenderWorldSnapshot {
    std::vector<DirectionalLight> directional;
    std::vector<PointLight> points;
    std::vector<RenderParticle> particles;
    std::vector<RenderParticleMaterial> particleMaterials;
    std::vector<RenderModelInstance> shadowInstances;
    std::shared_ptr<const void> pinned;
    std::uint64_t shadowWorldId{};
    std::uint16_t shadowResolution{1024};
    MaterialEvaluationContext materialContext{};
    std::vector<std::uint64_t> shadowFocusEntities;
    Aabb shadowFocusBounds{};
    std::optional<Vec3> shadowPriorityOrigin;
    std::vector<Aabb> shadowReceiverBounds;
    PlayerRevealSource playerReveal{};
    std::array<CharacterLight,2> characterLights{};
    CharacterShadowSource characterShadow{};
};

// Camera-local prepared receivers borrow only from immutable versions retained
// by pinned. The settings and vector storage belong to the submitted frame.
struct PreparedRenderView {
    RenderViewIdentity identity{};
    SceneSettings settings;
    std::vector<RenderModelInstance> instances;
    std::shared_ptr<const void> pinned;
    std::uint32_t width{},height{};
    std::vector<RenderCollisionOverlay> collisionOverlays;
};

struct PreparedRenderFrame {
    std::shared_ptr<const RenderWorldSnapshot> world;
    std::vector<PreparedRenderView> views;
    ViewportAppearance appearance;
    bool gridVisible{true};
    std::uint64_t resourceSignature{};
};

// Validation is CPU-only and occurs before any view is submitted. Independent
// preview/thumbnail entry points use the same pass plan, but do not enter this
// shared active-world Scene/Game frame.
enum class PreparedFrameError { None, MissingWorld, UnsupportedView, DuplicateView, InvalidExtent };
PreparedFrameError validatePreparedRenderFrame(const PreparedRenderFrame&) noexcept;
const char* preparedFrameErrorText(PreparedFrameError) noexcept;

// Borrowed execution adapter; its caller must retain the complete frame through
// submission. Renderer::renderFrame attaches that owner to queued GPU leases.
RenderScene preparedRenderScene(const PreparedRenderFrame&,const PreparedRenderView&);

} // namespace crimson
