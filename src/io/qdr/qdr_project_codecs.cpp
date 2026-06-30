////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_project_codecs.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace quader {
namespace {

using json = nlohmann::ordered_json;

std::string qdrString(std::string_view value) {
    return std::string(value.data(), value.size());
}

} // namespace

std::string_view qdrEntityKindName(EntityKind kind) {
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

EntityKind qdrEntityKindFromName(std::string_view value) {
    if (value == "empty") {
        return EntityKind::Empty;
    }
    if (value == "mesh") {
        return EntityKind::Mesh;
    }
    if (value == "light") {
        return EntityKind::Light;
    }
    return EntityKind::Generic;
}

std::string_view qdrLightTypeName(LightType type) {
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

LightType qdrLightTypeFromName(std::string_view value) {
    if (value == "point") {
        return LightType::Point;
    }
    if (value == "spot") {
        return LightType::Spot;
    }
    return LightType::Directional;
}

std::string_view qdrSceneToneMappingName(SceneToneMapping toneMapping) {
    switch (toneMapping) {
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

SceneToneMapping qdrSceneToneMappingFromName(std::string_view value) {
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
    return SceneToneMapping::Aces;
}

std::string_view qdrMeshCollisionTypeName(MeshCollisionType type) {
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

MeshCollisionType qdrMeshCollisionTypeFromName(std::string_view value) {
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
    return MeshCollisionType::Box;
}

std::string_view qdrMeshPhysicsBodyTypeName(MeshPhysicsBodyType type) {
    switch (type) {
    case MeshPhysicsBodyType::StaticBody:
        return "staticBody";
    case MeshPhysicsBodyType::RigidBody:
        return "rigidBody";
    }
    return "staticBody";
}

MeshPhysicsBodyType qdrMeshPhysicsBodyTypeFromName(std::string_view value) {
    if (value == "rigidBody") {
        return MeshPhysicsBodyType::RigidBody;
    }
    return MeshPhysicsBodyType::StaticBody;
}

std::string_view qdrTextureUsageName(TextureUsage usage) {
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

TextureUsage qdrTextureUsageFromName(std::string_view value) {
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
    return TextureUsage::Unknown;
}

std::string_view qdrTextureColorSpaceName(TextureColorSpace colorSpace) {
    switch (colorSpace) {
    case TextureColorSpace::Linear:
        return "linear";
    case TextureColorSpace::Srgb:
        return "srgb";
    }
    return "srgb";
}

TextureColorSpace qdrTextureColorSpaceFromName(std::string_view value) {
    if (value == "linear") {
        return TextureColorSpace::Linear;
    }
    return TextureColorSpace::Srgb;
}

nlohmann::ordered_json qdrVec2ToJson(QVec2 value) {
    return json{{"x", value.x}, {"y", value.y}};
}

nlohmann::ordered_json qdrVec3ToJson(QVec3 value) {
    return json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

QVec2 qdrVec2FromJson(const nlohmann::ordered_json& value) {
    return {
            value.value("x", 0.0f),
            value.value("y", 0.0f),
    };
}

QVec3 qdrVec3FromJson(const nlohmann::ordered_json& value, QVec3 fallback) {
    if (!value.is_object()) {
        return fallback;
    }
    return {
            value.value("x", fallback.x),
            value.value("y", fallback.y),
            value.value("z", fallback.z),
    };
}

nlohmann::ordered_json qdrTransformComponentToJson(const TransformComponent& transform) {
    return json{
            {"position", qdrVec3ToJson(transform.position)},
            {"rotationEuler", qdrVec3ToJson(transform.rotationEuler)},
            {"scale", qdrVec3ToJson(transform.scale)},
    };
}

TransformComponent qdrTransformComponentFromJson(const nlohmann::ordered_json& value) {
    TransformComponent transform;
    if (!value.is_object()) {
        return transform;
    }
    transform.position = qdrVec3FromJson(value.value("position", json::object()));
    transform.rotationEuler = qdrVec3FromJson(value.value("rotationEuler", json::object()));
    transform.scale = qdrVec3FromJson(value.value("scale", json::object()), {1.0f, 1.0f, 1.0f});
    return transform;
}

nlohmann::ordered_json qdrMeshComponentToJson(const MeshComponent& mesh) {
    return json{
            {"mesh", mesh.mesh},
            {"editorOnly", mesh.editorOnly},
            {"castShadows", mesh.castShadows},
            {"doubleSided", mesh.doubleSided},
            {"generateCollisions", mesh.generateCollisions},
            {"collisionType", qdrString(qdrMeshCollisionTypeName(mesh.collisionType))},
            {"displayCollision", mesh.displayCollision},
            {"bodyType", qdrString(qdrMeshPhysicsBodyTypeName(mesh.bodyType))},
    };
}

MeshComponent qdrMeshComponentFromJson(const nlohmann::ordered_json& value) {
    MeshComponent mesh;
    mesh.mesh = value.value("mesh", InvalidAssetId);
    mesh.editorOnly = value.value("editorOnly", false);
    mesh.castShadows = value.value("castShadows", true);
    mesh.doubleSided = value.value("doubleSided", false);
    mesh.generateCollisions = value.value("generateCollisions", true);
    mesh.collisionType = qdrMeshCollisionTypeFromName(value.value("collisionType", std::string("box")));
    mesh.displayCollision = value.value("displayCollision", false);
    mesh.bodyType = qdrMeshPhysicsBodyTypeFromName(value.value("bodyType", std::string("staticBody")));
    return mesh;
}

nlohmann::ordered_json qdrMaterialComponentToJson(const MaterialComponent& material) {
    json materialJson{{"material", material.material}};
    if (!material.materialSlots.empty()) {
        json slots = json::object();
        for (const auto& [slot, materialId] : material.materialSlots) {
            slots[std::to_string(slot)] = materialId;
        }
        materialJson["slots"] = std::move(slots);
    }
    return materialJson;
}

MaterialComponent qdrMaterialComponentFromJson(const nlohmann::ordered_json& value) {
    MaterialComponent material;
    material.material = value.value("material", InvalidAssetId);
    if (const auto slots = value.find("slots"); slots != value.end() && slots->is_object()) {
        for (const auto& [slotText, materialId] : slots->items()) {
            std::size_t consumed = 0u;
            const unsigned long slot = std::stoul(slotText, &consumed, 10);
            if (consumed != slotText.size() || slot > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("QDR project document material slot binding contains an invalid slot id.");
            }
            material.materialSlots[static_cast<std::uint32_t>(slot)] = materialId.get<AssetId>();
        }
    }
    return material;
}

nlohmann::ordered_json qdrLightComponentToJson(const LightComponent& light) {
    return json{
            {"type", qdrString(qdrLightTypeName(light.type))},
            {"color", qdrVec3ToJson(light.color)},
            {"intensity", light.intensity},
            {"range", light.range},
            {"castsShadows", light.castsShadows},
    };
}

LightComponent qdrLightComponentFromJson(const nlohmann::ordered_json& value) {
    LightComponent light;
    light.type = qdrLightTypeFromName(value.value("type", std::string("directional")));
    light.color = qdrVec3FromJson(value.value("color", json::object()), {1.0f, 1.0f, 1.0f});
    light.intensity = value.value("intensity", 1.0f);
    light.range = value.value("range", 10.0f);
    light.castsShadows = value.value("castsShadows", false);
    return light;
}

nlohmann::ordered_json qdrSceneSettingsComponentToJson(const SceneSettingsComponent& settings) {
    return json{
            {"environment", json{
                    {"environmentId", settings.environment.environmentId},
                    {"enabled", settings.environment.enabled},
                    {"showBackground", settings.environment.showBackground},
                    {"clearColor", qdrVec3ToJson(settings.environment.clearColor)},
                    {"intensity", settings.environment.intensity},
                    {"backgroundIntensity", settings.environment.backgroundIntensity},
                    {"rotationY", settings.environment.rotationY},
            }},
            {"camera", json{
                    {"aperture", settings.camera.aperture},
                    {"shutterSpeed", settings.camera.shutterSpeed},
                    {"sensitivity", settings.camera.sensitivity},
                    {"nearPlane", settings.camera.nearPlane},
                    {"farPlane", settings.camera.farPlane},
            }},
            {"postProcess", json{
                    {"enabled", settings.postProcess.enabled},
                    {"antiAliasing", settings.postProcess.antiAliasing},
                    {"ambientOcclusion", settings.postProcess.ambientOcclusion},
                    {"ambientOcclusionIntensity", settings.postProcess.ambientOcclusionIntensity},
                    {"ambientOcclusionRadius", settings.postProcess.ambientOcclusionRadius},
                    {"bloom", settings.postProcess.bloom},
                    {"bloomStrength", settings.postProcess.bloomStrength},
                    {"dithering", settings.postProcess.dithering},
                    {"toneMapping", qdrString(qdrSceneToneMappingName(settings.postProcess.toneMapping))},
            }},
    };
}

SceneSettingsComponent qdrSceneSettingsComponentFromJson(const nlohmann::ordered_json& value) {
    SceneSettingsComponent settings;
    if (!value.is_object()) {
        return settings;
    }
    if (const auto environmentJson = value.find("environment");
            environmentJson != value.end() && environmentJson->is_object()) {
        settings.environment.environmentId = environmentJson->value("environmentId", settings.environment.environmentId);
        settings.environment.enabled = environmentJson->value("enabled", settings.environment.enabled);
        settings.environment.showBackground =
                environmentJson->value("showBackground", settings.environment.showBackground);
        if (const auto clearColorJson = environmentJson->find("clearColor");
                clearColorJson != environmentJson->end()) {
            settings.environment.clearColor = qdrVec3FromJson(*clearColorJson, settings.environment.clearColor);
        }
        settings.environment.intensity = environmentJson->value("intensity", settings.environment.intensity);
        settings.environment.backgroundIntensity =
                environmentJson->value("backgroundIntensity", settings.environment.backgroundIntensity);
        settings.environment.rotationY = environmentJson->value("rotationY", settings.environment.rotationY);
    }
    if (const auto cameraJson = value.find("camera"); cameraJson != value.end() && cameraJson->is_object()) {
        settings.camera.aperture = cameraJson->value("aperture", settings.camera.aperture);
        settings.camera.shutterSpeed = cameraJson->value("shutterSpeed", settings.camera.shutterSpeed);
        settings.camera.sensitivity = cameraJson->value("sensitivity", settings.camera.sensitivity);
        settings.camera.nearPlane = cameraJson->value("nearPlane", settings.camera.nearPlane);
        settings.camera.farPlane = cameraJson->value("farPlane", settings.camera.farPlane);
    }
    if (const auto postProcessJson = value.find("postProcess");
            postProcessJson != value.end() && postProcessJson->is_object()) {
        settings.postProcess.enabled = postProcessJson->value("enabled", settings.postProcess.enabled);
        settings.postProcess.antiAliasing = postProcessJson->value("antiAliasing", settings.postProcess.antiAliasing);
        settings.postProcess.ambientOcclusion =
                postProcessJson->value("ambientOcclusion", settings.postProcess.ambientOcclusion);
        settings.postProcess.ambientOcclusionIntensity =
                postProcessJson->value("ambientOcclusionIntensity", settings.postProcess.ambientOcclusionIntensity);
        settings.postProcess.ambientOcclusionRadius =
                postProcessJson->value("ambientOcclusionRadius", settings.postProcess.ambientOcclusionRadius);
        settings.postProcess.bloom = postProcessJson->value("bloom", settings.postProcess.bloom);
        settings.postProcess.bloomStrength =
                postProcessJson->value("bloomStrength", settings.postProcess.bloomStrength);
        settings.postProcess.dithering = postProcessJson->value("dithering", settings.postProcess.dithering);
        settings.postProcess.toneMapping = qdrSceneToneMappingFromName(
                postProcessJson->value("toneMapping", std::string("aces")));
    }
    return settings;
}

} // namespace quader
