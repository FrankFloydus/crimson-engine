#pragma once
#include "scene/Scene.h"
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace crimson { class AssetLibrary; }
namespace crimson::integration {
struct NativeSlotMaterial {
    std::uint32_t source_slot{};
    std::string requested_id;
    std::shared_ptr<const MaterialAsset> asset;
    ModelMaterial surface;
    ModelMaterialTextures textures;
    std::uint64_t signature{};
    bool ready{},current{};
};
struct NativeEntityMaterialBindings {
    ModelMaterialOverrides assignments;
    SceneBlendSettings appearance;
    std::vector<NativeSlotMaterial> slot_materials;
    std::uint64_t source_revision{},shader_revision{},signature{};
    bool pending{};
    std::string diagnostic;
};
struct NativeEntityRenderBinding {
    EntityId entity{};
    Mat4 authored_world{},presented_world{};
    bool native_geometry{};
    std::shared_ptr<const NativeEntityMaterialBindings> materials;
    bool cast_shadows{true},receive_shadows{true},receive_decals{true};
    MeshShading mesh_shading{MeshShading::UseMeshNormals};
    SurfaceLightingScopes lighting_scopes{};
};
// Identity/pose publication over the one native scene. Geometry and material
// owners remain immutable renderer resources; this never creates asset files.
struct NativeSceneRenderBindings {
    std::uint64_t revision{};
    bool scene_lighting{}; // Transient Default workspace presentation, not authored data.
    std::unordered_map<std::string,NativeEntityRenderBinding> entities;
    std::uint64_t material_base_revision{},material_revision{},material_source_revision{},material_shader_revision{};
    std::vector<std::string> material_entities,pending_material_entities;
};

struct NativeMaterialApplication {
    std::vector<ModelMaterial> materials;
    std::vector<ModelMaterialTextures> textures;
    std::shared_ptr<const NativeEntityMaterialBindings> pinned;
};
void decorate_native_materials(NativeSceneRenderBindings&,const Scene&,::crimson::AssetLibrary&,
    const NativeSceneRenderBindings* previous=nullptr);
std::shared_ptr<const NativeSceneRenderBindings> refresh_native_materials(
    std::shared_ptr<const NativeSceneRenderBindings>,const Scene&,::crimson::AssetLibrary&);
std::shared_ptr<const NativeMaterialApplication> apply_native_materials(const NativeEntityRenderBinding&,
    std::span<const std::uint32_t> source_slots,std::span<const ModelMaterial> base_materials,
    std::span<const ModelMaterialTextures> base_textures);
void apply_native_render_flags(RenderModelInstance&,const NativeEntityRenderBinding&);
}
