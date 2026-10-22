#include "scene/SceneComponents.h"
#include "material_fixtures.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialGraphReferences.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "assets/MaterialAsset.h"
#include "assets/BuiltinGeometry.h"
#include "assets/GrungeGenerator.h"
#include "resources/TextureStreamer.h"
#include "resources/StreamingModel.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "editor/PreparedMaterialSignature.h"
#include "editor/MaterialPreview.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "model_fixtures.h"
#include "quader_bundle_fixture.h"
#include <bgfx/platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
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
fs::path fixturePath(const fs::path& root,const std::string& id) {
    const auto file=resolveAssetPath(root,id);const auto ext=file.extension();
    if(contentStorageReady(root)&&!isAssetUuid(id)&&(ext==".png"||ext==".PNG"||ext==".hdr"||ext==".tga"||ext==".crimson-material"||ext==".crimson-particle"||ext==".crimson-scene"))registerAssetPath(root,id,file);
    return file;
}
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
    return std::any_of(library.catalog().begin(),library.catalog().end(),[&](const auto& asset){return asset.id==canonicalAssetId(id);});
}
void finishDeletion(AssetLibrary& library,ResourceManager& resources,const AssetDeletionPlan& plan,
                    const std::unordered_map<std::string,std::string>& replacements={},SceneDocument* active=nullptr){
    require(plan.error.empty(),plan.error);
    std::unordered_map<std::string,std::string> canonicalReplacements;
    for(const auto& [id,target]:replacements)canonicalReplacements.emplace(canonicalAssetId(id),canonicalAssetId(target));
    if(library.deleteAssets(plan,canonicalReplacements,active))return;
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
    writeFileAtomically(fixturePath(p.root,"assets/preview.json"),nlohmann::json{{"version",1},{"model",model->id}}.dump());
    writeFileAtomically(fixturePath(p.root,"assets/editor-session.json"),nlohmann::json{{"version",1},{"model",model->id}}.dump());
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto unsaved=library.planDeletion({canonicalAssetId(model->id)},&active);
    require(unsaved.error.empty()&&!unsaved.needsReferenceWarning()&&unsaved.clearsActiveHistory,
        "Unsaved placement, undo history or editor bookmarks triggered an authored dependency warning.");
    require(!unsaved.usages.empty(),"Suppressing dependency warnings discarded active/history impact information.");
    active.createModel(model->id,"A second instance");require(active.save("Saved instances"),active.status());library.refreshScenes();
    const auto saved=library.planDeletion({canonicalAssetId(model->id)},&active);
    require(saved.error.empty()&&saved.needsReferenceWarning()&&saved.warningUsages.size()==1&&saved.warningUsages.front().ownerId==active.assetId(),
        "Saved scene references were missing or counted once per entity/current history.");
    const auto together=library.planDeletion({canonicalAssetId(model->id),canonicalAssetId(active.assetId())},&active);
    require(together.error.empty()&&!together.needsReferenceWarning(),"A scene removed with its model was counted as a surviving owner.");
}
void bundledModelPublicationAndReferences() {
    Project project;auto fixture=bundle_test::writeFixture(project.root);
    auto assigned=createMaterialAsset(project.root,"Assigned bundle surface");
    SceneDocument scene(project.root);const auto entity=scene.createModel(fixture.modelId,"Placed mesh");
    auto changed=*scene.scene().find(entity);changed.modelMaterials={AssetRef<MaterialAsset>{assigned.id}};
    require(scene.updateEntity(entity,changed)&&scene.save("Bundled scene"),scene.status());
    const auto sceneFile=resolveAssetPath(project.root,scene.assetId()),cache=resolveAssetPath(project.root,fixture.modelId);
    const auto sceneBytes=read(sceneFile);
    {
        ResourceManager resources;AssetLibrary library(project.root,&resources);
        const auto waitModel=[&](const std::string& name) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
            std::shared_ptr<const ModelAsset> model;
            do {resources.pump();library.updateResources(0);model=library.findModel(fixture.modelId);
                if(model&&model->name==name)return model;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }while(std::chrono::steady_clock::now()<deadline);
            throw std::runtime_error("Bundled model publication did not finish: "+library.status());
        };
        const auto loaded=waitModel("Triangle");
        require(hasAsset(library,fixture.modelId)&&loaded->materialIds==std::vector<std::string>{DefaultMaterialId}&&
            library.resolvedModelMaterials(*loaded,changed.modelMaterials).front().baseColor==assigned.surface.baseColor,
            "Startup discovery lost a bundled model or its per-instance material assignment.");
        const auto plan=library.planDeletion({fixture.modelId},&scene);
        require(plan.error.empty()&&plan.assetIds==std::vector<std::string>{fixture.modelId}&&plan.needsReferenceWarning()&&plan.clearsActiveHistory&&
            !library.deletionPending()&&read(fixture.file)==encodeQuaderSceneBundle(fixture.bundle)&&read(sceneFile)==sceneBytes,
            "Bundled model deletion was blocked, lost saved/live reference warnings, or changed authored data while planning.");
        auto manifest=nlohmann::json::parse(fixture.bundle.models.front().manifest);manifest["name"]="Updated triangle";
        fixture.bundle.models.front().manifest=manifest.dump();
        writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));library.onFileChanged(fixture.file);
        const auto refreshed=waitModel("Updated triangle");
        require(refreshed->id==fixture.modelId&&refreshed->materialIds==loaded->materialIds&&resolveAssetPath(project.root,fixture.modelId)==cache&&
            scene.scene().find(entity)->modelMaterials==changed.modelMaterials&&!scene.dirty()&&read(sceneFile)==sceneBytes,
            "Bundle source replacement changed stable IDs, instance assignments, or the saved scene.");
    }
    AssetReferenceManager policy(project.root);const std::array removed{AssetEntry{fixture.modelId,"Updated triangle",AssetKind::Model}};
    policy.replace(removed,{});policy.save();
    fs::remove_all(project.root/".crimson/cache/quader-imports");initializeQuaderSceneBundles(project.root);
    ResourceManager resources;AssetLibrary reopened(project.root,&resources);
    require(!hasAsset(reopened,fixture.modelId)&&!reopened.findModel(fixture.modelId)&&reopened.references().deleted(fixture.modelId)&&read(sceneFile)==sceneBytes,
        "Regenerating a bundle resurrected a tombstoned model or rewrote saved references.");
}
void manualBundledModelDeletion(bool asynchronous){
    Project project;auto fixture=bundle_test::writeFixture(project.root);
    const auto material=createMaterialAsset(project.root,"Shared bundle surface");
    auto manifest=nlohmann::json::parse(fixture.bundle.models.front().manifest);manifest["materialAssets"]=nlohmann::json::array({material.id});
    fixture.bundle.models.front().manifest=manifest.dump();
    auto sibling=fixture.bundle.models.front();sibling.legacyManifest="assets/models/quader-bundle-sibling/manifest.json";
    const auto siblingBase=fs::path(sibling.legacyManifest).parent_path();
    const std::array<const char*,4> members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
    for(std::size_t index=0;index<members.size();++index)sibling.memberIds[index]=canonicalAssetId((siblingBase/members[index]).generic_string());
    sibling.id=sibling.memberIds[0];manifest["name"]="Sibling mesh";sibling.manifest=manifest.dump();fixture.bundle.models.push_back(sibling);
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));initializeQuaderSceneBundles(project.root);
    const auto removedModel=fixture.bundle.models.front();const auto locations=quaderBundleCacheLocations(project.root,fixture.bundle);
    const auto cache=quaderBundleCacheDirectory(project.root,fixture.bundle.id,fixture.modelId);
    writeFileAtomically(cache/"keep.txt","Unrelated file");const auto materialBytes=read(resolveAssetPath(project.root,material.id));
    SceneDocument scene(project.root);const auto owner=scene.createQuader("Removed Quader node"),mesh=scene.createModel(fixture.modelId,"Removed mesh");
    scene.select({mesh},mesh);require(scene.reparentSelection(owner),scene.status());scene.select({owner},owner);require(scene.eraseSelection(),scene.status());
    require(!scene.scene().contains(owner)&&!scene.scene().contains(mesh)&&scene.history().canUndo(),"The removed-node deletion fixture did not retain undo history.");
    {
        ResourceManager resources;AssetLibrary library(project.root,asynchronous?&resources:nullptr);
        // Keep the same loaded metadata and browser snapshot that a running
        // editor retains across the deletion command and its next frame.
        std::shared_ptr<const ModelAsset> displayed;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {resources.pump();library.updateResources(0);displayed=library.findModel(fixture.modelId);
            if(displayed)break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(std::chrono::steady_clock::now()<deadline);
        require(bool(displayed),"The manual bundled-deletion fixture did not load its displayed model.");
        (void)library.browserCatalog();
        const auto plan=library.planDeletion({fixture.modelId},&scene);
        require(plan.error.empty()&&plan.assetIds==std::vector<std::string>{fixture.modelId}&&!plan.needsReferenceWarning()&&plan.clearsActiveHistory,
            "Deleting a Quader node still left its remaining model blocked or produced a saved-owner warning for history alone.");
        finishDeletion(library,resources,plan,{},&scene);
        const auto remaining=decodeQuaderSceneBundle(read(fixture.file));
        require(remaining.models.size()==1&&remaining.models.front().id==sibling.id&&remaining.models.front().manifest==sibling.manifest&&
            remaining.models.front().gltf==sibling.gltf&&remaining.models.front().buffer==sibling.buffer&&remaining.models.front().memberIds==sibling.memberIds,
            "Deleting one bundled mesh removed or changed a sibling's immutable payload.");
        require(!hasAsset(library,fixture.modelId)&&hasAsset(library,sibling.id)&&library.references().deleted(fixture.modelId)&&!library.references().deleted(sibling.id)&&
            scene.history().size()==0&&!scene.undo()&&!scene.redo()&&scene.dirty(),
            "Manual bundled-model deletion retained its card/history or changed a surviving model and dirty state.");
        require(read(cache/"keep.txt")=="Unrelated file"&&read(resolveAssetPath(project.root,material.id))==materialBytes&&hasAsset(library,material.id),
            "Manual mesh deletion removed unrelated cache files or independently authored native materials.");
        const auto stored=storedAssetPaths(project.root);
        for(const auto& location:locations){
            const bool deleted=std::find(removedModel.memberIds.begin(),removedModel.memberIds.end(),location.id)!=removedModel.memberIds.end();
            require(fs::is_regular_file(location.file)!=deleted,"Manual bundled-model deletion retained a member file or removed a sibling file.");
            require(std::any_of(stored.begin(),stored.end(),[&](const auto& value){return value.id==location.id;})!=deleted,
                "Manual bundled-model deletion retained a member location or removed a sibling registration.");
        }
        for(std::size_t index=1;index<removedModel.memberIds.size();++index)require(!library.references().deleted(removedModel.memberIds[index]),
            "Manual model deletion created independent deletion policies for generated package members.");
        for(const auto& path:library.takeOwnWrites())library.onFileChanged(path);
        for(const auto& location:locations)library.onFileChanged(location.file);
        for(unsigned frame=0;frame<8;++frame) {
            resources.pump();library.updateResources(frame);library.processImports();(void)library.browserCatalog();
            require(!library.findModel(fixture.modelId)&&!library.peekModel(fixture.modelId)&&!library.modelLoading(fixture.modelId)&&
                library.modelRevision(fixture.modelId)==0&&library.modelDependencies(fixture.modelId).empty()&&
                library.modelMaterialsReady(*displayed)&&library.modelMaterialsDiagnostic(*displayed).empty()&&
                library.resolvedModelMaterials(*displayed).size()==1&&!hasAsset(library,fixture.modelId)&&hasAsset(library,sibling.id),
                "Continuing editor frames or delayed file notifications resurrected a deleted bundled model or broke retained material metadata.");
        }
        require(library.planDeletion({sibling.id},&scene).error.empty(),
            "A subsequent deletion plan could no longer inspect the surviving bundle member.");
    }
    initializeQuaderSceneBundles(project.root);AssetLibrary reopened(project.root);
    require(!hasAsset(reopened,fixture.modelId)&&hasAsset(reopened,sibling.id)&&!fs::exists(cache/"manifest.json")&&read(cache/"keep.txt")=="Unrelated file"&&
        reopened.references().valid()&&reopened.references().deleted(fixture.modelId),
        "Restart reconstructed an explicitly deleted bundled model, invalidated its reference policy, or lost unrelated bundle data.");
}
void bundledBatchDeletionContinuesFrames(bool loadModels){
    Project project;auto fixture=bundle_test::writeFixture(project.root);
    auto material=createMaterialAsset(project.root,"Batch-deleted shared surface");
    const auto sources=model_test::writeModelFixtures(project.root/"sources");
    const std::string image="assets/textures/batch-deleted.png";
    const auto imageFile=fixturePath(project.root,image);fs::create_directories(imageFile.parent_path());fs::copy_file(sources.image,imageFile);
    material_test::legacyFixture(material);material.surface.baseColorTexture.image.id=image;saveMaterialAsset(project.root,material);
    auto manifest=nlohmann::json::parse(fixture.bundle.models.front().manifest);manifest["materialAssets"]=nlohmann::json::array({material.id});
    fixture.bundle.models.front().manifest=manifest.dump();
    auto sibling=fixture.bundle.models.front();sibling.legacyManifest="assets/models/quader-batch-sibling/manifest.json";
    const auto siblingBase=fs::path(sibling.legacyManifest).parent_path();
    const std::array<const char*,4> members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
    for(std::size_t index=0;index<members.size();++index)sibling.memberIds[index]=canonicalAssetId((siblingBase/members[index]).generic_string());
    sibling.id=sibling.memberIds[0];fixture.bundle.models.push_back(sibling);
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));initializeQuaderSceneBundles(project.root);
    const auto locations=quaderBundleCacheLocations(project.root,fixture.bundle);
    SceneDocument scene(project.root);const auto owner=scene.createQuader("Deleted batch owner");
    for(const auto& id:{fixture.modelId,sibling.id}) {
        const auto mesh=scene.createModel(id,"Deleted batch mesh");scene.select({mesh},mesh);require(scene.reparentSelection(owner),scene.status());
    }
    scene.select({owner},owner);require(scene.eraseSelection(),scene.status());
    {
        ResourceManager resources;AssetLibrary library(project.root,&resources);
        // The pending case retains resource handles without publishing their
        // completions; the loaded case exercises immutable displayed metadata.
        std::array<std::shared_ptr<const ModelAsset>,2> displayed;
        const std::array ids{fixture.modelId,sibling.id};
        for(std::size_t index=0;index<ids.size();++index)displayed[index]=library.findModel(ids[index]);
        if(loadModels) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
            while(std::chrono::steady_clock::now()<deadline&&(!displayed[0]||!displayed[1])) {
                resources.pump();library.updateResources(0);
                for(std::size_t index=0;index<ids.size();++index)displayed[index]=library.findModel(ids[index]);
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            require(displayed[0]&&displayed[1],"Batch deletion did not load its displayed model metadata.");
        }
        const auto displayedMaterial=library.findMaterial(material.id);require(bool(displayedMaterial),"Batch deletion lost its shared material before deletion.");
        require(library.find(image)!=nullptr,"Batch deletion did not register its pending texture request.");
        (void)library.browserCatalog();
        const auto plan=library.planDeletion({fixture.modelId,sibling.id,material.id,canonicalAssetId(image)},&scene);
        finishDeletion(library,resources,plan,{},&scene);
        for(const auto& path:library.takeOwnWrites())library.onFileChanged(path);
        for(const auto& location:locations)library.onFileChanged(location.file);
        for(unsigned frame=0;frame<8;++frame) {
            resources.pump();library.updateResources(frame);library.processImports();(void)library.browserCatalog();
            library.collectUnusedQuaderModels(scene);
            require(!library.findModel(fixture.modelId)&&!library.findModel(sibling.id)&&!library.peekModel(fixture.modelId)&&
                !library.find(image)&&!hasAsset(library,fixture.modelId)&&!hasAsset(library,sibling.id)&&!hasAsset(library,material.id)&&
                library.findMaterial(material.id)->id==DefaultMaterialId&&!scene.history().canUndo(),
                "Batch deletion left live resource requests, browser cards or undo payloads referencing removed bundle members.");
            for(const auto& model:displayed)if(model)require(library.modelMaterialsReady(*model)&&
                library.modelMaterialsDiagnostic(*model).empty()&&library.resolvedModelMaterials(*model).size()==1,
                "A retained model snapshot could not resolve the shared material deleted in the same batch.");
        }
        require(!fs::exists(fixture.file),"Batch deletion retained a removed bundled payload.");
    }
    ResourceManager resources;AssetLibrary reopened(project.root,&resources);
    for(unsigned frame=0;frame<3;++frame){resources.pump();reopened.updateResources(frame);(void)reopened.browserCatalog();}
    require(!hasAsset(reopened,fixture.modelId)&&!hasAsset(reopened,sibling.id)&&!hasAsset(reopened,material.id)&&!reopened.find(image)&&
        reopened.references().valid()&&reopened.findMaterial(material.id)->id==DefaultMaterialId,
        "Restart resurrected batch-deleted bundle members, material or texture, or lost their fallback policy.");
}
void multiBundleBatchDeletionWithSurvivingFallbacks(){
    Project project;auto fixture=bundle_test::writeFixture(project.root);
    const auto sources=model_test::writeModelFixtures(project.root/"sources");
    const std::string image=canonicalAssetId("assets/textures/shared-deleted.png"),replacementImage=canonicalAssetId("assets/textures/shared-kept.png");
    const auto imageFile=fixturePath(project.root,"assets/textures/shared-deleted.png"),replacementImageFile=fixturePath(project.root,"assets/textures/shared-kept.png");
    fs::create_directories(imageFile.parent_path());fs::copy_file(sources.image,imageFile);fs::copy_file(sources.image,replacementImageFile);
    auto material=createMaterialAsset(project.root,"Deleted shared surface");material_test::legacyFixture(material);
    material.surface.baseColorTexture.image.id=image;saveMaterialAsset(project.root,material);
    auto replacementMaterial=createMaterialAsset(project.root,"Surviving replacement surface");material_test::legacyFixture(replacementMaterial);
    replacementMaterial.surface.baseColor={.25f,.5f,.75f,1};replacementMaterial.surface.baseColorTexture.image.id=image;saveMaterialAsset(project.root,replacementMaterial);
    const auto makeModel=[&](const QuaderSceneBundle& owner,const std::string& suffix){
        auto model=fixture.bundle.models.front();model.legacyManifest="assets/models/quader-batch-"+suffix+"/manifest.json";
        const auto base=fs::path(model.legacyManifest).parent_path();const std::array<const char*,4> members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
        for(std::size_t index=0;index<members.size();++index)model.memberIds[index]=canonicalAssetId((base/members[index]).generic_string());
        model.id=model.memberIds[0];auto manifest=nlohmann::json::parse(model.manifest);
        manifest["name"]=suffix;manifest["quader"]["project"]=owner.projectId;manifest["quader"]["scene"]=owner.sceneId;
        manifest["materialAssets"]=nlohmann::json::array({material.id});model.manifest=manifest.dump();return model;
    };
    auto first=fixture.bundle,second=fixture.bundle;second.sceneId="second-batch-scene";second.id=quaderSceneBundleId(second.projectId,second.sceneId);second.name="Second batch bundle";
    first.models={makeModel(first,"first-deleted"),makeModel(first,"first-survivor")};
    second.models={makeModel(second,"second-deleted"),makeModel(second,"second-survivor")};
    const auto secondFile=defaultQuaderSceneBundlePath(project.root,second);
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(first));writeFileAtomically(secondFile,encodeQuaderSceneBundle(second));initializeQuaderSceneBundles(project.root);
    const auto firstLocations=quaderBundleCacheLocations(project.root,first),secondLocations=quaderBundleCacheLocations(project.root,second);
    SceneDocument scene(project.root);const auto firstDeleted=scene.createModel(first.models[0].id,"First removed mesh"),secondDeleted=scene.createModel(second.models[0].id,"Second removed mesh");
    const auto firstSurvivor=scene.createModel(first.models[1].id,"First survivor"),secondSurvivor=scene.createModel(second.models[1].id,"Second survivor");
    auto assigned=*scene.scene().find(firstSurvivor);assigned.modelMaterials={AssetRef<MaterialAsset>{material.id}};require(scene.updateEntity(firstSurvivor,assigned),scene.status());
    require(scene.save("Multi-bundle mixed deletion"),scene.status());const auto sceneId=scene.assetId();
    assigned=*scene.scene().find(firstSurvivor);assigned.name="Unsaved survivor name";require(scene.updateEntity(firstSurvivor,assigned),scene.status());
    {
        ResourceManager resources;AssetLibrary library(project.root,&resources);
        (void)library.findModel(first.models[0].id);(void)library.findModel(second.models[0].id);
        (void)library.findModel(first.models[1].id);(void)library.findModel(second.models[1].id);(void)library.find(image);(void)library.findMaterial(replacementMaterial.id);
        const auto plan=library.planDeletion({first.models[0].id,second.models[0].id,material.id,image},&scene);
        require(plan.error.empty()&&plan.needsReferenceWarning(),plan.error);
        finishDeletion(library,resources,plan,{{material.id,replacementMaterial.id},{image,replacementImage}},&scene);
        require(!scene.scene().contains(firstDeleted)&&!scene.scene().contains(secondDeleted)&&scene.scene().contains(secondSurvivor)&&
            scene.scene().find(firstSurvivor)->name=="Unsaved survivor name"&&scene.scene().find(firstSurvivor)->modelMaterials.front().id==replacementMaterial.id&&scene.dirty()&&!scene.history().canUndo(),
            "Multi-bundle batch deletion lost live survivors, unsaved edits or fallback assignments, or retained invalid history.");
        const auto saved=readSceneSnapshot(project.root,sceneId);
        require(record(saved,firstSurvivor).name=="First survivor"&&record(saved,firstSurvivor).modelMaterials.front().id==replacementMaterial.id&&
            std::none_of(saved.records.begin(),saved.records.end(),[&](const auto& value){return value.id==firstDeleted||value.id==secondDeleted;}),
            "Multi-bundle batch deletion did not atomically rewrite saved references while preserving unrelated live edits.");
        const auto rewritten=loadMaterialAsset(project.root,replacementMaterial.id);
        require(rewritten.surface.baseColor==replacementMaterial.surface.baseColor&&rewritten.surface.baseColorTexture.image.id==replacementImage,
            "A surviving replacement material retained a texture deleted in the same batch or lost its authored factors.");
        // Keep notifications delayed until surviving model requests have resumed;
        // both pre-deletion worker completions and later watcher events must be safe.
        auto delayed=library.takeOwnWrites();
        for(const auto& location:firstLocations)delayed.push_back(location.file);
        for(const auto& location:secondLocations)delayed.push_back(location.file);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};bool ready=false;
        do {
            resources.pump();library.updateResources(0);library.processImports();library.collectUnusedQuaderModels(scene);(void)library.browserCatalog();
            const auto a=library.findModel(first.models[1].id),b=library.findModel(second.models[1].id);
            ready=a&&b&&library.modelMaterialsReady(*a)&&library.modelMaterialsReady(*b);
            if(ready){for(const auto& value:{a,b}){const auto surfaces=library.resolvedModelMaterials(*value);
                require(surfaces.size()==1&&surfaces.front().baseColor==replacementMaterial.surface.baseColor&&
                    library.references().resolveTexture(surfaces.front().baseColorTexture.image.id)==replacementImage,
                    "A surviving bundled model could not resolve material and texture replacements from the same deletion batch.");}}
            if(!delayed.empty()&&ready){for(const auto& file:delayed)library.onFileChanged(file);delayed.clear();ready=false;}
            else if(ready)break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(std::chrono::steady_clock::now()<deadline);
        require(ready,"Surviving models did not recover after mixed multi-bundle deletion and delayed notifications: "+library.status());
        const auto locations=storedAssetPaths(project.root);
        for(const auto* owner:{&first,&second}){
            const auto file=owner==&first?fixture.file:secondFile;const auto current=decodeQuaderSceneBundle(read(file));
            require(current.models.size()==1&&current.models.front().id==owner->models[1].id&&current.models.front().buffer==owner->models[1].buffer,
                "Mixed multi-bundle deletion changed a surviving sibling or retained a selected mesh.");
            for(const auto& id:owner->models[0].memberIds)require(std::none_of(locations.begin(),locations.end(),[&](const auto& value){return value.id==id;}),
                "Mixed multi-bundle deletion retained a selected member location.");
        }
    }
    ResourceManager resources;AssetLibrary reopened(project.root,&resources);
    for(const auto* owner:{&first,&second})require(!hasAsset(reopened,owner->models[0].id)&&hasAsset(reopened,owner->models[1].id)&&
        loadModelPackage(resolveAssetPath(project.root,owner->models[1].id),project.root,reopened.modelResolvers())->materials.front().baseColor==replacementMaterial.surface.baseColor,
        "Restart lost a surviving bundle sibling or revived a selected mesh after mixed deletion.");
    require(reopened.references().valid()&&!hasAsset(reopened,material.id)&&!reopened.find(image)&&hasAsset(reopened,replacementMaterial.id)&&reopened.find(replacementImage),
        "Restart lost mixed-batch material/texture fallbacks or revived deleted native assets.");
}
void externallyChangedDeletionPolicy(unsigned phase){
    Project project;const auto fixture=bundle_test::writeFixture(project.root);SceneDocument scene(project.root);
    const auto mesh=scene.createModel(fixture.modelId,"Preserved mesh");require(scene.save("Policy edit during deletion"),scene.status());
    ResourceManager resources;AssetLibrary library(project.root,&resources);
    const auto sourceBytes=read(fixture.file),indexBytes=read(project.root/".crimson/asset-locations.json"),sceneBytes=read(resolveAssetPath(project.root,scene.assetId()));
    const auto cacheFile=resolveAssetPath(project.root,fixture.modelId);const auto cacheBytes=read(cacheFile);
    const auto policyFile=resolveAssetPath(project.root,"assets/references.json");const auto externalId=canonicalAssetId("assets/textures/externally-deleted.png");
    const auto external=nlohmann::json{{"version",1},{"deleted",nlohmann::json::array({externalId})},{"replacements",{{externalId,GrayTextureId}}}}.dump(2)+"\n";
    if(phase==0)writeFileAtomically(policyFile,external);
    const auto plan=library.planDeletion({fixture.modelId},&scene);
    if(phase==0)require(!plan.error.empty(),"Deletion planning accepted an externally changed policy missing from its in-memory manager.");
    else {
        require(plan.error.empty(),plan.error);
        if(phase==2)require(!library.deleteAssets(plan,{},&scene)&&library.deletionPending(),"Policy-conflict fixture did not start background validation.");
        writeFileAtomically(policyFile,external);library.onFileChanged(policyFile);
        require(!(phase==2?library.pumpDeletion(&scene):library.deleteAssets(plan,{},&scene))&&!library.deletionPending(),
            "Deletion committed over a reference-policy edit made after planning or during validation.");
    }
    require(read(policyFile)==external&&read(fixture.file)==sourceBytes&&read(cacheFile)==cacheBytes&&read(project.root/".crimson/asset-locations.json")==indexBytes&&
        read(resolveAssetPath(project.root,scene.assetId()))==sceneBytes&&scene.scene().contains(mesh)&&!library.references().deleted(fixture.modelId),
        "Rejected policy-conflict deletion changed bundle, cache, index, saved/live scene or the external policy.");
    require(!library.planDeletion({fixture.modelId},&scene).error.empty(),"Replanning with a stale policy manager silently accepted an external policy edit.");
    AssetLibrary reopened(project.root,&resources);require(reopened.references().deleted(externalId)&&reopened.planDeletion({fixture.modelId},&scene).error.empty(),
        "Reopening did not retain the external tombstone and allow a fresh deletion plan.");
}
void lastBundledMeshFolderCleanup(bool keepUnrelated){
    Project project;auto fixture=bundle_test::writeFixture(project.root);
    const auto sceneDirectory=project.root/"Content/diablo_room",modelDirectory=sceneDirectory/"models";
    const auto bundleFile=modelDirectory/"diablo_room.crimson-quader";
    fs::create_directories(modelDirectory);fs::rename(fixture.file,bundleFile);registerAssetPath(project.root,fixture.bundle.id,bundleFile);
    fs::remove(fixture.file.parent_path());fixture.file=bundleFile;initializeQuaderSceneBundles(project.root);
    if(keepUnrelated){writeFileAtomically(modelDirectory/"keep.txt","Unrelated authored bytes");fs::create_directory(modelDirectory/"authored_empty_folder");}
    SceneDocument scene(project.root);ResourceManager resources;
    {
        AssetLibrary library(project.root,&resources);const auto folder=library.browserCatalog().parent(fixture.modelId);
        require(library.browserCatalog().folderPath(folder)==modelDirectory,"Imported mesh was not presented in its physical models directory.");
        const auto plan=library.planDeletion({fixture.modelId},&scene);finishDeletion(library,resources,plan,{},&scene);
        require(!fs::exists(bundleFile)&&!hasAsset(library,fixture.modelId)&&!library.references().deleted(fixture.bundle.id),
            "Deleting the last mesh retained its empty bundle or tombstoned the stable source-bundle identity.");
        const auto locations=storedAssetPaths(project.root);
        require(std::none_of(locations.begin(),locations.end(),[&](const auto& value){return value.id==fixture.bundle.id;}),
            "Deleting the last mesh retained the empty bundle's file registration.");
        if(keepUnrelated)require(read(modelDirectory/"keep.txt")=="Unrelated authored bytes"&&fs::is_directory(modelDirectory/"authored_empty_folder")&&library.browserCatalog().isFolder(folder),
            "Automatic empty-folder cleanup removed unrelated files/directories or their browser folder.");
        else require(!fs::exists(modelDirectory)&&!fs::exists(sceneDirectory)&&!library.browserCatalog().isFolder(folder)&&fs::is_directory(project.root/"Content"),
            "Deleting the last mesh retained empty import ancestors or their browser folders, or removed Content.");
    }
    AssetLibrary reopened(project.root,&resources);require(!hasAsset(reopened,fixture.modelId)&&!fs::exists(bundleFile),
        "Restart reconstructed an explicitly deleted last mesh or its removed source bundle.");
}
void referencedBundledModelDeletion(bool replacement){
    Project project;const auto fixture=bundle_test::writeFixture(project.root);const auto fallback=seedModel(project);
    const auto fallbackId=canonicalAssetId(fallback->id);
    SceneDocument active(project.root);const auto owner=active.createQuader("Kept Quader node"),mesh=active.createModel(fixture.modelId,"Placed mesh");
    active.select({mesh},mesh);require(active.reparentSelection(owner),active.status());
    const auto child=active.createPointLight("Retained manual child");active.select({child},child);require(active.reparentSelection(mesh),active.status());
    const auto childWorld=*active.scene().worldTransform(child);require(active.save("Referenced bundle"),active.status());
    const auto sceneId=active.assetId();SceneDocument saved(project.root);const auto other=saved.createModel(fixture.modelId,"Saved mesh");require(saved.save("Other bundle reference"),saved.status());
    auto edited=*active.scene().find(child);edited.name="Unsaved retained light";require(active.updateEntity(child,edited),active.status());
    ResourceManager resources;AssetLibrary library(project.root,&resources);const auto plan=library.planDeletion({fixture.modelId},&active);
    require(plan.error.empty()&&plan.scenes.size()==2&&plan.warningUsages.size()==2&&plan.clearsActiveHistory,
        "Bundled-model deletion failed to report saved scenes and active/history references through the ordinary reference policy.");
    finishDeletion(library,resources,plan,replacement?std::unordered_map<std::string,std::string>{{fixture.modelId,fallback->id}}:std::unordered_map<std::string,std::string>{},&active);
    const auto first=readSceneSnapshot(project.root,sceneId),second=readSceneSnapshot(project.root,saved.assetId());
    require(active.scene().contains(owner)&&active.scene().find(child)->name=="Unsaved retained light"&&record(first,child).name=="Retained manual child"&&active.dirty(),
        "Deleting a referenced bundled mesh removed its Quader node or saved unrelated live edits.");
    if(replacement){
        require(active.scene().find(mesh)->model->id==fixture.modelId&&record(first,mesh).model->id==fixture.modelId&&record(second,other).model->id==fixture.modelId&&
            library.references().resolve(active.scene().find(mesh)->model->id)==fallbackId&&library.references().resolve(record(first,mesh).model->id)==fallbackId&&
            library.references().resolve(record(second,other).model->id)==fallbackId,
            "Bundled-model deletion changed authored model identities or failed to resolve live and saved references through the chosen fallback policy.");
        std::shared_ptr<const ModelAsset> resolved;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {resources.pump();library.updateResources(0);resolved=library.findModel(fixture.modelId);
            if(resolved)break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(std::chrono::steady_clock::now()<deadline);
        require(resolved&&canonicalAssetId(resolved->id)==fallbackId,"Loading a deleted bundled-model reference did not prepare its chosen replacement model.");
    }else{
        require(!active.scene().contains(mesh)&&active.scene().find(child)->parent==owner&&
            std::none_of(first.records.begin(),first.records.end(),[&](const auto& value){return value.id==mesh;})&&
            std::none_of(second.records.begin(),second.records.end(),[&](const auto& value){return value.id==other;})&&active.history().size()==0,
            "Bundled-model deletion did not remove model references and clear invalid history while preserving children.");
    }
    nearMatrix(*active.scene().worldTransform(child),childWorld);
    require(!fs::exists(fixture.file)&&!hasAsset(library,fixture.modelId)&&hasAsset(library,fallback->id),
        "Deleting the last bundled mesh left its payload published or removed an independent replacement model.");
    AssetReferenceManager reopened(project.root);
    require(reopened.valid()&&reopened.deleted(fixture.modelId)&&reopened.resolve(fixture.modelId)==(replacement?fallbackId:std::string{}),
        "Restart lost the model deletion/fallback policy after removing its generated location registration.");
}
void cancelAndStaleBundledDeletion(){
    Project project;const auto fixture=bundle_test::writeFixture(project.root);SceneDocument active(project.root);
    const auto owner=active.createQuader("Quader"),mesh=active.createModel(fixture.modelId,"Mesh");active.select({mesh},mesh);require(active.reparentSelection(owner),active.status());
    require(active.save("Linked bundle"),active.status());ResourceManager resources;AssetLibrary library(project.root,&resources);
    const auto sourceBytes=read(fixture.file),sceneBytes=read(resolveAssetPath(project.root,active.assetId())),indexBytes=read(project.root/".crimson/asset-locations.json");
    const auto cache=resolveAssetPath(project.root,fixture.modelId);const auto cacheBytes=read(cache),sceneId=active.assetId();
    auto plan=library.planDeletion({fixture.modelId},&active);require(plan.error.empty(),plan.error);
    require(!library.deleteAssets(plan,{},&active)&&library.deletionPending(),"Bundled model deletion did not start asynchronous validation.");
    library.cancelDeletion();require(!library.deletionPending()&&!library.pumpDeletion(&active),"Canceled bundled deletion retained a pending transaction.");
    for(unsigned i=0;i<30;++i){resources.pump();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(read(fixture.file)==sourceBytes&&read(resolveAssetPath(project.root,sceneId))==sceneBytes&&read(cache)==cacheBytes&&
        read(project.root/".crimson/asset-locations.json")==indexBytes&&!library.references().deleted(fixture.modelId)&&active.scene().contains(mesh),
        "Canceled bundled deletion changed source payload, cache, saved scene, location index or live entities.");
    plan=library.planDeletion({fixture.modelId},&active);require(!library.deleteAssets(plan,{},&active)&&library.deletionPending(),"Second bundled deletion did not start.");
    active.newScene();require(!library.pumpDeletion(&active)&&!library.deletionPending(),"A bundled deletion prepared for a replaced active scene was committed.");
    require(read(fixture.file)==sourceBytes&&read(resolveAssetPath(project.root,sceneId))==sceneBytes&&read(cache)==cacheBytes&&
        read(project.root/".crimson/asset-locations.json")==indexBytes&&!library.references().deleted(fixture.modelId)&&hasAsset(library,fixture.modelId),
        "Stale bundled deletion partially changed bundle, cache, saved scene, location index or deletion policy.");
}
void failedBundledDeletion(){
    Project project;const auto fixture=bundle_test::writeFixture(project.root);SceneDocument active(project.root);
    const auto mesh=active.createModel(fixture.modelId,"Mesh");require(active.save("Preserved bundle reference"),active.status());AssetLibrary library(project.root);
    const auto cache=resolveAssetPath(project.root,fixture.modelId);const auto cacheBytes=read(cache),sceneBytes=read(resolveAssetPath(project.root,active.assetId()));
    const auto indexBytes=read(project.root/".crimson/asset-locations.json");const auto historySize=active.history().size();
    const auto plan=library.planDeletion({fixture.modelId},&active);require(plan.error.empty(),plan.error);
    writeFileAtomically(fixture.file,"Invalid bundle changed after deletion planning");const auto invalidBytes=read(fixture.file);
    require(!library.deleteAssets(plan,{},&active)&&!library.deletionPending(),"Deleting a model from a corrupt/stale bundle unexpectedly succeeded.");
    require(read(fixture.file)==invalidBytes&&read(cache)==cacheBytes&&read(resolveAssetPath(project.root,active.assetId()))==sceneBytes&&
        read(project.root/".crimson/asset-locations.json")==indexBytes&&active.scene().contains(mesh)&&active.history().size()==historySize&&
        !active.dirty()&&!library.references().deleted(fixture.modelId)&&hasAsset(library,fixture.modelId),
        "Failed bundled deletion partially changed bundle bytes, generated members, saved/live scene, history, index or reference policy.");
}
void retiredQuaderModelCollection(bool asynchronous){
    Project project;auto fixture=bundle_test::writeFixture(project.root);
    const auto material=createMaterialAsset(project.root,"Retained wall material");
    auto manifest=nlohmann::json::parse(fixture.bundle.models.front().manifest);manifest["materialAssets"]=nlohmann::json::array({material.id});fixture.bundle.models.front().manifest=manifest.dump();
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));initializeQuaderSceneBundles(project.root);
    SceneDocument scene(project.root);const auto entity=scene.createModel(fixture.modelId,"Wall C");require(scene.save("Linked level"),scene.status());
    const auto savedId=scene.assetId(),savedBytes=read(resolveAssetPath(project.root,savedId));const auto cache=resolveAssetPath(project.root,fixture.modelId);
    ResourceManager resources;AssetLibrary library(project.root,asynchronous?&resources:nullptr);
    const auto waitModel=[&]{
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {resources.pump();library.updateResources(0);if(auto model=library.findModel(fixture.modelId))return model;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("Retained Quader model could not be loaded: "+library.status());
    };
    const auto loaded=waitModel();require(hasAsset(library,fixture.modelId),"Current source mesh was absent from the catalog.");
    fixture.bundle.models.front().current=false;writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));library.onFileChanged(fixture.file);
    require(!hasAsset(library,fixture.modelId),"A removed source mesh remained in the model catalog.");
    library.collectUnusedQuaderModels(scene);require(fs::is_regular_file(cache)&&read(resolveAssetPath(project.root,savedId))==savedBytes,"Cleanup removed referenced geometry or rewrote a saved scene.");
    const auto materialPlan=library.planDeletion({material.id},&scene);
    require(materialPlan.error.empty()&&materialPlan.needsReferenceWarning()&&std::any_of(materialPlan.usages.begin(),materialPlan.usages.end(),[&](const auto& usage){return usage.ownerId==fixture.modelId;}),
        "A hidden retained model lost its native material dependency or deletion warning.");
    const auto retained=waitModel();resources.pump();library.updateResources(0);
    require(retained->id==fixture.modelId&&!hasAsset(library,fixture.modelId),"Loading a retired model through a scene resurrected its browser card.");
    fixture.bundle.models.front().current=true;writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));
    const std::array publish{loaded};library.publishManagedModels(publish);
    require(hasAsset(library,fixture.modelId),"Reactivating an already loaded model did not restore its catalog card.");
    fixture.bundle.models.front().current=false;writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));library.publishManagedModels({});
    require(!hasAsset(library,fixture.modelId),"An empty source-model publication did not retire removed cards.");
    scene.newScene();library.collectUnusedQuaderModels(scene);
    require(fs::is_regular_file(cache),"An unopened saved scene did not retain its retired model payload.");
    require(scene.load(savedId),scene.status());scene.select({entity},entity);require(scene.eraseSelection()&&scene.save(),scene.status());
    library.collectUnusedQuaderModels(scene);require(fs::is_regular_file(cache),"Saving removed geometry discarded the Undo payload.");
    require(scene.undo()&&scene.scene().find(entity)&&scene.redo(),"Retired model collection broke scene undo/redo.");
    library.collectUnusedQuaderModels(scene);require(fs::is_regular_file(cache),"Redo history did not retain retired model geometry.");
    require(scene.reloadFromDisk(),scene.status());
    const auto broken=fixturePath(project.root,"assets/scenes/broken.crimson-scene");writeFileAtomically(broken,"{broken");
    library.collectUnusedQuaderModels(scene);require(fs::is_regular_file(cache)&&library.status().find("cleanup deferred")!=std::string::npos,"An unreadable saved scene did not defer physical model cleanup.");
    fs::remove(broken);library.refreshScenes();library.collectUnusedQuaderModels(scene);
    require(!fs::exists(cache)&&!fs::exists(fixture.file)&&!library.references().deleted(fixture.modelId)&&!scene.dirty(),
        "Unused retired geometry was retained, tombstoned or dirtied the active scene.");
    const auto paths=storedAssetPaths(project.root);require(std::none_of(paths.begin(),paths.end(),[&](const auto& path){return path.id==fixture.modelId;}),"Retired mesh cleanup left a dead asset location.");
    initializeQuaderSceneBundles(project.root);require(!fs::exists(cache),"Startup reconstruction resurrected a collected source mesh.");
    // Returning unchanged source geometry reuses its identity without a user
    // deletion tombstone. Restore through ordinary managed publication in the
    // same ResourceManager, while the former immutable reader is still pinned.
    fixture.bundle.models.front().current=true;
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));initializeQuaderSceneBundles(project.root);
    library.publishManagedModels(publish);
    const auto returned=waitModel();
    require(returned&&returned->id==fixture.modelId&&loaded->id==fixture.modelId&&hasAsset(library,fixture.modelId)&&
        !library.references().deleted(fixture.modelId)&&fs::is_regular_file(cache),
        "Identical source geometry could not return under its collected UUID in the same resource session.");
}
void retiredQuaderPolicyRetention(){
    Project project;auto fixture=bundle_test::writeFixture(project.root);fixture.bundle.models.front().current=false;
    writeFileAtomically(fixture.file,encodeQuaderSceneBundle(fixture.bundle));
    const auto cache=resolveAssetPath(project.root,fixture.modelId),policyFile=resolveAssetPath(project.root,"assets/references.json");
    const auto first=canonicalAssetId("assets/models/deleted-source/manifest.json"),second=canonicalAssetId("assets/models/deleted-middle/manifest.json");
    registerAssetPath(project.root,"assets/models/deleted-source/manifest.json",project.root/"Content/models/deleted-source/manifest.json");
    registerAssetPath(project.root,"assets/models/deleted-middle/manifest.json",project.root/"Content/models/deleted-middle/manifest.json");
    const std::array firstAsset{AssetEntry{first,"Deleted source",AssetKind::Model}},secondAsset{AssetEntry{second,"Deleted intermediate",AssetKind::Model}};
    const auto emptyPolicy=R"({"version":1,"deleted":[],"replacements":{}})";
    SceneDocument scene(project.root);
    {
        AssetLibrary library(project.root);AssetReferenceManager persisted(project.root);
        persisted.replace(firstAsset,{{first,second}});persisted.replace(secondAsset,{{second,fixture.modelId}});persisted.save();
        require(persisted.replacementTargets()==std::vector<std::string>{fixture.modelId},"Persistent replacement chains did not publish their surviving model target.");
        library.collectUnusedQuaderModels(scene);require(fs::is_regular_file(cache),"A newly saved replacement-only model owner was ignored by collection.");
        writeFileAtomically(policyFile,"{invalid policy");scene.newScene();library.collectUnusedQuaderModels(scene);
        require(fs::is_regular_file(cache)&&library.status().find("policy is invalid")!=std::string::npos,"Malformed persisted policy did not preserve retired mesh payloads.");
        writeFileAtomically(policyFile,emptyPolicy);library.references().replace(firstAsset,{{first,fixture.modelId}});
        library.collectUnusedQuaderModels(scene);require(fs::is_regular_file(cache),"An in-memory replacement-only model owner was ignored by collection.");
    }
    {
        writeFileAtomically(policyFile,"{invalid policy");AssetLibrary blocked(project.root);
        require(!blocked.references().valid(),"Malformed policy did not expose invalid reference state.");
        writeFileAtomically(policyFile,emptyPolicy);blocked.collectUnusedQuaderModels(scene);
        require(fs::is_regular_file(cache),"An invalid loaded policy allowed physical model cleanup.");
    }
    AssetLibrary reopened(project.root);reopened.collectUnusedQuaderModels(scene);
    require(!fs::exists(cache)&&!fs::exists(fixture.file),"Released replacement-only owners still pinned unused mesh payloads.");
}

