#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include "scene/MaterialSlotAssignment.h"
#include "editor/PlayAction.h"
#include "runtime/PlayStartPoint.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace crimson {
namespace shared { class Renderer; } using Renderer=shared::Renderer;
class AssetLibrary;
struct DecalAsset;
struct DecalSurfacePlacement {
    AssetRef<DecalAsset> asset;
    std::uint64_t replaceEntity{};
    PickResult hit;
};

enum class EditorCameraAction { None, EditorCamera, SelectCamera, TogglePilot, CreateCamera, RecallBookmark, FocusSelection };

struct EditorFrame {
    ViewportRegion viewport;
    // Pose used by this frame's overlays and pick requests. Navigation may
    // advance the editor camera after current-frame UI ownership is resolved.
    std::optional<Camera> viewportCamera;
    EditorCameraAction cameraAction{EditorCameraAction::None};
    std::uint64_t cameraEntity{};
    std::optional<Camera> bookmarkCamera;
    PlayAction playAction{PlayAction::None};
    std::optional<PlayStartPoint> playStartPoint;
    bool materialChanged{false};
    bool environmentChanged{false};
    std::string modelDropId;
    std::vector<std::string> modelDropIds;
    bool showSphere{false};
    bool frameModel{false};
    Vec2 modelDropPosition{};
    // Transient placement and visible thumbnail demands do not mutate a scene.
    std::vector<std::string> placementPreviewIds;
    Vec2 placementPreviewPosition{};
    AssetRef<MaterialAsset> materialDragId;
    Vec2 materialDragPosition{};
    bool materialDropDelivered{};
    std::optional<MaterialSlotAssignment> materialPreview,materialAssignment;
    std::vector<std::string> visiblePreviewIds;
    bool documentReplaced{false};
    std::string particleDropId, openParticle, particleFolder;
    Vec2 particleDropPosition{};
    bool createParticle{false};
    std::uint64_t playParticleEntity{};
    bool createMaterial{false};
    bool createBlendMaterial{false};
    std::string openMaterial, materialFolder;
    bool createNavigationAgentProfile{false};
    std::string openNavigationAgentProfile,navigationAgentProfileFolder;
    bool createGameplayEffect{false};
    std::string openGameplayEffect,gameplayEffectFolder;
    std::string openAnimation;
    std::string openRagdoll,createRagdollModel,ragdollFolder;
    std::uint64_t activateRagdollEntity{},resetRagdollEntity{};
    bool createDecal{};
    std::string openDecal,decalFolder;
    AssetRef<DecalAsset> decalDragId;
    Vec2 decalDragPosition{};
    bool decalDropDelivered{},surfacePickActive{},decalPlacementRejected{};
    std::string decalPlacementDiagnostic;
    std::optional<DecalSurfacePlacement> decalPlacement,decalPreview;
};

struct BrowserActions {
    std::string findReferences;
    std::string duplicateAsset,duplicateFolder;
    bool newScene{false};
    std::string openScene, deleteScene;
    std::vector<std::string> deleteAssetIds;
    std::vector<std::string> visiblePreviewIds;
    int previewSize{128};
    bool createParticle{false};
    std::string openParticle, particleFolder;
    bool createMaterial{false};
    bool createBlendMaterial{false};
    std::string openMaterial, materialFolder;
    bool createNavigationAgentProfile{false};
    std::string openNavigationAgentProfile,navigationAgentProfileFolder;
    bool createGameplayEffect{false};
    std::string openGameplayEffect,gameplayEffectFolder;
    std::string openAnimation;
    std::string openRagdoll,createRagdollModel,ragdollFolder;
    bool createDecal{};
    std::string openDecal,decalFolder;
};

} // namespace crimson
