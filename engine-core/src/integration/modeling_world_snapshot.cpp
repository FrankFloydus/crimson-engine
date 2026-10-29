#include "ModelingWorldSnapshot.h"
#include "native_scene_render_bindings.hpp"

namespace crimson::integration {
std::shared_ptr<const PreparedRenderFrame> append_modeling_world(
    std::shared_ptr<const PreparedRenderFrame> frame,std::shared_ptr<const ModelingWorldSnapshot> modeling,bool include_lights,std::shared_ptr<const NativeSceneRenderBindings> bindings,bool game_only){
    if(!frame||!frame->world||!modeling||!modeling->render)return frame;
    struct Pins {std::shared_ptr<const PreparedRenderFrame> engine;std::shared_ptr<const ModelingWorldSnapshot> modeling;std::shared_ptr<const NativeSceneRenderBindings> bindings;std::vector<std::shared_ptr<const NativeMaterialApplication>> materials;};
    auto pins=std::make_shared<Pins>();pins->engine=frame;pins->modeling=modeling;pins->bindings=bindings;
    auto combined=std::make_shared<PreparedRenderFrame>(*frame);auto world=std::make_shared<RenderWorldSnapshot>(*frame->world);const auto& external=*modeling->render;auto instances=external.shadowInstances;
    if(bindings)for(const auto& native:modeling->native_instances)if(native.index<instances.size())if(const auto found=bindings->entities.find(native.entity_uuid);found!=bindings->entities.end()){
        auto& instance=instances[native.index];instance.entityId=found->second.entity;instance.model.transform=found->second.presented_world*native.entity_from_local;instance.pickable=true;
        apply_native_render_flags(instance,found->second);
        if(instance.materials&&instance.textures)if(auto materials=apply_native_materials(found->second,native.source_slots,*instance.materials,*instance.textures)){
            instance.materials=&materials->materials;instance.textures=&materials->textures;pins->materials.push_back(std::move(materials));
        }
    }
    world->shadowInstances.insert(world->shadowInstances.end(),instances.begin(),instances.end());
    if(include_lights)world->directional.insert(world->directional.end(),external.directional.begin(),external.directional.end());
    if(include_lights)world->points.insert(world->points.end(),external.points.begin(),external.points.end());
    world->pinned=pins;world->shadowWorldId^=external.shadowWorldId;combined->world=std::move(world);
    for(auto& view:combined->views)if(!game_only||view.identity.purpose==RenderViewPurpose::Game){view.instances.insert(view.instances.end(),instances.begin(),instances.end());view.pinned=pins;view.meshPopulationRevision^=modeling->generation^(bindings?bindings->material_revision:0);view.giWorldId^=external.shadowWorldId;}
    combined->resourceSignature^=modeling->generation^(bindings?bindings->material_revision:0);return combined;
}
}