void legacyEnvironmentGlowPersistence(){
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto file=fixturePath(p.root,"assets/environment.json");
    writeFileAtomically(file,R"({"version":1,"map":"","enabled":false,"intensity":2.5,"rotation":35,"toneMapping":3})");
    EnvironmentSettings environment;environment.glow={true,4,2,3};environment.source=EnvironmentSource::ProceduralSky;
    environment.background={true,true,{1,0,0}};environment.fog={true,{1,0,0},.5f};
    environment.fog.mode=FogMode::Depth;environment.fog.height=5;environment.fog.heightDensity=.3f;environment.fog.lightEnergy=4;
    environment.debanding=true;
    environment.ssgi={true,2.f,6.f,.2f,.9f,SsgiQuality::High,2};environment.ssao={true,1.2f,.6f,SsaoQuality::High};
    library.loadEnvironment(environment);
    require(!environment.glow.enabled && environment.glow==GlowSettings{} && environment.toneMapping==ToneMapping::AgX && environment.intensity==2.5f,
        "Legacy environment without Glow did not retain its old appearance and default Glow off.");
    require(environment.source==EnvironmentSource::Hdri && environment.procedural==ProceduralSkySettings{} &&
        environment.background==EnvironmentBackground{} && environment.fog==FogSettings{} && !environment.debanding,
        "Legacy environment gained procedural lighting, a visible/custom background or fog.");
    require(environment.ssgi==SsgiSettings{}&&environment.ssao==SsaoSettings{},"Legacy environment without GI must load disabled defaults.");
    const auto oldGlow=nlohmann::json{{"version",1},{"glow",{{"enabled",true},{"strength",2.75f},{"radius",3.25f},{"threshold",.5f}}}}.dump();
    writeFileAtomically(file,oldGlow);library.loadEnvironment(environment);
    require(environment.glow.enabled && environment.glow.strength==2.75f && environment.glow.radius==3.25f &&
        environment.glow.threshold==.5f && read(file)==oldGlow,"Legacy Glow tuning changed or its file was rewritten on load.");
    environment=EnvironmentSettings{};
    environment.glow={true,2.25f,3.5f,4.75f};environment.source=EnvironmentSource::ProceduralSky;
    environment.glow.tint={.4f,.6f,.8f};environment.glow.saturation=1.3f;environment.glow.hdrScale=.25f;
    environment.glow.blendMode=GlowBlendMode::SoftLight;environment.glow.mix=.35f;
    environment.glow.anamorphic=.3f;environment.glow.quality=GlowQuality::Low;environment.glow.levels={0,.2f,.4f,.6f,.8f,1,0};
    environment.procedural={{.1f,.2f,.3f},{.3f,.4f,.5f},{.6f,.7f,.8f},{.8f,.6f,.4f},.7f,.4f,1.5f,45.f,.6f};
    environment.background={false,true,{.12f,.23f,.34f}};environment.fog={true,{.4f,.5f,.6f},.025f};
    environment.fog.mode=FogMode::Depth;environment.fog.depthBegin=15;environment.fog.depthEnd=0;
    environment.fog.depthCurve=.7f;environment.fog.depthOpacity=.8f;
    environment.fog.height=-4;environment.fog.heightDensity=-.025f;environment.fog.lightEnergy=2.5f;
    environment.debanding=true;
    environment.ssgi={true,2.25f,7.5f,.12f,.65f,SsgiQuality::Low,3};environment.ssao={true,1.6f,1.3f,SsaoQuality::Low};
    environment.ssgi.quality=SsgiQuality::Custom;environment.ssgi.rays=5;environment.ssgi.steps=27;environment.ssgi.rayBias=.04f;
    environment.ssgi.denoiseRadius=2;environment.ssgi.denoiseStrength=.6f;environment.ssgi.radianceClamp=16;
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
    invalid=environment;invalid.glow.levels[6]=1.1f;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid Glow level replaced the saved legacy environment file.");
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
    invalid=environment;invalid.ssao.radius=0;library.saveEnvironment(invalid);
    require(read(file)==saved,"Invalid SSAO tuning replaced the saved legacy environment file.");
    auto noGi=nlohmann::json::parse(saved);noGi.erase("ssgi");noGi.erase("ssao");const auto noGiText=noGi.dump();writeFileAtomically(file,noGiText);
    library.loadEnvironment(reopened);auto noGiExpected=environment;noGiExpected.ssgi={};noGiExpected.ssao={};
    require(reopened==noGiExpected&&read(file)==noGiText,"Legacy file without screen-space lighting changed appearance or rewrote its source.");
    auto oneBounce=nlohmann::json::parse(saved);oneBounce["ssgi"].erase("bounces");const auto oneBounceText=oneBounce.dump();writeFileAtomically(file,oneBounceText);
    library.loadEnvironment(reopened);auto oneBounceExpected=environment;oneBounceExpected.ssgi.bounces=1;
    require(reopened==oneBounceExpected&&read(file)==oneBounceText,"Legacy GI without Bounces changed other tuning or rewrote its source.");
    auto previousGi=nlohmann::json::parse(saved);previousGi.erase("ssgi");
    previousGi["gi"]={{"enabled",true},{"strength",environment.ssgi.strength},{"bounces",environment.ssgi.bounces},{"method",1},
        {"screenSpace",{{"maxDistance",environment.ssgi.maxDistance},{"thickness",environment.ssgi.thickness},
            {"quality",static_cast<int>(environment.ssgi.quality)},{"temporalStability",environment.ssgi.temporalStability}}},
        {"dynamicWorld",{{"coverage",36},{"quality",2},{"temporalStability",.8}}}};
    previousGi["gi"]["screenSpace"]=nlohmann::json::parse(saved)["ssgi"];
    const auto previousGiText=previousGi.dump();writeFileAtomically(file,previousGiText);library.loadEnvironment(reopened);
    require(reopened==environment&&read(file)==previousGiText,"Removed GI wrapper conversion lost screen-space tuning or rewrote authored data.");
    library.saveEnvironment(reopened);const auto convertedGi=nlohmann::json::parse(read(file));
    require(convertedGi.contains("ssgi")&&convertedGi.contains("ssao")&&!convertedGi.contains("gi"),"Environment save retained the removed method representation.");
    auto badGi=nlohmann::json::parse(saved);badGi["ssao"]["quality"]=1.5;const auto badGiText=badGi.dump();writeFileAtomically(file,badGiText);
    auto retained=environment;library.loadEnvironment(retained);
    require(retained==environment&&read(file)==badGiText,"Invalid legacy SSAO replaced working values or rewrote its source.");
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
    fs::create_directories(fixturePath(p.root,"assets/textures"));fs::copy_file(fixtures.image,fixturePath(p.root,image));
    const auto legacyMaterial=nlohmann::json{{"version",2},{"roughnessMap",image}}.dump();
    const auto legacyEnvironment=nlohmann::json{{"version",1},{"map",image}}.dump();
    writeFileAtomically(fixturePath(p.root,"assets/material.json"),legacyMaterial);writeFileAtomically(fixturePath(p.root,"assets/environment.json"),legacyEnvironment);
    SceneDocument active(p.root);active.createModel(model->id,"Model");require(active.save("Scene"),active.status());
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto unused=library.planDeletion({canonicalAssetId(image)},&active);
    require(unused.error.empty()&&!unused.needsReferenceWarning(),"Archived sphere material/environment files triggered an asset dependency warning.");
    require(std::count_if(unused.usages.begin(),unused.usages.end(),[](const auto& use){return use.editorState;})==2,"Archived references were removed from full impact tracking.");
    const auto environment=active.createEnvironment();auto record=*active.scene().find(environment);record.environment->environment.map=image;
    record.environment->environment.source=EnvironmentSource::ProceduralSky;
    require(active.updateEntity(environment,record),active.status());require(active.save(),active.status());library.refreshScenes();
    const auto used=library.planDeletion({canonicalAssetId(image)},&active);
    require(used.error.empty()&&used.warningUsages.size()==1&&used.warningUsages.front().ownerId==active.assetId(),"A procedural Environment's retained HDRI dependency was hidden with legacy settings.");
    std::vector<std::string> all;for(const auto& asset:library.catalog())if(library.browserCatalog().mutableItem(asset.id))all.push_back(asset.id);
    const auto deletingAll=library.planDeletion(all,&active);
    require(deletingAll.error.empty()&&!deletingAll.needsReferenceWarning(),"Selecting all usable browser assets left a legacy or same-batch owner warning.");
    finishDeletion(library,resources,deletingAll,{},&active);
    require(read(fixturePath(p.root,"assets/material.json"))==legacyMaterial&&read(fixturePath(p.root,"assets/environment.json"))==legacyEnvironment,
        "Warning classification changed preserved legacy configuration files.");
    require(library.references().resolveTexture(image)==GrayTextureId,
        "Suppressing archived warnings changed the persisted texture replacement policy.");
}

