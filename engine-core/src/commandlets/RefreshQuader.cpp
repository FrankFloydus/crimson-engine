#include "assets/AssetReferenceManager.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialAsset.h"
#include "assets/ModelAsset.h"
#include "assets/ModelImportIdentity.h"
#include "editor/QuaderSceneIntegration.h"
#include "quader/QuaderBridge.h"
#include "quader/QuaderPaintMigration.h"
#include "resources/AssetId.h"
#include "resources/Paths.h"
#include "scene/SceneDocument.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
using namespace crimson;
using Json=nlohmann::json;
namespace fs=std::filesystem;

void require(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
std::string utf8(const fs::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::string hexadecimal(std::uint64_t value) {
    std::ostringstream text;text<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return text.str();
}
SceneSnapshot snapshot(const SceneDocument& document) {
    SceneSnapshot result{document.assetId(),document.displayName(),document.scene().records(),
        document.scene().properties(),document.previewCamera()};
    result.stagedNavigationProfiles=document.stagedNavigationAgentProfiles();
    result.stagedGameplayEffects=document.stagedGameplayEffects();
    return result;
}
std::string serialized(const SceneDocument& document) {
    return serializeSceneSnapshot(snapshot(document));
}
Json paintSummary(const std::optional<SceneMeshPaint>& paint) {
    if(!paint)return nullptr;
    Json draws=Json::array();
    for(const auto& draw:paint->draws) {
        require(draw.weights&&validMeshPaintPayload(*draw.weights),"Paint summary contains an invalid payload.");
        const auto bytes=meshPaintBytes(*draw.weights);
        std::array<std::uint64_t,3> totals{};std::uint64_t nonzero{};
        for(std::uint32_t vertex=0;vertex<draw.weights->vertexCount;++vertex) {
            const auto rgb=draw.weights->rgb(vertex);
            for(unsigned channel=0;channel<3;++channel)totals[channel]+=rgb[channel];
            nonzero+=rgb[0]||rgb[1]||rgb[2];
        }
        draws.push_back({{"draw",draw.draw},{"primitive",draw.primitive},{"level",draw.level},
            {"source",hexadecimal(draw.sourceSignature)},{"vertices",draw.weights->vertexCount},
            {"rgb8Sha256",modelImportDataDigest(bytes)},{"nonzeroVertices",nonzero},{"channelSums",totals}});
    }
    return {{"level",paint->subdivisionLevel},{"recipe",paint->recipeVersion},{"draws",std::move(draws)}};
}
Json modelSummary(const ModelAsset& model) {
    Json primitives=Json::array();std::size_t quadCount{},faceCount{};
    for(std::size_t index=0;index<model.primitives.size();++index) {
        const auto& primitive=model.primitives[index];
        const auto quads=std::count_if(primitive.sourceFaces.begin(),primitive.sourceFaces.end(),
            [](const auto& face){return face.cornerCount==4;});
        quadCount+=quads;faceCount+=primitive.sourceFaces.size();
        primitives.push_back({{"primitive",index},{"materialSlot",primitive.material},
            {"sourceFaces",primitive.sourceFaces.size()},{"sourceQuads",quads},
            {"vertices",primitive.sourceVertexCount},{"indices",primitive.sourceIndexCount}});
    }
    return {{"id",model.id},{"name",model.name},{"sourceFaces",faceCount},{"sourceQuads",quadCount},
        {"draws",model.draws.size()},{"primitives",std::move(primitives)}};
}
SceneSnapshot originalFromRecovery(const SceneSnapshot& current,const SceneSnapshot& recovery,EntityId owner) {
    require(current.id!=recovery.id,"The recovery scene must have a different identity from the scene being upgraded.");
    require(current.records.size()==recovery.records.size(),"The scene entity set changed after the original topology upgrade; recovery cannot be applied safely.");
    auto comparable=recovery;comparable.id=current.id;comparable.name=current.name;
    std::unordered_map<EntityId,const EntityRecord*> currentEntities;
    for(const auto& record:current.records)
        require(currentEntities.emplace(record.id,&record).second,"The current scene has duplicate entity identities.");
    std::unordered_map<EntityId,bool> recoveryEntities;bool foundOwner{};
    for(auto& record:comparable.records) {
        require(recoveryEntities.emplace(record.id,true).second,"The recovery scene has duplicate entity identities.");
        const auto found=currentEntities.find(record.id);
        require(found!=currentEntities.end(),"The recovery scene has a different entity set from the current scene.");
        const auto& present=*found->second;
        if(record.id==owner) {
            require(record.quader&&present.quader,"The Quader owner changed after the original topology upgrade.");
            require(!record.quader->autoSync,"The original recovery scene must have automatic Quader synchronization disabled.");
            record.quader->saveKey=present.quader->saveKey;
            record.quader->autoSync=present.quader->autoSync;foundOwner=true;
        }
        if(record.quaderGenerated&&record.quaderGenerated->owner==owner&&record.model&&present.model) {
            record.model=present.model;
            record.meshPaint=present.meshPaint;
        }
    }
    require(foundOwner,"The recovery scene does not contain the requested Quader owner.");
    require(serializeSceneSnapshot(comparable)==serializeSceneSnapshot(current),
        "The current scene has authored changes beyond generated model/paint and the owner's save key. Recovery was not applied.");
    auto result=recovery;result.id=current.id;result.name=current.name;result.fingerprint=current.fingerprint;
    for(auto& record:result.records)if(record.id==owner)record.quader->autoSync=currentEntities.at(owner)->quader->autoSync;
    return result;
}
struct Options {fs::path root;std::string scene,recovery;EntityId owner{};};
Options options(int argc,char** argv) {
    Options result;std::string owner;
    for(int index=1;index<argc;++index) {
        const std::string option=argv[index];
        require(index+1<argc,"Expected a value after "+option+".");
        const std::string value=argv[++index];
        if(option=="--project-root")result.root=pathFromUtf8(value);
        else if(option=="--scene")result.scene=value;
        else if(option=="--from-recovery")result.recovery=value;
        else if(option=="--owner")owner=value;
        else throw std::runtime_error("Unknown option: "+option);
    }
    require(!result.root.empty()&&isAssetUuid(result.scene)&&!owner.empty(),
        "Specify --project-root <root> --scene <scene UUID> --owner <hex entity ID>.");
    require(result.recovery.empty()||isAssetUuid(result.recovery),"The recovery scene must be an asset UUID.");
    std::string_view digits=owner;if(digits.starts_with("0x")||digits.starts_with("0X"))digits.remove_prefix(2);
    const auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),result.owner,16);
    require(!digits.empty()&&parsed.ec==std::errc{}&&parsed.ptr==digits.data()+digits.size()&&result.owner,
        "The owner must be a nonzero hexadecimal entity ID.");
    result.root=fs::weakly_canonical(result.root);
    require(fs::is_directory(result.root),"The project directory does not exist.");
    return result;
}
}

