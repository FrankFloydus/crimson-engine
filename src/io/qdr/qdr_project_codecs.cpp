////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_project_codecs.hpp"

#include "io/qdr/qdr_diagnostic.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

std::string qdr_string(std::string_view value) {
  return std::string(value.data(), value.size());
}

[[noreturn]] void throw_unsupported_enum(std::string_view label,
                                         std::string_view value) {
  throw quader_io::QdrDiagnosticException(quader_io::qdr_make_error(
      "qdr.unsupported_enum", "",
      "QDR project document contains unsupported " + qdr_string(label) +
          " enum value: " + qdr_string(value) + "."));
}

std::string enum_field_string(const Json &value, const char *key,
                              std::string_view label,
                              QdrProjectEnumParseMode mode,
                              std::string_view legacy_fallback) {
  const auto field = value.find(key);
  if (field == value.end()) {
    if (mode == QdrProjectEnumParseMode::LegacyDefault) {
      return qdr_string(legacy_fallback);
    }
    throw quader_io::QdrDiagnosticException(quader_io::qdr_make_error(
        "qdr.missing_enum", "",
        "QDR project document is missing " + qdr_string(label) +
            " enum field."));
  }
  if (!field->is_string()) {
    throw quader_io::QdrDiagnosticException(quader_io::qdr_make_error(
        "qdr.invalid_enum", "",
        "QDR project document " + qdr_string(label) +
            " enum field must be a string."));
  }
  return field->get<std::string>();
}

} // namespace

std::string_view qdr_entity_kind_name(EntityKind kind) {
  switch (kind) {
  case EntityKind::Empty:
    return "empty";
  case EntityKind::Mesh:
    return "mesh";
  case EntityKind::Light:
    return "light";
  case EntityKind::Generic:
    return "generic";
  }
  return "generic";
}

EntityKind qdr_entity_kind_from_name(std::string_view value) {
  return qdr_entity_kind_from_name(value,
                                   QdrProjectEnumParseMode::LegacyDefault);
}

EntityKind qdr_entity_kind_from_name(std::string_view value,
                                     QdrProjectEnumParseMode mode) {
  if (value == "empty") {
    return EntityKind::Empty;
  }
  if (value == "mesh") {
    return EntityKind::Mesh;
  }
  if (value == "light") {
    return EntityKind::Light;
  }
  if (value == "generic") {
    return EntityKind::Generic;
  }
  if (mode == QdrProjectEnumParseMode::StrictCurrentSchema) {
    throw_unsupported_enum("entity kind", value);
  }
  return EntityKind::Generic;
}

std::string_view qdr_light_type_name(LightType type) {
  switch (type) {
  case LightType::Directional:
    return "directional";
  case LightType::Point:
    return "point";
  case LightType::Spot:
    return "spot";
  }
  return "directional";
}

LightType qdr_light_type_from_name(std::string_view value) {
  return qdr_light_type_from_name(value,
                                  QdrProjectEnumParseMode::LegacyDefault);
}

LightType qdr_light_type_from_name(std::string_view value,
                                   QdrProjectEnumParseMode mode) {
  if (value == "directional") {
    return LightType::Directional;
  }
  if (value == "point") {
    return LightType::Point;
  }
  if (value == "spot") {
    return LightType::Spot;
  }
  if (mode == QdrProjectEnumParseMode::StrictCurrentSchema) {
    throw_unsupported_enum("light type", value);
  }
  return LightType::Directional;
}

std::string_view qdr_scene_tone_mapping_name(SceneToneMapping tone_mapping) {
  switch (tone_mapping) {
  case SceneToneMapping::Linear:
    return "linear";
  case SceneToneMapping::AcesLegacy:
    return "acesLegacy";
  case SceneToneMapping::Aces:
    return "aces";
  case SceneToneMapping::Filmic:
    return "filmic";
  case SceneToneMapping::DisplayRange:
    return "displayRange";
  }
  return "aces";
}

SceneToneMapping qdr_scene_tone_mapping_from_name(std::string_view value) {
  return qdr_scene_tone_mapping_from_name(
      value, QdrProjectEnumParseMode::LegacyDefault);
}

