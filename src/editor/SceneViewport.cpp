#include "SceneViewport.h"
#include "ViewportAssetDrop.h"
#include "ViewportSelection.h"
#include "CollisionWire.h"
#include "CollisionOverlay.h"
#include "MeshCollisionOverlay.h"
#include "PlayerLightRigOverlay.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "scene/SceneExtraction.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace crimson {
namespace {
constexpr ImU32 WireColor=IM_COL32(255,166,26,255);
Aabb boundsOf(const SceneDocument& document, const AssetLibrary& assets,const ParticleBoundsQuery& particles = {},const CollisionMeshQuery& meshes = {}) {
    return selectionBounds(document.scene(), document.selection().ids, [&](const std::string& id)->std::optional<Aabb> {
        const auto model = assets.findModel(id);
        return model ? std::optional<Aabb>{model->bounds} : std::nullopt;
    },particles,meshes);
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
                     const Vec4& viewport,ImU32 color,ImVec2 mouse,const CollisionMeshQuery& meshes = {}) {
    float distance=1e9f;
    const auto line=[&](Vec3 a,Vec3 b) {
        const auto projected=projectClippedSegment(math::transformPoint(world,a),math::transformPoint(world,b),vp,viewport);
        if(!projected)return;
        const ImVec2 start((*projected)[0][0],(*projected)[0][1]),end((*projected)[1][0],(*projected)[1][1]);
        draw.AddLine(start,end,color,1.5f);distance=std::min(distance,segmentDistance(mouse,start,end));
    };
    const auto collisionLine=[&](ImVec2 start,ImVec2 end) { distance=std::min(distance,segmentDistance(mouse,start,end)); };
    if(record.meshCollider && meshes)if(const auto source=meshes(record.meshCollider->source.id);source.mesh)
        drawCollisionOverlay(draw,meshCollisionDebug(source.mesh),[](const Vec3& point){return point;},world,vp,viewport,record.meshCollider->collisionColor,collisionLine);
    if(record.collider)drawCollisionOverlay(draw,boxCollisionMesh(),[&](const Vec3& point) {
        const auto& size=record.collider->size;return Vec3{point[0]*size[0],point[1]*size[1],point[2]*size[2]};
    },world,vp,viewport,record.collider->collisionColor,collisionLine);
    if(record.character) {
        const auto radius=record.character->radius,height=record.character->height;
        drawCollisionOverlay(draw,capsuleCollisionMesh(height,radius),[&](const Vec3& point) { return capsuleCollisionPoint(point,height,radius); },
            world,vp,viewport,record.character->collisionColor,collisionLine);
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
    if (dragging_) document.cancel();
    waitForRelease_ = waitForRelease_ || dragging_;
    ImGuizmo::Enable(false);
    dragging_ = false; roots_.clear(); renderer.cancelPicking(); ++serial_;
    pickIntent_=false;
    materialDrop_.cancel();
}
void SceneViewport::focus(Camera& camera, const SceneDocument& document, const AssetLibrary& assets, float aspect) {
    const auto bounds = boundsOf(document,assets,particleBounds_,collisionMeshes_);
    if (bounds.valid) camera.frameBounds(bounds.minimum,bounds.maximum,aspect);
}
void SceneViewport::drawPlayerLightRig(const Camera& camera,const Scene& scene) {
    if(!imageDrawList_ || imageRegion_.width<=0 || imageRegion_.height<=0)return;
    const auto player=scene.find(scene.properties().playerCharacter);
    if(!player || !player->character || (!player->character->lightRig.enabled&&!player->character->darknessReveal.enabled) ||
        !player->character->lightRig.showGizmos)return;
    const auto world=scene.worldTransform(player->id);if(!world)return;
    const auto vp=camera.projectionMatrix(imageRegion_.width/imageRegion_.height,ClipDepth::NegativeOneToOne)*camera.viewMatrix();
    drawPlayerLightRigOverlay(*imageDrawList_,player->character->lightRig,player->character->darknessReveal,
        math::transformPoint(*world,{}),player->character->height,vp,
        {imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height},ClipDepth::NegativeOneToOne);
}
EditorFrame SceneViewport::draw(Camera& camera, SceneDocument& document, AssetLibrary& assets,
                                Renderer& renderer, bool navigationActive, uint32_t bgfxFrame, bool keyboardBlocked,
                                bool showCollisions,bool showTransformGizmo) {
    EditorFrame frame;
    imageDrawList_=nullptr;
    ImGuizmo::BeginFrame();
    // Keep the handles, without floating operation/value text during a drag.
    auto& gizmoStyle=ImGuizmo::GetStyle();
    gizmoStyle.Colors[ImGuizmo::TEXT].w=0;
    gizmoStyle.Colors[ImGuizmo::TEXT_SHADOW].w=0;
    const auto cameraView = camera.viewMatrix();
    if (previousSelection_ != document.selection().ids || previousView_ != cameraView ||
        (!dragging_ && selectionGeneration_ != document.scene().generation()) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // A new click supersedes the prior asynchronous intent before its
        // readback can clear selection or let a newly appeared gizmo claim it.
        renderer.cancelPicking(); ++serial_;
        pickIntent_=false;
        materialDrop_.cancel();
    }
    previousSelection_ = document.selection().ids;
    previousView_ = cameraView;
    selectionGeneration_ = document.scene().generation();
    const auto pendingPick = renderer.pollPick(bgfxFrame);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) waitForRelease_ = false;
    if(!ImGui::GetDragDropPayload())materialDropCanceled_=false;
    if (ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const bool blockKeys = ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) ||
            navigationActive || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (size.x > 0 && size.y > 0) {
            const auto* host=ImGui::GetWindowViewport();
            const auto scale = host->FramebufferScale;
            renderer.resizeViewport(static_cast<uint32_t>(std::max(1.0f,std::round(size.x*scale.x))),
                                    static_cast<uint32_t>(std::max(1.0f,std::round(size.y*scale.y))));
            const auto origin = ImGui::GetCursorScreenPos();
            ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(renderer.viewportTexture())),size);
            const bool imageHovered = ImGui::IsItemHovered();
            frame.viewport = {origin.x,origin.y,size.x,size.y,imageHovered,
                ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)};
            frame.viewport.windowId=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(host->PlatformHandle));
            imageDrawList_=ImGui::GetWindowDrawList();imageRegion_=frame.viewport;
            acceptViewportModelDrop(frame);
            const bool editorFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (!keyboardBlocked && !blockKeys && !document.transactionActive() && !materialDrop_.active() &&
                ImGui::GetDragDropPayload()==nullptr && (imageHovered || editorFocus)) {
                if (ImGui::IsKeyPressed(ImGuiKey_T,false)) operation_ = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R,false)) operation_ = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E,false)) operation_ = ImGuizmo::SCALE;
                if (ImGui::IsKeyPressed(ImGuiKey_F,false)) focus(camera,document,assets,size.x/size.y);
            }
            frame.viewportCamera=camera;
            const auto view = camera.viewMatrix();
            // ImGuizmo expects homogeneous clip depth. Renderer uses its backend convention.
            const auto projection = camera.projectionMatrix(size.x/size.y,ClipDepth::NegativeOneToOne);
            const bool materialContextChanged=materialDrop_.active() &&
                (pickSize_!=Vec2{size.x,size.y} || pickProjection_!=projection || pickView_!=view ||
                 intentGeneration_!=document.scene().generation() || intentRevision_!=document.scene().revision());
            if((materialDrop_.active() || !frame.materialDragId.empty()) && ImGui::IsKeyPressed(ImGuiKey_Escape,false))materialDropCanceled_=true;
            const bool materialBlocked=blockKeys || keyboardBlocked || dragging_ || document.transactionActive() || materialDropCanceled_;
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
            if (pendingPick && pendingPick->mode==PickMode::Selection && !materialDrop_.active() &&
                pickSize_ == Vec2{size.x,size.y} && pickProjection_ == projection && pickView_ == view &&
                pendingPick->generation == document.scene().generation() && pendingPick->revision == document.scene().revision() &&
                pendingPick->serial == serial_ && !blockKeys && !document.transactionActive()) {
                renderer.cancelPicking();
                select(document,pendingPick->entityId ? pendingPick->entityId : showCollisions ? pickCollisionFallback_ : NoEntity,pickCtrl_,pickShift_);
            }
            if (pickIntent_) {
                if (blockKeys || document.transactionActive() || ImGui::GetDragDropPayload() ||
                    pickSize_!=Vec2{size.x,size.y} || pickProjection_!=projection || pickView_!=view ||
                    intentGeneration_!=document.scene().generation() || intentRevision_!=document.scene().revision() || intentSerial_!=serial_)
                    pickIntent_=false;
                else renderer.requestPick({intentGeneration_,intentRevision_,intentSerial_,pickX_,pickY_,renderer.streamingSignature()});
            }
            const auto vp = projection*view;
            const Vec4 viewport{origin.x,origin.y,size.x,size.y};
            auto* draw = ImGui::GetWindowDrawList();
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
            if (!showTransformGizmo && dragging_) cancel(document,renderer);
            if (!showTransformGizmo) ImGuizmo::Enable(false);
            if (showTransformGizmo && !waitForRelease_ && !materialDrop_.active() && !frame.materialAssignment &&
                !document.selection().ids.empty() && !navigationActive && (!document.transactionActive() || dragging_)) {
                if (!dragging_) {
                    const auto top = document.scene().topmost(document.selection().ids);
                    const auto bounds = boundsOf(document,assets,{},collisionMeshes_);
                    if (document.selection().ids.size() == 1) {
                        const auto world = document.scene().worldTransform(top.front()).value_or(Mat4{});
                        proxy_ = math::translation(math::transformPoint(world,{0,0,0}));
                    } else if (bounds.valid) proxy_ = math::translation((bounds.minimum+bounds.maximum)*0.5);
                    startProxy_ = proxy_;
                }
                ImGuizmo::SetDrawlist(draw); ImGuizmo::SetRect(origin.x,origin.y,size.x,size.y);
                ImGuizmo::SetOrthographic(false);
                const float proxyClipZ=math::transformPoint(vp,math::transformPoint(proxy_,{0,0,0}))[2];
                const bool front=dragging_ || (math::finite(proxyClipZ) && proxyClipZ >= .001f);
                ImGuizmo::Enable(front && !blockKeys && ImGui::GetDragDropPayload() == nullptr);
                const bool changed = front && ImGuizmo::Manipulate(view.data(),projection.data(),static_cast<ImGuizmo::OPERATION>(operation_),
                    ImGuizmo::WORLD,proxy_.data());
                overGizmo = front && ImGuizmo::IsOver();
                if (ImGuizmo::IsUsing() && !dragging_) {
                    ImGui::SetWindowFocus();
                    roots_.clear();
                    for (const auto id : document.scene().topmost(document.selection().ids)) {
                        const auto record = document.scene().find(id);
                        const auto world = document.scene().worldTransform(id);
                        const auto parent = record && record->parent ? document.scene().worldTransform(record->parent) : std::optional<Mat4>{Mat4{}};
                        const auto inv = parent ? math::inverse(*parent) : std::nullopt;
                        if (!record || !world || !inv) { roots_.clear(); break; }
                        roots_.push_back({id,record->local,*world,*inv});
                    }
                    if (!roots_.empty() && document.beginTransform("Transform selection",document.scene().topmost(document.selection().ids))) { dragging_=true; pickIntent_=false; renderer.cancelPicking(); ++serial_; }
                }
                if (dragging_ && changed) {
                    const auto inverse = math::inverse(startProxy_);
                    if (inverse) {
                        const Mat4 delta = proxy_* *inverse;
                        document.update([&](auto& records, auto&) {
                            for (const auto& root : roots_) {
                                const auto found = std::find_if(records.begin(),records.end(),[&](const auto& r){return r.id==root.id;});
                                if (found != records.end()) found->local = root.parentInverse*delta*root.world;
                            }
                        });
                    }
                }
                if (dragging_ && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) cancel(document,renderer);
                else if (dragging_ && !ImGuizmo::IsUsing()) { document.commit(); dragging_=false; roots_.clear(); }
            }
            draw->ChannelsSetCurrent(0);
            // Resolve markers after applying this frame's gizmo delta, so their
            // heading and the light submitted later in the frame agree exactly.
            EntityId iconHit{}; float iconDistance = 15.0f;
            for (const auto& record : document.scene().records()) {
                if (!record.directional && !record.point && !record.particle && !record.collider && !record.meshCollider && !record.character && !record.camera && (record.model || record.parent)) continue;
                const auto world = document.scene().worldTransform(record.id);
                if (!world) continue;
                const auto position = math::transformPoint(*world,{0,0,0});
                const auto screen = math::project(position,vp,viewport,ClipDepth::NegativeOneToOne);
                const bool centerVisible=screen && (*screen)[2]>=0 && (*screen)[2]<=1;
                const ImVec2 center=screen?ImVec2((*screen)[0],(*screen)[1]):ImVec2{};
                const auto color = WireColor;
                float distance = centerVisible?std::hypot(center.x-ImGui::GetIO().MousePos.x,center.y-ImGui::GetIO().MousePos.y):1e9f;
                if(record.point && selected(document,record.id))pointLightRange(*draw,position,record.point->radius,vp,viewport,color);
                if (record.collider || record.character || record.meshCollider) {
                    if(!showCollisions)continue;
                    const auto& rgba=record.character ? record.character->collisionColor : record.collider?record.collider->collisionColor:record.meshCollider->collisionColor;
                    const auto collisionColor=ImGui::ColorConvertFloat4ToU32({rgba[0],rgba[1],rgba[2],rgba[3]});
                    distance=std::min(distance,gameplayMarker(*draw,record,*world,vp,viewport,collisionColor,ImGui::GetIO().MousePos,collisionMeshes_));
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
                else draw->AddRect(ImVec2(center.x-5,center.y-5),ImVec2(center.x+5,center.y+5),color);
                if (centerVisible && selected(document,record.id)) draw->AddCircle(center,11,color,16);
                if (distance < iconDistance) { iconDistance=distance; iconHit=record.id; }
            }
            // Selection outlines stay above collision wires, including a Box
            // Collider whose edges coincide exactly with its selected parent.
            // Authored bounds also keep particle children from animating this box.
            const auto selectionBounds=boundsOf(document,assets,{},collisionMeshes_);
            const auto& selection=document.selection().ids;
            const bool pointLightsOnly=!selection.empty() && std::all_of(selection.begin(),selection.end(),[&](EntityId id) {
                const auto entity=document.scene().find(id);return entity && entity->point.has_value();
            });
            if (selectionBounds.valid && !pointLightsOnly) {
                const auto center=(selectionBounds.minimum+selectionBounds.maximum)*.5f;
                boxWireSegments(selectionBounds.maximum-selectionBounds.minimum,[&](Vec3 a,Vec3 b) {
                    if(const auto line=projectClippedSegment(center+a,center+b,vp,viewport))
                        draw->AddLine({(*line)[0][0],(*line)[0][1]},{(*line)[1][0],(*line)[1][1]},WireColor);
                });
            }
            draw->ChannelsMerge();
            draw->PopClipRect();
            if (imageHovered && !blockKeys && !overGizmo && !dragging_ && !materialDrop_.active() && !frame.materialAssignment && !document.transactionActive() &&
                ImGui::GetDragDropPayload() == nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::SetWindowFocus();
                if (iconHit) { renderer.cancelPicking(); select(document,iconHit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift); }
                else {
                    pickCtrl_ = ImGui::GetIO().KeyCtrl; pickShift_ = ImGui::GetIO().KeyShift;
                    pickProjection_ = projection; pickView_ = view; pickSize_ = {size.x,size.y};
                    const auto mouse=ImGui::GetIO().MousePos;
                    pickCollisionFallback_=NoEntity;
                    if(showCollisions)if(const auto ray=math::rayFromScreen(mouse.x,mouse.y,vp,viewport,ClipDepth::NegativeOneToOne))
                        pickCollisionFallback_=pickCollisionShape(document.scene(),*ray,camera.farPlane,collisionMeshes_).value_or(NoEntity);
                    pickX_=static_cast<uint32_t>(std::clamp((mouse.x-origin.x)/size.x*renderer.width(),0.0f,static_cast<float>(renderer.width()-1)));
                    pickY_=static_cast<uint32_t>(std::clamp((mouse.y-origin.y)/size.y*renderer.height(),0.0f,static_cast<float>(renderer.height()-1)));
                    intentGeneration_=document.scene().generation(); intentRevision_=document.scene().revision(); intentSerial_=++serial_;
                    pickIntent_=renderer.requestPick({intentGeneration_,intentRevision_,intentSerial_,pickX_,pickY_,renderer.streamingSignature()});
                }
            }
            frame.viewport.inputBlocked |= dragging_ || materialDrop_.active() || frame.materialAssignment.has_value() ||
                (overGizmo && !blockKeys) || document.transactionActive() || ImGui::GetDragDropPayload() != nullptr;
        }
    }
    ImGui::End();
    if(materialDrop_.active() && (frame.viewport.width<=0 || frame.viewport.height<=0)) {
        materialDrop_.cancel();renderer.cancelPicking();++serial_;
    }
    previousSelection_=document.selection().ids;
    previousView_=camera.viewMatrix();
    selectionGeneration_=document.scene().generation();
    return frame;
}
}
