////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_project_codecs.hpp"

namespace quader {

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

} // namespace quader
