#pragma once
#include "scene/Scene.h"
#include "assets/BrowserCatalog.h"
#include <array>
#include <functional>
#include <span>

namespace crimson {
class SceneDocument;
enum class ColliderControl : unsigned { Shape,SizeX,SizeY,SizeZ,Radius,Height,Source,Visible,Color };
struct ColliderInspectorTransactions {
    std::function<bool(ColliderControl,const char*)> begin;
    std::function<void(ColliderControl)> finish;
};
struct ColliderInspectorModels {
    std::span<const AssetEntry> entries;
    std::function<std::string(const std::string&)> name;
};
struct ColliderInspectorState {
    std::uint32_t activeItem{};
    ColliderControl active{ColliderControl::Shape};
    bool editing{};
    EntityId entity{};
    std::uint64_t documentGeneration{};
    std::array<char,256> search{};
};
// A model on this entity or its nearest visual ancestor supplies a new Trimesh.
// Authored source/dimension fallbacks survive switches to other primitive types.
SceneCollider colliderForShape(const Scene&,EntityId,const SceneCollider&,ColliderShape);
// Release an owned input buffer before an external commit, cancel or Undo.
void releaseColliderInspector(ColliderInspectorState&);
void drawColliderInspector(SceneDocument&,ColliderInspectorState&,const EntityRecord&,
    const ColliderInspectorModels&,const ColliderInspectorTransactions&);
}
