////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_types.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace quader {

class SceneSession;
class ProjectSession;

/// Reports whether an immutable capture was queued and its stable reference.
struct SelectionDiagnosticRequest final {
  bool ok = false;
  std::string id;
  std::string message;
};

/// Returns a durable dump location, bounded report or explicit write failure.
struct SelectionDiagnosticReply final {
  bool ok = false;
  std::string id;
  std::filesystem::path absolute_path;
  std::string summary;
  std::string message;
};

/** Captures on the owner thread; serializes only pinned immutable data on a worker. */
class SelectionDiagnosticService final {
public:
  SelectionDiagnosticService();
  ~SelectionDiagnosticService();
  SelectionDiagnosticService(const SelectionDiagnosticService &) = delete;
  SelectionDiagnosticService &operator=(const SelectionDiagnosticService &) = delete;

  [[nodiscard]] SelectionDiagnosticRequest request_dump(
      const SceneSession &scene, const ProjectSession *project,
      SceneSessionId scene_id, std::filesystem::path directory);
  [[nodiscard]] bool pending() const noexcept;
  [[nodiscard]] std::optional<SelectionDiagnosticReply> take_completed();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