void embeddedMaterialWarningOwners(){
    Project p;const auto model=seedModel(p);
    for(const auto& material:model->materialIds)fs::remove(fixturePath(p.root,material));
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto image=model->images.front();
    const auto imageOnly=library.planDeletion({canonicalAssetId(image)});
    require(imageOnly.error.empty()&&imageOnly.warningUsages.size()==1&&imageOnly.warningUsages.front().ownerId==model->id,
        "A model's embedded material was hidden or exposed as a missing standalone owner.");
    const auto withOwner=library.planDeletion({canonicalAssetId(image),canonicalAssetId(model->id)});
    require(withOwner.error.empty()&&!withOwner.needsReferenceWarning(),"Deleting a model with its image retained a nonexistent material owner warning.");
    // A native sidecar arriving before catalog refresh is still consumed through
    // its model; it must not become a second, unselectable warning owner.
    promoteModelMaterials(p.root,*model);
    const auto notYetCataloged=library.planDeletion({canonicalAssetId(image)});
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
    const auto retainedCollider=active.createCollider("Retained primitive collider");auto retained=*active.scene().find(retainedCollider);
    retained.collider->shape=ColliderShape::Sphere;retained.collider->radius=.8f;retained.collider->source=AssetRef<ModelAsset>{model->id};
    require(active.updateEntity(retainedCollider,retained),active.status());
    require(active.save("First scene"),active.status());const auto firstId=active.assetId();
    SceneDocument other(p.root);const auto otherModel=other.createModel(model->id,"Another saved instance");
    const auto trimesh=other.createCollider("Trimesh collider");auto triangle=*other.scene().find(trimesh);
    triangle.collider->shape=ColliderShape::Trimesh;triangle.collider->source=AssetRef<ModelAsset>{model->id};
    require(other.updateEntity(trimesh,triangle),other.status());
    const auto otherChild=other.createPointLight("Preserved light");other.select({otherChild});require(other.reparentSelection(otherModel),other.status());
    const auto otherWorld=*other.scene().worldTransform(otherChild);require(other.save("Second scene"),other.status());
    if(dirty){auto unsaved=*active.scene().find(child);unsaved.name="Unsaved child rename";require(active.updateEntity(child,unsaved),active.status());}
    require(active.dirty()==dirty,"Fixture clean/dirty state is wrong.");
    const auto materialBytes=read(fixturePath(p.root,model->materialIds.front()));const auto imageBytes=read(fixturePath(p.root,model->images.front()));
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto plan=library.planDeletion({canonicalAssetId(model->id)},&active);
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
    require(active.scene().find(retainedCollider)->collider->shape==ColliderShape::Sphere&&active.scene().find(retainedCollider)->collider->radius==.8f&&
        active.scene().find(retainedCollider)->collider->source.empty()&&record(first,retainedCollider).collider->source.empty()&&
        record(second,trimesh).collider->shape==ColliderShape::Trimesh&&record(second,trimesh).collider->source.empty(),
        "Model deletion failed to clear retained/active collider sources while preserving authored shapes and dimensions.");
    Scene firstScene;firstScene.apply(first.records,first.properties);nearMatrix(*firstScene.worldTransform(child),childWorld);
    Scene secondScene;secondScene.apply(second.records,second.properties);nearMatrix(*secondScene.worldTransform(otherChild),otherWorld);
    require(!fs::exists(fixturePath(p.root,model->id))&&!hasAsset(library,model->id),"Deleted model remained published in storage or the catalog.");
    require(read(fixturePath(p.root,model->materialIds.front()))==materialBytes&&read(fixturePath(p.root,model->images.front()))==imageBytes&&
        hasAsset(library,model->materialIds.front())&&hasAsset(library,model->images.front()),"Deleting a model removed independent material/image assets.");
    require(!active.externalFileChanged(),"The active scene fingerprint was not advanced to its externally remapped saved file.");
}

void fallbackReopening(){
    Project p;const auto model=seedModel(p);const auto fixtures=model_test::writeModelFixtures(p.root/"fallback-source");
    fs::create_directories(fixturePath(p.root,"assets/textures"));fs::copy_file(fixtures.image,fixturePath(p.root,"assets/textures/replacement.png"));
    const std::string imageFallback=canonicalAssetId("assets/textures/replacement.png");
    auto material=loadMaterialAsset(p.root,model->materialIds.front());material.id=canonicalAssetId("assets/materials/replacement.crimson-material");material.name="Replacement material";material_test::legacyFixture(material);
    material.surface.baseColor={.25f,.5f,.75f,1};material.surface.baseColorTexture.image=AssetRef<DecodedImage>{imageFallback};saveMaterialAsset(p.root,material);
    {
        ResourceManager resources;AssetLibrary library(p.root,&resources);
        const auto image=model->images.front();const auto plan=library.planDeletion({canonicalAssetId(image)});
        require(plan.error.empty()&&plan.needsReferenceWarning(),"A package image's actual material dependencies were not reported.");
        finishDeletion(library,resources,plan,{{image,imageFallback}});
        require(!fs::exists(fixturePath(p.root,image))&&library.references().resolveTexture(image)==imageFallback,"Texture fallback was not published.");
        const auto reopened=loadModelPackage(model->id,p.root,library.modelResolvers());
        require(library.references().resolveTexture(reopened->materials.front().baseColorTexture.image.id)==imageFallback,"Managed model reopening ignored a deleted package-image fallback.");
        const auto materialPlan=library.planDeletion({canonicalAssetId(model->materialIds.front())});
        finishDeletion(library,resources,materialPlan,{{model->materialIds.front(),material.id}});
        require(loadModelPackage(model->id,p.root,library.modelResolvers())->materials.front().baseColor==material.surface.baseColor,"Material fallback lost authored factors when reopening.");
        const auto normalUses=library.planDeletion({canonicalAssetId(model->images.at(1))});
        require(normalUses.error.empty()&&std::none_of(normalUses.warningUsages.begin(),normalUses.warningUsages.end(),[&](const auto& usage){return library.references().deleted(usage.ownerId);}),
            "A replaced imported material sidecar remained a ghost dependency owner.");
        finishDeletion(library,resources,library.planDeletion({canonicalAssetId(material.id)}));
        const auto defaults=loadModelPackage(model->id,p.root,library.modelResolvers())->materials.front();
        require(defaults.baseColor==Vec4{.5f,.5f,.5f,1}&&defaults.roughness==1&&defaults.metallic==0,"No-fallback material deletion did not use the requested default.");
        const auto normal=model->images.at(1);finishDeletion(library,resources,library.planDeletion({canonicalAssetId(normal)}));
        require(library.references().resolveTexture(normal,true)==NeutralNormalTextureId&&library.references().resolveTexture(normal)==GrayTextureId,"No-fallback texture defaults lost role semantics.");
    }
    ResourceManager resources;AssetLibrary reopened(p.root,&resources);
    const auto modelAgain=loadModelPackage(model->id,p.root,reopened.modelResolvers());
    require(!modelAgain->draws.empty()&&reopened.references().resolve(model->materialIds.front())==DefaultMaterialId,
        "Persisted deletion redirects failed on a fresh AssetLibrary.");
}

void cancelAndStaleDeletion(){
    Project p;const auto model=seedModel(p);SceneDocument active(p.root);const auto entity=active.createModel(model->id,"Model");require(active.save("Scene"),active.status());
    ResourceManager resources;AssetLibrary library(p.root,&resources);auto plan=library.planDeletion({canonicalAssetId(model->id)},&active);
    const auto savedId=active.assetId();const auto beforeModel=read(fixturePath(p.root,model->id)),beforeScene=read(fixturePath(p.root,savedId)),beforeBrowser=read(fixturePath(p.root,"assets/browser.json"));
    require(!library.deleteAssets(plan,{},&active)&&library.deletionPending(),"Runtime asset deletion did not defer validation.");
    library.cancelDeletion();require(!library.deletionPending()&&!library.pumpDeletion(&active),"Canceled deletion retained a pending publication.");
    for(unsigned i=0;i<30;++i){resources.pump();std::this_thread::sleep_for(std::chrono::milliseconds{1});}
    require(read(fixturePath(p.root,model->id))==beforeModel&&read(fixturePath(p.root,active.assetId()))==beforeScene&&read(fixturePath(p.root,"assets/browser.json"))==beforeBrowser&&
        !fs::exists(fixturePath(p.root,"assets/references.json"))&&active.scene().contains(entity),"Canceled deletion changed authored files or the active scene.");
    plan=library.planDeletion({canonicalAssetId(model->id)},&active);require(!library.deleteAssets(plan,{},&active)&&library.deletionPending(),"Second deletion failed to start.");
    active.newScene();require(!library.pumpDeletion(&active)&&!library.deletionPending(),"A deletion prepared for a replaced scene was published.");
    require(read(fixturePath(p.root,model->id))==beforeModel&&read(fixturePath(p.root,savedId))==beforeScene,"Stale deletion changed managed model or saved scene files.");
}

