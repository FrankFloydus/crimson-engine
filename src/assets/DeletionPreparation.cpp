#include "DeletionPreparation.h"
#include "MaterialAsset.h"
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
void materialDependencies(const std::filesystem::path& root,const MaterialAsset& definition,const AssetReferenceManager& policy,std::vector<std::filesystem::path>& result,bool particle) {
    if(!definition.id.starts_with("builtin:"))result.push_back(checkedManagedAssetPath(root,definition.id));
    const auto& surface=definition.surface;const std::array<const ModelTexture*,5> slots{{&surface.baseColorTexture,&surface.metallicRoughnessTexture,&surface.normalTexture,&surface.occlusionTexture,&surface.emissiveTexture}};
    for(size_t slot=0;slot<slots.size();++slot)if(!slots[slot]->image.empty()){
        const auto image=particle?policy.resolveParticleTexture(slots[slot]->image.id):policy.resolveTexture(slots[slot]->image.id,slot==2);
        if(image.empty()||image.starts_with("builtin:"))continue;
        const auto file=checkedManagedAssetPath(root,image);auto extension=file.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(extension==".hdr")throw std::runtime_error("A material cannot use HDR images in material slots.");result.push_back(file);
    }
}
std::vector<std::filesystem::path> dependencies(const std::filesystem::path& root,std::span<const AssetEntry> models,const AssetReferenceManager& policy,std::span<const AssetEntry> particles){
    std::vector<std::filesystem::path> result;
    for(const auto& model:models){const auto file=checkedManagedAssetPath(root,model.id);result.push_back(file);std::ifstream input(file);if(!input)throw std::runtime_error("Model manifest is unavailable: "+model.id);
        const auto manifest=nlohmann::json::parse(input);const auto document=file.parent_path()/pathFromUtf8(manifest.at("document").get<std::string>());result.push_back(document);
        for(const auto& dependency:manifest.at("dependencies")){const auto id=(file.parent_path()/pathFromUtf8(dependency.at("path").get<std::string>())).lexically_relative(root);
            const auto utf8=id.generic_u8string();const std::string identity(reinterpret_cast<const char*>(utf8.data()),utf8.size());
            if(dependency.at("kind")=="image"){const auto target=policy.resolveTexture(identity);if(!target.empty()&&!target.starts_with("builtin:"))result.push_back(checkedManagedAssetPath(root,target));}
            else result.push_back(checkedManagedAssetPath(root,identity));
        }
        const auto metadata=loadModelMaterialMetadata(file,root,ModelResolvers{[policy](const auto& id){return policy.resolveTexture(id);},{}});
        for(const auto& id:metadata->materialIds){const auto target=policy.resolve(id);if(!target.empty()&&!target.starts_with("builtin:")){
            const auto material=checkedManagedAssetPath(root,target);if(std::filesystem::is_regular_file(material)){
                const auto definition=loadMaterialAsset(root,target);if(!modelMaterialCompatible(definition))throw std::runtime_error("Model material requires Standard PBR with Opaque or Mask blending: "+definition.name);
                materialDependencies(root,definition,policy,result,false);
            }
        }}
    }
    const auto materials=root/"assets/materials";if(std::filesystem::exists(materials))for(const auto& file:std::filesystem::directory_iterator(materials))if(file.is_regular_file()&&file.path().extension()==".crimson-material") {
        const auto text=file.path().lexically_relative(root).generic_u8string();const std::string id(reinterpret_cast<const char*>(text.data()),text.size());
        if(policy.deleted(id))continue;const auto definition=loadMaterialAsset(root,id);materialDependencies(root,definition,policy,result,particleMaterialCompatible(definition));
    }
    for(const auto& entry:particles){result.push_back(checkedManagedAssetPath(root,entry.id));const auto effect=loadParticleAsset(root,entry.id);
        const auto material=loadMaterialAsset(root,policy.resolveParticleMaterial(effect.emitter.material.id));
        if(!particleMaterialCompatible(material))throw std::runtime_error("Particle effect references an incompatible sprite material: "+entry.name+" / "+material.name);
        materialDependencies(root,material,policy,result,true);
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
        if(target==DefaultParticleMaterialId)throw std::runtime_error("Particle material is unsupported by the model renderer.");
        if(!target.empty()&&std::filesystem::exists(root/pathFromUtf8(target))){const auto material=loadMaterialAsset(root,target);if(!modelMaterialCompatible(material))throw std::runtime_error("Model material requires Standard PBR with Opaque or Mask blending: "+material.name);return material.surface;}return {};}};
    for(const auto& entry:models){canceled(stop);auto model=loadModelPackage(root/pathFromUtf8(entry.id),root,resolvers);canceled(stop);
        model->stream=std::make_shared<streaming::ModelStream>(streaming::buildModelCache(*model,root));
        for(const auto& surface:model->materials)for(const auto* slot:{&surface.baseColorTexture,&surface.metallicRoughnessTexture,&surface.normalTexture,&surface.occlusionTexture,&surface.emissiveTexture})if(!slot->image.empty()){
            const auto image=policy.resolveTexture(slot->image.id,slot==&surface.normalTexture);if(!image.starts_with("builtin:"))(void)decodeManagedImage(checkedManagedAssetPath(root,image));
        }
        for(auto& primitive:model->primitives){primitive.sourceVertexCount=primitive.vertices.size();primitive.sourceIndexCount=primitive.indices.size();primitive.vertices=decltype(primitive.vertices){};primitive.indices=decltype(primitive.indices){};}
        result->bytes+=sizeof(ModelAsset)+model->nodes.size()*sizeof(ModelNode)+model->draws.size()*sizeof(ModelDraw)+model->materials.size()*sizeof(ModelMaterial)+model->primitives.size()*sizeof(ModelPrimitive);
        result->models.push_back(std::move(model));if(progress)progress->completed.fetch_add(1);
    }
    for(const auto& entry:particles){
        canceled(stop);const auto definition=loadParticleAsset(root,entry.id);canceled(stop);
        const auto material=loadMaterialAsset(root,policy.resolveParticleMaterial(definition.emitter.material.id));
        const auto image=policy.resolveParticleTexture(material.surface.baseColorTexture.image.id);
        if(!image.empty()&&!image.starts_with("builtin:"))(void)decodeManagedImage(checkedManagedAssetPath(root,image));
        result->bytes+=sizeof(ParticleEffectAsset)+definition.id.size()+definition.name.size();
        if(progress)progress->completed.fetch_add(1);
    }
    canceled(stop);result->fileStateFingerprint=fingerprintFileStates(result->dependencies);
    if(before!=result->fileStateFingerprint)throw std::runtime_error("Asset dependencies changed during deletion validation.");
    return result;
}
bool deletionValidationCurrent(const DeletionValidation& validation){try{return fingerprintFileStates(validation.dependencies)==validation.fileStateFingerprint;}catch(...){return false;}}
}
