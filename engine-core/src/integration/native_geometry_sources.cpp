#include "native_geometry_sources.hpp"
#include "assets/AssetLibrary.h"
#include "assets/MaterialGraphReferences.h"
#include <crimson/renderer/forward/StreamingModel.h>
#include <stdexcept>

namespace crimson::integration {
std::optional<std::uint32_t> native_material_source_slot(const ModelingWorldSnapshot& world,
    std::string_view uuid,std::uint64_t source,std::uint32_t slot) {
    for(const auto& instance:world.native_instances)if(instance.entity_uuid==uuid&&instance.source_instance_id==source)
        return slot<instance.source_slots.size()?std::optional{instance.source_slots[slot]}:std::nullopt;
    return {};
}
bool native_material_can_apply(const MaterialAsset& material,const ModelAsset& model,std::uint32_t slot,
    AssetLibrary& assets,std::string& diagnostic) {
    try {
        if(!modelMaterialCompatible(material))throw std::runtime_error("The material cannot render on mesh geometry.");
        const auto surface=assets.nativeMaterialSurface(material);
        const auto required=surface.graph?materialGraphRequiredUvs(*assets.resolvedMaterialGraph(surface.graph)):std::uint8_t{};
        bool found=false;
        for(const auto& primitive:model.primitives)if(primitive.material==slot) {
            found=true;if((primitive.uvMask&required)!=required)throw std::runtime_error("The material requires an unavailable native mesh UV set.");
            if(!surface.graph&&!surface.triplanar)for(const auto* texture:{&surface.baseColorTexture,&surface.metallicRoughnessTexture,&surface.normalTexture,&surface.occlusionTexture,&surface.emissiveTexture})
                if(!texture->image.empty()&&(texture->texCoord>1u||!(primitive.uvMask&(1u<<texture->texCoord))))throw std::runtime_error("The material requires an unavailable native mesh UV set.");
        }
        if(!found)throw std::runtime_error("The native mesh material slot changed.");
        return true;
    }catch(const std::exception& error){diagnostic=error.what();return false;}
}
std::shared_ptr<const ExternalMeshSnapshot> native_geometry_sources(
    std::shared_ptr<const ModelingWorldSnapshot> modeling,std::shared_ptr<const NativeSceneRenderBindings> bindings,
    const Scene& scene,std::shared_ptr<const ExternalMeshSnapshot> previous) {
    if(!modeling||!modeling->render||!bindings)return {};
    const auto revision=modeling->generation^(bindings->revision*1099511628211ull);
    if(previous&&previous->revision==revision&&previous->sourceOwner==modeling&&previous->bindingsOwner==bindings)return previous;
    auto result=std::make_shared<ExternalMeshSnapshot>();result->revision=revision;result->sourceOwner=modeling;result->bindingsOwner=bindings;
    for(const auto& native:modeling->native_instances) {
        if(native.index>=modeling->render->shadowInstances.size())continue;
        const auto binding=bindings->entities.find(native.entity_uuid);if(binding==bindings->entities.end()||!binding->second.native_geometry)continue;
        const auto record=scene.find(binding->second.entity);if(!record||!record->nativeGeometry)continue;
        const auto& rendered=modeling->render->shadowInstances[native.index];if(!rendered.model.asset||!native.original_lod)continue;
        ExternalMeshSource source;source.model=rendered.model.asset;source.originalLod=native.original_lod;source.entityFromLocal=native.entity_from_local;
        source.revision=rendered.resourceRevision;source.fingerprint=source.model->stream?source.model->stream->key:rendered.resourceRevision;
        source.occurrence.id=binding->second.entity;source.occurrence.world=binding->second.presented_world;
        source.occurrence.sourceInstanceId=native.source_instance_id;source.occurrence.visible=scene.isVisible(record->id);
        source.nativeSource={native.member_uuid,native.index_offset,native.index_count};
        source.occurrence.receiveDecals=record->receiveDecals;source.occurrence.meshShading=record->meshShading;
        source.occurrence.meshPaint=record->meshPaint;source.occurrence.blendSettings=record->blendSettings;
        if(rendered.materials&&rendered.textures)if(auto applied=apply_native_materials(binding->second,native.source_slots,*rendered.materials,*rendered.textures))
            source.materials=std::shared_ptr<const std::vector<ModelMaterial>>(applied,&applied->materials);
        if(!source.materials)source.materials=rendered.materials
            ?std::shared_ptr<const std::vector<ModelMaterial>>(modeling,rendered.materials)
            :std::shared_ptr<const std::vector<ModelMaterial>>(source.model,&source.model->materials);
        result->byOccurrence.emplace(std::pair{source.occurrence.id,source.occurrence.sourceInstanceId},result->meshes.size());
        result->meshes.push_back(std::move(source));
    }
    return result;
}
}
