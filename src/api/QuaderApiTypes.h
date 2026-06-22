////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "math/Types.h"
#include "editor/modeling/services/ModelingActionCatalog.h"
#include "editor/modeling/ModelingTypes.h"
#include "scene/Components.h"

#include <mesh/polygon/document.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace quader {

using QuaderElementId = std::int32_t;
inline constexpr QuaderElementId InvalidQuaderElementId = -1;
using QuaderPolygonDocument = quader_poly::Document;

/**
 * Enumerates QuaderComponentKind values used by the api layer.
 */
enum class QuaderComponentKind {
    Object,
    Vertex,
    Edge,
    Face,
};

/**
 * Enumerates QuaderActiveSelectionKind values used by the api layer.
 */
enum class QuaderActiveSelectionKind {
    None,
    Object,
    Vertex,
    Edge,
    Face,
    Light,
};

/**
 * Enumerates QuaderAssetKind values used by the api layer.
 */
enum class QuaderAssetKind {
    Mesh,
    Material,
    Texture,
};

/**
 * Enumerates QuaderSelectionEdit values used by the api layer.
 */
enum class QuaderSelectionEdit {
    Replace,
    Add,
    Remove,
    Toggle,
};

/**
 * Enumerates QuaderMutationKind values used by the api layer.
 */
enum class QuaderMutationKind {
    None,
    Document,
    Modeling,
    Asset,
    Runtime,
};

/**
 * Represents a Quader Revision Stamp value used by the public Quader API surface.
 */
struct QuaderRevisionStamp {
    std::uint64_t projectRevision = 0;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t modelingContentRevision = 0;
    std::uint64_t modelingSelectionRevision = 0;
};

/**
 * Represents a Quader Entity Ref value used by the public Quader API surface.
 */
struct QuaderEntityRef {
    EntityId id = InvalidEntityId;
    EditorObjectId editorObjectId = InvalidEditorObjectId;
};

/**
 * Represents a Quader Asset Ref value used by the public Quader API surface.
 */
struct QuaderAssetRef {
    AssetId id = InvalidAssetId;
    QuaderAssetKind kind = QuaderAssetKind::Mesh;
};

/**
 * Represents a Quader Modeling Object Ref value used by the public Quader API surface.
 */
struct QuaderModelingObjectRef {
    EntityId entity = InvalidEntityId;
    EditorObjectId editorObjectId = InvalidEditorObjectId;
    ModelingObjectId modelingObjectId = InvalidModelingObjectId;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
};

/**
 * Represents a Quader Component Ref value used by the public Quader API surface.
 */
struct QuaderComponentRef {
    QuaderModelingObjectRef object;
    QuaderComponentKind kind = QuaderComponentKind::Object;
    QuaderElementId vertex = InvalidQuaderElementId;
    QuaderElementId edgeA = InvalidQuaderElementId;
    QuaderElementId edgeB = InvalidQuaderElementId;
    QuaderElementId face = InvalidQuaderElementId;
};

/**
 * Represents a Quader Bounds value used by the public Quader API surface.
 */
struct QuaderBounds {
    Vec3 min;
    Vec3 max;
};

/**
 * Stores the Quader Create Entity Args data contract used by the public Quader API surface.
 */
struct QuaderCreateEntityArgs {
    std::string name;
    EntityKind kind = EntityKind::Generic;
    EntityId parent = InvalidEntityId;
    TransformComponent transform;
};

/**
 * Stores the Quader Create Box Args data contract used by the public Quader API surface.
 */
struct QuaderCreateBoxArgs {
    ModelingBounds bounds;
    std::string name = "Box";
};

/**
 * Stores the Quader Create Mesh Args data contract used by the public Quader API surface.
 */
struct QuaderCreateMeshArgs {
    std::string name = "Mesh";
    std::uint32_t materialSlot = 0;
};

/**
 * Stores the Quader Component Selection Args data contract used by the public Quader API surface.
 */
struct QuaderComponentSelectionArgs {
    QuaderModelingObjectRef object;
    QuaderComponentKind kind = QuaderComponentKind::Vertex;
    std::vector<QuaderElementId> vertices;
    std::vector<std::pair<QuaderElementId, QuaderElementId>> edges;
    std::vector<QuaderElementId> faces;
    QuaderComponentRef active;
    QuaderSelectionEdit edit = QuaderSelectionEdit::Replace;
};

/**
 * Represents a Quader Preview Settings value used by the public Quader API surface.
 */
struct QuaderPreviewSettings {
    ModelingInteractionSettings interaction;
};

/**
 * Stores the Quader Material Args data contract used by the public Quader API surface.
 */
struct QuaderMaterialArgs {
    std::string name = "Material";
    Vec3 baseColor = {1.0f, 1.0f, 1.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
};

} // namespace quader