SceneToneMapping
qdr_scene_tone_mapping_from_name(std::string_view value,
                                 QdrProjectEnumParseMode mode) {
  if (value == "linear") {
    return SceneToneMapping::Linear;
  }
  if (value == "acesLegacy") {
    return SceneToneMapping::AcesLegacy;
  }
  if (value == "aces") {
    return SceneToneMapping::Aces;
  }
  if (value == "filmic") {
    return SceneToneMapping::Filmic;
  }
  if (value == "displayRange") {
    return SceneToneMapping::DisplayRange;
  }
  if (mode == QdrProjectEnumParseMode::StrictCurrentSchema) {
    throw_unsupported_enum("scene tone mapping", value);
  }
  return SceneToneMapping::Aces;
}

std::string_view qdr_mesh_collision_type_name(MeshCollisionType type) {
  switch (type) {
  case MeshCollisionType::Box:
    return "box";
  case MeshCollisionType::Sphere:
    return "sphere";
  case MeshCollisionType::Cylinder:
    return "cylinder";
  case MeshCollisionType::ConvexHull:
    return "convexHull";
  case MeshCollisionType::Mesh:
    return "mesh";
  }
  return "box";
}

MeshCollisionType qdr_mesh_collision_type_from_name(std::string_view value) {
  return qdr_mesh_collision_type_from_name(
      value, QdrProjectEnumParseMode::LegacyDefault);
}

MeshCollisionType
qdr_mesh_collision_type_from_name(std::string_view value,
                                  QdrProjectEnumParseMode mode) {
  if (value == "box") {
    return MeshCollisionType::Box;
  }
  if (value == "sphere") {
    return MeshCollisionType::Sphere;
  }
  if (value == "cylinder") {
    return MeshCollisionType::Cylinder;
  }
  if (value == "convexHull") {
    return MeshCollisionType::ConvexHull;
  }
  if (value == "mesh") {
    return MeshCollisionType::Mesh;
  }
  if (mode == QdrProjectEnumParseMode::StrictCurrentSchema) {
    throw_unsupported_enum("mesh collision type", value);
  }
  return MeshCollisionType::Box;
}

std::string_view qdr_mesh_physics_body_type_name(MeshPhysicsBodyType type) {
  switch (type) {
  case MeshPhysicsBodyType::StaticBody:
    return "staticBody";
  case MeshPhysicsBodyType::RigidBody:
    return "rigidBody";
  }
  return "staticBody";
}

MeshPhysicsBodyType
qdr_mesh_physics_body_type_from_name(std::string_view value) {
  return qdr_mesh_physics_body_type_from_name(
      value, QdrProjectEnumParseMode::LegacyDefault);
}

MeshPhysicsBodyType
qdr_mesh_physics_body_type_from_name(std::string_view value,
                                     QdrProjectEnumParseMode mode) {
  if (value == "staticBody") {
    return MeshPhysicsBodyType::StaticBody;
  }
  if (value == "rigidBody") {
    return MeshPhysicsBodyType::RigidBody;
  }
  if (mode == QdrProjectEnumParseMode::StrictCurrentSchema) {
    throw_unsupported_enum("mesh physics body type", value);
  }
  return MeshPhysicsBodyType::StaticBody;
}

std::string_view qdr_texture_usage_name(TextureUsage usage) {
  switch (usage) {
  case TextureUsage::BaseColor:
    return "baseColor";
  case TextureUsage::Normal:
    return "normal";
  case TextureUsage::Roughness:
    return "roughness";
  case TextureUsage::Metallic:
    return "metallic";
  case TextureUsage::Curvature:
    return "curvature";
  case TextureUsage::Thickness:
    return "thickness";
  case TextureUsage::AmbientOcclusion:
    return "ambientOcclusion";
  case TextureUsage::Position:
    return "position";
  case TextureUsage::Id:
    return "id";
  case TextureUsage::Mask:
    return "mask";
  case TextureUsage::Unknown:
    return "unknown";
  }
  return "unknown";
}

TextureUsage qdr_texture_usage_from_name(std::string_view value) {
  return qdr_texture_usage_from_name(value,
                                     QdrProjectEnumParseMode::LegacyDefault);
}

