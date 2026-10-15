#include "QuaderInspector.h"
#include "ScenePanels.h"
#include "quader/QuaderBridge.h"
#include "scene/SceneDocument.h"
#include "resources/Paths.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_keyboard.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace crimson {
namespace {
struct DialogResult {std::mutex mutex;std::string path;bool done{};};
struct Draft {
    QuaderLink committed,requested;
    QuaderProjectCatalog catalog;
    std::array<char,32769> path{};
    std::string error;
    std::shared_ptr<DialogResult> dialog;
    bool initialized{};
};
void inspect(Draft& draft) {
    draft.catalog={};draft.error.clear();draft.requested.projectPath=draft.path.data();
    if(draft.requested.projectPath.empty())return;
    try {
        draft.catalog=inspectQuaderProject(pathFromUtf8(draft.requested.projectPath));
        draft.requested.projectId=draft.catalog.id;
        if(std::none_of(draft.catalog.scenes.begin(),draft.catalog.scenes.end(),[&](const auto& scene){return scene.id==draft.requested.sceneId;}))
            draft.requested.sceneId=draft.catalog.defaultScene.empty() && !draft.catalog.scenes.empty()?draft.catalog.scenes.front().id:draft.catalog.defaultScene;
    } catch(const std::exception& error){draft.error=error.what();}
}
}
void drawQuaderInspector(SceneDocument& document,const EntityRecord& record,QuaderBridge& bridge) {
    static std::unordered_map<EntityId,Draft> drafts;
    static const SceneDocument* current{};static std::uint64_t generation{};
    if(current!=&document || generation!=document.documentGeneration()){drafts.clear();current=&document;generation=document.documentGeneration();}
    auto& draft=drafts[record.id];
    if(!draft.initialized || draft.committed!=*record.quader) {
        draft.committed=*record.quader;draft.requested=*record.quader;draft.initialized=true;
        std::snprintf(draft.path.data(),draft.path.size(),"%s",record.quader->projectPath.c_str());inspect(draft);
    }
    if(draft.dialog) {
        std::lock_guard lock(draft.dialog->mutex);
        if(draft.dialog->done) {
            if(!draft.dialog->path.empty()) {
                std::snprintf(draft.path.data(),draft.path.size(),"%s",draft.dialog->path.c_str());inspect(draft);
            }
        }
    }
    if(draft.dialog) {bool done;{std::lock_guard lock(draft.dialog->mutex);done=draft.dialog->done;}if(done)draft.dialog.reset();}
    ImGui::BeginDisabled(document.transient());
    ImGui::SetNextItemWidth(std::max(80.f,ImGui::GetContentRegionAvail().x-ImGui::GetFrameHeight()*4));
    ImGui::InputText("##Quader project",draft.path.data(),draft.path.size());
    if(ImGui::IsItemDeactivatedAfterEdit())inspect(draft);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Quader project (.qproj)");
    ImGui::SameLine();
    if(ImGui::Button("Browse") && !draft.dialog) {
        draft.dialog=std::make_shared<DialogResult>();
        auto* payload=new std::shared_ptr<DialogResult>(draft.dialog);
        static const SDL_DialogFileFilter filters[]={{"Quader project","qproj"}};
        SDL_ShowOpenFileDialog([](void* data,const char* const* files,int) {
            const std::unique_ptr<std::shared_ptr<DialogResult>> result(static_cast<std::shared_ptr<DialogResult>*>(data));
            std::lock_guard lock((*result)->mutex);if(files && files[0])(*result)->path=files[0];(*result)->done=true;
        },payload,SDL_GetKeyboardFocus(),filters,1,draft.requested.projectPath.empty()?nullptr:draft.requested.projectPath.c_str(),false);
    }
    ImGui::SameLine();
    if(ImGui::SmallButton("Clear")) {
        if(document.transactionActive())finishScenePanelEdit(document);
        auto changed=record;changed.quader=QuaderLink{};document.updateEntity(record.id,changed,"Clear Quader source");bridge.cancel(record.id);
        ImGui::EndDisabled();return;
    }
    const auto selected=std::find_if(draft.catalog.scenes.begin(),draft.catalog.scenes.end(),[&](const auto& scene){return scene.id==draft.requested.sceneId;});
    if(ImGui::BeginCombo("Scene",selected==draft.catalog.scenes.end()?"None":selected->path.c_str())) {
        for(const auto& scene:draft.catalog.scenes)if(ImGui::Selectable(scene.path.c_str(),scene.id==draft.requested.sceneId))draft.requested.sceneId=scene.id;
        ImGui::EndCombo();
    }
    ImGui::InputFloat("Import Scale",&draft.requested.importScale,.1f,1.f,"%.4f");
    const bool scaleEdited=ImGui::IsItemDeactivatedAfterEdit();
    auto linked=*record.quader;
    const auto updatePolicy=[&](const char* label) {
        if(document.transactionActive() && !finishScenePanelEdit(document))return;
        auto changed=record;changed.quader=linked;
        if(document.updateEntity(record.id,changed,label)) {
            draft.requested.autoSync=linked.autoSync;
            draft.requested.refreshMaterials=linked.refreshMaterials;
            draft.requested.syncLights=linked.syncLights;
            draft.committed=linked;
        }
    };
    if(ImGui::Checkbox("Auto sync on save",&linked.autoSync)) {
        updatePolicy("Toggle Quader auto sync");
    }
    if(ImGui::Checkbox("Sync materials",&linked.refreshMaterials)) {
        updatePolicy("Toggle Quader material sync");
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("On: overwrite materials and textures from Quader on reimport. Off: import missing materials and keep existing Crimson edits. Scene Undo does not restore overwritten materials.");
    if(ImGui::Checkbox("Sync lights",&linked.syncLights)) {
        updatePolicy("Toggle Quader light sync");
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("On: import Quader lights. Off: exclude Quader lights and remove previously imported Quader lights on the next reimport.");
    const bool valid=draft.error.empty() && !draft.requested.projectPath.empty() && !draft.requested.sceneId.empty();
    ImGui::BeginDisabled(!valid);
    const bool import=ImGui::Button(bridge.busy(record.id)?"Import again":"Import");
    ImGui::EndDisabled();
    if(valid && (import || (scaleEdited && !record.quader->projectId.empty()))) {
        if(document.transactionActive())finishScenePanelEdit(document);
        bridge.requestImport(record.id,draft.requested,document.documentGeneration());
    }
    if(bridge.busy(record.id)){ImGui::SameLine();if(ImGui::Button("Cancel"))bridge.cancel(record.id);}
    if(!draft.error.empty())ImGui::TextWrapped("%s",draft.error.c_str());
    const auto status=bridge.status(record.id);if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
    ImGui::EndDisabled();
}
}
