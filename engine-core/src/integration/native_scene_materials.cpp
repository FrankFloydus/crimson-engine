#include "native_scene_render_bindings.hpp"
#include "assets/AssetLibrary.h"
#include "assets/MaterialGraphReferences.h"

#include <algorithm>
#include <stdexcept>

namespace crimson::integration {
namespace {
std::uint64_t mix(std::uint64_t value,std::uint64_t part){return (value^part)*1099511628211ull;}
std::uint64_t texture_signature(const ModelMaterialTextures& textures) {
    std::uint64_t result=textures.graph?textures.graph->signature:14695981039346656037ull;
    for(std::size_t slot=0;slot<textures.handles.size();++slot){result=mix(result,textures.handles[slot].idx);result=mix(result,textures.flags[slot]);result=mix(result,textures.signatures[slot]);result=mix(result,textures.sourceFingerprints[slot]);}
    return result;
}
std::shared_ptr<const NativeEntityMaterialBindings> prepare_materials(const EntityRecord& record,AssetLibrary& assets,
    std::shared_ptr<const NativeEntityMaterialBindings> previous) {
    if(record.modelMaterials.empty())return {};
    const auto source_revision=assets.referenceReportRevision(),shader_revision=assets.materialShaderRevision();
    const bool same_request=previous&&previous->assignments==record.modelMaterials&&previous->appearance==record.blendSettings&&
        previous->source_revision==source_revision&&previous->shader_revision==shader_revision;
    if(same_request&&!previous->pending)return previous;
    auto next=std::make_shared<NativeEntityMaterialBindings>();next->assignments=record.modelMaterials;next->appearance=record.blendSettings;
    next->source_revision=source_revision;next->shader_revision=shader_revision;
    next->signature=mix(14695981039346656037ull,source_revision);
    for(std::uint32_t slot=0;slot<record.modelMaterials.size();++slot) {
        const auto& assignment=record.modelMaterials[slot];if(assignment.empty())continue;
        NativeSlotMaterial packet;packet.source_slot=slot;packet.requested_id=assignment.id;
        const auto old=previous?std::ranges::find(previous->slot_materials,slot,&NativeSlotMaterial::source_slot):std::vector<NativeSlotMaterial>::const_iterator{};
        try {
            auto material=assets.findMaterial(assignment.id);
            if(!material)throw std::runtime_error("The assigned native mesh material is unavailable.");
            if(!modelMaterialCompatible(*material))throw std::runtime_error("The assigned material cannot render on mesh geometry.");
            auto surface=assets.nativeMaterialSurface(*material,findSceneBlendMaterialSettings(record.blendSettings,slot,material->id));
            if(surface.graph&&(materialGraphRequiredUvs(*assets.resolvedMaterialGraph(surface.graph))&~std::uint8_t{1u}))
                throw std::runtime_error("The assigned material requires an unavailable native mesh UV set.");
            auto textures=assets.nativeMaterialBindings(surface);
            const bool usable=!textures.graphPending&&textures.sourceReady&&textures.drawable();
            const bool pending=textures.graphPending||!textures.sourceReady||!textures.complete;
            next->pending|=pending;
            if(!usable) {
                if(next->diagnostic.empty())next->diagnostic=textures.diagnostic;
            } else {
                packet.asset=std::move(material);packet.surface=std::move(surface);packet.textures=std::move(textures);
                packet.ready=true;packet.current=!pending;packet.signature=mix(assets.materialRevision(assignment.id),texture_signature(packet.textures));
            }
        }catch(const std::exception& error){if(next->diagnostic.empty())next->diagnostic=error.what();}
        if(!packet.ready&&previous&&old!=previous->slot_materials.end()&&old->ready) {
            packet=*old;packet.requested_id=assignment.id;packet.current=false;
        }
        next->signature=mix(mix(next->signature,slot),packet.signature);
        next->slot_materials.push_back(std::move(packet));
    }
    if(same_request&&previous->signature==next->signature&&previous->pending==next->pending&&previous->diagnostic==next->diagnostic)return previous;
    return next;
}
void stamps(NativeSceneRenderBindings& bindings,AssetLibrary& assets) {
    bindings.material_source_revision=assets.referenceReportRevision();bindings.material_shader_revision=assets.materialShaderRevision();
    bindings.material_revision=14695981039346656037ull;bindings.pending_material_entities.clear();
    for(const auto& uuid:bindings.material_entities) {
        const auto found=bindings.entities.find(uuid);if(found==bindings.entities.end()||!found->second.materials)continue;
        bindings.material_revision=mix(bindings.material_revision,found->second.materials->signature);
        if(found->second.materials->pending)bindings.pending_material_entities.push_back(uuid);
    }
    bindings.revision=mix(bindings.material_base_revision,bindings.material_revision);
}
}
void decorate_native_materials(NativeSceneRenderBindings& bindings,const Scene& scene,AssetLibrary& assets,const NativeSceneRenderBindings* previous) {
    bindings.material_base_revision=bindings.revision;bindings.material_entities.clear();
    for(auto& [uuid,binding]:bindings.entities) {
        const auto record=scene.find(binding.entity);if(!record)continue;
        binding.cast_shadows=record->castShadows;binding.receive_shadows=record->receiveShadows;binding.receive_decals=record->receiveDecals;
        binding.mesh_shading=record->meshShading;binding.lighting_scopes=record->lightingScopes;
        if(record->modelMaterials.empty())continue;
        std::shared_ptr<const NativeEntityMaterialBindings> old;
        if(previous)if(const auto found=previous->entities.find(uuid);found!=previous->entities.end())old=found->second.materials;
        binding.materials=prepare_materials(*record,assets,std::move(old));
        if(binding.materials)bindings.material_entities.push_back(uuid);
    }
    std::ranges::sort(bindings.material_entities);stamps(bindings,assets);
}
std::shared_ptr<const NativeSceneRenderBindings> refresh_native_materials(std::shared_ptr<const NativeSceneRenderBindings> previous,const Scene& scene,AssetLibrary& assets) {
    if(!previous)return {};
    const bool changed=previous->material_source_revision!=assets.referenceReportRevision()||previous->material_shader_revision!=assets.materialShaderRevision();
    if(!changed&&previous->pending_material_entities.empty())return previous;
    std::vector<std::pair<std::string,std::shared_ptr<const NativeEntityMaterialBindings>>> updates;
    for(const auto& uuid:changed?previous->material_entities:previous->pending_material_entities) {
        const auto found=previous->entities.find(uuid);if(found==previous->entities.end())continue;
        const auto record=scene.find(found->second.entity);if(!record)continue;
        auto material=prepare_materials(*record,assets,found->second.materials);
        if(material!=found->second.materials)updates.emplace_back(uuid,std::move(material));
    }
    if(updates.empty()&&!changed)return previous;
    auto next=std::make_shared<NativeSceneRenderBindings>(*previous);
    for(auto& [uuid,materials]:updates)next->entities.at(uuid).materials=std::move(materials);
    stamps(*next,assets);return next;
}
void apply_native_render_flags(RenderModelInstance& instance,const NativeEntityRenderBinding& binding) {
    instance.castShadows&=binding.cast_shadows;instance.receiveShadows=binding.receive_shadows;
    instance.receiveDecals=binding.receive_decals;instance.meshShading=binding.mesh_shading;instance.lightingScopes=binding.lighting_scopes;
}
std::shared_ptr<const NativeMaterialApplication> apply_native_materials(const NativeEntityRenderBinding& entity,
    std::span<const std::uint32_t> source_slots,std::span<const ModelMaterial> base,std::span<const ModelMaterialTextures> textures) {
    if(!entity.materials||source_slots.size()!=base.size()||textures.size()!=base.size())return {};
    std::shared_ptr<NativeMaterialApplication> result;
    for(std::size_t slot=0;slot<source_slots.size();++slot) {
        const auto found=std::ranges::find(entity.materials->slot_materials,source_slots[slot],&NativeSlotMaterial::source_slot);
        if(found==entity.materials->slot_materials.end()||!found->ready)continue;
        if(!result){result=std::make_shared<NativeMaterialApplication>();result->materials.assign(base.begin(),base.end());result->textures.assign(textures.begin(),textures.end());result->pinned=entity.materials;}
        result->materials[slot]=found->surface;result->materials[slot].doubleSided=base[slot].doubleSided;result->materials[slot].receiveFog=base[slot].receiveFog;
        result->textures[slot]=found->textures;
    }
    return result;
}
}