void activeSceneAndFolderDeletion(){
    Project p;const auto model=seedModel(p);SceneDocument active(p.root);active.createModel(model->id,"Retained model asset");require(active.save("Delete this scene"),active.status());
    const auto sceneId=active.assetId();const auto generation=active.documentGeneration();
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto folder=library.browserCatalog().createFolder(BrowserCatalog::ContentFolder,"Scenes");
    const auto childFolder=library.browserCatalog().createFolder(folder,"Nested");
    library.browserCatalog().move(std::array<std::string,1>{sceneId},childFolder);
    const auto plan=library.planDeletion({canonicalAssetId(folder)},&active);
    require(plan.error.empty()&&plan.assetIds==std::vector<std::string>{sceneId}&&plan.folderIds.size()==2,"Folder deletion did not expand nested content.");
    finishDeletion(library,resources,plan,{},&active);
    require(active.assetId().empty()&&active.scene().records().empty()&&active.dirty()&&active.history().size()==0&&active.documentGeneration()!=generation,
        "Deleting the active scene did not open a new empty unsaved document.");
    require(!fs::exists(fixturePath(p.root,sceneId))&&!hasAsset(library,sceneId)&&!library.browserCatalog().contains(folder)&&!library.browserCatalog().contains(childFolder),"Folder/scene deletion left browser entries or files.");
    require(fs::exists(fixturePath(p.root,model->id))&&hasAsset(library,model->id),"Deleting a scene deleted a referenced model asset.");
    BrowserCatalog reopened(p.root);reopened.synchronize(library.catalog());require(!reopened.contains(folder)&&!reopened.contains(sceneId),"Deleted folder contents reappeared on reopening.");
}
void particleMaterialDeletionAndAuthority(){
    Project p;auto material=createMaterialAsset(p.root,"Shared sprite");auto effect=createParticleAsset(p.root,"Sparks");
    material_test::legacyFixture(material);material.shading=MaterialShading::Unlit;material.blend=MaterialBlend::Additive;saveMaterialAsset(p.root,material);
    effect.emitter.material=AssetRef<MaterialAsset>{material.id};saveParticleAsset(p.root,effect);
    SceneDocument scene(p.root);const auto entity=scene.createParticle(effect.id,"Placed effect",math::translation({2,3,4}));require(scene.save("Particle scene"),scene.status());
    const auto sceneBytes=read(fixturePath(p.root,scene.assetId()));ResourceManager resources;AssetLibrary library(p.root,&resources);
    library.protectParticleDocument(effect.id);library.setParticleDocumentReferences({material.id},{material.id});
    const auto plan=library.planDeletion({canonicalAssetId(material.id)},&scene);
    require(plan.error.empty()&&plan.warningUsages.size()==1&&plan.warningUsages.front().ownerId==effect.id,
        "Saved effect material dependency was missing, counted per scene instance, or inflated by active/history usages.");
    require(std::any_of(plan.usages.begin(),plan.usages.end(),[](const auto& usage){return usage.historical;}),"Material deletion lost active particle history impact.");
    finishDeletion(library,resources,plan,{},&scene);
    require(!fs::exists(fixturePath(p.root,material.id))&&loadParticleAsset(p.root,effect.id).emitter.material.id==DefaultParticleMaterialId&&
        library.references().resolveParticleMaterial(material.id)==DefaultParticleMaterialId,"Material deletion did not atomically install the protected sprite fallback.");
    require(scene.scene().contains(entity)&&scene.scene().find(entity)->particle->id==effect.id&&read(fixturePath(p.root,scene.assetId()))==sceneBytes,
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
    auto external=committed;external.surface.baseColor={.8f,.7f,.6f,1};saveMaterialAsset(p.root,external);library.onFileChanged(fixturePath(p.root,user->id));
    require(library.materialRevision(user->id)==revision&&library.findMaterial(user->id)->surface.baseColor==committed.surface.baseColor,
        "Background reload replaced open material document authority.");
    library.protectMaterialDocument({});library.refreshMaterials();require(library.findMaterial(user->id)->surface.baseColor==external.surface.baseColor,"Closing material authority did not allow reload.");
    const auto good=library.preparedParticleMaterial(user->id);writeFileAtomically(fixturePath(p.root,user->id),"{invalid");library.refreshMaterials();const auto retained=library.preparedParticleMaterial(user->id);
    require(good->current&&retained->ready&&!retained->current&&!retained->diagnostic.empty()&&retained->signature==good->signature&&retained->render.baseColor==good->render.baseColor,
        "Failed material reload lost the immutable last-good renderer packet or its diagnostic.");
    saveMaterialAsset(p.root,external);library.refreshMaterials();require(library.preparedParticleMaterial(user->id)->diagnostic.empty(),"Corrected material reload retained a stale failure diagnostic.");
}
void spriteImageDeletionAndIncompatibleMaterial(){
    Project p;const auto fixtures=model_test::writeModelFixtures(p.root/"source");const std::string image="assets/textures/sprite.png";
    fs::create_directories(fixturePath(p.root,"assets/textures"));fs::copy_file(fixtures.image,fixturePath(p.root,image));
    auto sprite=createMaterialAsset(p.root,"Textured");material_test::legacyFixture(sprite);sprite.shading=MaterialShading::Unlit;sprite.blend=MaterialBlend::Alpha;sprite.surface.baseColorTexture.image=AssetRef<DecodedImage>{image};saveMaterialAsset(p.root,sprite);
    auto expectedGraph=loadMaterialAsset(p.root,sprite.id).graph;
    rewriteMaterialGraphTextures(expectedGraph,[&](const auto& id,MaterialTextureRole role){
        return id==canonicalAssetId(image)?std::string(role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId):id;
    });
    auto effect=createParticleAsset(p.root,"Smoke");effect.emitter.material=AssetRef<MaterialAsset>{sprite.id};saveParticleAsset(p.root,effect);
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto plan=library.planDeletion({canonicalAssetId(image)});
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
    auto pbr=defaultMaterialAsset();pbr.id=canonicalAssetId("assets/materials/pbr.crimson-material");pbr.name="PBR";saveMaterialAsset(p.root,pbr);library.refreshMaterials();
    const auto deletion=library.planDeletion({canonicalAssetId(sprite.id)});
    const auto candidates=library.fallbackCandidates(sprite.id);
    require(std::any_of(candidates.begin(),candidates.end(),[&](const auto& entry){return entry.id==pbr.id;}),"Shared material deletion excluded a valid Lit/Opaque replacement.");
    finishDeletion(library,resources,deletion,{{sprite.id,pbr.id}});
    require(!fs::exists(fixturePath(p.root,sprite.id))&&loadParticleAsset(p.root,effect.id).emitter.material.id==pbr.id&&library.references().resolveMaterial(sprite.id)==pbr.id,
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
    SceneDocument scene(p.root);const auto box=scene.createBox();auto entity=*scene.scene().find(box);entity.modelMaterials=ModelMaterialOverrides{AssetRef<MaterialAsset>{meshMaterial.id}};
    require(scene.updateEntity(box,entity)&&scene.save("Usage scene"),scene.status());
    auto effect=createParticleAsset(p.root,"Usage effect");effect.emitter.material.id=particleMaterial.id;saveParticleAsset(p.root,effect);
    const auto sceneBytes=read(fixturePath(p.root,scene.assetId())),effectBytes=read(fixturePath(p.root,effect.id));
    ResourceManager resources;AssetLibrary library(p.root,&resources);const auto geometry=library.findModel(BuiltinBoxModelId);
    require(geometry&&library.modelMaterialsDiagnostic(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{meshMaterial.id}}).empty(),"Surface fixture was not compatible before Usage changed.");
    require(library.preparedParticleMaterial(particleMaterial.id)->ready,"Particle fixture was not usable before Usage changed.");
    const auto savedSurface=meshMaterial,savedParticle=particleMaterial;
    materialOutputNode(meshMaterial.graph).output.shading=MaterialShading::Unlit;
    materialOutputNode(meshMaterial.graph).output.blend=MaterialBlend::Additive;canonicalizeMaterialAsset(meshMaterial);
    saveMaterialAsset(p.root,meshMaterial);library.publishMaterial(std::make_shared<MaterialAsset>(meshMaterial));
    const auto modelPacket=library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{meshMaterial.id}});
    require(library.modelMaterialsReady(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{meshMaterial.id}})&&library.modelMaterialsDiagnostic(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{meshMaterial.id}}).empty()&&
        modelPacket.front().baseColor==meshMaterial.surface.baseColor,"Shared mesh shader/blend edit installed an unrelated fallback.");
    materialOutputNode(particleMaterial.graph).output.shading=MaterialShading::StandardPbr;
    materialOutputNode(particleMaterial.graph).output.blend=MaterialBlend::Mask;canonicalizeMaterialAsset(particleMaterial);
    saveMaterialAsset(p.root,particleMaterial);library.publishMaterial(std::make_shared<MaterialAsset>(particleMaterial));
    const auto particlePacket=library.preparedParticleMaterial(particleMaterial.id);
    require(particlePacket->ready&&particlePacket->current&&particlePacket->diagnostic.empty()&&
        particlePacket->render.blend==MaterialBlend::Mask&&particlePacket->render.shading==MaterialShading::StandardPbr,
        "Sprite did not retain the assigned Lit/Masked material.");
    require(scene.scene().find(box)->modelMaterials.front().id==meshMaterial.id&&record(readSceneSnapshot(p.root,scene.assetId()),box).modelMaterials.front().id==meshMaterial.id&&
        loadParticleAsset(p.root,effect.id).emitter.material.id==particleMaterial.id&&read(fixturePath(p.root,scene.assetId()))==sceneBytes&&read(fixturePath(p.root,effect.id))==effectBytes&&
        library.references().resolveMaterial(meshMaterial.id)==meshMaterial.id&&library.references().resolveParticleMaterial(particleMaterial.id)==particleMaterial.id,
        "Shader/blend edit rewrote saved/live consumer references, source files or deletion aliases.");
    saveMaterialAsset(p.root,savedSurface);library.publishMaterial(std::make_shared<MaterialAsset>(savedSurface));
    saveMaterialAsset(p.root,savedParticle);library.publishMaterial(std::make_shared<MaterialAsset>(savedParticle));
    require(library.modelMaterialsDiagnostic(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{meshMaterial.id}}).empty()&&
        library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{meshMaterial.id}}).front().baseColor==savedSurface.surface.baseColor&&
        library.preparedParticleMaterial(particleMaterial.id)->ready&&library.preparedParticleMaterial(particleMaterial.id)->diagnostic.empty(),
        "Restoring shader/blend did not resume the existing saved assignments or clear compatibility diagnostics.");
}
void boxMaterialDeletionAndPublication(){
    Project p;initializeContentStorage(p.root);
    auto red=defaultMaterialAsset();red.id=canonicalAssetId("assets/materials/red.crimson-material");red.name="Red";red.surface.baseColor={1,.1f,.1f,1};saveMaterialAsset(p.root,red);
    auto blue=red;blue.id=canonicalAssetId("assets/materials/blue.crimson-material");blue.name="Blue";blue.surface.baseColor={.1f,.1f,1,1};saveMaterialAsset(p.root,blue);
    auto sprite=createMaterialAsset(p.root,"Unsupported sprite");material_test::legacyFixture(sprite);sprite.shading=MaterialShading::Unlit;sprite.blend=MaterialBlend::Additive;saveMaterialAsset(p.root,sprite);
    SceneDocument active(p.root);const auto box=active.createBox();auto entity=*active.scene().find(box);entity.modelMaterials=ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}};
    require(active.updateEntity(box,entity) && active.save("Box scene"),active.status());
    SceneDocument other(p.root);const auto otherBox=other.createBox();entity=*other.scene().find(otherBox);entity.modelMaterials=ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}};
    require(other.updateEntity(otherBox,entity) && other.save("Other Box scene"),other.status());
    entity=*active.scene().find(box);entity.name="Unsaved local name";require(active.updateEntity(box,entity),active.status());
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto geometry=library.findModel(BuiltinBoxModelId);require(bool(geometry),"Built-in Box was unavailable.");
    const auto original=geometry->materials.front().baseColor;
    require(library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}}).front().baseColor==red.surface.baseColor && library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{blue.id}}).front().baseColor==blue.surface.baseColor,
        "Per-instance Box material identities collided.");
    require(geometry->materials.front().baseColor==original && library.findModel(BuiltinBoxModelId)==geometry,"Instance assignment mutated or copied protected geometry.");
    library.protectMaterialDocument(red.id);auto committed=red;committed.surface.roughness=.31f;library.publishMaterial(std::make_shared<MaterialAsset>(committed));
    require(library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}}).front().roughness==.31f,"Committed material changes did not reach Box bindings.");
    auto external=red;external.surface.roughness=.87f;saveMaterialAsset(p.root,external);library.onFileChanged(fixturePath(p.root,red.id));
    require(library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}}).front().roughness==.31f,"Disk hot reload replaced open material authority.");
    library.protectMaterialDocument({});library.refreshMaterials();
    require(library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}}).front().roughness==.87f,"Closed material document did not resume resource publication.");
    const auto plan=library.planDeletion({canonicalAssetId(red.id)},&active);
    require(plan.error.empty() && plan.warningUsages.size()==2 && plan.scenes.size()==2 && plan.clearsActiveHistory,"Box material deletion did not discover saved/live/history references.");
    const auto candidates=library.fallbackCandidates(red.id);
    require(std::any_of(candidates.begin(),candidates.end(),[&](const auto& value){return value.id==blue.id;}) &&
        std::any_of(candidates.begin(),candidates.end(),[&](const auto& value){return value.id==sprite.id;}),"Box material deletion excluded a valid shared additive replacement.");
    finishDeletion(library,resources,plan,{{red.id,blue.id}},&active);
    const auto saved=readSceneSnapshot(p.root,active.assetId()),otherSaved=readSceneSnapshot(p.root,other.assetId());
    require(record(saved,box).modelMaterials.front().id==blue.id && record(otherSaved,otherBox).modelMaterials.front().id==blue.id,"Material deletion did not update every saved scene.");
    require(record(saved,box).name=="Box" && active.scene().find(box)->name=="Unsaved local name" && active.dirty(),"Material deletion saved or discarded unrelated live edits.");
    require(active.scene().find(box)->modelMaterials.front().id==blue.id && active.history().size()==0 && active.scene().contains(box),"Material deletion lost the entity or allowed deleted material undo.");
    require(library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}}).front().baseColor==blue.surface.baseColor,"Deleted material redirect did not resolve its replacement.");
    finishDeletion(library,resources,library.planDeletion({canonicalAssetId(blue.id)},&active),{},&active);
    require(active.scene().find(box)->modelMaterials.front().id==DefaultMaterialId && record(readSceneSnapshot(p.root,other.assetId()),otherBox).modelMaterials.front().id==DefaultMaterialId,
        "Default Box material fallback was not durable.");
    require(geometry->materials.front().baseColor==original && library.resolvedModelMaterials(*geometry,ModelMaterialOverrides{AssetRef<MaterialAsset>{red.id}}).front().baseColor==original,"Deleted override damaged shared geometry/default material.");
}
void importedMaterialSlotsAndDeletion(){
    Project p;const auto fixtures=model_test::writeModelFixtures(p.root/"sources");
    auto source=nlohmann::json::parse(read(fixtures.gltf));
    source["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_1");
    model_test::writeJson(fixtures.gltf,source);
    const auto model=importModelPackage(fixtures.gltf,p.root);promoteModelMaterials(p.root,*model);
    // No unused fallback is stored; both primitives retain their authored slot
    // identities and independent UV availability.
    require(model->materials.size()==2 && model->materialIds.size()==model->materials.size() && model->primitives.size()==2 &&
        model->primitives[0].material==0 && model->primitives[1].material==1 &&
        model->primitives[0].uvMask==1 && model->primitives[1].uvMask==3,"Imported material-slot fixture lost its distinct UV availability.");
    std::vector<std::pair<fs::path,std::string>> unchanged;
    for(const auto& folder:{p.root/"sources",(fixturePath(p.root,model->id)).parent_path()})
        for(const auto& file:fs::recursive_directory_iterator(folder))if(file.is_regular_file())unchanged.emplace_back(file.path(),read(file.path()));
    for(const auto& id:model->materialIds)unchanged.emplace_back(fixturePath(p.root,id),read(fixturePath(p.root,id)));
    const auto create=[&](const std::string& name,Vec4 color) {
        auto material=createMaterialAsset(p.root,name);material_test::legacyFixture(material);material.surface.baseColor=color;
        saveMaterialAsset(p.root,material);return loadMaterialAsset(p.root,material.id);
    };
    const auto red=create("Slot red",{1,.1f,.1f,1}),blue=create("Slot blue",{.1f,.1f,1,1}),gold=create("Slot gold",{1,.7f,.1f,1});
    const auto overrides=[](const std::string& first,const std::string& second) {
        return ModelMaterialOverrides{AssetRef<MaterialAsset>{first},AssetRef<MaterialAsset>{second}};
    };
    SceneDocument active(p.root);const auto first=active.createModel(model->id,"First imported instance"),second=active.createModel(model->id,"Second imported instance");
    auto entity=*active.scene().find(first);entity.modelMaterials=overrides(red.id,blue.id);require(active.updateEntity(first,entity),active.status());
    entity=*active.scene().find(second);entity.modelMaterials=overrides(blue.id,red.id);require(active.updateEntity(second,entity)&&active.save("Imported slots"),active.status());
    SceneDocument other(p.root);const auto otherEntity=other.createModel(model->id,"Other imported instance");
    entity=*other.scene().find(otherEntity);entity.modelMaterials=overrides(red.id,red.id);require(other.updateEntity(otherEntity,entity)&&other.save("Other imported slots"),other.status());
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto inherited=library.resolvedModelMaterials(*model);
    const auto resolvedFirst=library.resolvedModelMaterials(*model,active.scene().find(first)->modelMaterials);
    const auto resolvedSecond=library.resolvedModelMaterials(*model,active.scene().find(second)->modelMaterials);
    require(resolvedFirst[0].baseColor==red.surface.baseColor && resolvedFirst[1].baseColor==blue.surface.baseColor &&
        resolvedSecond[0].baseColor==blue.surface.baseColor && resolvedSecond[1].baseColor==red.surface.baseColor,
        "Per-slot assignments leaked across imported slots or instances.");
    entity=*active.scene().find(first);setModelMaterialOverride(entity.modelMaterials,0,{});require(active.updateEntity(first,entity),active.status());
    const auto clearedFirst=library.resolvedModelMaterials(*model,active.scene().find(first)->modelMaterials);
    require(active.scene().find(first)->modelMaterials.size()==2 && active.scene().find(first)->modelMaterials[0].empty() &&
        clearedFirst[0].baseColor==inherited[0].baseColor && clearedFirst[0].graph==inherited[0].graph && clearedFirst[1].baseColor==blue.surface.baseColor,
        "Clearing an interior override shifted slots or failed to inherit its source material.");
    require(active.undo() && active.scene().find(first)->modelMaterials==overrides(red.id,blue.id),"Undo did not restore the independent imported slot.");
    entity=*active.scene().find(first);setModelMaterialOverride(entity.modelMaterials,1,{});require(active.updateEntity(first,entity),active.status());
    const auto clearedLast=library.resolvedModelMaterials(*model,active.scene().find(first)->modelMaterials);
    require(active.scene().find(first)->modelMaterials.size()==1 && clearedLast[0].baseColor==red.surface.baseColor &&
        clearedLast[1].baseColor==inherited[1].baseColor && clearedLast[1].graph==inherited[1].graph,
        "Clearing the final override failed to restore the imported material.");
    require(active.undo() && active.scene().find(first)->modelMaterials==overrides(red.id,blue.id),"Undo lost a trailing imported material slot.");

    auto uvMaterial=createMaterialAsset(p.root,"UV1 slot");const auto output=materialOutputNode(uvMaterial.graph).id;
    const auto uv=addMaterialNode(uvMaterial.graph,MaterialNodeKind::UV).id,texture=addMaterialNode(uvMaterial.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(uvMaterial.graph,uv)->uv.mode=MaterialUvMode::UV1;
    findMaterialNode(uvMaterial.graph,texture)->texture.image.id=model->images.front();
    connectMaterialNodes(uvMaterial.graph,uv,MaterialPin::UV,texture,MaterialPin::UV);
    connectMaterialNodes(uvMaterial.graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    saveMaterialAsset(p.root,uvMaterial);canonicalizeMaterialAsset(uvMaterial);library.publishMaterial(std::make_shared<MaterialAsset>(uvMaterial));
    bool rejected=false;try{(void)library.resolvedModelMaterials(*model,overrides(uvMaterial.id,blue.id));}catch(const std::exception& error){rejected=std::string(error.what()).find("UV")!=std::string::npos;}
    require(rejected,"An imported slot accepted a material requiring its unavailable UV1 set.");
    require(library.resolvedModelMaterials(*model,overrides(red.id,uvMaterial.id))[1].graph==library.findMaterial(uvMaterial.id)->surface.graph,
        "UV validation incorrectly applied another slot's primitive UV requirements.");
    auto invalid=overrides(red.id,blue.id);
    const auto nonexistent=model->legacyUnusedMaterialSlot?*model->legacyUnusedMaterialSlot+1:model->materials.size();
    setModelMaterialOverride(invalid,nonexistent,AssetRef<MaterialAsset>{gold.id});
    rejected=false;try{(void)library.resolvedModelMaterials(*model,invalid);}catch(const std::exception&){rejected=true;}
    require(rejected && !library.modelMaterialsReady(*model,invalid) && !library.modelMaterialsDiagnostic(*model,invalid).empty(),
        "A nonexistent imported slot silently dropped its explicit material assignment.");

    entity=*active.scene().find(first);entity.name="Unsaved slot name";require(active.updateEntity(first,entity),active.status());
    const auto plan=library.planDeletion({canonicalAssetId(red.id)},&active);
    require(plan.error.empty() && plan.warningUsages.size()==2 && plan.scenes.size()==2 && plan.clearsActiveHistory,
        "Imported slot deletion missed saved/live/history owners or counted each slot as a separate saved owner.");
    finishDeletion(library,resources,plan,{{red.id,gold.id}},&active);
    const auto saved=readSceneSnapshot(p.root,active.assetId()),otherSaved=readSceneSnapshot(p.root,other.assetId());
    require(record(saved,first).modelMaterials==overrides(gold.id,blue.id) && record(saved,second).modelMaterials==overrides(blue.id,gold.id) &&
        record(otherSaved,otherEntity).modelMaterials==overrides(gold.id,gold.id),"Material deletion failed to replace every saved imported slot.");
    require(active.scene().find(first)->modelMaterials==overrides(gold.id,blue.id) && active.scene().find(second)->modelMaterials==overrides(blue.id,gold.id) &&
        active.history().size()==0 && !active.undo(),"Material deletion left a live slot or history path to the deleted material.");
    require(record(saved,first).name=="First imported instance" && active.scene().find(first)->name=="Unsaved slot name" && active.dirty(),
        "Imported slot deletion overwrote unrelated unsaved entity changes.");
    const auto redirected=library.resolvedModelMaterials(*model,overrides(red.id,red.id));
    require(redirected[0].baseColor==gold.surface.baseColor && redirected[1].baseColor==gold.surface.baseColor,
        "Deleted material aliases did not resolve independently in every imported slot.");
    for(const auto& [file,bytes]:unchanged)require(fs::exists(file) && read(file)==bytes,"Instance material edits or deletion rewrote imported geometry/source material files: "+file.string());
    require(model->materialIds[0]!=gold.id && model->materials[0].baseColor==inherited[0].baseColor && model->materials[1].baseColor==inherited[1].baseColor,
        "Instance material assignment mutated the shared imported model metadata.");
}
void legacyUnusedModelMaterialAssignments(){
    Project p;const auto fixtures=model_test::writeModelFixtures(p.root/"sources");
    auto source=nlohmann::json::parse(read(fixtures.gltf));source["materials"].erase(1);
    for(auto& primitive:source["meshes"][0]["primitives"])primitive["material"]=0;
    model_test::writeJson(fixtures.gltf,source);
    const auto imported=importModelPackage(fixtures.gltf,p.root);promoteModelMaterials(p.root,*imported);
    require(imported->materials.size()==1&&imported->materialIds.size()==1,"Single-material fixture created an unused fallback slot.");
    const auto manifestFile=fixturePath(p.root,imported->id);auto manifest=nlohmann::json::parse(read(manifestFile));
    manifest["format"]="Quader";
    manifest["materialAssets"]={imported->materialIds.front(),DefaultMaterialId};
    // Reproduce the former Quader package representation in disposable data.
    // The extra manifest entry was never referenced by either primitive.
    writeFileAtomically(manifestFile,manifest.dump(2));
    std::vector<std::pair<fs::path,std::string>> packageBytes;
    for(const auto& file:fs::recursive_directory_iterator(manifestFile.parent_path()))
        if(file.is_regular_file())packageBytes.emplace_back(file.path(),read(file.path()));
    const auto model=loadModelPackage(imported->id,p.root),metadata=loadModelMaterialMetadata(imported->id,p.root);
    for(const auto& loaded:{model,metadata})require(loaded->id==imported->id&&loaded->materials.size()==1&&
        loaded->materialIds==imported->materialIds&&loaded->legacyUnusedMaterialSlot==std::optional<std::size_t>{1},
        "Legacy Quader loading retained a phantom slot or changed the used material identity.");
    require(std::all_of(model->primitives.begin(),model->primitives.end(),[](const auto& primitive){return primitive.material==0;}),
        "Discarding a legacy unused fallback changed a real primitive's material index.");
    auto overrideMaterial=createMaterialAsset(p.root,"Preserved real slot");material_test::legacyFixture(overrideMaterial);
    overrideMaterial.surface.baseColor={.15f,.8f,.25f,1};saveMaterialAsset(p.root,overrideMaterial);
    overrideMaterial=loadMaterialAsset(p.root,overrideMaterial.id);
    const ModelMaterialOverrides authored{AssetRef<MaterialAsset>{overrideMaterial.id},
        AssetRef<MaterialAsset>{canonicalAssetId("assets/materials/unused-legacy-override.crimson-material")}};
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto usable=[&](const ModelAsset& asset,const ModelMaterialOverrides& assignments){
        const auto resolved=library.resolvedModelMaterials(asset,assignments);
        require(resolved.size()==1&&resolved.front().baseColor==overrideMaterial.surface.baseColor&&
            library.modelMaterialsReady(asset,assignments)&&library.modelMaterialsDiagnostic(asset,assignments).empty(),
            "A known legacy unused assignment hid the mesh, altered its real override or reported a missing phantom material.");
    };
    usable(*model,authored);usable(*metadata,authored);
    model->stream=std::make_shared<streaming::ModelStream>(streaming::buildModelCache(*model,p.root,false));
    const std::array<std::shared_ptr<const ModelAsset>,1> prepared{model};library.publishManagedModels(prepared);
    std::shared_ptr<const ModelAsset> published;
    const auto publicationDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    do {
        resources.pump();library.updateResources(0);published=library.findModel(model->id);
        if(published)break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(std::chrono::steady_clock::now()<publicationDeadline);
    require(published&&published->legacyUnusedMaterialSlot==model->legacyUnusedMaterialSlot&&
        published->stream==model->stream&&published->primitives.front().vertices.empty(),
        "Prepared-model publication lost legacy slot compatibility while stripping streamed geometry.");
    usable(*published,authored);
    while(!resources.idle()&&std::chrono::steady_clock::now()<publicationDeadline) {
        resources.pump();library.updateResources(0);std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    resources.pump();require(resources.idle(),"Legacy model metadata publication did not finish.");
    const auto warm=streaming::loadModelMetadataCache(manifestFile,p.root);
    require(warm&&warm->materialIds==model->materialIds&&warm->legacyUnusedMaterialSlot==model->legacyUnusedMaterialSlot,
        "Published legacy model metadata restored a phantom slot or lost its compatibility index.");
    usable(*warm,authored);
    const auto reopenedModel=loadModelPackage(imported->id,p.root);usable(*reopenedModel,authored);
    auto invalid=authored;setModelMaterialOverride(invalid,2,AssetRef<MaterialAsset>{overrideMaterial.id});
    bool rejected=false;try{(void)library.resolvedModelMaterials(*model,invalid);}catch(const std::exception&){rejected=true;}
    require(rejected&&!library.modelMaterialsReady(*model,invalid)&&!library.modelMaterialsDiagnostic(*model,invalid).empty(),
        "Legacy fallback compatibility accepted an unrelated out-of-range material assignment.");
    SceneDocument active(p.root);const auto first=active.createModel(model->id,"Legacy instance");
    auto entity=*active.scene().find(first);entity.modelMaterials=authored;
    require(active.updateEntity(first,entity)&&active.save("Legacy material slots"),active.status());
    const auto saved=read(fixturePath(p.root,active.assetId()));SceneDocument loaded(p.root);
    require(loaded.load(active.assetId())&&!loaded.dirty()&&loaded.scene().find(first)->modelMaterials==authored&&
        read(fixturePath(p.root,active.assetId()))==saved,
        "Loading a scene normalized away its authored legacy override vector or rewrote the scene.");
    loaded.select({first},first);require(loaded.duplicateSelection(),loaded.status());const auto copy=loaded.selection().primary;
    require(copy!=first&&loaded.scene().find(copy)->modelMaterials==authored,"Duplicating a legacy instance lost real or retained unused assignments.");
    require(loaded.undo()&&!loaded.scene().contains(copy)&&loaded.scene().find(first)->modelMaterials==authored,
        "Legacy instance duplication undo changed original material assignments.");
    require(loaded.redo()&&loaded.scene().find(copy)->modelMaterials==authored,"Legacy instance duplication redo lost material assignments.");
    require(loaded.save(),loaded.status());SceneDocument savedAgain(p.root);
    require(savedAgain.load(loaded.assetId())&&savedAgain.scene().find(first)->modelMaterials==authored&&
        savedAgain.scene().find(copy)->modelMaterials==authored,"Saving duplicated legacy assignments changed their slot indices.");
    SceneDocument runtime(p.root);require(runtime.resetTransientFrom(savedAgain),runtime.status());
    require(runtime.scene().find(first)->modelMaterials==authored,"Edit/Play copy lost a legacy material assignment.");
    auto runtimeEntity=*runtime.scene().find(first);setModelMaterialOverride(runtimeEntity.modelMaterials,0,AssetRef<MaterialAsset>{model->materialIds.front()});
    require(runtime.updateEntity(first,runtimeEntity)&&savedAgain.scene().find(first)->modelMaterials==authored&&
        runtime.scene().find(first)->modelMaterials[1]==authored[1],"Play material edits changed the authored vector or dropped its retained compatibility entry.");
    const auto extracted=extractScene(savedAgain.scene());
    require(extracted.models.size()==2,"Legacy slot compatibility removed a model during render extraction.");
    for(const auto& packet:extracted.models)usable(*model,packet.materialOverrides);
    for(const auto& [file,bytes]:packageBytes)require(read(file)==bytes,"Legacy fallback normalization rewrote an immutable model package.");
    // Current explicit packages contain only real material identities. They must
    // not inherit the legacy exception for a newly authored invalid assignment.
    manifest["materialAssets"]={imported->materialIds.front()};writeFileAtomically(manifestFile,manifest.dump(2));
    const auto current=loadModelPackage(imported->id,p.root);
    require(current->materials.size()==1&&!current->legacyUnusedMaterialSlot,"A current one-material Quader package retained a legacy phantom slot.");
    rejected=false;try{(void)library.resolvedModelMaterials(*current,authored);}catch(const std::exception&){rejected=true;}
    require(rejected&&!library.modelMaterialsReady(*current,authored)&&!library.modelMaterialsDiagnostic(*current,authored).empty(),
        "A current package silently accepted a material assignment outside its real slots.");
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
    fs::create_directories(fixturePath(p.root,"assets/textures"));fs::copy_file(fixtures.image,fixturePath(p.root,image));
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
        require(nlohmann::json::parse(read(fixturePath(p.root,material.id))).at("version")==6,"Dormant deletion fixture was not authored as a graph.");
    }
    ResourceManager resources;AssetLibrary library(p.root,&resources);
    library.setMaterialDocumentReferences({}, {image});
    const auto plan=library.planDeletion({canonicalAssetId(image)});
    require(plan.error.empty()&&plan.warningUsages.size()==2,"Dormant graph textures were omitted or duplicate roles inflated authored deletion owners.");
    require(std::any_of(plan.usages.begin(),plan.usages.end(),[](const auto& use){return use.historical;}),"Graph texture deletion omitted the open material's undo dependency.");
    finishDeletion(library,resources,plan);
    require(!fs::exists(fixturePath(p.root,image)),"Graph texture deletion did not remove the managed image.");
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
    fs::create_directories(fixturePath(p.root,"assets/textures"));fs::copy_file(fixtures.image,fixturePath(p.root,image));
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
    Project p;initializeContentStorage(p.root);auto material=defaultMaterialAsset();material.id=canonicalAssetId("assets/materials/history.crimson-material");material.name="History material";saveMaterialAsset(p.root,material);
    SceneDocument scene(p.root);const auto box=scene.createBox();require(scene.save("Default Box"),scene.status());const auto saved=read(fixturePath(p.root,scene.assetId()));
    auto entity=*scene.scene().find(box);entity.modelMaterials=ModelMaterialOverrides{AssetRef<MaterialAsset>{material.id}};require(scene.updateEntity(box,entity),scene.status());
    entity.modelMaterials={};require(scene.updateEntity(box,entity),scene.status());ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto plan=library.planDeletion({canonicalAssetId(material.id)},&scene);require(plan.error.empty() && !plan.needsReferenceWarning() && plan.clearsActiveHistory,"History-only Box material impact was missing or treated as a saved dependency.");
    finishDeletion(library,resources,plan,{},&scene);
    require(scene.history().size()==0 && scene.dirty() && scene.scene().find(box)->modelMaterials.empty() && read(fixturePath(p.root,scene.assetId()))==saved,
        "History-only material deletion changed saved content or retained resurrectable references.");
}
void navigationProfileDeletion() {
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto profile=library.createNavigationAgentProfile("Shared creature");
    SceneDocument active(p.root);const auto actor=active.createEnemy("Creature");auto value=*active.scene().find(actor);
    value.controller->profile=AssetRef<NavigationAgentProfile>{profile->id};require(active.updateEntity(actor,value),active.status());
    const auto volume=active.createNavigationVolume();value=*active.scene().find(volume);
    value.navigationVolume->profiles.emplace_back(DefaultNavigationAgentProfileId);require(active.updateEntity(volume,value),active.status());
    require(active.save("Profile owners"),active.status());
    SceneDocument other(p.root);const auto otherActor=other.createEnemy("Other creature");value=*other.scene().find(otherActor);
    value.controller->profile=AssetRef<NavigationAgentProfile>{profile->id};require(other.updateEntity(otherActor,value)&&other.save("Other profile owner"),other.status());
    library.refreshScenes();value=*active.scene().find(actor);value.local=math::translation({7,2,3});value.movement->enabled=false;
    require(active.updateEntity(actor,value),active.status());const auto unsaved=*active.scene().find(actor);
    const auto plan=library.planDeletion({profile->id},&active);
    require(plan.error.empty()&&plan.needsReferenceWarning()&&plan.scenes.size()==2&&plan.clearsActiveHistory,"Profile deletion did not index saved, live, volume and historical users.");
    finishDeletion(library,resources,plan,{},&active);
    const auto current=*active.scene().find(actor);
    require(current.controller->profile.id==DefaultNavigationAgentProfileId&&current.local==unsaved.local&&current.movement==unsaved.movement&&current.effects==unsaved.effects&&active.dirty(),"Profile deletion removed its actor or discarded unrelated unsaved edits.");
    require(active.scene().find(volume)->navigationVolume->profiles==std::vector<AssetRef<NavigationAgentProfile>>{AssetRef<NavigationAgentProfile>{DefaultNavigationAgentProfileId}},"Profile replacement left duplicate supported profiles.");
    require(readSceneSnapshot(p.root,active.assetId()).records.size()==active.scene().records().size()&&record(readSceneSnapshot(p.root,other.assetId()),otherActor).controller->profile.id==DefaultNavigationAgentProfileId,"Profile deletion did not preserve/rewrite saved actor references.");
    require(!hasAsset(library,profile->id)&&library.findNavigationAgentProfile(profile->id)==defaultNavigationAgentProfile(),"Profile deletion did not publish its protected default replacement.");
    require(!library.deleteAssets(library.planDeletion({DefaultNavigationAgentProfileId}),{}),"Protected Default Agent was deleted.");
}
void gameplayEffectDeletion() {
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto first=library.createGameplayEffect("Deleted initializer"),replacement=library.createGameplayEffect("Replacement"),middle=library.createGameplayEffect("Retained middle");
    const auto refs=[](std::initializer_list<std::string> ids){std::vector<AssetRef<GameplayEffectAsset>> result;for(const auto& id:ids)result.emplace_back(id);return result;};
    SceneDocument active(p.root);const auto actor=active.createEnemy("Saved effect owner");auto value=*active.scene().find(actor);
    value.effects->starting=refs({first->id,middle->id,first->id,replacement->id});require(active.updateEntity(actor,value)&&active.save("Effect owners"),active.status());
    const auto currentFormat=nlohmann::json::parse(read(sceneAssetPath(p.root,active.assetId())));
    require(currentFormat.at("version")==23&&std::none_of(currentFormat.at("entities").begin(),currentFormat.at("entities").end(),[](const auto& entity){return entity.contains("attributes");}),
        "Current scene wrote an authored attribute membership/value table.");
    SceneDocument other(p.root);const auto otherActor=other.createEnemy("Other owner");value=*other.scene().find(otherActor);
    value.effects->starting=refs({first->id,first->id});require(other.updateEntity(otherActor,value)&&other.save("Other effects"),other.status());
    library.refreshScenes();value=*active.scene().find(actor);value.name="Unrelated unsaved name";value.local=math::translation({8,2,4});
    require(active.updateEntity(actor,value),active.status());const auto unsaved=value;
    const auto pinned=library.findGameplayEffect(first->id);library.protectGameplayEffectDocument(first->id);
    require(!library.deleteAssets(library.planDeletion({first->id},&active),{},&active)&&hasAsset(library,first->id),"Open effect document did not protect its managed asset.");
    library.protectGameplayEffectDocument({});const auto plan=library.planDeletion({first->id},&active);
    require(plan.error.empty()&&plan.needsReferenceWarning()&&plan.scenes.size()==2&&plan.clearsActiveHistory,"Effect deletion missed saved/live/history users.");
    finishDeletion(library,resources,plan,{{first->id,replacement->id}},&active);
    const auto expected=refs({replacement->id,middle->id,replacement->id,replacement->id});
    const auto saved=readSceneSnapshot(p.root,active.assetId()),otherSaved=readSceneSnapshot(p.root,other.assetId());
    require(active.scene().find(actor)->effects->starting==expected&&record(saved,actor).effects->starting==expected&&
        record(otherSaved,otherActor).effects->starting==refs({replacement->id,replacement->id}),"Effect replacement reordered or deduplicated repeated entries.");
    require(active.scene().find(actor)->name==unsaved.name&&active.scene().find(actor)->local==unsaved.local&&active.dirty()&&record(saved,actor).name=="Saved effect owner",
        "Effect transaction discarded unrelated unsaved changes or wrote them into the saved scene.");
    require(active.history().size()==0&&!active.undo()&&pinned&&pinned->id==first->id&&library.findGameplayEffect(first->id)->id==replacement->id,
        "Effect deletion retained resurrection history or invalidated an immutable pinned definition.");
    finishDeletion(library,resources,library.planDeletion({replacement->id},&active),{},&active);
    require(active.scene().find(actor)->effects->starting==refs({middle->id})&&record(readSceneSnapshot(p.root,active.assetId()),actor).effects->starting==refs({middle->id})&&
        record(readSceneSnapshot(p.root,other.assetId()),otherActor).effects->starting.empty(),"Clearing a deleted effect removed its entity, retained duplicates or selected a default.");
    require(!library.findGameplayEffect(first->id)&&!library.findGameplayEffect(replacement->id),"Deleted effect aliases resurrected a managed definition.");
    require(!library.deleteAssets(library.planDeletion({PlayerStartingEffectId}),{}),"Protected Player starting effect was deleted.");
}
void unrelatedDeletionMaterializesLegacyEffects(bool independentlySaved) {
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);const auto material=library.createMaterial("Delete unrelated material");
    SceneDocument source(p.root);const auto first=source.createEnemy("First legacy"),second=source.createEnemy("Second legacy"),box=source.createBox();
    auto value=*source.scene().find(box);value.modelMaterials={AssetRef<MaterialAsset>{material->id}};
    require(source.updateEntity(box,value)&&source.save("Legacy effect owners"),source.status());
    auto legacy=nlohmann::json::parse(read(sceneAssetPath(p.root,source.assetId())));legacy["version"]=21;unsigned index{};
    for(auto& entity:legacy["entities"])if(entity.contains("enemy")) {
        entity.erase("effects");const auto capacity=index++?123.:77.;
        entity["attributes"]=nlohmann::json::array({{{"tag","Stats.Health"},{"startingBase",0}},{{"tag","Stats.MaxHealth"},{"startingBase",capacity}},{{"tag","Stats.MovementSpeed"},{"startingBase",2.5}}});
    }
    writeFileAtomically(sceneAssetPath(p.root,source.assetId()),legacy.dump());library.refreshScenes();SceneDocument active(p.root);require(active.load(source.assetId()),active.status());
    const auto firstId=active.scene().find(first)->effects->starting.front().id,secondId=active.scene().find(second)->effects->starting.front().id;
    auto expectedFirst=*active.gameplayEffect(firstId);const auto expectedSecond=*active.gameplayEffect(secondId);std::string independentlySavedBytes;
    if(independentlySaved) {
        expectedFirst.name="Independently edited initializer";expectedFirst.modifiers.front().magnitude.literal=900;
        saveGameplayEffectAsset(p.root,expectedFirst);library.refreshGameplayEffects();independentlySavedBytes=read(gameplayEffectAssetPath(p.root,firstId));
    }
    value=*active.scene().find(second);value.local=math::translation({5,3,2});require(active.updateEntity(second,value),active.status());
    finishDeletion(library,resources,library.planDeletion({material->id},&active),{},&active);
    const auto rewritten=readSceneSnapshot(p.root,source.assetId());
    require(rewritten.stagedGameplayEffects.empty()&&record(rewritten,first).effects->starting.front().id==firstId&&record(rewritten,second).effects->starting.front().id==secondId,
        "Unrelated deletion lost staged initializer identities or wrote dangling migrated references.");
    require(loadGameplayEffectAsset(p.root,firstId)==expectedFirst&&loadGameplayEffectAsset(p.root,secondId)==expectedSecond,
        "Unrelated scene rewrite changed independent legacy attribute values.");
    if(independentlySaved)require(read(gameplayEffectAssetPath(p.root,firstId))==independentlySavedBytes,"Deletion overwrote an independently edited initializer with a conversion seed.");
    require(active.scene().find(second)->local==value.local&&active.dirty()&&hasAsset(library,firstId)&&hasAsset(library,secondId),
        "Legacy effect publication lost an unrelated live edit or failed shared browser publication.");
}
void unrelatedDeletionMaterializesLegacyProfiles(bool independentlySaved) {
    Project p;ResourceManager resources;AssetLibrary library(p.root,&resources);
    const auto material=library.createMaterial("Delete this material");SceneDocument source(p.root);
    const auto actor=source.createEnemy("Legacy custom creature"),box=source.createBox();auto value=*source.scene().find(box);
    value.modelMaterials={AssetRef<MaterialAsset>{material->id}};require(source.updateEntity(box,value)&&source.save("Legacy pending profiles"),source.status());
    auto legacy=nlohmann::json::parse(read(sceneAssetPath(p.root,source.assetId())));legacy["version"]=17;
    for(auto& entity:legacy["entities"])if(entity.contains("controller")){entity["controller"].erase("profile");entity["controller"]["height"]=2.6f;entity["controller"]["radius"]=.45f;}
    writeFileAtomically(sceneAssetPath(p.root,source.assetId()),legacy.dump());library.refreshScenes();
    SceneDocument active(p.root);require(active.load(source.assetId()),active.status());
    const auto activeProfile=active.scene().find(actor)->controller->profile.id;
    auto expectedTraversal=AgentTraversalSettings{2.6f,.45f,45,.3f};std::string independentlySavedContents;
    if(independentlySaved) {
        const auto originalScene=read(sceneAssetPath(p.root,source.assetId()));
        auto profile=*active.navigationAgentProfile(activeProfile);profile.traversal={3.1f,.55f,30,.1f};
        saveNavigationAgentProfile(p.root,profile);library.refreshNavigationAgentProfiles();expectedTraversal=profile.traversal;
        independentlySavedContents=read(navigationAgentProfilePath(p.root,activeProfile));
        require(!active.navigationAgentProfile(activeProfile)&&!active.dirty(),"Materialized migration profile resurrected its seed or dirtied the scene.");
        require(read(sceneAssetPath(p.root,source.assetId()))==originalScene,"Saving a converted profile rewrote its legacy scene.");
    } else {
        value=*active.scene().find(actor);value.local=math::translation({6,2,1});require(active.updateEntity(actor,value),active.status());
    }
    finishDeletion(library,resources,library.planDeletion({material->id},&active),{},&active);
    const auto rewritten=readSceneSnapshot(p.root,source.assetId());const auto& creature=record(rewritten,actor);
    require(rewritten.stagedNavigationProfiles.empty()&&creature.controller->profile.id!=DefaultNavigationAgentProfileId,"Unrelated deletion wrote an incomplete legacy-profile conversion.");
    require(loadNavigationAgentProfile(p.root,creature.controller->profile.id).traversal==expectedTraversal,"Unrelated scene rewrite lost the authored custom capsule.");
    require(creature.controller->profile.id==activeProfile&&active.scene().find(actor)->controller->profile.id==activeProfile,"Legacy deletion conversion changed the saved or live profile identity.");
    if(independentlySaved) {
        require(!active.dirty()&&active.loadedFingerprint()==sceneFileFingerprint(p.root,source.assetId()),"Legacy rewrite left the clean active scene inconsistent with its saved state.");
        require(read(navigationAgentProfilePath(p.root,activeProfile))==independentlySavedContents,"Legacy rewrite overwrote an independently edited profile with its migration seed.");
    } else require(active.scene().find(actor)->local==value.local&&active.dirty(),"Legacy deletion conversion discarded unrelated live edits.");
    require(hasAsset(library,creature.controller->profile.id)&&library.browserCatalog().contains(creature.controller->profile.id),"Materialized legacy profile was absent from the shared browser catalog.");
    require(std::none_of(rewritten.records.begin(),rewritten.records.end(),[](const auto& entity){return entity.navigationVolume.has_value();}),"Unrelated deletion auto-created a Navigation Volume.");
}
void materialPreviewSourceLifetime(const fs::path& compiler,const fs::path& sources,const fs::path& includes) {
    Project p;const std::string image="assets/textures/shared-node-source.png";
    model_test::Bytes pixels(128*128*4,255);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x) {
        const auto offset=(std::size_t(y)*128+x)*4;pixels[offset]=x<64?255:0;pixels[offset+1]=y<64?0:255;
    }
    fs::create_directories(fixturePath(p.root,"assets/textures"));
    model_test::writeBytes(fixturePath(p.root,image),model_test::png(128,128,pixels));
    // Keep the active shared-input map alive, but immediately evict payloads
    // once all decode handles are released. Zero idle grace would erase that
    // map itself at each update and exercise a different lifetime contract.
    ResourceConfig config;config.cpuRetentionBytes=0;config.idleGrace=std::chrono::seconds{60};
    ResourceManager resources(config);MaterialGraphPrograms programs(resources,{compiler,sources,includes,fixturePath(p.root,"assets/cache/shaders")});
    AssetLibrary library(p.root,&resources);library.setMaterialGraphPrograms(&programs);
    MaterialAsset material;material.id="mixed-node-source-lifetime";material.name="Mixed node source lifetime";
    const auto output=addMaterialNode(material.graph,MaterialNodeKind::Output).id;
    const auto uv=addMaterialNode(material.graph,MaterialNodeKind::UV).id;
    const auto source=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,source)->texture.image.id=image;
    const auto multiply=addMaterialNode(material.graph,MaterialNodeKind::Multiply).id;
    findMaterialNode(material.graph,multiply)->right={MaterialValueType::Scalar,{.6f,.6f,.6f,.6f}};
    const auto gray=addMaterialNode(material.graph,MaterialNodeKind::Grayscale).id;
    const auto levels=addMaterialNode(material.graph,MaterialNodeKind::Levels).id;
    findMaterialNode(material.graph,levels)->image.gamma=1.285f;
    const auto resize=addMaterialNode(material.graph,MaterialNodeKind::Resize).id;
    findMaterialNode(material.graph,resize)->resize={128,128,MaterialResizeFilter::Area};
    connectMaterialNodes(material.graph,uv,MaterialPin::UV,source,MaterialPin::UV);
    connectMaterialNodes(material.graph,source,MaterialPin::RGB,multiply,MaterialPin::Left);
    connectMaterialNodes(material.graph,source,MaterialPin::RGB,gray,MaterialPin::Left);
    connectMaterialNodes(material.graph,gray,MaterialPin::Value,levels,MaterialPin::Left);
    connectMaterialNodes(material.graph,levels,MaterialPin::Value,resize,MaterialPin::Left);
    connectMaterialNodes(material.graph,resize,MaterialPin::Value,output,MaterialPin::Roughness);
    canonicalizeMaterialAsset(material);const auto graph=std::make_shared<const MaterialGraph>(material.graph);
    const auto authored=serializeMaterialAsset(material);
    const auto tick=[&]{resources.pump();library.updateResources(bgfx::frame());programs.trim();};
    const auto ready=[](const ModelMaterialTextures& value){return value.graph&&!value.graphPending&&value.sourceReady&&value.complete&&value.drawable();};
    ModelMaterialTextures full;const auto fullDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{25};
    do {
        tick();full=library.materialPreviewBindings(material,128);
        if(ready(full)&&full.graph->programs->optimized&&resources.idle())break;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }while(std::chrono::steady_clock::now()<fullDeadline);
    require(ready(full)&&full.graph->programs->optimized&&resources.idle(),"Shared-input lifetime fixture could not warm its Resize material: "+full.diagnostic);
    const auto sourceId=findMaterialNode(material.graph,source)->texture.image.id;
    const auto initialPublications=resources.takePublications();
    require(std::any_of(initialPublications.begin(),initialPublications.end(),[&](const auto& value) {
        return value.kind==ResourceKind::Image&&value.id==sourceId&&value.variant=="source";
    }),"Shared-input lifetime fixture did not initially decode its managed PNG.");
    const auto fullSignature=full.graph->signature;
    for(unsigned frame=0;frame<10;++frame){tick();full=library.materialPreviewBindings(material,128);require(ready(full)&&full.graph->signature==fullSignature,"Warm Resize material did not retain its completed source generation.");}
    require(resources.stats().evictions>0,"Zero-retention lifetime fixture did not evict any released CPU payload.");
    (void)resources.takePublications();
    std::array<PreparedTexture,3> previews;
    const auto previewReady=[](const PreparedTexture& value){return value.sourceReady&&value.complete&&bgfx::isValid(value.handle)&&value.lease&&!value.failed;};
    bool settled=false;const auto previewDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    do {
        tick();
        // Production order: node drawing requests generic source decodes,
        // then scene/material preparation hits an already generated Resize.
        // That cache hit must not clear the node preview's only decode owner
        // before the next pump can accept the asynchronous result.
        previews[0]=library.materialNodeImagePreview(material.id,graph,{multiply,MaterialPin::Value},128);
        previews[1]=library.materialNodeImagePreview(material.id,graph,{levels,MaterialPin::Value},128);
        previews[2]=library.materialNodeImagePreview(material.id,graph,{levels,MaterialPin::Value},256);
        full=library.materialPreviewBindings(material,128);
        require(ready(full)&&full.graph->signature==fullSignature,"Generic node requests invalidated the warm Resize material.");
        settled=std::all_of(previews.begin(),previews.end(),previewReady)&&resources.idle();
        if(settled)break;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }while(std::chrono::steady_clock::now()<previewDeadline);
    require(settled,"Generic node previews lost source decode ownership to a later warm material cache hit: "+
        previews[0].diagnostic+" / "+previews[1].diagnostic+" / "+previews[2].diagnostic);
    const auto loads=resources.stats().loads;const auto signatures=std::array{previews[0].signature,previews[1].signature,previews[2].signature};
    for(unsigned frame=0;frame<30;++frame) {
        tick();previews[0]=library.materialNodeImagePreview(material.id,graph,{multiply,MaterialPin::Value},128);
        previews[1]=library.materialNodeImagePreview(material.id,graph,{levels,MaterialPin::Value},128);
        previews[2]=library.materialNodeImagePreview(material.id,graph,{levels,MaterialPin::Value},256);
        full=library.materialPreviewBindings(material,128);
        for(unsigned index=0;index<previews.size();++index)require(previewReady(previews[index])&&previews[index].signature==signatures[index],
            "Settled mixed-order node previews changed their accepted display images.");
    }
    require(resources.stats().loads==loads&&serializeMaterialAsset(material)==authored,
        "Warm mixed-order node/material requests repeated generation or changed the authored graph.");
    library.releaseMaterialNodeImagePreviews(material.id);resources.stop();
}
void materialPreviewResources(const fs::path& compiler,const fs::path& sources,const fs::path& includes){
    Project p;const std::string image="assets/textures/preview-pattern.png";
    model_test::Bytes pixels(512*512*4,255);
    for(std::size_t y=0;y<512;++y)for(std::size_t x=0;x<512;++x){const auto offset=(y*512+x)*4;pixels[offset]=x<256?255:0;pixels[offset+1]=y<256?0:255;}
    fs::create_directories(fixturePath(p.root,"assets/textures"));model_test::writeBytes(fixturePath(p.root,image),model_test::png(512,512,pixels));
    const std::string coldNodeImage="assets/textures/cold-node-preview-pattern.png";
    model_test::writeBytes(fixturePath(p.root,coldNodeImage),model_test::png(512,512,pixels));
    const std::string growingImage="assets/textures/cached-blur-size.png";
    const auto growingSeedFile=fixturePath(p.root,growingImage);
    model_test::writeBytes(growingSeedFile,model_test::png(2,2,model_test::Bytes{
        0,32,64,255, 96,128,160,255, 192,224,255,255, 64,128,192,255}));
    auto material=createMaterialAsset(p.root,"Streamed preview");const auto output=materialOutputNode(material.graph).id;
    const auto uv=addMaterialNode(material.graph,MaterialNodeKind::UV).id;findMaterialNode(material.graph,uv)->uv.mode=MaterialUvMode::WorldTriplanar;
    const auto color=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,color)->texture.image.id=image;findMaterialNode(material.graph,color)->texture.sampler.minFilter=ModelFilter::LinearMipmapLinear;
    connectMaterialNodes(material.graph,uv,MaterialPin::UV,color,MaterialPin::UV);connectMaterialNodes(material.graph,color,MaterialPin::RGB,output,MaterialPin::BaseColor);
    canonicalizeMaterialAsset(material);saveMaterialAsset(p.root,material);const auto sourceBytes=read(fixturePath(p.root,material.id));
    bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
    require(bgfx::init(init),"Could not initialize material-preview resource check.");
    try {{
        ResourceManager resources;MaterialGraphPrograms programs(resources,{compiler,sources,includes,fixturePath(p.root,"assets/cache/shaders")});
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
        require(read(fixturePath(p.root,material.id))==sourceBytes,"Preview publication rewrote its authored material.");
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
        // A generator is a real linear-data image binding. Editing its recipe
        // replaces pixels consumed by Roughness while keeping the same shader.
        MaterialAsset noise;noise.id="grunge-resource-fixture";noise.name="Generated roughness";
        const auto noiseOutput=addMaterialNode(noise.graph,MaterialNodeKind::Output).id;
        const auto grunge=addMaterialNode(noise.graph,MaterialNodeKind::Grunge).id;
        const auto grungeMetal=addMaterialNode(noise.graph,MaterialNodeKind::Grunge).id;
        findMaterialNode(noise.graph,grungeMetal)->grunge.seed=44;
        connectMaterialNodes(noise.graph,grunge,MaterialPin::Value,noiseOutput,MaterialPin::Roughness);
        connectMaterialNodes(noise.graph,grungeMetal,MaterialPin::Value,noiseOutput,MaterialPin::Metallic);
        canonicalizeMaterialAsset(noise);
        auto noiseBinding=until([&]{return library.materialPreviewBindings(noise,512);},ready,"Grunge roughness texture did not publish");
        require(noiseBinding.graph->sources.size()==2&&std::all_of(noiseBinding.graph->sources.begin(),noiseBinding.graph->sources.end(),[](const auto& source){return source.grunge&&source.image.empty();}),
            "Generated roughness was replaced with an authored image reference.");
        const auto sourceSlot=[](const PreparedGraphMaterial& graph,MaterialNodeId node) {
            const auto source=std::find_if(graph.sources.begin(),graph.sources.end(),[&](const auto& value){return value.nodeId==node;});
            require(source!=graph.sources.end(),"Generated node has no prepared texture binding.");return source->slot;
        };
        const auto previousNoise=noiseBinding.graph;
        const auto previousNoiseSlot=sourceSlot(*previousNoise,grunge),previousMetalSlot=sourceSlot(*previousNoise,grungeMetal);
        findMaterialNode(noise.graph,grunge)->grunge.baseNoisiness=.19f;canonicalizeMaterialAsset(noise);
        const auto pendingNoise=library.materialPreviewBindings(noise,512);
        require(!pendingNoise.sourceReady&&bgfx::isValid(previousNoise->textures[previousNoiseSlot])&&previousNoise->leases[previousNoiseSlot],
            "A pending Grunge edit exposed incomplete pixels or discarded the previous packet lease.");
        noiseBinding=until([&]{return library.materialPreviewBindings(noise,512);},ready,"Edited Grunge roughness did not regenerate");
        const auto noiseSlot=sourceSlot(*noiseBinding.graph,grunge),metalSlot=sourceSlot(*noiseBinding.graph,grungeMetal);
        require(noiseBinding.graph->programs==previousNoise->programs&&noiseBinding.graph->uniforms==previousNoise->uniforms&&
            noiseBinding.graph->signature!=previousNoise->signature&&noiseBinding.graph->sourceFingerprints[noiseSlot]!=previousNoise->sourceFingerprints[previousNoiseSlot]&&
            noiseBinding.graph->sourceFingerprints[metalSlot]==previousNoise->sourceFingerprints[previousMetalSlot],
            "Grunge recipe edits did not replace texture content independently of shader topology and uniforms.");
        PreparedTexture thumbnail;
        const auto thumbnailDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {
            tick();thumbnail=library.grungePreview(findMaterialNode(noise.graph,grunge)->grunge,256);
            if(!thumbnail.diagnostic.empty())throw std::runtime_error(thumbnail.diagnostic);
            if(!thumbnail.complete)std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }while(!thumbnail.complete&&std::chrono::steady_clock::now()<thumbnailDeadline);
        require(thumbnail.complete&&thumbnail.sourceReady&&bgfx::isValid(thumbnail.handle)&&thumbnail.lease&&
            thumbnail.sourceWidth==GrungeTextureEdge&&thumbnail.sourceHeight==GrungeTextureEdge&&
            thumbnail.sourceFingerprint==noiseBinding.graph->sourceFingerprints[noiseSlot],
            "Grunge node preview did not reuse a mip of the canonical generated 2D image.");
        require(thumbnail.handle.idx==noiseBinding.graph->textures[noiseSlot].idx,
            "Grunge Value preview replaced its raw data image with a color-encoded display derivative.");
        std::unordered_map<std::string,std::uint64_t> versions;
        require(preparedGraphSourcesCoherent(*noiseBinding.graph,versions)&&versions.empty(),
            "Distinct generated recipes collided under the empty managed-image identity.");
        auto managed=*noiseBinding.graph;
        for(auto& source:managed.sources){source.grunge.reset();source.image.id="shared-managed-image";managed.sourceFingerprints[source.slot]=source.slot==noiseSlot?11:22;}
        require(!preparedGraphSourcesCoherent(managed,versions),"Managed image revision conflicts stopped blocking incoherent publication.");
        versions.clear();for(const auto& source:managed.sources)managed.sourceFingerprints[source.slot]=11;
        require(preparedGraphSourcesCoherent(managed,versions)&&versions.size()==1,"Matching shared managed image versions failed coherent publication.");

        // Numeric previews inspect the authored signal directly, while RGB
        // previews retain one display transfer. Inspect the actual published
        // image, rather than only a cache key or a helper's isolated equation.
        MaterialAsset transfer;transfer.id="node-preview-transfer";transfer.name="Numeric preview transfer";
        addMaterialNode(transfer.graph,MaterialNodeKind::Output);
        std::array<MaterialNodeId,3> scalarNodes{};
        const std::array scalarValues{.25f,.5f,.75f};
        for(unsigned i=0;i<scalarNodes.size();++i) {
            scalarNodes[i]=addMaterialNode(transfer.graph,MaterialNodeKind::Scalar).id;
            findMaterialNode(transfer.graph,scalarNodes[i])->scalar=scalarValues[i];
        }
        const auto transferVector=addMaterialNode(transfer.graph,MaterialNodeKind::Vector2).id;
        findMaterialNode(transfer.graph,transferVector)->vector2={.25f,.75f};
        const auto transferColor=addMaterialNode(transfer.graph,MaterialNodeKind::Color).id;
        findMaterialNode(transfer.graph,transferColor)->color={.5f,.5f,.5f,.25f};
        canonicalizeMaterialAsset(transfer);const auto transferAuthored=serializeMaterialAsset(transfer);
        const auto transferGraph=std::make_shared<const MaterialGraph>(transfer.graph);
        const auto displayPixels=[&](MaterialGraphPreviewTarget target) {
            (void)resources.takePublications();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};PreparedTexture texture;
            do {
                tick();texture=library.materialNodeImagePreview(transfer.id,transferGraph,target,32);
                if(texture.sourceReady&&texture.complete&&bgfx::isValid(texture.handle)&&resources.idle())break;
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }while(std::chrono::steady_clock::now()<deadline);
            require(texture.sourceReady&&texture.complete&&bgfx::isValid(texture.handle),
                "Numeric/color preview did not publish: "+texture.diagnostic);
            const auto publications=resources.takePublications();
            const auto publication=std::find_if(publications.begin(),publications.end(),[&](const auto& value) {
                return value.kind==ResourceKind::TextureMips&&value.variant=="generated-float16-mips-v1:32x32"&&
                    value.fingerprint==texture.sourceFingerprint;
            });
            require(publication!=publications.end()&&publication->id.starts_with("material-node-image-v4:"),
                "Node display used an obsolete transfer cache identity or published no pixels.");
            ResourceRequestOptions inspect;inspect.variant=publication->variant;inspect.retainLoader=false;
            const auto descriptor=resources.requestDerived<streaming::TextureMipCache>(publication->id,[](std::stop_token)->ResourceLoadResult<streaming::TextureMipCache> {
                throw std::runtime_error("Inspecting display transfer scheduled another generation.");
            },std::move(inspect));
            const auto snapshot=descriptor.snapshot();
            require(descriptor.state()==ResourceState::Ready&&snapshot&&snapshot->format==ImagePixelFormat::RGBA16F&&
                !snapshot->memory.empty()&&snapshot->memory.front().width==32&&snapshot->memory.front().height==32,
                "Display transfer inspection lost the published float image.");
            return snapshot->memory.front();
        };
        const auto displayComponent=[](const DecodedImage& image,unsigned x,unsigned y,unsigned channel) {
            std::uint16_t value{};std::memcpy(&value,image.rgba.data()+((std::size_t(y)*image.width+x)*4+channel)*2,sizeof(value));
            const unsigned exponent=(value>>10)&31,mantissa=value&1023;
            require(exponent!=31,"Display transfer produced a nonfinite half value.");
            const auto decoded=exponent?std::ldexp(1.f+mantissa/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24);
            return value&0x8000?-decoded:decoded;
        };
        const auto displayNear=[&](const DecodedImage& image,unsigned x,unsigned y,unsigned channel,float expected,const char* failure) {
            require(std::abs(displayComponent(image,x,y,channel)-expected)<.0006f,failure);
        };
        for(unsigned i=0;i<scalarNodes.size();++i) {
            const auto numeric=displayPixels({scalarNodes[i],MaterialPin::Value});
            for(unsigned channel=0;channel<3;++channel)displayNear(numeric,0,0,channel,scalarValues[i],"Scalar preview applied a color transfer to numeric data.");
            displayNear(numeric,0,0,3,1,"Scalar preview was not opaque.");
        }
        const auto vectorDisplay=displayPixels({transferVector,MaterialPin::XY});
        displayNear(vectorDisplay,0,0,0,.25f,"Vector2 X preview applied a color transfer.");
        displayNear(vectorDisplay,0,0,1,.75f,"Vector2 Y preview applied a color transfer.");
        displayNear(vectorDisplay,0,0,2,0,"Vector2 display changed its zero blue channel.");
        const auto rgbDisplay=displayPixels({transferColor,MaterialPin::RGB});
        for(unsigned channel=0;channel<3;++channel)displayNear(rgbDisplay,0,0,channel,.73535698f,"RGB preview lost its single linear-to-sRGB transfer or read scalar data.");
        const auto rgbaDisplay=displayPixels({transferColor,MaterialPin::RGBA});
        const auto srgb=[](float value){return value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f;};
        for(unsigned channel=0;channel<3;++channel) {
            displayNear(rgbaDisplay,0,0,channel,srgb(.5f*.25f+.18f*.75f),"RGBA preview changed linear alpha/checker composition.");
            displayNear(rgbaDisplay,8,0,channel,srgb(.5f*.25f+.32f*.75f),"RGBA preview lost the second checker level.");
        }
        displayNear(rgbaDisplay,0,0,3,1,"RGBA display copy retained translucent alpha.");
        const auto unchanged=bakeMaterialGraphImagePreview(*transferGraph,{transferColor,MaterialPin::RGBA},{},{2,2,0});
        require(unchanged.rgba[0]==.5f&&unchanged.rgba[3]==.25f&&serializeMaterialAsset(transfer)==transferAuthored,
            "Preview transfer modified the canonical linear image or authored graph.");
        library.releaseMaterialNodeImagePreviews(transfer.id);

        // Match an ordinary authored image graph, rather than exercising only
        // builtin/generated sources: explicit UV0, a managed Color PNG, and
        // independently previewable color and grayscale processing branches.
        MaterialAsset textured; textured.id="managed-texture-node-images";textured.name="Managed texture node images";
        const auto texturedOutput=addMaterialNode(textured.graph,MaterialNodeKind::Output).id;
        const auto texturedUv=addMaterialNode(textured.graph,MaterialNodeKind::UV).id;
        findMaterialNode(textured.graph,texturedUv)->uv.mode=MaterialUvMode::UV0;
        const auto texturedSource=addMaterialNode(textured.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(textured.graph,texturedSource)->texture.image.id=coldNodeImage;
        findMaterialNode(textured.graph,texturedSource)->texture.role=MaterialTextureRole::Color;
        const auto texturedMultiply=addMaterialNode(textured.graph,MaterialNodeKind::Multiply).id;
        findMaterialNode(textured.graph,texturedMultiply)->right={MaterialValueType::Scalar,{.6f,.6f,.6f,.6f}};
        const auto texturedGray=addMaterialNode(textured.graph,MaterialNodeKind::Grayscale).id;
        const auto texturedLevels=addMaterialNode(textured.graph,MaterialNodeKind::Levels).id;
        findMaterialNode(textured.graph,texturedLevels)->image.gamma=1.285f;
        const auto unrelatedSource=addMaterialNode(textured.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(textured.graph,unrelatedSource)->texture.image.id="assets/textures/missing-unused-preview.png";
        connectMaterialNodes(textured.graph,texturedUv,MaterialPin::UV,texturedSource,MaterialPin::UV);
        connectMaterialNodes(textured.graph,texturedSource,MaterialPin::RGB,texturedMultiply,MaterialPin::Left);
        connectMaterialNodes(textured.graph,texturedSource,MaterialPin::RGB,texturedGray,MaterialPin::Left);
        connectMaterialNodes(textured.graph,texturedGray,MaterialPin::Value,texturedLevels,MaterialPin::Left);
        connectMaterialNodes(textured.graph,texturedMultiply,MaterialPin::Value,texturedOutput,MaterialPin::BaseColor);
        connectMaterialNodes(textured.graph,texturedLevels,MaterialPin::Value,texturedOutput,MaterialPin::Roughness);
        canonicalizeMaterialAsset(textured);
        const auto texturedAuthored=serializeMaterialAsset(textured);
        const auto texturedGraph=std::make_shared<const MaterialGraph>(textured.graph);
        const MaterialGraphPreviewTarget multiplyTarget{texturedMultiply,MaterialPin::Value},levelsTarget{texturedLevels,MaterialPin::Value};
        const auto usedImage=findMaterialNode(textured.graph,texturedSource)->texture.image.id;
        for(const auto target:{multiplyTarget,levelsTarget})
            require(materialGraphImagePreviewSources(textured.graph,target)==std::vector<std::string>{usedImage},
                "Managed node preview included an unrelated texture or omitted its actual linked PNG source.");
        const auto awaitTextured=[&](const std::shared_ptr<const MaterialGraph>& graph,MaterialGraphPreviewTarget target,const char* failure) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{15};PreparedTexture texture;
            do {
                tick();texture=library.materialNodeImagePreview(textured.id,graph,target,128);
                if(texture.sourceReady&&texture.complete&&bgfx::isValid(texture.handle)&&resources.idle()) {
                    require(texture.lease&&!texture.failed&&texture.width==128&&texture.height==128&&
                        texture.sourceFingerprint&&texture.signature,"Managed node image accepted an incomplete texture descriptor.");
                    return texture;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }while(std::chrono::steady_clock::now()<deadline);
            throw std::runtime_error(std::string(failure)+": "+texture.diagnostic);
        };
        const auto inspectTextured=[&](const PreparedTexture& texture,bool grayscale) {
            const auto publications=resources.takePublications();
            const auto publication=std::find_if(publications.begin(),publications.end(),[&](const auto& value) {
                return value.kind==ResourceKind::TextureMips&&value.variant=="generated-float16-mips-v1:128x128"&&
                    value.fingerprint==texture.sourceFingerprint;
            });
            require(publication!=publications.end(),"Managed node image published no inspectable display pixels.");
            ResourceRequestOptions inspect;inspect.variant=publication->variant;inspect.retainLoader=false;
            const auto descriptor=resources.requestDerived<streaming::TextureMipCache>(publication->id,[](std::stop_token)->ResourceLoadResult<streaming::TextureMipCache> {
                throw std::runtime_error("Inspecting managed node display pixels scheduled another generation.");
            },std::move(inspect));
            const auto snapshot=descriptor.snapshot();
            require(descriptor.state()==ResourceState::Ready&&snapshot&&snapshot->format==ImagePixelFormat::RGBA16F&&
                !snapshot->memory.empty()&&snapshot->memory.size()==snapshot->levels.size()&&
                snapshot->memory.front().width==128&&snapshot->memory.front().height==128&&
                snapshot->memory.front().rgba.size()==128*128*8,
                "Managed node preview accepted a missing, wrong-size or non-float display image.");
            const auto& bytes=snapshot->memory.front().rgba;
            const auto component=[&](unsigned x,unsigned y,unsigned channel) {
                std::uint16_t value{};std::memcpy(&value,bytes.data()+((std::size_t(y)*128+x)*4+channel)*2,sizeof(value));
                const unsigned exponent=(value>>10)&31,mantissa=value&1023;
                require(exponent!=31,"Managed preview pixels contain nonfinite display values.");
                const auto decoded=exponent?std::ldexp(1.f+mantissa/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24);
                return value&0x8000?-decoded:decoded;
            };
            float minimum=1,maximum=0;
            for(unsigned y:{16u,112u})for(unsigned x:{16u,112u}) {
                const float red=component(x,y,0),green=component(x,y,1),blue=component(x,y,2);
                minimum=std::min(minimum,red);maximum=std::max(maximum,red);
                require(std::abs(component(x,y,3)-1)<.001f,"Managed node display unexpectedly became transparent.");
                if(grayscale)require(std::abs(red-green)<.001f&&std::abs(red-blue)<.001f,
                    "Grayscale/Levels preview did not broadcast its scalar result to RGB.");
            }
            require(maximum-minimum>.1f,"Managed node preview completed with flat placeholder pixels instead of the linked texture.");
            if(!grayscale)require(component(16,16,0)>.7f&&component(112,16,0)<.01f&&
                component(16,112,1)>.7f&&component(16,16,1)<.01f,
                "Multiply node preview lost its Color PNG quadrant pattern or processed the wrong branch.");
        };
        (void)resources.takePublications();
        const auto texturedProduct=awaitTextured(texturedGraph,multiplyTarget,"Managed Color-to-Multiply image preview did not settle");
        inspectTextured(texturedProduct,false);
        const auto texturedHeight=awaitTextured(texturedGraph,levelsTarget,"Managed Color-to-Grayscale-to-Levels image preview did not settle");
        inspectTextured(texturedHeight,true);
        require(texturedProduct.sourceFingerprint!=texturedHeight.sourceFingerprint&&texturedProduct.signature!=texturedHeight.signature,
            "Independent managed color/grayscale preview targets shared one display identity.");
        auto unrelatedEdit=textured.graph;
        findMaterialNode(unrelatedEdit,texturedMultiply)->right.value={.25f,.25f,.25f,.25f};
        findMaterialNode(unrelatedEdit,unrelatedSource)->texture.image.id="assets/textures/another-missing-unused-preview.png";
        const auto unrelatedGraph=std::make_shared<const MaterialGraph>(std::move(unrelatedEdit));
        const auto texturedWarmLoads=resources.stats().loads;
        for(unsigned frame=0;frame<40;++frame) {
            tick();const auto product=library.materialNodeImagePreview(textured.id,texturedGraph,multiplyTarget,128);
            const auto height=library.materialNodeImagePreview(textured.id,unrelatedGraph,levelsTarget,128);
            require(product.sourceReady&&product.complete&&product.signature==texturedProduct.signature&&
                height.sourceReady&&height.complete&&height.signature==texturedHeight.signature,
                "Warm managed node polling or an unrelated branch edit replaced the accepted image.");
        }
        require(resources.stats().loads==texturedWarmLoads&&resources.takePublications().empty()&&serializeMaterialAsset(textured)==texturedAuthored,
            "Managed node preview repeated warm jobs, followed an unrelated missing source or changed its authored graph.");
        // A continuous gesture must publish intermediate images. A quiet-time
        // debounce resets on every edit and leaves both views stale until the
        // user releases the slider, even for this small point-operation graph.
        auto liveGraph=textured.graph;
        const auto scan=addMaterialNode(liveGraph,MaterialNodeKind::HistogramScan).id;
        connectMaterialNodes(liveGraph,texturedGray,MaterialPin::Value,scan,MaterialPin::Left);
        const MaterialGraphPreviewTarget scanTarget{scan,MaterialPin::Value};
        auto liveSnapshot=std::make_shared<const MaterialGraph>(liveGraph);
        auto liveTexture=awaitTextured(liveSnapshot,scanTarget,"Histogram Scan preview did not warm");
        auto lastLiveSignature=liveTexture.signature;
        unsigned liveUpdates{};
        const auto dragStarted=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<64;++frame) {
            auto& controls=findMaterialNode(liveGraph,scan)->atomic;
            controls.coverage=.1f+static_cast<float>(frame)*.012f;controls.contrast=.8f;
            liveSnapshot=std::make_shared<const MaterialGraph>(liveGraph);
            tick();
            (void)library.materialNodeImagePreview(textured.id,liveSnapshot,scanTarget,256);
            liveTexture=library.materialNodeImagePreview(textured.id,liveSnapshot,scanTarget,128);
            if(bgfx::isValid(liveTexture.handle)&&!liveTexture.failed&&liveTexture.signature!=lastLiveSignature) {
                ++liveUpdates;lastLiveSignature=liveTexture.signature;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{8});
        }
        require(liveUpdates>=2,"Continuous Histogram Scan edits starved intermediate node image updates.");
        const auto finalLiveTexture=awaitTextured(liveSnapshot,scanTarget,"Histogram Scan preview did not settle after the drag");
        require(finalLiveTexture.signature!=texturedHeight.signature&&serializeMaterialAsset(textured)==texturedAuthored,
            "Interactive node image updates lost their identity or modified authored material data.");
        std::cout<<"Continuous Histogram Scan drag: "<<liveUpdates<<" inline updates in "
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-dragStarted).count()<<" ms.\n";
        library.releaseMaterialNodeImagePreviews(textured.id);
        const auto independentFinal=awaitTextured(liveSnapshot,scanTarget,"Independent final Histogram Scan image did not settle");
        require(independentFinal.sourceFingerprint==finalLiveTexture.sourceFingerprint&&independentFinal.signature==finalLiveTexture.signature,
            "The settled drag retained an intermediate definition instead of the final Histogram Scan value.");
        library.releaseMaterialNodeImagePreviews(textured.id);
        // Exercise the public adapter, including a shared direct/baked source,
        // rather than only TextureStreamer's generated-float cache in isolation.
        MaterialAsset resized;resized.id="resize-resource-fixture";resized.name="Processed Resize";
        const auto resizeOutput=addMaterialNode(resized.graph,MaterialNodeKind::Output).id;
        const auto resizeSource=addMaterialNode(resized.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(resized.graph,resizeSource)->texture.image.id=image;
        const auto gray=addMaterialNode(resized.graph,MaterialNodeKind::Grayscale).id;
        findMaterialNode(resized.graph,gray)->image.grayMode=MaterialGrayscaleMode::Red;
        const auto levels=addMaterialNode(resized.graph,MaterialNodeKind::Levels).id;
        findMaterialNode(resized.graph,levels)->image.gamma=1.7f;
        const auto resize=addMaterialNode(resized.graph,MaterialNodeKind::Resize).id;
        findMaterialNode(resized.graph,resize)->resize={256,128,MaterialResizeFilter::Area};
        connectMaterialNodes(resized.graph,resizeSource,MaterialPin::RGBA,gray,MaterialPin::Left);
        connectMaterialNodes(resized.graph,gray,MaterialPin::Value,levels,MaterialPin::Left);
        connectMaterialNodes(resized.graph,levels,MaterialPin::Value,resize,MaterialPin::Left);
        connectMaterialNodes(resized.graph,resize,MaterialPin::Value,resizeOutput,MaterialPin::Roughness);
        connectMaterialNodes(resized.graph,resizeSource,MaterialPin::RGB,resizeOutput,MaterialPin::BaseColor);
        canonicalizeMaterialAsset(resized);
        auto resizeBinding=until([&]{return library.materialPreviewBindings(resized,256);},
            [&](const auto& binding){return ready(binding)&&binding.graph->programs->optimized&&resources.idle();},
            "Processed Resize did not publish a complete settled packet");
        const auto oldResize=resizeBinding.graph;
        const auto resizedSlot=sourceSlot(*oldResize,resize),directSlot=sourceSlot(*oldResize,resizeSource);
        require(oldResize->sources.size()==2&&oldResize->sources[resizedSlot].resize&&
            !oldResize->sources[directSlot].resize&&oldResize->sourceFingerprints[resizedSlot]!=0&&
            oldResize->sourceFingerprints[directSlot]==fingerprintFiles({fixturePath(p.root,image)}),
            "Resize adapter lost its processed sampler or immutable source-file generation.");
        const auto warmLoads=resources.stats().loads;
        for(unsigned frame=0;frame<120;++frame) {
            tick();const auto warm=library.materialPreviewBindings(resized,256);
            require(ready(warm)&&warm.graph->signature==oldResize->signature&&
                warm.graph->textureSignatures==oldResize->textureSignatures&&
                warm.graph->textures[resizedSlot].idx==oldResize->textures[resizedSlot].idx,
                "Warm AssetLibrary Resize requests replaced their immutable GPU packet.");
        }
        require(resources.stats().loads==warmLoads,
            "Warm AssetLibrary Resize requests repeated CPU image decode or baking.");
        auto editedPixels=pixels;
        for(std::size_t i=0;i<editedPixels.size();i+=4){editedPixels[i]=255-editedPixels[i];editedPixels[i+2]=73;}
        const auto imageFile=fixturePath(p.root,image);
        model_test::writeBytes(imageFile,model_test::png(512,512,editedPixels));library.onFileChanged(imageFile);
        const auto editedFingerprint=fingerprintFiles({imageFile});
        require(editedFingerprint!=oldResize->sourceFingerprints[directSlot],"Resize reload fixture did not edit source bytes.");
        const auto coherentEdit=[&](const auto& binding) {
            if(!ready(binding))return false;
            const auto direct=binding.graph->sourceFingerprints[directSlot],baked=binding.graph->sourceFingerprints[resizedSlot];
            const bool oldPair=direct==oldResize->sourceFingerprints[directSlot]&&baked==oldResize->sourceFingerprints[resizedSlot];
            const bool newPair=direct==editedFingerprint&&baked!=oldResize->sourceFingerprints[resizedSlot];
            require(oldPair||newPair,"AssetLibrary published direct and resized pixels from different source-file generations.");
            return newPair;
        };
        resizeBinding=until([&]{return library.materialPreviewBindings(resized,256);},coherentEdit,
            "Edited Resize input did not publish a coherent regenerated packet");
        require(resizeBinding.graph->programs==oldResize->programs&&resizeBinding.graph->uniforms==oldResize->uniforms&&
            resizeBinding.graph->signature!=oldResize->signature&&resources.stats().loads>warmLoads&&
            bgfx::isValid(oldResize->textures[resizedSlot])&&oldResize->leases[resizedSlot],
            "Resize source reload rebuilt shaders, retained old content, or invalidated a leased prior generation.");
        // A cached filter inherits its source dimensions. Do not request the
        // source through AssetLibrary::find: its thumbnail path can refresh the
        // catalog dimensions and hide a stale-size hot-reload regression.
        // AssetLibrary initialization migrates legacy assets/ files into the
        // managed Content layout. Resolve the current physical path here.
        const auto growingFile=fixturePath(p.root,growingImage);
        auto growing=resized;growing.id="blur-size-resource-fixture";growing.name="Blur source size reload";
        findMaterialNode(growing.graph,resizeSource)->texture.image.id=growingImage;
        auto* blurNode=findMaterialNode(growing.graph,resize);blurNode->kind=MaterialNodeKind::Blur;blurNode->name="Blur";blurNode->atomic.amount=.02f;
        canonicalizeMaterialAsset(growing);
        auto growingBinding=until([&]{return library.materialPreviewBindings(growing,4);},
            [&](const auto& binding){return ready(binding)&&binding.graph->programs->optimized&&resources.idle();},
            "Initial 2x2 cached Blur packet did not settle");
        const auto oldBlur=growingBinding.graph;
        const auto blurSlot=sourceSlot(*oldBlur,resize),blurDirectSlot=sourceSlot(*oldBlur,resizeSource);
        const auto originalBlurFingerprint=oldBlur->sourceFingerprints[blurDirectSlot];
        require(originalBlurFingerprint==fingerprintFiles({growingFile})&&oldBlur->sources[blurSlot].resize,
            "Blur size fixture did not use the managed direct/derived source path.");
        (void)resources.takePublications();
        model_test::writeBytes(growingFile,model_test::png(4,2,model_test::Bytes{
            255,64,16,255, 208,96,48,255, 160,128,80,255, 112,160,112,255,
            64,192,144,255, 16,224,176,255, 96,128,208,255, 176,32,240,255}));
        library.onFileChanged(growingFile);
        const auto changedBlurFingerprint=fingerprintFiles({growingFile});
        require(changedBlurFingerprint!=originalBlurFingerprint,"Blur size reload did not change the managed source.");
        const auto pendingBlur=library.materialPreviewBindings(growing,4);
        require(!pendingBlur.sourceReady&&bgfx::isValid(oldBlur->textures[blurSlot])&&oldBlur->leases[blurSlot]&&
            bgfx::isValid(oldBlur->textures[blurDirectSlot])&&oldBlur->leases[blurDirectSlot],
            "A pending source-size change was accepted as ready or invalidated the previous complete packet's leases.");
        growingBinding=until([&]{return library.materialPreviewBindings(growing,4);},[&](const auto& binding){
            if(!ready(binding))return false;
            const auto direct=binding.graph->sourceFingerprints[blurDirectSlot],baked=binding.graph->sourceFingerprints[blurSlot];
            const bool oldPair=direct==originalBlurFingerprint&&baked==oldBlur->sourceFingerprints[blurSlot];
            const bool newPair=direct==changedBlurFingerprint&&baked!=oldBlur->sourceFingerprints[blurSlot];
            require(oldPair||newPair,"Source-size reload published direct and cached Blur pixels from different generations.");
            return newPair&&binding.graph->programs->optimized&&resources.idle();
        },"2x2-to-4x2 source reload did not publish its new cached Blur extent without a thumbnail request");
        require(growingBinding.graph->programs==oldBlur->programs&&growingBinding.graph->uniforms==oldBlur->uniforms&&
            growingBinding.graph->signature!=oldBlur->signature&&bgfx::isValid(oldBlur->textures[blurSlot])&&oldBlur->leases[blurSlot],
            "Source-size reload rebuilt the Blur shader or retired a still-leased previous generation.");
        const auto publications=resources.takePublications();
        const auto published=std::find_if(publications.begin(),publications.end(),[&](const auto& value){
            return value.kind==ResourceKind::TextureMips&&value.variant=="generated-float16-mips-v1:4x2"&&
                value.fingerprint==growingBinding.graph->sourceFingerprints[blurSlot];
        });
        require(published!=publications.end(),"The ready Blur packet did not publish a generated 4x2 image descriptor.");
        ResourceRequestOptions inspect;inspect.variant=published->variant;inspect.retainLoader=false;
        const auto descriptor=resources.requestDerived<streaming::TextureMipCache>(published->id,[](std::stop_token)->ResourceLoadResult<streaming::TextureMipCache>{
            throw std::runtime_error("Inspecting a ready Blur generation unexpectedly scheduled another image job.");
        },std::move(inspect));
        const auto actualExtent=descriptor.snapshot();
        require(descriptor.state()==ResourceState::Ready&&actualExtent&&actualExtent->format==ImagePixelFormat::RGBA16F&&
            !actualExtent->levels.empty()&&actualExtent->levels.front().width==4&&actualExtent->levels.front().height==2,
            "Blur hot reload retained its old 2x2 lattice despite accepting a changed source fingerprint.");
        const auto settledBlur=growingBinding.graph;const auto blurWarmLoads=resources.stats().loads;
        for(unsigned frame=0;frame<60;++frame){
            tick();const auto warm=library.materialPreviewBindings(growing,4);
            require(ready(warm)&&warm.graph->signature==settledBlur->signature&&warm.graph->textureSignatures==settledBlur->textureSignatures&&
                warm.graph->textures[blurSlot].idx==settledBlur->textures[blurSlot].idx,
                "Warm changed-size Blur requests replaced their settled GPU packet.");
        }
        require(resources.stats().loads==blurWarmLoads,"Warm changed-size Blur requests repeated decode or cached image generation.");
        // A pinned node may be disconnected from the material Output. Scale
        // and Disorder still replace its image without compiling another shader.
        MaterialAsset cloud;cloud.id="disconnected-clouds-preview";cloud.name="Clouds preview";
        addMaterialNode(cloud.graph,MaterialNodeKind::Output);
        const auto cloudNode=addMaterialNode(cloud.graph,MaterialNodeKind::ProceduralTexture).id;
        findMaterialNode(cloud.graph,cloudNode)->procedural.settings=material_procedural::default_settings(material_procedural::Pattern::Clouds1);
        findMaterialNode(cloud.graph,cloudNode)->procedural.width=findMaterialNode(cloud.graph,cloudNode)->procedural.height=128;
        canonicalizeMaterialAsset(cloud);const MaterialGraphPreviewTarget cloudTarget{cloudNode,MaterialPin::Value};
        auto nodePrograms=library.createMaterialNodePreviewPrograms();require(bool(nodePrograms),"Pinned node preview program owner is unavailable.");
        const auto requestCloud=[&]{auto result=library.materialNodePreviewBindings(*nodePrograms,cloud,cloudTarget,128);nodePrograms->trim();return result;};
        const auto settledCloud=[&](const auto& binding){return ready(binding)&&binding.graph->programs->optimized&&resources.idle();};
        auto cloudBinding=until(requestCloud,settledCloud,"Initial disconnected Clouds 1 preview did not settle");
        require(cloudBinding.graph->sources.size()==1&&cloudBinding.graph->sources.front().resize,"Clouds preview did not use its cached image boundary.");
        const auto cloudSlot=cloudBinding.graph->sources.front().slot;
        auto cloudPixels=bakeMaterialCachedImage(cloud.graph,cloudNode,{});
        const auto outputHash=materialGraphRenderHash(cloud.surface);auto cloudDefinition=materialNodePreviewDefinitionKey(cloud,cloudTarget);
        for(unsigned control=0;control<2;++control) {
            const auto previous=cloudBinding.graph;const auto previousPixels=cloudPixels;
            if(control==0)findMaterialNode(cloud.graph,cloudNode)->procedural.settings.scale=2;
            else findMaterialNode(cloud.graph,cloudNode)->procedural.settings.disorder=.35f;
            canonicalizeMaterialAsset(cloud);
            require(materialGraphRenderHash(cloud.surface)==outputHash&&materialNodePreviewDefinitionKey(cloud,cloudTarget)!=cloudDefinition,
                "Disconnected procedural edits retained the pinned branch identity or changed the unrelated material Output.");
            cloudDefinition=materialNodePreviewDefinitionKey(cloud,cloudTarget);
            const auto pending=requestCloud();
            require(!pending.sourceReady&&bgfx::isValid(previous->textures[cloudSlot])&&previous->leases[cloudSlot],
                "A pending Clouds parameter edit reported stale pixels as current or invalidated the last-good image.");
            cloudBinding=until(requestCloud,settledCloud,control==0?"Clouds 1 Scale edit did not replace its pinned preview":"Clouds 1 Disorder edit did not replace its pinned preview");
            require(cloudBinding.graph->programs==previous->programs&&cloudBinding.graph->uniforms==previous->uniforms&&
                cloudBinding.graph->sourceFingerprints[cloudSlot]!=previous->sourceFingerprints[cloudSlot]&&
                cloudBinding.graph->textureSignatures[cloudSlot]!=previous->textureSignatures[cloudSlot]&&cloudBinding.graph->signature!=previous->signature,
                "A procedural numeric edit retained stale image/packet fingerprints or recompiled its preview shader.");
            cloudPixels=bakeMaterialCachedImage(cloud.graph,cloudNode,{});std::size_t changedPixels{};
            for(std::size_t i=0;i<cloudPixels.rgba.size();i+=4)changedPixels+=std::abs(cloudPixels.rgba[i]-previousPixels.rgba[i])>1e-4f;
            require(changedPixels>100,"Clouds 1 parameter edit changed only a cache key, not the generated image.");
        }
        // Rapid superseded definitions must not keep the final settled edit
        // behind old jobs, and a layout edit must not regenerate image pixels.
        for(float scale:{1.25f,1.5f,1.75f}){findMaterialNode(cloud.graph,cloudNode)->procedural.settings.scale=scale;canonicalizeMaterialAsset(cloud);tick();(void)requestCloud();}
        const auto preRapid=cloudBinding.graph;
        cloudBinding=until(requestCloud,settledCloud,"Final coalesced Clouds Scale edit did not replace the preview");
        require(cloudBinding.graph->programs==preRapid->programs&&cloudBinding.graph->sourceFingerprints[cloudSlot]!=preRapid->sourceFingerprints[cloudSlot],"Rapid edit settlement retained an obsolete procedural image.");
        const auto stableCloud=cloudBinding.graph;const auto cloudWarmLoads=resources.stats().loads;
        findMaterialNode(cloud.graph,cloudNode)->position={321,654};canonicalizeMaterialAsset(cloud);
        for(unsigned frame=0;frame<40;++frame){tick();const auto warm=requestCloud();require(ready(warm)&&warm.graph->signature==stableCloud->signature&&warm.graph->sourceFingerprints==stableCloud->sourceFingerprints,"Warm/layout-only Clouds preview requests changed its accepted image.");}
        require(resources.stats().loads==cloudWarmLoads,"Warm/layout-only Clouds requests repeated image generation.");
        auto flatGraph=std::make_shared<const MaterialGraph>(cloud.graph);
        const auto awaitFlat=[&](const std::shared_ptr<const MaterialGraph>& graph,const char* failure) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{15};PreparedTexture texture;
            do {tick();texture=library.materialNodeImagePreview(cloud.id,graph,cloudTarget,128);if(texture.sourceReady&&texture.complete&&bgfx::isValid(texture.handle))return texture;std::this_thread::sleep_for(std::chrono::milliseconds{2});}while(std::chrono::steady_clock::now()<deadline);
            throw std::runtime_error(std::string(failure)+": "+texture.diagnostic);
        };
        auto flatCloud=awaitFlat(flatGraph,"Initial flat Clouds preview did not publish");
        for(unsigned control=0;control<2;++control) {
            const auto previous=flatCloud;
            if(control==0)findMaterialNode(cloud.graph,cloudNode)->procedural.settings.scale=1.5f;
            else findMaterialNode(cloud.graph,cloudNode)->procedural.settings.disorder=.2f;
            canonicalizeMaterialAsset(cloud);flatGraph=std::make_shared<const MaterialGraph>(cloud.graph);
            const auto pending=library.materialNodeImagePreview(cloud.id,flatGraph,cloudTarget,128);
            require(!pending.sourceReady&&previous.lease&&bgfx::isValid(previous.handle),"Flat procedural preview accepted stale pixels during an edit or dropped its last-good lease.");
            flatCloud=awaitFlat(flatGraph,control==0?"Flat Clouds Scale edit did not refresh":"Flat Clouds Disorder edit did not refresh");
            require(flatCloud.sourceFingerprint!=previous.sourceFingerprint&&flatCloud.signature!=previous.signature,"Flat Clouds edit retained its accepted image identity.");
        }
        const auto flatWarmLoads=resources.stats().loads;
        for(unsigned frame=0;frame<40;++frame){tick();const auto warm=library.materialNodeImagePreview(cloud.id,flatGraph,cloudTarget,128);require(warm.sourceReady&&warm.complete&&warm.signature==flatCloud.signature,"Unchanged flat procedural preview changed its accepted image.");}
        require(resources.stats().loads==flatWarmLoads,"Warm flat procedural preview requests scheduled another evaluation.");

        // The inline thumbnail and pinned 2D sidebar are display sizes of the
        // same authored cached image. Their tiny display exports must reuse
        // one canonical source rather than generating its lattice twice.
        auto sharedCloud=cloud;sharedCloud.id="shared-node-image-source";sharedCloud.name="Shared node image source";
        auto* sharedSource=findMaterialNode(sharedCloud.graph,cloudNode);
        sharedSource->procedural.width=512;sharedSource->procedural.height=256;sharedSource->procedural.settings.seed+=101;
        canonicalizeMaterialAsset(sharedCloud);const auto sharedAuthored=serializeMaterialAsset(sharedCloud);
        const auto sharedGraph=std::make_shared<const MaterialGraph>(sharedCloud.graph);
        const auto awaitShared=[&](unsigned edge,const char* failure) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{20};PreparedTexture texture;
            do {
                tick();texture=library.materialNodeImagePreview(sharedCloud.id,sharedGraph,cloudTarget,edge);
                if(texture.sourceReady&&texture.complete&&bgfx::isValid(texture.handle)&&resources.idle())return texture;
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
            }while(std::chrono::steady_clock::now()<deadline);
            throw std::runtime_error(std::string(failure)+": "+texture.diagnostic);
        };
        (void)resources.takePublications();
        const auto inlineShared=awaitShared(128,"Shared-source inline thumbnail did not settle");
        const auto inlinePublications=resources.takePublications();
        const auto isSource=[](const auto& publication) {
            return publication.kind==ResourceKind::TextureMips&&publication.variant=="generated-float16-mips-v1:512x256";
        };
        require(std::count_if(inlinePublications.begin(),inlinePublications.end(),isSource)==1,
            "Inline cached-node preview did not publish exactly one complete authored 512x256 source.");
        const auto sourcePublication=*std::find_if(inlinePublications.begin(),inlinePublications.end(),isSource);
        const auto inlineDisplay=std::find_if(inlinePublications.begin(),inlinePublications.end(),[&](const auto& publication) {
            return publication.kind==ResourceKind::TextureMips&&publication.variant=="generated-float16-mips-v1:128x128"&&
                publication.fingerprint==inlineShared.sourceFingerprint;
        });
        require(inlineDisplay!=inlinePublications.end()&&inlineDisplay->id!=sourcePublication.id,
            "Inline display pixels did not publish separately from their canonical source.");
        const auto sidebarShared=awaitShared(256,"Shared-source pinned sidebar image did not settle");
        const auto sidebarPublications=resources.takePublications();
        require(std::none_of(sidebarPublications.begin(),sidebarPublications.end(),isSource),
            "Requesting a 256-pixel sidebar regenerated the accepted 512x256 source used by its inline thumbnail.");
        const auto sidebarDisplay=std::find_if(sidebarPublications.begin(),sidebarPublications.end(),[&](const auto& publication) {
            return publication.kind==ResourceKind::TextureMips&&publication.variant=="generated-float16-mips-v1:256x256"&&
                publication.fingerprint==sidebarShared.sourceFingerprint;
        });
        require(sidebarDisplay!=sidebarPublications.end()&&sidebarDisplay->id!=sourcePublication.id&&sidebarDisplay->id!=inlineDisplay->id&&
            inlineShared.width==128&&inlineShared.height==128&&sidebarShared.width==256&&sidebarShared.height==256,
            "Inline/sidebar display exports lost their independent size or shared the wrong image identity.");
        ResourceRequestOptions inspectShared;inspectShared.variant=sourcePublication.variant;inspectShared.retainLoader=false;
        const auto sharedDescriptor=resources.requestDerived<streaming::TextureMipCache>(sourcePublication.id,[](std::stop_token)->ResourceLoadResult<streaming::TextureMipCache> {
            throw std::runtime_error("Inspecting the shared canonical source scheduled another generation.");
        },std::move(inspectShared));
        const auto sharedSnapshot=sharedDescriptor.snapshot();
        require(sharedDescriptor.state()==ResourceState::Ready&&sharedSnapshot&&sharedSnapshot.fingerprint()==sourcePublication.fingerprint&&
            sharedSnapshot.revision()==sourcePublication.revision&&sharedSnapshot->format==ImagePixelFormat::RGBA16F&&
            !sharedSnapshot->levels.empty()&&sharedSnapshot->levels.front().width==512&&sharedSnapshot->levels.front().height==256,
            "Inline/sidebar reuse replaced or downscaled its immutable canonical source descriptor.");
        const auto sharedWarmLoads=resources.stats().loads;
        for(unsigned frame=0;frame<40;++frame) {
            tick();const auto inlineWarm=library.materialNodeImagePreview(sharedCloud.id,sharedGraph,cloudTarget,128);
            const auto sidebarWarm=library.materialNodeImagePreview(sharedCloud.id,sharedGraph,cloudTarget,256);
            require(inlineWarm.sourceReady&&inlineWarm.complete&&inlineWarm.signature==inlineShared.signature&&
                sidebarWarm.sourceReady&&sidebarWarm.complete&&sidebarWarm.signature==sidebarShared.signature,
                "Warm inline/sidebar polling changed its accepted shared-source display images.");
        }
        require(resources.stats().loads==sharedWarmLoads&&resources.takePublications().empty()&&serializeMaterialAsset(sharedCloud)==sharedAuthored,
            "Warm inline/sidebar polling repeated source/display work or changed the authored graph.");
        library.releaseMaterialNodeImagePreviews(sharedCloud.id);
        const auto proceduralAsset=library.createMaterial("Procedural Resize UV0");
        require(bool(proceduralAsset),"Could not create the procedural Resize compatibility fixture.");
        auto procedural=*proceduralAsset;procedural.graph={};
        const auto proceduralOutput=addMaterialNode(procedural.graph,MaterialNodeKind::Output).id;
        const auto proceduralNoise=addMaterialNode(procedural.graph,MaterialNodeKind::Noise).id;
        const auto proceduralResize=addMaterialNode(procedural.graph,MaterialNodeKind::Resize).id;
        connectMaterialNodes(procedural.graph,proceduralNoise,MaterialPin::RGB,proceduralResize,MaterialPin::Left);
        connectMaterialNodes(procedural.graph,proceduralResize,MaterialPin::Value,proceduralOutput,MaterialPin::BaseColor);
        canonicalizeMaterialAsset(procedural);library.publishMaterial(std::make_shared<MaterialAsset>(procedural));
        auto missingUv=*geometry;missingUv.materialIds.assign(missingUv.materials.size(),procedural.id);
        for(auto& primitive:missingUv.primitives)primitive.uvMask=0;
        bool missingUvRejected=false;try{(void)library.resolvedModelMaterials(missingUv);}catch(const std::exception& error) {
            missingUvRejected=std::string(error.what()).find("unavailable UV set")!=std::string::npos;
        }
        require(missingUvRejected,"A procedural-only Resize bypassed its required UV0 compatibility check.");
        for(auto& primitive:missingUv.primitives)primitive.uvMask=1;
        require(library.resolvedModelMaterials(missingUv).size()==missingUv.materials.size(),
            "A procedural-only Resize rejected a mesh supplying UV0.");
        require(read(fixturePath(p.root,material.id))==sourceBytes,"Runtime material edits rewrote the authored fixture.");
        resources.stop();
    }materialPreviewSourceLifetime(compiler,sources,includes);bgfx::shutdown();}catch(...){bgfx::shutdown();throw;}
    std::cout<<"Material preview coherent coarse publication and selected texture recovery under zero cache budget passed.\n";
}
}

