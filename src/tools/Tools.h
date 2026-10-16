#pragma once

#include "crimson-renderer/RenderTypes.h"
#include "scene/MaterialSlotAssignment.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace crimson {

class Renderer;
class AssetLibrary;
struct ModelInstance;

enum class EditorCameraAction { None, EditorCamera, SelectCamera, TogglePilot, CreateCamera };

struct EditorFrame {
    ViewportRegion viewport;
    // Pose used by this frame's overlays and pick requests. Navigation may
    // advance the editor camera after current-frame UI ownership is resolved.
    std::optional<Camera> viewportCamera;
    EditorCameraAction cameraAction{EditorCameraAction::None};
    std::uint64_t cameraEntity{};
    bool togglePlay{false};
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
    std::string openMaterial, materialFolder;
};

struct BrowserActions {
    bool newScene{false};
    std::string openScene, deleteScene;
    std::vector<std::string> deleteAssetIds;
    std::vector<std::string> visiblePreviewIds;
    int previewSize{128};
    bool createParticle{false};
    std::string openParticle, particleFolder;
    bool createMaterial{false};
    std::string openMaterial, materialFolder;
};

BrowserActions drawAssetBrowser(AssetLibrary& assets, SDL_Window* window);
bool drawEnvironmentControls(EnvironmentSettings& environment, AssetLibrary& assets, const Renderer& renderer, bool persistLegacy = false);
bool drawEnvironmentImageField(EnvironmentSettings&, AssetLibrary&);
bool drawToneMappingControl(ToneMapping&);
enum class GlowControl { Enabled, Strength, Size, Threshold };
using GlowControlEdit = std::function<void(GlowControl,bool)>;
bool drawGlowControls(GlowSettings&, const GlowControlEdit& edit = {});

// Call once after creating the ImGui context. DPI is supplied by the host;
// this does not change window/framebuffer coordinates or SDL input scaling.
void applyToolsStyle(float dpiScale = 1.0f);
EditorFrame drawEditor(SceneSettings& settings, Renderer& renderer, AssetLibrary& assets, SDL_Window* window,
                       const ModelInstance* model = nullptr, const std::string& previewStatus = {});

} // namespace crimson
