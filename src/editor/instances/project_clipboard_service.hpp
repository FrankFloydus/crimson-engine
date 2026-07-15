////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "io/project/project_types.hpp"

#include <memory>

namespace quader {

class ProjectSession;

/**
 * Owns the editor clipboard whose lifetime spans the open scenes of one
 * project. Formal instance payloads are self-contained so a destination scene
 * never retains an entity reference into the source scene.
 */
class ProjectClipboardService final {
public:
  ProjectClipboardService();
  ~ProjectClipboardService();

  ProjectClipboardService(const ProjectClipboardService &) = delete;
  ProjectClipboardService &operator=(const ProjectClipboardService &) = delete;
  ProjectClipboardService(ProjectClipboardService &&) noexcept;
  ProjectClipboardService &operator=(ProjectClipboardService &&) noexcept;

  /** True when the active selection contains at least one occurrence root. */
  [[nodiscard]] bool
  selection_contains_instance(const ProjectSession &project) const;

  /** True only for a supported all-occurrence Object-mode selection. */
  [[nodiscard]] bool
  can_copy_instance_selection(const ProjectSession &project) const;

  /** Captures selected occurrences plus their scene-local definitions. */
  [[nodiscard]] CommandResult
  copy_instance_selection(ProjectSession &project);

  [[nodiscard]] bool has_instance_payload() const;
  [[nodiscard]] bool
  can_paste_instance_selection(const ProjectSession &project) const;

  /**
   * Pastes linked occurrences in the source scene, or imports independent
   * definitions when another scene in the same project is active.
   */
  [[nodiscard]] CommandResult
  paste_instance_selection(ProjectSession &project);

  /** Records that the existing ordinary-modeling clipboard is authoritative. */
  void note_ordinary_copy(SceneSessionId source_scene);
  [[nodiscard]] bool has_ordinary_payload() const;
  [[nodiscard]] bool
  ordinary_payload_belongs_to(SceneSessionId scene) const;

  void clear();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
