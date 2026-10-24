#pragma once
#include "GameplayInspectorContext.h"
#include "runtime/SceneLights.h"
#include "math/Math.h"
#include <optional>
#include <cstdint>
#include <string>
#include <functional>
#include <array>

namespace crimson {
class SceneDocument;
class AssetLibrary;
namespace shared { class Renderer; } using Renderer=shared::Renderer;
class QuaderBridge;
struct NavigationInspectorContext {
    std::string* previewProfile{};
    std::string* openProfile{};
    std::function<std::string(const std::string&)> buildStatus;
};
struct DecalInspectorContext {
    std::string* openAsset{};
    std::uint64_t* placeEntity{};
    std::function<std::string(std::uint64_t)> status;
    std::function<std::optional<Mat4>(std::uint64_t)> surfaceFrame;
};
struct RagdollInspectorContext {
    std::string* openAsset{};
    std::function<bool(std::uint64_t)> active;
    std::uint64_t* activateEntity{};
    std::uint64_t* resetEntity{};
};

// Explicit menu actions may finish an owned panel edit before querying history.
// Viewport gestures have different ownership and are never committed here.
bool finishScenePanelEdit(SceneDocument& document);
void drawSceneOutliner(SceneDocument& document, AssetLibrary& assets, bool allowEdits = true);
void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits = true,
                        std::uint64_t* playParticle = nullptr,std::string* openParticle = nullptr,QuaderBridge* quader=nullptr,
                        std::string* openMaterial=nullptr,const NavigationInspectorContext& navigation={},
                        const std::array<std::string,32>* collisionLayerNames=nullptr,
                        const GameplayInspectorContext& gameplay={},std::string* openAnimation=nullptr,
                        const DecalInspectorContext& decals={},const RagdollInspectorContext& ragdolls={},
                        const LightPreviewContext& lights={});
} // namespace crimson
