#include "DeletionPreparation.h"
#include "MaterialAsset.h"
#include "ContentStorage.h"
#include "QuaderSceneBundle.h"
#include "resources/AssetId.h"
#include "MaterialGraphReferences.h"
#include "ParticleAsset.h"
#include "resources/Paths.h"
#include "resources/ResourceManager.h"
#include "resources/StreamingModel.h"
#include "resources/ImageDecoder.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
void canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Asset deletion preparation canceled.");}
void materialDependencies(const std::filesystem::path& root,const MaterialAsset& definition,const AssetReferenceManager& policy,std::vector<std::filesystem::path>& result) {
    if(!isEngineAssetId(definition.id))result.push_back(checkedManagedAssetPath(root,definition.id));
    visitMaterialTextures(definition.surface,false,[&](const auto& id,MaterialTextureRole role){
        const auto image=policy.resolveTexture(id,role==MaterialTextureRole::Normal);
        if(image.empty()||isEngineAssetId(image))return;
        const auto file=checkedManagedAssetPath(root,image);auto extension=file.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(extension==".hdr")throw std::runtime_error("A material cannot use HDR images in material slots.");result.push_back(file);
    });
}
std::vector<std::filesystem::path> dependencies(const std::filesystem::path& root,std::span<const AssetEntry> models,const AssetReferenceManager& policy,std::span<const AssetEntry> particles){
    std::vector<std::filesystem::path> result;
    for(const auto& model:models){const auto file=checkedManagedAssetPath(root,model.id);result.push_back(file);std::ifstream input(file);if(!input)throw std::runtime_error("Model manifest is unavailable: "+model.id);
        if(const auto bundle=quaderBundleSource(root,model.id))result.push_back(*bundle);
        const auto manifest=nlohmann::json::parse(input);const auto document=file.parent_path()/pathFromUtf8(manifest.at("document").get<std::string>());result.push_back(document);
        for(const auto& dependency:manifest.at("dependencies")){const auto identity=derivedAssetId(model.id,"image:"+dependency.at("path").get<std::string>());
            if(dependency.at("kind")=="image"){const auto target=policy.resolveTexture(identity);if(!target.empty()&&!isEngineAssetId(target))result.push_back(checkedManagedAssetPath(root,target));}
            else result.push_back(file.parent_path()/pathFromUtf8(dependency.at("path").get<std::string>()));
        }
        const auto metadata=loadModelMaterialMetadata(file,root,ModelResolvers{[policy](const auto& id){return policy.resolveTexture(id);},{}});
        for(const auto& id:metadata->materialIds){const auto target=policy.resolve(id);if(!target.empty()&&!isEngineAssetId(target)){
            const auto material=checkedManagedAssetPath(root,target);if(std::filesystem::is_regular_file(material)){
                const auto definition=loadMaterialAsset(root,target);
                materialDependencies(root,definition,policy,result);
            }
        }}
    }
    std::vector<StoredAssetPath> materialFiles;
    if(contentStorageReady(root))materialFiles=storedAssetPaths(root);
    else {const auto materials=root/"assets/materials";if(std::filesystem::exists(materials))for(const auto& file:std::filesystem::directory_iterator(materials))
        if(file.is_regular_file())materialFiles.push_back({assetIdAtPath(root,file.path()),file.path()});}
    for(const auto& file:materialFiles)if(file.file.extension()==".crimson-material"&&std::filesystem::is_regular_file(file.file)) {
        const auto& id=file.id;
        if(policy.deleted(id))continue;const auto definition=loadMaterialAsset(root,id);materialDependencies(root,definition,policy,result);
    }
    for(const auto& entry:particles){result.push_back(checkedManagedAssetPath(root,entry.id));const auto effect=loadParticleAsset(root,entry.id);
        const auto material=loadMaterialAsset(root,policy.resolveParticleMaterial(effect.emitter.material.id));
        materialDependencies(root,material,policy,result);
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
}
std::shared_ptr<DeletionValidation> validateAssetDeletion(const std::filesystem::path& root,std::span<const AssetEntry> models,const AssetReferenceManager& policy,
    std::stop_token stop,const std::shared_ptr<DeletionProgress>& progress,std::span<const AssetEntry> particles){
    canceled(stop);auto result=std::make_shared<DeletionValidation>();result->dependencies=dependencies(root,models,policy,particles);
    std::sort(result->dependencies.begin(),result->dependencies.end());
    result->dependencies.erase(std::unique(result->dependencies.begin(),result->dependencies.end()),result->dependencies.end());
    const auto before=fingerprintFileStates(result->dependencies);
    const ModelResolvers resolvers{[policy](const auto& id){return policy.resolveTexture(id);},[policy,root](const auto& id)->std::optional<ModelMaterial>{
        const auto target=policy.resolveMaterial(id);if(target==DefaultMaterialId)return defaultMaterialAsset().surface;
        if(target==DefaultParticleMaterialId){auto material=defaultParticleMaterialAsset();canonicalizeMaterialAsset(material);return material.surface;}
        if(!target.empty()&&std::filesystem::exists(resolveAssetPath(root,target))){const auto material=loadMaterialAsset(root,target);return modelMaterialCompatible(material)?material.surface:defaultMaterialAsset().surface;}return {};}};
    for(const auto& entry:models){canceled(stop);auto model=loadModelPackage(resolveAssetPath(root,entry.id),root,resolvers);canceled(stop);
        model->stream=std::make_shared<streaming::ModelStream>(streaming::buildModelCache(*model,root));
        for(const auto& surface:model->materials)visitMaterialTextures(surface,false,[&](const auto& id,MaterialTextureRole role){
            const auto image=policy.resolveTexture(id,role==MaterialTextureRole::Normal);
            if(!image.empty()&&!isEngineAssetId(image))(void)decodeManagedImage(checkedManagedAssetPath(root,image));
        });
        for(const auto& primitive:model->primitives) {
            if(primitive.material>=model->materials.size())throw std::runtime_error("Replacement model has an invalid material index.");
            const auto& surface=model->materials[primitive.material];if(!surface.graph)continue;
            const auto& graph=*surface.graph;const auto& output=materialOutputNode(graph).output;
            for(const auto id:materialGraphOrder(graph,output.shading,output.blend)) {
                const auto& node=*findMaterialNode(graph,id);if(node.kind!=MaterialNodeKind::Texture2D||node.texture.image.empty())continue;
                auto mode=MaterialUvMode::UV0;if(const auto* link=materialInputLink(graph,node.id,MaterialPin::UV))mode=resolveMaterialUvMapping(graph,link->fromNode).mode;
                if(mode==MaterialUvMode::LocalTriplanar||mode==MaterialUvMode::WorldTriplanar)continue;
                const auto uv=mode==MaterialUvMode::UV1?1u:0u;
                if(!(primitive.uvMask&(1u<<uv)))throw std::runtime_error("Replacement graph requires an unavailable UV set: "+surface.name);
                // Generated normal evaluation derives a basis from the selected
                // UV set when it differs from the mesh's imported tangent UV.
            }
        }
        for(auto& primitive:model->primitives){primitive.sourceVertexCount=primitive.vertices.size();primitive.sourceIndexCount=primitive.indices.size();primitive.vertices=decltype(primitive.vertices){};primitive.indices=decltype(primitive.indices){};}
        result->bytes+=sizeof(ModelAsset)+model->nodes.size()*sizeof(ModelNode)+model->draws.size()*sizeof(ModelDraw)+model->materials.size()*sizeof(ModelMaterial)+model->primitives.size()*sizeof(ModelPrimitive);
        result->models.push_back(std::move(model));if(progress)progress->completed.fetch_add(1);
    }
    for(const auto& entry:particles){
        canceled(stop);const auto definition=loadParticleAsset(root,entry.id);canceled(stop);
        auto material=loadMaterialAsset(root,policy.resolveParticleMaterial(definition.emitter.material.id));
        if(!particleMaterialCompatible(material))material=defaultParticleMaterialAsset();
        visitMaterialTextures(material.surface,false,[&](const auto& id,MaterialTextureRole role){
            const auto image=policy.resolveTexture(id,role==MaterialTextureRole::Normal);
            if(!image.empty()&&!isEngineAssetId(image))(void)decodeManagedImage(checkedManagedAssetPath(root,image));
        });
        result->bytes+=sizeof(ParticleEffectAsset)+definition.id.size()+definition.name.size();
        if(progress)progress->completed.fetch_add(1);
    }
    canceled(stop);result->fileStateFingerprint=fingerprintFileStates(result->dependencies);
    if(before!=result->fileStateFingerprint)throw std::runtime_error("Asset dependencies changed during deletion validation.");
    return result;
}
bool deletionValidationCurrent(const DeletionValidation& validation){try{return fingerprintFileStates(validation.dependencies)==validation.fileStateFingerprint;}catch(...){return false;}}
}
