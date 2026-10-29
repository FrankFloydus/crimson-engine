#include "integration/LevelSession.h"
#include "integration/LevelProjectState.h"
#include "assets/ContentStorage.h"
#include "assets/AssetTransaction.h"
#include "math/Json.h"
#include "resources/ShaderCompiler.h"
#include "assets/AssetLibrary.h"
#include "runtime/PlaySession.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct TemporaryProject {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-native-level-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryProject(){std::filesystem::create_directories(root);}
    ~TemporaryProject(){std::error_code error;std::filesystem::remove_all(root,error);}
};
void nativeDomainOwnership(){
    TemporaryProject project;std::string saved;EntityId player{},spot{};
    {
        integration::LevelSession level(project.root);
        require(level.bindDocument("project-a:scene-a"),"A pristine level could not bind its modeling document.");
        require(!std::filesystem::exists(project.root/".crimson/level-bindings.json"),"Reading a level binding wrote authored associations.");
        player=level.document().createPlayer("Eye player");spot=level.document().createSpotLight("Hall spot");
        require(player&&spot,"Engine entity authoring did not reach its schema-25 document.");
        require(level.save("Engine level"),level.status().c_str());saved=level.document().assetId();
        require(std::filesystem::exists(project.root/".crimson/level-bindings.json"),"Explicit level save did not atomically associate its modeling scene.");
        auto changed=*level.document().scene().find(spot);changed.name="Unsaved lamp";
        require(level.document().updateEntity(spot,changed),"Could not stage a dirty level switch.");
        require(!level.bindDocument("project-a:scene-b")&&level.document().scene().find(spot)->name=="Unsaved lamp", "A modeling document switch discarded an unsaved Engine level.");
        require(level.undo()&&!level.document().dirty(),"Level Undo did not restore its accepted save state.");
        const auto authored=level.document().scene().records();const auto history=level.document().history().size();
        require(level.play(),level.status().c_str());require(level.playing(),"Play did not own a transient Engine simulation.");
        require(level.setPaused(true)&&level.paused(),"Engine Pause did not own explicit runtime state.");
        require(level.step()&&level.runtime().tickCount()==1&&level.paused(),"Engine Step failed to publish exactly one fixed tick while paused.");
        level.stop();require(!level.playing(),"Stop retained the transient physics world.");
        const auto after=level.document().scene().records();
        require(after.size()==authored.size()&&std::equal(after.begin(),after.end(),authored.begin(),[](const auto& a,const auto& b){return sameEntity(a,b);}),"Play changed the authored Engine level.");
        require(level.document().history().size()==history&&!level.document().dirty(),"Play entered authoring history or changed its save state.");
        require(level.bindDocument("project-a:scene-b"),"A clean level could not switch modeling context.");
    }
    {
        integration::LevelSession restored(project.root);
        require(restored.bindDocument("project-a:scene-a"),restored.status().c_str());
        require(restored.document().assetId()==saved&&restored.document().scene().find(player)&&restored.document().scene().find(spot),"Reopening the native app lost the saved level association or persistent entities.");
        require(!restored.document().dirty(),"Loading the associated Engine level authored a migration.");
    }
}
std::string bytes(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>{input},{}};}
void originalProjectRestoration(){
    TemporaryProject project;std::string asset;EntityId cameraEntity{};
    {
        integration::LevelSession source(project.root);cameraEntity=source.document().createCamera("Authored eye");require(cameraEntity&&source.save("Original authored level"),"Could not create original Engine level fixture.");asset=source.document().assetId();
    }
    const auto statePath=project.root/".crimson/editor-session.json";
    const nlohmann::json state{{"version",1},{"scene",asset},{"panels",{{"outliner",false}}},{"overlays",{{"grid",false}}},{"cameras",{{asset,{{"position",Vec3{3,4,5}},{"target",Vec3{1,2,0}},{"up",Vec3{0,1,0}},{"fov",75.f},{"near",.05f},{"far",4096.f},{"fovAxis","vertical"}}}}}};
    {std::ofstream output(statePath);output<<state.dump(2);}
    const auto original=bytes(statePath);const auto authored=checkedManagedAssetPath(project.root,asset);const auto originalScene=bytes(authored);
    {
        integration::LevelSession restored(project.root);
        require(restored.document().assetId()==asset&&restored.document().scene().find(cameraEntity),"An Engine project required a modeler project before restoring its level.");
        const auto camera=restored.editorCamera();require(camera&&camera->position==Vec3{3,4,5}&&camera->fov==75.f&&camera->fovAxis==CameraFovAxis::Vertical,"Original per-scene camera and lens were not restored.");
        require(!restored.document().dirty()&&bytes(statePath)==original&&bytes(authored)==originalScene,"Startup restoration wrote scene/session data.");
        require(restored.bindDocument("separate-modeler:scene",true)&&restored.document().assetId()==asset,"First paired modeling binding discarded the accepted original level.");
        auto changed=*camera;changed.position={9,4,5};restored.rememberEditorCamera(changed);require(bytes(statePath)==original,"Remembering a camera wrote outside an explicit publication.");
        require(restored.saveEditorState(),"Accepted session metadata did not save atomically.");
    }
    const auto accepted=nlohmann::json::parse(bytes(statePath));require(accepted.at("panels")==state.at("panels")&&accepted.at("overlays")==state.at("overlays"),"Session publication discarded retained original fields.");
    require(bytes(authored)==originalScene,"Session camera publication changed the authored eye camera.");
    std::filesystem::create_directories(project.root/"Content/subfolder");const auto found=integration::findLevelProjectRoot(project.root/"Content/subfolder");require(found&&*found==project.root,"Native Engine root discovery did not find Content without a Quader manifest.");
    const auto selection=project.root/"private-host/engine-project.json";require(!integration::readLevelProjectSelection(selection)&&!std::filesystem::exists(selection),"Missing project selection wrote startup data.");
    integration::writeLevelProjectSelection(selection,project.root);require(integration::readLevelProjectSelection(selection)==project.root,"Explicit native Engine folder choice did not restore independently.");
    {std::ofstream output(statePath);output<<"{broken";}integration::LevelProjectState malformed(project.root);require(!malformed.save(asset)&&bytes(statePath)=="{broken","Malformed session state was silently overwritten.");
}
template<class Predicate> void updateUntil(integration::LevelSession& level,Predicate done){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{4};while(!done()){level.update(0,true);if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Native resource notification exceeded its bounded CPU deadline.");std::this_thread::sleep_for(std::chrono::milliseconds{2});}}
void nativeResourceBoundary(){
    TemporaryProject project;integration::LevelSession level(project.root);const auto player=level.document().createPlayer("Reload player"),spot=level.document().createSpotLight("Original spot");require(player&&spot&&level.save("Reload level"),"Could not save native resource fixture.");
    const auto savedGeneration=level.document().documentGeneration(),savedHistory=level.document().history().size();
    auto shader=level.resources().request<ShaderBinary>(AssetRef<ShaderBinary>{"native-session-publication"},[](std::stop_token){auto result=std::make_shared<ShaderBinary>();result->name="cpu publication fixture";result->bytes={1,2,3};return ResourceLoadResult<ShaderBinary>{result,3,1,{}};});
    updateUntil(level,[&]{return shader.state()==ResourceState::Ready;});level.update(0,true);require(level.resources().takePublications().empty(),"LevelSession retained an unbounded resource-publication event log.");
    const auto ownDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds{300};while(std::chrono::steady_clock::now()<ownDeadline){level.update(0,true);std::this_thread::sleep_for(std::chrono::milliseconds{2});}
    require(level.document().documentGeneration()==savedGeneration&&level.document().history().size()==savedHistory,"An own Save notification reloaded the document and discarded Undo history.");(void)level.takeFileChanges();
    auto external=readSceneSnapshot(project.root,level.document().assetId());for(auto& record:external.records)if(record.id==spot)record.name="External clean spot";saveSceneSnapshot(project.root,external.id,external);
    updateUntil(level,[&]{return level.document().scene().find(spot)->name=="External clean spot";});require(!level.document().dirty(),"Read-only external scene refresh authored a change.");
    require(level.play(),"Could not start isolated Play before deferred resource reload.");(void)level.takeFileChanges();
    external=readSceneSnapshot(project.root,level.document().assetId());for(auto& record:external.records)if(record.id==spot)record.name="External during Play";saveSceneSnapshot(project.root,external.id,external);
    bool notified{};updateUntil(level,[&]{const auto changes=level.takeFileChanges();notified=notified||!changes.empty();return notified;});
    require(level.playing()&&level.document().scene().find(spot)->name=="External clean spot","An external file changed the authored or accepted Play scene before Stop.");
    level.stop();level.update(0,true);require(level.document().scene().find(spot)->name=="External during Play"&&!level.document().dirty(),"Stop did not accept a deferred external change into a clean editing document.");
}
}
int main(){try{nativeDomainOwnership();originalProjectRestoration();nativeResourceBoundary();std::cout<<"Native Engine roots, original scene/camera restoration, resource publication/reload, level associations, authoring history and isolated fixed Play lifecycle passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