int main(int argc,char** argv) {
    try {
        const auto settings=options(argc,argv);const auto& root=settings.root;
        initializeContentStorage(root);
        AssetReferenceManager policy(root);require(policy.valid(),policy.status());
        SceneDocument document(root);require(document.load(settings.scene),document.status());
        std::optional<SceneSnapshot> existingRecovery;
        if(!settings.recovery.empty()) {
            const auto current=readSceneSnapshot(root,document.assetId());
            require(current.fingerprint==document.loadedFingerprint(),"The scene changed before its recovery input could be captured.");
            existingRecovery=readSceneSnapshot(root,settings.recovery);
            const auto restored=originalFromRecovery(current,*existingRecovery,settings.owner);
            require(document.openPreparedSnapshot(restored),document.status());
            require(!document.externalFileChanged(),"The current scene changed while its original paint was being restored in memory.");
        }
        const auto owner=document.scene().find(settings.owner);
        require(owner&&owner->quader,"The selected entity is not a linked Quader owner.");
        require(!owner->quader->projectPath.empty(),"The Quader owner has no source project.");
        const auto beforeSnapshot=snapshot(document);const auto before=serialized(document);
        const auto historySize=document.history().size();
        require(!document.transactionActive()&&!document.dirty(),"The loaded scene must have no pending edit.");

        std::unordered_map<std::string,std::shared_ptr<const ModelAsset>> models;
        const auto loadModel=[&](const std::string& id)->std::shared_ptr<const ModelAsset> {
            if(const auto found=models.find(id);found!=models.end())return found->second;
            ModelResolvers resolvers;
            resolvers.image=[&](const std::string& image){return policy.resolveTexture(image);};
            resolvers.material=[&](const std::string& material)->std::optional<ModelMaterial> {
                const auto resolved=policy.resolveModelMaterial(material);
                return loadMaterialAsset(root,resolved).surface;
            };
            auto model=loadModelPackage(resolveAssetPath(root,id),root,resolvers);
            require(model&&model->id==id,"The captured model generation has the wrong identity.");
            models.emplace(id,model);return model;
        };
        const auto inputs=[&](EntityId target) {
            std::vector<QuaderPaintInput> result;
            for(const auto& record:document.scene().records())if(record.model&&record.quaderGenerated&&record.quaderGenerated->owner==target)
                result.push_back({record.quaderGenerated->sourceKey,record.model->id,loadModel(record.model->id),record.meshPaint});
            return result;
        };
        Json report{{"version",1},{"scene",document.assetId()},{"owner",hexadecimal(settings.owner)},
            {"sceneFile",utf8(sceneAssetPath(root,document.assetId()))},{"migrations",Json::array()}};
        QuaderBridge bridge(root);bool applied{},undoVerified{};std::string applicationError;
        std::vector<QuaderTarget> targets{{settings.owner,*owner->quader,true,owner->quader->saveKey}};
        QuaderBridgeHooks hooks;
        hooks.dirtyMaterial=[](const std::string&){return false;};
        hooks.resolveMaterial=[&](const std::string& id){return policy.resolveMaterial(id);};
        hooks.resolveTexture=[&](const std::string& id,bool normal){return policy.resolveTexture(id,normal);};
        hooks.reservedAsset=[&](const std::string& id){return policy.deleted(id);};
        hooks.assetPolicyRevision=[&]{return policy.revision();};
        hooks.capturePaintInputs=[&](EntityId target)->std::optional<std::vector<QuaderPaintInput>> {return inputs(target);};
        hooks.paintInputsCurrent=[&](EntityId target,const QuaderImportResult& result) {
            const auto current=inputs(target);if(current.size()!=result.paintInputs.size())return false;
            for(const auto& input:result.paintInputs) {
                const auto found=std::find_if(current.begin(),current.end(),[&](const auto& value){return value.sourceKey==input.sourceKey;});
                if(found==current.end()||found->modelId!=input.modelId||found->paint!=input.paint)return false;
            }
            return true;
        };
        hooks.applyImport=[&](EntityId target,const QuaderLink& link,const QuaderImportResult& result) {
            try {
                require(result.topologyOnly,"This commandlet only upgrades unchanged-source Quader topology. The source or import policies changed; use ordinary editor reimport.");
                require(!document.externalFileChanged()&&serialized(document)==before,"The original scene changed while the upgrade was preparing.");
                require(applyQuaderSceneImport(document,target,link,result,[&](const std::string& id)->std::optional<std::vector<std::string>> {
                    return loadModel(id)->materialIds;
                }),document.status());
                require(document.history().size()==historySize+1&&document.history().undoLabel()=="Import Quader",
                    "The Quader upgrade did not create exactly one import history command.");
                const auto after=serialized(document);
                require(document.undo()&&serialized(document)==before,"Quader upgrade undo did not restore the complete original scene.");
                require(document.redo()&&serialized(document)==after,"Quader upgrade redo did not restore the complete upgraded scene.");
                undoVerified=true;
                report["sourceKeyBefore"]=owner->quader->saveKey;report["sourceKeyAfter"]=result.sourceKey;
                report["topologyOnly"]=result.topologyOnly;report["importedModels"]=result.meshes.size();
                for(const auto& replacement:result.paintReplacements) {
                    const auto incoming=std::find_if(result.meshes.begin(),result.meshes.end(),[&](const auto& mesh){return mesh.sourceKey==replacement.sourceKey;});
                    require(incoming!=result.meshes.end(),"A paint replacement has no incoming model occurrence.");
                    const auto model=std::find_if(result.preparedModels.begin(),result.preparedModels.end(),[&](const auto& value){return value&&value->id==incoming->modelId;});
                    require(model!=result.preparedModels.end(),"A paint replacement has no prepared model generation.");
                    report["migrations"].push_back({{"sourceKey",replacement.sourceKey},
                        {"beforeModel",modelSummary(*loadModel(replacement.beforeModelId))},{"afterModel",modelSummary(**model)},
                        {"beforePaint",paintSummary(replacement.before)},{"afterPaint",paintSummary(replacement.after)},
                        {"maxWeightError",replacement.maxError},{"rmsWeightError",replacement.rmsError}});
                }
                targets[0].link=link;targets[0].link.saveKey=result.sourceKey;
                targets[0].consumedSaveKey=result.sourceKey;applied=true;return true;
            }catch(const std::exception& error){applicationError=error.what();throw;}
        };
        hooks.publishAssets=[&](const QuaderImportResult& result) {
            for(const auto& model:result.preparedModels)if(model)models[model->id]=model;
        };
        bridge.requestImport(settings.owner,*owner->quader,document.documentGeneration());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes{10};
        while(bridge.busy(settings.owner)&&std::chrono::steady_clock::now()<deadline) {
            bridge.update(targets,document.documentGeneration(),false,hooks);
            if(!applicationError.empty()) {
                bridge.cancel(settings.owner);
                throw std::runtime_error(applicationError);
            }
            if(bridge.busy(settings.owner))std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        require(!bridge.busy(settings.owner),"The Quader upgrade did not finish within ten minutes: "+bridge.status(settings.owner));
        require(applied&&undoVerified,applicationError.empty()?"Quader upgrade was not applied: "+bridge.status(settings.owner):applicationError);
        require(!document.externalFileChanged(),"The original scene changed before the upgrade could be saved.");

        if(existingRecovery) {
            const auto persisted=readSceneSnapshot(root,existingRecovery->id);
            require(persisted.fingerprint==existingRecovery->fingerprint&&
                serializeSceneSnapshot(persisted)==serializeSceneSnapshot(*existingRecovery),
                "The original recovery scene changed while its paint was being migrated.");
            auto expected=beforeSnapshot;expected.id=existingRecovery->id;expected.name=existingRecovery->name;
            for(auto& record:expected.records)if(record.id==settings.owner&&record.quader)record.quader->autoSync=false;
            require(serializeSceneSnapshot(expected)==serializeSceneSnapshot(*existingRecovery),
                "The existing recovery scene does not match the verified original paint baseline.");
            report["recoveryScene"]=existingRecovery->id;
            report["recoveryFile"]=utf8(sceneAssetPath(root,existingRecovery->id));
            report["recoveryReused"]=true;
        } else {
            auto recoverySnapshot=beforeSnapshot;
            for(auto& record:recoverySnapshot.records)if(record.id==settings.owner&&record.quader)record.quader->autoSync=false;
            SceneDocument recovery(root);require(recovery.openPreparedSnapshot(recoverySnapshot),recovery.status());
            const auto recoveryName=availableSceneName(root,document.displayName()+" Before Quad Grid Upgrade");
            require(recovery.save(recoveryName,true),recovery.status());
            recoverySnapshot.id=recovery.assetId();recoverySnapshot.name=recovery.displayName();
            require(serializeSceneSnapshot(readSceneSnapshot(root,recovery.assetId()))==serializeSceneSnapshot(recoverySnapshot),
                "The durable recovery scene does not match the original scene and paint.");
            report["recoveryScene"]=recovery.assetId();report["recoveryFile"]=utf8(sceneAssetPath(root,recovery.assetId()));
            report["recoveryReused"]=false;
        }
        report["undoVerified"]=true;
        report["undoHistoryPersistsAcrossReopen"]=false;
        require(document.save(),document.status());
        require(serializeSceneSnapshot(readSceneSnapshot(root,document.assetId()))==serialized(document),
            "The saved upgraded scene does not match the verified document.");
        report["saved"]=true;report["status"]=bridge.status(settings.owner);
        std::cout<<report.dump(2)<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Quader topology refresh failed: "<<error.what()<<'\n';return 1;}
}
