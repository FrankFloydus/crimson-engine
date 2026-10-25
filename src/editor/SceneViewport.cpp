#include "SceneViewport.h"
#include "DecalAuthoring.h"
#include "tools/RenderDebugControls.h"
#include "ViewportAssetDrop.h"
#include "ViewportSelection.h"
#include "ViewportSelectionUi.h"
#include "tools/ScenePanels.h"
#include "CollisionWire.h"
#include "TraceOverlay.h"
#include "ColliderGizmoUi.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "scene/SceneExtraction.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace crimson {
namespace {
constexpr ImU32 WireColor=IM_COL32(255,166,26,255);
Aabb boundsOf(const SceneDocument& document, const AssetLibrary& assets,const ParticleBoundsQuery& particles = {},const CollisionMeshQuery& meshes = {},const NavigationAgentProfileQuery& profiles = {},const DecalBoundsQuery& decals={}) {
    return selectionBounds(document.scene(), document.selection().ids, [&](const std::string& id)->std::optional<Aabb> {
        const auto model = assets.findModel(id);
        return model ? std::optional<Aabb>{model->bounds} : std::nullopt;
    },particles,meshes,profiles,decals);
}
bool selected(const SceneDocument& document, EntityId id) {
    const auto& ids = document.selection().ids;
    return std::find(ids.begin(),ids.end(),id) != ids.end();
}
float segmentDistance(ImVec2 point, ImVec2 start, ImVec2 end) {
    const float x=end.x-start.x, y=end.y-start.y;
    const float lengthSquared=x*x+y*y;
    const float t=lengthSquared>0 ? std::clamp(((point.x-start.x)*x+(point.y-start.y)*y)/lengthSquared,0.0f,1.0f) : 0;
    return std::hypot(point.x-start.x-t*x,point.y-start.y-t*y);
}
float gameplayMarker(ImDrawList& draw,const EntityRecord& record,const Mat4& world,const Mat4& vp,
                     const Vec4& viewport,ImU32 color,ImVec2 mouse,const NavigationAgentProfileQuery& profiles = {}) {
    float distance=1e9f;
    const auto line=[&](Vec3 a,Vec3 b) {
        const auto projected=projectClippedSegment(math::transformPoint(world,a),math::transformPoint(world,b),vp,viewport);
        if(!projected)return;
        const ImVec2 start((*projected)[0][0],(*projected)[0][1]),end((*projected)[1][0],(*projected)[1][1]);
        draw.AddLine(start,end,color,1.5f);distance=std::min(distance,segmentDistance(mouse,start,end));
    };
    if(record.controller) {
        const auto resolved=resolveAgentTraversal(*record.controller,profiles);if(!resolved)return distance;
        const auto radius=resolved->radius,height=resolved->height;
        line({0,height*.5f,0},{0,height*.5f,-radius*2});
    }
    if(record.camera) {
        const std::array<Vec3,4> corners{{{-.5f,-.3f,-.8f},{.5f,-.3f,-.8f},{.5f,.3f,-.8f},{-.5f,.3f,-.8f}}};
        for(unsigned i=0;i<4;++i) { line({},corners[i]);line(corners[i],corners[(i+1)%4]); }
    }
    return distance;
}
void pointLightRange(ImDrawList& draw,const Vec3& position,float radius,const Mat4& vp,const Vec4& viewport,ImU32 color) {
    // Point-light radius is world-space, independent of parent scaling.
    constexpr unsigned segments=64;
    for(unsigned plane=0;plane<3;++plane)for(unsigned i=0;i<segments;++i) {
        const auto point=[&](unsigned index) {
            const auto angle=index*2*std::numbers::pi_v<float>/segments;
            Vec3 offset;offset[(plane+1)%3]=radius*std::cos(angle);offset[(plane+2)%3]=radius*std::sin(angle);
            return position+offset;
        };
        if(const auto line=projectClippedSegment(point(i),point(i+1),vp,viewport))
            draw.AddLine({(*line)[0][0],(*line)[0][1]},{(*line)[1][0],(*line)[1][1]},color,1.5f);
    }
}
float directionalMarker(ImDrawList& draw, const Camera& camera, const Mat4& world, const Mat4& viewProjection,
                        const Vec4& viewport, ImVec2 center, ImU32 color, ImVec2 mouse) {
    const Vec3 position=math::transformPoint(world,{0,0,0});
    const Vec3 direction=directionalLightDirection(world);
    // This is an editor marker, so its shaft keeps a useful screen size while
    // its heading is the very same world-space direction sent to the renderer.
    const double depth=std::max(.001,math::dot(position-camera.eye(),camera.forward()));
    const double worldPerPixel=2*depth*std::tan(camera.verticalFov(viewport[2]/viewport[3])*std::numbers::pi/360)/viewport[3];
    const auto tip=math::project(position+direction*(worldPerPixel*48),viewProjection,viewport,ClipDepth::NegativeOneToOne);
    draw.AddCircle(center,5,color,12,1.5f);
    float hitDistance=std::hypot(center.x-mouse.x,center.y-mouse.y);
    if (tip && (*tip)[2]>=0 && (*tip)[2]<=1) {
        const ImVec2 endpoint((*tip)[0],(*tip)[1]);
        const float dx=endpoint.x-center.x, dy=endpoint.y-center.y, length=std::hypot(dx,dy);
        if (length>2) {
            const float ux=dx/length, uy=dy/length, head=std::min(9.0f,length*.4f);
            draw.AddLine(center,endpoint,color,2);
            draw.AddTriangleFilled(endpoint,
                ImVec2(endpoint.x-ux*head-uy*head*.5f,endpoint.y-uy*head+ux*head*.5f),
                ImVec2(endpoint.x-ux*head+uy*head*.5f,endpoint.y-uy*head-ux*head*.5f),color);
            hitDistance=std::min(hitDistance,segmentDistance(mouse,center,endpoint));
        } else if (math::dot(direction,camera.forward())<0) draw.AddCircleFilled(center,2,color,8);
        else {
            draw.AddLine(ImVec2(center.x-3,center.y-3),ImVec2(center.x+3,center.y+3),color,1.5f);
            draw.AddLine(ImVec2(center.x-3,center.y+3),ImVec2(center.x+3,center.y-3),color,1.5f);
        }
    }
    return hitDistance;
}
}
void SceneViewport::select(SceneDocument& document, EntityId id, bool toggle, bool append) {
    pickIntent_=false;
    if(selectViewportHit(document,id,toggle,append))++serial_;
}
void SceneViewport::cancel(SceneDocument& document, Renderer& renderer) {
    waitForRelease_ = waitForRelease_ || transformDrag_.active() || colliderDrag_.active() || volumeDrag_.active() || vertexPaint_.active();
    vertexPaint_.cancel(document,renderer);
    transformDrag_.cancel(document);
    colliderDrag_.cancel(document);
    volumeDrag_.cancel(document);
    if(!ImGuizmo::IsUsingAny()||ImGuizmo::IsUsing())ImGuizmo::Enable(false);
    renderer.cancelPicking(); ++serial_;
    pickIntent_=false;
    materialDrop_.cancel();
    decalDrop_.cancel();
    decalPlacementJustArmed_=false;
}
void SceneViewport::focus(Camera& camera, const SceneDocument& document, const AssetLibrary& assets, float aspect, bool preserveLens) {
    const auto bounds = boundsOf(document,assets,particleBounds_,collisionMeshes_,agentProfiles_,[&](EntityId id)->std::optional<Aabb> {
        const auto patch=decalPatches_?decalPatches_(id):nullptr;return patch?std::optional{patch->bounds}:std::nullopt;
    });
    if (bounds.valid) {
        const auto nearPlane=camera.nearPlane,farPlane=camera.farPlane;
        camera.frameBounds(bounds.minimum,bounds.maximum,aspect);
        if(preserveLens){camera.nearPlane=nearPlane;camera.farPlane=farPlane;}
    }
}
void SceneViewport::drawTraces(const Camera& camera,std::span<const TraceVisualRecord> traces) {
    if(!imageDrawList_ || imageRegion_.width<=0 || imageRegion_.height<=0)return;
    drawTraceOverlay(*imageDrawList_,camera,imageRegion_.width,imageRegion_.height,
        {imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height},traces,ClipDepth::NegativeOneToOne);
}
void SceneViewport::drawDecalPlacementPlane(const Mat4& frame,const Camera& camera,bgfx::TextureHandle texture,const Vec4& color) {
    if(!imageDrawList_||imageRegion_.width<=0||imageRegion_.height<=0)return;
    const Vec4 viewport{imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height};
    const auto vp=camera.projectionMatrix(viewport[2]/viewport[3],ClipDepth::NegativeOneToOne)*camera.viewMatrix();
    constexpr std::array<Vec3,4> local{{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}}};
    std::array<ImVec2,4> points;
    for(unsigned i=0;i<4;++i) {
        const auto p=math::project(math::transformPoint(frame,local[i]),vp,viewport,ClipDepth::NegativeOneToOne);
        if(!p||(*p)[2]<0||(*p)[2]>1)return;
        points[i]={(*p)[0],(*p)[1]};
    }
    imageDrawList_->PushClipRect({viewport[0],viewport[1]},{viewport[0]+viewport[2],viewport[1]+viewport[3]},true);
    const auto fill=ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],std::clamp(color[3],0.f,.65f)});
    if(bgfx::isValid(texture))imageDrawList_->AddImageQuad(ImTextureRef(ImGuiBgfx::textureId(texture)),points[0],points[1],points[2],points[3],{0,1},{1,1},{1,0},{0,0},fill);
    else imageDrawList_->AddQuadFilled(points[0],points[1],points[2],points[3],fill);
    imageDrawList_->AddQuad(points[0],points[1],points[2],points[3],WireColor,1.5f);
    imageDrawList_->PopClipRect();
}
void SceneViewport::prepareVertexPaintUi(SceneDocument& document,Renderer& renderer,std::uint32_t bgfxFrame) {
    if(!vertexPaint_.interacting())return;
    CRIMSON_PROFILE_ZONE("Vertex paint pre UI release and commands");
    const auto& io=ImGui::GetIO();
    const bool command=(io.KeyCtrl&&(ImGui::IsKeyPressed(ImGuiKey_S,false)||ImGui::IsKeyPressed(ImGuiKey_Z,false)||
        ImGui::IsKeyPressed(ImGuiKey_Y,false)||ImGui::IsKeyPressed(ImGuiKey_D,false)))||
        ImGui::IsKeyPressed(ImGuiKey_Delete,false)||ImGui::IsKeyPressed(ImGuiKey_F2,false)||
        ImGui::IsKeyPressed(ImGuiKey_Tab,false)||ImGui::IsKeyPressed(ImGuiKey_Enter,false)||
        ImGui::IsKeyPressed(ImGuiKey_P,false)||
        ImGui::IsKeyPressed(ImGuiKey_LeftAlt,false)||ImGui::IsKeyPressed(ImGuiKey_RightAlt,false);
    // Layer fields use ordinary scene history, without holding the paint
    // surface/clock or waiting for GPU visibility. Finish before menus draw.
    const bool blendCommand=command&&!io.WantTextInput&&!ImGui::IsKeyPressed(ImGuiKey_Enter,false)&&!ImGui::IsKeyPressed(ImGuiKey_Tab,false);
    vertexPaint_.prepareBlendSettingsUi(document,ImGui::IsMouseDown(ImGuiMouseButton_Left),blendCommand,
        io.AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false));
    if(!vertexPaint_.active())return;
    const auto* window=ImGui::FindWindowByName("Viewport");
    const bool unavailable=!window||window->Collapsed||imageRegion_.width<=0||imageRegion_.height<=0;
    Vec2 pointer{};bool hovered=false;
    if(!unavailable) {
        pointer={(io.MousePos.x-imageRegion_.x)/imageRegion_.width*renderer.width(),
            (io.MousePos.y-imageRegion_.y)/imageRegion_.height*renderer.height()};
        hovered=io.MousePos.x>=imageRegion_.x&&io.MousePos.y>=imageRegion_.y&&
            io.MousePos.x<imageRegion_.x+imageRegion_.width&&io.MousePos.y<imageRegion_.y+imageRegion_.height;
    }
    const bool cancel=unavailable||io.AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup);
    const bool held=ImGui::IsMouseDown(ImGuiMouseButton_Left);
    // A result already available on the release frame is ordinary published
    // input. Unfinished GPU work is still canceled immediately by release.
    const auto pick=!held&&!command&&!cancel?renderer.pollPick(bgfxFrame):std::optional<PickResult>{};
    vertexPaint_.prepareUi(document,renderer,serial_,pointer,hovered,held,command,cancel,pick);
}
EditorFrame SceneViewport::draw(Camera& camera, SceneDocument& document, AssetLibrary& assets,
                                Renderer& renderer, bool navigationActive, uint32_t bgfxFrame, bool keyboardBlocked,
                                bool showCollisions,bool showTransformGizmo,bool allowCameraNavigation,bool cameraGestureActive,bool preserveCameraLens) {
    EditorFrame frame;
    imageDrawList_=nullptr;
    ImGuizmo::BeginFrame();
    // Keep the handles, without floating operation/value text during a drag.
    auto& gizmoStyle=ImGuizmo::GetStyle();
    gizmoStyle.Colors[ImGuizmo::TEXT].w=0;
    gizmoStyle.Colors[ImGuizmo::TEXT_SHADOW].w=0;
    const auto cameraView = camera.viewMatrix();
    const bool changedContext=previousSelection_ != document.selection().ids || previousView_ != cameraView ||
        (!transformDrag_.active() && !colliderDrag_.active() && !volumeDrag_.active() && selectionGeneration_ != document.scene().generation());
    if (changedContext || ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // A new click supersedes the prior asynchronous intent before its
        // readback can clear selection or let a newly appeared gizmo claim it.
        // Paint owns its own frozen-view surface request. A normal selection
        // intent or paint's RGB publication must not cancel that request while
        // leaving the tool waiting for a result that can no longer arrive.
        if(!vertexPaint_.surfaceHeld())renderer.cancelPicking();
        ++serial_;
        pickIntent_=false;
        materialDrop_.cancel();
        if(changedContext&&!decalPlacementJustArmed_)decalDrop_.cancel();else decalDrop_.clearPick();
    }
    decalPlacementJustArmed_=false;
    previousSelection_ = document.selection().ids;
    previousView_ = cameraView;
    selectionGeneration_ = document.scene().generation();
    const auto pendingPick = [&] {
        if(vertexPaint_.surfaceHeld()){CRIMSON_PROFILE_ZONE("Vertex paint GPU surface poll");return renderer.pollPick(bgfxFrame);}
        return renderer.pollPick(bgfxFrame);
    }();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) waitForRelease_ = false;
    if(!ImGui::GetDragDropPayload()){materialDropCanceled_=false;decalDropCanceled_=false;}
    if (ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        drawRenderDebugControls(renderer,RenderViewPurpose::Scene,&rotationSnapDegrees_);
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 0 && size.y > 0) {
            const auto* host=ImGui::GetWindowViewport();
            const auto scale = host->FramebufferScale;
            renderer.resizeViewport(static_cast<uint32_t>(std::max(1.0f,std::round(size.x*scale.x))),
                                    static_cast<uint32_t>(std::max(1.0f,std::round(size.y*scale.y))));
            const auto origin = ImGui::GetCursorScreenPos();
            ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(renderer.viewportTexture())),size);
            const auto imageInteraction=viewportImageInteraction([&]{return finishScenePanelEdit(document);});
            const bool imageHovered=imageInteraction.hovered;
            const bool blockKeys=imageInteraction.textInputBlocked || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) ||
                navigationActive || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            frame.viewport = {origin.x,origin.y,size.x,size.y,imageHovered,
                imageInteraction.textInputBlocked || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)};
            frame.viewport.windowId=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(host->PlatformHandle));
            imageDrawList_=ImGui::GetWindowDrawList();imageRegion_=frame.viewport;
            acceptViewportModelDrop(frame);
            const bool editorFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if(transformDrag_.active() && (!transformDrag_.valid(document) || !editorFocus || ImGui::GetIO().AppFocusLost ||
                blockKeys || keyboardBlocked || ImGui::IsKeyPressed(ImGuiKey_Escape,false)))cancel(document,renderer);
            if (!vertexPaint_.enabled() && !keyboardBlocked && !blockKeys && !transformDrag_.active() && !document.transactionActive() && !materialDrop_.active() && !decalDrop_.active() &&
                ImGui::GetDragDropPayload()==nullptr && (imageHovered || editorFocus)) {
                if (ImGui::IsKeyPressed(ImGuiKey_T,false)) operation_ = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R,false)) operation_ = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E,false)) operation_ = ImGuizmo::SCALE;
                if (allowCameraNavigation && ImGui::IsKeyPressed(ImGuiKey_F,false)) focus(camera,document,assets,size.x/size.y,preserveCameraLens);
            }
            frame.viewportCamera=camera;
            const auto view = camera.viewMatrix();
            // ImGuizmo expects homogeneous clip depth. Renderer uses its backend convention.
            const auto projection = camera.projectionMatrix(size.x/size.y,ClipDepth::NegativeOneToOne);
            const bool materialContextChanged=materialDrop_.active() &&
                (pickSize_!=Vec2{size.x,size.y} || pickProjection_!=projection || pickView_!=view ||
                 intentGeneration_!=document.scene().generation() || intentRevision_!=document.scene().revision());
            if((materialDrop_.active() || !frame.materialDragId.empty()) && ImGui::IsKeyPressed(ImGuiKey_Escape,false))materialDropCanceled_=true;
            const bool materialBlocked=vertexPaint_.enabled() || blockKeys || keyboardBlocked || transformDrag_.active() || colliderDrag_.active() || volumeDrag_.active() || document.transactionActive() || materialDropCanceled_;
            if(materialDrop_.active() && (materialBlocked || materialContextChanged ||
                (frame.materialDragId.empty() && !materialDrop_.delivered()))) {
                materialDrop_.cancel();renderer.cancelPicking();++serial_;
            }
            if(!frame.materialDragId.empty() && !materialBlocked && !materialContextChanged) {
                if(!materialDrop_.active()) {
                    pickProjection_=projection;pickView_=view;pickSize_={size.x,size.y};
                    intentGeneration_=document.scene().generation();intentRevision_=document.scene().revision();
                }
                const ViewportMaterialDrop::Pixel pixel{
                    static_cast<uint32_t>(std::clamp((frame.materialDragPosition[0]-origin.x)/size.x*renderer.width(),0.0f,static_cast<float>(renderer.width()-1))),
                    static_cast<uint32_t>(std::clamp((frame.materialDragPosition[1]-origin.y)/size.y*renderer.height(),0.0f,static_cast<float>(renderer.height()-1)))};
                if(materialDrop_.update(frame.materialDragId,pixel,frame.materialDropDelivered)) {
                    renderer.cancelPicking();++serial_;pickIntent_=false;
                }
            }
            if(pendingPick && pendingPick->mode==PickMode::MaterialSlot && materialDrop_.active() &&
                pendingPick->generation==intentGeneration_ && pendingPick->revision==intentRevision_) {
                frame.materialAssignment=materialDrop_.resolve(pendingPick->serial,pendingPick->entityId,pendingPick->materialSlot);
                if(!materialDrop_.pending())renderer.cancelPicking();
            }
            if(materialDrop_.needsPick())materialDrop_.request(++serial_);
            if(const auto& request=materialDrop_.pending()) {
                if(!renderer.requestPick({intentGeneration_,intentRevision_,request->serial,request->pixel.x,request->pixel.y,0,PickMode::MaterialSlot})) {
                    materialDrop_.cancel();renderer.cancelPicking();++serial_;
                }
            }
            frame.materialPreview=frame.materialAssignment ? frame.materialAssignment : materialDrop_.preview();
            const bool decalContextChanged=decalDrop_.active()&&!decalDrop_.awaitingClick()&&
                (pickSize_!=Vec2{size.x,size.y}||pickProjection_!=projection||pickView_!=view||
                 intentGeneration_!=document.scene().generation()||intentRevision_!=document.scene().revision());
            if((decalDrop_.active()||!frame.decalDragId.empty())&&ImGui::IsKeyPressed(ImGuiKey_Escape,false))decalDropCanceled_=true;
            const bool decalBlocked=vertexPaint_.enabled()||blockKeys||keyboardBlocked||transformDrag_.active()||colliderDrag_.active()||volumeDrag_.active()||
                materialDrop_.active()||document.transactionActive()||decalDropCanceled_;
            if(decalDrop_.active()&&(decalBlocked||decalContextChanged||
                (frame.decalDragId.empty()&&!decalDrop_.delivered()&&!decalDrop_.awaitingClick()))) {
                decalDrop_.cancel();renderer.cancelPicking();++serial_;
            }
            const auto decalPixel=[&](Vec2 screen) {return ViewportDecalDrop::Pixel{
                static_cast<std::uint32_t>(std::clamp((screen[0]-origin.x)/size.x*renderer.width(),0.f,float(renderer.width()-1))),
                static_cast<std::uint32_t>(std::clamp((screen[1]-origin.y)/size.y*renderer.height(),0.f,float(renderer.height()-1)))};};
            const auto captureDecalContext=[&] {
                pickProjection_=projection;pickView_=view;pickSize_={size.x,size.y};
                intentGeneration_=document.scene().generation();intentRevision_=document.scene().revision();pickIntent_=false;
            };
            if(!frame.decalDragId.empty()&&!decalBlocked&&!decalContextChanged) {
                if(!decalDrop_.active())captureDecalContext();
                if(decalDrop_.update(frame.decalDragId,decalPixel(frame.decalDragPosition),frame.decalDropDelivered)) {
                    renderer.cancelPicking();++serial_;pickIntent_=false;
                }
            }
            if(decalDrop_.awaitingClick()&&imageHovered&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!decalBlocked) {
                captureDecalContext();const auto mouse=ImGui::GetIO().MousePos;
                decalDrop_.click(decalPixel({mouse.x,mouse.y}));renderer.cancelPicking();++serial_;
            }
            if(pendingPick&&pendingPick->mode==PickMode::Surface&&decalDrop_.active()&&
                pendingPick->generation==intentGeneration_&&pendingPick->revision==intentRevision_) {
                const bool delivered=decalDrop_.delivered();frame.decalPlacement=decalDrop_.resolve(*pendingPick);
                frame.decalPlacementRejected=delivered&&!decalDrop_.active()&&!frame.decalPlacement;
                if(!decalDrop_.pending())renderer.cancelPicking();
            }
            if(decalDrop_.needsPick())decalDrop_.request(++serial_);
            if(const auto& request=decalDrop_.pending())
                if(!renderer.requestPick({intentGeneration_,intentRevision_,request->serial,request->pixel.x,request->pixel.y,0,PickMode::Surface})) {
                    frame.decalPlacementRejected=true;frame.decalPlacementDiagnostic=renderer.pickingStatus();
                    decalDrop_.cancel();decalDropCanceled_=true;renderer.cancelPicking();++serial_;
                }
            frame.decalPreview=frame.decalPlacement?frame.decalPlacement:decalDrop_.preview();
            frame.surfacePickActive=decalDrop_.active();
            if (pendingPick && pendingPick->mode==PickMode::Selection && !materialDrop_.active() && !decalDrop_.active() &&
                pickSize_ == Vec2{size.x,size.y} && pickProjection_ == projection && pickView_ == view &&
                pendingPick->generation == document.scene().generation() && pendingPick->revision == document.scene().revision() &&
                pendingPick->serial == serial_ && !blockKeys && !document.transactionActive()) {
                renderer.cancelPicking();
                select(document,pendingPick->entityId ? pendingPick->entityId : pickCollisionFallback_,pickCtrl_,pickShift_);
            }
            if (pickIntent_) {
                if (blockKeys || document.transactionActive() || ImGui::GetDragDropPayload() ||
                    pickSize_!=Vec2{size.x,size.y} || pickProjection_!=projection || pickView_!=view ||
                    intentGeneration_!=document.scene().generation() || intentRevision_!=document.scene().revision() || intentSerial_!=serial_)
                    pickIntent_=false;
                else renderer.requestPick({intentGeneration_,intentRevision_,intentSerial_,pickX_,pickY_,0,PickMode::Selection});
            }
            const auto vp = projection*view;
            const Vec4 viewport{origin.x,origin.y,size.x,size.y};
            auto* draw = ImGui::GetWindowDrawList();
            const auto mouse=ImGui::GetIO().MousePos;
            const Vec2 paintPointer{(mouse.x-origin.x)/size.x*renderer.width(),(mouse.y-origin.y)/size.y*renderer.height()};
            const bool paintPressed=imageHovered&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if(vertexPaint_.enabled()&&paintPressed&&!blockKeys&&!keyboardBlocked)ImGui::SetWindowFocus();
            vertexPaint_.update(document,renderer,pendingPick,serial_,vp,renderer.width(),renderer.height(),paintPointer,imageHovered,
                (editorFocus||paintPressed)&&!ImGui::GetIO().AppFocusLost,
                blockKeys||keyboardBlocked||materialDrop_.active()||decalDrop_.active()||ImGui::GetDragDropPayload()!=nullptr,
                paintPressed,ImGui::IsMouseDown(ImGuiMouseButton_Left),ImGui::IsKeyPressed(ImGuiKey_Escape,false));
            frame.surfacePickActive|=vertexPaint_.active();
            draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
            if (!frame.placementPreviewIds.empty()) {
                auto label=assets.displayName(frame.placementPreviewIds.front());
                if (frame.placementPreviewIds.size()>1) label+=" +"+std::to_string(frame.placementPreviewIds.size()-1);
                const auto mouse=ImGui::GetIO().MousePos;
                draw->AddText(ImVec2(mouse.x+16,mouse.y+18),ImGui::GetColorU32(ImGuiCol_Text),label.c_str());
            }
            // Apply manipulation before drawing the resulting bounds/markers,
            // but composite its handles last so collision fills cannot hide them.
            draw->ChannelsSplit(2);
            draw->ChannelsSetCurrent(1);
            bool overGizmo = false;
            const auto volumeHandles=drawNavigationVolumeGizmo(*draw,document,volumeDrag_,vp,viewport,
                !vertexPaint_.enabled() && imageHovered && !waitForRelease_ && !blockKeys && !keyboardBlocked && !transformDrag_.active() && !colliderDrag_.active() && !materialDrop_.active() && !decalDrop_.active() &&
                !frame.materialAssignment && ImGui::GetDragDropPayload()==nullptr && (!document.transactionActive() || volumeDrag_.active()),
                editorFocus,blockKeys || keyboardBlocked || ImGui::IsKeyPressed(ImGuiKey_Escape,false));
            if(volumeHandles.started || volumeHandles.canceled) {renderer.cancelPicking();pickIntent_=false;++serial_;}
            if(volumeHandles.canceled)waitForRelease_=true;
            const auto colliderHandles=drawColliderGizmo(*draw,document,colliderDrag_,vp,viewport,showCollisions,
                !vertexPaint_.enabled() && imageHovered && !waitForRelease_ && !blockKeys && !keyboardBlocked && !transformDrag_.active() && !volumeDrag_.active() && !volumeHandles.hovered && !materialDrop_.active() && !decalDrop_.active() &&
                !frame.materialAssignment && ImGui::GetDragDropPayload()==nullptr && (!document.transactionActive() || colliderDrag_.active()),
                editorFocus,blockKeys || keyboardBlocked || ImGui::IsKeyPressed(ImGuiKey_Escape,false));
            if(colliderHandles.started || colliderHandles.canceled) {renderer.cancelPicking();pickIntent_=false;++serial_;}
            if(colliderHandles.canceled)waitForRelease_=true;
            overGizmo=colliderHandles.hovered || volumeHandles.hovered;
            std::vector<EntityId> visibleSelection;
            for(const auto id:document.selection().ids)if(document.scene().isVisible(id))visibleSelection.push_back(id);
            const auto transformRoots=showTransformGizmo && !vertexPaint_.enabled() && !visibleSelection.empty()?document.scene().topmost(visibleSelection):std::vector<EntityId>{};
            const bool transformVisible=showTransformGizmo && !transformRoots.empty();
            if (!transformVisible && transformDrag_.active()) cancel(document,renderer);
            // ImGuizmo's enabled/using state is global. An independent document
            // preview may own the current drag even when Scene has no selection.
            const bool foreignGizmo=ImGuizmo::IsUsingAny()&&!ImGuizmo::IsUsing();
            if (!transformVisible&&!foreignGizmo) ImGuizmo::Enable(false);
            if (transformVisible && !waitForRelease_ && !colliderHandles.hovered && !volumeHandles.hovered && !colliderDrag_.active() && !volumeDrag_.active() && !materialDrop_.active() && !decalDrop_.active() && !frame.materialAssignment &&
                !foreignGizmo && !navigationActive && !keyboardBlocked && !ImGui::GetIO().AppFocusLost && (!document.transactionActive() || transformDrag_.active())) {
                if (!transformDrag_.active()) {
                    decalEditFrame_.reset();
                    const auto bounds = boundsOf(document,assets,{},collisionMeshes_,agentProfiles_);
                    if (visibleSelection.size() == 1) {
                        const auto world = document.scene().worldTransform(transformRoots.front()).value_or(Mat4{});
                        proxy_ = math::translation(math::transformPoint(world,{0,0,0}));
                        if(operation_==ImGuizmo::ROTATE||operation_==ImGuizmo::SCALE) {
                            const auto id=transformRoots.front();
                            decalEditFrame_=crimson::decalEditFrame(document.scene(),id,decalFrames_?decalFrames_(id):std::nullopt);
                            if(decalEditFrame_)if(const auto frame=decals::decalGizmoFrame(*decalEditFrame_))proxy_=*frame;
                        }
                    } else if (bounds.valid) proxy_ = math::translation((bounds.minimum+bounds.maximum)*0.5);
                    startProxy_ = proxy_;
                }
                ImGuizmo::SetDrawlist(draw); ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
                ImGuizmo::SetOrthographic(false);
                const float proxyClipZ=math::transformPoint(vp,math::transformPoint(proxy_,{0,0,0}))[2];
                const bool front=transformDrag_.active() || (math::finite(proxyClipZ) && proxyClipZ >= .001f);
                ImGuizmo::Enable(front && !blockKeys && ImGui::GetDragDropPayload() == nullptr);
                const auto handles=decalEditFrame_?(operation_==ImGuizmo::ROTATE?ImGuizmo::ROTATE_Z:
                    static_cast<ImGuizmo::OPERATION>(ImGuizmo::SCALE_X|ImGuizmo::SCALE_Y)):static_cast<ImGuizmo::OPERATION>(operation_);
                const bool changed = front && ImGuizmo::Manipulate(view.data(),projection.data(),handles,
                    decalEditFrame_?ImGuizmo::LOCAL:ImGuizmo::WORLD,proxy_.data(),nullptr,
                    rotationGizmoSnap(operation_==ImGuizmo::ROTATE,ImGui::GetIO().KeyShift,rotationSnapDegrees_));
                overGizmo |= front && ImGuizmo::IsOver();
                if (ImGuizmo::IsUsing() && !transformDrag_.active()) {
                    ImGui::SetWindowFocus();
                    const auto operation=operation_==ImGuizmo::ROTATE?ViewportTransformOperation::Rotate:
                        operation_==ImGuizmo::SCALE?ViewportTransformOperation::Scale:ViewportTransformOperation::Translate;
                    if(transformDrag_.begin(document,transformRoots,startProxy_,operation,ImGui::GetIO().KeyShift,decalEditFrame_)) {
                        pickIntent_=false;renderer.cancelPicking();++serial_;
                    }
                }
                if(transformDrag_.active() && changed) {
                    const auto threshold=std::max(0.f,ImGui::GetIO().MouseDragThreshold);
                    const bool moved=ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left]>=threshold*threshold;
                    if(!transformDrag_.update(document,proxy_,moved)) {
                        waitForRelease_=true;cancel(document,renderer);
                    }
                }
                if(transformDrag_.active() && ImGui::IsKeyPressed(ImGuiKey_Escape,false))cancel(document,renderer);
                else if(transformDrag_.active() && !ImGuizmo::IsUsing())transformDrag_.finish(document);
            }
            // Release ownership even if a newly blocked/hidden handle skipped
            // Manipulate this frame. A transaction must not outlive its button.
            if(transformDrag_.active() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                transformDrag_.finish(document);if(!ImGuizmo::IsUsingAny()||ImGuizmo::IsUsing())ImGuizmo::Enable(false);
            }
            draw->ChannelsSetCurrent(0);
            // Resolve markers after applying this frame's gizmo delta, so their
            // heading and the light submitted later in the frame agree exactly.
            EntityId iconHit{}; float iconDistance = 15.0f;
            for (const auto& record : document.scene().records()) {
                if(!document.scene().isVisible(record.id))continue;
                if (!viewportEntityHasMarker(record)) continue;
                const auto world = document.scene().worldTransform(record.id);
                if (!world) continue;
                const auto position = math::transformPoint(*world,{0,0,0});
                const auto screen = math::project(position,vp,viewport,ClipDepth::NegativeOneToOne);
                const bool centerVisible=screen && (*screen)[2]>=0 && (*screen)[2]<=1;
                const ImVec2 center=screen?ImVec2((*screen)[0],(*screen)[1]):ImVec2{};
                const auto color = WireColor;
                float distance = centerVisible && viewportEntityHasCenterMarker(record)?
                    std::hypot(center.x-ImGui::GetIO().MousePos.x,center.y-ImGui::GetIO().MousePos.y):1e9f;
                if(record.point && selected(document,record.id))pointLightRange(*draw,position,record.point->radius,vp,viewport,color);
                if(record.navigationVolume) {
                    boxWireSegments(record.navigationVolume->size,[&](Vec3 a,Vec3 b) {
                        if(const auto line=projectClippedSegment(math::transformPoint(*world,a),math::transformPoint(*world,b),vp,viewport)) {
                            const ImVec2 start{(*line)[0][0],(*line)[0][1]},end{(*line)[1][0],(*line)[1][1]};
                            draw->AddLine(start,end,WireColor,selected(document,record.id)?1.5f:1.f);
                            distance=std::min(distance,segmentDistance(ImGui::GetIO().MousePos,start,end));
                        }
                    });
                }
                else if(record.decal&&record.decal->projection==SceneDecalProjection::Box) {
                    boxWireSegments({1,1,1},[&](Vec3 a,Vec3 b) {
                        if(const auto line=projectClippedSegment(math::transformPoint(*world,a),math::transformPoint(*world,b),vp,viewport)) {
                            const ImVec2 from{(*line)[0][0],(*line)[0][1]},to{(*line)[1][0],(*line)[1][1]};
                            if(selected(document,record.id))draw->AddLine(from,to,WireColor,1.5f);
                            distance=std::min(distance,segmentDistance(ImGui::GetIO().MousePos,from,to));
                        }
                    });
                }
                else if (record.collider || record.controller) {
                    if(!showCollisions)continue;
                    const auto& rgba=record.controller ? record.controller->collisionColor : record.collider->collisionColor;
                    const auto collisionColor=ImGui::ColorConvertFloat4ToU32({rgba[0],rgba[1],rgba[2],rgba[3]});
                    distance=std::min(distance,gameplayMarker(*draw,record,*world,vp,viewport,collisionColor,ImGui::GetIO().MousePos,agentProfiles_));
                }
                else if(record.camera)distance=std::min(distance,gameplayMarker(*draw,record,*world,vp,viewport,color,ImGui::GetIO().MousePos));
                else if(!centerVisible)continue;
                else if (record.directional) distance=directionalMarker(*draw,camera,*world,vp,viewport,center,color,ImGui::GetIO().MousePos);
                else if (record.point) draw->AddCircle(center,7,color,12,2);
                else if (record.particle) {
                    draw->AddCircle(center,6,color,12,1.5f);
                    draw->AddCircleFilled({center.x-5,center.y-9},2,color,8);
                    draw->AddCircleFilled({center.x+5,center.y-13},2,color,8);
                }
                else if(record.decal) {
                    draw->AddQuad({center.x,center.y-7},{center.x+7,center.y},{center.x,center.y+7},{center.x-7,center.y},color,1.5f);
                }
                if (centerVisible && selected(document,record.id)) draw->AddCircle(center,11,color,16);
                if (distance < iconDistance) { iconDistance=distance; iconHit=record.id; }
            }
            // Selection outlines stay above collision wires, including a Box
            // Collider whose edges coincide exactly with its selected parent.
            // Authored bounds also keep particle children from animating this box.
            const auto& selection=document.selection().ids;
            const bool pointLightsOnly=!selection.empty() && std::all_of(selection.begin(),selection.end(),[&](EntityId id) {
                const auto entity=document.scene().find(id);return entity && entity->point.has_value();
            });
            if(!pointLightsOnly)for(const auto& box:selectionBoxes(document.scene(),selection,[&](const std::string& id)->std::optional<Aabb> {
                const auto model=assets.findModel(id);return model?std::optional<Aabb>{model->bounds}:std::nullopt;
            },collisionMeshes_,agentProfiles_,[&](EntityId id)->std::optional<Aabb> {
                const auto patch=decalPatches_?decalPatches_(id):nullptr;return patch?std::optional{patch->bounds}:std::nullopt;
            })) {
                if(decalPatches_&&decalPatches_(box.id))continue;
                const auto center=(box.local.minimum+box.local.maximum)*.5f;
                boxWireSegments(box.local.maximum-box.local.minimum,[&](Vec3 a,Vec3 b) {
                    if(const auto line=projectClippedSegment(math::transformPoint(box.world,center+a),math::transformPoint(box.world,center+b),vp,viewport))
                        draw->AddLine({(*line)[0][0],(*line)[0][1]},{(*line)[1][0],(*line)[1][1]},WireColor);
                });
            }
            if(decalPatches_)for(const auto& record:document.scene().records())if(record.decal&&document.scene().isVisible(record.id))if(const auto patch=decalPatches_(record.id)) {
                const bool decalSelected=selected(document,record.id);
                for(std::size_t i=0;i+2<patch->indices.size();i+=3)for(unsigned edge=0;edge<3;++edge) {
                    const auto& a=patch->vertices[patch->indices[i+edge]];
                    const auto& b=patch->vertices[patch->indices[i+(edge+1)%3]];
                    const bool boundary=(a.uv[0]<=1e-5f&&b.uv[0]<=1e-5f)||(a.uv[0]>=1-1e-5f&&b.uv[0]>=1-1e-5f)||
                        (a.uv[1]<=1e-5f&&b.uv[1]<=1e-5f)||(a.uv[1]>=1-1e-5f&&b.uv[1]>=1-1e-5f);
                    if(boundary)if(const auto line=projectClippedSegment(a.position,b.position,vp,viewport)) {
                        const ImVec2 from{(*line)[0][0],(*line)[0][1]},to{(*line)[1][0],(*line)[1][1]};
                        const auto distance=segmentDistance(ImGui::GetIO().MousePos,from,to);
                        const bool hovered=distance<7&&imageHovered&&!decalDrop_.active()&&!materialDrop_.active();
                        if(hovered&&distance<iconDistance){iconHit=record.id;iconDistance=distance;}
                        if(decalSelected||hovered)draw->AddLine(from,to,WireColor,1.5f);
                    }
                }
            }
            draw->ChannelsMerge();
            vertexPaint_.drawCursor(*draw,frame.viewport,paintPointer,renderer.width(),renderer.height());
            draw->PopClipRect();
            if (!vertexPaint_.enabled() && imageHovered && !blockKeys && !overGizmo && !transformDrag_.active() && !colliderDrag_.active() && !volumeDrag_.active() && !waitForRelease_ && !materialDrop_.active() && !decalDrop_.active() && !frame.materialAssignment && !document.transactionActive() &&
                ImGui::GetDragDropPayload() == nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::SetWindowFocus();
                if (iconHit) { renderer.cancelPicking(); select(document,iconHit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift); }
                else {
                    pickCtrl_ = ImGui::GetIO().KeyCtrl; pickShift_ = ImGui::GetIO().KeyShift;
                    pickProjection_ = projection; pickView_ = view; pickSize_ = {size.x,size.y};
                    const auto mouse=ImGui::GetIO().MousePos;
                    pickCollisionFallback_=NoEntity;
                    if(const auto ray=math::rayFromScreen(mouse.x,mouse.y,vp,viewport,ClipDepth::NegativeOneToOne))
                        pickCollisionFallback_=pickCollisionShape(document.scene(),*ray,camera.farPlane,collisionMeshes_,agentProfiles_,showCollisions).value_or(NoEntity);
                    pickX_=static_cast<uint32_t>(std::clamp((mouse.x-origin.x)/size.x*renderer.width(),0.0f,static_cast<float>(renderer.width()-1)));
                    pickY_=static_cast<uint32_t>(std::clamp((mouse.y-origin.y)/size.y*renderer.height(),0.0f,static_cast<float>(renderer.height()-1)));
                    intentGeneration_=document.scene().generation(); intentRevision_=document.scene().revision(); intentSerial_=++serial_;
                    pickIntent_=renderer.requestPick({intentGeneration_,intentRevision_,intentSerial_,pickX_,pickY_,0,PickMode::Selection});
                }
            }
            frame.viewport.inputBlocked |= vertexPaint_.active() || (vertexPaint_.enabled()&&imageHovered&&ImGui::IsMouseDown(ImGuiMouseButton_Left)) || transformDrag_.active() || colliderDrag_.active() || volumeDrag_.active() || materialDrop_.active() || decalDrop_.active() || frame.materialAssignment.has_value() || frame.decalPlacement.has_value() ||
                (overGizmo && !blockKeys) || (document.transactionActive() && !cameraGestureActive) || ImGui::GetDragDropPayload() != nullptr;
        }
    }
    ImGui::End();
    if((vertexPaint_.surfaceHeld() || transformDrag_.active() || colliderDrag_.active() || volumeDrag_.active()) && (frame.viewport.width<=0 || frame.viewport.height<=0))cancel(document,renderer);
    if(materialDrop_.active() && (frame.viewport.width<=0 || frame.viewport.height<=0)) {
        materialDrop_.cancel();renderer.cancelPicking();++serial_;
    }
    if(decalDrop_.active()&&(frame.viewport.width<=0||frame.viewport.height<=0)) {
        decalDrop_.cancel();renderer.cancelPicking();++serial_;
    }
    previousSelection_=document.selection().ids;
    previousView_=camera.viewMatrix();
    selectionGeneration_=document.scene().generation();
    return frame;
}
}
