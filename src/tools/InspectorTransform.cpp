#include "InspectorTransform.h"
#include "PropertyReset.h"
#include "editor/DecalAuthoring.h"
#include "decals/DecalTransform.h"
#include "scene/SceneDocument.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <numbers>

namespace crimson {
namespace {
template<class Mutator> SceneDocument::Mutation changeEntity(EntityId id,Mutator mutate) {
    return [id,mutate](auto& records,auto&) {
        const auto found=std::find_if(records.begin(),records.end(),[id](const auto& record){return record.id==id;});
        if (found!=records.end()) mutate(*found);
    };
}
}
bool finishInspectorTransform(SceneDocument& document,InspectorTransformState& cache) {
    if (!cache.editing || !document.transactionActive()) return false;
    if (cache.activeItem==ImGui::GetActiveID()) ImGui::ClearActiveID();
    if (!document.commit()) return false;
    cache.editing=false; cache.refresh=true;
    return true;
}
void drawInspectorTransform(SceneDocument& document, InspectorTransformState& cache, const EntityRecord& record,
                            const InspectorTransformTransactions& transactions,const std::optional<Mat4>& decalSurfaceFrame) {
    if (!ImGui::CollapsingHeader("Transform",ImGuiTreeNodeFlags_DefaultOpen)) {
        if (cache.editing) {
            if (cache.activeItem==ImGui::GetActiveID()) ImGui::ClearActiveID();
            transactions.finish(cache.activeField);
            cache.editing=false; cache.refresh=true;
        }
        return;
    }
    const auto inDegrees=[](const Vec3& radians) {
        Vec3 degrees;
        for (size_t axis=0;axis<3;++axis)
            degrees[axis]=static_cast<float>(static_cast<double>(radians[axis])*(180.0/std::numbers::pi_v<double>));
        return degrees;
    };
    const bool sameContext=cache.entity==record.id && cache.documentGeneration==document.documentGeneration() &&
        cache.selection==document.selection().ids;
    const bool box=record.decal&&record.decal->projection==SceneDecalProjection::Box;
    const bool decal=record.decal&&record.decal->projection==SceneDecalProjection::Surface;
    const auto worldSize=[](const Mat4& frame){return Vec3{
        static_cast<float>(math::length(math::transformDirection(frame,{1,0,0}))),
        static_cast<float>(math::length(math::transformDirection(frame,{0,1,0}))),
        static_cast<float>(math::length(math::transformDirection(frame,{0,0,1})))};};
    if (!cache.valid || !sameContext || cache.refresh || cache.shownMatrix!=record.local ||
        (decal&&cache.shownDecalSurfaceFrame!=decalSurfaceFrame) ||
        cache.sceneGeneration!=document.scene().generation() || cache.sceneRevision!=document.scene().revision()) {
        const auto displayed=math::affineTransformFields(record.local,sameContext && cache.valid ? &cache.fields : nullptr);
        if (displayed) {
            const float decalReference=sameContext?cache.degrees[2]:0;
            cache.fields=*displayed; cache.degrees=inDegrees(displayed->rotationRadians);
            cache.valid=true; cache.error.clear();
            if(box)if(const auto frame=document.scene().worldTransform(record.id))cache.fields.scale=worldSize(*frame);
            if(decal){
                const auto frame=decalEditFrame(document.scene(),record.id,decalSurfaceFrame);
                cache.decalFrameValid=bool(frame);
                if(frame){
                    const auto size=decals::decalSize(*frame);
                    cache.fields.scale={size[0],size[1],1};
                    cache.degrees={0,0,decals::decalRotationDegrees(*frame,decalReference)};
                }
            }
        } else {
            cache.valid=false; cache.error="This transform cannot be displayed as position, rotation and scale.";
        }
        cache.entity=record.id; cache.documentGeneration=document.documentGeneration(); cache.selection=document.selection().ids;
        cache.sceneGeneration=document.scene().generation(); cache.sceneRevision=document.scene().revision();
        cache.shownMatrix=record.local; cache.refresh=false;
        cache.shownDecalSurfaceFrame=decalSurfaceFrame;
    }
    if (cache.valid) {
        ImGui::PushID("Transform properties");
        const std::array<const char*,3> labels{"Position","Rotation (deg)",(decal||box)?"Size (m)":"Scale"};
        const std::array<const char*,3> commandLabels{"Change local position",decal?"Rotate decal":"Change local rotation",(decal||box)?"Resize decal":"Change local scale"};
        const std::array<const char*,3> axes{"X","Y","Z"};
        const std::array<ImU32,3> axisColors{IM_COL32(170,0,0,255),IM_COL32(0,170,0,255),IM_COL32(0,0,170,255)};
        const std::array<float,3> speeds{.05f,.5f,.02f};
        for (size_t property=0;property<3;++property) {
            ImGui::PushID(static_cast<int>(property));
            ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(labels[property]);
            if (property==2) {
                const float width=ImGui::GetFrameHeight()*2+ImGui::GetStyle().ItemInnerSpacing.x*2+ImGui::CalcTextSize("Lock proportions").x;
                const float right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-width;
                ImGui::SameLine(std::max(ImGui::GetItemRectMax().x-ImGui::GetWindowPos().x+ImGui::GetStyle().ItemSpacing.x,right));
                resettableProperty(cache.scaleLinked,InspectorTransformState{}.scaleLinked,[&]{return ImGui::Checkbox("Lock proportions",&cache.scaleLinked);});
            }
            const unsigned columns=decal&&property>0?(property==1?1u:2u):3u;
            if (ImGui::BeginTable("Axes",static_cast<int>(columns),ImGuiTableFlags_SizingStretchSame)) {
                for(unsigned column=0;column<columns;++column)ImGui::TableSetupColumn(axes[column],ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                for (unsigned column=0;column<columns;++column) {
                    const unsigned axis=decal&&property==1?2u:column;
                    ImGui::TableSetColumnIndex(static_cast<int>(column));
                    if(!(decal&&property==1)){
                        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted((decal||box)&&property==2?(axis==0?"Width":axis==1?"Height":"Depth"):axes[axis]);
                        ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
                    }
                    ImGui::PushID(static_cast<int>(axis)); ImGui::SetNextItemWidth(-1);
                    const auto field=static_cast<unsigned>(property*3+axis);
                    auto value=property==0 ? cache.fields.position[axis] : property==1 ? cache.degrees[axis] : cache.fields.scale[axis];
                    ImGui::BeginDisabled(decal&&property>0&&!cache.decalFrameValid);
                    const bool changed=resettableProperty(value,property==2?1.f:0.f,[&]{return ImGui::DragFloat("##value",&value,speeds[property],0,0,"%.3f");});
                    ImGui::EndDisabled();
                    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
                    const float stripWidth=std::max(1.f,ImGui::GetFontSize()*.15f);
                    if(!(decal&&property==1))ImGui::GetWindowDrawList()->AddRectFilled({minimum.x,minimum.y},{minimum.x+stripWidth,maximum.y},ImGui::GetColorU32(axisColors[axis]));
                    if ((ImGui::IsItemActivated() || changed) && (!cache.editing || cache.activeField!=field || !document.transactionActive())) {
                        // Another field can finish earlier in this same draw.
                        // Capture the document's current matrix and rotation
                        // basis, rather than this panel's begin-frame record.
                        const auto current=document.scene().find(record.id);
                        const auto captured=current ? math::affineTransformFields(current->local,&cache.fields) : std::nullopt;
                        const auto frame=decal?decalEditFrame(document.scene(),record.id,decalSurfaceFrame):box?document.scene().worldTransform(record.id):std::optional<Mat4>{};
                        const auto parentWorld=current&&current->parent?document.scene().worldTransform(current->parent):std::optional<Mat4>{Mat4{}};
                        const auto parentInverse=parentWorld?math::inverse(*parentWorld):std::nullopt;
                        if (captured && (!(decal||box)||property==0||(frame&&parentInverse)) && transactions.begin(field,commandLabels[property])) {
                            const float decalReference=cache.degrees[2];
                            cache.baseline=current->local; cache.fields=*captured; cache.degrees=inDegrees(captured->rotationRadians);
                            cache.captured=*captured; cache.capturedDegrees=cache.degrees; cache.editing=true;
                            if(box&&frame){
                                cache.capturedDecalFrame=*frame;if(parentInverse)cache.capturedParentInverse=*parentInverse;
                                cache.fields.scale=cache.captured.scale=worldSize(*frame);
                            }
                            if(decal&&frame){
                                cache.capturedDecalFrame=*frame;if(parentInverse)cache.capturedParentInverse=*parentInverse;
                                const auto size=decals::decalSize(*frame);
                                cache.fields.scale=cache.captured.scale={size[0],size[1],1};
                                cache.degrees=cache.capturedDegrees={0,0,decals::decalRotationDegrees(*frame,decalReference)};
                            }
                            cache.capturedScaleLinked=cache.scaleLinked;
                            cache.activeField=field; cache.activeItem=ImGui::GetItemID();
                            cache.shownMatrix=current->local; cache.sceneGeneration=document.scene().generation();
                            cache.sceneRevision=document.scene().revision(); cache.refresh=false;
                        }
                    }
                    if (changed && cache.editing && cache.activeField==field && document.transactionActive()) {
                        auto requested=property==0 ? cache.captured.position : property==1 ? cache.captured.rotationRadians : cache.captured.scale;
                        requested[axis]=property==1 ? static_cast<float>(static_cast<double>(value)*(std::numbers::pi_v<double>/180.0)) : value;
                        if (property==2 && cache.capturedScaleLinked) {
                            // Always derive the ratio from the gesture baseline,
                            // including when a drag crosses through zero. A zero
                            // initial component has no ratio; match the material
                            // editor's additive fallback so collapsed axes can grow.
                            if (cache.captured.scale[axis]!=0)
                                requested=cache.captured.scale*(static_cast<double>(value)/cache.captured.scale[axis]);
                            else {
                                const float delta=value-cache.captured.scale[axis];
                                requested=cache.captured.scale+Vec3{delta,delta,delta};
                            }
                            requested[axis]=value;
                            if(decal)requested[2]=1;
                        }
                        std::optional<Mat4> candidate;
                        if(box&&property==2){
                            if(const auto basis=math::affineTransformFields(cache.capturedDecalFrame)) {
                                auto signedSize=requested;
                                for(unsigned component=0;component<3;++component)if(basis->scale[component]<0)signedSize[component]*=-1;
                                if(const auto world=math::withAffineScale(cache.capturedDecalFrame,*basis,signedSize))candidate=cache.capturedParentInverse*(*world);
                            }
                        }else if(decal&&property>0){
                            const auto world=property==1?decals::withDecalRotation(cache.capturedDecalFrame,value):
                                decals::withDecalSize(cache.capturedDecalFrame,{requested[0],requested[1]});
                            if(world)candidate=cache.capturedParentInverse*(*world);
                        }else candidate=property==0 ? math::withAffinePosition(cache.baseline,requested) : property==1 ?
                            math::withAffineRotation(cache.baseline,cache.captured,requested) : math::withAffineScale(cache.baseline,cache.captured,requested);
                        if (candidate && document.update(changeEntity(record.id,[matrix=*candidate](auto& entity){entity.local=matrix;}))) {
                            if (property==0) cache.fields.position=requested;
                            else if (property==1) { cache.fields.rotationRadians=requested; cache.degrees[axis]=value; }
                            else cache.fields.scale=requested;
                            cache.shownMatrix=*candidate; cache.sceneGeneration=document.scene().generation(); cache.sceneRevision=document.scene().revision(); cache.error.clear();
                        } else cache.error="Enter a finite transform value.";
                    }
                    if (cache.editing && cache.activeField==field &&
                        (ImGui::IsItemDeactivated() || (changed && !ImGui::IsItemActive()))) {
                        transactions.finish(field); cache.editing=false; cache.refresh=true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    // A scalar can lose ownership after its submission (another window, a
    // clipped field, or a menu). Do not depend on observing its one-frame
    // IsItemDeactivated event to release the scene transaction.
    if (cache.editing && ImGui::GetActiveID()!=cache.activeItem) {
        transactions.finish(cache.activeField); cache.editing=false; cache.refresh=true;
    }
    if (!cache.error.empty()) ImGui::TextWrapped("%s",cache.error.c_str());
    if (!document.status().empty()) ImGui::TextWrapped("%s",document.status().c_str());
}
} // namespace crimson
