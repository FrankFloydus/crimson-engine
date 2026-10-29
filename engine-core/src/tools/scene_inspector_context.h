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
struct EntityRecord;
struct SceneMeshPaint;
using MeshPaintSubdivisionQuery=std::function<bool(const EntityRecord&,std::uint32_t,SceneMeshPaint&,std::string&)>;
// The resource owner prepares/resamples immutable geometry off the UI path.
void setMeshPaintSubdivisionQuery(SceneDocument&,MeshPaintSubdivisionQuery);
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
// A paint stroke uses the mouse in the viewport. Its transaction must not gray
// panels; SceneEditor ends it before release or a keyboard command reaches them.
inline bool scenePanelEditBlocked(bool allowEdits,bool canceled,bool transactionActive,bool inspectorOwnsEdit,bool paintStrokeActive) {
    return !allowEdits||canceled||(transactionActive&&!inspectorOwnsEdit&&!paintStrokeActive);
}
} // namespace crimson
