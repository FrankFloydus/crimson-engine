#pragma once
#include "scene/Scene.h"
#include <functional>

namespace crimson {
class SceneDocument;
enum class PrimitiveMeshControl : unsigned { SizeX,SizeY,SizeZ,Radius,Height,Sides,Rings };
struct PrimitiveMeshInspectorTransactions {
    std::function<bool(PrimitiveMeshControl,const char*)> begin;
    std::function<void(PrimitiveMeshControl)> finish;
};
struct PrimitiveMeshInspectorState {
    std::uint32_t activeItem{};
    PrimitiveMeshControl active{PrimitiveMeshControl::SizeX};
    bool editing{};
    EntityId entity{};
    std::uint64_t documentGeneration{};
};
void releasePrimitiveMeshInspector(PrimitiveMeshInspectorState&);
void drawPrimitiveMeshInspector(SceneDocument&,PrimitiveMeshInspectorState&,const EntityRecord&,
    const PrimitiveMeshInspectorTransactions&);
}
