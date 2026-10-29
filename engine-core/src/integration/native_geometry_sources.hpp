#pragma once
#include "ModelingWorldSnapshot.h"
#include "native_scene_render_bindings.hpp"
#include "editor/ExternalMeshSources.h"

namespace crimson::integration {
std::shared_ptr<const ExternalMeshSnapshot> native_geometry_sources(
    std::shared_ptr<const ModelingWorldSnapshot>,std::shared_ptr<const NativeSceneRenderBindings>,
    const Scene&,std::shared_ptr<const ExternalMeshSnapshot> previous={});
std::optional<std::uint32_t> native_material_source_slot(const ModelingWorldSnapshot&,
    std::string_view entity_uuid,std::uint64_t source_instance,std::uint32_t rendered_slot);
bool native_material_can_apply(const MaterialAsset&,const ModelAsset&,std::uint32_t rendered_slot,
    AssetLibrary&,std::string& diagnostic);
}
