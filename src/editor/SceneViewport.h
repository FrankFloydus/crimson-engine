#pragma once
#include "tools/Tools.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "ViewportMaterialDrop.h"
#include "ViewportDecalDrop.h"
#include "ColliderGizmo.h"
#include "NavigationVolumeGizmo.h"
#include "ViewportTransformGesture.h"
#include "ViewportRotationSnap.h"
#include "runtime/TraceVisualization.h"
#include "tools/VertexPaintTool.h"
#include <span>
#include <optional>
#include <vector>

struct ImDrawList;

namespace crimson {
class SceneViewport {
public:
    EditorFrame draw(Camera&, SceneDocument&, AssetLibrary&, Renderer&, bool navigationActive, uint32_t bgfxFrame, bool keyboardBlocked=false,
                     bool showCollisions=true, bool showTransformGizmo=true, bool allowCameraNavigation=true,
                     bool cameraGestureActive=false, bool preserveCameraLens=false);
    void cancel(SceneDocument&, Renderer&);
    bool interacting() const { return transformDrag_.active() || colliderDrag_.active() || volumeDrag_.active() || materialDrop_.active() || decalDrop_.active() || vertexPaint_.active(); }
    void setVertexPaintTargetQuery(VertexPaintTargetQuery query){vertexPaint_.setTargetQuery(std::move(query));}
    void setVertexPaintSurfaceQuery(VertexPaintSurfaceQuery query){vertexPaint_.setSurfaceQuery(std::move(query));}
    std::span<const std::string> vertexPaintPreviewMaterials() const{return vertexPaint_.previewMaterials();}
    bool vertexPaintEnabled() const {return vertexPaint_.enabled();}
    bool vertexPaintActive() const {return vertexPaint_.active();}
    bool vertexPaintSurfaceHeld() const {return vertexPaint_.surfaceHeld();}
    void setVertexPaintEnabled(bool enabled,SceneDocument& document,Renderer& renderer){cancel(document,renderer);vertexPaint_.setEnabled(enabled,document,renderer);}
    void drawVertexPaintControls(SceneDocument& document,AssetLibrary& assets,Renderer& renderer){vertexPaint_.drawControls(document,assets,renderer);}
    void beginDecalPlacement(EntityId entity) { decalDrop_.beginReplace(entity);decalPlacementJustArmed_=true; }
    void focus(Camera&, const SceneDocument&, const AssetLibrary&, float aspect, bool preserveLens=false);
    void drawDecalPlacementPlane(const Mat4&,const Camera&,bgfx::TextureHandle,const Vec4&);
    void drawTraces(const Camera&,std::span<const TraceVisualRecord>);
    void setParticleBoundsQuery(ParticleBoundsQuery query) { particleBounds_=std::move(query); }
    void setCollisionMeshQuery(CollisionMeshQuery query) { collisionMeshes_=std::move(query); }
    void setAgentProfileQuery(NavigationAgentProfileQuery query) { agentProfiles_=std::move(query); }
    void setDecalPatchQuery(std::function<std::shared_ptr<const DecalPatchData>(EntityId)> query) { decalPatches_=std::move(query); }
    void setDecalFrameQuery(std::function<std::optional<Mat4>(EntityId)> query) { decalFrames_=std::move(query); }
private:
    int operation_{1 | 2 | 4}; // ImGuizmo::TRANSLATE, kept out of public headers.
    float rotationSnapDegrees_{DefaultRotationSnapDegrees};
    bool pickCtrl_{false}, pickShift_{false}, waitForRelease_{false};
    uint64_t serial_{}, selectionGeneration_{};
    std::vector<EntityId> previousSelection_;
    Mat4 proxy_{}, startProxy_{}, previousView_{}, pickProjection_{}, pickView_{};
    Vec2 pickSize_{};
    uint64_t intentGeneration_{},intentRevision_{},intentSerial_{};
    uint32_t pickX_{},pickY_{};
    EntityId pickCollisionFallback_{};
    bool pickIntent_{};
    ViewportMaterialDrop materialDrop_;
    ViewportDecalDrop decalDrop_;
    ColliderGizmoDrag colliderDrag_;
    NavigationVolumeGizmoDrag volumeDrag_;
    ViewportTransformGesture transformDrag_;
    VertexPaintTool vertexPaint_;
    bool materialDropCanceled_{};
    bool decalDropCanceled_{};
    bool decalPlacementJustArmed_{};
    ParticleBoundsQuery particleBounds_;
    CollisionMeshQuery collisionMeshes_;
    NavigationAgentProfileQuery agentProfiles_;
    std::function<std::shared_ptr<const DecalPatchData>(EntityId)> decalPatches_;
    std::function<std::optional<Mat4>(EntityId)> decalFrames_;
    std::optional<Mat4> decalEditFrame_;
    ImDrawList* imageDrawList_{};
    ViewportRegion imageRegion_;
    void select(SceneDocument&, EntityId, bool toggle, bool append);
};
}
