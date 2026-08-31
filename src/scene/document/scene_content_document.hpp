////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "core/types.hpp"
#include "scene/scene_document.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quader {

/**
 * Stores the Project Document Record data contract used by the editor document ownership layer.
 */
struct SceneContentDocumentRecord {
    std::uint64_t id = 0;
    std::string name = "Untitled Project";
};

/**
 * Selects whether a SceneContentDocument constructor should immediately synthesize the real scene root.
 */
enum class SceneContentDocumentRootPolicy {
    EnsureSceneRoot,
    DeferSceneRoot,
};

/**
 * Stores the Project Document Identity Issue data contract used by the editor document ownership layer.
 */
struct SceneContentDocumentIdentityIssue {
  EntityId entity = kInvalidEntityId;
  EditorObjectId object_id = kInvalidEditorObjectId;
  std::string message;
};

/**
 * Stores the Project Document Identity Report data contract used by the editor document ownership layer.
 */
struct SceneContentDocumentIdentityReport {
    std::vector<SceneContentDocumentIdentityIssue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

/**
 * Stores one Project Document asset/reference issue.
 */
struct SceneContentDocumentAssetReferenceIssue {
  EntityId entity = kInvalidEntityId;
  EditorObjectId object_id = kInvalidEditorObjectId;
  AssetId asset = kInvalidAssetId;
  ProjectAssetId project_asset;
  std::string message;
};

/**
 * Stores Project Document asset/reference validation results.
 */
struct SceneContentDocumentAssetReferenceReport {
    std::vector<SceneContentDocumentAssetReferenceIssue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

/**
 * Owns Project Document behavior within the editor document ownership layer.
 */
class SceneContentDocument {
public:
  explicit SceneContentDocument(SceneContentDocumentRootPolicy root_policy =
                               SceneContentDocumentRootPolicy::EnsureSceneRoot);
  explicit SceneContentDocument(SceneContentDocumentRecord record,
                           SceneContentDocumentRootPolicy root_policy =
                               SceneContentDocumentRootPolicy::EnsureSceneRoot);
  SceneContentDocument(const SceneContentDocument &) = delete;
  SceneContentDocument &operator=(const SceneContentDocument &) = delete;
  SceneContentDocument(SceneContentDocument &&) noexcept = default;
  SceneContentDocument &operator=(SceneContentDocument &&) noexcept = default;

  std::uint64_t id() const { return record_.id; }
  const std::string &name() const { return record_.name; }
  void rename(std::string name);

  SceneDocument &scene() { return scene_; }
  const SceneDocument &scene() const { return scene_; }

  AssetRegistry &assets() { return assets_; }
  const AssetRegistry &assets() const { return assets_; }

  SceneContentDocumentIdentityReport validate_object_identity_invariants() const;
  SceneContentDocumentAssetReferenceReport validate_asset_references() const;
  EntityId ensure_scene_root();

  std::uint64_t revision() const;

private:
  void mark_dirty();

  SceneContentDocumentRecord record_;
  std::uint64_t revision_ = 0;
  SceneDocument scene_;
  AssetRegistry assets_;
};

} // namespace quader
