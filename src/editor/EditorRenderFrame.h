#pragma once

#include "EditorResources.h"
#include "crimson-renderer/RenderFrame.h"
#include "scene/SceneExtraction.h"
#include "scene/MaterialSlotAssignment.h"

#include <memory>
#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace crimson {
class AssetLibrary;
struct PreparedParticleMaterial;

// Exact immutable material and caster versions shared by every camera in one
// presented world. Model receiver versions belong to their individual view.
struct EditorRenderWorldPins {
    std::vector<std::shared_ptr<const PreparedParticleMaterial>> particles;
    std::shared_ptr<const PreparedEditorShadows> shadows;
};

// Freeze copied world values without retaining a document or simulation. Moves
// transfer packet storage; geometry, material packets and GPU leases stay shared.
inline std::shared_ptr<const RenderWorldSnapshot> freezeEditorRenderWorld(
    ExtractedScene& extracted,std::vector<RenderParticle> particles,
    std::vector<RenderParticleMaterial> materials,std::shared_ptr<const EditorRenderWorldPins> pins,
    std::uint64_t worldId,std::uint16_t shadowResolution,MaterialEvaluationContext context,
    std::optional<Vec3> priorityOrigin={}) {
    auto world=std::make_shared<RenderWorldSnapshot>();
    world->directional=std::move(extracted.directional);world->points=std::move(extracted.points);
    world->particles=std::move(particles);world->particleMaterials=std::move(materials);
    if(pins && pins->shadows) {
        world->shadowInstances=pins->shadows->instances;
    }
    world->pinned=std::move(pins);world->shadowWorldId=worldId;
    world->shadowResolution=shadowResolution;world->materialContext=context;
    world->shadowPriorityOrigin=priorityOrigin;
    return world;
}

// Captured after simulation presentation and before either camera prepares its
// receivers. Cameras and dimensions refer to the images used by input/overlays.
struct EditorRenderFrameInput {
    ExtractedScene extracted;
    SceneSettings settings;
    Camera sceneCamera;
    std::optional<Camera> gameCamera;
    std::uint32_t sceneWidth{},sceneHeight{},gameWidth{},gameHeight{};
    float sceneAspect{1.f};
    bool sceneVisible{},gameVisible{};
    std::span<const EntityId> selection;
    std::span<const std::string> placementIds;
    Vec3 placementPosition;
    std::vector<RenderParticle> particles;
    std::vector<RenderParticleMaterial> particleMaterials;
    std::vector<std::shared_ptr<const PreparedParticleMaterial>> particlePins;
    std::uint64_t worldId{};
    std::uint64_t giWorldId{},gameCameraId{},environmentId{};
    std::uint16_t shadowResolution{1024};
    MaterialEvaluationContext materialContext;
    ViewportAppearance appearance;
    bool gridVisible{};
    std::optional<MaterialSlotAssignment> materialPreview;
    std::vector<RenderCollisionOverlay> sceneCollisionOverlays,gameCollisionOverlays;
    Vec3 shadowPriorityOrigin() const {
        return gameVisible && gameWidth && gameHeight && gameCamera ? gameCamera->eye() : sceneCamera.eye();
    }
};

// A hover may replace the authored packet only after every pass and source is
// coherent. Coarse resident textures are sufficient; a diagnostic fallback is not.
inline bool materialSlotPreviewReady(const std::shared_ptr<const PreparedEditorModel>& candidate) {
    return candidate && candidate->ready && candidate->materials.size()==candidate->textures.size() &&
        std::all_of(candidate->textures.begin(),candidate->textures.end(),[](const auto& binding) {
            return binding.drawable() && binding.sourceReady && !binding.graphPending;
        });
}

struct EditorRenderFramePreparationStats {
    double shadowPreparationMs{},receiverPreparationMs{};
    std::size_t preparedCasters{};
    // Ready permits coherent coarse detail; refined also requires the requested
    // detail and complete texture bindings. Missing/pending bindings clear both.
    bool ready{true},refined{true};
};

// The editor owns asset policy; the renderer receives only frozen prepared
// packets. No document, asset lookup or resource publication occurs in execute.
std::shared_ptr<const PreparedRenderFrame> prepareEditorRenderFrame(
    EditorRenderFrameInput,AssetLibrary&,EditorResources&,EditorRenderFramePreparationStats* = nullptr);
} // namespace crimson
