#include "InspectorTransform.h"
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
                            const InspectorTransformTransactions& transactions) {
    ImGui::SeparatorText("Local transform");
    const auto inDegrees=[](const Vec3& radians) {
        Vec3 degrees;
        for (size_t axis=0;axis<3;++axis)
            degrees[axis]=static_cast<float>(static_cast<double>(radians[axis])*(180.0/std::numbers::pi_v<double>));
        return degrees;
    };
    const bool sameContext=cache.entity==record.id && cache.documentGeneration==document.documentGeneration() &&
        cache.selection==document.selection().ids;
    if (!cache.valid || !sameContext || cache.refresh || cache.shownMatrix!=record.local ||
        cache.sceneGeneration!=document.scene().generation() || cache.sceneRevision!=document.scene().revision()) {
        const auto displayed=math::affineTransformFields(record.local,sameContext && cache.valid ? &cache.fields : nullptr);
        if (displayed) {
            cache.fields=*displayed; cache.degrees=inDegrees(displayed->rotationRadians);
            cache.valid=true; cache.error.clear();
        } else {
            cache.valid=false; cache.error="This transform cannot be displayed as position, rotation and scale.";
        }
        cache.entity=record.id; cache.documentGeneration=document.documentGeneration(); cache.selection=document.selection().ids;
        cache.sceneGeneration=document.scene().generation(); cache.sceneRevision=document.scene().revision();
        cache.shownMatrix=record.local; cache.refresh=false;
    }
    if (cache.valid && ImGui::BeginTable("Transform properties",2,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Property",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("Rotation (deg)").x);
        ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);
        const std::array<const char*,3> labels{"Position","Rotation (deg)","Scale"};
        const std::array<const char*,3> commandLabels{"Change local position","Change local rotation","Change local scale"};
        const std::array<const char*,3> axes{"X","Y","Z"};
        const std::array<float,3> speeds{.05f,.5f,.02f};
        for (size_t property=0;property<3;++property) {
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(labels[property]);
            ImGui::TableSetColumnIndex(1); ImGui::PushID(static_cast<int>(property));
            if (ImGui::BeginTable("Axes",2,ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Axis",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize());
                ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);
                for (size_t axis=0;axis<3;++axis) {
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(axes[axis]);
                    ImGui::TableSetColumnIndex(1); ImGui::PushID(static_cast<int>(axis)); ImGui::SetNextItemWidth(-1);
                    const auto field=static_cast<unsigned>(property*3+axis);
                    auto value=property==0 ? cache.fields.position[axis] : property==1 ? cache.degrees[axis] : cache.fields.scale[axis];
                    const bool changed=ImGui::DragFloat("##value",&value,speeds[property],0,0,"%.3f");
                    if ((ImGui::IsItemActivated() || changed) && (!cache.editing || cache.activeField!=field || !document.transactionActive())) {
                        // Another field can finish earlier in this same draw.
                        // Capture the document's current matrix and rotation
                        // basis, rather than this panel's begin-frame record.
                        const auto current=document.scene().find(record.id);
                        const auto captured=current ? math::affineTransformFields(current->local,&cache.fields) : std::nullopt;
                        if (captured && transactions.begin(field,commandLabels[property])) {
                            cache.baseline=current->local; cache.fields=*captured; cache.degrees=inDegrees(captured->rotationRadians);
                            cache.captured=*captured; cache.capturedDegrees=cache.degrees; cache.editing=true;
                            cache.activeField=field; cache.activeItem=ImGui::GetItemID();
                            cache.shownMatrix=current->local; cache.sceneGeneration=document.scene().generation();
                            cache.sceneRevision=document.scene().revision(); cache.refresh=false;
                        }
                    }
                    if (changed && cache.editing && cache.activeField==field && document.transactionActive()) {
                        auto requested=property==0 ? cache.captured.position : property==1 ? cache.captured.rotationRadians : cache.captured.scale;
                        requested[axis]=property==1 ? static_cast<float>(static_cast<double>(value)*(std::numbers::pi_v<double>/180.0)) : value;
                        const auto candidate=property==0 ? math::withAffinePosition(cache.baseline,requested) : property==1 ?
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
        ImGui::EndTable();
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
