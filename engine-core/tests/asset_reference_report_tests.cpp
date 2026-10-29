// Actual authored-reference collector and panel in a private CPU ImGui context.
// No native windows, editor process, GPU initialization or user Content writes.
#include "tools/AssetReferencesPanel.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "scene/SceneDocument.h"
#include <imgui_internal.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace crimson {
struct AssetReferencePanelTestAccess {
    static const auto& matches(const AssetReferencesPanel& panel,unsigned tab){return panel.matches_[tab];}
    static const auto& rows(const AssetReferencesPanel& panel,unsigned tab){return panel.displayRows_[tab];}
    static const auto& query(const AssetReferencesPanel& panel){return panel.filteredQuery_;}
};
namespace reference_test {
struct Control {std::string name;unsigned tab;std::size_t row;ImRect bounds;bool enabled;};
std::vector<Control> controls;
}
void assetReferenceControlForTest(const char* name,unsigned tab,std::size_t row,ImVec2 minimum,ImVec2 maximum,bool enabled) {
    reference_test::controls.push_back({name,tab,row,{minimum,maximum},enabled});
}
}
namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
void require(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-reference-report-"+std::to_string(Clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root);}
    ~Project(){
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-reference-report-"))fs::remove_all(target,error);
    }
};
std::map<fs::path,std::string> files(const fs::path& root) {
    std::map<fs::path,std::string> result;
    for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path().lexically_relative(root),read(entry.path()));
    return result;
}
struct Fixture {
    Project project;
    ResourceManager resources{ResourceConfig{.workers=1}};
    AssetLibrary assets{project.root,&resources};
    SceneDocument saved{project.root},active{project.root};
    std::string source,blend,inactive;
    EntityId savedEntity{},first{},second{};
    Fixture(){
        auto material=*assets.createMaterial("Reference source");source=material.id;
        const auto image=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(material.graph,image)->texture.image.id=GrayTextureId; // Disconnected is still authored.
        canonicalizeMaterialAsset(material);saveMaterialAsset(project.root,material);assets.publishMaterial(std::make_shared<const MaterialAsset>(material));
        inactive=assets.createMaterial("Inactive layer source")->id;
        auto recipe=*assets.createBlendMaterial("Reference Blend");blend=recipe.id;
        for(unsigned i=0;i<3;++i){auto& layer=recipe.blendDefinition->layers[i];layer.source=BlendMaterialSource::Existing;layer.material.id=i==2?inactive:source;layer.enabled=i<2;}
        canonicalizeMaterialAsset(recipe);saveMaterialAsset(project.root,recipe);assets.publishMaterial(std::make_shared<const MaterialAsset>(recipe));
        savedEntity=saved.createBox("Saved occurrence");assign(saved,{savedEntity});require(saved.save("Saved reference owner"),saved.status());assets.refreshScenes();
        first=active.createBox("Same name");second=active.createBox("Same name");assign(active,{first,second});
        const auto removed=active.createBox("Historical occurrence");assign(active,{removed});active.select({removed});require(active.eraseSelection(),active.status());
        assets.protectMaterialDocument(blend);assets.setMaterialDocumentReferences({}, {}, {source},{source});
        const auto archived=resolveAssetPath(project.root,"assets/editor-session.json");fs::create_directories(archived.parent_path());
        writeFileAtomically(archived,std::string("{\"roughnessMap\":\"")+GrayTextureId+"\"}\n");
    }
    void assign(SceneDocument& document,std::vector<EntityId> ids){
        require(document.edit("Assign shared Blend",[&](std::vector<EntityRecord>& records,SceneProperties&){
            for(auto& record:records)if(std::find(ids.begin(),ids.end(),record.id)!=ids.end())record.modelMaterials={AssetRef<MaterialAsset>{blend}};
        }),document.status());
    }
};
void collectorChecks(Fixture& fixture) {
    const auto before=files(fixture.project.root);const auto policy=fixture.assets.references().serialize();
    const auto input=fixture.assets.referenceIndexInput(&fixture.active);
    const auto report=makeAssetReferenceReport(input,GrayTextureId);
    const auto has=[&](auto predicate){return std::any_of(report.uses.begin(),report.uses.end(),predicate);};
    require(has([&](const auto& use){return use.ownerId==fixture.source&&use.targetId==GrayTextureId&&!use.active;}),"Disconnected authored texture disappeared from Uses.");
    require(has([&](const auto& use){return use.ownerId==fixture.saved.assetId()&&use.entityId==fixture.savedEntity&&!use.active;}),"Transitive saved-scene use is missing its stable entity locator.");
    for(const auto id:{fixture.first,fixture.second})require(has([&](const auto& use){return use.active&&!use.historical&&use.entityId==id;}),"Same-named current entities collapsed into one reference row.");
    require(has([](const auto& use){return use.active&&use.historical;}),"Scene/document undo references were omitted.");
    require(has([&](const auto& use){return use.active&&!use.entityId&&use.ownerId==fixture.blend&&!use.historical;}),"Open material draft references were omitted.");
    require(has([](const auto& use){return use.editorState;}),"Archived editor state was confused with saved authored owners.");
    const auto dependencies=makeAssetReferenceReport(input,fixture.blend);
    for(const auto& id:{fixture.source,fixture.inactive,std::string(GrayTextureId)})
        require(std::any_of(dependencies.dependencies.begin(),dependencies.dependencies.end(),[&](const auto& use){return use.targetId==id;}),"Dependencies omitted an inactive layer or transitive raw map.");
    require(fixture.assets.references().serialize()==policy,"Reference reporting mutated authoritative deletion policy.");
    require(files(fixture.project.root)==before,"Reference reporting rewrote authored files or generated cache assets.");
    std::stop_source stopped;stopped.request_stop();bool cancelled{};
    try{makeAssetReferenceReport(input,fixture.source,stopped.get_token());}catch(const std::exception&){cancelled=true;}
    require(cancelled,"A cancelled report still traversed/published sources.");
    auto moved=input;++moved.storageRevision;bool rejected{};
    try{makeAssetReferenceReport(moved,fixture.source);}catch(const std::exception&){rejected=true;}
    require(rejected,"A stale location snapshot was accepted.");
    const auto revision=fixture.assets.referenceReportRevision(&fixture.active);
    fixture.assets.findModel(BuiltinBoxModelId);fixture.assets.browserCatalog();
    require(revision==fixture.assets.referenceReportRevision(&fixture.active),"Ordinary primitive/browser hydration invalidated authored references.");
    fixture.assets.setMaterialDocumentReferences({}, {}, {}, {});
    require(revision!=fixture.assets.referenceReportRevision(&fixture.active),"Open document reference changes did not invalidate an old report.");
    const auto captured=makeAssetReferenceReport(input,fixture.source);
    require(std::any_of(captured.uses.begin(),captured.uses.end(),[](const auto& use){return use.active&&!use.entityId&&!use.historical;}),"Captured document references borrowed later mutable editor state.");
    fixture.assets.setMaterialDocumentReferences({}, {}, {fixture.source},{fixture.source});
}
struct Context {
    ImGuiContext* value=ImGui::CreateContext();
    Context(){auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1200,900};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));}
    ~Context(){ImGui::DestroyContext(value);}
};
class Ui {
public:
    explicit Ui(Fixture& fixture):fixture(fixture){}
    AssetReferenceNavigation tick(){
        fixture.resources.pump();reference_test::controls.clear();ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({1000,740});
        auto action=panel.draw(fixture.assets,&fixture.active,fixture.resources);ImGui::Render();
        require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"References panel left invalid ImGui state.");return action;
    }
    void ready(){const auto deadline=Clock::now()+std::chrono::seconds{10};while(Clock::now()<deadline){tick();if(panel.report()){tick();return;}require(panel.diagnostic().empty(),panel.diagnostic());std::this_thread::sleep_for(std::chrono::milliseconds{1});}throw std::runtime_error("Reference report did not publish.");}
    reference_test::Control control(std::string_view name,unsigned tab=0,std::size_t row=0)const {
        const auto found=std::find_if(reference_test::controls.begin(),reference_test::controls.end(),[&](const auto& control){return control.name==name&&control.tab==tab&&control.row==row;});
        require(found!=reference_test::controls.end(),"The actual report control was not submitted.");return *found;
    }
    AssetReferenceNavigation click(reference_test::Control control){auto& io=ImGui::GetIO();const auto center=control.bounds.GetCenter();io.AddMousePosEvent(center.x,center.y);tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);return tick();}
    void query(const char* value){click(control("search"));auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);tick();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);tick();io.AddKeyEvent(ImGuiKey_Backspace,true);tick();io.AddKeyEvent(ImGuiKey_Backspace,false);io.AddInputCharactersUTF8(value);tick();}
    Fixture& fixture;Context context;AssetReferencesPanel panel;
};
void panelChecks(Fixture& fixture) {
    Ui ui(fixture);const auto before=files(fixture.project.root);
    ui.panel.open(fixture.source,fixture.assets,&fixture.active,fixture.resources);ui.ready();
    const auto loaded=fixture.resources.stats().loads;const auto report=ui.panel.report();
    const auto* rows=AssetReferencePanelTestAccess::rows(ui.panel,0).data();
    for(int frame=0;frame<40;++frame)ui.tick();
    require(ui.panel.report()==report&&fixture.resources.stats().loads==loaded&&AssetReferencePanelTestAccess::rows(ui.panel,0).data()==rows,"Idle References redraw rebuilt the report or requeued source work.");
    ui.query("CURRENT SCENE");require(AssetReferencePanelTestAccess::query(ui.panel)=="current scene","Actual search input did not accept/filter case-insensitively.");
    const auto& matches=AssetReferencePanelTestAccess::matches(ui.panel,0);require(matches.size()==2,"Search did not distinguish the two current scene occurrences from saved/history uses.");
    const auto row=matches.front();const auto selected=ui.click(ui.control("action",0,row));
    require(selected.assetId.empty()&&selected.sceneId==fixture.active.assetId()&&selected.entityId==report->uses[row].entityId&&selected.documentGeneration==fixture.active.documentGeneration(),"Select returned an unstable or wrong-document entity intent.");
    ui.query("Saved");const auto& savedMatches=AssetReferencePanelTestAccess::matches(ui.panel,0);
    const auto savedRow=std::find_if(savedMatches.begin(),savedMatches.end(),[&](auto index){return report->uses[index].ownerId==fixture.saved.assetId();});
    require(savedRow!=savedMatches.end(),"Saved owner was not searchable.");const auto opened=ui.click(ui.control("action",0,*savedRow));
    require(opened.assetId==fixture.saved.assetId()&&opened.sceneId==fixture.saved.assetId()&&opened.entityId==fixture.savedEntity,"Saved scene Open lost its stable entity locator.");
    fixture.active.createEmpty("Change while report is visible");ui.tick();require(ui.panel.stale()&&ui.panel.report()==report,"An authored edit did not mark retained results stale.");
    for(const auto& control:reference_test::controls)if(control.name=="action")require(!control.enabled,"Stale results still enabled navigation.");
    ui.click(ui.control("refresh"));ui.ready();require(!ui.panel.stale()&&ui.panel.report()!=report,"Explicit Refresh did not replace the stale snapshot.");
    ui.panel.open(fixture.blend,fixture.assets,&fixture.active,fixture.resources);ui.ready();ui.click(ui.control("tab",1));ui.tick();
    const auto dependencyReport=ui.panel.report();const auto found=std::find_if(dependencyReport->dependencies.begin(),dependencyReport->dependencies.end(),[&](const auto& use){return use.targetId==fixture.source;});
    require(found!=dependencyReport->dependencies.end(),"Dependencies tab omitted the source material.");
    const auto dependencyRow=static_cast<std::size_t>(found-dependencyReport->dependencies.begin());const auto dependency=ui.click(ui.control("action",1,dependencyRow));
    require(dependency.assetId==fixture.source&&!dependency.entityId,"Dependencies Open did not target its asset identity.");
    ui.panel.close();require(!ui.panel.isOpen()&&!ui.panel.report()&&!ui.panel.loading(),"Close retained report ownership or a live request.");
    require(files(fixture.project.root)==before,"Panel interactions wrote project content or document state.");
}
void supersededChecks(Fixture& fixture) {
    struct Gate {std::atomic_bool started{},release{};};const auto gate=std::make_shared<Gate>();
    auto blocker=fixture.resources.requestDerived<AssetReferenceReport>("reference-report-test-gate",[gate](std::stop_token stop){gate->started=true;while(!gate->release.load()&&!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds{1});return ResourceLoadResult<AssetReferenceReport>{std::make_shared<AssetReferenceReport>()};});
    const auto deadline=Clock::now()+std::chrono::seconds{5};while(!gate->started.load()&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds{1});require(gate->started.load(),"The report cancellation gate did not start.");
    AssetReferencesPanel panel;panel.open(fixture.source,fixture.assets,&fixture.active,fixture.resources);
    fixture.active.createEmpty("Edit before worker starts");panel.update(fixture.assets,&fixture.active);
    require(panel.stale()&&!panel.loading()&&!panel.report(),"A superseded queued report retained publication interest.");
    gate->release=true;
    const auto idleDeadline=Clock::now()+std::chrono::seconds{5};while(!fixture.resources.idle()&&Clock::now()<idleDeadline){fixture.resources.pump();panel.update(fixture.assets,&fixture.active);std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(fixture.resources.idle()&&!panel.report(),"A stale completion replaced the active report.");
    panel.close();blocker={};
    panel.open(fixture.source,fixture.assets,&fixture.active,fixture.resources);panel.close();
    const auto closeDeadline=Clock::now()+std::chrono::seconds{5};while(!fixture.resources.idle()&&Clock::now()<closeDeadline){fixture.resources.pump();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(fixture.resources.idle()&&!panel.report()&&fixture.resources.stats().liveInterests==0,"Closing an in-flight report retained request interest.");
}
}
int main(){try{Fixture fixture;collectorChecks(fixture);panelChecks(fixture);supersededChecks(fixture);std::cout<<"Authored reference snapshots, transitive uses/dependencies, stable navigation, explicit refresh, idle ownership and cancellation passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
