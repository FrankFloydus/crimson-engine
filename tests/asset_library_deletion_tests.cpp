#include "material_fixtures.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include "assets/BuiltinGeometry.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "editor/PreparedMaterialSignature.h"
#include "scene/SceneDocument.h"
#include "model_fixtures.h"
#include <bgfx/platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;

void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
struct Project {
    fs::path root;
    Project(){
        static unsigned sequence{};
        root=fs::temp_directory_path()/("crimson-library-delete-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++sequence));
        fs::create_directories(root/"assets");
    }
    ~Project(){
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-library-delete-test-"))fs::remove_all(target,error);
    }
};
std::shared_ptr<ModelAsset> seedModel(const Project& project,bool embedded=false){
    const auto fixtures=model_test::writeModelFixtures(project.root/"sources");
    auto model=importModelPackage(embedded?fixtures.glb:fixtures.gltf,project.root);promoteModelMaterials(project.root,*model);return model;
}
void nearMatrix(const Mat4& left,const Mat4& right){
    for(size_t i=0;i<16;++i)require(std::abs(left[i]-right[i])<0.0002f,"Asset deletion changed a surviving child's world transform.");
}
bool hasAsset(const AssetLibrary& library,const std::string& id){
    return std::any_of(library.catalog().begin(),library.catalog().end(),[&](const auto& asset){return asset.id==id;});
}
void finishDeletion(AssetLibrary& library,ResourceManager& resources,const AssetDeletionPlan& plan,
                    const std::unordered_map<std::string,std::string>& replacements={},SceneDocument* active=nullptr){
    require(plan.error.empty(),plan.error);
    if(library.deleteAssets(plan,replacements,active))return;
    require(library.deletionPending(),library.status());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{20};
    while(std::chrono::steady_clock::now()<deadline){
        resources.pump();
        if(library.pumpDeletion(active))return;
        require(library.deletionPending(),library.status());
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    throw std::runtime_error("Public AssetLibrary deletion did not complete: "+library.status());
}
const EntityRecord& record(const SceneSnapshot& scene,EntityId id){
    const auto found=std::find_if(scene.records.begin(),scene.records.end(),[&](const auto& entity){return entity.id==id;});
    require(found!=scene.records.end(),"Saved scene lost an unrelated entity.");return *found;
}

void warningQueries(){
    Project p;const auto model=seedModel(p);SceneDocument active(p.root);active.createModel(model->id,"Only an unsaved instance");
    writeFileAtomically(p.root/"assets/preview.json",nlohmann::json{{"version",1},{"model",model->id}}.dump());
    writeFileAtomically(p.root/"assets/editor-session.json",nlohmann::json{{"version",1},{"model",model->id}}.dump());
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto unsaved=library.planDeletion({model->id},&active);
    require(unsaved.error.empty()&&!unsaved.needsReferenceWarning()&&unsaved.clearsActiveHistory,
        "Unsaved placement, undo history or editor bookmarks triggered an authored dependency warning.");
    require(!unsaved.usages.empty(),"Suppressing dependency warnings discarded active/history impact information.");
    active.createModel(model->id,"A second instance");require(active.save("Saved instances"),active.status());library.refreshScenes();
    const auto saved=library.planDeletion({model->id},&active);
    require(saved.error.empty()&&saved.needsReferenceWarning()&&saved.warningUsages.size()==1&&saved.warningUsages.front().ownerId==active.assetId(),
        "Saved scene references were missing or counted once per entity/current history.");
    const auto together=library.planDeletion({model->id,active.assetId()},&active);
    require(together.error.empty()&&!together.needsReferenceWarning(),"A scene removed with its model was counted as a surviving owner.");
}

void legacyEnvironmentGlowPersistence(){
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto file=p.root/"assets/environment.json";
    writeFileAtomically(file,R"({"version":1,"map":"","enabled":false,"intensity":2.5,"rotation":35,"toneMapping":3})");
    EnvironmentSettings environment;environment.glow={true,4,2,3};environment.source=EnvironmentSource::ProceduralSky;
    environment.background={true,true,{1,0,0}};environment.fog={true,{1,0,0},.5f};
    environment.fog.mode=FogMode::Depth;environment.fog.height=5;environment.fog.heightDensity=.3f;environment.fog.lightEnergy=4;
    environment.debanding=true;
    library.loadEnvironment(environment);
    require(!environment.glow.enabled && environment.glow==GlowSettings{} && environment.toneMapping==ToneMapping::AgX && environment.intensity==2.5f,
        "Legacy environment without Glow did not retain its old appearance and default Glow off.");
    require(environment.source==EnvironmentSource::Hdri && environment.procedural==ProceduralSkySettings{} &&
        environment.background==EnvironmentBackground{} && environment.fog==FogSettings{} && !environment.debanding,
        "Legacy environment gained procedural lighting, a visible/custom background or fog.");
    environment.glow={true,2.25f,3.5f,4.75f};environment.source=EnvironmentSource::ProceduralSky;
    environment.procedural={{.1f,.2f,.3f},{.3f,.4f,.5f},{.6f,.7f,.8f},{.8f,.6f,.4f},.7f,.4f,1.5f,45.f,.6f};
    environment.background={false,true,{.12f,.23f,.34f}};environment.fog={true,{.4f,.5f,.6f},.025f};
    environment.fog.mode=FogMode::Depth;environment.fog.depthBegin=15;environment.fog.depthEnd=0;
    environment.fog.depthCurve=.7f;environment.fog.depthOpacity=.8f;
    environment.fog.height=-4;environment.fog.heightDensity=-.025f;environment.fog.lightEnergy=2.5f;
    environment.debanding=true;
    library.saveEnvironment(environment);const auto saved=read(file);
    EnvironmentSettings reopened;library.loadEnvironment(reopened);
    require(reopened==environment,"Legacy environment expanded settings failed exact save/reopen.");
    require(nlohmann::json::parse(saved).at("version")==4,"Expanded legacy environment did not save the current file version.");
    auto versionTwo=nlohmann::json::parse(saved);versionTwo["version"]=2;versionTwo.erase("debanding");
    for(const auto* field:{"mode","depthBegin","depthEnd","depthCurve","depthOpacity","height","heightDensity","lightEnergy"})versionTwo["fog"].erase(field);
    const auto versionTwoText=versionTwo.dump();writeFileAtomically(file,versionTwoText);
    auto versionTwoExpected=environment;versionTwoExpected.fog={environment.fog.enabled,environment.fog.color,environment.fog.density};versionTwoExpected.debanding=false;
    library.loadEnvironment(reopened);
    require(reopened==versionTwoExpected && read(file)==versionTwoText,"Legacy v2 migration changed exponential fog or enabled depth/height/energy effects.");
    auto versionThree=nlohmann::json::parse(saved);versionThree["version"]=3;versionThree.erase("debanding");
    const auto versionThreeText=versionThree.dump();writeFileAtomically(file,versionThreeText);
    auto versionThreeExpected=environment;versionThreeExpected.debanding=false;
    library.loadEnvironment(reopened);
    require(reopened==versionThreeExpected && read(file)==versionThreeText,"Legacy v3 migration changed existing settings or enabled Debanding.");
    writeFileAtomically(file,saved);library.loadEnvironment(reopened);
    require(reopened==environment,"Restoring extended fog after legacy load lost authored settings.");
    auto invalid=environment;invalid.glow.radius=9;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid Glow settings replaced the saved legacy environment file.");
    invalid=environment;invalid.fog.density=-.1f;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid fog density replaced the saved legacy environment file.");
    invalid=environment;invalid.procedural.sunHaloExtent=invalid.procedural.sunDiameter*.5f;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid procedural sun shape replaced the saved legacy environment file.");
    invalid=environment;invalid.background.color[0]=1.1f;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid background color replaced the saved legacy environment file.");
    invalid=environment;invalid.fog.depthEnd=invalid.fog.depthBegin;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid depth range replaced the saved legacy environment file.");
    invalid=environment;invalid.fog.depthOpacity=1.1f;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid depth opacity replaced the saved legacy environment file.");
    invalid=environment;invalid.fog.lightEnergy=-1;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid fog light energy replaced the saved legacy environment file.");
    auto corrupt=nlohmann::json::parse(saved);corrupt["glow"]["strength"]=-1;writeFileAtomically(file,corrupt.dump());
    auto working=environment;library.loadEnvironment(working);
    require(working==environment,"Invalid legacy Glow load replaced working environment values.");
    for(const auto invalidDebanding:{nlohmann::json(0),nlohmann::json(1),nlohmann::json("true"),nlohmann::json(nullptr)}) {
        corrupt=nlohmann::json::parse(saved);corrupt["debanding"]=invalidDebanding;
        const auto invalidText=corrupt.dump();writeFileAtomically(file,invalidText);library.loadEnvironment(working);
        require(working==environment && read(file)==invalidText,"Invalid Debanding load replaced working values or rewrote its source.");
    }
    corrupt=nlohmann::json::parse(saved);corrupt["fog"]["density"]=-1;writeFileAtomically(file,corrupt.dump());
    library.loadEnvironment(working);require(working==environment,"Invalid legacy fog load replaced working environment values.");
    corrupt=nlohmann::json::parse(saved);corrupt["procedural"]["skyCurve"]=0;writeFileAtomically(file,corrupt.dump());
    library.loadEnvironment(working);require(working==environment,"Invalid legacy sky load replaced working environment values.");
    corrupt=nlohmann::json::parse(saved);corrupt["fog"]["mode"]=1.5;writeFileAtomically(file,corrupt.dump());
    library.loadEnvironment(working);require(working==environment,"Invalid legacy fog mode type replaced working environment values.");
    corrupt=nlohmann::json::parse(saved);corrupt["fog"]["heightDensity"]="0.2";writeFileAtomically(file,corrupt.dump());
    library.loadEnvironment(working);require(working==environment,"Invalid legacy height density type replaced working environment values.");
    corrupt=nlohmann::json::parse(saved);corrupt["fog"]["depthCurve"]=0;writeFileAtomically(file,corrupt.dump());
    library.loadEnvironment(working);require(working==environment,"Invalid legacy depth curve replaced working environment values.");
    writeFileAtomically(file,saved);library.loadEnvironment(working);
    require(working==environment && read(file)==saved,"Restoring a valid legacy file changed values or rewrote its source.");
}

void archivedLegacyWarnings(){
    Project p;const auto model=seedModel(p);const auto fixtures=model_test::writeModelFixtures(p.root/"legacy-source");
    const std::string image="assets/textures/0b21c54a-61bd-4a5a-a0a9-693d6ad4f15f_roughness_linear.png";
    fs::create_directories(p.root/"assets/textures");fs::copy_file(fixtures.image,p.root/image);
    const auto legacyMaterial=nlohmann::json{{"version",2},{"roughnessMap",image}}.dump();
    const auto legacyEnvironment=nlohmann::json{{"version",1},{"map",image}}.dump();
    writeFileAtomically(p.root/"assets/material.json",legacyMaterial);writeFileAtomically(p.root/"assets/environment.json",legacyEnvironment);
    SceneDocument active(p.root);active.createModel(model->id,"Model");require(active.save("Scene"),active.status());
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto unused=library.planDeletion({image},&active);
    require(unused.error.empty()&&!unused.needsReferenceWarning(),"Archived sphere material/environment files triggered an asset dependency warning.");
    require(std::count_if(unused.usages.begin(),unused.usages.end(),[](const auto& use){return use.editorState;})==2,"Archived references were removed from full impact tracking.");
    const auto environment=active.createEnvironment();auto record=*active.scene().find(environment);record.environment->environment.map=image;
    record.environment->environment.source=EnvironmentSource::ProceduralSky;
    require(active.updateEntity(environment,record),active.status());require(active.save(),active.status());library.refreshScenes();
    const auto used=library.planDeletion({image},&active);
    require(used.error.empty()&&used.warningUsages.size()==1&&used.warningUsages.front().ownerId==active.assetId(),"A procedural Environment's retained HDRI dependency was hidden with legacy settings.");
    std::vector<std::string> all;for(const auto& asset:library.catalog())all.push_back(asset.id);
    const auto deletingAll=library.planDeletion(all,&active);
    require(deletingAll.error.empty()&&!deletingAll.needsReferenceWarning(),"Selecting all usable browser assets left a legacy or same-batch owner warning.");
    finishDeletion(library,resources,deletingAll,{},&active);
    require(read(p.root/"assets/material.json")==legacyMaterial&&read(p.root/"assets/environment.json")==legacyEnvironment,
        "Warning classification changed preserved legacy configuration files.");
    require(library.references().resolveTexture(image)==GrayTextureId,
        "Suppressing archived warnings changed the persisted texture replacement policy.");
}

void embeddedMaterialWarningOwners(){
    Project p;const auto model=seedModel(p);
    for(const auto& material:model->materialIds)fs::remove(p.root/material);
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto image=model->images.front();
    const auto imageOnly=library.planDeletion({image});
    require(imageOnly.error.empty()&&imageOnly.warningUsages.size()==1&&imageOnly.warningUsages.front().ownerId==model->id,
        "A model's embedded material was hidden or exposed as a missing standalone owner.");
    const auto withOwner=library.planDeletion({image,model->id});
    require(withOwner.error.empty()&&!withOwner.needsReferenceWarning(),"Deleting a model with its image retained a nonexistent material owner warning.");
    // A native sidecar arriving before catalog refresh is still consumed through
    // its model; it must not become a second, unselectable warning owner.
    promoteModelMaterials(p.root,*model);
    const auto notYetCataloged=library.planDeletion({image});
    require(notYetCataloged.error.empty()&&notYetCataloged.warningUsages.size()==1&&notYetCataloged.warningUsages.front().ownerId==model->id,
        "Uncataloged native material sidecars became extra ghost warning owners.");
}

void removeModelAcrossScenes(bool dirty){
    Project p;const auto model=seedModel(p);SceneDocument active(p.root);
    const auto parent=active.createEmpty("Unrelated root");auto parentRecord=*active.scene().find(parent);
    parentRecord.local=math::translation({3,2,-4})*math::rotation(math::axisAngle({0,1,0},0.4))*math::scaling({-2,1.5f,0.75f});
    require(active.updateEntity(parent,parentRecord),active.status());
    const auto removed=active.createModel(model->id,"Deleted model",math::translation({6,1,2}));
    active.select({removed});require(active.reparentSelection(parent),active.status());
    const auto child=active.createEmpty("Saved child");active.select({child});require(active.reparentSelection(removed),active.status());
    auto childRecord=*active.scene().find(child);childRecord.local=math::translation({1,3,2});childRecord.local[4]=0.2f;
    require(active.updateEntity(child,childRecord),active.status());
    const auto childWorld=*active.scene().worldTransform(child);
    require(active.save("First scene"),active.status());const auto firstId=active.assetId();
    SceneDocument other(p.root);const auto otherModel=other.createModel(model->id,"Another saved instance");
    const auto otherChild=other.createPointLight("Preserved light");other.select({otherChild});require(other.reparentSelection(otherModel),other.status());
    const auto otherWorld=*other.scene().worldTransform(otherChild);require(other.save("Second scene"),other.status());
    if(dirty){auto unsaved=*active.scene().find(child);unsaved.name="Unsaved child rename";require(active.updateEntity(child,unsaved),active.status());}
    require(active.dirty()==dirty,"Fixture clean/dirty state is wrong.");
    const auto materialBytes=read(p.root/model->materialIds.front());const auto imageBytes=read(p.root/model->images.front());
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto plan=library.planDeletion({model->id},&active);
    require(plan.error.empty()&&plan.scenes.size()==2&&plan.warningUsages.size()==2&&plan.clearsActiveHistory,"Public deletion did not report both saved scenes and active history.");
    finishDeletion(library,resources,plan,{},&active);
    require(!active.scene().contains(removed)&&active.scene().contains(child)&&active.scene().find(child)->parent==parent,"Active model removal deleted or detached unrelated children incorrectly.");
    nearMatrix(*active.scene().worldTransform(child),childWorld);
    require(active.dirty()==dirty&&active.history().size()==0&&!active.history().canUndo(),"Global model deletion changed the document's dirty state or retained invalid undo records.");
    require(active.scene().find(child)->name==(dirty?"Unsaved child rename":"Saved child"),"Deletion lost the active unsaved edit.");
    const auto first=readSceneSnapshot(p.root,firstId),second=readSceneSnapshot(p.root,other.assetId());
    require(std::none_of(first.records.begin(),first.records.end(),[](const auto& entity){return entity.model.has_value();})&&
        std::none_of(second.records.begin(),second.records.end(),[](const auto& entity){return entity.model.has_value();}),"A saved project scene retained the deleted model.");
    require(record(first,child).name=="Saved child","Deletion saved unrelated dirty document edits to disk.");
    Scene firstScene;firstScene.apply(first.records,first.properties);nearMatrix(*firstScene.worldTransform(child),childWorld);
    Scene secondScene;secondScene.apply(second.records,second.properties);nearMatrix(*secondScene.worldTransform(otherChild),otherWorld);
    require(!fs::exists(p.root/model->id)&&!hasAsset(library,model->id),"Deleted model remained published in storage or the catalog.");
    require(read(p.root/model->materialIds.front())==materialBytes&&read(p.root/model->images.front())==imageBytes&&
        hasAsset(library,model->materialIds.front())&&hasAsset(library,model->images.front()),"Deleting a model removed independent material/image assets.");
    require(!active.externalFileChanged(),"The active scene fingerprint was not advanced to its externally remapped saved file.");
}

void fallbackReopening(){
    Project p;const auto model=seedModel(p);const auto fixtures=model_test::writeModelFixtures(p.root/"fallback-source");
    fs::create_directories(p.root/"assets/textures");fs::copy_file(fixtures.image,p.root/"assets/textures/replacement.png");
    const std::string imageFallback="assets/textures/replacement.png";
    auto material=loadMaterialAsset(p.root,model->materialIds.front());material.id="assets/materials/replacement.crimson-material";material.name="Replacement material";material_test::legacyFixture(material);
    material.surface.baseColor={.25f,.5f,.75f,1};material.surface.baseColorTexture.image=AssetRef<DecodedImage>{imageFallback};saveMaterialAsset(p.root,material);
    {
        ResourceManager resources;AssetLibrary library(p.root,&resources);
        const auto image=model->images.front();const auto plan=library.planDeletion({image});
        require(plan.error.empty()&&plan.needsReferenceWarning(),"A package image's actual material dependencies were not reported.");
        finishDeletion(library,resources,plan,{{image,imageFallback}});
        require(!fs::exists(p.root/image)&&library.references().resolveTexture(image)==imageFallback,"Texture fallback was not published.");
        const auto reopened=loadModelPackage(model->id,p.root,library.modelResolvers());
        require(library.references().resolveTexture(reopened->materials.front().baseColorTexture.image.id)==imageFallback,"Managed model reopening ignored a deleted package-image fallback.");
        const auto materialPlan=library.planDeletion({model->materialIds.front()});
        finishDeletion(library,resources,materialPlan,{{model->materialIds.front(),material.id}});
        require(loadModelPackage(model->id,p.root,library.modelResolvers())->materials.front().baseColor==material.surface.baseColor,"Material fallback lost authored factors when reopening.");
        const auto normalUses=library.planDeletion({model->images.at(1)});
        require(normalUses.error.empty()&&std::none_of(normalUses.warningUsages.begin(),normalUses.warningUsages.end(),[&](const auto& usage){return library.references().deleted(usage.ownerId);}),
            "A replaced imported material sidecar remained a ghost dependency owner.");
        finishDeletion(library,resources,library.planDeletion({material.id}));
        const auto defaults=loadModelPackage(model->id,p.root,library.modelResolvers())->materials.front();
        require(defaults.baseColor==Vec4{.5f,.5f,.5f,1}&&defaults.roughness==1&&defaults.metallic==0,"No-fallback material deletion did not use the requested default.");
        const auto normal=model->images.at(1);finishDeletion(library,resources,library.planDeletion({normal}));
        require(library.references().resolveTexture(normal,true)==NeutralNormalTextureId&&library.references().resolveTexture(normal)==GrayTextureId,"No-fallback texture defaults lost role semantics.");
    }
    ResourceManager resources;AssetLibrary reopened(p.root,&resources);
    const auto modelAgain=loadModelPackage(model->id,p.root,reopened.modelResolvers());
    require(!modelAgain->draws.empty()&&reopened.references().resolve(model->materialIds.front())==DefaultMaterialId,
        "Persisted deletion redirects failed on a fresh AssetLibrary.");
}

void cancelAndStaleDeletion(){
    Project p;const auto model=seedModel(p);SceneDocument active(p.root);const auto entity=active.createModel(model->id,"Model");require(active.save("Scene"),active.status());
    ResourceManager resources;AssetLibrary library(p.root,&resources);auto plan=library.planDeletion({model->id},&active);
    const auto savedId=active.assetId();const auto beforeModel=read(p.root/model->id),beforeScene=read(p.root/savedId),beforeBrowser=read(p.root/"assets/browser.json");
    require(!library.deleteAssets(plan,{},&active)&&library.deletionPending(),"Runtime asset deletion did not defer validation.");
    library.cancelDeletion();require(!library.deletionPending()&&!library.pumpDeletion(&active),"Canceled deletion retained a pending publication.");
    for(unsigned i=0;i<30;++i){resources.pump();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(read(p.root/model->id)==beforeModel&&read(p.root/active.assetId())==beforeScene&&read(p.root/"assets/browser.json")==beforeBrowser&&
        !fs::exists(p.root/"assets/references.json")&&active.scene().contains(entity),"Canceled deletion changed authored files or the active scene.");
    plan=library.planDeletion({model->id},&active);require(!library.deleteAssets(plan,{},&active)&&library.deletionPending(),"Second deletion failed to start.");
    active.newScene();require(!library.pumpDeletion(&active)&&!library.deletionPending(),"A deletion prepared for a replaced scene was published.");
    require(read(p.root/model->id)==beforeModel&&read(p.root/savedId)==beforeScene,"Stale deletion changed managed model or saved scene files.");
}

void activeSceneAndFolderDeletion(){
    Project p;const auto model=seedModel(p);SceneDocument active(p.root);active.createModel(model->id,"Retained model asset");require(active.save("Delete this scene"),active.status());
    const auto sceneId=active.assetId();const auto generation=active.documentGeneration();
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto folder=library.browserCatalog().createFolder({},"Scenes");
    const auto childFolder=library.browserCatalog().createFolder(folder,"Nested");
    library.browserCatalog().move(std::array<std::string,1>{sceneId},childFolder);
    const auto plan=library.planDeletion({folder},&active);
    require(plan.error.empty()&&plan.assetIds==std::vector<std::string>{sceneId}&&plan.folderIds.size()==2,"Folder deletion did not expand nested content.");
    finishDeletion(library,resources,plan,{},&active);
    require(active.assetId().empty()&&active.scene().records().empty()&&active.dirty()&&active.history().size()==0&&active.documentGeneration()!=generation,
        "Deleting the active scene did not open a new empty unsaved document.");
    require(!fs::exists(p.root/sceneId)&&!hasAsset(library,sceneId)&&!library.browserCatalog().contains(folder)&&!library.browserCatalog().contains(childFolder),"Folder/scene deletion left browser entries or files.");
    require(fs::exists(p.root/model->id)&&hasAsset(library,model->id),"Deleting a scene deleted a referenced model asset.");
    BrowserCatalog reopened(p.root);reopened.synchronize(library.catalog());require(!reopened.contains(folder)&&!reopened.contains(sceneId),"Deleted folder contents reappeared on reopening.");
}
void particleMaterialDeletionAndAuthority(){
    Project p;auto material=createMaterialAsset(p.root,"Shared sprite");auto effect=createParticleAsset(p.root,"Sparks");
    material_test::legacyFixture(material);material.shading=MaterialShading::Unlit;material.blend=MaterialBlend::Additive;saveMaterialAsset(p.root,material);
    effect.emitter.material=AssetRef<MaterialAsset>{material.id};saveParticleAsset(p.root,effect);
    SceneDocument scene(p.root);const auto entity=scene.createParticle(effect.id,"Placed effect",math::translation({2,3,4}));require(scene.save("Particle scene"),scene.status());
    const auto sceneBytes=read(p.root/scene.assetId());ResourceManager resources;AssetLibrary library(p.root,&resources);
    library.protectParticleDocument(effect.id);library.setParticleDocumentReferences({material.id},{material.id});
    const auto plan=library.planDeletion({material.id},&scene);
    require(plan.error.empty()&&plan.warningUsages.size()==1&&plan.warningUsages.front().ownerId==effect.id,
        "Saved effect material dependency was missing, counted per scene instance, or inflated by active/history usages.");
    require(std::any_of(plan.usages.begin(),plan.usages.end(),[](const auto& usage){return usage.historical;}),"Material deletion lost active particle history impact.");
    finishDeletion(library,resources,plan,{},&scene);
    require(!fs::exists(p.root/material.id)&&loadParticleAsset(p.root,effect.id).emitter.material.id==DefaultParticleMaterialId&&
        library.references().resolveParticleMaterial(material.id)==DefaultParticleMaterialId,"Material deletion did not atomically install the protected sprite fallback.");
    require(scene.scene().contains(entity)&&scene.scene().find(entity)->particle->id==effect.id&&read(p.root/scene.assetId())==sceneBytes,
        "Deleting a sprite material removed its effect instance or modified the scene file.");
    const auto fallback=library.preparedParticleMaterial(material.id);require(fallback->ready&&fallback->render.baseColor==Vec4{1,1,1,1}&&fallback->render.blend==MaterialBlend::Additive&&!bgfx::isValid(fallback->render.colorTexture),
        "Deleted sprite material did not prepare the original white procedural appearance.");
    library.protectParticleDocument({});library.setParticleDocumentReferences({},{});
    const auto user=library.createMaterial("Live sprite");require(bool(user),library.status());library.protectMaterialDocument(user->id);
    auto committed=*user;material_test::legacyFixture(committed);committed.shading=MaterialShading::Unlit;committed.blend=MaterialBlend::Additive;committed.surface.baseColor={.2f,.3f,.4f,.5f};
    committed.surface.uvScale={2,3,4};committed.surface.uvOffset={.2f,.3f,.4f};committed.surface.uvRotation={0,0,30};library.publishMaterial(std::make_shared<MaterialAsset>(committed));
    const auto revision=library.materialRevision(user->id);const auto first=library.preparedParticleMaterial(user->id);require(first->ready&&first->render.baseColor==committed.surface.baseColor,"Committed open material was not immediately visible.");
    require(first->render.uvScale==committed.surface.uvScale && first->render.uvOffset==committed.surface.uvOffset && first->render.uvRotation==committed.surface.uvRotation,
        "Prepared particle material discarded committed texture mapping.");
    auto external=committed;external.surface.baseColor={.8f,.7f,.6f,1};saveMaterialAsset(p.root,external);library.onFileChanged(p.root/user->id);
    require(library.materialRevision(user->id)==revision&&library.findMaterial(user->id)->surface.baseColor==committed.surface.baseColor,
        "Background reload replaced open material document authority.");
    library.protectMaterialDocument({});library.refreshMaterials();require(library.findMaterial(user->id)->surface.baseColor==external.surface.baseColor,"Closing material authority did not allow reload.");
    const auto good=library.preparedParticleMaterial(user->id);writeFileAtomically(p.root/user->id,"{invalid");library.refreshMaterials();const auto retained=library.preparedParticleMaterial(user->id);
    require(good->current&&retained->ready&&!retained->current&&!retained->diagnostic.empty()&&retained->signature==good->signature&&retained->render.baseColor==good->render.baseColor,
        "Failed material reload lost the immutable last-good renderer packet or its diagnostic.");
    saveMaterialAsset(p.root,external);library.refreshMaterials();require(library.preparedParticleMaterial(user->id)->diagnostic.empty(),"Corrected material reload retained a stale failure diagnostic.");
}
void spriteImageDeletionAndIncompatibleMaterial(){
    Project p;const auto fixtures=model_test::writeModelFixtures(p.root/"source");const std::string image="assets/textures/sprite.png";
    fs::create_directories(p.root/"assets/textures");fs::copy_file(fixtures.image,p.root/image);
    auto sprite=createMaterialAsset(p.root,"Textured");material_test::legacyFixture(sprite);sprite.shading=MaterialShading::Unlit;sprite.blend=MaterialBlend::Alpha;sprite.surface.baseColorTexture.image=AssetRef<DecodedImage>{image};saveMaterialAsset(p.root,sprite);
    auto expectedGraph=loadMaterialAsset(p.root,sprite.id).graph;
    rewriteMaterialGraphTextures(expectedGraph,[&](const auto& id,MaterialTextureRole role){
        return id==image?std::string(role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId):id;
    });
    auto effect=createParticleAsset(p.root,"Smoke");effect.emitter.material=AssetRef<MaterialAsset>{sprite.id};saveParticleAsset(p.root,effect);
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto plan=library.planDeletion({image});
    require(plan.error.empty()&&plan.warningUsages.size()==1&&plan.warningUsages.front().ownerId==sprite.id,
        "Sprite image warnings counted transitive effects as direct image owners.");
    finishDeletion(library,resources,plan);const auto cleared=loadMaterialAsset(p.root,sprite.id);
    require(cleared.surface.baseColorTexture.image.id==GrayTextureId&&cleared.blend==MaterialBlend::Alpha&&library.references().resolveTexture(image)==GrayTextureId,
        "Shared texture deletion lost blending or failed to persist the role-correct fallback.");
    const auto published=library.findMaterial(sprite.id);
    require(published&&particleMaterialCompatible(*published)&&modelMaterialCompatible(*published)&&
        serializeMaterialGraph(cleared.graph)==serializeMaterialGraph(expectedGraph)&&
        serializeMaterialGraph(published->graph)==serializeMaterialGraph(expectedGraph),
        "Texture deletion changed the authored graph or failed to publish its shared role-correct replacement.");
    auto pbr=defaultMaterialAsset();pbr.id="assets/materials/pbr.crimson-material";pbr.name="PBR";saveMaterialAsset(p.root,pbr);library.refreshMaterials();
    const auto deletion=library.planDeletion({sprite.id});
    const auto candidates=library.fallbackCandidates(sprite.id);
    require(std::any_of(candidates.begin(),candidates.end(),[&](const auto& entry){return entry.id==pbr.id;}),"Shared material deletion excluded a valid Lit/Opaque replacement.");
    finishDeletion(library,resources,deletion,{{sprite.id,pbr.id}});
    require(!fs::exists(p.root/sprite.id)&&loadParticleAsset(p.root,effect.id).emitter.material.id==pbr.id&&library.references().resolveMaterial(sprite.id)==pbr.id,
        "Shared material replacement failed to update the saved particle assignment.");
    const auto shared=library.preparedParticleMaterial(pbr.id);
    require(shared->ready&&shared->current&&shared->diagnostic.empty()&&shared->render.baseColor==pbr.surface.baseColor,
        "Lit/Opaque material failed to prepare for a sprite consumer.");
    const auto model=seedModel(p,true);library.refreshMaterials();require(library.modelMaterialsReady(*model),"Imported material metadata did not satisfy model readiness.");
    auto unified=loadMaterialAsset(p.root,model->materialIds.front());materialOutputNode(unified.graph).output.shading=MaterialShading::Unlit;
    materialOutputNode(unified.graph).output.blend=MaterialBlend::Additive;canonicalizeMaterialAsset(unified);library.publishMaterial(std::make_shared<MaterialAsset>(unified));
    require(library.modelMaterialsReady(*model)&&library.modelMaterialsDiagnostic(*model).empty()&&
        library.resolvedModelMaterials(*model).front().baseColor==unified.surface.baseColor,"Shared additive material was replaced by a mesh fallback.");

}
void changedUsagePreservesConsumerAssignments() {
    Project p;auto meshMaterial=createMaterialAsset(p.root,"Changing Surface");
    auto particleMaterial=copyMaterialAsset(p.root,defaultParticleMaterialAsset(),"Changing Particle");
    SceneDocument scene(p.root);const auto box=scene.createBox();auto entity=*scene.scene().find(box);entity.modelMaterial.id=meshMaterial.id;
    require(scene.updateEntity(box,entity)&&scene.save("Usage scene"),scene.status());
    auto effect=createParticleAsset(p.root,"Usage effect");effect.emitter.material.id=particleMaterial.id;saveParticleAsset(p.root,effect);
    const auto sceneBytes=read(p.root/scene.assetId()),effectBytes=read(p.root/effect.id);
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto geometry=library.findModel(BuiltinBoxModelId);
    require(geometry&&library.modelMaterialsDiagnostic(*geometry,meshMaterial.id).empty(),"Surface fixture was not compatible before Usage changed.");
    require(library.preparedParticleMaterial(particleMaterial.id)->ready,"Particle fixture was not usable before Usage changed.");
    const auto savedSurface=meshMaterial,savedParticle=particleMaterial;
    materialOutputNode(meshMaterial.graph).output.shading=MaterialShading::Unlit;
    materialOutputNode(meshMaterial.graph).output.blend=MaterialBlend::Additive;canonicalizeMaterialAsset(meshMaterial);
    saveMaterialAsset(p.root,meshMaterial);library.publishMaterial(std::make_shared<MaterialAsset>(meshMaterial));
    const auto modelPacket=library.resolvedModelMaterials(*geometry,meshMaterial.id);
    require(library.modelMaterialsReady(*geometry,meshMaterial.id)&&library.modelMaterialsDiagnostic(*geometry,meshMaterial.id).empty()&&
        modelPacket.front().baseColor==meshMaterial.surface.baseColor,"Shared mesh shader/blend edit installed an unrelated fallback.");
    materialOutputNode(particleMaterial.graph).output.shading=MaterialShading::StandardPbr;
    materialOutputNode(particleMaterial.graph).output.blend=MaterialBlend::Mask;canonicalizeMaterialAsset(particleMaterial);
    saveMaterialAsset(p.root,particleMaterial);library.publishMaterial(std::make_shared<MaterialAsset>(particleMaterial));
    const auto particlePacket=library.preparedParticleMaterial(particleMaterial.id);
    require(particlePacket->ready&&particlePacket->current&&particlePacket->diagnostic.empty()&&
        particlePacket->render.blend==MaterialBlend::Mask&&particlePacket->render.shading==MaterialShading::StandardPbr,
        "Sprite did not retain the assigned Lit/Masked material.");
    require(scene.scene().find(box)->modelMaterial.id==meshMaterial.id&&record(readSceneSnapshot(p.root,scene.assetId()),box).modelMaterial.id==meshMaterial.id&&
        loadParticleAsset(p.root,effect.id).emitter.material.id==particleMaterial.id&&read(p.root/scene.assetId())==sceneBytes&&read(p.root/effect.id)==effectBytes&&
        library.references().resolveMaterial(meshMaterial.id)==meshMaterial.id&&library.references().resolveParticleMaterial(particleMaterial.id)==particleMaterial.id,
        "Shader/blend edit rewrote saved/live consumer references, source files or deletion aliases.");
    saveMaterialAsset(p.root,savedSurface);library.publishMaterial(std::make_shared<MaterialAsset>(savedSurface));
    saveMaterialAsset(p.root,savedParticle);library.publishMaterial(std::make_shared<MaterialAsset>(savedParticle));
    require(library.modelMaterialsDiagnostic(*geometry,meshMaterial.id).empty()&&
        library.resolvedModelMaterials(*geometry,meshMaterial.id).front().baseColor==savedSurface.surface.baseColor&&
        library.preparedParticleMaterial(particleMaterial.id)->ready&&library.preparedParticleMaterial(particleMaterial.id)->diagnostic.empty(),
        "Restoring shader/blend did not resume the existing saved assignments or clear compatibility diagnostics.");
}
void boxMaterialDeletionAndPublication(){
    Project p;
    auto red=defaultMaterialAsset();red.id="assets/materials/red.crimson-material";red.name="Red";red.surface.baseColor={1,.1f,.1f,1};saveMaterialAsset(p.root,red);
    auto blue=red;blue.id="assets/materials/blue.crimson-material";blue.name="Blue";blue.surface.baseColor={.1f,.1f,1,1};saveMaterialAsset(p.root,blue);
    auto sprite=createMaterialAsset(p.root,"Unsupported sprite");material_test::legacyFixture(sprite);sprite.shading=MaterialShading::Unlit;sprite.blend=MaterialBlend::Additive;saveMaterialAsset(p.root,sprite);
    SceneDocument active(p.root);const auto box=active.createBox();auto entity=*active.scene().find(box);entity.modelMaterial=AssetRef<MaterialAsset>{red.id};
    require(active.updateEntity(box,entity) && active.save("Box scene"),active.status());
    SceneDocument other(p.root);const auto otherBox=other.createBox();entity=*other.scene().find(otherBox);entity.modelMaterial=AssetRef<MaterialAsset>{red.id};
    require(other.updateEntity(otherBox,entity) && other.save("Other Box scene"),other.status());
    entity=*active.scene().find(box);entity.name="Unsaved local name";require(active.updateEntity(box,entity),active.status());
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto geometry=library.findModel(BuiltinBoxModelId);require(bool(geometry),"Built-in Box was unavailable.");
    const auto original=geometry->materials.front().baseColor;
    require(library.resolvedModelMaterials(*geometry,red.id).front().baseColor==red.surface.baseColor && library.resolvedModelMaterials(*geometry,blue.id).front().baseColor==blue.surface.baseColor,
        "Per-instance Box material identities collided.");
    require(geometry->materials.front().baseColor==original && library.findModel(BuiltinBoxModelId)==geometry,"Instance assignment mutated or copied protected geometry.");
    library.protectMaterialDocument(red.id);auto committed=red;committed.surface.roughness=.31f;library.publishMaterial(std::make_shared<MaterialAsset>(committed));
    require(library.resolvedModelMaterials(*geometry,red.id).front().roughness==.31f,"Committed material changes did not reach Box bindings.");
    auto external=red;external.surface.roughness=.87f;saveMaterialAsset(p.root,external);library.onFileChanged(p.root/red.id);
    require(library.resolvedModelMaterials(*geometry,red.id).front().roughness==.31f,"Disk hot reload replaced open material authority.");
    library.protectMaterialDocument({});library.refreshMaterials();
    require(library.resolvedModelMaterials(*geometry,red.id).front().roughness==.87f,"Closed material document did not resume resource publication.");
    const auto plan=library.planDeletion({red.id},&active);
    require(plan.error.empty() && plan.warningUsages.size()==2 && plan.scenes.size()==2 && plan.clearsActiveHistory,"Box material deletion did not discover saved/live/history references.");
    const auto candidates=library.fallbackCandidates(red.id);
    require(std::any_of(candidates.begin(),candidates.end(),[&](const auto& value){return value.id==blue.id;}) &&
        std::any_of(candidates.begin(),candidates.end(),[&](const auto& value){return value.id==sprite.id;}),"Box material deletion excluded a valid shared additive replacement.");
    finishDeletion(library,resources,plan,{{red.id,blue.id}},&active);
    const auto saved=readSceneSnapshot(p.root,active.assetId()),otherSaved=readSceneSnapshot(p.root,other.assetId());
    require(record(saved,box).modelMaterial.id==blue.id && record(otherSaved,otherBox).modelMaterial.id==blue.id,"Material deletion did not update every saved scene.");
    require(record(saved,box).name=="Box" && active.scene().find(box)->name=="Unsaved local name" && active.dirty(),"Material deletion saved or discarded unrelated live edits.");
    require(active.scene().find(box)->modelMaterial.id==blue.id && active.history().size()==0 && active.scene().contains(box),"Material deletion lost the entity or allowed deleted material undo.");
    require(library.resolvedModelMaterials(*geometry,red.id).front().baseColor==blue.surface.baseColor,"Deleted material redirect did not resolve its replacement.");
    finishDeletion(library,resources,library.planDeletion({blue.id},&active),{},&active);
    require(active.scene().find(box)->modelMaterial.id==DefaultMaterialId && record(readSceneSnapshot(p.root,other.assetId()),otherBox).modelMaterial.id==DefaultMaterialId,
        "Default Box material fallback was not durable.");
    require(geometry->materials.front().baseColor==original && library.resolvedModelMaterials(*geometry,red.id).front().baseColor==original,"Deleted override damaged shared geometry/default material.");
}
void immutableMaterialMetadata(){
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);
    auto material=createMaterialAsset(p.root,"Cached graph UVs");const auto output=materialOutputNode(material.graph).id;
    const auto uv=addMaterialNode(material.graph,MaterialNodeKind::UV).id;
    const auto texture=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,texture)->texture.image.id="assets/textures/metadata-only.png";
    connectMaterialNodes(material.graph,uv,MaterialPin::UV,texture,MaterialPin::UV);connectMaterialNodes(material.graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    canonicalizeMaterialAsset(material);library.publishMaterial(std::make_shared<MaterialAsset>(material));
    ModelAsset model;model.id="metadata-only-model";model.materialIds={material.id};model.materials={defaultMaterialAsset().surface};model.primitives.resize(1);model.primitives.front().uvMask=1;
    const auto first=library.findMaterial(material.id);
    for(unsigned i=0;i<25;++i)require(library.modelMaterialsReady(model)&&library.modelMaterialsDiagnostic(model).empty()&&
        library.resolvedModelMaterials(model).front().graph==first->surface.graph,"An unchanged immutable material failed repeated metadata reuse.");
    findMaterialNode(material.graph,uv)->uv.mode=MaterialUvMode::UV1;library.publishMaterial(std::make_shared<MaterialAsset>(material));
    require(library.findMaterial(material.id)!=first,"Material publication did not create a new immutable version.");
    bool rejected=false;try{(void)library.resolvedModelMaterials(model);}catch(const std::exception&){rejected=true;}
    require(rejected,"A newly published UV1 requirement reused stale UV0 validation.");
    model.primitives.front().uvMask=3;require(!library.resolvedModelMaterials(model).empty(),"Changed geometry UV availability reused a stale model validation result.");
    findMaterialNode(material.graph,uv)->uv.mode=MaterialUvMode::WorldTriplanar;library.publishMaterial(std::make_shared<MaterialAsset>(material));model.primitives.front().uvMask=0;
    require(!library.resolvedModelMaterials(model).empty(),"Triplanar replacement retained the previous graph's UV requirements.");
    findMaterialNode(material.graph,texture)->texture.image={};findMaterialNode(material.graph,uv)->uv.mode=MaterialUvMode::UV1;
    library.publishMaterial(std::make_shared<MaterialAsset>(material));
    require(!library.resolvedModelMaterials(model).empty(),"An unassigned texture inherited a stale UV requirement.");
}
void dormantGraphTextureDeletion(){
    Project p;const auto fixtures=model_test::writeModelFixtures(p.root/"source");const std::string image="assets/textures/dormant.png";
    fs::create_directories(p.root/"assets/textures");fs::copy_file(fixtures.image,p.root/image);
    std::array<std::string,2> ids;
    std::array<std::array<MaterialNodeId,2>,2> textureNodes{};
    for(std::size_t index=0;index<2;++index) {
        auto material=createMaterialAsset(p.root,index?"Dormant sprite":"Dormant mesh");ids[index]=material.id;
        for(std::size_t role=0;role<2;++role) {
            const auto id=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;textureNodes[index][role]=id;
            auto& texture=findMaterialNode(material.graph,id)->texture;texture.image.id=image;
            texture.role=role?MaterialTextureRole::Normal:MaterialTextureRole::Color;
            // The normal branch remains authored on a now-inactive Output pin;
            // the color node is completely disconnected.
            if(role)connectMaterialNodes(material.graph,id,MaterialPin::Normal,materialOutputNode(material.graph).id,MaterialPin::Normal);
        }
        materialOutputNode(material.graph).output.shading=MaterialShading::Unlit;
        if(index)materialOutputNode(material.graph).output.blend=MaterialBlend::Alpha;
        saveMaterialAsset(p.root,material);
        require(nlohmann::json::parse(read(p.root/material.id)).at("version")==6,"Dormant deletion fixture was not authored as a graph.");
    }
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    library.setMaterialDocumentReferences({}, {image});
    const auto plan=library.planDeletion({image});
    require(plan.error.empty()&&plan.warningUsages.size()==2,"Dormant graph textures were omitted or duplicate roles inflated authored deletion owners.");
    require(std::any_of(plan.usages.begin(),plan.usages.end(),[](const auto& use){return use.historical;}),"Graph texture deletion omitted the open material's undo dependency.");
    finishDeletion(library,resources,plan);
    require(!fs::exists(p.root/image),"Graph texture deletion did not remove the managed image.");
    for(std::size_t index=0;index<2;++index) {
        const auto restored=loadMaterialAsset(p.root,ids[index]);
        const auto* color=findMaterialNode(restored.graph,textureNodes[index][0]);
        const auto* normal=findMaterialNode(restored.graph,textureNodes[index][1]);
        require(color&&normal&&normal->texture.image.id==NeutralNormalTextureId&&
            color->texture.image.id==GrayTextureId,"Dormant graph deletion lost nodes or installed a fallback with the wrong texture role.");
        require(materialInputLink(restored.graph,materialOutputNode(restored.graph).id,MaterialPin::Normal)!=nullptr,"Graph deletion dropped an inactive shader-model link.");
    }
    library.setMaterialDocumentReferences({},{});
    AssetLibrary reopened(p.root);
    require(reopened.references().resolveTexture(image,true)==NeutralNormalTextureId&&reopened.references().resolveTexture(image)==GrayTextureId,"Role-correct graph fallback policy did not survive reopening.");
}
void pendingGraphKeepsCoverageExplicit() {
    Project p;const auto fixtures=model_test::writeModelFixtures(p.root/"source");
    const std::string image="assets/textures/alpha-color.png";
    fs::create_directories(p.root/"assets/textures");fs::copy_file(fixtures.image,p.root/image);
    auto material=createMaterialAsset(p.root,"Color without alpha coverage");
    auto& graph=material.graph;const auto output=materialOutputNode(graph).id;
    materialOutputNode(graph).output.blend=MaterialBlend::Alpha;materialOutputNode(graph).output.opacity=.7f;
    const auto texture=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,texture)->texture.image.id=image;
    connectMaterialNodes(graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    canonicalizeMaterialAsset(material);saveMaterialAsset(p.root,material);
    auto opaque=createMaterialAsset(p.root,"Conventional opaque fallback");
    const auto opaqueOutput=materialOutputNode(opaque.graph).id;
    const auto base=addMaterialNode(opaque.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(opaque.graph,base)->texture.image.id=image;
    findMaterialNode(opaque.graph,base)->texture.sampler.wrapU=ModelWrap::Mirror;
    connectMaterialNodes(opaque.graph,base,MaterialPin::RGB,opaqueOutput,MaterialPin::BaseColor);
    const auto normal=addMaterialNode(opaque.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(opaque.graph,normal)->texture.image.id=image;
    findMaterialNode(opaque.graph,normal)->texture.role=MaterialTextureRole::Normal;
    connectMaterialNodes(opaque.graph,normal,MaterialPin::Normal,opaqueOutput,MaterialPin::Normal);
    canonicalizeMaterialAsset(opaque);saveMaterialAsset(p.root,opaque);
    bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
    require(bgfx::init(init),"Could not initialize scoped Noop graph-fallback check.");
    try {
        {
            ResourceManager resources;
            ShaderCompileConfig config;config.compiler=p.root/"missing-shaderc.exe";config.sourceDirectory=p.root/"missing-shaders";
            config.includeDirectory=p.root/"missing-includes";config.cacheDirectory=p.root/"shader-cache";
            MaterialGraphPrograms programs(resources,config);AssetLibrary library(p.root,&resources);library.setMaterialGraphPrograms(&programs);
            const auto preview=library.materialPreviewBindings(material);
            require(preview.graphPending&&!preview.graph&&!preview.diagnostic.empty()&&
                std::all_of(preview.handles.begin(),preview.handles.end(),[](const auto handle){return !bgfx::isValid(handle);}),
                "Unavailable graph compiler exposed a legacy texture/alpha approximation in preview bindings.");
            const auto particle=library.preparedParticleMaterial(material.id);
            require(particle->ready&&!particle->current&&!particle->diagnostic.empty()&&!particle->render.graph&&
                !bgfx::isValid(particle->render.colorTexture)&&particle->render.baseColor[3]==.7f,
                "Pending sprite graph used implicit texture alpha or claimed to be a complete current packet.");
            materialOutputNode(material.graph).output.opacity=.2f;canonicalizeMaterialAsset(material);
            library.publishMaterial(std::make_shared<MaterialAsset>(material));
            const auto changed=library.preparedParticleMaterial(material.id);
            require(changed->ready&&!changed->current&&!changed->render.graph&&changed->render.baseColor[3]==.2f,
                "A pending fallback was retained as though it were a prior complete graph bundle.");
            ModelAsset model;model.id="pending-ordinary-model";model.materials={opaque.surface};model.primitives.resize(1);
            model.primitives.front().material=0;model.primitives.front().uvMask=3;model.primitives.front().tangentUv=0;
            ModelMaterialTextures pending;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
            do {
                resources.pump();library.updateResources(bgfx::frame());pending=library.modelBindings(model).front();
                if(pending.sourceReady&&pending.complete&&bgfx::isValid(pending.handles[0])&&bgfx::isValid(pending.handles[2]))break;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }while(std::chrono::steady_clock::now()<deadline);
            require(!pending.graphPending&&!pending.graph&&pending.diagnostic.empty()&&pending.sourceReady&&pending.complete&&pending.drawable()&&
                bgfx::isValid(pending.handles[0])&&bgfx::isValid(pending.handles[2])&&pending.leases[0]&&pending.leases[2]&&
                pending.sourceFingerprints[0]!=0&&pending.sourceFingerprints[2]!=0&&
                (pending.flags[0]&BGFX_SAMPLER_U_MIRROR)!=0,
                "An exact conventional graph did not render completely through embedded programs while its compiler was unavailable.");
            model.primitives.front().tangentUv=1;
            const auto mismatched=library.modelBindings(model).front();
            require(mismatched.graphPending&&!mismatched.graph&&
                std::all_of(mismatched.handles.begin(),mismatched.handles.end(),[](auto handle){return !bgfx::isValid(handle);}),
                "A fixed graph fallback used a normal texture with a different authored tangent UV basis.");
            model.primitives.front().tangentUv=0;model.materials={material.surface};
            const auto alphaModel=library.modelBindings(model).front();
            require(alphaModel.graphPending&&!alphaModel.graph&&
                std::all_of(alphaModel.handles.begin(),alphaModel.handles.end(),[](auto handle){return !bgfx::isValid(handle);}),
                "A cold alpha model graph inherited the conventional opaque texture fallback and implicit texture-alpha coverage.");
        }
        bgfx::shutdown();
    }catch(...){bgfx::shutdown();throw;}
}
void boxMaterialHistoryOnlyDeletion(){
    Project p;auto material=defaultMaterialAsset();material.id="assets/materials/history.crimson-material";material.name="History material";saveMaterialAsset(p.root,material);
    SceneDocument scene(p.root);const auto box=scene.createBox();require(scene.save("Default Box"),scene.status());const auto saved=read(p.root/scene.assetId());
    auto entity=*scene.scene().find(box);entity.modelMaterial=AssetRef<MaterialAsset>{material.id};require(scene.updateEntity(box,entity),scene.status());
    entity.modelMaterial={};require(scene.updateEntity(box,entity),scene.status());ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto plan=library.planDeletion({material.id},&scene);require(plan.error.empty() && !plan.needsReferenceWarning() && plan.clearsActiveHistory,"History-only Box material impact was missing or treated as a saved dependency.");
    finishDeletion(library,resources,plan,{},&scene);
    require(scene.history().size()==0 && scene.dirty() && scene.scene().find(box)->modelMaterial.empty() && read(p.root/scene.assetId())==saved,
        "History-only material deletion changed saved content or retained resurrectable references.");
}
void materialPreviewResources(const fs::path& compiler,const fs::path& sources,const fs::path& includes){
    Project p;const std::string image="assets/textures/preview-pattern.png";
    model_test::Bytes pixels(512*512*4,255);
    for(std::size_t y=0;y<512;++y)for(std::size_t x=0;x<512;++x){const auto offset=(y*512+x)*4;pixels[offset]=x<256?255:0;pixels[offset+1]=y<256?0:255;}
    fs::create_directories(p.root/"assets/textures");model_test::writeBytes(p.root/image,model_test::png(512,512,pixels));
    auto material=createMaterialAsset(p.root,"Streamed preview");const auto output=materialOutputNode(material.graph).id;
    const auto uv=addMaterialNode(material.graph,MaterialNodeKind::UV).id;findMaterialNode(material.graph,uv)->uv.mode=MaterialUvMode::WorldTriplanar;
    const auto color=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,color)->texture.image.id=image;findMaterialNode(material.graph,color)->texture.sampler.minFilter=ModelFilter::LinearMipmapLinear;
    connectMaterialNodes(material.graph,uv,MaterialPin::UV,color,MaterialPin::UV);connectMaterialNodes(material.graph,color,MaterialPin::RGB,output,MaterialPin::BaseColor);
    canonicalizeMaterialAsset(material);saveMaterialAsset(p.root,material);const auto sourceBytes=read(p.root/material.id);
    bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
    require(bgfx::init(init),"Could not initialize material-preview resource check.");
    try {{
        ResourceManager resources;MaterialGraphPrograms programs(resources,{compiler,sources,includes,p.root/"assets/cache/shaders"});
        AssetLibrary library(p.root,&resources);library.setMaterialGraphPrograms(&programs);
        const auto tick=[&]{resources.pump();library.updateResources(bgfx::frame());programs.trim();};
        const auto until=[&](auto request,const auto& accepted,const char* failure){
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{35};ModelMaterialTextures bindings;
            while(std::chrono::steady_clock::now()<deadline){tick();bindings=request();if(accepted(bindings))return bindings;std::this_thread::sleep_for(std::chrono::milliseconds{2});}
            throw std::runtime_error(std::string(failure)+": "+bindings.diagnostic);
        };
        const auto ready=[](const auto& binding){return binding.graph&&!binding.graphPending&&binding.sourceReady&&binding.complete&&binding.drawable();};
        auto full=until([&]{return library.materialPreviewBindings(material,512);},ready,"Initial material preview did not refine");
        const auto fullSignature=full.graph->textureSignatures;
        // Reproduce the room regression: fixed rendering allocations had reduced
        // the generic texture cache allowance to zero, degrading visible materials.
        std::vector<ModelMaterial> surfaces{material.surface};const auto geometry=builtinBoxGeometry();
        for(unsigned frame=0;frame<80;++frame){tick();library.setTextureDemand(512);auto visible=library.modelBindings(*geometry,surfaces);library.trimTextureCache(0);std::this_thread::sleep_for(std::chrono::milliseconds{2});}
        auto degraded=library.modelBindings(*geometry,surfaces).front();
        require(!degraded.graph&&!degraded.graphPending&&degraded.signatures[0]!=fullSignature[0],"Zero-budget fixture did not exercise texture degradation on the complete embedded model path.");
        bool coarseDrawable=false;
        until([&]{
            library.setTextureDemand(1);auto binding=library.materialPreviewBindings(material,512);
            coarseDrawable|=binding.sourceReady&&binding.drawable()&&!binding.complete;library.trimTextureCache(0);return binding;
        },[&](const auto& binding){return ready(binding)&&binding.graph->textureSignatures==fullSignature;},"Selected material preview inherited visible-scene texture pressure");
        require(coarseDrawable,"Preview resource fixture did not pass through a coherent coarse packet before refinement.");
        for(unsigned frame=0;frame<20;++frame){tick();auto binding=library.materialPreviewBindings(material,512);library.trimTextureCache(0);
            require(ready(binding)&&binding.graph->textureSignatures==fullSignature,"Zero cache allowance degraded the selected material preview again.");}
        require(read(p.root/material.id)==sourceBytes,"Preview publication rewrote its authored material.");
        auto animated=material;const auto animatedOutput=materialOutputNode(animated.graph).id;
        findMaterialNode(animated.graph,uv)->uv.mode=MaterialUvMode::UV0;
        materialOutputNode(animated.graph).output.blend=MaterialBlend::Mask;
        const auto panner=addMaterialNode(animated.graph,MaterialNodeKind::Panner).id;
        const auto time=addMaterialNode(animated.graph,MaterialNodeKind::Time).id;
        findMaterialNode(animated.graph,panner)->panner.speed={.25f,0};findMaterialNode(animated.graph,time)->time={false,true,4};
        connectMaterialNodes(animated.graph,uv,MaterialPin::UV,panner,MaterialPin::UV);
        connectMaterialNodes(animated.graph,time,MaterialPin::Value,panner,MaterialPin::Time);
        connectMaterialNodes(animated.graph,panner,MaterialPin::UV,color,MaterialPin::UV);
        connectMaterialNodes(animated.graph,color,MaterialPin::A,animatedOutput,MaterialPin::OpacityMask);canonicalizeMaterialAsset(animated);
        auto animatedBinding=until([&]{return library.materialPreviewBindings(animated,512);},ready,"Animated material preview did not publish");
        const auto preparedSignature=[&](const auto& definition,const auto& binding){
            const std::array<ModelMaterial,1> materials{definition.surface};const std::array<ModelMaterialTextures,1> textures{binding};
            return preparedModelSignature(*geometry,1,materials,textures);
        };
        const auto animatedSignature=preparedSignature(animated,animatedBinding),animatedPacket=animatedBinding.graph->signature;
        require(animatedBinding.graph->dynamicCoverage&&animatedBinding.graph->coverageSignature({1,1})!=animatedBinding.graph->coverageSignature({3,3}),
            "Animated masked coverage stopped invalidating its affected shadow maps.");
        findMaterialNode(animated.graph,panner)->panner.speed[0]=1.25f;canonicalizeMaterialAsset(animated);
        auto changed=until([&]{return library.materialPreviewBindings(animated,512);},ready,"Edited Panner did not publish");
        require(changed.graph->uniforms==animatedBinding.graph->uniforms&&changed.graph->signature!=animatedPacket&&preparedSignature(animated,changed)!=animatedSignature,
            "A Panner edit with unchanged t=0 uniforms retained the old runtime graph packet.");
        const auto pannerSignature=changed.graph->signature;
        findMaterialNode(animated.graph,time)->time={true,true,2};canonicalizeMaterialAsset(animated);
        changed=until([&]{return library.materialPreviewBindings(animated,512);},ready,"Edited Time did not publish");
        require(changed.graph->uniforms==animatedBinding.graph->uniforms&&changed.graph->signature!=pannerSignature,
            "Time period/pause edits with unchanged t=0 uniforms retained the old runtime graph packet.");
        materialOutputNode(animated.graph).output.blend=MaterialBlend::Opaque;canonicalizeMaterialAsset(animated);
        changed=until([&]{return library.materialPreviewBindings(animated,512);},ready,"Opaque animated material did not publish");
        require(!changed.graph->dynamicCoverage&&changed.graph->coverageSignature({1,1})==changed.graph->coverageSignature({3,3}),
            "Opaque animation invalidated static shadow coverage.");
        require(read(p.root/material.id)==sourceBytes,"Runtime material edits rewrote the authored fixture.");
        resources.stop();
    }bgfx::shutdown();}catch(...){bgfx::shutdown();throw;}
    std::cout<<"Material preview coherent coarse publication and selected texture recovery under zero cache budget passed.\n";
}
}

int main(int argc,char** argv){try{
    if(argc==5&&std::string(argv[1])=="--material-preview-resources") {materialPreviewResources(argv[2],argv[3],argv[4]);return 0;}
    // Public deletion checks use disposable CPU data; the final fallback check
    // opens a scoped Noop device without a native application window.
    warningQueries();legacyEnvironmentGlowPersistence();archivedLegacyWarnings();embeddedMaterialWarningOwners();removeModelAcrossScenes(false);removeModelAcrossScenes(true);fallbackReopening();cancelAndStaleDeletion();activeSceneAndFolderDeletion();particleMaterialDeletionAndAuthority();spriteImageDeletionAndIncompatibleMaterial();changedUsagePreservesConsumerAssignments();boxMaterialDeletionAndPublication();immutableMaterialMetadata();boxMaterialHistoryOnlyDeletion();dormantGraphTextureDeletion();pendingGraphKeepsCoverageExplicit();
    std::cout<<"Public AssetLibrary deletion, warning, fallback and document integration checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"AssetLibrary deletion test failed: "<<error.what()<<'\n';return 1;}}
