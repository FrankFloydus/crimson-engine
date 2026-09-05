////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "io/project/project_types.hpp"

#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class ProjectSession;
class QuaderEditorApi;
class QuaderProjectApi;
class QuaderSceneDocumentsApiInternalAccess;
class QuaderEventsApi;

/// Describes one open scene tab through stable public value types.
struct QuaderSceneDocumentInfo final {
  SceneSessionId session_id;
  SceneId scene_id;
  std::string name;
  std::optional<ProjectRelativePath> path;
  bool dirty = false;
  bool active = false;
};

/// A project recovery copy; recovering opens an independent unsaved scene tab.
struct QuaderAutosaveInfo final {
  ProjectRelativePath path;
  SceneId scene_id;
  std::string name;
  std::optional<ProjectRelativePath> source_path;
  std::int64_t saved_at_unix_ms = 0;
};

/// Selects the user decision for a dirty-scene close prompt.
enum class QuaderDirtySceneDecision {
  Save,
  Discard,
  Cancel,
};

/// Supplies native dialog and confirmation hooks to the scene API.
struct QuaderSceneDocumentsRuntimeHooks final {
  std::function<std::optional<std::filesystem::path>()> choose_open_scene_path;
  std::function<std::optional<std::filesystem::path>(SceneSessionId)>
      choose_save_scene_as_path;
  std::function<QuaderDirtySceneDecision(const QuaderSceneDocumentInfo &,
                                         std::string_view action_id)>
      confirm_dirty_scene;
};

/// Provides public multi-document scene lifecycle operations.
class QuaderSceneDocumentsApi final {
public:
  explicit QuaderSceneDocumentsApi(QuaderApiSession standalone_session);
  explicit QuaderSceneDocumentsApi(ProjectSession &project_session);
  ~QuaderSceneDocumentsApi();

  QuaderSceneDocumentsApi(const QuaderSceneDocumentsApi &) = delete;
  QuaderSceneDocumentsApi &operator=(const QuaderSceneDocumentsApi &) = delete;

  QuaderApiResult<SceneSessionId> new_scene();
  QuaderApiResult<SceneSessionId> open(ProjectRelativePath path);
  QuaderApiResult<SceneSessionId> open();
  QuaderApiResult<SceneSessionId>
  open_native_path(const std::filesystem::path &path);
  QuaderApiVoidResult activate(SceneSessionId id);
  QuaderApiVoidResult save(SceneSessionId id);
  QuaderApiVoidResult save_active();
  QuaderApiVoidResult save_as(SceneSessionId id, ProjectRelativePath path);
  QuaderApiVoidResult save_as(SceneSessionId id);
  QuaderApiVoidResult save_as_native_path(
      SceneSessionId id, const std::filesystem::path &path);
  QuaderApiVoidResult save_all();
  [[nodiscard]] QuaderApiResult<std::vector<QuaderAutosaveInfo>> autosaves() const;
  /// Creates/resolves recovery storage for the native recovery file picker.
  [[nodiscard]] QuaderApiResult<std::filesystem::path> autosave_directory() const;
  QuaderApiResult<SceneSessionId>
  recover_autosave(ProjectRelativePath path);
  QuaderApiResult<SceneSessionId>
  recover_autosave_native_path(const std::filesystem::path &path);
  QuaderApiVoidResult close(SceneSessionId id);
  QuaderApiVoidResult close_active();
  QuaderApiVoidResult activate_next();
  QuaderApiVoidResult activate_previous();

  [[nodiscard]] std::vector<QuaderSceneDocumentInfo> open_scenes() const;
  [[nodiscard]] std::optional<QuaderSceneDocumentInfo> active_scene() const;
  [[nodiscard]] std::optional<SceneSessionId> active_scene_id() const;
  [[nodiscard]] bool has_active_scene() const;
  [[nodiscard]] bool has_dirty_scenes() const;

  void set_runtime_hooks(QuaderSceneDocumentsRuntimeHooks hooks);

private:
  /// Reports whether all scene-close prompts allow project closure to proceed.
  struct ProjectClosePreparation {
    bool proceed = false;
  };
  [[nodiscard]] QuaderApiResult<ProjectClosePreparation>
  prepare_project_close(std::string_view action_id);
  void set_binding_changed_callback(
      std::function<void(QuaderApiSession)> callback);
  void attach_events(QuaderEventsApi &events);
  void publish_dirty_changed();

  friend class QuaderEditorApi;
  friend class QuaderProjectApi;
  friend class QuaderSceneDocumentsApiInternalAccess;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
