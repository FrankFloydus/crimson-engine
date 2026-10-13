#include "SceneViewport.h"
#include "ViewportAssetDrop.h"
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
Aabb boundsOf(const SceneDocument& document, const AssetLibrary& assets,const ParticleBoundsQuery& particles) {
    return selectionBounds(document.scene(), document.selection().ids, [&](const std::string& id)->std::optional<Aabb> {
        const auto model = assets.findModel(id);
        return model ? std::optional<Aabb>{model->bounds} : std::nullopt;
    },particles);
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
    if (document.transactionActive()) return;
    auto ids = document.selection().ids;
    if (!toggle && !append) ids.clear();
    const auto found = std::find(ids.begin(),ids.end(),id);
    if (id) {
        if (toggle && found != ids.end()) ids.erase(found);
        else if (found == ids.end()) ids.push_back(id);
    }
    document.select(std::move(ids),id);
    ++serial_;
}
void SceneViewport::cancel(SceneDocument& document, Renderer& renderer) {
    if (dragging_) document.cancel();
    waitForRelease_ = waitForRelease_ || dragging_;
    ImGuizmo::Enable(false);
    dragging_ = false; roots_.clear(); renderer.cancelPicking(); ++serial_;
    pickIntent_=false;
}
void SceneViewport::focus(Camera& camera, const SceneDocument& document, const AssetLibrary& assets, float aspect) {
    const auto bounds = boundsOf(document,assets,particleBounds_);
    if (bounds.valid) camera.frameBounds(bounds.minimum,bounds.maximum,aspect);
}
EditorFrame SceneViewport::draw(Camera& camera, SceneDocument& document, AssetLibrary& assets,
                                Renderer& renderer, bool navigationActive, uint32_t bgfxFrame, bool keyboardBlocked) {
    EditorFrame frame;
    ImGuizmo::BeginFrame();
    // Keep the handles, without floating operation/value text during a drag.
    auto& gizmoStyle=ImGuizmo::GetStyle();
    gizmoStyle.Colors[ImGuizmo::TEXT].w=0;
    gizmoStyle.Colors[ImGuizmo::TEXT_SHADOW].w=0;
    const auto cameraView = camera.viewMatrix();
    if (previousSelection_ != document.selection().ids || previousView_ != cameraView ||
        (!dragging_ && selectionGeneration_ != document.scene().generation())) {
        renderer.cancelPicking(); ++serial_;
        pickIntent_=false;
    }
    previousSelection_ = document.selection().ids;
    previousView_ = cameraView;
    selectionGeneration_ = document.scene().generation();
    const auto pendingPick = renderer.pollPick(bgfxFrame);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) waitForRelease_ = false;
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
            acceptViewportModelDrop(frame);
            const bool editorFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (!keyboardBlocked && !blockKeys && !document.transactionActive() && (imageHovered || editorFocus)) {
                if (ImGui::IsKeyPressed(ImGuiKey_T,false)) operation_ = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R,false)) operation_ = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E,false)) operation_ = ImGuizmo::SCALE;
                if (ImGui::IsKeyPressed(ImGuiKey_F,false)) focus(camera,document,assets,size.x/size.y);
            }
            const auto view = camera.viewMatrix();
            // ImGuizmo expects homogeneous clip depth. Renderer uses its backend convention.
            const auto projection = camera.projectionMatrix(size.x/size.y,ClipDepth::NegativeOneToOne);
            if (pendingPick && pickSize_ == Vec2{size.x,size.y} && pickProjection_ == projection && pickView_ == view &&
                pendingPick->generation == document.scene().generation() && pendingPick->revision == document.scene().revision() &&
                pendingPick->serial == serial_ && !blockKeys && !document.transactionActive()) {
                renderer.cancelPicking(); select(document,pendingPick->entityId,pickCtrl_,pickShift_);
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
            const auto selectionBounds=boundsOf(document,assets,particleBounds_);
            if (selectionBounds.valid) {
                std::array<std::optional<Vec3>,8> corners;
                for (unsigned i=0;i<8;++i) corners[i]=math::project({
                    i&1 ? selectionBounds.maximum[0] : selectionBounds.minimum[0],
                    i&2 ? selectionBounds.maximum[1] : selectionBounds.minimum[1],
                    i&4 ? selectionBounds.maximum[2] : selectionBounds.minimum[2]},vp,viewport,ClipDepth::NegativeOneToOne);
                for (unsigned i=0;i<8;++i) for (unsigned bit=1;bit<=4;bit*=2) if (!(i&bit) && corners[i] && corners[i|bit])
                    draw->AddLine(ImVec2((*corners[i])[0],(*corners[i])[1]),ImVec2((*corners[i|bit])[0],(*corners[i|bit])[1]),ImGui::GetColorU32(ImGuiCol_HeaderActive));
            }
            bool overGizmo = false;
            if (!waitForRelease_ && !document.selection().ids.empty() && !navigationActive && (!document.transactionActive() || dragging_)) {
                if (!dragging_) {
                    const auto top = document.scene().topmost(document.selection().ids);
                    const auto bounds = boundsOf(document,assets,particleBounds_);
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
                    if (!roots_.empty() && document.begin("Transform selection")) { dragging_=true; pickIntent_=false; renderer.cancelPicking(); ++serial_; }
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
            // Resolve markers after applying this frame's gizmo delta, so their
            // heading and the light submitted later in the frame agree exactly.
            EntityId iconHit{}; float iconDistance = 15.0f;
            for (const auto& record : document.scene().records()) {
                if (!record.directional && !record.point && !record.particle && (record.model || record.parent)) continue;
                const auto world = document.scene().worldTransform(record.id);
                if (!world) continue;
                const auto position = math::transformPoint(*world,{0,0,0});
                if (math::dot(position-camera.eye(),camera.forward()) <= 0) continue;
                const auto screen = math::project(position,vp,viewport,ClipDepth::NegativeOneToOne);
                if (!screen || (*screen)[2] < 0 || (*screen)[2] > 1) continue;
                const ImVec2 center((*screen)[0],(*screen)[1]);
                const auto color = ImGui::GetColorU32(selected(document,record.id) ? ImGuiCol_HeaderActive : ImGuiCol_Text);
                float distance = std::hypot(center.x-ImGui::GetIO().MousePos.x,center.y-ImGui::GetIO().MousePos.y);
                if (record.directional) distance=directionalMarker(*draw,camera,*world,vp,viewport,center,color,ImGui::GetIO().MousePos);
                else if (record.point) draw->AddCircle(center,7,color,12,2);
                else if (record.particle) {
                    draw->AddCircle(center,6,color,12,1.5f);
                    draw->AddCircleFilled({center.x-5,center.y-9},2,color,8);
                    draw->AddCircleFilled({center.x+5,center.y-13},2,color,8);
                }
                else draw->AddRect(ImVec2(center.x-5,center.y-5),ImVec2(center.x+5,center.y+5),color);
                if (selected(document,record.id)) draw->AddCircle(center,11,color,16);
                if (distance < iconDistance) { iconDistance=distance; iconHit=record.id; }
            }
            draw->PopClipRect();
            if (imageHovered && !blockKeys && !overGizmo && !dragging_ && !document.transactionActive() &&
                ImGui::GetDragDropPayload() == nullptr && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::SetWindowFocus();
                if (iconHit) { renderer.cancelPicking(); select(document,iconHit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift); }
                else {
                    pickCtrl_ = ImGui::GetIO().KeyCtrl; pickShift_ = ImGui::GetIO().KeyShift;
                    pickProjection_ = projection; pickView_ = view; pickSize_ = {size.x,size.y};
                    const auto mouse=ImGui::GetIO().MousePos;
                    pickX_=static_cast<uint32_t>(std::clamp((mouse.x-origin.x)/size.x*renderer.width(),0.0f,static_cast<float>(renderer.width()-1)));
                    pickY_=static_cast<uint32_t>(std::clamp((mouse.y-origin.y)/size.y*renderer.height(),0.0f,static_cast<float>(renderer.height()-1)));
                    intentGeneration_=document.scene().generation(); intentRevision_=document.scene().revision(); intentSerial_=++serial_;
                    pickIntent_=renderer.requestPick({intentGeneration_,intentRevision_,intentSerial_,pickX_,pickY_,renderer.streamingSignature()});
                }
            }
            frame.viewport.inputBlocked |= dragging_ || (overGizmo && !blockKeys) || document.transactionActive() || ImGui::GetDragDropPayload() != nullptr;
        }
    }
    ImGui::End();
    previousSelection_=document.selection().ids;
    previousView_=camera.viewMatrix();
    selectionGeneration_=document.scene().generation();
    return frame;
}
}
