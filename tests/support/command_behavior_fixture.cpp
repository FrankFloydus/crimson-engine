////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "command_behavior_fixture.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_dev_tools_api.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader::test {
namespace {

[[noreturn]] void setup_failure(std::string_view context,
                                std::string_view message) {
  throw std::runtime_error(std::string(context) + ": " +
                           std::string(message));
}

void require_ok(const QuaderApiVoidResult &result,
                std::string_view context) {
  if (!result.ok()) {
    setup_failure(context, result.error_message());
  }
}

template <typename T>
T require_value(QuaderApiResult<T> result, std::string_view context) {
  if (!result.ok()) {
    setup_failure(context, result.error_message());
  }
  return result.value();
}

void require_project_ok(const ProjectSessionResult &result,
                        std::string_view context) {
  if (!result.ok) {
    setup_failure(context, result.message);
  }
}

void require_command_ok(const CommandResult &result,
                        std::string_view context) {
  if (!result.ok) {
    setup_failure(context, result.message);
  }
}

ProjectRelativePath project_path(std::string_view path) {
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse_portable(path);
  if (!parsed.ok()) {
    setup_failure("Parse project-relative path", parsed.error().message);
  }
  return parsed.take_value();
}

QuaderElementId api_element(quader_poly::ElementId element) {
  return static_cast<QuaderElementId>(element);
}

/// Owns the only directory tree this fixture is permitted to remove.
class ScopedFixtureRoot final {
public:
  ScopedFixtureRoot() {
    static std::atomic<std::uint64_t> sequence{0};
    const std::uint64_t tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t serial = sequence.fetch_add(1u);
    path_ = std::filesystem::temp_directory_path() /
            ("quader-command-behavior-" + std::to_string(tick) + "-" +
             std::to_string(serial));
    std::error_code error;
    if (!std::filesystem::create_directory(path_, error) || error) {
      setup_failure("Create command behavior temporary root",
                    error ? error.message() : "directory already exists");
    }
  }

