////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_asset_dependency_types.hpp"

#include <memory>

namespace quader {

class ProjectSession;

/// Derives dependencies from document owners; never owns mutable asset truth.
class ProjectAssetDependencyService final {
public:
  explicit ProjectAssetDependencyService(ProjectSession &project);
  ~ProjectAssetDependencyService();

  ProjectAssetDependencyService(const ProjectAssetDependencyService &) = delete;
  ProjectAssetDependencyService &
  operator=(const ProjectAssetDependencyService &) = delete;

  /// A true refresh validates exact disk bytes before a destructive decision.
  /// Ordinary retained reads perform no filesystem or entity scans unchanged.
  [[nodiscard]] std::shared_ptr<const ProjectAssetDependencySnapshot>
  snapshot(bool refresh_files = false) const;
  void invalidate() noexcept;
  [[nodiscard]] const ProjectAssetDependencyWorkStats &work_stats() const noexcept;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace quader
