////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/texture_asset.hpp"
#include "core/color_space.hpp"
#include "scene/components.hpp"

#include <nlohmann/json.hpp>

#include <string_view>

namespace quader {

/**
 * Selects compatibility defaults for legacy payloads or strict enum validation
 * for current project documents.
 */
enum class QdrProjectEnumParseMode {
  LegacyDefault,
  StrictCurrentSchema,
};

[[nodiscard]] std::string_view qdr_entity_kind_name(EntityKind kind);
[[nodiscard]] EntityKind qdr_entity_kind_from_name(std::string_view value);
[[nodiscard]] EntityKind
qdr_entity_kind_from_name(std::string_view value,
                          QdrProjectEnumParseMode mode);

[[nodiscard]] std::string_view qdr_light_type_name(LightType type);
[[nodiscard]] LightType qdr_light_type_from_name(std::string_view value);
[[nodiscard]] LightType
qdr_light_type_from_name(std::string_view value,
                         QdrProjectEnumParseMode mode);

[[nodiscard]] std::string_view
qdr_scene_tone_mapping_name(SceneToneMapping tone_mapping);
[[nodiscard]] SceneToneMapping
qdr_scene_tone_mapping_from_name(std::string_view value);
[[nodiscard]] SceneToneMapping
qdr_scene_tone_mapping_from_name(std::string_view value,
                                 QdrProjectEnumParseMode mode);

[[nodiscard]] std::string_view
qdr_mesh_collision_type_name(MeshCollisionType type);
[[nodiscard]] MeshCollisionType
qdr_mesh_collision_type_from_name(std::string_view value);
[[nodiscard]] MeshCollisionType
qdr_mesh_collision_type_from_name(std::string_view value,
                                  QdrProjectEnumParseMode mode);

[[nodiscard]] std::string_view
qdr_mesh_physics_body_type_name(MeshPhysicsBodyType type);
[[nodiscard]] MeshPhysicsBodyType
qdr_mesh_physics_body_type_from_name(std::string_view value);
[[nodiscard]] MeshPhysicsBodyType
qdr_mesh_physics_body_type_from_name(std::string_view value,
                                     QdrProjectEnumParseMode mode);

[[nodiscard]] std::string_view qdr_texture_usage_name(TextureUsage usage);
[[nodiscard]] TextureUsage qdr_texture_usage_from_name(std::string_view value);
[[nodiscard]] TextureUsage
qdr_texture_usage_from_name(std::string_view value,
                            QdrProjectEnumParseMode mode);

[[nodiscard]] std::string_view
qdr_texture_color_space_name(TextureColorSpace color_space);
[[nodiscard]] TextureColorSpace
qdr_texture_color_space_from_name(std::string_view value);
[[nodiscard]] TextureColorSpace
qdr_texture_color_space_from_name(std::string_view value,
                                  QdrProjectEnumParseMode mode);

[[nodiscard]] nlohmann::ordered_json qdr_vec2_to_json(QVec2 value);
[[nodiscard]] nlohmann::ordered_json qdr_vec3_to_json(QVec3 value);
[[nodiscard]] QVec2 qdr_vec2_from_json(const nlohmann::ordered_json &value);
[[nodiscard]] QVec3 qdr_vec3_from_json(const nlohmann::ordered_json &value,
                                       QVec3 fallback = {});

[[nodiscard]] nlohmann::ordered_json
qdr_transform_component_to_json(const TransformComponent &transform);
[[nodiscard]] TransformComponent
qdr_transform_component_from_json(const nlohmann::ordered_json &value);
[[nodiscard]] nlohmann::ordered_json
qdr_mesh_component_to_json(const MeshComponent &mesh);
[[nodiscard]] MeshComponent
qdr_mesh_component_from_json(const nlohmann::ordered_json &value);
[[nodiscard]] MeshComponent
qdr_mesh_component_from_json(const nlohmann::ordered_json &value,
                             QdrProjectEnumParseMode mode);
[[nodiscard]] nlohmann::ordered_json
qdr_material_component_to_json(const MaterialComponent &material);
[[nodiscard]] MaterialComponent
qdr_material_component_from_json(const nlohmann::ordered_json &value);
[[nodiscard]] nlohmann::ordered_json
qdr_light_component_to_json(const LightComponent &light);
[[nodiscard]] LightComponent
qdr_light_component_from_json(const nlohmann::ordered_json &value);
[[nodiscard]] LightComponent
qdr_light_component_from_json(const nlohmann::ordered_json &value,
                              QdrProjectEnumParseMode mode);
[[nodiscard]] nlohmann::ordered_json
qdr_scene_settings_component_to_json(const SceneSettingsComponent &settings);
[[nodiscard]] SceneSettingsComponent
qdr_scene_settings_component_from_json(const nlohmann::ordered_json &value);
[[nodiscard]] SceneSettingsComponent
qdr_scene_settings_component_from_json(const nlohmann::ordered_json &value,
                                       QdrProjectEnumParseMode mode);

} // namespace quader