  ~ScopedFixtureRoot() {
    const std::filesystem::path expected_parent =
        std::filesystem::temp_directory_path().lexically_normal();
    const std::filesystem::path normalized = path_.lexically_normal();
    const std::string filename = normalized.filename().string();
    if (normalized.parent_path() != expected_parent ||
        !filename.starts_with("quader-command-behavior-")) {
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(normalized, ignored);
  }

  ScopedFixtureRoot(const ScopedFixtureRoot &) = delete;
  ScopedFixtureRoot &operator=(const ScopedFixtureRoot &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

  [[nodiscard]] std::filesystem::path
  make_empty_child(std::string_view name) const {
    const std::filesystem::path child = path_ / std::string(name);
    std::error_code error;
    if (!std::filesystem::create_directory(child, error) || error) {
      setup_failure("Create command behavior project root",
                    error ? error.message() : "directory already exists");
    }
    return child;
  }

private:
  std::filesystem::path path_;
};

/// Holds one deterministic neutral polygon document and its stable element IDs.
struct PolygonFixture {
  quader_poly::Document document;
  std::vector<quader_poly::ElementId> vertices;
  std::vector<quader_poly::ElementId> faces;
};

PolygonFixture make_two_quad_plane() {
  PolygonFixture fixture;
  fixture.vertices = {
      quader_poly::add_vertex(fixture.document, {-1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {0.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {-1.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {0.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, 1.0f, 0.0f}),
  };
  fixture.faces.push_back(quader_poly::add_face(
      fixture.document,
      std::array{fixture.vertices[0], fixture.vertices[1],
                 fixture.vertices[4], fixture.vertices[3]}));
  fixture.faces.push_back(quader_poly::add_face(
      fixture.document,
      std::array{fixture.vertices[1], fixture.vertices[2],
                 fixture.vertices[5], fixture.vertices[4]}));
  if (std::ranges::any_of(fixture.faces, [](quader_poly::ElementId face) {
        return face == quader_poly::kInvalidElementId;
      })) {
    setup_failure("Create canonical two-quad plane",
                  "polygon face creation failed");
  }
  return fixture;
}

PolygonFixture make_single_quad() {
  PolygonFixture fixture;
  fixture.vertices = {
      quader_poly::add_vertex(fixture.document, {-1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {-1.0f, 1.0f, 0.0f}),
  };
  fixture.faces.push_back(
      quader_poly::add_face(fixture.document, fixture.vertices));
  if (fixture.faces.front() == quader_poly::kInvalidElementId) {
    setup_failure("Create canonical single quad",
                  "polygon face creation failed");
  }
  return fixture;
}

PolygonFixture make_dissolvable_vertex_face() {
  PolygonFixture fixture;
  fixture.vertices = {
      quader_poly::add_vertex(fixture.document, {-1.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {0.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {0.0f, 1.0f, 0.0f}),
  };
  fixture.faces.push_back(
      quader_poly::add_face(fixture.document, fixture.vertices));
  if (fixture.faces.front() == quader_poly::kInvalidElementId) {
    setup_failure("Create canonical dissolvable vertex face",
                  "polygon face creation failed");
  }
  return fixture;
}

PolygonFixture make_texture_face() {
  PolygonFixture fixture;
  fixture.vertices = {
      quader_poly::add_vertex(fixture.document, {0.25f, -1.25f, 0.35f}),
      quader_poly::add_vertex(fixture.document, {3.25f, -0.75f, 0.35f}),
      quader_poly::add_vertex(fixture.document, {2.75f, 2.25f, 0.35f}),
      quader_poly::add_vertex(fixture.document, {-0.25f, 1.5f, 0.35f}),
  };
  fixture.faces.push_back(
      quader_poly::add_face(fixture.document, fixture.vertices));
  if (fixture.faces.front() == quader_poly::kInvalidElementId) {
    setup_failure("Create canonical texture face",
                  "polygon face creation failed");
  }
  quader_poly::Face &face = fixture.document.faces.front();
  face.uvs = {{0.13f, 0.27f}, {1.47f, 0.38f},
              {1.22f, 1.64f}, {-0.16f, 1.41f}};
  face.texture_transform = {
      .scale = {1.3f, -0.8f},
      .shift_uv = {0.17f, -0.23f},
      .rotation_degrees = 23.0f,
  };
  return fixture;
}

PolygonFixture make_near_duplicate_triangles(float duplicate_offset) {
  PolygonFixture fixture;
  fixture.vertices = {
      quader_poly::add_vertex(fixture.document, {0.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {0.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(
          fixture.document, {0.0f, 1.0f + duplicate_offset, 0.0f}),
      quader_poly::add_vertex(fixture.document, {-1.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {-1.0f, 0.0f, 0.0f}),
  };
  fixture.faces = {
      quader_poly::add_face(
          fixture.document,
          std::array{fixture.vertices[0], fixture.vertices[1],
                     fixture.vertices[2]}),
      quader_poly::add_face(
          fixture.document,
          std::array{fixture.vertices[3], fixture.vertices[4],
                     fixture.vertices[5]}),
  };
  if (std::ranges::any_of(fixture.faces, [](quader_poly::ElementId face) {
        return face == quader_poly::kInvalidElementId;
      })) {
    setup_failure("Create canonical near-duplicate triangles",
                  "polygon face creation failed");
  }
  return fixture;
}

PolygonFixture make_irregular_radial_quad() {
  PolygonFixture fixture;
  fixture.vertices = {
      quader_poly::add_vertex(fixture.document, {-2.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {0.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {3.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(fixture.document, {0.0f, -1.0f, 0.0f}),
  };
  fixture.faces.push_back(
      quader_poly::add_face(fixture.document, fixture.vertices));
  if (fixture.faces.front() == quader_poly::kInvalidElementId) {
    setup_failure("Create canonical irregular radial quad",
                  "polygon face creation failed");
  }
  return fixture;
}

PolygonFixture make_parallel_quad_pair() {
  PolygonFixture fixture;
  fixture.vertices = {
      quader_poly::add_vertex(fixture.document, {-1.0f, -1.0f, -2.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, -1.0f, -2.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, 1.0f, -2.0f}),
      quader_poly::add_vertex(fixture.document, {-1.0f, 1.0f, -2.0f}),
      quader_poly::add_vertex(fixture.document, {-1.0f, -1.0f, 2.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, -1.0f, 2.0f}),
      quader_poly::add_vertex(fixture.document, {1.0f, 1.0f, 2.0f}),
      quader_poly::add_vertex(fixture.document, {-1.0f, 1.0f, 2.0f}),
  };
  fixture.faces = {
      quader_poly::add_face(
          fixture.document,
          std::array{fixture.vertices[0], fixture.vertices[1],
                     fixture.vertices[2], fixture.vertices[3]}),
      quader_poly::add_face(
          fixture.document,
          std::array{fixture.vertices[7], fixture.vertices[6],
                     fixture.vertices[5], fixture.vertices[4]}),
  };
  if (std::ranges::any_of(fixture.faces, [](quader_poly::ElementId face) {
        return face == quader_poly::kInvalidElementId;
      })) {
    setup_failure("Create canonical parallel quad pair",
                  "polygon face creation failed");
  }
  return fixture;
}

PolygonFixture make_pentagon() {
  PolygonFixture fixture;
  constexpr float kTau = 6.28318530717958647692f;
  for (std::size_t index = 0; index < 5u; ++index) {
    const float angle =
        kTau * static_cast<float>(index) / static_cast<float>(5u);
    fixture.vertices.push_back(quader_poly::add_vertex(
        fixture.document, {std::cos(angle), std::sin(angle), 0.0f}));
  }
  fixture.faces.push_back(
      quader_poly::add_face(fixture.document, fixture.vertices));
  if (fixture.faces.front() == quader_poly::kInvalidElementId) {
    setup_failure("Create canonical pentagon", "polygon face creation failed");
  }
  return fixture;
}

PolygonFixture make_open_quad_boundary() {
  PolygonFixture fixture;
  fixture.document = quader_poly::make_cube({});
  if (fixture.document.faces.empty()) {
    setup_failure("Create canonical open boundary", "cube has no faces");
  }
  const quader_poly::Face removed = fixture.document.faces.front();
  fixture.vertices.assign(removed.vertices.begin(), removed.vertices.end());
  fixture.document.faces.erase(fixture.document.faces.begin());
  return fixture;
}

ModelingBoxPreview valid_primitive_preview() {
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = {
      QVec3{-1.0f, 0.0f, -1.0f}, QVec3{-1.0f, 0.0f, 1.0f},
      QVec3{1.0f, 0.0f, 1.0f},   QVec3{1.0f, 0.0f, -1.0f},
      QVec3{-1.0f, 2.0f, -1.0f}, QVec3{-1.0f, 2.0f, 1.0f},
      QVec3{1.0f, 2.0f, 1.0f},   QVec3{1.0f, 2.0f, -1.0f},
  };
  preview.corner_count = preview.corners.size();
  return preview;
}

QuaderApiVoidResult runtime_result(bool changed, std::string message = {}) {
  return {
      .ok = true,
      .changed = changed,
      .message = std::move(message),
      .mutation_kind =
          changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
  };
}

} // namespace

/// Owns the isolated native sessions and deterministic hooks behind one contract run.
class CommandBehaviorFixture::Impl final {
public:
  explicit Impl(const CommandBehaviorContract &contract)
      : contract_(contract), command_id_(contract.command_id) {
    create_editor_owner();
    require_ok(editor().commands().set_keybinding_storage_path(
                   temporary_.path() / "keybindings.json"),
               "Isolate canonical command keybindings");
    install_runtime_hooks();
    configure_fixture();
    configure_command_specific_state();
    require_enabled_command();
    external_calls_ = 0u;
  }

  ~Impl() {
    source_editor_.reset();
    editor_.reset();
    source_project_.reset();
    project_.reset();
    standalone_scene_.reset();
  }

  [[nodiscard]] QuaderEditorApi &editor() { return *editor_; }
  [[nodiscard]] const QuaderEditorApi &editor() const { return *editor_; }

  [[nodiscard]] SceneSession &native_scene() {
    if (project_ != nullptr) {
      OpenSceneSession *active = project_->active_scene();
      return active != nullptr ? *active->session : project_->fallback_scene();
    }
    return *standalone_scene_;
  }

  [[nodiscard]] const SceneSession &native_scene() const {
    if (project_ != nullptr) {
      const OpenSceneSession *active = project_->active_scene();
      return active != nullptr ? *active->session : project_->fallback_scene();
    }
    return *standalone_scene_;
  }

  [[nodiscard]] std::uint64_t external_call_count() const {
    return external_calls_;
  }

private:
  void create_editor_owner() {
    const bool project_backed =
        contract_.fixture == CanonicalCommandFixture::ProjectWithScene ||
        contract_.fixture == CanonicalCommandFixture::TwoScenes ||
        contract_.fixture == CanonicalCommandFixture::ExternalRuntime;
    if (!project_backed) {
      standalone_scene_ = std::make_unique<SceneSession>();
      editor_ = std::make_unique<QuaderEditorApi>(
          QuaderApiSessionInternalAccess::from_scene_session(
              *standalone_scene_));
      return;
    }

    project_root_ = temporary_.make_empty_child("Project");
    project_ = std::make_unique<ProjectSession>();
    require_project_ok(project_->create(project_root_),
                       "Create canonical project");
    editor_ = std::make_unique<QuaderEditorApi>(*project_);
  }

  void install_runtime_hooks() {
    install_preferences_hooks();
    install_viewport_hooks();
    install_ui_hooks();
    install_project_hooks();
    install_scene_document_hooks();
#ifndef NDEBUG
    editor().dev_tools().set_runtime_hooks(QuaderDevToolsRuntimeHooks{
        .set_clipboard_text =
            [this](std::string_view text, std::string &error) {
              ++external_calls_;
              clipboard_text_ = std::string(text);
              error.clear();
              return true;
            },
    });
#endif
  }

  void install_ui_hooks() {
    editor().ui().set_runtime_hooks(QuaderUIRuntimeHooks{
        .show_command_search =
            [this] {
              ++external_calls_;
              return runtime_result(false, "Command Search test hook");
            },
    });
  }

  void install_preferences_hooks() {
    editor().preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
        .locale = [this] { return locale_; },
        .set_locale =
            [this](std::string locale) {
              ++external_calls_;
              const bool changed = locale_ != locale;
              locale_ = std::move(locale);
              return runtime_result(changed);
            },
        .vsync_enabled = [this] { return vsync_enabled_; },
        .set_vsync_enabled =
            [this](bool value) { return set_preference(vsync_enabled_, value); },
        .session_settings = [this] { return session_preferences_; },
        .set_session_settings =
            [this](SessionPreferences value) {
              ++external_calls_;
              const bool changed = session_preferences_ != value;
              session_preferences_ = value;
              return runtime_result(changed);
            },
        .save =
            [this] {
              ++external_calls_;
              return runtime_result(false);
            },
        .snapping_enabled = [this] { return snapping_enabled_; },
        .set_snapping_enabled =
            [this](bool value) {
              return set_preference(snapping_enabled_, value);
            },
        .backface_selection_enabled =
            [this] { return backface_selection_enabled_; },
        .set_backface_selection_enabled =
            [this](bool value) {
              return set_preference(backface_selection_enabled_, value);
            },
        .grid_visible = [this] { return grid_visible_; },
        .set_grid_visible =
            [this](bool value) { return set_preference(grid_visible_, value); },
        .gizmos_visible = [this] { return gizmos_visible_; },
        .set_gizmos_visible =
            [this](bool value) {
              return set_preference(gizmos_visible_, value);
            },
        .mesh_grid_visible = [this] { return mesh_grid_visible_; },
        .set_mesh_grid_visible =
            [this](bool value) {
              return set_preference(mesh_grid_visible_, value);
            },
        .overlays_visible = [this] { return overlays_visible_; },
        .set_overlays_visible =
            [this](bool value) {
              return set_preference(overlays_visible_, value);
            },
        .texture_lock_enabled = [this] { return texture_lock_enabled_; },
        .set_texture_lock_enabled = [this](bool value) {
          return set_preference(texture_lock_enabled_, value);
        },
        .texture_lock_scale_enabled =
            [this] { return texture_lock_scale_enabled_; },
        .set_texture_lock_scale_enabled = [this](bool value) {
          return set_preference(texture_lock_scale_enabled_, value);
        },
        .grid_snap_enabled = [this] { return grid_snap_enabled_; },
        .set_grid_snap_enabled =
            [this](bool value) {
              return set_preference(grid_snap_enabled_, value);
            },
        .vertex_snap_enabled = [this] { return vertex_snap_enabled_; },
        .set_vertex_snap_enabled =
            [this](bool value) {
              return set_preference(vertex_snap_enabled_, value);
            },
        .rotation_snap_enabled = [this] { return rotation_snap_enabled_; },
        .set_rotation_snap_enabled =
            [this](bool value) {
              return set_preference(rotation_snap_enabled_, value);
            },
        .rotation_snap_degrees = [this] { return rotation_snap_degrees_; },
        .set_rotation_snap_degrees =
            [this](double value) {
              ++external_calls_;
              const bool changed = rotation_snap_degrees_ != value;
              rotation_snap_degrees_ = value;
              return runtime_result(changed);
            },
        .viewport_info_visible = [this] { return viewport_info_visible_; },
        .set_viewport_info_visible =
            [this](bool value) {
              return set_preference(viewport_info_visible_, value);
            },
        .navigation_gizmo_visible =
            [this] { return navigation_gizmo_visible_; },
        .set_navigation_gizmo_visible =
            [this](bool value) {
              return set_preference(navigation_gizmo_visible_, value);
            },
    });
  }

  void install_viewport_hooks() {
    editor().viewports().set_runtime_hooks(QuaderViewportRuntimeHooks{
        .frame_all =
            [this](ViewportPaneId pane) {
              ++external_calls_;
              QuaderViewportApiRuntimeAccess::workspace(editor().viewports())
                  .camera(pane)
                  .focus_bounds({-10.0f, -8.0f, -6.0f},
                                {10.0f, 8.0f, 6.0f});
              return runtime_result(true, "Frame all test hook");
            },
        .frame_selection =
            [this](ViewportPaneId pane) {
              ++external_calls_;
              QuaderViewportApiRuntimeAccess::workspace(editor().viewports())
                  .camera(pane)
                  .focus_bounds({-2.0f, -1.0f, -0.5f},
                                {3.0f, 2.0f, 1.5f});
              return runtime_result(true, "Frame selection test hook");
            },
        .focus_origin =
            [this](ViewportPaneId pane) {
              ++external_calls_;
              QuaderViewportApiRuntimeAccess::workspace(editor().viewports())
                  .camera(pane)
                  .focus_origin(32.0f);
              return runtime_result(true, "Focus origin test hook");
            },
        .spawn_window =
            [this] {
              ++external_calls_;
              return runtime_result(true, "Spawn viewport test hook");
            },
        .capture_screenshot =
            [this](ViewportPaneId, const std::filesystem::path &) {
              ++external_calls_;
              return runtime_result(true, "Screenshot test hook");
            },
    });
  }

  void install_project_hooks() {
    const std::filesystem::path create_root =
        temporary_.make_empty_child("ReplacementProject");
    export_path_ = temporary_.path() / "selection.obj";
    editor().project().set_runtime_hooks(QuaderProjectRuntimeHooks{
        .choose_create_root =
            [this, create_root]() -> std::optional<std::filesystem::path> {
              ++external_calls_;
              return create_root;
            },
        .choose_open_manifest_path =
            [this]() -> std::optional<std::filesystem::path> {
              ++external_calls_;
              return project_open_path_;
            },
        .choose_open_path =
            [this]() -> std::optional<std::filesystem::path> {
              ++external_calls_;
              return scene_open_path_;
            },
        .choose_export_obj_path =
            [this]() -> std::optional<std::filesystem::path> {
              ++external_calls_;
              return export_path_;
            },
        .confirm_dirty_project_replacement =
            [this](std::string_view) {
              ++external_calls_;
              return QuaderDirtyProjectDecision::Discard;
            },
        .prepare_material_document_close =
            [this](std::string_view) {
              ++external_calls_;
              return true;
            },
    });
  }

  void install_scene_document_hooks() {
    if (project_ != nullptr) {
      scene_save_path_ = project_root_ / "Scenes" / "ContractSaved.qdr";
    } else {
      scene_save_path_ = temporary_.path() / "ContractSaved.qdr";
    }
    editor().scene_documents().set_runtime_hooks(
        QuaderSceneDocumentsRuntimeHooks{
            .choose_open_scene_path =
                [this]() -> std::optional<std::filesystem::path> {
                  ++external_calls_;
                  return scene_open_path_;
                },
            .choose_save_scene_as_path =
                [this](SceneSessionId)
                    -> std::optional<std::filesystem::path> {
                  ++external_calls_;
                  return scene_save_path_;
                },
            .confirm_dirty_scene =
                [this](const QuaderSceneDocumentInfo &, std::string_view) {
                  ++external_calls_;
                  return QuaderDirtySceneDecision::Discard;
                },
        });
  }

  QuaderApiVoidResult set_preference(bool &storage, bool value) {
    ++external_calls_;
    const bool changed = storage != value;
    storage = value;
    return runtime_result(changed);
  }

  void configure_fixture() {
    switch (contract_.fixture) {
    case CanonicalCommandFixture::EmptyEditor:
    case CanonicalCommandFixture::ProjectWithScene:
    case CanonicalCommandFixture::Viewport:
    case CanonicalCommandFixture::ExternalRuntime:
      break;
    case CanonicalCommandFixture::TwoScenes:
      require_value(editor().scene_documents().new_scene(),
                    "Create second canonical scene");
      break;
    case CanonicalCommandFixture::ObjectSelection:
      if (command_id_ ==
          quader_command::kModelingCreateOuterHull.string()) {
        const QuaderModelingObjectRef object = create_polygon_object(
            make_single_quad(), "Open Outer Hull Surface");
        require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
                   "Enter Object mode for outer-hull surface");
        require_ok(editor().selection().select_object(object),
                   "Select canonical outer-hull surface");
      } else {
        (void)create_selected_box("Selected Object");
      }
      break;
    case CanonicalCommandFixture::ObjectPairSelection:
      configure_object_pair();
      break;
    case CanonicalCommandFixture::ObjectSetSelection:
      configure_object_set();
      break;
    case CanonicalCommandFixture::IdenticalObjectsOneSelected:
      configure_identical_objects();
      break;
    case CanonicalCommandFixture::NgonObjectSelection:
      configure_ngon();
      break;
    case CanonicalCommandFixture::GroupSelection:
      configure_group();
      break;
    case CanonicalCommandFixture::InstanceSelection:
      configure_instance(false);
      break;
    case CanonicalCommandFixture::InstanceEdit:
      configure_instance(true);
      break;
    case CanonicalCommandFixture::SingleVertexSelection:
      configure_vertex_selection(1u);
      break;
    case CanonicalCommandFixture::VertexPairSelection:
      configure_vertex_selection(2u);
      break;
    case CanonicalCommandFixture::VertexSetSelection:
      configure_vertex_selection(3u);
      break;
    case CanonicalCommandFixture::SingleEdgeSelection:
      configure_edge_selection(false);
      break;
    case CanonicalCommandFixture::EdgePairSelection:
      configure_edge_selection(true);
      break;
    case CanonicalCommandFixture::BoundaryLoopSelection:
      configure_boundary_loop();
      break;
    case CanonicalCommandFixture::SingleFaceSelection:
      configure_face_selection(false);
      break;
    case CanonicalCommandFixture::FacePairSelection:
      configure_face_selection(true);
      break;
    case CanonicalCommandFixture::TextureFaceSelection:
      configure_texture_face();
      break;
    case CanonicalCommandFixture::UvPeelEdgeSelection:
      configure_uv_peel_edge();
      break;
    case CanonicalCommandFixture::CurveSelection:
      configure_curve_selection();
      break;
    case CanonicalCommandFixture::HiddenObject:
      configure_hidden_object();
      break;
    case CanonicalCommandFixture::LayeredObjectSelection:
      configure_layered_object();
      break;
    case CanonicalCommandFixture::ClipboardObject:
      configure_clipboard();
      break;
    case CanonicalCommandFixture::UndoHistory:
      (void)create_selected_box("Undo History Object");
      break;
    case CanonicalCommandFixture::RedoHistory:
      (void)create_selected_box("Redo History Object");
      require_ok(editor().history().undo(), "Prepare redo history");
      break;
    case CanonicalCommandFixture::RepeatHistory:
      configure_repeat_history();
      break;
    case CanonicalCommandFixture::ModelingPreview:
      configure_modeling_preview();
      break;
    case CanonicalCommandFixture::PrimitivePreview:
      configure_primitive_preview();
      break;
    }
  }

  QuaderModelingObjectRef create_box(std::string name) {
    return require_value(
        editor().modeling().create_box(
            QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                         .max = {1.0f, 1.0f, 1.0f}},
            std::move(name)),
        "Create canonical box");
  }

  QuaderModelingObjectRef create_selected_box(std::string name) {
    QuaderModelingObjectRef box = create_box(std::move(name));
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
               "Enter canonical Object selection mode");
    require_ok(editor().selection().select_object(box),
               "Select canonical box");
    return box;
  }

  QuaderModelingObjectRef create_polygon_object(PolygonFixture polygon,
                                                 std::string name) {
    return require_value(
        editor().modeling().create_mesh(
            QuaderPolygonDocumentInternalAccess::from_backend(
                std::move(polygon.document)),
            QuaderCreateMeshArgs{.name = std::move(name)}),
        "Create canonical polygon mesh");
  }

  void configure_object_pair() {
    const QuaderModelingObjectRef first = create_box("Object Pair A");
    const QuaderModelingObjectRef second = create_box("Object Pair B");
    require_ok(editor().scene().set_transform(
                   first.entity,
                   QuaderTransform{.position = {-2.0f, 1.0f, 0.5f}}),
               "Position first object-pair mesh");
    require_ok(editor().scene().set_transform(
                   second.entity,
                   QuaderTransform{.position = {4.5f, -1.5f, 3.0f}}),
               "Position second object-pair mesh");
    if (command_id_ ==
        quader_command::kModelingCopyPivotFromActive.string()) {
      const QuaderModelingObjectRef child =
          create_box("Copy Pivot Active Source Child");
      require_ok(editor().scene().set_transform(
                     child.entity,
                     QuaderTransform{.position = {8.0f, 0.0f, 0.0f}}),
                 "Offset Copy Pivot source child");
      require_ok(editor().scene().reparent(child.entity, second.entity, false),
                 "Parent Copy Pivot source child");
    }
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
               "Enter Object mode for object pair");
    require_ok(editor().selection().select_objects(
                   {first.entity, second.entity}, second.entity),
               "Select canonical object pair");
  }

  void configure_object_set() {
    const QuaderModelingObjectRef first = create_box("Object Set A");
    const QuaderModelingObjectRef second = create_box("Object Set B");
    const QuaderModelingObjectRef third = create_box("Object Set C");
    require_ok(editor().scene().set_transform(
                   first.entity,
                   QuaderTransform{.position = {-4.0f, 1.5f, -2.0f},
                                   .scale = {1.0f, 0.75f, 1.25f}}),
               "Position first object-set mesh");
    require_ok(editor().scene().set_transform(
                   second.entity,
                   QuaderTransform{.position = {1.0f, -3.0f, 4.0f},
                                   .scale = {0.6f, 1.4f, 0.9f}}),
               "Position second object-set mesh");
    require_ok(editor().scene().set_transform(
                   third.entity,
                   QuaderTransform{.position = {9.0f, 2.0f, -5.0f},
                                   .scale = {1.8f, 0.8f, 1.1f}}),
               "Position third object-set mesh");
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
               "Enter Object mode for object set");
    require_ok(editor().selection().select_objects(
                   {first.entity, second.entity, third.entity},
                   second.entity),
               "Select canonical object set");
  }

  void configure_identical_objects() {
    const QuaderModelingObjectRef selected = create_box("Similar Source");
    const QuaderModelingObjectRef peer = create_box("Similar Peer");
    require_ok(editor().scene().set_transform(
                   peer.entity,
                   QuaderTransform{.position = {6.0f, 2.0f, -3.0f},
                                   .rotation_euler = {0.2f, 0.5f, -0.1f},
                                   .scale = {1.5f, 0.8f, 2.0f}}),
               "Transform exact-geometry peer");
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
               "Enter Object mode for similar meshes");
    require_ok(editor().selection().select_object(selected),
               "Select only exact-geometry source");
  }

  void configure_ngon() {
    PolygonFixture polygon = make_pentagon();
    const QuaderModelingObjectRef object =
        create_polygon_object(std::move(polygon), "Pentagon");
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
               "Enter Object mode for N-Gon selection");
    require_ok(editor().selection().select_object(object),
               "Select canonical N-Gon object");
  }

  void configure_group() {
    const QuaderModelingObjectRef first = create_box("Group Member A");
    const QuaderModelingObjectRef second = create_box("Group Member B");
    require_ok(editor().selection().select_objects(
                   {first.entity, second.entity}, second.entity),
               "Select canonical group members");
    const QuaderEntityRef group =
        require_value(editor().scene().group_selection(),
                      "Create canonical group");
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Group),
               "Enter Group selection mode");
    require_ok(editor().selection().select_object(group.id),
               "Select canonical group");
  }

  void configure_instance(bool enter_edit) {
    const QuaderModelingObjectRef member =
        create_selected_box("Instance Member");
    require_ok(editor().modeling().create_instance(),
               "Create canonical instance");
    EntityId occurrence = kInvalidEntityId;
    for (const QuaderEntitySnapshot &snapshot : editor().query().entities()) {
      if (snapshot.selected && snapshot.kind == QuaderEntityKind::Instance) {
        occurrence = snapshot.id;
        break;
      }
    }
    if (occurrence == kInvalidEntityId) {
      setup_failure("Create canonical instance",
                    "selected occurrence was not projected");
    }
    if (!enter_edit) {
      return;
    }
    require_ok(editor().modeling().enter_instance_edit(occurrence),
               "Enter canonical instance edit scope");
    require_ok(editor().selection().select_object(member),
               "Select canonical instance definition member");
  }

  void configure_vertex_selection(std::size_t count) {
    PolygonFixture polygon;
    std::vector<quader_poly::ElementId> selected_vertices;
    std::string name = "Vertex Selection Plane";
    const bool merge_vertices =
        command_id_ ==
            quader_command::kModelingMergeVerticesToActive.string() ||
        command_id_ ==
            quader_command::kModelingMergeVerticesToCenter.string() ||
        command_id_ == quader_command::kModelingMergeVertices.string() ||
        command_id_ ==
            quader_command::kModelingMergeVerticesAtCenter.string();
    const bool radial_vertices =
        command_id_ == quader_command::kModelingRadialAlign.string() ||
        command_id_ ==
            quader_command::kModelingRadialAlignVertices.string();

    if (count == 1u &&
        command_id_ == quader_command::kModelingDissolveVertices.string()) {
      polygon = make_dissolvable_vertex_face();
      selected_vertices = {polygon.vertices[1]};
      name = "Dissolvable Vertex Face";
    } else if (count == 2u && merge_vertices) {
      polygon = make_single_quad();
      selected_vertices = {polygon.vertices[0], polygon.vertices[1]};
      name = "Mergeable Vertex Pair";
    } else if (count == 2u &&
               command_id_ ==
                   quader_command::kModelingRemoveDoubles.string()) {
      polygon = make_near_duplicate_triangles(0.0f);
      selected_vertices = {polygon.vertices[2], polygon.vertices[3]};
      name = "Exact Duplicate Vertex Pair";
    } else if (count == 2u &&
               command_id_ ==
                   quader_command::kModelingMergeByDistance.string()) {
      polygon = make_near_duplicate_triangles(0.1f);
      selected_vertices = {polygon.vertices[2], polygon.vertices[3]};
      name = "Near Duplicate Vertex Pair";
    } else if (count >= 3u && radial_vertices) {
      polygon = make_irregular_radial_quad();
      selected_vertices = polygon.vertices;
      name = "Irregular Radial Vertex Ring";
    } else {
      polygon = make_two_quad_plane();
      if (count == 1u) {
        selected_vertices = {polygon.vertices[1]};
      } else if (count == 2u) {
        selected_vertices = {polygon.vertices[0], polygon.vertices[4]};
      } else {
        selected_vertices = {polygon.vertices[0], polygon.vertices[2],
                             polygon.vertices[3]};
      }
    }
    const QuaderModelingObjectRef object =
        create_polygon_object(std::move(polygon), std::move(name));
    if (selected_vertices.size() == 1u) {
      require_ok(editor().selection().select_vertex(
                     object, api_element(selected_vertices.front())),
                 "Select canonical single vertex");
      return;
    }
    for (std::size_t index = 0; index < selected_vertices.size(); ++index) {
      require_ok(editor().selection().select_vertex(
                     object, api_element(selected_vertices[index]),
                     index == 0u ? QuaderSelectionEdit::Replace
                                 : QuaderSelectionEdit::Add),
                 "Select canonical vertex set");
    }
  }

  void configure_edge_selection(bool pair) {
    const bool radial_edges =
        pair && command_id_ ==
                    quader_command::kModelingRadialAlignEdges.string();
    PolygonFixture polygon = radial_edges ? make_irregular_radial_quad()
                                          : make_two_quad_plane();
    const std::vector<quader_poly::ElementId> vertices = polygon.vertices;
    const QuaderModelingObjectRef object =
        create_polygon_object(std::move(polygon), "Edge Selection Plane");
    if (!pair) {
      const bool loop_seed =
          command_id_ == quader_command::kSelectEdgeLoops.string();
      require_ok(editor().selection().select_edge(
                     object,
                     api_element(loop_seed ? vertices[0] : vertices[1]),
                     api_element(loop_seed ? vertices[1] : vertices[4])),
                 loop_seed ? "Select canonical boundary loop seed"
                           : "Select canonical manifold edge");
      return;
    }
    if (radial_edges) {
      for (std::size_t index = 0; index < vertices.size(); ++index) {
        require_ok(editor().selection().select_edge(
                       object, api_element(vertices[index]),
                       api_element(vertices[(index + 1u) % vertices.size()]),
                       index == 0u ? QuaderSelectionEdit::Replace
                                   : QuaderSelectionEdit::Add),
                   "Select canonical irregular radial edge ring");
      }
      return;
    }
    require_ok(editor().selection().select_edge(
                   object, api_element(vertices[0]),
                   api_element(vertices[3])),
               "Select first canonical edge");
    require_ok(editor().selection().select_edge(
                   object, api_element(vertices[2]), api_element(vertices[5]),
                   QuaderSelectionEdit::Add),
               "Select active canonical edge");
  }

  void configure_boundary_loop() {
    PolygonFixture polygon = make_open_quad_boundary();
    const std::vector<quader_poly::ElementId> vertices = polygon.vertices;
    const QuaderModelingObjectRef object =
        create_polygon_object(std::move(polygon), "Open Quad Boundary");
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      const std::size_t next = (index + 1u) % vertices.size();
      require_ok(editor().selection().select_edge(
                     object, api_element(vertices[index]),
                     api_element(vertices[next]),
                     index == 0u ? QuaderSelectionEdit::Replace
                                 : QuaderSelectionEdit::Add),
                 "Select canonical open boundary edge");
    }
  }

  void configure_face_selection(bool pair) {
    const bool bridge_faces =
        pair &&
        (command_id_ == quader_command::kModelingBridgeFaces.string() ||
         command_id_ ==
             quader_command::kModelingInterpolatedBridgeFaces.string());
    const bool isolated_face =
        !pair &&
        (command_id_ == quader_command::kModelingThickenFaces.string() ||
         command_id_ == quader_command::kModelingSliceQuad.string());
    const bool radial_face =
        !pair && command_id_ ==
                     quader_command::kModelingRadialAlignFaces.string();
    PolygonFixture polygon = bridge_faces   ? make_parallel_quad_pair()
                             : radial_face  ? make_irregular_radial_quad()
                             : isolated_face ? make_single_quad()
                                             : make_two_quad_plane();
    const std::vector<quader_poly::ElementId> faces = polygon.faces;
    const QuaderModelingObjectRef object =
        create_polygon_object(std::move(polygon), "Face Selection Plane");
    if (command_id_ ==
        quader_command::kSelectFacesWithSameMaterial.string()) {
      const QuaderAssetRef material = require_value(
          editor().assets().add_material(
              QuaderMaterialArgs{.name = "Canonical Shared Face Material"}),
          "Create canonical shared face material");
      require_ok(editor().assets().assign_material_to_entity(object.entity,
                                                              material.id),
                 "Assign canonical shared face material");
    }
    if (!pair) {
      require_ok(editor().selection().select_face(object,
                                                  api_element(faces[0])),
                 "Select canonical single face");
      return;
    }
    require_ok(editor().selection().select_faces(
                   object, {api_element(faces[0]), api_element(faces[1])}),
               "Select canonical face pair");
  }

  void configure_texture_face() {
    PolygonFixture polygon = make_texture_face();
    const quader_poly::ElementId face = polygon.faces.front();
    const QuaderModelingObjectRef object =
        create_polygon_object(std::move(polygon), "Texture Action Face");
    require_ok(editor().selection().select_face(object, api_element(face)),
               "Select canonical texture face");
  }

  void configure_uv_peel_edge() {
    PolygonFixture polygon = make_texture_face();
    const auto first = polygon.vertices[0];
    const auto second = polygon.vertices[1];
    const QuaderModelingObjectRef object =
        create_polygon_object(std::move(polygon), "UV Peel Open Quad");
    require_ok(editor().selection().select_edge(
                   object, api_element(first), api_element(second)),
               "Select canonical UV Peel guide edge");
  }

  void configure_curve_selection() {
    const QuaderModelingObjectRef curve = require_value(
        editor().modeling().create_curve({
            .points = {{.position = {0.0F, 0.0F, 0.0F},
                        .outgoing_handle = {1.0F, 0.0F, 0.0F}},
                       {.position = {4.0F, 1.0F, 0.0F},
                        .incoming_handle = {-1.0F, 0.0F, 0.0F}}},
            .name = "Path Array Curve",
        }),
        "Create canonical Path Array curve");
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
               "Enter Object mode for Path Array curve");
    require_ok(editor().selection().select_object(curve),
               "Select canonical Path Array curve");
  }

  void configure_hidden_object() {
    const QuaderModelingObjectRef object = create_box("Hidden Object");
    require_ok(editor().scene().set_visible(object.entity, false),
               "Hide canonical object");
    require_ok(editor().selection().set_mode(QuaderSelectionMode::Object),
               "Enter Object mode for hidden object fixture");
  }

  void configure_layered_object() {
    (void)create_selected_box("Layered Object");
    (void)require_value(editor().layers().create_from_selection(
                            "Command Behavior Layer"),
                        "Create canonical layer from selection");
  }

  void configure_clipboard() {
    (void)create_selected_box("Clipboard Source");
    require_ok(editor().modeling().copy_selection(),
               "Seed canonical modeling clipboard");
  }

  void configure_repeat_history() {
    (void)create_selected_box("Repeatable Object");
    require_ok(editor().modeling().execute_tool("invert_mesh"),
               "Record canonical repeatable operation");
  }

  void configure_modeling_preview() {
    (void)create_selected_box("Preview Object");
    require_ok(editor().modeling().begin_preview("half_cut"),
               "Begin canonical modeling preview");
  }

  void configure_primitive_preview() {
    require_ok(editor().tools().activate(ViewportToolId::CreatePrimitive),
               "Activate canonical primitive tool");
    require_command_ok(native_scene().primitive_creation().accept_preview(
                           valid_primitive_preview(), "Preview Box"),
                       "Accept canonical primitive preview");
  }

  void configure_command_specific_state() {
    if (contract_.fixture == CanonicalCommandFixture::Viewport) {
      if (command_id_ == quader_command::kViewportGridReset.string()) {
        require_ok(editor().viewports().set_grid_preset(9),
                   "Move grid preset away from reset target");
      } else if (command_id_ ==
                     quader_command::kViewportGridIncrement.string() ||
                 command_id_ ==
                     quader_command::kViewportGridDecrement.string()) {
        require_ok(editor().viewports().set_grid_preset(5),
                   "Place grid preset inside increment/decrement bounds");
      }
    }
    if (contract_.fixture != CanonicalCommandFixture::ExternalRuntime) {
      return;
    }

    if (command_id_ == quader_command::kProjectOpen.string()) {
      prepare_project_open_source();
      return;
    }
    if (command_id_ == quader_command::kSceneDocumentOpen.string()) {
      prepare_scene_open_source();
      return;
    }
    if (command_id_ == quader_command::kSceneDocumentSave.string() ||
        command_id_ == quader_command::kSceneDocumentSaveAs.string() ||
        command_id_ == quader_command::kSceneDocumentSaveAll.string()) {
      (void)create_selected_box("Dirty Scene Save Object");
      return;
    }
    if (command_id_ ==
            quader_command::kProjectExportSelectionObj.string() ||
        command_id_ == quader_command::kViewportFrameSelection.string() ||
        command_id_ ==
            quader_command::kViewportToggleLocalVisibility.string()
#ifndef NDEBUG
        || command_id_ == quader_command::kDevCopySelectionJson.string()
#endif
    ) {
      (void)create_selected_box("External Runtime Selected Object");
    }
  }

  void prepare_project_open_source() {
    const std::filesystem::path source_root =
        temporary_.make_empty_child("OpenSourceProject");
    source_project_ = std::make_unique<ProjectSession>();
    require_project_ok(source_project_->create(source_root),
                       "Create project-open source workspace");
    source_editor_ = std::make_unique<QuaderEditorApi>(*source_project_);
    require_ok(source_editor_->commands().set_keybinding_storage_path(
                   temporary_.path() / "source-keybindings.json"),
               "Isolate project-open source keybindings");
    const std::optional<SceneSessionId> active =
        source_editor_->scene_documents().active_scene_id();
    if (!active.has_value()) {
      setup_failure("Prepare project-open source", "source has no scene");
    }
    require_ok(source_editor_->scene_documents().save_as(
                   *active, project_path("Scenes/Opened.qdr")),
               "Persist project-open source scene");
    project_open_path_ = source_project_->manifest_path();
  }

  void prepare_scene_open_source() {
    const std::optional<SceneSessionId> source_id =
        editor().scene_documents().active_scene_id();
    if (!source_id.has_value()) {
      setup_failure("Prepare scene-open source", "project has no scene");
    }
    (void)create_selected_box("Scene Open Source Object");
    const ProjectRelativePath source_path =
        project_path("Scenes/OpenSource.qdr");
    require_ok(editor().scene_documents().save_as(*source_id, source_path),
               "Persist scene-open source");
    require_value(editor().scene_documents().new_scene(),
                  "Create scene beside open source");
    require_ok(editor().scene_documents().close(*source_id),
               "Close scene-open source tab");
    scene_open_path_ = project_root_ / source_path.native_path();
  }

  void require_enabled_command() const {
    const std::optional<QuaderCommandDescriptor> descriptor =
        editor().commands().descriptor(command_id_);
    if (!descriptor.has_value()) {
      setup_failure("Configure command behavior fixture",
                    "command is not registered: " + command_id_);
    }
    if (!descriptor->enabled) {
      setup_failure("Configure command behavior fixture",
                    "command remains disabled: " + command_id_);
    }
  }

  CommandBehaviorContract contract_;
  std::string command_id_;
  ScopedFixtureRoot temporary_;
  std::filesystem::path project_root_;
  std::filesystem::path project_open_path_;
  std::filesystem::path scene_open_path_;
  std::filesystem::path scene_save_path_;
  std::filesystem::path export_path_;
  std::unique_ptr<SceneSession> standalone_scene_;
  std::unique_ptr<ProjectSession> project_;
  std::unique_ptr<ProjectSession> source_project_;
  std::unique_ptr<QuaderEditorApi> editor_;
  std::unique_ptr<QuaderEditorApi> source_editor_;
  std::uint64_t external_calls_ = 0u;
  std::string clipboard_text_;
  std::string locale_ = "en";
  bool vsync_enabled_ = true;
  SessionPreferences session_preferences_;
  bool snapping_enabled_ = true;
  bool backface_selection_enabled_ = false;
  bool texture_lock_enabled_ = true;
  bool texture_lock_scale_enabled_ = false;
  bool grid_visible_ = true;
  bool gizmos_visible_ = true;
  bool mesh_grid_visible_ = true;
  bool overlays_visible_ = true;
  bool grid_snap_enabled_ = true;
  bool vertex_snap_enabled_ = true;
  bool rotation_snap_enabled_ = true;
  bool viewport_info_visible_ = true;
  bool navigation_gizmo_visible_ = true;
  double rotation_snap_degrees_ = 15.0;
};

std::unique_ptr<CommandBehaviorFixture>
CommandBehaviorFixture::create(const CommandBehaviorContract &contract) {
  return std::unique_ptr<CommandBehaviorFixture>(new CommandBehaviorFixture(
      std::make_unique<CommandBehaviorFixture::Impl>(contract)));
}

CommandBehaviorFixture::CommandBehaviorFixture(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

CommandBehaviorFixture::~CommandBehaviorFixture() = default;

QuaderEditorApi &CommandBehaviorFixture::editor() { return impl_->editor(); }

const QuaderEditorApi &CommandBehaviorFixture::editor() const {
  return impl_->editor();
}

SceneSession &CommandBehaviorFixture::native_scene() {
  return impl_->native_scene();
}

const SceneSession &CommandBehaviorFixture::native_scene() const {
  return impl_->native_scene();
}

std::uint64_t CommandBehaviorFixture::external_call_count() const {
  return impl_->external_call_count();
}

} // namespace quader::test
