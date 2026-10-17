#include "GameplayEffectEditor.h"
#include "DocumentEditorMenu.h"
#include "assets/AssetLibrary.h"
#include "GameplayTagControls.h"
#include "resources/Paths.h"
#include <limits>
#include <type_traits>
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>

namespace crimson {
bool GameplayEffectEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen() && window && window->RootWindow->ID==ImHashStr("###GameplayEffect");
}
void GameplayEffectEditor::finishInteraction() {
    if(document_.transactionActive()) {
        if(!document_.commit())return;
        ImGui::ClearActiveID();suppressFinalUpdate_=true;
    }
}
void GameplayEffectEditor::cancelInteraction() {
    if(document_.transactionActive()){document_.cancel();ImGui::ClearActiveID();suppressFinalUpdate_=true;}
}
void GameplayEffectEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    pending_=pending;pendingId_=std::move(id);
    if(document_.isOpen() && document_.dirty()) dirtyPopup_=true;
    else if(assets) perform(*assets);
    else if(pending==Pending::Exit) { pending_=Pending::None;exitReady_=true; }
}
void GameplayEffectEditor::requestExit() { if(!exitReady_) request(Pending::Exit,{},nullptr); }
void GameplayEffectEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(document_.isOpen() && document_.assetId()==id) { focusNext_=true;return; }
    request(Pending::Open,std::move(id),&assets);
}
void GameplayEffectEditor::requestCreate(std::string folder,AssetLibrary& assets) {
    pendingFolder_=std::move(folder);request(Pending::Create,{},&assets);
}
void GameplayEffectEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;
    pending_=Pending::None;
    continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        document_.close();assets.protectGameplayEffectDocument({});assets.refreshGameplayEffects();
        return;
    }
    message_.clear();
    bool loaded=false;
    if(pending==Pending::Open) {
        loaded=document_.load(pendingId_);
        if(!loaded && staged_)if(const auto seed=staged_(pendingId_);seed.definition)loaded=document_.loadSnapshot(*seed.definition);
    }
    if(pending==Pending::Create) {
        loaded=document_.create();
        if(loaded) {
            assets.noteAssetWrite(document_.assetId());assets.refreshGameplayEffects();
            try {
                if(!pendingFolder_.empty()) {
                    const std::array<std::string,1> ids{document_.assetId()};
                    assets.browserCatalog().move(ids,pendingFolder_);assets.noteAssetWrite("assets/browser.json");
                }
                assets.select(document_.assetId());
            } catch(const std::exception& error) { message_=error.what(); }
        }
    }
    if(loaded) {
        assets.protectGameplayEffectDocument(document_.assetId());publishedRevision_=0;
        publish(assets);assets.refreshGameplayEffects();focusNext_=true;externalChanged_=false;
    } else message_=document_.status();
    pendingId_.clear();pendingFolder_.clear();
}
void GameplayEffectEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!document_.isOpen()) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    publish(assets);
    if(saveAs || !document_.editable()) {
        std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;
    }
    if(document_.externalFileChanged()) { externalPopup_=true;message_="The effect changed on disk. Choose Overwrite, Reload, or Save As.";return; }
    if(document_.save()) { assets.noteAssetWrite(document_.assetId());assets.refreshGameplayEffects();message_.clear(); }
    else message_=document_.status();
}
void GameplayEffectEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!document_.isOpen() || !document_.editable()) return;
    try {
        const auto file=resolveAssetPath(document_.projectRoot(),document_.assetId()).lexically_normal();
        for(const auto& path:paths)if(resourcePathKey(path)==resourcePathKey(file))externalChanged_=true;
    } catch(...) { /* A staged migration effect has no file until Save. */ }
}
void GameplayEffectEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_) { ImGui::OpenPopup("Unsaved Gameplay Effect");dirtyPopup_=false; }
    if(ImGui::BeginPopupModal("Unsaved Gameplay Effect",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Save changes to %s?",document_.definition().name.c_str());
        if(ImGui::Button("Save")) {
            continueAfterSave_=true;
            save(assets);
            if(!document_.dirty()) { perform(assets);continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
            else if(externalPopup_ || savePopup_)ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Discard")) {
            if(document_.discard()) { publish(assets);perform(assets);ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(savePopup_) { ImGui::OpenPopup("Save Gameplay Effect as");savePopup_=false; }
    if(ImGui::BeginPopupModal("Save Gameplay Effect as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save") || enter) && saveName_[0]) {
            const auto previous=document_.assetId();
            const auto previousSaved=document_.savedDefinition();
            if(document_.save(saveName_.data(),true)) {
                assets.protectGameplayEffectDocument(document_.assetId());
                if(!isEngineAssetId(previous)) {
                    auto source=previousSaved;
                    try {source=loadGameplayEffectAsset(assets.projectRoot(),previous);}
                    catch(...) { /* Missing/corrupt sources retain their saved generation. */ }
                    assets.publishGameplayEffect(std::make_shared<const GameplayEffectAsset>(std::move(source)));
                }
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshGameplayEffects();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;
                ImGui::CloseCurrentPopup();
            } else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) { if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_) { ImGui::OpenPopup("Gameplay Effect changed on disk");externalPopup_=false; }
    if(ImGui::BeginPopupModal("Gameplay Effect changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The effect changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save()) {
                assets.noteAssetWrite(document_.assetId());assets.refreshGameplayEffects();message_.clear();
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();
        if(ImGui::Button("Reload and discard edits")) {
            const auto id=document_.assetId();
            if(document_.load(id)) {
                publish(assets);message_.clear();if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Keep editing")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();if(ImGui::Button("Save As")) { save(assets,true);ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
}
void GameplayEffectEditor::publish(AssetLibrary& assets) {
    if(!document_.isOpen())return;
    if(publishedRevision_!=document_.revision() || publishedId_!=document_.assetId()) {
        if(document_.editable())assets.publishGameplayEffect(document_.committed());
        publishedRevision_=document_.revision();publishedId_=document_.assetId();
    }
}
void GameplayEffectEditor::drawProperties(bool canceled) {
    auto value=document_.definition();
    const auto instant=[&]{return value.durationPolicy==EffectDurationPolicy::Instant;};
    const auto commitValue=[&](const char* label) {
        if(canceled)return;finishInteraction();document_.edit(label,[&](auto& asset){asset=value;});
    };
    const auto gesture=[&](bool changed,const char* label) {
        if(canceled)return;
        if((ImGui::IsItemActivated()||changed)&&!document_.transactionActive())document_.begin(label);
        if(changed)document_.update([&](auto& asset){asset=value;});
        if(ImGui::IsItemDeactivated()||(changed&&!ImGui::IsItemActive()))document_.commit();
    };
    const auto number=[&](const char* label,double& field,double minimum=0.,double maximum=std::numeric_limits<double>::max()) {
        const bool changed=ImGui::DragScalar(label,ImGuiDataType_Double,&field,.1f,&minimum,&maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        gesture(changed,label);
    };
    const auto check=[&](const char* label,bool& field){if(ImGui::Checkbox(label,&field))commitValue(label);};
    const auto combo=[&](const char* label,auto& field,const char* choices) {
        auto selected=static_cast<int>(field);
        if(ImGui::Combo(label,&selected,choices)){
            field=static_cast<std::remove_reference_t<decltype(field)>>(selected);
            if constexpr(std::is_same_v<std::remove_reference_t<decltype(field)>,EffectDurationPolicy>)
                if(field==EffectDurationPolicy::HasDuration&&value.duration<=0)value.duration=1;
            commitValue(label);
        }
    };
    ImGui::BeginDisabled(!document_.editable());
    if(ImGui::CollapsingHeader("Duration",ImGuiTreeNodeFlags_DefaultOpen)) {
        combo("Duration Policy",value.durationPolicy,"Instant\0Has Duration\0Infinite\0");
        if(value.durationPolicy==EffectDurationPolicy::HasDuration)number("Duration (s)",value.duration,.001);
        if(!instant()) {
            number("Period (s)",value.period);
            if(value.period>0)check("Execute Periodic on Application",value.executeOnApplication);
        }
    }
    if(ImGui::CollapsingHeader("Modifiers",ImGuiTreeNodeFlags_DefaultOpen)) {
        attributePickers_.resize(value.modifiers.size());std::optional<std::size_t> remove;
        for(std::size_t i=0;i<value.modifiers.size();++i) {
            ImGui::PushID(static_cast<int>(i));auto& modifier=value.modifiers[i];
            ImGui::SeparatorText(("Modifier "+std::to_string(i+1)).c_str());
            if(drawGameplayTagControl("Attribute",modifier.attribute,attributePickers_[i],{GameplayTag{"Stats"},&nativeAttributeCatalog()}))commitValue("Change modifier attribute");
            combo("Operation",modifier.operation,"Add Base\0Multiply Additive\0Divide Additive\0Multiply Compound\0Add Final\0Override\0");
            const auto& registry=nativeEffectCalculations();const auto* calculation=registry.find(modifier.magnitude.calculation);
            const auto magnitudeName=modifier.magnitude.calculation.empty()?std::string("Literal"):calculation?calculation->name:modifier.magnitude.calculation;
            if(ImGui::BeginCombo("Magnitude",magnitudeName.c_str())) {
                if(ImGui::Selectable("Literal",modifier.magnitude.calculation.empty())) {
                    modifier.magnitude.calculation.clear();modifier.magnitude.parameters.clear();commitValue("Use literal magnitude");
                }
                for(const auto& registered:registry.definitions()) {
                    ImGui::PushID(registered.id.c_str());
                    if(ImGui::Selectable(registered.name.c_str(),modifier.magnitude.calculation==registered.id)) {
                        modifier.magnitude.calculation=registered.id;modifier.magnitude.parameters.clear();
                        for(const auto& parameter:registered.parameters)modifier.magnitude.parameters.push_back({parameter.name,parameter.defaultValue});
                        commitValue("Select magnitude calculation");
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            calculation=registry.find(modifier.magnitude.calculation);
            if(modifier.magnitude.calculation.empty())number("Value",modifier.magnitude.literal,-std::numeric_limits<double>::max());
            else if(calculation) {
                ImGui::PushID("Native parameters");
                for(const auto& metadata:calculation->parameters) {
                    ImGui::PushID(metadata.name.c_str());
                    auto found=std::find_if(modifier.magnitude.parameters.begin(),modifier.magnitude.parameters.end(),[&](const auto& parameter){return parameter.name==metadata.name;});
                    auto scalar=found==modifier.magnitude.parameters.end()?metadata.defaultValue:found->value;
                    const auto minimum=metadata.minimum.value_or(-std::numeric_limits<double>::max()),maximum=metadata.maximum.value_or(std::numeric_limits<double>::max());
                    const bool changed=ImGui::DragScalar(metadata.name.c_str(),ImGuiDataType_Double,&scalar,.1f,&minimum,&maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);
                    if(changed) {
                        if(found==modifier.magnitude.parameters.end())modifier.magnitude.parameters.push_back({metadata.name,scalar});else found->value=scalar;
                    }
                    gesture(changed,"Change magnitude parameter");
                    ImGui::PopID();
                }
                ImGui::PopID();
            } else ImGui::TextDisabled("Unavailable calculation: %s",modifier.magnitude.calculation.c_str());
            if(ImGui::SmallButton("Remove"))remove=i;
            ImGui::SameLine();ImGui::BeginDisabled(i==0);
            if(ImGui::SmallButton("Up")){std::swap(value.modifiers[i-1],value.modifiers[i]);commitValue("Move modifier");}
            ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(i+1==value.modifiers.size());
            if(ImGui::SmallButton("Down")){std::swap(value.modifiers[i+1],value.modifiers[i]);commitValue("Move modifier");}
            ImGui::EndDisabled();ImGui::PopID();
        }
        if(remove){value.modifiers.erase(value.modifiers.begin()+static_cast<std::ptrdiff_t>(*remove));commitValue("Remove modifier");}
        if(ImGui::Button("Add Modifier")) {
            value.modifiers.push_back({stats::MovementSpeed,EffectModifierOperation::AddBase,{}});commitValue("Add modifier");
        }
    }
    if(ImGui::CollapsingHeader("Stacking",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(instant()) {
            ImGui::TextDisabled("Unused for Instant effects");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Instant effects execute once and do not create an active stack.");
        }
        ImGui::BeginDisabled(instant());auto& stack=value.stacking;
        combo("Type",stack.type,"None\0Aggregate by Source\0Aggregate by Target\0");
        if(stack.type!=EffectStackingType::None) {
            const std::uint32_t zero=0,maximum=std::numeric_limits<std::uint32_t>::max();
            const bool changed=ImGui::DragScalar("Limit",ImGuiDataType_U32,&stack.limit,1.f,&zero,&maximum,"%u",ImGuiSliderFlags_AlwaysClamp);
            gesture(changed,"Change stack limit");if(ImGui::IsItemHovered())ImGui::SetTooltip("0 allows unlimited stacks.");
            if(value.durationPolicy==EffectDurationPolicy::HasDuration) {
                combo("Duration Refresh",stack.durationRefresh,"Refresh on Successful Application\0Never Refresh\0Extend Duration\0");
                number("Max Duration (s)",stack.maxDuration);if(ImGui::IsItemHovered())ImGui::SetTooltip("0 leaves duration uncapped.");
                combo("Expiration",stack.expiration,"Clear Entire Stack\0Remove Single Stack and Refresh Duration\0Refresh Duration\0");
                if(stack.durationRefresh==EffectDurationRefresh::ExtendDuration && stack.expiration!=EffectStackExpiration::ClearEntireStack) {
                    ImGui::TextDisabled("Extended duration refreshes on expiration");
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("This expiration policy refreshes the duration after the extended duration ends.");
                }
            }
            if(value.period>0)combo("Period Reset",stack.periodReset,"Reset on Successful Application\0Never Reset\0");
            check("Deny Overflow Application",stack.denyOverflowApplication);check("Clear Stack on Overflow",stack.clearStackOnOverflow);
            check("Factor in Stack Count",stack.factorInStackCount);
        }
        ImGui::EndDisabled();
    }
    if(ImGui::CollapsingHeader("Tags",ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr std::array<const char*,6> labels{"Asset Tags","Granted Tags","Source Required","Source Blocked","Target Required","Target Blocked"};
        const std::array<GameplayTagContainer*,6> fields{&value.assetTags,&value.grantedTags,&value.sourceRequired,&value.sourceBlocked,&value.targetRequired,&value.targetBlocked};
        for(std::size_t i=0;i<fields.size();++i)if(drawGameplayTagContainerControl(labels[i],*fields[i],tagPickers_[i]))commitValue("Change effect tags");
    }
    ImGui::EndDisabled();
    // A collapsed section can remove its numeric widget before deactivation is
    // submitted. Finish only this document's gesture once no widget owns input.
    if(document_.transactionActive()&&!ImGui::IsAnyItemActive())finishInteraction();
}
void GameplayEffectEditor::draw(AssetLibrary& assets) {
    if(!document_.isOpen()) {modals(assets);return;}
    if(externalChanged_ && !document_.transactionActive()) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty())externalPopup_=true;
            else if(!document_.load(document_.assetId()))message_=document_.status();
        }
    }
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowSize({460,300},ImGuiCond_FirstUseEver);
    bool open=true;
    const auto title=document_.definition().name+(document_.dirty()?" *":"")+"###GameplayEffect";
    if(ImGui::Begin(title.c_str(),&open,DocumentEditorWindowFlags)) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction();},[&]{return DocumentEditorMenuState{
            document_.editable(),document_.isOpen(),document_.canUndo(),document_.canRedo(),document_.undoLabel(),document_.redoLabel()};});
        if(action==DocumentEditorAction::Save)save(assets);
        if(action==DocumentEditorAction::SaveAs)save(assets,true);
        if(action==DocumentEditorAction::Close)request(Pending::Close,{},&assets);
        if(action==DocumentEditorAction::Undo)undo();
        if(action==DocumentEditorAction::Redo)redo();
        const bool canceled=suppressFinalUpdate_ || (document_.transactionActive() &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape,false) || ImGui::GetIO().AppFocusLost));
        if(canceled)cancelInteraction();suppressFinalUpdate_=false;
        drawProperties(canceled);
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
    } else finishInteraction();
    ImGui::End();
    if(!open)request(Pending::Close,{},&assets);
    publish(assets);modals(assets);
}
} // namespace crimson
