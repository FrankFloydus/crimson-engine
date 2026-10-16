#pragma once
#include "tools/Tools.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "ViewportMaterialDrop.h"
#include "ColliderGizmo.h"
#include <span>
#include <vector>

struct ImDrawList;

namespace crimson {
class SceneViewport {
public:
    EditorFrame draw(Camera&, SceneDocument&, AssetLibrary&, Renderer&, bool navigationActive, uint32_t bgfxFrame, bool keyboardBlocked=false,
                     bool showCollisions=true, bool showTransformGizmo=true, bool allowCameraNavigation=true,
                     bool cameraGestureActive=false, bool preserveCameraLens=false);
    void cancel(SceneDocument&, Renderer&);
    bool interacting() const { return dragging_ || colliderDrag_.active() || materialDrop_.active(); }
    void focus(Camera&, const SceneDocument&, const AssetLibrary&, float aspect, bool preserveLens=false);
    void drawPlayerLightRig(const Camera&,const Scene&);
    void setParticleBoundsQuery(ParticleBoundsQuery query) { particleBounds_=std::move(query); }
    void setCollisionMeshQuery(CollisionMeshQuery query) { collisionMeshes_=std::move(query); }
private:
    struct DragRoot { EntityId id; Mat4 local, world, parentInverse; };
    int operation_{1 | 2 | 4}; // ImGuizmo::TRANSLATE, kept out of public headers.
    bool dragging_{false}, pickCtrl_{false}, pickShift_{false}, waitForRelease_{false};
    uint64_t serial_{}, selectionGeneration_{};
    std::vector<EntityId> previousSelection_;
    Mat4 proxy_{}, startProxy_{}, previousView_{}, pickProjection_{}, pickView_{};
    Vec2 pickSize_{};
    uint64_t intentGeneration_{},intentRevision_{},intentSerial_{};
    uint32_t pickX_{},pickY_{};
    EntityId pickCollisionFallback_{};
    bool pickIntent_{};
    ViewportMaterialDrop materialDrop_;
    ColliderGizmoDrag colliderDrag_;
    bool materialDropCanceled_{};
    std::vector<DragRoot> roots_;
    ParticleBoundsQuery particleBounds_;
    CollisionMeshQuery collisionMeshes_;
    ImDrawList* imageDrawList_{};
    ViewportRegion imageRegion_;
    void select(SceneDocument&, EntityId, bool toggle, bool append);
};
}