TextureUsage qdr_texture_usage_from_name(std::string_view value,
                                         QdrProjectEnumParseMode mode) {
  if (value == "unknown") {
    return TextureUsage::Unknown;
  }
  if (value == "baseColor") {
    return TextureUsage::BaseColor;
  }
  if (value == "normal") {
    return TextureUsage::Normal;
  }
  if (value == "roughness") {
    return TextureUsage::Roughness;
  }
  if (value == "metallic") {
    return TextureUsage::Metallic;
  }
  if (value == "curvature") {
    return TextureUsage::Curvature;
  }
  if (value == "thickness") {
    return TextureUsage::Thickness;
  }
  if (value == "ambientOcclusion") {
    return TextureUsage::AmbientOcclusion;
  }
  if (value == "position") {
    return TextureUsage::Position;
  }
  if (value == "id") {
    return TextureUsage::Id;
  }
  if (value == "mask") {
    return TextureUsage::Mask;
  }
  if (mode == QdrProjectEnumParseMode::StrictCurrentSchema) {
    throw_unsupported_enum("texture usage", value);
  }
  return TextureUsage::Unknown;
}

std::string_view qdr_texture_color_space_name(TextureColorSpace color_space) {
  switch (color_space) {
  case TextureColorSpace::Linear:
    return "linear";
  case TextureColorSpace::Srgb:
    return "srgb";
  }
  return "srgb";
}

TextureColorSpace qdr_texture_color_space_from_name(std::string_view value) {
  return qdr_texture_color_space_from_name(
      value, QdrProjectEnumParseMode::LegacyDefault);
}

TextureColorSpace
qdr_texture_color_space_from_name(std::string_view value,
                                  QdrProjectEnumParseMode mode) {
  if (value == "linear") {
    return TextureColorSpace::Linear;
  }
  if (value == "srgb") {
    return TextureColorSpace::Srgb;
  }
  if (mode == QdrProjectEnumParseMode::StrictCurrentSchema) {
    throw_unsupported_enum("texture color space", value);
  }
  return TextureColorSpace::Srgb;
}

nlohmann::ordered_json qdr_vec2_to_json(QVec2 value) {
  return Json{{"x", value.x}, {"y", value.y}};
}

