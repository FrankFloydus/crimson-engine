////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quader {

class ProjectSession;
class SceneSession;

/** Classifies the backing document represented by a presentation item. */
enum class MaterialPresentationKind : std::uint8_t {
  Embedded,
  Standard,
  Graph,
};

/** Captures source-owner revisions that identify one material catalog publication. */
struct MaterialPresentationRevision final {
  std::uint64_t project_activation_generation = 0u;
  std::uint64_t project_material_revision = 0u;
  std::uint64_t active_scene_id = 0u;
  std::uint64_t embedded_material_revision = 0u;

  friend bool operator==(const MaterialPresentationRevision &,
                         const MaterialPresentationRevision &) = default;
};

/** Carries renderer-neutral identity and display metadata for one material. */
struct MaterialPresentationItem final {
  MaterialReference reference;
  MaterialPresentationKind kind = MaterialPresentationKind::Embedded;
  std::string name;
  std::string project_relative_path;
  std::uintmax_t qmat_size_bytes = 0u;
  std::vector<std::byte> encoded_albedo;
  std::array<float, 3> fallback_color{1.0f, 1.0f, 1.0f};
  std::uint64_t content_revision = 0u;
  bool available = true;
  bool editable = false;
};

/** Publishes an immutable material catalog together with its source revision. */
struct MaterialPresentationSnapshot final {
  MaterialPresentationRevision revision;
  std::vector<MaterialPresentationItem> materials;
};

/**
 * Owns the cross-session projection used by every retained material surface.
 * Project and scene owners remain authoritative; the service only captures an
 * immutable catalog plus a cheap source revision for unchanged-frame gates.
 */
class MaterialPresentationService final {
public:
  [[nodiscard]] static MaterialPresentationRevision
  revision(const ProjectSession &project, const SceneSession *scene);

  [[nodiscard]] static MaterialPresentationSnapshot
  snapshot(ProjectSession &project, const SceneSession *scene);
};

} // namespace quader

