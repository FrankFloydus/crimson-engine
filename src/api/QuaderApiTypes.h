#pragma once

#include "core/Types.h"
#include "math/Types.h"
#include "modeling/ModelingActionCatalog.h"
#include "modeling/ModelingSessionService.h"
#include "scene/Components.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace quader {

using QuaderElementId = std::int32_t;
inline constexpr QuaderElementId InvalidQuaderElementId = -1;

enum class QuaderComponentKind {
    Object,
    Vertex,
    Edge,
    Face,
};

enum class QuaderActiveSelectionKind {
    None,
    Object,
    Vertex,
    Edge,
    Face,
    Light,
};

enum class QuaderAssetKind {
    Mesh,
    Material,
    Texture,
};

enum class QuaderSelectionEdit {
    Replace,
    Add,
    Remove,
    Toggle,
};

enum class QuaderMutationKind {
    None,
    Document,
    Modeling,
    Asset,
    Runtime,
};

struct QuaderRevisionStamp {
    std::uint64_t projectRevision = 0;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t sdkContentRevision = 0;
    std::uint64_t sdkSelectionRevision = 0;
};

struct QuaderEntityRef {
    EntityId id = InvalidEntityId;
};

struct QuaderAssetRef {
    AssetId id = InvalidAssetId;
    QuaderAssetKind kind = QuaderAssetKind::Mesh;
};

struct QuaderModelingObjectRef {
    EntityId entity = InvalidEntityId;
    ModelingObjectId sdkObjectId = InvalidModelingObjectId;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
};

struct QuaderComponentRef {
    QuaderModelingObjectRef object;
    QuaderComponentKind kind = QuaderComponentKind::Object;
    QuaderElementId vertex = InvalidQuaderElementId;
    QuaderElementId edgeA = InvalidQuaderElementId;
    QuaderElementId edgeB = InvalidQuaderElementId;
    QuaderElementId face = InvalidQuaderElementId;
};

struct QuaderBounds {
    Vec3 min;
    Vec3 max;
};

struct QuaderCreateEntityArgs {
    std::string name;
    EntityKind kind = EntityKind::Generic;
    EntityId parent = InvalidEntityId;
    TransformComponent transform;
};

struct QuaderCreateBoxArgs {
    ModelingBounds bounds;
    std::string name = "Box";
};

struct QuaderCreateMeshArgs {
    std::string name = "Mesh";
    std::uint32_t materialSlot = 0;
};

struct QuaderComponentSelectionArgs {
    QuaderModelingObjectRef object;
    QuaderComponentKind kind = QuaderComponentKind::Vertex;
    std::vector<QuaderElementId> vertices;
    std::vector<std::pair<QuaderElementId, QuaderElementId>> edges;
    std::vector<QuaderElementId> faces;
    QuaderComponentRef active;
    QuaderSelectionEdit edit = QuaderSelectionEdit::Replace;
};

struct QuaderPreviewSettings {
    ModelingInteractionSettings interaction;
};

struct QuaderMaterialArgs {
    std::string name = "Material";
    Vec3 baseColor = {1.0f, 1.0f, 1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
};

} // namespace quader