nlohmann::ordered_json qdr_vec3_to_json(QVec3 value) {
  return Json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

QVec2 qdr_vec2_from_json(const nlohmann::ordered_json &value) {
  return {
      value.value("x", 0.0f),
      value.value("y", 0.0f),
  };
}

QVec3 qdr_vec3_from_json(const nlohmann::ordered_json &value, QVec3 fallback) {
  if (!value.is_object()) {
    return fallback;
  }
  return {
      value.value("x", fallback.x),
      value.value("y", fallback.y),
      value.value("z", fallback.z),
  };
}

nlohmann::ordered_json
qdr_transform_component_to_json(const TransformComponent &transform) {
  Json value = Json{
      {"position", qdr_vec3_to_json(transform.position)},
      {"rotationEuler", qdr_vec3_to_json(transform.rotation_euler)},
      {"scale", qdr_vec3_to_json(transform.scale)},
  };
  if (transform.custom_pivot) {
    value["customPivot"] = true;
  }
  return value;
}

TransformComponent
qdr_transform_component_from_json(const nlohmann::ordered_json &value) {
  TransformComponent transform;
  if (!value.is_object()) {
    return transform;
  }
  transform.position =
      qdr_vec3_from_json(value.value("position", Json::object()));
  transform.rotation_euler =
      qdr_vec3_from_json(value.value("rotationEuler", Json::object()));
  transform.scale = qdr_vec3_from_json(value.value("scale", Json::object()),
                                       {1.0f, 1.0f, 1.0f});
  transform.custom_pivot = value.value("customPivot", false);
  return transform;
}

nlohmann::ordered_json qdr_mesh_component_to_json(const MeshComponent &mesh) {
  return Json{
      {"mesh", mesh.mesh},
      {"editorOnly", mesh.editor_only},
      {"castShadows", mesh.cast_shadows},
      {"doubleSided", mesh.double_sided},
      {"generateCollisions", mesh.generate_collisions},
      {"collisionType",
       qdr_string(qdr_mesh_collision_type_name(mesh.collision_type))},
      {"displayCollision", mesh.display_collision},
      {"bodyType", qdr_string(qdr_mesh_physics_body_type_name(mesh.body_type))},
  };
}

MeshComponent
qdr_mesh_component_from_json(const nlohmann::ordered_json &value) {
  return qdr_mesh_component_from_json(
      value, QdrProjectEnumParseMode::LegacyDefault);
}

MeshComponent
qdr_mesh_component_from_json(const nlohmann::ordered_json &value,
                             QdrProjectEnumParseMode mode) {
  MeshComponent mesh;
  mesh.mesh = value.value("mesh", kInvalidAssetId);
  mesh.editor_only = value.value("editorOnly", false);
  mesh.cast_shadows = value.value("castShadows", true);
  mesh.double_sided = value.value("doubleSided", false);
  mesh.generate_collisions = value.value("generateCollisions", true);
  mesh.collision_type = qdr_mesh_collision_type_from_name(
      enum_field_string(value, "collisionType", "mesh collision type", mode,
                        "box"),
      mode);
  mesh.display_collision = value.value("displayCollision", false);
  mesh.body_type = qdr_mesh_physics_body_type_from_name(
      enum_field_string(value, "bodyType", "mesh physics body type", mode,
                        "staticBody"),
      mode);
  return mesh;
}

nlohmann::ordered_json
qdr_material_component_to_json(const MaterialComponent &material) {
  Json material_json{{"material", material.material}};
  if (!material.material_slots.empty()) {
    std::vector<std::pair<std::uint32_t, AssetId>> sorted_slots;
    sorted_slots.reserve(material.material_slots.size());
    for (const auto &[slot, material_id] : material.material_slots) {
      sorted_slots.emplace_back(slot, material_id);
    }
    std::sort(sorted_slots.begin(), sorted_slots.end(),
              [](const auto &left, const auto &right) {
                return left.first < right.first;
              });

    Json slots = Json::object();
    for (const auto &[slot, material_id] : sorted_slots) {
      slots[std::to_string(slot)] = material_id;
    }
    material_json["slots"] = std::move(slots);
  }
  return material_json;
}

MaterialComponent
qdr_material_component_from_json(const nlohmann::ordered_json &value) {
  MaterialComponent material;
  material.material = value.value("material", kInvalidAssetId);
  if (const auto slots = value.find("slots");
      slots != value.end() && slots->is_object()) {
    for (const auto &[slotText, materialId] : slots->items()) {
      std::size_t consumed = 0u;
      const unsigned long slot = std::stoul(slotText, &consumed, 10);
      if (consumed != slotText.size() ||
          slot > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("QDR project document material slot binding "
                                 "contains an invalid slot id.");
      }
      material.material_slots[static_cast<std::uint32_t>(slot)] =
          materialId.get<AssetId>();
    }
  }
  return material;
}

nlohmann::ordered_json
qdr_light_component_to_json(const LightComponent &light) {
  return Json{
      {"type", qdr_string(qdr_light_type_name(light.type))},
      {"color", qdr_vec3_to_json(light.color)},
      {"intensity", light.intensity},
      {"range", light.range},
      {"castsShadows", light.casts_shadows},
  };
}

LightComponent
qdr_light_component_from_json(const nlohmann::ordered_json &value) {
  return qdr_light_component_from_json(
      value, QdrProjectEnumParseMode::LegacyDefault);
}

LightComponent
qdr_light_component_from_json(const nlohmann::ordered_json &value,
                              QdrProjectEnumParseMode mode) {
  LightComponent light;
  light.type = qdr_light_type_from_name(
      enum_field_string(value, "type", "light type", mode, "directional"),
      mode);
  light.color = qdr_vec3_from_json(value.value("color", Json::object()),
                                   {1.0f, 1.0f, 1.0f});
  light.intensity = value.value("intensity", 1.0f);
  light.range = value.value("range", 10.0f);
  light.casts_shadows = value.value("castsShadows", false);
  return light;
}

nlohmann::ordered_json
qdr_scene_settings_component_to_json(const SceneSettingsComponent &settings) {
  return Json{
      {"environment",
       Json{
           {"environmentId", settings.environment.environment_id},
           {"enabled", settings.environment.enabled},
           {"showBackground", settings.environment.show_background},
           {"clearColor", qdr_vec3_to_json(settings.environment.clear_color)},
           {"intensity", settings.environment.intensity},
           {"backgroundIntensity", settings.environment.background_intensity},
           {"rotationY", settings.environment.rotation_y},
       }},
      {"camera",
       Json{
           {"aperture", settings.camera.aperture},
           {"shutterSpeed", settings.camera.shutter_speed},
           {"sensitivity", settings.camera.sensitivity},
           {"nearPlane", settings.camera.near_plane},
           {"farPlane", settings.camera.far_plane},
       }},
      {"postProcess",
       Json{
           {"enabled", settings.post_process.enabled},
           {"antiAliasing", settings.post_process.anti_aliasing},
           {"ambientOcclusion", settings.post_process.ambient_occlusion},
           {"ambientOcclusionIntensity",
            settings.post_process.ambient_occlusion_intensity},
           {"ambientOcclusionRadius",
            settings.post_process.ambient_occlusion_radius},
           {"bloom", settings.post_process.bloom},
           {"bloomStrength", settings.post_process.bloom_strength},
           {"dithering", settings.post_process.dithering},
           {"toneMapping", qdr_string(qdr_scene_tone_mapping_name(
                               settings.post_process.tone_mapping))},
       }},
  };
}

SceneSettingsComponent
qdr_scene_settings_component_from_json(const nlohmann::ordered_json &value) {
  return qdr_scene_settings_component_from_json(
      value, QdrProjectEnumParseMode::LegacyDefault);
}

SceneSettingsComponent
qdr_scene_settings_component_from_json(const nlohmann::ordered_json &value,
                                       QdrProjectEnumParseMode mode) {
  SceneSettingsComponent settings;
  if (!value.is_object()) {
    return settings;
  }
  if (const auto environment_json = value.find("environment");
      environment_json != value.end() && environment_json->is_object()) {
    settings.environment.environment_id = environment_json->value(
        "environmentId", settings.environment.environment_id);
    settings.environment.enabled =
        environment_json->value("enabled", settings.environment.enabled);
    settings.environment.show_background = environment_json->value(
        "showBackground", settings.environment.show_background);
    if (const auto clear_color_json = environment_json->find("clearColor");
        clear_color_json != environment_json->end()) {
      settings.environment.clear_color = qdr_vec3_from_json(
          *clear_color_json, settings.environment.clear_color);
    }
    settings.environment.intensity =
        environment_json->value("intensity", settings.environment.intensity);
    settings.environment.background_intensity = environment_json->value(
        "backgroundIntensity", settings.environment.background_intensity);
    settings.environment.rotation_y =
        environment_json->value("rotationY", settings.environment.rotation_y);
  }
  if (const auto camera_json = value.find("camera");
      camera_json != value.end() && camera_json->is_object()) {
    settings.camera.aperture =
        camera_json->value("aperture", settings.camera.aperture);
    settings.camera.shutter_speed =
        camera_json->value("shutterSpeed", settings.camera.shutter_speed);
    settings.camera.sensitivity =
        camera_json->value("sensitivity", settings.camera.sensitivity);
    settings.camera.near_plane =
        camera_json->value("nearPlane", settings.camera.near_plane);
    settings.camera.far_plane =
        camera_json->value("farPlane", settings.camera.far_plane);
  }
  if (const auto post_process_json = value.find("postProcess");
      post_process_json != value.end() && post_process_json->is_object()) {
    settings.post_process.enabled =
        post_process_json->value("enabled", settings.post_process.enabled);
    settings.post_process.anti_aliasing = post_process_json->value(
        "antiAliasing", settings.post_process.anti_aliasing);
    settings.post_process.ambient_occlusion = post_process_json->value(
        "ambientOcclusion", settings.post_process.ambient_occlusion);
    settings.post_process.ambient_occlusion_intensity =
        post_process_json->value(
            "ambientOcclusionIntensity",
            settings.post_process.ambient_occlusion_intensity);
    settings.post_process.ambient_occlusion_radius = post_process_json->value(
        "ambientOcclusionRadius",
        settings.post_process.ambient_occlusion_radius);
    settings.post_process.bloom =
        post_process_json->value("bloom", settings.post_process.bloom);
    settings.post_process.bloom_strength = post_process_json->value(
        "bloomStrength", settings.post_process.bloom_strength);
    settings.post_process.dithering =
        post_process_json->value("dithering", settings.post_process.dithering);
    settings.post_process.tone_mapping = qdr_scene_tone_mapping_from_name(
        enum_field_string(*post_process_json, "toneMapping",
                          "scene tone mapping", mode, "aces"),
        mode);
  }
  return settings;
}

} // namespace quader
