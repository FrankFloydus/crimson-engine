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

[[nodiscard]] std::string_view qdrEntityKindName(EntityKind kind);
[[nodiscard]] EntityKind qdrEntityKindFromName(std::string_view value);

[[nodiscard]] std::string_view qdrLightTypeName(LightType type);
[[nodiscard]] LightType qdrLightTypeFromName(std::string_view value);

[[nodiscard]] std::string_view qdrSceneToneMappingName(SceneToneMapping toneMapping);
[[nodiscard]] SceneToneMapping qdrSceneToneMappingFromName(std::string_view value);

[[nodiscard]] std::string_view qdrMeshCollisionTypeName(MeshCollisionType type);
[[nodiscard]] MeshCollisionType qdrMeshCollisionTypeFromName(std::string_view value);

[[nodiscard]] std::string_view qdrMeshPhysicsBodyTypeName(MeshPhysicsBodyType type);
[[nodiscard]] MeshPhysicsBodyType qdrMeshPhysicsBodyTypeFromName(std::string_view value);

[[nodiscard]] std::string_view qdrTextureUsageName(TextureUsage usage);
[[nodiscard]] TextureUsage qdrTextureUsageFromName(std::string_view value);

[[nodiscard]] std::string_view qdrTextureColorSpaceName(TextureColorSpace colorSpace);
[[nodiscard]] TextureColorSpace qdrTextureColorSpaceFromName(std::string_view value);

[[nodiscard]] nlohmann::ordered_json qdrVec2ToJson(QVec2 value);
[[nodiscard]] nlohmann::ordered_json qdrVec3ToJson(QVec3 value);
[[nodiscard]] QVec2 qdrVec2FromJson(const nlohmann::ordered_json& value);
[[nodiscard]] QVec3 qdrVec3FromJson(const nlohmann::ordered_json& value, QVec3 fallback = {});

[[nodiscard]] nlohmann::ordered_json qdrTransformComponentToJson(const TransformComponent& transform);
[[nodiscard]] TransformComponent qdrTransformComponentFromJson(const nlohmann::ordered_json& value);
[[nodiscard]] nlohmann::ordered_json qdrMeshComponentToJson(const MeshComponent& mesh);
[[nodiscard]] MeshComponent qdrMeshComponentFromJson(const nlohmann::ordered_json& value);
[[nodiscard]] nlohmann::ordered_json qdrMaterialComponentToJson(const MaterialComponent& material);
[[nodiscard]] MaterialComponent qdrMaterialComponentFromJson(const nlohmann::ordered_json& value);
[[nodiscard]] nlohmann::ordered_json qdrLightComponentToJson(const LightComponent& light);
[[nodiscard]] LightComponent qdrLightComponentFromJson(const nlohmann::ordered_json& value);
[[nodiscard]] nlohmann::ordered_json qdrSceneSettingsComponentToJson(const SceneSettingsComponent& settings);
[[nodiscard]] SceneSettingsComponent qdrSceneSettingsComponentFromJson(const nlohmann::ordered_json& value);

} // namespace quader
