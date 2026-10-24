#include "RagdollEditor.h"
#include "RagdollPreviewInteraction.h"
#include "RagdollEditorLayout.h"
#include "DocumentEditorMenu.h"
#include "PropertyReset.h"
#include "physics/PhysicsWorld.h"
#include "editor/RagdollAuthoring.h"
#include <numbers>
#include <unordered_set>
#include <functional>
#include "assets/AssetLibrary.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "resources/Paths.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace crimson {
namespace {
struct PreviewPins {
    std::shared_ptr<const PreparedEditorModel> model;
    std::shared_ptr<const ModelPose> pose;
    std::shared_ptr<const PreparedEnvironment> environment;
};
}
bool RagdollEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen()&&window&&window->RootWindow->ID==ImHashStr("###RagdollEditor");
}
void RagdollEditor::finishInteraction(bool preserveActiveItem) {
    const bool gizmoActive=gizmo_.active();
    if(gizmoActive&&!gizmo_.finish(document_)){message_=gizmo_.diagnostic();return;}
    if(gizmoActive||(document_.transactionActive()&&document_.commit())){if(!preserveActiveItem)ImGui::ClearActiveID();suppressFinalUpdate_=true;propertySection_=0;}
}
void RagdollEditor::cancelInteraction() {
    previewGesture_=0;endDrag();gizmo_.cancel(document_);
    if(document_.transactionActive()){document_.cancel();ImGui::ClearActiveID();suppressFinalUpdate_=true;propertySection_=0;}
}
void RagdollEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None)return;
    if(gizmo_.active())finishInteraction();
    if(document_.transactionActive()&&!document_.commit()){message_=document_.status();return;}
    pending_=pending;pendingId_=std::move(id);
    if(isOpen()&&document_.dirty())dirtyPopup_=true;
    else if(assets)perform(*assets);
    else if(pending==Pending::Exit){pending_=Pending::None;exitReady_=true;}
}
void RagdollEditor::requestExit(){if(!exitReady_)request(Pending::Exit,{},nullptr);}
void RagdollEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(isOpen()&&document_.assetId()==id){focusNext_=true;return;}
    request(Pending::Open,std::move(id),&assets);
}
void RagdollEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;pending_=Pending::None;continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        gizmo_.cancel(document_);document_.close();assets.protectRagdollDocument({});assets.setRagdollDocumentReferences({},{});assets.refreshRagdolls();
        resetSimulation();boundSource_.reset();return;
    }
    if(pending==Pending::Open) {
        if(document_.load(pendingId_)) {
            assets.protectRagdollDocument(document_.assetId());publishedRevision_=0;
            publish(assets);focusNext_=true;externalChanged_=false;message_.clear();
            seconds_=0;startingClip_=-1;framePending_=true;resetSimulation();boundSource_.reset();previewGesture_=0;selectedBody_=-1;selectedNode_=-1;rotationBody_=-1;selectedNodes_.clear();
        }else message_=document_.status();
    }
    pendingId_.clear();
}
void RagdollEditor::publish(AssetLibrary& assets) {
    if(!isOpen())return;
    if(publishedId_!=document_.assetId()||publishedRevision_!=document_.revision()) {
        try {
            assets.publishRagdoll(document_.committed());
            publishedId_=document_.assetId();publishedRevision_=document_.revision();
        }catch(const std::exception& error){message_=error.what();}
    }
    assets.setRagdollDocumentReferences(document_.modelReferences(),document_.modelReferences(true));
}
void RagdollEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!isOpen())return;
    if(gizmo_.active())finishInteraction();
    if(document_.transactionActive()&&!document_.commit()){message_=document_.status();return;}
    publish(assets);
    if(saveAs){std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;}
    if(document_.externalFileChanged()){externalPopup_=true;return;}
    if(document_.save()){assets.noteAssetWrite(document_.assetId());assets.refreshRagdolls();message_.clear();}
    else message_=document_.status();
}
void RagdollEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!isOpen())return;
    try {
        const auto file=ragdollAssetPath(document_.projectRoot(),document_.assetId());
        for(const auto& path:paths)if(resourcePathKey(path)==resourcePathKey(file))externalChanged_=true;
    }catch(...){externalChanged_=true;}
}
void RagdollEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_){ImGui::OpenPopup("Unsaved ragdoll");dirtyPopup_=false;}
    if(ImGui::BeginPopupModal("Unsaved ragdoll",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Save changes to %s?",document_.definition().name.c_str());
        if(ImGui::Button("Save")) {
            continueAfterSave_=true;save(assets);
            if(!document_.dirty()){perform(assets);continueAfterSave_=false;ImGui::CloseCurrentPopup();}
            else if(externalPopup_||savePopup_)ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();if(ImGui::Button("Discard")) {
            if(document_.discard()){publish(assets);perform(assets);ImGui::CloseCurrentPopup();}
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")){pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(savePopup_){ImGui::OpenPopup("Save ragdoll as");savePopup_=false;}
    if(ImGui::BeginPopupModal("Save ragdoll as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save")||enter)&&saveName_[0]) {
            const auto previous=document_.assetId();const auto previousSaved=document_.savedDefinition();
            if(document_.save(saveName_.data(),true)) {
                assets.protectRagdollDocument(document_.assetId());
                auto source=previousSaved;
                try{source=loadRagdollAsset(assets.projectRoot(),previous);}catch(...){ }
                assets.publishRagdoll(std::make_shared<const RagdollAsset>(std::move(source)));
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshRagdolls();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")){if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_){ImGui::OpenPopup("Ragdoll changed on disk");externalPopup_=false;}
    if(ImGui::BeginPopupModal("Ragdoll changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The ragdoll changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save({},false,true)) {
                assets.noteAssetWrite(document_.assetId());assets.refreshRagdolls();message_.clear();
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Reload and discard edits")) {
            const auto id=document_.assetId();
            if(document_.load(id)) {
                publish(assets);message_.clear();if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Keep editing")){pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("Save As")){save(assets,true);ImGui::CloseCurrentPopup();}
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
}

namespace {
std::string nodeLabel(const ModelAsset& model,std::uint32_t node) {
    return node<model.nodes.size()?(model.nodes[node].name.empty()?"Bone "+std::to_string(node):model.nodes[node].name):"Missing bone "+std::to_string(node);
}
struct ItemEdit {bool changed{},activated{},deactivated{},active{};};
ItemEdit axisRow(const char* label,Vec3& value,float speed,float minimum=0,float maximum=0,bool link=false) {
    ItemEdit result;ImGui::PushID(label);ImGui::TextUnformatted(label);
    if(ImGui::BeginTable("Axes",3,ImGuiTableFlags_SizingStretchSame)) {
        constexpr std::array<const char*,3> names{"X","Y","Z"};
        constexpr std::array<ImU32,3> colors{IM_COL32(170,0,0,255),IM_COL32(0,170,0,255),IM_COL32(0,0,170,255)};
        for(unsigned axis=0;axis<3;++axis) {
            ImGui::TableNextColumn();ImGui::PushID(int(axis));ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(names[axis]);ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);ImGui::SetNextItemWidth(-1);
            const auto before=value[axis];const bool changed=ImGui::DragFloat("##value",&value[axis],speed,minimum,maximum,"%.3f",maximum>minimum?ImGuiSliderFlags_AlwaysClamp:0);
            if(changed&&link&&before>1e-6f)for(unsigned other=0;other<3;++other)if(other!=axis)value[other]=std::clamp(value[other]*value[axis]/before,minimum,maximum);
            const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();ImGui::GetWindowDrawList()->AddRectFilled(a,{a.x+std::max(1.f,ImGui::GetFontSize()*.15f),b.y},colors[axis]);
            result.changed|=changed;result.activated|=ImGui::IsItemActivated();result.deactivated|=ImGui::IsItemDeactivated();result.active|=ImGui::IsItemActive();ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();return result;
}
Quat quaternionDegrees(const Vec3& degrees) {
    const double k=std::numbers::pi/360.;const auto x=degrees[0]*k,y=degrees[1]*k,z=degrees[2]*k;
    const auto cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    return {float(sx*cy*cz-cx*sy*sz),float(cx*sy*cz+sx*cy*sz),float(cx*cy*sz-sx*sy*cz),float(cx*cy*cz+sx*sy*sz)};
}
Vec3 rotationDegrees(const Quat& q,math::AffineTransformFields& reference) {
    if(const auto fields=math::affineTransformFields(math::rotation(q),&reference)){reference=*fields;return fields->rotationRadians*(180./std::numbers::pi);}return {};
}
// Refit only newly added bodies; changing the physical subset preserves every
// surviving body's authored dimensions, masses and joint settings.
void setPhysicalNodes(RagdollAsset& asset,const ModelAsset& model,const std::vector<std::uint32_t>& nodes) {
    if(nodes.empty()){asset.bodies.clear();return;}
    auto generated=asset;generateRagdollBodies(generated,model,nodes);
    for(auto& body:generated.bodies)for(const auto& existing:asset.bodies)if(body.node==existing.node){const auto parent=body.parent;body=existing;body.parent=parent;break;}
    asset=std::move(generated);
}
}
void RagdollEditor::bones(AssetLibrary& assets,const std::shared_ptr<const ModelAsset>& source) {
    ImGui::SeparatorText("Bones");
    ImGui::TextDisabled("%zu physical bodies",document_.definition().bodies.size());
    if(!source) {
        ImGui::TextWrapped("%s",assets.modelLoading(document_.definition().sourceModel.id)?"Preparing skeleton...":"Source model is unavailable.");
        return;
    }
    try {validateRagdollSource(document_.definition(),*source);}
    catch(const std::exception& error){ImGui::TextWrapped("%s",error.what());return;}
    const auto& model=*source;const auto& skin=model.skins[document_.definition().skin];
    const std::unordered_set<std::uint32_t> joints(skin.joints.begin(),skin.joints.end());
    const float halfWidth=std::max(1.f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*.5f);
    if(ImGui::Button("Select All",{halfWidth,0}))selectedNodes_=skin.joints;
    ImGui::SameLine();if(ImGui::Button("Clear Selection",{halfWidth,0}))selectedNodes_.clear();
    if(ImGui::Button(selectedNodes_.empty()?"Generate All Bodies":"Generate Selected Bodies",{ImGui::GetContentRegionAvail().x,0})) {
        finishInteraction();document_.edit("Generate ragdoll bodies",[&](auto& asset){generateRagdollBodies(asset,model,selectedNodes_);});selectedBody_=-1;resetSimulation();
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Replace the current body setup with fitted starting shapes");
    const bool canAdd=selectedNode_>=0&&joints.contains(std::uint32_t(selectedNode_))&&selectedBody_<0;
    ImGui::BeginDisabled(!canAdd);
    if(ImGui::Button("Add Body",{halfWidth,0})) {
        finishInteraction();std::vector<std::uint32_t> nodes;for(const auto& body:document_.definition().bodies)nodes.push_back(body.node);nodes.push_back(std::uint32_t(selectedNode_));
        document_.edit("Add ragdoll body",[&](auto& asset){setPhysicalNodes(asset,model,nodes);});
        for(std::size_t i=0;i<document_.definition().bodies.size();++i)if(document_.definition().bodies[i].node==std::uint32_t(selectedNode_))selectedBody_=int(i);
        resetSimulation();
    }
    ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(selectedBody_<0||std::size_t(selectedBody_)>=document_.definition().bodies.size());
    if(ImGui::Button("Remove Body",{halfWidth,0})) {
        finishInteraction();std::vector<std::uint32_t> nodes;for(std::size_t i=0;i<document_.definition().bodies.size();++i)if(int(i)!=selectedBody_)nodes.push_back(document_.definition().bodies[i].node);
        document_.edit("Remove ragdoll body",[&](auto& asset){setPhysicalNodes(asset,model,nodes);});selectedBody_=-1;resetSimulation();
    }
    ImGui::EndDisabled();
    ImGui::BeginChild("Bone hierarchy",{0,0},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing,ImGui::GetFontSize()*.65f);
    const std::function<void(std::uint32_t)> node=[&](std::uint32_t index) {
        const auto& value=model.nodes[index];ImGui::PushID(int(index));
        const bool joint=joints.contains(index);bool included=std::find(selectedNodes_.begin(),selectedNodes_.end(),index)!=selectedNodes_.end();
        if(joint) {
            if(ImGui::Checkbox("##generate",&included)){if(included)selectedNodes_.push_back(index);else std::erase(selectedNodes_,index);}
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Include when generating bodies");
            ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
        }
        const auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth|
            (value.children.empty()?ImGuiTreeNodeFlags_Leaf:0)|(selectedNode_==int(index)?ImGuiTreeNodeFlags_Selected:0);
        const auto label=nodeLabel(model,index);const bool expanded=ImGui::TreeNodeEx("Bone",flags,"%s",label.c_str());
        if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen()) {
            finishInteraction();selectedNode_=int(index);selectedBody_=-1;
            for(std::size_t i=0;i<document_.definition().bodies.size();++i)if(document_.definition().bodies[i].node==index)selectedBody_=int(i);
        }
        if(expanded){for(const auto child:value.children)node(child);ImGui::TreePop();}ImGui::PopID();
    };
    for(const auto root:model.roots)node(root);
    ImGui::PopStyleVar();ImGui::EndChild();
}
void RagdollEditor::properties(const std::shared_ptr<const ModelAsset>& source,bool canceled) {
    ImGui::SeparatorText("Properties");
    ImGui::PushItemWidth(std::max(70.f,ImGui::GetContentRegionAvail().x*.46f));
    struct ItemWidthScope {~ItemWidthScope(){ImGui::PopItemWidth();}} itemWidthScope;
    int section=1;
    const auto field=[&](const char* label,const ItemEdit& item,const RagdollDocument::Mutation& mutation) {
        if(!canceled&&(item.activated||item.changed)&&!document_.transactionActive())if(document_.begin(std::string("Change ")+label))propertySection_=section;
        if(!canceled&&item.changed)document_.update(mutation);
        if(item.deactivated||(item.changed&&!item.active))if(document_.commit())propertySection_=0;
    };
    const auto ordinary=[&](const char* label,bool changed,const RagdollDocument::Mutation& mutation){field(label,{changed,ImGui::IsItemActivated(),ImGui::IsItemDeactivated(),ImGui::IsItemActive()},mutation);};
    if(ImGui::CollapsingHeader("Ragdoll",ImGuiTreeNodeFlags_DefaultOpen)) {
        float gravity=document_.definition().gravityFactor;
        ordinary("gravity",resettableProperty(gravity,1.f,[&]{return ImGui::DragFloat("Gravity Factor",&gravity,.01f,0,10,"%.2f",ImGuiSliderFlags_AlwaysClamp);}),[&](auto& asset){asset.gravityFactor=gravity;});
    }else if(propertySection_==section)finishInteraction();
    if(selectedBody_<0||std::size_t(selectedBody_)>=document_.definition().bodies.size())return;
    if(rotationBody_!=selectedBody_){bodyRotationFields_={};jointRotationFields_={};rotationBody_=selectedBody_;}
    section=2;
    if(ImGui::CollapsingHeader("Body",ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto index=std::size_t(selectedBody_);auto value=document_.definition().bodies[index];
        ImGui::TextWrapped("Bone: %s",source?nodeLabel(*source,value.node).c_str():std::to_string(value.node).c_str());
        if(value.parent>=0&&std::size_t(value.parent)<document_.definition().bodies.size())ImGui::TextWrapped("Parent: %s",source?nodeLabel(*source,document_.definition().bodies[value.parent].node).c_str():std::to_string(value.parent).c_str());else ImGui::TextUnformatted("Parent: None");
        int shape=int(value.shape);const bool changed=ImGui::Combo("Shape",&shape,"Capsule\0Box\0Sphere\0");ordinary("body shape",changed,[&](auto& asset){asset.bodies[index].shape=RagdollShape(shape);});
        auto position=value.position;const auto positionEdit=axisRow("Position (m)",position,.005f);field("body position",positionEdit,[&](auto& asset){asset.bodies[index].position=position;});
        auto rotation=rotationDegrees(value.rotation,bodyRotationFields_);const auto rotationEdit=axisRow("Rotation (degrees)",rotation,.5f);field("body rotation",rotationEdit,[&](auto& asset){asset.bodies[index].rotation=quaternionDegrees(rotation);});
        if(value.shape==RagdollShape::Box){ImGui::Checkbox("Lock proportions",&proportionLock_);auto size=value.size;const auto item=axisRow("Dimensions (m)",size,.005f,.001f,100.f,proportionLock_);field("body dimensions",item,[&](auto& asset){asset.bodies[index].size=size;});}
        else {
            float radius=value.radius;ordinary("body radius",ImGui::DragFloat("Radius (m)",&radius,.005f,.001f,50,"%.3f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){auto& body=asset.bodies[index];body.radius=radius;body.height=std::max(body.height,2*radius);});
            if(value.shape==RagdollShape::Capsule){float height=value.height;ordinary("body height",ImGui::DragFloat("Height (m)",&height,.005f,2*value.radius,100,"%.3f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].height=height;});}
        }
        float mass=value.mass;ordinary("body mass",ImGui::DragFloat("Mass (kg)",&mass,.05f,.001f,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].mass=mass;});
        float friction=value.friction;ordinary("body friction",ImGui::DragFloat("Friction",&friction,.01f,0,2,"%.2f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].friction=friction;});
        float restitution=value.restitution;ordinary("body restitution",ImGui::DragFloat("Restitution",&restitution,.01f,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].restitution=restitution;});
    }else if(propertySection_==section)finishInteraction();
    section=3;const auto index=std::size_t(selectedBody_);const auto body=document_.definition().bodies[index];
    if(body.parent>=0&&ImGui::CollapsingHeader("Joint",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto position=body.joint.position;const auto positionEdit=axisRow("Anchor (m)",position,.005f);field("joint anchor",positionEdit,[&](auto& asset){asset.bodies[index].joint.position=position;});
        auto rotation=rotationDegrees(body.joint.rotation,jointRotationFields_);const auto rotationEdit=axisRow("Joint Rotation (degrees)",rotation,.5f);field("joint rotation",rotationEdit,[&](auto& asset){asset.bodies[index].joint.rotation=quaternionDegrees(rotation);});
        auto swingY=body.joint.swingY;ordinary("joint swing Y",ImGui::DragFloat("Swing Y (degrees)",&swingY,.5f,0,179.9f,"%.1f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].joint.swingY=swingY;});
        auto swingZ=body.joint.swingZ;ordinary("joint swing Z",ImGui::DragFloat("Swing Z (degrees)",&swingZ,.5f,0,179.9f,"%.1f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].joint.swingZ=swingZ;});
        auto minimum=body.joint.twistMin;ordinary("joint twist minimum",ImGui::DragFloat("Twist Minimum (degrees)",&minimum,.5f,-180,body.joint.twistMax,"%.1f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].joint.twistMin=minimum;});
        auto maximum=body.joint.twistMax;ordinary("joint twist maximum",ImGui::DragFloat("Twist Maximum (degrees)",&maximum,.5f,body.joint.twistMin,180,"%.1f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].joint.twistMax=maximum;});
        auto friction=body.joint.friction;ordinary("joint friction torque",ImGui::DragFloat("Friction Torque (Nm)",&friction,.05f,0,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp),[&](auto& asset){asset.bodies[index].joint.friction=friction;});
    }else if(propertySection_==section)finishInteraction();
}
void RagdollEditor::endDrag(){if(simulation_)simulation_->endRagdollDrag();dragging_=false;}
void RagdollEditor::resetSimulation(){endDrag();simulation_.reset();simulating_=false;paused_=false;accumulator_=0;previewWorld_=Mat4{};manualPins_.clear();scopePins_.clear();}
void RagdollEditor::testControls() {
    const auto count=document_.definition().bodies.size();manualPins_.resize(count);
    ImGui::BeginDisabled(simulating_);
    ImGui::Combo("Test Mode",&simulationMode_,"Whole Ragdoll\0Selected Body\0Selected Branch\0");
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Reset before changing the simulation scope");
    ImGui::EndDisabled();
    const bool selected=selectedBody_>=0&&std::size_t(selectedBody_)<count;
    const bool fixed=selected&&std::size_t(selectedBody_)<scopePins_.size()&&scopePins_[std::size_t(selectedBody_)]!=0;
    ImGui::BeginDisabled(!selected||fixed);bool pinned=selected&&(manualPins_[std::size_t(selectedBody_)]!=0||fixed);
    if(ImGui::Checkbox("Pin Selected Body",&pinned)&&selected) {
        const auto index=std::size_t(selectedBody_);
        if(!simulation_||simulation_->setRagdollBodyPinned(1,index,pinned||fixed)){manualPins_[index]=pinned;if(dragging_&&pinned)endDrag();}
        else message_=simulation_->status();
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",fixed?"Reset to change Test Mode; this body is outside the current scope":"Hold this body at its current pose during the test");
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Clear Pins"))for(std::size_t i=0;i<count;++i)if(manualPins_[i]) {
        const bool scopePin=i<scopePins_.size()&&scopePins_[i];
        if(!simulation_||simulation_->setRagdollBodyPinned(1,i,scopePin))manualPins_[i]=0;else message_=simulation_->status();
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Clear manual pins; bodies outside the test scope stay fixed");
}
void RagdollEditor::startSimulation(const std::shared_ptr<const ModelAsset>& source,const ModelPose& pose) {
    const auto manualPins=manualPins_;resetSimulation();manualPins_=manualPins;if(!source)return;
    try {
        finishInteraction();validateRagdollSource(document_.definition(),*source);
        if(document_.definition().bodies.empty())throw std::runtime_error("Add physical bodies before starting simulation.");
        if(simulationMode_!=0&&(selectedBody_<0||std::size_t(selectedBody_)>=document_.definition().bodies.size()))throw std::runtime_error("Select a physical body before starting this test mode.");
        EntityRecord floor;floor.id=2;floor.name="Ragdoll preview floor";floor.local=math::translation({0,-.5f,0});floor.collider.emplace();floor.collider->size={100,1,100};floor_.apply({floor},{});
        simulation_=std::make_unique<PhysicsWorld>();simulation_->reconcileStatics(floor_);
        previewWorld_=math::translation({0,dropHeight_-(pose.bounds.valid?std::min(0.f,pose.bounds.minimum[1]):0.f),0});
        if(!simulation_->createRagdoll(1,std::make_shared<const RagdollAsset>(document_.definition()),source,pose,previewWorld_))throw std::runtime_error(simulation_->status());
        const auto mask=ragdollSimulationMask(document_.definition().bodies,std::size_t(selectedBody_),static_cast<RagdollSimulationScope>(simulationMode_));
        scopePins_=ragdollScopePinMask(document_.definition().bodies,mask,false);manualPins_.resize(scopePins_.size());
        for(std::size_t i=0;i<scopePins_.size();++i)if(scopePins_[i]||manualPins_[i])
            if(!simulation_->setRagdollBodyPinned(1,i,true))throw std::runtime_error(simulation_->status());
        simulationRevision_=document_.previewRevision();testRevision_=simulationRevision_;simulating_=true;message_.clear();
    }catch(const std::exception& error){resetSimulation();message_=error.what();}
}

void RagdollEditor::overlays(const ModelAsset& source,const ModelPose& pose,const ViewportRegion& region) {
    std::vector<Mat4> reference;
    if(showJoints_&&selectedBody_>=0&&std::size_t(selectedBody_)<document_.definition().bodies.size())
        try{validateRagdollSource(document_.definition(),source);reference=ragdollReferenceTransforms(source);}catch(...){ }
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect({region.x,region.y},{region.x+region.width,region.y+region.height},true);
    const auto projection=settings_.camera.projectionMatrix(region.width/region.height,ClipDepth::ZeroToOne)*settings_.camera.viewMatrix();
    const auto pixel=[&](const Vec4& p){return ImVec2{region.x+(p[0]/p[3]+1)*.5f*region.width,region.y+(1-p[1]/p[3])*.5f*region.height};};
    const auto line=[&](const Vec3& first,const Vec3& second,ImU32 color,float thickness=1.f) {
        auto a=math::transformHomogeneous(projection,{first[0],first[1],first[2],1});auto b=math::transformHomogeneous(projection,{second[0],second[1],second[2],1});
        if(!math::finite(a)||!math::finite(b))return;
        float low=0,high=1;const auto distance=[](const Vec4& p,unsigned plane){switch(plane){case 0:return p[3]+p[0];case 1:return p[3]-p[0];case 2:return p[3]+p[1];case 3:return p[3]-p[1];case 4:return p[2];default:return p[3]-p[2];}};
        for(unsigned plane=0;plane<6;++plane){const auto da=distance(a,plane),db=distance(b,plane);if(da<0&&db<0)return;if(da<0)low=std::max(low,da/(da-db));if(db<0)high=std::min(high,da/(da-db));}if(low>high)return;
        auto clippedA=a,clippedB=b;for(unsigned i=0;i<4;++i){clippedA[i]=a[i]+(b[i]-a[i])*low;clippedB[i]=a[i]+(b[i]-a[i])*high;}if(clippedA[3]<=1e-6f||clippedB[3]<=1e-6f)return;draw->AddLine(pixel(clippedA),pixel(clippedB),color,thickness);
    };
    const auto point=[&](const Vec3& p,ImU32 color,float radius){const auto clip=math::transformHomogeneous(projection,{p[0],p[1],p[2],1});if(clip[3]>1e-6f&&clip[2]>=0&&clip[2]<=clip[3]&&std::abs(clip[0])<=clip[3]&&std::abs(clip[1])<=clip[3])draw->AddCircleFilled(pixel(clip),radius,color);};
    if(showBones_)for(std::size_t i=0;i<source.nodes.size()&&i<pose.worldTransforms.size();++i) {
        const auto parent=source.nodes[i].parent;const auto world=previewWorld_*pose.worldTransforms[i];const Vec3 p{world[12],world[13],world[14]};
        const auto color=selectedNode_==int(i)?IM_COL32(255,195,50,255):IM_COL32(210,210,210,200);
        if(parent>=0&&std::size_t(parent)<pose.worldTransforms.size()){const auto other=previewWorld_*pose.worldTransforms[parent];line(p,{other[12],other[13],other[14]},color,1.5f);}point(p,color,selectedNode_==int(i)?4.f:2.f);
    }
    const auto simulated=simulation_?simulation_->ragdollBodyTransforms(1,float(accumulator_*60)):std::vector<Mat4>{};
    const auto& definition=document_.definition();
    for(std::size_t index=0;index<definition.bodies.size();++index) {
        const auto& body=definition.bodies[index];if(body.node>=pose.worldTransforms.size())continue;
        const auto bone=index<simulated.size()?simulated[index]:previewWorld_*math::orthonormalFrame(pose.worldTransforms[body.node]);
        const auto shape=bone*math::translation(body.position)*math::rotation(body.rotation);
        const bool manual=index<manualPins_.size()&&manualPins_[index],fixed=index<scopePins_.size()&&scopePins_[index];
        const auto color=manual?IM_COL32(232,105,174,255):fixed?IM_COL32(125,145,165,230):int(index)==selectedBody_?IM_COL32(255,195,50,255):IM_COL32(57,175,221,230);const float thick=int(index)==selectedBody_?2.f:1.f;
        const auto edge=[&](const Vec3& a,const Vec3& b){line(math::transformPoint(shape,a),math::transformPoint(shape,b),color,thick);};
        const auto ring=[&](unsigned axis,float along,float radius){Vec3 previous{};for(unsigned i=0;i<=32;++i){const double angle=i*2*std::numbers::pi/32;Vec3 p;p[axis]=along;p[(axis+1)%3]=float(std::cos(angle)*radius);p[(axis+2)%3]=float(std::sin(angle)*radius);if(i)edge(previous,p);previous=p;}};
        if(showShapes_) {
            if(body.shape==RagdollShape::Box){for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis)if(!(corner&(1u<<axis))){Vec3 a,b;for(unsigned j=0;j<3;++j)a[j]=b[j]=((corner&(1u<<j))?.5f:-.5f)*body.size[j];b[axis]=-a[axis];edge(a,b);}}
            else if(body.shape==RagdollShape::Sphere){for(unsigned axis=0;axis<3;++axis)ring(axis,0,body.radius);}
            else {
                const float half=std::max(0.f,body.height*.5f-body.radius);ring(1,half,body.radius);ring(1,-half,body.radius);
                for(unsigned side=0;side<4;++side){const double angle=side*std::numbers::pi/2;const float x=float(std::cos(angle)*body.radius),z=float(std::sin(angle)*body.radius);edge({x,-half,z},{x,half,z});}
                for(unsigned axis:{0u,2u})for(int sign:{-1,1}){Vec3 previous{};for(unsigned i=0;i<=16;++i){const double angle=i*std::numbers::pi/16;Vec3 p;p[1]=float(sign*(half+body.radius*std::sin(angle)));p[axis]=float(body.radius*std::cos(angle));if(i)edge(previous,p);previous=p;}}
            }
        }
        if(showJoints_&&body.parent>=0&&int(index)==selectedBody_&&body.node<reference.size()) {
            const auto parentIndex=std::size_t(body.parent);const auto parentNode=definition.bodies[parentIndex].node;
            if(parentNode>=reference.size()||parentNode>=pose.worldTransforms.size())continue;
            const auto parentCurrent=parentIndex<simulated.size()?simulated[parentIndex]:previewWorld_*math::orthonormalFrame(pose.worldTransforms[parentNode]);
            const auto inverseParent=math::inverse(reference[parentNode]);if(!inverseParent)continue;
            // Limits belong to the parent's neutral constraint frame. The
            // child may rotate freely inside that frame during the test.
            const auto joint=parentCurrent*(*inverseParent)*reference[body.node]*math::translation(body.joint.position)*math::rotation(body.joint.rotation);
            const auto origin=math::transformPoint(joint,{});const float length=std::clamp(body.height*.5f,.08f,.5f);
            constexpr std::array<ImU32,3> axisColors{IM_COL32(230,65,65,255),IM_COL32(65,210,65,255),IM_COL32(65,110,240,255)};
            for(unsigned axis=0;axis<3;++axis){Vec3 tip;tip[axis]=length;line(origin,math::transformPoint(joint,tip),axisColors[axis],2.f);}
            // Jolt bounds swing on an ellipse in quaternion Y/Z, not Euler
            // angles. Rotate the twist axis by each boundary quaternion.
            const double swingY=std::sin(body.joint.swingY*std::numbers::pi/360),swingZ=std::sin(body.joint.swingZ*std::numbers::pi/360);
            Vec3 previous{};for(unsigned i=0;i<=48;++i){const double angle=i*2*std::numbers::pi/48;const auto qy=swingY*std::cos(angle),qz=swingZ*std::sin(angle),qw=std::sqrt(std::max(0.,1-qy*qy-qz*qz));const auto tip=math::transformPoint(joint,{float((1-2*(qy*qy+qz*qz))*length),float(2*qz*qw*length),float(-2*qy*qw*length)});if(i)line(previous,tip,IM_COL32(180,180,60,230));if(i%8==0)line(origin,tip,IM_COL32(180,180,60,150));previous=tip;}
            for(unsigned i=0;i<=32;++i){const double angle=(body.joint.twistMin+(body.joint.twistMax-body.joint.twistMin)*i/32.)*std::numbers::pi/180;const auto tip=math::transformPoint(joint,{0,float(std::cos(angle)*length*.7),float(std::sin(angle)*length*.7)});if(i)line(previous,tip,axisColors[0],2.f);previous=tip;}
        }
    }
    if(dragging_){point(dragPoint_,IM_COL32(255,195,50,255),5);}
    draw->PopClipRect();
}
void RagdollEditor::preparePreview(const std::shared_ptr<const ModelAsset>& source) {
    if(source!=boundSource_){previewStatus_.clear();gizmo_.cancel(document_);resetSimulation();simulationMode_=0;boundSource_=source;framePending_=true;}
    if(simulating_&&simulationRevision_!=document_.previewRevision())resetSimulation();
    if(testRevision_!=document_.previewRevision()){manualPins_.assign(document_.definition().bodies.size(),0);scopePins_.clear();simulationMode_=0;testRevision_=document_.previewRevision();}
    if(source&&startingClip_>=int(source->animations.size()))startingClip_=-1;
}
std::shared_ptr<const ModelPose> RagdollEditor::startingPose(const std::shared_ptr<const ModelAsset>& source) {
    if(source)try {
        return std::make_shared<const ModelPose>(evaluateModelPose(*source,startingClip_>=0?std::optional<std::size_t>{std::size_t(startingClip_)}:std::nullopt,seconds_,false));
    }catch(const std::exception& error){previewStatus_=error.what();}
    return {};
}
void RagdollEditor::simulationControls(const std::shared_ptr<const ModelAsset>& source) {
    preparePreview(source);
    ImGui::SeparatorText("Simulation");
    ImGui::PushItemWidth(std::max(70.f,ImGui::GetContentRegionAvail().x*.5f));
    bool compatible=false;if(source)try{validateRagdollSource(document_.definition(),*source);compatible=true;}catch(...){ }
    ImGui::BeginDisabled(!compatible||document_.definition().bodies.empty());
    if(ImGui::Button(simulating_?(paused_?"Resume":"Pause"):"Simulate")) {
        if(simulating_){paused_=!paused_;if(paused_)endDrag();}
        else if(const auto pose=startingPose(source))startSimulation(source,*pose);
    }
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Reset")){finishInteraction();resetSimulation();}
    ImGui::SameLine();if(ImGui::Button("Frame")){finishInteraction();framePending_=true;}
    testControls();
    ImGui::BeginDisabled(simulating_||gizmo_.active());
    if(ImGui::BeginCombo("Starting Pose",source&&startingClip_>=0?source->animations[startingClip_].name.c_str():"Reference Pose")) {
        if(ImGui::Selectable("Reference Pose",startingClip_<0)){startingClip_=-1;seconds_=0;}
        if(source)for(std::size_t i=0;i<source->animations.size();++i){ImGui::PushID(int(i));const auto& clip=source->animations[i];const auto label=clip.name.empty()?"Animation "+std::to_string(i):clip.name;if(ImGui::Selectable(label.c_str(),startingClip_==int(i))){startingClip_=int(i);seconds_=clip.start;}ImGui::PopID();}ImGui::EndCombo();
    }
    if(source&&startingClip_>=0){const auto& clip=source->animations[startingClip_];float time=float(seconds_);if(ImGui::SliderFloat("Time (s)",&time,clip.start,clip.end,"%.3f"))seconds_=time;}
    ImGui::DragFloat("Drop Height (m)",&dropHeight_,.05f,0,100,"%.2f",ImGuiSliderFlags_AlwaysClamp);ImGui::EndDisabled();
    ImGui::SeparatorText("Display");
    ImGui::Checkbox("Bones",&showBones_);ImGui::SameLine();ImGui::Checkbox("Shapes",&showShapes_);ImGui::SameLine();ImGui::Checkbox("Joints",&showJoints_);
    ImGui::PopItemWidth();
}
void RagdollEditor::preview(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,EditorResources& prepared,double dt) {
    // Sidebar edits and test controls run first, so this image always consumes
    // the current document and pose rather than the previous frame's controls.
    const auto source=assets.findModel(document_.definition().sourceModel.id);
    preparePreview(source);previewStatus_.clear();
    const auto startingPose=this->startingPose(source);
    const auto available=ImGui::GetContentRegionAvail();if(available.x<=0||available.y<=0){gizmo_.cancel(document_);endDrag();return;}
    const auto origin=ImGui::GetCursorScreenPos();const ViewportRegion region{origin.x,origin.y,available.x,available.y};
    const auto scale=ImGui::GetWindowViewport()->FramebufferScale;
    const auto width=std::uint32_t(std::clamp(available.x*scale.x,1.f,16384.f)),height=std::uint32_t(std::clamp(available.y*scale.y,1.f,16384.f));
    bool imageDrawn=false;
    try {
        renderer.resizeRagdollPreview(width,height);
        if(!environment_) {
            ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Selected;const auto root=assets.projectRoot();
            environment_=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{std::string(StudioEnvironmentId)},[root](std::stop_token token){if(token.stop_requested())throw std::runtime_error("Ragdoll preview environment preparation canceled.");auto bake=prepareEnvironmentCpu(root,{});std::size_t bytes=bake->data.diffuse.rgba.size()*sizeof(float)+bake->data.brdf.size()*sizeof(float);for(const auto& level:bake->data.specular)bytes+=level.rgba.size()*sizeof(float);return ResourceLoadResult<PreparedEnvironment>{bake,bytes,bake->key,{}};},std::move(options));
        }
        const auto environment=environment_.snapshot();if(environment&&environment.revision()!=environmentRevision_)if(renderer.publishRagdollPreviewEnvironment(*environment))environmentRevision_=environment.revision();
        if(framePending_&&startingPose&&startingPose->bounds.valid){settings_.camera=Camera{};auto bounds=startingPose->bounds;if(simulating_)bounds=math::transformBounds(bounds,previewWorld_);settings_.camera.frameBounds(bounds.minimum,bounds.maximum,float(width)/height);framePending_=false;}
        const auto model=source?prepared.prepare(source->id,previewWorld_,settings_.camera,height,true,ResourcePriority::Selected,float(width)/height):nullptr;
        const auto texture=renderer.ragdollPreviewTexture();
        const bool imageReady=bgfx::isValid(texture)&&texture.idx==displayedTarget_&&model&&model->ready&&startingPose&&environmentRevision_;
        const auto interaction=drawRagdollPreviewImage(imageReady?ImTextureRef(ImGuiBgfx::textureId(texture)):ImTextureRef{},available,imageReady);
        const bool activated=ImGui::IsItemActivated();
        const int mouseOwner=interaction.active?ImGui::GetCurrentContext()->ActiveIdMouseButton:-1;
        imageDrawn=true;displayedTarget_=texture.idx;
        if(activated&&mouseOwner==ImGuiMouseButton_Left&&!gizmo_.active()&&document_.transactionActive()) {
            finishInteraction(true);suppressFinalUpdate_=false;
        }
        const auto& io=ImGui::GetIO();
        std::vector<Mat4> boneFrames;bool compatible=false;
        if(source&&startingPose)try {validateRagdollSource(document_.definition(),*source);compatible=true;boneFrames.reserve(startingPose->worldTransforms.size());for(const auto& matrix:startingPose->worldTransforms)boneFrames.push_back(previewWorld_*math::orthonormalFrame(matrix));}catch(...){ }
        struct DrawChannels {ImDrawList* list;explicit DrawChannels(ImDrawList* value):list(value){list->ChannelsSplit(2);}~DrawChannels(){list->ChannelsMerge();}} channels(ImGui::GetWindowDrawList());
        channels.list->ChannelsSetCurrent(1);bool overGizmo=false;
        if(!simulating_&&compatible&&selectedBody_>=0&&std::size_t(selectedBody_)<document_.definition().bodies.size()) {
            const auto node=document_.definition().bodies[std::size_t(selectedBody_)].node;
            if(node<boneFrames.size())overGizmo=gizmo_.draw(document_,std::size_t(selectedBody_),boneFrames[node],settings_.camera,region,static_cast<RagdollAuthoringOperation>(gizmoOperation_),proportionLock_,!imageReady||interaction.blocked||(!interaction.hovered&&!gizmo_.active()));
            if(!gizmo_.diagnostic().empty())previewStatus_=gizmo_.diagnostic();
        }else if(gizmo_.active())gizmo_.cancel(document_);
        channels.list->ChannelsSetCurrent(0);
        const bool ownsInput=!interaction.blocked&&!overGizmo&&!gizmo_.active();
        const auto ray=settings_.camera.screenRay(io.MousePos.x,io.MousePos.y,region);
        if(ownsInput&&interaction.hovered) {
            if(activated&&mouseOwner==ImGuiMouseButton_Middle){endDrag();ImGui::SetWindowFocus();previewGesture_=io.KeyShift?2:1;}
            if(io.MouseWheel)settings_.camera.zoom(io.MouseWheel);
            if(activated&&mouseOwner==ImGuiMouseButton_Left&&ray&&previewGesture_==0) {
                finishInteraction(true);ImGui::SetWindowFocus();
                if(imageReady&&simulation_) {if(const auto hit=simulation_->pickRagdoll(*ray,settings_.camera.farPlane)){selectedBody_=int(hit->bodyIndex);selectedNode_=int(document_.definition().bodies[hit->bodyIndex].node);if(!paused_&&simulation_->beginRagdollDrag(hit->entity,hit->bodyIndex,hit->position)){dragging_=true;dragPoint_=hit->position;dragNormal_=settings_.camera.forward();}}}
                else if(imageReady&&compatible&&ray)if(const auto hit=pickRagdollBodies(document_.definition().bodies,boneFrames,*ray,settings_.camera.farPlane)){selectedBody_=int(hit->body);selectedNode_=int(document_.definition().bodies[hit->body].node);}
            }
        }
        const bool focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if(!ownsInput||!focused||mouseOwner!=ImGuiMouseButton_Left||!ImGui::IsMouseDown(ImGuiMouseButton_Left)||paused_)endDrag();
        if(dragging_&&ray)if(const auto target=math::intersectPlane(*ray,dragPoint_,dragNormal_))simulation_->updateRagdollDrag(*target);
        if(!ownsInput||!focused||mouseOwner!=ImGuiMouseButton_Middle||!ImGui::IsMouseDown(ImGuiMouseButton_Middle))previewGesture_=0;
        if(previewGesture_==1)settings_.camera.orbit(io.MouseDelta.x,io.MouseDelta.y,settings_.camera.target);
        if(previewGesture_==2)settings_.camera.pan(io.MouseDelta.x,io.MouseDelta.y,available.x,available.y);
        if(simulating_&&!paused_&&simulation_&&std::isfinite(dt)&&dt>0){accumulator_+=std::min(dt,.1);while(accumulator_>=1./60){simulation_->step(floor_,NoEntity,{},1.f/60);accumulator_-=1./60;}}
        auto pose=simulating_&&simulation_?simulation_->sampleRagdollPose(1,float(accumulator_*60)):startingPose;
        if(!source)previewStatus_=assets.modelLoading(document_.definition().sourceModel.id)?"Preparing ragdoll model...":"Ragdoll source model is unavailable.";
        else if(!model||!model->ready)previewStatus_="Preparing ragdoll preview...";else if(!environmentRevision_)previewStatus_="Preparing preview environment...";
        if(environment_.state()==ResourceState::Failed)previewStatus_=environment_.diagnostic();
        if(model&&model->ready&&pose&&environmentRevision_){auto pins=std::make_shared<PreviewPins>();pins->model=model;pins->pose=pose;pins->environment=environment.shared();std::array<RenderModelInstance,1> instances{{{0,{model->asset,previewWorld_},&model->textures,model->revision,false,false,&model->materials}}};instances.front().pose=pose;instances.front().castShadows=false;RenderScene scene{settings_,{},nullptr,nullptr,instances,{}, {},true,pins};renderer.renderRagdollPreview(scene);overlays(*source,*pose,region);}
    }catch(const std::exception& error){previewStatus_=error.what();gizmo_.cancel(document_);endDrag();if(!imageDrawn)drawRagdollPreviewImage(ImTextureRef{},available,false);}
}
void RagdollEditor::draw(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,EditorResources& prepared,double dt) {
    if(pending_==Pending::Exit&&!document_.dirty())perform(assets);
    if(isOpen()&&assets.references().deleted(document_.assetId())){gizmo_.cancel(document_);resetSimulation();document_.close();assets.protectRagdollDocument({});assets.setRagdollDocumentReferences({},{});message_="The open Ragdoll asset was deleted.";}
    if(isOpen()&&externalChanged_&&!document_.transactionActive()&&pending_==Pending::None){externalChanged_=false;if(document_.externalFileChanged()){if(document_.dirty())externalPopup_=true;else {resetSimulation();if(!document_.load(document_.assetId()))message_=document_.status();}}}
    if(!isOpen()){gizmo_.cancel(document_);resetSimulation();renderer.releaseRagdollPreview();displayedTarget_=UINT16_MAX;environment_={};environmentRevision_=0;boundSource_.reset();modals(assets);return;}
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowDockID(0,ImGuiCond_Always);ImGui::SetNextWindowSize({1240,800},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSizeConstraints({840,480},{FLT_MAX,FLT_MAX});
    bool open=true;const auto title="Ragdoll Editor - "+document_.definition().name+(document_.dirty()?" *":"")+"###RagdollEditor";
    const bool contents=ImGui::Begin(title.c_str(),&open,DocumentEditorWindowFlags);if(!open)request(Pending::Close,{},&assets);
    if(contents&&isOpen()) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction();},[&]{return DocumentEditorMenuState{canSave(),canSaveAs(),canUndo(),canRedo(),document_.undoLabel(),document_.redoLabel()};});
        if(action==DocumentEditorAction::Save)save(assets);if(action==DocumentEditorAction::SaveAs)save(assets,true);if(action==DocumentEditorAction::Close)request(Pending::Close,{},&assets);if(action==DocumentEditorAction::Undo)undo();if(action==DocumentEditorAction::Redo)redo();
        if(isOpen()) {
            const bool canceled=suppressFinalUpdate_||(document_.transactionActive()&&(ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::GetIO().AppFocusLost));if(canceled&&document_.transactionActive())cancelInteraction();suppressFinalUpdate_=false;
            const auto source=assets.findModel(document_.definition().sourceModel.id);
            preparePreview(source);
            const auto& keyboard=ImGui::GetIO();
            if(!simulating_&&!gizmo_.active()&&!keyboard.WantTextInput&&!ImGui::IsAnyItemActive()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)&&ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                if(ImGui::IsKeyPressed(ImGuiKey_T,false))gizmoOperation_=0;
                if(ImGui::IsKeyPressed(ImGuiKey_R,false))gizmoOperation_=1;
                if(ImGui::IsKeyPressed(ImGuiKey_E,false))gizmoOperation_=2;
            }
            if(!drawRagdollWorkspace(simulationHeight_,
                [&]{bones(assets,source);},
                [&]{
                    simulationControls(source);
                    if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
                    if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
                    if(!previewStatus_.empty())ImGui::TextWrapped("%s",previewStatus_.c_str());
                },
                [&]{properties(source,canceled);},
                [&]{preview(assets,renderer,resources,prepared,dt);})) {gizmo_.cancel(document_);endDrag();}
        }
    }else {finishInteraction();endDrag();previewGesture_=0;}
    ImGui::End();publish(assets);modals(assets);
}
}
