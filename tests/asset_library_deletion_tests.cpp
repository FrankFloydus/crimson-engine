#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include "assets/BuiltinGeometry.h"
#include "scene/SceneDocument.h"
#include "model_fixtures.h"

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
    EnvironmentSettings environment;environment.glow={true,4,2,3};library.loadEnvironment(environment);
    require(!environment.glow.enabled && environment.glow==GlowSettings{} && environment.toneMapping==ToneMapping::AgX && environment.intensity==2.5f,
        "Legacy environment without Glow did not retain its old appearance and default Glow off.");
    environment.glow={true,2.25f,3.5f,4.75f};library.saveEnvironment(environment);const auto saved=read(file);
    EnvironmentSettings reopened;library.loadEnvironment(reopened);
    require(reopened.glow==environment.glow && reopened.intensity==environment.intensity && reopened.rotation==environment.rotation,
        "Legacy environment Glow settings failed save/reopen.");
    auto invalid=environment;invalid.glow.radius=9;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid Glow settings replaced the saved legacy environment file.");
    auto corrupt=nlohmann::json::parse(saved);corrupt["glow"]["strength"]=-1;writeFileAtomically(file,corrupt.dump());
    auto working=environment;library.loadEnvironment(working);
    require(working.glow==environment.glow && working.rotation==environment.rotation,"Invalid legacy Glow load replaced working environment values.");
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
    require(active.updateEntity(environment,record),active.status());require(active.save(),active.status());library.refreshScenes();
    const auto used=library.planDeletion({image},&active);
    require(used.error.empty()&&used.warningUsages.size()==1&&used.warningUsages.front().ownerId==active.assetId(),"A saved environment entity's actual image dependency was hidden with legacy settings.");
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
    auto material=loadMaterialAsset(p.root,model->materialIds.front());material.id="assets/materials/replacement.crimson-material";material.name="Replacement material";
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
    material.shading=MaterialShading::Unlit;material.blend=MaterialBlend::Additive;saveMaterialAsset(p.root,material);
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
    const auto fallback=library.preparedParticleMaterial(material.id);require(fallback->ready&&fallback->render.baseColor==Vec4{1,1,1,1}&&fallback->render.blend==ParticleBlendMode::Additive&&!bgfx::isValid(fallback->render.colorTexture),
        "Deleted sprite material did not prepare the original white procedural appearance.");
    library.protectParticleDocument({});library.setParticleDocumentReferences({},{});
    const auto user=library.createMaterial("Live sprite");require(bool(user),library.status());library.protectMaterialDocument(user->id);
    auto committed=*user;committed.shading=MaterialShading::Unlit;committed.blend=MaterialBlend::Additive;committed.surface.baseColor={.2f,.3f,.4f,.5f};
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
    auto sprite=createMaterialAsset(p.root,"Textured");sprite.shading=MaterialShading::Unlit;sprite.blend=MaterialBlend::Alpha;sprite.surface.baseColorTexture.image=AssetRef<DecodedImage>{image};saveMaterialAsset(p.root,sprite);
    auto effect=createParticleAsset(p.root,"Smoke");effect.emitter.material=AssetRef<MaterialAsset>{sprite.id};saveParticleAsset(p.root,effect);
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto plan=library.planDeletion({image});
    require(plan.error.empty()&&plan.warningUsages.size()==1&&plan.warningUsages.front().ownerId==sprite.id,
        "Sprite image warnings counted transitive effects as direct image owners.");
    finishDeletion(library,resources,plan);const auto cleared=loadMaterialAsset(p.root,sprite.id);
    require(cleared.surface.baseColorTexture.image.empty()&&cleared.blend==MaterialBlend::Alpha&&library.references().resolveParticleTexture(image).empty(),
        "Sprite image deletion lost blending or failed to persist procedural fallback.");
    require(library.preparedParticleMaterial(sprite.id)->ready,"Cleared sprite image did not become a usable procedural material.");
    auto pbr=defaultMaterialAsset();pbr.id="assets/materials/pbr.crimson-material";pbr.name="PBR";saveMaterialAsset(p.root,pbr);library.refreshMaterials();
    const auto before=read(p.root/effect.id);const auto deletion=library.planDeletion({sprite.id});
    const auto candidates=library.fallbackCandidates(sprite.id);require(std::none_of(candidates.begin(),candidates.end(),[&](const auto& entry){return entry.id==pbr.id;}),"Sprite deletion offered an incompatible PBR fallback.");
    require(!library.deleteAssets(deletion,{{sprite.id,pbr.id}}),"Incompatible material fallback was published.");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(library.deletionPending()&&std::chrono::steady_clock::now()<deadline){resources.pump();(void)library.pumpDeletion(nullptr);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(!library.deletionPending()&&fs::exists(p.root/sprite.id)&&read(p.root/effect.id)==before&&library.references().resolveMaterial(sprite.id)==sprite.id,
        "Incompatible sprite material fallback changed authored files or the published policy.");
    require(!library.preparedParticleMaterial(pbr.id)->ready&&!library.preparedParticleMaterial(pbr.id)->diagnostic.empty(),"Explicit unsupported PBR sprite assignment was silently treated as a particle default.");
    const auto model=seedModel(p,true);library.refreshMaterials();require(library.modelMaterialsReady(*model),"Synchronously authored imported material metadata did not satisfy model readiness.");
    auto unsupported=loadMaterialAsset(p.root,model->materialIds.front());unsupported.shading=MaterialShading::Unlit;unsupported.blend=MaterialBlend::Additive;library.publishMaterial(std::make_shared<MaterialAsset>(unsupported));
    require(!library.modelMaterialsReady(*model)&&!library.modelMaterialsDiagnostic(*model).empty(),"Model renderer silently accepted a sprite-only material mode.");
}
void boxMaterialDeletionAndPublication(){
    Project p;
    auto red=defaultMaterialAsset();red.id="assets/materials/red.crimson-material";red.name="Red";red.surface.baseColor={1,.1f,.1f,1};saveMaterialAsset(p.root,red);
    auto blue=red;blue.id="assets/materials/blue.crimson-material";blue.name="Blue";blue.surface.baseColor={.1f,.1f,1,1};saveMaterialAsset(p.root,blue);
    auto sprite=createMaterialAsset(p.root,"Unsupported sprite");sprite.shading=MaterialShading::Unlit;sprite.blend=MaterialBlend::Additive;saveMaterialAsset(p.root,sprite);
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
        std::none_of(candidates.begin(),candidates.end(),[&](const auto& value){return value.id==sprite.id;}),"Box material deletion offered an incompatible sprite fallback.");
    require(!library.deleteAssets(plan,{{red.id,sprite.id}},&active) && fs::exists(p.root/red.id),"Incompatible Box material deletion modified files.");
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
}

int main(){try{
    // No renderer is initialized and no GPU preparation is advanced. The public
    // runtime AssetLibrary path, reference index and worker deletion pipeline
    // operate on disposable managed data and immutable CPU resources only.
    warningQueries();legacyEnvironmentGlowPersistence();archivedLegacyWarnings();embeddedMaterialWarningOwners();removeModelAcrossScenes(false);removeModelAcrossScenes(true);fallbackReopening();cancelAndStaleDeletion();activeSceneAndFolderDeletion();particleMaterialDeletionAndAuthority();spriteImageDeletionAndIncompatibleMaterial();boxMaterialDeletionAndPublication();boxMaterialHistoryOnlyDeletion();
    std::cout<<"Public AssetLibrary deletion, warning, fallback and document integration checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"AssetLibrary deletion test failed: "<<error.what()<<'\n';return 1;}}
