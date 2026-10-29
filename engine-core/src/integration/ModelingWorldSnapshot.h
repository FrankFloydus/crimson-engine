#pragma once
#include "crimson-renderer/RenderFrame.h"
#include "scene/Scene.h"
#include <memory>
#include <vector>
#include <functional>
#include <stop_token>

namespace crimson::integration {
struct NativeSceneRenderBindings;
// Derived immutable publication only. Source geometry and material versions are
// retained by render->pinned; collision payloads have no asset/file identity.
struct NativeModelingInstance {std::size_t index{};std::string entity_uuid;Mat4 entity_from_local;std::vector<std::uint32_t> source_slots;std::uint64_t source_instance_id{};std::function<std::shared_ptr<const ModelPrimitive>(std::uint32_t,std::stop_token)> original_lod;std::string member_uuid;std::uint32_t index_offset{},index_count{};};
struct NativeModelingCollision {std::size_t index{};std::string entity_uuid;Mat4 entity_from_local;};
struct ModelingWorldSnapshot final {
    std::uint64_t source_session{},generation{};
    std::shared_ptr<const RenderWorldSnapshot> render;
    std::vector<EntityRecord> collision_records;
    std::vector<NativeModelingInstance> native_instances;
    std::vector<NativeModelingCollision> native_collisions;
};
std::shared_ptr<const PreparedRenderFrame> append_modeling_world(
    std::shared_ptr<const PreparedRenderFrame>,std::shared_ptr<const ModelingWorldSnapshot>,bool include_lights=true,std::shared_ptr<const NativeSceneRenderBindings> bindings={},bool game_only=false);
}
