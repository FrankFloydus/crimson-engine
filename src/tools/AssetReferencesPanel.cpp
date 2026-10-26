#include "AssetReferencesPanel.h"
#include "assets/AssetLibrary.h"
#include "scene/SceneDocument.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <unordered_map>

namespace crimson {
#if defined(CC_ASSET_REFERENCE_UI_TEST)
void assetReferenceControlForTest(const char*,unsigned,std::size_t,ImVec2,ImVec2,bool);
#endif
namespace {
std::atomic_uint64_t nextReferenceRequest{1};
std::string lower(std::string text){for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;}
const char* state(const AssetUsage& use){return use.editorState?"Editor state":use.historical?"Undo / Redo":use.active?(use.entityId?"Current scene":"Open editor"):"Saved";}
void submitted(const char* name,unsigned tab=0,std::size_t row=0,bool enabled=true) {
#if defined(CC_ASSET_REFERENCE_UI_TEST)
    assetReferenceControlForTest(name,tab,row,ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),enabled);
#else
    (void)name;(void)tab;(void)row;(void)enabled;
#endif
}
std::size_t reportBytes(const AssetReferenceReport& report) {
    auto bytes=sizeof(report)+report.targetId.capacity()+report.targetName.capacity()+report.catalog.capacity()*sizeof(AssetEntry);
    for(const auto& entry:report.catalog)bytes+=entry.id.capacity()+entry.name.capacity();
    for(const auto* rows:{&report.uses,&report.dependencies}) {
        bytes+=rows->capacity()*sizeof(AssetUsage);
        for(const auto& use:*rows)bytes+=use.targetId.capacity()+use.ownerId.capacity()+use.ownerName.capacity()+use.slot.capacity();
    }
    return bytes;
}
}
void AssetReferencesPanel::close() {
    cancellation_.request_stop();request_={};report_.reset();open_=false;stale_=false;diagnostic_.clear();
    displayRows_={};matches_={};filteredQuery_.clear();filterDirty_=true;
}
void AssetReferencesPanel::open(std::string id,AssetLibrary& assets,const SceneDocument* active,ResourceManager& resources) {
    close();target_=std::move(id);open_=true;focus_=true;search_.fill(0);request(assets,active,resources);
}
void AssetReferencesPanel::request(AssetLibrary& assets,const SceneDocument* active,ResourceManager& resources) {
    cancellation_.request_stop();request_={};report_.reset();cancellation_=std::stop_source{};stale_=false;diagnostic_.clear();filterDirty_=true;
    for(auto& rows:displayRows_)rows.clear();for(auto& rows:matches_)rows.clear();
    try {
        auto input=assets.referenceIndexInput(active);revision_=input.requestRevision;
        sceneId_=active?active->assetId():std::string{};documentGeneration_=active?active->documentGeneration():0;
        ResourceRequestOptions options;options.priority=ResourcePriority::Background;options.retainLoader=false;options.cancelWhenUnused=true;
        options.dependencyValidation=DependencyValidation::FileState;
        for(const auto& location:input.locations) {
            options.dependencies.push_back(location.file);
            if(location.file.filename()=="manifest.json")options.dependencies.push_back(location.file.parent_path()/"model.gltf");
        }
        std::sort(options.dependencies.begin(),options.dependencies.end());
        options.dependencies.erase(std::unique(options.dependencies.begin(),options.dependencies.end()),options.dependencies.end());
        const auto target=target_;auto cancellation=cancellation_;
        const auto key="asset-reference-report-"+std::to_string(nextReferenceRequest.fetch_add(1));
        request_=resources.requestDerived<AssetReferenceReport>(key,[input=std::move(input),target,cancellation](std::stop_token stop) mutable {
            std::stop_callback stopped(stop,[&]{cancellation.request_stop();});
            auto report=std::make_shared<AssetReferenceReport>(makeAssetReferenceReport(input,target,cancellation.get_token()));
            const auto bytes=reportBytes(*report);
            return ResourceLoadResult<AssetReferenceReport>{std::move(report),bytes,input.requestRevision};
        },std::move(options));
        if(!request_)diagnostic_="Reference request could not be queued.";
    }catch(const std::exception& error){revision_=assets.referenceReportRevision(active);diagnostic_=error.what();}
}
void AssetReferencesPanel::prepareRows() {
    std::unordered_map<std::string,const AssetEntry*> catalog;catalog.reserve(report_->catalog.size());
    for(const auto& entry:report_->catalog)catalog.emplace(entry.id,&entry);
    for(unsigned tab=0;tab<2;++tab) {
        const auto& source=tab?report_->dependencies:report_->uses;auto& rows=displayRows_[tab];rows.clear();rows.reserve(source.size());
        for(const auto& use:source) {
            const auto& id=tab?use.targetId:use.ownerId;const auto known=catalog.find(id);
            const auto name=tab?(known==catalog.end()?id:known->second->name):use.ownerName;
            rows.push_back({name,lower(name+" "+use.ownerName+" "+use.ownerId+" "+use.targetId+" "+use.slot+" "+state(use)),
                known==catalog.end()?AssetKind{}:known->second->kind,known!=catalog.end()});
        }
    }
    filterDirty_=true;
}
void AssetReferencesPanel::filterRows() {
    if(!filterDirty_)return;filteredQuery_=lower(search_.data());
    for(unsigned tab=0;tab<2;++tab) {
        auto& matches=matches_[tab];matches.clear();const auto& rows=displayRows_[tab];
        for(std::size_t i=0;i<rows.size();++i)if(filteredQuery_.empty()||rows[i].search.find(filteredQuery_)!=std::string::npos)matches.push_back(i);
    }
    filterDirty_=false;
}
void AssetReferencesPanel::update(const AssetLibrary& assets,const SceneDocument* active) {
    if(!open_)return;
    if(assets.referenceReportRevision(active)!=revision_) {
        stale_=true;cancellation_.request_stop();request_={};return;
    }
    if(!request_)return;
    if(const auto snapshot=request_.snapshot();snapshot&&snapshot->requestRevision==revision_&&snapshot.shared()!=report_) {
        report_=snapshot.shared();prepareRows();
    }
    if(request_.state()==ResourceState::Failed){diagnostic_=request_.diagnostic();request_={};}
}
AssetReferenceNavigation AssetReferencesPanel::draw(AssetLibrary& assets,const SceneDocument* active,ResourceManager& resources) {
    AssetReferenceNavigation result;if(!open_)return result;update(assets,active);
    ImGui::SetNextWindowSize({780,440},ImGuiCond_FirstUseEver);if(focus_){ImGui::SetNextWindowFocus();focus_=false;}
    bool visible=open_;
    if(ImGui::Begin("Asset References",&visible)) {
        ImGui::TextUnformatted(report_?report_->targetName.c_str():assets.displayName(target_).c_str());
        ImGui::SameLine();if(ImGui::SmallButton("Refresh"))request(assets,active,resources);submitted("refresh");
        if(stale_)ImGui::TextDisabled("References changed. Refresh to update.");
        else if(!diagnostic_.empty())ImGui::TextWrapped("%s",diagnostic_.c_str());
        else if(!report_)ImGui::TextDisabled("Scanning references...");
        ImGui::SetNextItemWidth(-1);
        if(ImGui::InputTextWithHint("##reference-search","Search references",search_.data(),search_.size()))filterDirty_=true;
        submitted("search");filterRows();
        if(report_&&ImGui::BeginTabBar("Reference direction")) {
            for(unsigned tab=0;tab<2;++tab) {
                const bool selected=ImGui::BeginTabItem(tab?"Dependencies":"Uses");submitted("tab",tab);
                if(!selected)continue;
                const auto& rows=tab?report_->dependencies:report_->uses;const auto& matches=matches_[tab];
                if(matches.empty())ImGui::TextDisabled("No references.");
                else if(ImGui::BeginTable("References",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupColumn(tab?"Dependency":"Owner");ImGui::TableSetupColumn("Reference");ImGui::TableSetupColumn("State");ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthFixed,64);
                    ImGui::TableHeadersRow();ImGuiListClipper clipper;clipper.Begin(static_cast<int>(matches.size()));
                    while(clipper.Step())for(int item=clipper.DisplayStart;item<clipper.DisplayEnd;++item) {
                        const auto index=matches[static_cast<std::size_t>(item)];const auto& use=rows[index];const auto& display=displayRows_[tab][index];ImGui::PushID(static_cast<int>(index));
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(display.name.c_str());
                        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",(tab?use.targetId:use.ownerId).c_str());
                        ImGui::TableNextColumn();
                        if(tab)ImGui::Text("%s / %s",use.ownerName.c_str(),use.slot.c_str());else ImGui::TextUnformatted(use.slot.c_str());
                        ImGui::TableNextColumn();ImGui::TextUnformatted(state(use));ImGui::TableNextColumn();
                        const auto& id=tab?use.targetId:use.ownerId;
                        const bool current=!tab&&use.active&&!use.historical&&use.entityId&&active&&use.ownerId==sceneId_&&active->assetId()==sceneId_&&active->documentGeneration()==documentGeneration_;
                        const bool enabled=!stale_&&!use.editorState&&(current||display.known);
                        ImGui::BeginDisabled(!enabled);
                        if(ImGui::SmallButton(current?"Select":"Open")) {
                            result.assetId=current?std::string{}:id;
                            result.sceneId=current?sceneId_:display.known&&display.kind==AssetKind::Scene?id:std::string{};
                            result.entityId=current||(!tab&&!result.sceneId.empty())?use.entityId:0;
                            result.documentGeneration=current?documentGeneration_:0;
                        }
                        submitted("action",tab,index,enabled);ImGui::EndDisabled();ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();if(!visible)close();return result;
}
}