int main(int argc,char** argv){try{
    if(argc==5&&std::string(argv[1])=="--material-preview-input-lifetime") {
        bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
        require(bgfx::init(init),"Could not initialize the shared node-preview input lifetime check.");
        try{materialPreviewSourceLifetime(argv[2],argv[3],argv[4]);bgfx::shutdown();}catch(...){bgfx::shutdown();throw;}
        return 0;
    }
    if(argc==5&&std::string(argv[1])=="--material-preview-resources") {materialPreviewResources(argv[2],argv[3],argv[4]);return 0;}
    // Public deletion checks use disposable CPU data; the final fallback check
    // opens a scoped Noop device without a native application window.
    warningQueries();bundledModelPublicationAndReferences();manualBundledModelDeletion(false);manualBundledModelDeletion(true);bundledBatchDeletionContinuesFrames(false);bundledBatchDeletionContinuesFrames(true);multiBundleBatchDeletionWithSurvivingFallbacks();externallyChangedDeletionPolicy(0);externallyChangedDeletionPolicy(1);externallyChangedDeletionPolicy(2);lastBundledMeshFolderCleanup(false);lastBundledMeshFolderCleanup(true);referencedBundledModelDeletion(false);referencedBundledModelDeletion(true);cancelAndStaleBundledDeletion();failedBundledDeletion();retiredQuaderModelCollection(false);retiredQuaderModelCollection(true);retiredQuaderPolicyRetention();legacyEnvironmentGlowPersistence();archivedLegacyWarnings();embeddedMaterialWarningOwners();removeModelAcrossScenes(false);removeModelAcrossScenes(true);fallbackReopening();cancelAndStaleDeletion();activeSceneAndFolderDeletion();particleMaterialDeletionAndAuthority();spriteImageDeletionAndIncompatibleMaterial();changedUsagePreservesConsumerAssignments();boxMaterialDeletionAndPublication();importedMaterialSlotsAndDeletion();legacyUnusedModelMaterialAssignments();immutableMaterialMetadata();boxMaterialHistoryOnlyDeletion();dormantGraphTextureDeletion();pendingGraphKeepsCoverageExplicit();navigationProfileDeletion();gameplayEffectDeletion();unrelatedDeletionMaterializesLegacyEffects(false);unrelatedDeletionMaterializesLegacyEffects(true);unrelatedDeletionMaterializesLegacyProfiles(false);unrelatedDeletionMaterializesLegacyProfiles(true);
    std::cout<<"Public AssetLibrary deletion, warning, fallback and document integration checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"AssetLibrary deletion test failed: "<<error.what()<<'\n';return 1;}}
