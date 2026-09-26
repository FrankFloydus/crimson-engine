////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using quader::test::guardrail::contains_any;
using quader::test::guardrail::files_with_extension;
using quader::test::guardrail::lines_containing;
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_file;
using quader::test::guardrail::source_files_under;
using quader::test::guardrail::source_root;
using quader::test::guardrail::source_slice;
using quader::test::guardrail::split_lines;
using quader::test::guardrail::starts_with;

std::vector<std::filesystem::path>
doc_and_example_files_under(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  for (const std::string_view extension : {
           std::string_view{".cpp"},
           std::string_view{".lua"},
           std::string_view{".md"},
           std::string_view{".txt"},
       }) {
    std::vector<std::filesystem::path> with_extension =
        files_with_extension(root, extension);
    files.insert(files.end(), with_extension.begin(), with_extension.end());
  }
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}

std::vector<std::filesystem::path> public_doc_and_example_files() {
  std::vector<std::filesystem::path> files =
      doc_and_example_files_under(source_root() / "examples");
  files.erase(std::remove_if(files.begin(), files.end(),
                             [](const std::filesystem::path &file) {
                               return file.filename() == "AGENTS.md";
                             }),
              files.end());
  return files;
}

bool is_internal_api_header(const std::filesystem::path &path) {
  const std::string relative = normalized_relative(path);
  return relative == "src/api/quader_api_mutation_runner.hpp" ||
         relative == "src/api/quader_api_status_internal.hpp" ||
         relative == "src/api/quader_settings_presentation_internal.hpp" ||
         relative == "src/api/quader_api_session_internal_access.hpp" ||
         relative == "src/api/quader_events_api_internal_access.hpp" ||
         relative == "src/api/quader_project_api_internal_access.hpp" ||
         relative ==
             "src/api/quader_project_assets_api_internal_access.hpp" ||
         relative ==
             "src/api/quader_standard_material_documents_api_internal_access.hpp" ||
         relative ==
             "src/api/quader_scene_documents_api_internal_access.hpp" ||
         relative == "src/api/quader_api_type_conversions.hpp" ||
         relative == "src/api/quader_polygon_document_internal_access.hpp" ||
         relative ==
             "src/editor/viewport/quader_viewport_api_runtime_access.hpp";
}

std::vector<std::filesystem::path> public_api_header_files() {
  std::vector<std::filesystem::path> headers;
  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "src/api", ".hpp")) {
    if (!is_internal_api_header(file)) {
      headers.push_back(file);
    }
  }
  return headers;
}

bool allowed_raw_modeling_mutation(std::string_view relative,
                                   std::string_view line) {
  (void)relative;
  (void)line;
  return false;
}

bool is_modeling_mesh_sync_owner(std::string_view relative) {
  return relative ==
         "src/editor/modeling/services/modeling_mesh_sync_service.cpp";
}

bool is_identifier_character(char value) {
  return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

bool contains_identifier_token(std::string_view source,
                               std::string_view token) {
  std::size_t position = source.find(token);
  while (position != std::string_view::npos) {
    const bool left_boundary =
        position == 0 || !is_identifier_character(source[position - 1]);
    const std::size_t end = position + token.size();
    const bool right_boundary =
        end >= source.size() || !is_identifier_character(source[end]);
    if (left_boundary && right_boundary) {
      return true;
    }
    position = source.find(token, position + 1);
  }
  return false;
}

bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

/**
 * Captures one typed command constant declaration and its stable string id.
 */
struct QuaderCommandConstant {
  std::string name;
  std::string id;
};

std::vector<QuaderCommandConstant>
quader_command_constants(const std::string &source) {
  std::vector<QuaderCommandConstant> result;
  constexpr std::string_view kMarker = "inline constexpr QuaderCommandId ";
  for (std::size_t marker_position = source.find(kMarker);
       marker_position != std::string::npos;
       marker_position =
           source.find(kMarker, marker_position + kMarker.size())) {
    const std::size_t name_begin = marker_position + kMarker.size();
    const std::size_t name_end = source.find('{', name_begin);
    if (name_end == std::string::npos) {
      continue;
    }
    const std::string name =
        trim(source.substr(name_begin, name_end - name_begin));
    if (name.find('[') != std::string::npos) {
      continue;
    }
    const std::size_t value_begin = source.find('"', name_end);
    const std::size_t value_end = source.find('"', value_begin + 1u);
    if (value_begin == std::string::npos ||
        value_end == std::string::npos) {
      continue;
    }
    result.push_back({name,
                      source.substr(value_begin + 1u,
                                    value_end - value_begin - 1u)});
  }
  return result;
}

std::unordered_map<std::string, std::string>
quader_command_ids_by_constant_name(const std::string &source) {
  std::unordered_map<std::string, std::string> result;
  for (const QuaderCommandConstant &constant :
       quader_command_constants(source)) {
    result.emplace(constant.name, constant.id);
  }
  return result;
}

bool is_public_behavior_test_name(std::string_view filename) {
  return ends_with(filename, "_api_tests.cpp") ||
         ends_with(filename, "_public_api_tests.cpp") ||
         (filename.find("_behavior") != std::string_view::npos &&
          ends_with(filename, "_tests.cpp"));
}

bool is_whitelisted_domain_api_test(std::string_view relative) {
  return relative == "tests/modeling_domain/qdr_document_model_api_tests.cpp";
}

std::vector<std::filesystem::path> public_behavior_test_files() {
  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "tests", ".cpp")) {
    const std::string relative = normalized_relative(file);
    if (is_public_behavior_test_name(file.filename().generic_string()) &&
        !is_whitelisted_domain_api_test(relative)) {
      files.push_back(file);
    }
  }
  return files;
}

std::vector<std::filesystem::path> public_cpp_example_files() {
  std::vector<std::filesystem::path> examples;
  for (const std::filesystem::path &file : public_doc_and_example_files()) {
    if (file.extension() == ".cpp" &&
        normalized_relative(file).find("examples/") == 0) {
      examples.push_back(file);
    }
  }
  return examples;
}

bool is_documentation_only_example(const std::string &source) {
  return source.find("QUADER_DOCUMENTATION_ONLY_EXAMPLE") !=
             std::string::npos ||
         source.find("documentation-only") != std::string::npos;
}

bool allowed_public_behavior_harness_use(std::string_view relative,
                                         std::string_view line) {
  if (relative == "tests/face_texture_mirror_api_tests.cpp") {
    // BehaviorScenario snapshots omit authored corner UVs, normals and
    // occurrence sampling. This fixture inspects their published mesh and
    // injects publication failures to verify exact rollback. Its one native
    // sampling setter establishes the occurrence fixture; all material/UV,
    // selection and history actions still enter through QuaderEditorApi.
    const std::string trimmed = trim(std::string(line));
    const std::array<std::string_view, 8> fixture_access{
        "#include \"api/quader_api_session_internal_access.hpp\"",
        "#include \"editor/modeling/services/modeling_session_service.hpp\"",
        "SceneSession session_;",
        "QuaderEditorApi editor_{QuaderApiSessionInternalAccess::from_scene_session(session_)};",
        "auto mesh = session_.modeling().compile_object_mesh(object.modeling_object_id);",
        "ASSERT_TRUE(session_.scene().set_texture_mapping(object.entity,",
        "session_.modeling().set_modeling_sdk_projection_failure_for_tests(point);",
        "session_.modeling().set_modeling_sdk_projection_failure_for_tests(",
    };
    return std::find(fixture_access.begin(), fixture_access.end(), trimmed) !=
           fixture_access.end();
  }
  if (relative != "tests/face_texture_canvas_api_tests.cpp") {
    return false;
  }
  // The canvas API fixture owns a standalone session and observes publication
  // counters/failures. Editing, selection and history still use the public API.
  const std::string trimmed = trim(std::string(line));
  const std::array<std::string_view, 9> fixture_access{
      "#include \"api/quader_api_session_internal_access.hpp\"",
      "#include \"editor/modeling/services/modeling_session_service.hpp\"",
      "SceneSession session;",
      "QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};",
      "harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(point);",
      "harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(ModelingSdkProjectionFailurePoint::None);",
      "harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(",
      "const auto before = harness.session.modeling().modeling_sdk_projection_stats_for_tests();",
      "const auto after = harness.session.modeling().modeling_sdk_projection_stats_for_tests();",
  };
  return std::find(fixture_access.begin(), fixture_access.end(), trimmed) !=
         fixture_access.end();
}

std::vector<std::string_view> ui_mutating_owner_api_tokens() {
  return {
      "context.document.create_",
      "context.document.destroy_",
      "context.document.remove_",
      "context.document.rename_",
      "context.document.set_",
      "context.document.clear_",
      "context.document.select_",
      "ui_context.document.create_",
      "ui_context.document.destroy_",
      "ui_context.document.remove_",
      "ui_context.document.rename_",
      "ui_context.document.set_",
      "ui_context.document.clear_",
      "ui_context.document.select_",
      "document.create_",
      "document.destroy_",
      "document.remove_",
      "document.rename_",
      "document.set_",
      "document.clear_",
      "document.select_",

      "context.assets.add_",
      "context.assets.replace_",
      "context.assets.remove_",
      "context.assets.set_",
      "context.assets.update_",
      "ui_context.assets.add_",
      "ui_context.assets.replace_",
      "ui_context.assets.remove_",
      "ui_context.assets.set_",
      "ui_context.assets.update_",
      "assets.add_",
      "assets.replace_",
      "assets.remove_",
      "assets.set_",
      "assets.update_",
      "asset_registry.add_",
      "asset_registry.replace_",
      "asset_registry.remove_",
      "asset_registry.set_",
      "asset_registry.update_",
      "registry.add_",
      "registry.replace_",
      "registry.remove_",
      "registry.set_",
      "registry.update_",

      "context.modeling.execute_",
      "context.modeling.commit_",
      "context.modeling.cancel_",
      "context.modeling.begin_",
      "context.modeling.update_",
      "context.modeling.create_",
      "context.modeling.delete_",
      "context.modeling.apply_",
      "context.modeling.select_",
      "context.modeling.sync_",
      "context.modeling.pick_",
      "context.modeling.set_",
      "ui_context.modeling.execute_",
      "ui_context.modeling.commit_",
      "ui_context.modeling.cancel_",
      "ui_context.modeling.begin_",
      "ui_context.modeling.update_",
      "ui_context.modeling.create_",
      "ui_context.modeling.delete_",
      "ui_context.modeling.apply_",
      "ui_context.modeling.select_",
      "ui_context.modeling.sync_",
      "ui_context.modeling.pick_",
      "ui_context.modeling.set_",
      "modeling.execute_",
      "modeling.commit_",
      "modeling.cancel_",
      "modeling.begin_",
      "modeling.update_",
      "modeling.create_",
      "modeling.delete_",
      "modeling.apply_",
      "modeling.select_",
      "modeling.sync_",
      "modeling.pick_",
      "modeling.set_",

      "context.selection.select_",
      "context.selection.clear_",
      "context.selection.set_",
      "ui_context.selection.select_",
      "ui_context.selection.clear_",
      "ui_context.selection.set_",
      "selection_service.select_",
      "selection_service.clear_",
      "selection_service.set_",
  };
}

bool allowed_ui_mutating_owner_api_call(std::string_view relative,
                                        std::string_view line) {
  const std::string trimmed_line = trim(std::string(line));
  if (trimmed_line.empty() || starts_with(trimmed_line, "//")) {
    return true;
  }

  // UI-owned settings, viewport workspace state, and render presentation
  // routes are intentionally absent from the forbidden owner-token list.
  (void)relative;
  return false;
}

} // namespace

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersAvoidRuntimeAndSdkInternals) {
  for (const std::filesystem::path &file : public_api_header_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_EQ(source.find("editor/modeling/runtime/session.hpp"),
              std::string::npos)
        << relative;
    EXPECT_FALSE(contains_any(source,
                              {
                                  "<filament/",
                                  "\"filament/",
                                  "<SDL",
                                  "\"SDL",
                                  "<entt/",
                                  "\"entt/",
                              }))
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, ObjectHandleQueriesRouteThroughPublicQueryApi) {
  const std::string handle =
      read_file(source_root() / "src/api/quader_object_handle.cpp");

  EXPECT_NE(handle.find("api_->query().modeling_object_for_entity(ref_.entity)"),
            std::string::npos);
  EXPECT_NE(handle.find("api_->query().compiled_object_mesh(ref_)"),
            std::string::npos);
  EXPECT_EQ(handle.find("QuaderApiSessionInternalAccess"), std::string::npos);
  EXPECT_EQ(handle.find("SceneSession"), std::string::npos);
  EXPECT_EQ(handle.find("ModelingSessionService"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicTypedCommandCatalogStaysCanonicalAndUnique) {
  const std::string command_header =
      read_file(source_root() / "src/api/quader_command_ids.hpp");
  const std::unordered_map<std::string, std::string> ids_by_constant =
      quader_command_ids_by_constant_name(command_header);
  const std::string typed_catalog = source_slice(
      command_header, "inline constexpr QuaderCommandId kTypedCommandIds[]",
      "[[nodiscard]] inline constexpr std::span");

  std::set<std::string> seen;
  for (std::string line : split_lines(typed_catalog)) {
    line = trim(std::move(line));
    if (!line.empty() && line.back() == ',') {
      line.pop_back();
    }
    const auto found = ids_by_constant.find(line);
    if (found == ids_by_constant.end()) {
      continue;
    }
    EXPECT_TRUE(seen.insert(found->second).second)
        << found->second << " from " << line;
  }

  ASSERT_TRUE(ids_by_constant.contains("kDuplicateSelection"));
  ASSERT_TRUE(ids_by_constant.contains("kModelingDuplicateMeshes"));
  EXPECT_EQ(ids_by_constant.at("kDuplicateSelection"), "edit.duplicate");
  EXPECT_EQ(ids_by_constant.at("kModelingDuplicateMeshes"),
            "modeling.duplicate_meshes");
  EXPECT_NE(ids_by_constant.at("kDuplicateSelection"),
            ids_by_constant.at("kModelingDuplicateMeshes"));

  for (std::string_view alias : {
           std::string_view{"kHistoryUndo"},
           std::string_view{"kHistoryRedo"},
           std::string_view{"kHistoryRepeatLastAction"},
           std::string_view{"kModelingDeleteSelection"},
           std::string_view{"kModelingApplyActiveMaterial"},
       }) {
    EXPECT_EQ(typed_catalog.find(alias), std::string::npos) << alias;
  }
}

TEST(ProgrammaticApiGuardrailTests,
     PublicCommandAliasesStayExplicitAndOutOfTypedCatalog) {
  /**
   * Describes one deprecated compatibility alias set allowed by the public
   * command catalog guardrail.
   */
  struct ExpectedAliasSet {
    std::string_view id;
    std::string_view canonical;
    std::vector<std::string_view> aliases;
  };

  const std::string command_header =
      read_file(source_root() / "src/api/quader_command_ids.hpp");
  const std::string typed_catalog = source_slice(
      command_header, "inline constexpr QuaderCommandId kTypedCommandIds[]",
      "[[nodiscard]] inline constexpr std::span");

  std::unordered_map<std::string, std::set<std::string>> names_by_id;
  for (const QuaderCommandConstant &constant :
       quader_command_constants(command_header)) {
    names_by_id[constant.id].insert(constant.name);
  }

  const std::vector<ExpectedAliasSet> expected_alias_sets = {
      {"edit.undo", "kEditUndo", {"kHistoryUndo"}},
      {"edit.redo", "kEditRedo", {"kHistoryRedo"}},
      {"edit.repeat_last_action",
       "kEditRepeatLastAction",
       {"kHistoryRepeatLastAction"}},
      {"edit.delete", "kDeleteSelection", {"kModelingDeleteSelection"}},
  };

  std::set<std::string> expected_duplicate_ids;
  for (const ExpectedAliasSet &alias_set : expected_alias_sets) {
    expected_duplicate_ids.insert(std::string(alias_set.id));

    std::set<std::string> expected_names = {
        std::string(alias_set.canonical)};
    for (std::string_view alias : alias_set.aliases) {
      expected_names.insert(std::string(alias));
    }

    const auto found = names_by_id.find(std::string(alias_set.id));
    ASSERT_NE(found, names_by_id.end()) << alias_set.id;
    EXPECT_TRUE(found->second == expected_names) << alias_set.id;
    EXPECT_NE(typed_catalog.find(alias_set.canonical), std::string::npos)
        << alias_set.canonical;
    for (std::string_view alias : alias_set.aliases) {
      EXPECT_EQ(typed_catalog.find(alias), std::string::npos) << alias;
    }
  }

  for (const auto &[id, names] : names_by_id) {
    if (names.size() <= 1u) {
      continue;
    }
    EXPECT_NE(expected_duplicate_ids.find(id), expected_duplicate_ids.end())
        << "new duplicate command constant for " << id
        << " must be removed or explicitly deprecated by this guardrail";
  }
}

TEST(ProgrammaticApiGuardrailTests,
     SelectionCommandsUseOnlyTheCanonicalSelectionFamily) {
  const std::string command_header =
      read_file(source_root() / "src/api/quader_command_ids.hpp");
  const std::unordered_map<std::string, std::string> ids =
      quader_command_ids_by_constant_name(command_header);

  EXPECT_EQ(ids.at("kSelectAll"), "select.all");
  EXPECT_EQ(ids.at("kSelectInverse"), "select.inverse");
  EXPECT_EQ(ids.at("kSelectToggleBackfaceSelection"),
            "select.toggle_backface_selection");
  EXPECT_EQ(command_header.find("edit.select_all"), std::string::npos);
  EXPECT_EQ(command_header.find("edit.invert_selection"), std::string::npos);
  EXPECT_EQ(command_header.find("selection.select_all"), std::string::npos);
  EXPECT_EQ(command_header.find("selection.invert"), std::string::npos);
  EXPECT_EQ(command_header.find("kEditSelectAll"), std::string::npos);
  EXPECT_EQ(command_header.find("kEditInvertSelection"), std::string::npos);
  EXPECT_EQ(command_header.find("kSelectionSelectAll"), std::string::npos);
  EXPECT_EQ(command_header.find("kSelectionInvert"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersAvoidInternalOwnerContracts) {
  for (const std::filesystem::path &file : public_api_header_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_FALSE(contains_any(source,
                              {
                                  "\"assets/",
                                  "<assets/",
                                  "\"editor/",
                                  "<editor/",
                                  "\"mesh/",
                                  "<mesh/",
                                  "\"render/",
                                  "<render/",
                                  "\"scene/",
                                  "<scene/",
                                  "\"viewport/",
                                  "<viewport/",
                                  "quader_editor_core::",
                                  "quader_host_contract::",
                                  "quader_io::",
                                  "quader_poly::",
                              }))
        << relative;

    for (std::string_view token : {
             "SceneSession",
             "EditorHistoryService",
             "SceneDocument",
             "AssetRegistry",
             "ModelingSessionService",
             "MeshAsset",
             "TextureAsset",
             "MaterialDocument",
             "MeshAssetRevision",
             "TransformComponent",
             "ViewportOverlayFrame",
             "ModelingBounds",
             "ModelingSelectionSummary",
             "ModelingToolStatus",
         }) {
      EXPECT_FALSE(contains_identifier_token(source, token))
          << relative << " exposes " << token;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicRenderApiDoesNotExposeRenderBridge) {
  const std::string header =
      read_file(source_root() / "src/api/quader_render_api.hpp");

  EXPECT_EQ(header.find("SceneRenderBridge"), std::string::npos);
  EXPECT_EQ(header.find("sync_scene("), std::string::npos);
  EXPECT_EQ(header.find("sync_tool_previews("), std::string::npos);
  EXPECT_EQ(header.find("sync_selection_overlay("), std::string::npos);
  EXPECT_NE(header.find("friend class QuaderRenderApiRuntimeAccess"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     SelectionRuntimeAccessRemainsNativeEditorOnly) {
  const std::string public_header =
      read_file(source_root() / "src/api/quader_selection_api.hpp");
  const std::string runtime_header = read_file(
      source_root() /
      "src/editor/selection/quader_selection_api_runtime_access.hpp");

  EXPECT_NE(public_header.find("friend class QuaderSelectionApiRuntimeAccess"),
            std::string::npos);
  EXPECT_EQ(public_header.find("EditorSelectionService"), std::string::npos);
  EXPECT_EQ(public_header.find("ResolvedSelection"), std::string::npos);
  EXPECT_NE(runtime_header.find("EditorSelectionService &"),
            std::string::npos);
  EXPECT_EQ(runtime_header.find("ResolvedSelection"), std::string::npos);
  EXPECT_EQ(runtime_header.find("apply_resolved_"), std::string::npos);

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/ui")) {
    const std::string source = read_file(file);
    EXPECT_EQ(source.find("QuaderSelectionApiRuntimeAccess"),
              std::string::npos)
        << normalized_relative(file);
    EXPECT_EQ(source.find(
                  "editor/selection/quader_selection_api_runtime_access.hpp"),
              std::string::npos)
        << normalized_relative(file);
  }
}

TEST(ProgrammaticApiGuardrailTests, ApiLayerUsesSharedResultStatusConversion) {
  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/api")) {
    const std::string relative = normalized_relative(file);
    if (relative == "src/api/quader_api_mutation_runner.cpp") {
      continue;
    }

    const std::string source = read_file(file);
    for (const std::string &line : lines_containing(
             source, {".ok = result.ok", ".changed = result.changed"})) {
      ADD_FAILURE() << relative
                    << ": use quader_api_status_from_result for QuaderApiResult "
                       "status copies: "
                    << line;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiValueContractHeadersAvoidInternalOwnerHeaders) {
    for (const std::string_view relative : {
                 std::string_view{"src/api/quader_api_types.hpp"},
                 std::string_view{"src/api/quader_api_snapshot.hpp"},
         }) {
      const std::string source =
          read_file(source_root() / std::string(relative));

      EXPECT_FALSE(contains_any(source,
                                {
                                    "\"editor/",
                                    "\"scene/",
                                    "\"assets/",
                                    "\"render/",
                                    "\"viewport/",
                                    "\"mesh/",
                                    "<mesh/",
                                    "quader_poly::",
                                    "quader_editor_core::",
                                    "quader_io::",
                                    "quader_host_contract::",
                                    "TransformComponent",
                                    "ModelingBounds",
                                    "ModelingSelectionSummary",
                                    "ModelingToolStatus",
                                    "ViewportOverlayFrame",
                                    "MeshAssetRevision",
                                }))
          << relative;
    }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersDoNotExposeMutableInternalOwners) {
  for (const std::filesystem::path &file : public_api_header_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    for (std::string_view forbidden : {
             "SceneDocument&",
             "AssetRegistry&",
             "ViewportWorkspaceService",
             "MeshAsset*",
             "MaterialDocument*",
             "TextureAsset*",
             "entt::registry&",
             "EditorSession&",
             "EditorCommandRegistry",
             "QuaderKeybindingService",
             "\"editor/commands/editor_command_registry.hpp\"",
             "\"editor/input/quader_keybinding_service.hpp\"",
             "\"editor/commands/document/",
         }) {
      EXPECT_EQ(source.find(forbidden), std::string::npos)
          << relative << " exposes " << forbidden;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, ReadOnlyApiPathsUseConstSceneSessionAccess) {
  const std::string internal_access = read_file(
      source_root() / "src/api/quader_api_session_internal_access.hpp");
  const std::string query_api =
      read_file(source_root() / "src/api/quader_query_api.cpp");
  const std::string render_api =
      read_file(source_root() / "src/api/quader_render_api.cpp");
  const std::string history_api =
      read_file(source_root() / "src/api/quader_history_api.cpp");

  EXPECT_NE(internal_access.find("const SceneSession &"),
            std::string::npos);
  EXPECT_NE(internal_access.find("read_scene_session("),
            std::string::npos);
  for (const std::string &source : {query_api, render_api, history_api}) {
    EXPECT_NE(source.find("read_scene_session("), std::string::npos);
    EXPECT_EQ(source.find("SceneSession &scene_session("),
              std::string::npos);
  }
  EXPECT_NE(render_api.find(".read_view()"), std::string::npos);
  EXPECT_EQ(render_api.find("build_render_scene_snapshot(session.scene, session.assets)"),
            std::string::npos);
  EXPECT_EQ(render_api.find("bridge.sync(build_render_scene_snapshot("),
            std::string::npos);
  EXPECT_NE(render_api.find("bridge.sync(session.scene, session.assets"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicCommandApiUsesViewportInputValueContracts) {
  for (const std::filesystem::path &file : public_api_header_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_EQ(source.find("\"viewport/viewport_input_router.hpp\""),
              std::string::npos)
        << relative;
    EXPECT_EQ(source.find("\"viewport/viewport_input_types.hpp\""),
              std::string::npos)
        << relative;
    EXPECT_EQ(source.find("\"editor/input/quader_input_binding.hpp\""),
              std::string::npos)
        << relative;
  }

  const std::string command_header =
      read_file(source_root() / "src/api/quader_commands_api.hpp");
  const std::string keybinding_header =
      read_file(source_root() / "src/api/quader_keybinding_types.hpp");
  const std::string tools_header =
      read_file(source_root() / "src/api/quader_tools_api.hpp");
  EXPECT_NE(command_header.find("\"api/quader_viewport_input_types.hpp\""),
            std::string::npos);
  EXPECT_NE(tools_header.find("\"api/quader_viewport_input_types.hpp\""),
            std::string::npos);
  EXPECT_NE(keybinding_header.find("\"api/quader_input_types.hpp\""),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ModelingSessionServiceDoesNotExposeSelectionSyncAuthority) {
  const std::string header =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_session_service.hpp");
  EXPECT_EQ(header.find("sync_object_selection_from_scene("), std::string::npos);
  EXPECT_EQ(header.find("sync_scene_selection_from_modeling("), std::string::npos);
  EXPECT_EQ(header.find("project_scene_selection_from_modeling("),
            std::string::npos);
  EXPECT_EQ(header.find("friend class EditorSelectionService;"),
            std::string::npos);

  const std::string source =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_session_service.cpp");
  EXPECT_EQ(source.find("scene_.set_selected("), std::string::npos);
  EXPECT_NE(source.find("SelectionProjectionService"), std::string::npos);

  const std::string editor_selection_header =
      read_file(source_root() / "src" / "editor" / "selection" /
                "editor_selection_service.hpp");
  EXPECT_EQ(editor_selection_header.find("syncObjectsFromScene("),
            std::string::npos);
  EXPECT_EQ(editor_selection_header.find("sync_objects_from_scene("),
            std::string::npos);
  EXPECT_EQ(editor_selection_header.find("syncSceneFromModeling("),
            std::string::npos);
  EXPECT_EQ(editor_selection_header.find("sync_scene_from_modeling("),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     ModelingMeshSyncServiceOwnsSceneAssetSyncMutations) {
  const std::string header = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_mesh_sync_service.hpp");
  const std::string owner = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_mesh_sync_service.cpp");

  EXPECT_NE(header.find("Owns the modeling runtime to SceneDocument / "
                        "AssetRegistry synchronization boundary"),
            std::string::npos);
  for (std::string_view required_owner_call : {
           std::string_view{".create_entity("},
           std::string_view{".destroy_entity("},
           std::string_view{".set_modeling_object("},
           std::string_view{".set_mesh("},
           std::string_view{".set_material("},
           std::string_view{".add_mesh("},
           std::string_view{".replace_mesh("},
       }) {
    EXPECT_NE(owner.find(required_owner_call), std::string::npos)
        << required_owner_call;
  }

  const std::vector<std::string_view> sync_mutations = {
      ".create_entity(",
      ".create_entity_with_id(",
      ".destroy_entity(",
      ".set_parent(",
      ".set_transform(",
      ".set_transforms(",
      ".set_visible(",
      ".set_locked(",
      ".set_modeling_object(",
      ".set_mesh(",
      ".set_material(",
      ".clear_material(",
      ".add_mesh(",
      ".replace_mesh(",
      ".update_mesh(",
      ".add_material(",
      ".replace_material(",
      ".update_material(",
      ".add_texture(",
      ".replace_texture(",
      ".update_texture(",
      ".set_material_texture(",
  };

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/editor/modeling")) {
    const std::string relative = normalized_relative(file);
    const std::string source = read_file(file);
    for (const std::string &line : lines_containing(source, sync_mutations)) {
      EXPECT_TRUE(is_modeling_mesh_sync_owner(relative))
          << relative << ": route scene/asset sync mutation through "
          << "ModelingMeshSyncService: " << line;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, ApiImplementationDoesNotBypassCommandExecutionBoundary) {
  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/api")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    for (std::string_view forbidden : {
             "execute_instant_operation",
             "execute_context_action",
             "commit_active_operation_preview",
             "cancel_active_operation_preview",
         }) {
      EXPECT_EQ(source.find(forbidden), std::string::npos)
          << relative << " calls " << forbidden;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, ApiImplementationDoesNotIncludeConcreteDocumentCommands) {
  const std::vector<std::string_view> concrete_document_command_headers = {
      "\"editor/commands/document/apply_entity_transform_command.hpp\"",
      "\"editor/commands/document/batch_document_command.hpp\"",
      "\"editor/commands/document/create_entity_command.hpp\"",
      "\"editor/commands/document/destroy_entity_command.hpp\"",
      "\"editor/commands/document/duplicate_entity_command.hpp\"",
      "\"editor/commands/document/duplicate_selected_hierarchy_command.hpp\"",
      "\"editor/commands/document/group_selection_command.hpp\"",
      "\"editor/commands/document/rename_entity_command.hpp\"",
      "\"editor/commands/document/reparent_entity_command.hpp\"",
      "\"editor/commands/document/select_entities_command.hpp\"",
      "\"editor/commands/document/set_lock_command.hpp\"",
      "\"editor/commands/document/set_material_command.hpp\"",
      "\"editor/commands/document/set_mesh_component_command.hpp\"",
      "\"editor/commands/document/set_visibility_command.hpp\"",
      "\"editor/commands/document/ungroup_selection_command.hpp\"",
  };

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/api")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    for (std::string_view header : concrete_document_command_headers) {
      EXPECT_EQ(source.find(header), std::string::npos)
          << relative << " includes " << header;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiAndBehaviorDslDoNotExposeModelingActionCompatibilityRoute) {
  for (const std::filesystem::path &root : {
           source_root() / "src/api",
           source_root() / "tests/support",
       }) {
    for (const std::filesystem::path &file : source_files_under(root)) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);

      EXPECT_EQ(source.find(".modeling().executeAction("), std::string::npos)
          << relative;
      EXPECT_EQ(source.find(".modeling().execute_action("), std::string::npos)
          << relative;
      EXPECT_EQ(source.find("QuaderModelingApi::execute_action"),
                std::string::npos)
          << relative;
      EXPECT_EQ(source.find("QuaderScript& executeAction"), std::string::npos)
          << relative;
      EXPECT_EQ(source.find("QuaderScript& execute_action"),
                std::string::npos)
          << relative;
      EXPECT_EQ(source.find("BehaviorWhen& executeAction"), std::string::npos)
          << relative;
      EXPECT_EQ(source.find("BehaviorWhen& execute_action"),
                std::string::npos)
          << relative;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, CommandLayerDoesNotExposeGenericModelingActionExecutionRoute) {
  for (const std::filesystem::path &file : {
           source_root() / "src/api/quader_api_mutation_runner.hpp",
           source_root() / "src/api/quader_api_mutation_runner.cpp",
           source_root() /
               "src/editor/commands/modeling_command_execution_service.hpp",
           source_root() /
               "src/editor/commands/modeling_command_execution_service.cpp",
       }) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_EQ(source.find("runTrackedModelingAction"), std::string::npos)
        << relative;
    EXPECT_EQ(source.find("run_tracked_modeling_action"), std::string::npos)
        << relative;
    EXPECT_EQ(source.find("executeAction("), std::string::npos) << relative;
    EXPECT_EQ(source.find("execute_action("), std::string::npos) << relative;
    EXPECT_EQ(source.find("executeActionUntracked("), std::string::npos)
        << relative;
    EXPECT_EQ(source.find("execute_action_untracked("), std::string::npos)
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, SelectionCommandToolsPreferEditorSelectionService) {
  const std::unordered_map<std::string, std::string_view> expected_routes{
      {"src/editor/commands/modeling/clear_selection_tool.cpp",
       "context.selection->clear()"},
      {"src/editor/commands/modeling/select_all_tool.cpp",
       "context.selection->select_all()"},
      {"src/editor/commands/modeling/invert_selection_tool.cpp",
       "context.selection->invert()"},
  };

  for (const auto &[relative, expectedRoute] : expected_routes) {
    const std::string source = read_file(source_root() / relative);
    EXPECT_NE(source.find("context.selection != nullptr"), std::string::npos)
        << relative;
    EXPECT_NE(source.find(expectedRoute), std::string::npos) << relative;
  }

  const std::string invert_tool = read_file(
      source_root() / "src/editor/commands/modeling/invert_selection_tool.cpp");
  EXPECT_EQ(invert_tool.find("context.modeling.invert_selection()"),
            std::string::npos);
  EXPECT_NE(invert_tool.find(
                "Invert Selection requires the editor selection service."),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     EditorSelectionSummaryGatesProjectionBehindOwnerRevisions) {
  const std::string source = read_file(
      source_root() / "src/editor/selection/editor_selection_service.cpp");
  const std::string summary = source_slice(
      source, "ModelingSelectionSummary EditorSelectionService::summary() const",
      "ModelingHoverSummary EditorSelectionService::hover_summary() const");
  const std::size_t unchanged_gate =
      summary.find("if (summary_cache_valid_ && !preview_active");
  const std::size_t projected_summary =
      summary.find("modeling_.selection_summary()");

  ASSERT_NE(unchanged_gate, std::string::npos);
  ASSERT_NE(projected_summary, std::string::npos);
  EXPECT_LT(unchanged_gate, projected_summary);
  EXPECT_NE(summary.find("modeling_.content_revision()"), std::string::npos);
  EXPECT_NE(summary.find("modeling_.selection_revision()"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     CurveSubSelectionSnapshotKeepsCanonicalSummaryPollingAllocationFree) {
  const std::string source = read_file(
      source_root() / "src/editor/curve/curve_tool_service.cpp");
  const std::string snapshot = source_slice(
      source,
      "EditorSubSelectionSnapshot CurveToolService::sub_selection_snapshot() const",
      "CurveToolService::capture_sub_selection_baseline() const");

  EXPECT_EQ(snapshot.find("selected_entities()"), std::string::npos);
  EXPECT_EQ(snapshot.find("curve_document("), std::string::npos);
  EXPECT_EQ(snapshot.find("preview_state()"), std::string::npos);
  EXPECT_EQ(snapshot.find("valid_control_selection("), std::string::npos);
  EXPECT_NE(snapshot.find("observed_selection_revision_"),
            std::string::npos);
  EXPECT_NE(snapshot.find("observed_modeling_selection_revision_"),
            std::string::npos);

  const std::string execution = read_file(
      source_root() /
      "src/editor/commands/modeling_command_execution_service.cpp");
  const std::string execute = source_slice(
      execution,
      "ModelingCommandResult ModelingCommandExecutionService::execute(",
      "ModelingCommandResult "
      "ModelingCommandExecutionService::execute_untracked(");
  const std::size_t sub_selection =
      execute.find("selection.try_delete_sub_selection()");
  const std::size_t scene_selection =
      execute.find("scene_aware_delete_handles_selection(");
  ASSERT_NE(sub_selection, std::string::npos);
  ASSERT_NE(scene_selection, std::string::npos);
  EXPECT_LT(sub_selection, scene_selection);
  const std::string execute_untracked = source_slice(
      execution,
      "ModelingCommandResult "
      "ModelingCommandExecutionService::execute_untracked(",
      "ModelingCommandResult ModelingCommandExecutionService::begin_preview(");
  EXPECT_NE(execute_untracked.find("selection->try_delete_sub_selection()"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicSelectionApiDelegatesEntityEditPolicyToEditorSelectionService) {
  const std::string api_header =
      read_file(source_root() / "src/api/quader_selection_api.hpp");
  const std::string api_source =
      read_file(source_root() / "src/api/quader_selection_api.cpp");
  const std::string service_source = read_file(
      source_root() / "src/editor/selection/editor_selection_service.cpp");

  EXPECT_EQ(api_header.find("EditorSelectionEdit"), std::string::npos);
  EXPECT_EQ(api_source.find("editedEntitySelection"), std::string::npos);
  EXPECT_EQ(api_source.find("editedObjectSelection"), std::string::npos);
  EXPECT_EQ(api_source.find("session_.scene().selected_entities()"),
            std::string::npos);
  EXPECT_NE(api_source.find(
                "scene_session(session_).selection().edit_scene_selection("),
            std::string::npos);
  EXPECT_NE(service_source.find("EditorSelectionService::edit_scene_selection("),
            std::string::npos);
  EXPECT_NE(service_source.find("resolve_scene_selection_edit("),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     ComponentSelectionInversionUsesSingleModelingSessionOwner) {
  const std::string sdk_selection = read_file(
      source_root() / "src/sdk/core/modeling/src/selection.cpp");
  const std::string selection_invert = source_slice(
      sdk_selection, "OperationReceipt SelectionApi::invert() {",
      "ObjectSelection SelectionApi::objects(");
  ASSERT_FALSE(selection_invert.empty());
  EXPECT_NE(selection_invert.find("invert_component_selection_batch("),
            std::string::npos);
  EXPECT_EQ(selection_invert.find(".invert_selection("), std::string::npos);
  EXPECT_EQ(selection_invert.find("commit_document_receipt("),
            std::string::npos);

  const std::string host_adapter = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");
  const std::string adapter_invert = source_slice(
      host_adapter,
      "[[nodiscard]] ModelingCommandResult invert_selection(",
      "[[nodiscard]] ModelingCommandResult clear_selection() {");
  ASSERT_FALSE(adapter_invert.empty());
  EXPECT_NE(adapter_invert.find("session_.invert_component_selection_batch("),
            std::string::npos);
  EXPECT_EQ(adapter_invert.find(".invert_selection("), std::string::npos);
  EXPECT_EQ(adapter_invert.find("set_component_selections("),
            std::string::npos);
  EXPECT_EQ(adapter_invert.find("set_object_selection("), std::string::npos);
  EXPECT_EQ(adapter_invert.find("std::vector<EntityId> inverted"),
            std::string::npos);

  const std::string adapter_entry = source_slice(
      host_adapter,
      "ModelingCommandResult ModelingSdkProjectionAdapter::invert_selection(",
      "ModelingCommandResult ModelingSdkProjectionAdapter::clear_selection() {");
  ASSERT_FALSE(adapter_entry.empty());
  EXPECT_NE(adapter_entry.find("return impl_->invert_selection(universes);"),
            std::string::npos);

  const std::string adapter_header = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_sdk_projection_adapter.hpp");
  const std::string adapter_declaration = source_slice(
      adapter_header,
      "[[nodiscard]] ModelingCommandResult invert_selection(",
      "[[nodiscard]] ModelingCommandResult clear_selection();");
  ASSERT_FALSE(adapter_declaration.empty());
  EXPECT_EQ(adapter_declaration.find("universes = {}"), std::string::npos);

  const std::string modeling_service = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string service_invert = source_slice(
      modeling_service, "ModelingCommandResult invert_selection() {",
      "ModelingCommandResult clear_selection() {");
  ASSERT_FALSE(service_invert.empty());
  EXPECT_NE(service_invert.find("selection_mode_uses_scene_document("),
            std::string::npos);
  EXPECT_NE(service_invert.find("component_selection_universes("),
            std::string::npos);
  EXPECT_NE(service_invert.find(
                "sdk_projection_adapter_.invert_selection(universes)"),
            std::string::npos);
  EXPECT_EQ(service_invert.find("sdk_projection_adapter_.invert_selection();"),
            std::string::npos);

  const std::string selection_service = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string constrained_select_all = source_slice(
      selection_service,
      "ModelingSelectionService::select_all_in_constrained_component_scope(",
      "ModelingSelectionService::component_selection_universes(");
  ASSERT_FALSE(constrained_select_all.empty());
  EXPECT_EQ(constrained_select_all.find("invert"), std::string::npos);
  EXPECT_EQ(constrained_select_all.find("currently_selected"),
            std::string::npos);

  const std::string runtime_header = read_file(
      source_root() / "src/editor/modeling/runtime/session.hpp");
  const std::string runtime_policy = read_file(
      source_root() / "src/editor/modeling/runtime/selection_policy.cpp");
  EXPECT_EQ(runtime_header.find("SelectionSetCommand"), std::string::npos);
  EXPECT_EQ(runtime_policy.find("apply_selection_set_command"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ObjectAndGroupSelectionCommandsUseSceneDocumentSelectionOwner) {
  const std::string adapter =
      read_file(source_root() /
                "src/editor/modeling/services/selection_mode_adapter.hpp");
  const std::string service_source = read_file(
      source_root() / "src/editor/selection/editor_selection_service.cpp");
  const std::string selection_owner = read_file(
      source_root() /
      "src/editor/selection/scene_selection_application.cpp");
  const std::string delete_source = read_file(
      source_root() /
      "src/editor/commands/modeling/scene_aware_delete_selection_command.cpp");
  const std::string modeling_api =
      read_file(source_root() / "src/api/quader_modeling_api.cpp");
  const std::string mutation_runner =
      read_file(source_root() / "src/api/quader_api_mutation_runner.cpp");

  EXPECT_NE(adapter.find(
                "selection_mode_uses_scene_document(ModelingSelectionMode mode)"),
            std::string::npos);
  EXPECT_NE(service_source.find("selection_mode_uses_scene_document("),
            std::string::npos);
  EXPECT_NE(delete_source.find("selection_mode_uses_scene_document("),
            std::string::npos);
  EXPECT_NE(modeling_api.find("selection_mode_uses_scene_document("),
            std::string::npos);
  EXPECT_NE(mutation_runner.find("session.selection().set_mode(mode)"),
            std::string::npos);
  EXPECT_EQ(mutation_runner.find("selection_mode_uses_scene_document("),
            std::string::npos);
  EXPECT_EQ(service_source.find("sceneDocumentOwnsSelectionMode"),
            std::string::npos);
  EXPECT_EQ(delete_source.find(
                "scene_aware_delete_uses_document_route(ModelingSelectionMode mode) "
                "{\n    return mode == ModelingSelectionMode::Object"),
            std::string::npos);
  EXPECT_NE(selection_owner.find("user_selectable_scene_entities_for_mode"),
            std::string::npos);
  EXPECT_EQ(selection_owner.find("scene.is_scene_root(entity)"),
            std::string::npos);
  EXPECT_NE(service_source.find(
                "make_user_scene_selection(scene_, entities, active)"),
            std::string::npos);
  EXPECT_NE(service_source.find("set_scene_selection(selected, active)"),
            std::string::npos);
  EXPECT_NE(
      service_source.find(
          "set_scene_selection(std::span<const EntityId>{}, kInvalidEntityId)"),
      std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, SceneSelectionApplicationOwnerCoversCommandServiceAndProjection) {
  const std::string cmake = read_file(source_root() / "CMakeLists.txt");
  const std::string owner_header = read_file(
      source_root() / "src/editor/selection/scene_selection_application.hpp");
  const std::string owner_source = read_file(
      source_root() / "src/editor/selection/scene_selection_application.cpp");
  const std::string command = read_file(
      source_root() /
      "src/editor/commands/document/select_entities_command.hpp");
  const std::string document_service = read_file(
      source_root() / "src/editor/commands/document_command_service.cpp");
  const std::string editor_selection = read_file(
      source_root() / "src/editor/selection/editor_selection_service.cpp");
  const std::string projection = read_file(
      source_root() / "src/editor/selection/selection_projection_service.cpp");

  EXPECT_NE(cmake.find("src/editor/selection/scene_selection_application.cpp"),
            std::string::npos);
  EXPECT_NE(owner_header.find("enum class SceneSelectionApplicationMode"),
            std::string::npos);
  EXPECT_NE(owner_header.find("UndoableCommand"), std::string::npos);
  EXPECT_NE(owner_header.find("RuntimeSync"), std::string::npos);
  EXPECT_NE(owner_source.find("apply_undoable_scene_selection("),
            std::string::npos);
  EXPECT_NE(owner_source.find("apply_runtime_sync_scene_selection("),
            std::string::npos);

  EXPECT_NE(command.find("editor/selection/scene_selection_application.hpp"),
            std::string::npos);
  EXPECT_NE(command.find("capture_scene_selection(scene)"),
            std::string::npos);
  EXPECT_NE(command.find("make_scene_selection(scene, entities, active)"),
            std::string::npos);
  EXPECT_NE(command.find("SceneSelectionApplicationMode::UndoableCommand"),
            std::string::npos);
  EXPECT_NE(command.find(
                "DocumentCommandResult undo() override { return apply(before_); }"),
            std::string::npos);
  EXPECT_NE(command.find(
                "DocumentCommandResult redo() override { return apply(after_); }"),
            std::string::npos);
  EXPECT_NE(command.find("apply_scene_selection(scene(), selection, mode_)"),
            std::string::npos);

  EXPECT_NE(document_service.find("SceneSelectionApplicationMode mode"),
            std::string::npos);
  EXPECT_NE(document_service.find("std::make_unique<SelectEntitiesCommand>("),
            std::string::npos);
  EXPECT_NE(document_service.find("scene_, std::move(entities), active,"),
            std::string::npos);

  EXPECT_NE(editor_selection.find(
                "make_user_scene_selection(scene_, entities, active)"),
            std::string::npos);
  EXPECT_NE(editor_selection.find(
                "SceneSelectionApplicationMode::UndoableCommand"),
            std::string::npos);
  EXPECT_NE(editor_selection.find("resolve_scene_selection_edit("),
            std::string::npos);
  EXPECT_EQ(editor_selection.find(
                "bool scene_entity_selectable_for_user_selection"),
            std::string::npos);
  EXPECT_EQ(editor_selection.find("struct ResolvedSceneSelectionEdit"),
            std::string::npos);

  EXPECT_NE(projection.find("SceneSelectionApplicationMode::RuntimeSync"),
            std::string::npos);
  EXPECT_NE(projection.find("apply_scene_selection("), std::string::npos);
  EXPECT_NE(projection.find("quader::add_scene_selection("),
            std::string::npos);
  EXPECT_EQ(projection.find("apply_exact_scene_selection("),
            std::string::npos);
  EXPECT_EQ(projection.find("scene.set_selected("), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, GroupSelectionPickingUsesModelingSelectionService) {
  const std::string editor_selection_header = read_file(
      source_root() / "src/editor/selection/editor_selection_service.hpp");
  const std::string editor_selection_source = read_file(
      source_root() / "src/editor/selection/editor_selection_service.cpp");
  const std::string modeling_session_header =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_session_service.hpp");
  const std::string modeling_session_source =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string modeling_selection_header =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_selection_service.hpp");
  const std::string modeling_selection_source =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_session_services.cpp");

  EXPECT_NE(modeling_selection_header.find("pick_object_id("), std::string::npos);
  EXPECT_NE(modeling_selection_header.find("pick_object_entity("),
            std::string::npos);
  EXPECT_NE(modeling_selection_header.find("box_pick_object_ids("),
            std::string::npos);
  EXPECT_NE(modeling_selection_source.find(
                "rectangle_hits_from_projected_candidates"),
            std::string::npos);
  EXPECT_NE(modeling_session_header.find("pick_object_entity("),
            std::string::npos);
  EXPECT_NE(modeling_session_header.find("box_pick_object_entities("),
            std::string::npos);
  EXPECT_NE(modeling_session_source.find("selection_service_.pick_object_entity("),
            std::string::npos);
  EXPECT_NE(modeling_session_source.find("selection_service_.box_pick_object_ids("),
            std::string::npos);
  EXPECT_EQ(editor_selection_source.find("modeling_.pick_object_entity(request)"),
            std::string::npos);
  EXPECT_NE(editor_selection_source.find("query_modeling_hit("),
            std::string::npos);
  EXPECT_NE(editor_selection_source.find("apply_resolved_click("),
            std::string::npos);
  EXPECT_NE(
      editor_selection_source.find("modeling_.box_pick_object_entities(request)"),
      std::string::npos);
  EXPECT_EQ(editor_selection_header.find("AssetRegistry"), std::string::npos);
  EXPECT_EQ(editor_selection_header.find("pickMeshEntityByBounds"),
            std::string::npos);
  EXPECT_EQ(editor_selection_header.find("boxPickMeshEntitiesByBounds"),
            std::string::npos);
  EXPECT_EQ(editor_selection_source.find(
                "EditorSelectionService::pick_mesh_entity_by_bounds"),
            std::string::npos);
  EXPECT_EQ(editor_selection_source.find(
                "EditorSelectionService::box_pick_mesh_entities_by_bounds"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ApiDoesNotTreatHostContractDescriptorsAsCommands) {
  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/api")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_FALSE(contains_any(source,
                              {
                                  "host-contract",
                                  "host_contract",
                                  "quader_host_contract",
                              }))
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, ProductionSourcesDoNotIncludeTestSupportDsl) {
  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_FALSE(contains_any(source,
                              {
                                  "\"tests/support/",
                                  "<tests/support/",
                                  "\"support/behavior_scenario.hpp\"",
                                  "\"support/behavior_expect.hpp\"",
                                  "\"support/behavior_snapshot.hpp\"",
                                  "\"support/modeling_test_events.hpp\"",
                              }))
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotAddSceneDocumentMutations) {
  const std::vector<std::string_view> direct_scene_mutations = {
      "document.create_entity(",
      "document.create_empty(",
      "document.destroy_entity(",
      "document.rename_entity(",
      "document.set_transform(",
      "document.set_selected(",
      "document.clear_selection(",
      "document.set_parent(",
      "document.set_visible(",
      "document.set_locked(",
      "document.set_mesh(",
      "document.set_material(",
      "document.set_modeling_object(",
      "context.document.create_entity(",
      "context.document.create_empty(",
      "context.document.destroy_entity(",
      "context.document.rename_entity(",
      "context.document.set_transform(",
      "context.document.set_selected(",
      "context.document.clear_selection(",
      "context.document.set_parent(",
      "context.document.set_visible(",
      "context.document.set_locked(",
      "context.document.set_mesh(",
      "context.document.set_material(",
      "context.document.set_modeling_object(",
  };

  for (const std::filesystem::path &root : {
           source_root() / "src/ui",
           source_root() / "src/app",
           source_root() / "src/viewport",
           source_root() / "src/editor/tools",
       }) {
    for (const std::filesystem::path &file :
         files_with_extension(root, ".cpp")) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      for (const std::string &line :
           lines_containing(source, direct_scene_mutations)) {
        ADD_FAILURE() << relative << ": " << line;
      }
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotAddRawModelingMutations) {
  const std::vector<std::string_view> raw_modeling_mutations = {
      "modeling.execute_instant_operation(",
      "modeling.execute_context_action(",
      "modeling.commit_active_operation_preview(",
      "modeling.cancel_active_operation_preview(",
      "modeling.begin_transform_preview(",
      "modeling.update_transform_translate_preview(",
      "modeling.update_transform_matrix_preview(",
      "modeling.update_transform_extrude_preview(",
      "modeling.update_transform_inset_preview(",
      "modeling.commit_transform_preview(",
      "modeling.cancel_transform_preview(",
      "modeling.begin_transform_drag(",
      "modeling.update_transform_drag(",
      "modeling.commit_transform_drag(",
      "modeling.cancel_transform_drag(",
      "modeling.create_box_from_bounds(",
      "modeling.pick_select(",
      "modeling.set_selection_mode(",
      "modeling.sync_object_selection_from_scene(",
      "modeling.sync_scene_selection_from_modeling(",
      "modeling.select_all(",
      "modeling.clear_selection(",
      "modeling.delete_component_selection(",
      "modeling.apply_material(",
      "context.modeling.execute_instant_operation(",
      "context.modeling.execute_context_action(",
      "context.modeling.commit_active_operation_preview(",
      "context.modeling.cancel_active_operation_preview(",
      "context.modeling.begin_transform_preview(",
      "context.modeling.update_transform_translate_preview(",
      "context.modeling.update_transform_matrix_preview(",
      "context.modeling.update_transform_extrude_preview(",
      "context.modeling.update_transform_inset_preview(",
      "context.modeling.commit_transform_preview(",
      "context.modeling.cancel_transform_preview(",
      "context.modeling.begin_transform_drag(",
      "context.modeling.update_transform_drag(",
      "context.modeling.commit_transform_drag(",
      "context.modeling.cancel_transform_drag(",
      "context.modeling.create_box_from_bounds(",
      "context.modeling.pick_select(",
      "context.modeling.set_selection_mode(",
      "context.modeling.sync_object_selection_from_scene(",
      "context.modeling.sync_scene_selection_from_modeling(",
      "context.modeling.select_all(",
      "context.modeling.clear_selection(",
      "context.modeling.delete_component_selection(",
      "context.modeling.apply_material(",
  };

  for (const std::filesystem::path &root :
       {source_root() / "src/ui", source_root() / "src/app",
        source_root() / "src/viewport"}) {
    for (const std::filesystem::path &file :
         files_with_extension(root, ".cpp")) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      for (const std::string &line :
           lines_containing(source, raw_modeling_mutations)) {
        EXPECT_TRUE(allowed_raw_modeling_mutation(relative, line))
            << relative << ": " << line;
      }
    }
  }
}

TEST(ProgrammaticApiGuardrailTests,
     QtToolSettingsPanelWritesSettingsThroughPublicToolsApi) {
  const std::string panel =
      read_file(source_root() / "src/ui/qt_tool_settings_panel.cpp");

  EXPECT_NE(panel.find("editor_->tools().set_setting("), std::string::npos);
  EXPECT_EQ(panel.find("editor_->tools().reset_active_settings()"),
            std::string::npos);
  EXPECT_EQ(panel.find("set_interaction_settings("), std::string::npos);
  EXPECT_EQ(panel.find("context.tool_settings"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     UiMutatingOwnerApiCallsRouteThroughPublicEditorApi) {
  const std::vector<std::string_view> forbidden_owner_mutations =
      ui_mutating_owner_api_tokens();

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/ui")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    for (const std::string &line :
         lines_containing(source, forbidden_owner_mutations)) {
      EXPECT_TRUE(allowed_ui_mutating_owner_api_call(relative, line))
          << relative
          << ": UI editor intent must route through UIContext::editor / "
             "QuaderEditorApi. Direct UI mutation is limited to UI settings, "
             "viewport workspace state, and render presentation exceptions: "
          << line;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, ViewportRoutesDoNotExecuteFixedRawModelingCommandStrings) {
  const std::vector<std::string_view> forbidden_raw_command_routes = {
      "commands().execute(\"modeling.",
      "std::string command = \"modeling.\"",
      "command.append(tool_id)",
      "contextMenuCommandId(std::string_view",
      "context_menu_command_id(std::string_view",
      "const std::string command_id = contextMenuCommandId(action.action_id)",
      "const std::string command_id = "
      "context_menu_command_id(action.action_id)",
  };

  for (std::string_view relative : {
           std::string_view{"src/ui/viewport_shortcut_controller.cpp"},
           std::string_view{"src/ui/ui_system.cpp"},
       }) {
    const std::string source = read_file(source_root() / std::string(relative));
    for (const std::string &line :
         lines_containing(source, forbidden_raw_command_routes)) {
      EXPECT_TRUE(false) << relative << ": " << line;
    }
    }
}

TEST(ProgrammaticApiGuardrailTests, ViewportShortcutControllerDoesNotRemapCommands) {
  const std::string source =
      read_file(source_root() / "src/ui/viewport_shortcut_controller.cpp");
  const std::string header =
      read_file(source_root() / "src/ui/viewport_shortcut_controller.hpp");

  EXPECT_EQ(source.find("QuaderCommand::"), std::string::npos);
  EXPECT_EQ(source.find("executeViewportModelingShortcut"), std::string::npos);
  EXPECT_EQ(source.find("execute_viewport_modeling_shortcut"),
            std::string::npos);
  EXPECT_EQ(source.find("commandForMirrorAxis"), std::string::npos);
  EXPECT_EQ(source.find("command_for_mirror_axis"), std::string::npos);
  EXPECT_EQ(source.find("ModelingSessionService"), std::string::npos);
  EXPECT_EQ(header.find("ModelingSessionService"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ViewportShadingConversionUsesSharedApiHelper) {
  const std::string viewport_api =
      read_file(source_root() / "src/api/quader_viewport_api.cpp");
  const std::string viewport_panel =
      read_file(source_root() / "src/ui/qt_viewport_panel.cpp");
  const std::string conversion_helper = read_file(
      source_root() / "src/viewport/viewport_shading_mode_conversions.hpp");

  EXPECT_NE(conversion_helper.find("quader_api_to_viewport_shading_mode"),
            std::string::npos);
  EXPECT_NE(conversion_helper.find("quader_api_from_viewport_shading_mode"),
            std::string::npos);
  for (std::string_view debug_mode : {
           "DiffuseOnly", "SpecularOnly", "Albedo", "Roughness",
           "Metalness", "Normal"}) {
    EXPECT_NE(conversion_helper.find(debug_mode), std::string::npos)
        << debug_mode;
  }
  EXPECT_EQ(viewport_api.find("ViewportShadingMode toViewportShadingMode"),
            std::string::npos);
  EXPECT_EQ(viewport_api.find("QuaderShadingMode toQuaderShadingMode"),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("ViewportShadingMode toViewportShadingMode"),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("QuaderShadingMode toQuaderShadingMode"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, UiPanelRoutingUsesStableIdsAndNoDeadShowMenu) {
  const std::string main_menu_header =
      read_file(source_root() / "src/ui/qt_main_menu.hpp");
  const std::string main_menu_source =
      read_file(source_root() / "src/ui/qt_main_menu.cpp");
  const std::string settings_source =
      read_file(source_root() / "src/ui/ui_settings.cpp");
  const std::string ui_panel_header =
      read_file(source_root() / "src/ui/ui_panel.hpp");

  EXPECT_NE(ui_panel_header.find("ui_panel_stable_id"), std::string::npos);
  EXPECT_NE(ui_panel_header.find("ui_panel_default_visible"), std::string::npos);
  EXPECT_EQ(main_menu_header.find("ShowMenu"), std::string::npos);
  EXPECT_EQ(main_menu_header.find("showMenu_"), std::string::npos);
  EXPECT_EQ(main_menu_source.find("void ShowMenu::draw"), std::string::npos);
  EXPECT_EQ(main_menu_header.find("toolSettingsPanelOpen"), std::string::npos);
  EXPECT_EQ(main_menu_header.find("UISettings& settings"), std::string::npos);
  EXPECT_EQ(main_menu_source.find("show_panel(ui_panel_key"), std::string::npos);
  EXPECT_EQ(main_menu_source.find("hide_panel(ui_panel_key"), std::string::npos);
  EXPECT_NE(main_menu_header.find("std::function<bool(UIPanelId)> panel_visible"),
            std::string::npos);
  EXPECT_NE(
      main_menu_header.find("std::function<void(UIPanelId, bool)> set_panel_visible"),
      std::string::npos);
  EXPECT_NE(main_menu_source.find("ui_panel_stable_id(id)"),
            std::string::npos);
  EXPECT_EQ(settings_source.find("file << ui_panel_key"), std::string::npos);
  EXPECT_NE(settings_source.find("for (UIPanelId id : kUiDockPanelIds)"),
            std::string::npos);
  EXPECT_NE(settings_source.find("file << ui_panel_stable_id(id)"),
            std::string::npos);
  EXPECT_EQ(ui_panel_header.find("return \"tool_settings\""),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     QtViewportPanelUsesWorkspaceOwnerWithoutPublicPassThroughs) {
  const std::string header =
      read_file(source_root() / "src/ui/qt_viewport_panel.hpp");
  const std::string source =
      read_file(source_root() / "src/ui/qt_viewport_panel.cpp");

  EXPECT_NE(header.find("sync_active_tool_from_editor("), std::string::npos);
  EXPECT_NE(source.find("QtViewportPanel::sync_active_tool_from_editor("),
            std::string::npos);
  EXPECT_NE(source.find("workspace_->active_pane_placements()"),
            std::string::npos);
  EXPECT_NE(source.find("workspace_->active_split_handles()"),
            std::string::npos);
  EXPECT_NE(header.find("void update_pane_geometry()"),
            std::string::npos);
  EXPECT_NE(header.find("void update_split_handles()"),
            std::string::npos);
  EXPECT_EQ(header.find("setViewportLayout("), std::string::npos);
  EXPECT_EQ(header.find("set_viewport_layout("), std::string::npos);
  EXPECT_EQ(header.find("set_grid_preset("), std::string::npos);
  EXPECT_EQ(header.find("paneShadingMode("), std::string::npos);
  EXPECT_EQ(header.find("pane_shading_mode("), std::string::npos);
  EXPECT_EQ(header.find("setPaneShadingMode("), std::string::npos);
  EXPECT_EQ(header.find("set_pane_shading_mode("), std::string::npos);
  EXPECT_EQ(header.find("paneView("), std::string::npos);
  EXPECT_EQ(header.find("active_pane_placements()"), std::string::npos);
  EXPECT_EQ(header.find("active_split_handles()"), std::string::npos);
  EXPECT_EQ(header.find("pane_rect_min("), std::string::npos);
  EXPECT_EQ(header.find("pane_rect_size("), std::string::npos);
  EXPECT_EQ(header.find("SceneDocument& document,\n            "
                        "ModelingSessionService& modeling"),
            std::string::npos);
  EXPECT_EQ(source.find("ViewportPanel::set_viewport_layout("),
            std::string::npos);
  EXPECT_EQ(source.find("ViewportPanel::set_grid_preset("), std::string::npos);
  EXPECT_EQ(source.find("ViewportPanel::active_pane_placements("),
            std::string::npos);
  EXPECT_EQ(source.find("ViewportPanel::active_split_handles("),
            std::string::npos);
  EXPECT_EQ(source.find("ViewportPanel::pane_rect_min("), std::string::npos);
  EXPECT_EQ(source.find("ViewportPanel::pane_rect_size("), std::string::npos);
  EXPECT_EQ(source.find("fromModelingViewportTool"), std::string::npos);
  EXPECT_EQ(source.find("from_modeling_viewport_tool"), std::string::npos);
  EXPECT_NE(source.find(
                "editor.tools().activate(input_router_.state().active_tool)"),
            std::string::npos);
  EXPECT_NE(source.find("find_viewport_tool(editor.tools().active_tool())"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     EdgeLengthHudBorrowsVolatileHoverAndGatesRetainedPresentation) {
  const std::string hud =
      read_file(source_root() / "src/ui/qt_viewport_edge_length_hud.cpp");
  const std::string presentation = source_slice(
      hud, "void QtViewportEdgeLengthHud::present(",
      "void QtViewportEdgeLengthHud::hide()");
  const auto signature_gate = presentation.find(
      "!frame_signature_valid_ || frame_signature_ != frame_signature");
  const auto scan = presentation.find("std::find_if(");
  const auto geometry_gate =
      presentation.find("same_dimension_geometry(dimensions_[index]");
  const auto formatting = presentation.find("dimension_length_text(");
  const auto view_gate = presentation.find("if (view_valid_");
  const auto projection =
      presentation.find("camera.project_visible_world_segment(");
  const auto text_setter = presentation.find("label->setText(");
  const auto geometry_update = presentation.find("present_label_geometry(");
  const std::string geometry = source_slice(
      hud, "void present_label_geometry(", "} // namespace");
  const auto resize_gate = geometry.find("if (label.size() != geometry.size())");
  const auto move_gate = geometry.find("if (label.pos() != geometry.topLeft())");
  const auto mask_refresh =
      geometry.find("refresh_native_mask_if_needed()", move_gate);
  ASSERT_NE(signature_gate, std::string::npos);
  ASSERT_NE(scan, std::string::npos);
  ASSERT_NE(geometry_gate, std::string::npos);
  ASSERT_NE(formatting, std::string::npos);
  ASSERT_NE(view_gate, std::string::npos);
  ASSERT_NE(projection, std::string::npos);
  ASSERT_NE(text_setter, std::string::npos);
  ASSERT_NE(geometry_update, std::string::npos);
  ASSERT_NE(resize_gate, std::string::npos);
  ASSERT_NE(move_gate, std::string::npos);
  ASSERT_NE(mask_refresh, std::string::npos);
  EXPECT_LT(signature_gate, scan);
  EXPECT_LT(geometry_gate, formatting);
  EXPECT_LT(view_gate, projection);
  EXPECT_LT(view_gate, text_setter);
  EXPECT_LT(text_setter, geometry_update);
  EXPECT_LT(resize_gate, move_gate);
  EXPECT_LT(move_gate, mask_refresh);
  EXPECT_EQ(presentation.find("label->setGeometry(geometry)"),
            std::string::npos)
      << "Position-only motion must not rebuild the native glyph mask.";
  EXPECT_NE(presentation.find("ViewportOverlaySource::ComponentHover"),
            std::string::npos);
  EXPECT_NE(presentation.find("ViewportOverlayLayer::HoverEdge"),
            std::string::npos);
  EXPECT_NE(hud.find("viewport_host_defaults().map_units_per_world_unit"),
            std::string::npos);
  EXPECT_NE(hud.find("setFrameStyle(QFrame::NoFrame)"), std::string::npos);
  EXPECT_NE(hud.find("setAutoFillBackground(false)"), std::string::npos);
  EXPECT_EQ(hud.find("setAttribute(Qt::WA_TranslucentBackground)"),
            std::string::npos)
      << "Layered native children halve Vulkan presentation throughput on "
         "some Windows compositors.";
  EXPECT_NE(hud.find("setAttribute(Qt::WA_NoSystemBackground)"),
            std::string::npos);
  EXPECT_NE(hud.find("!font_size_valid_ ||"), std::string::npos);
  EXPECT_NE(hud.find("font.setPointSizeF(point_size)"),
            std::string::npos);
  EXPECT_NE(hud.find("tightBoundingRect(text)"), std::string::npos);
  EXPECT_NE(hud.find("painter.drawText("), std::string::npos);
  EXPECT_NE(
      hud.find("setRenderHint(QPainter::TextAntialiasing, false)"),
      std::string::npos);
  EXPECT_NE(hud.find("kDimensionLabelGapPixels"), std::string::npos);
  EXPECT_NE(hud.find("outside_reference"), std::string::npos);
  EXPECT_NE(hud.find("frame.hover_edge_dimension_context"),
            std::string::npos);
  EXPECT_NE(hud.find("label_support_radius"), std::string::npos);
  EXPECT_NE(hud.find("QBitmap glyph_mask(size())"), std::string::npos);
  EXPECT_NE(hud.find("QRegion{glyph_mask}"), std::string::npos);
  EXPECT_NE(hud.find("QImage::Format_ARGB32"), std::string::npos);
  EXPECT_NE(hud.find("qRgba(0, 0, 0, 255)"), std::string::npos);
  EXPECT_NE(hud.find("palette().color(foregroundRole())"), std::string::npos);
  EXPECT_NE(hud.find("glyph_color.rgba()"), std::string::npos);
  EXPECT_NE(hud.find("QPainter::CompositionMode_Source"),
            std::string::npos);
  EXPECT_NE(hud.find("QPainter::SmoothPixmapTransform, false"),
            std::string::npos);
  EXPECT_NE(hud.find("painter.drawImage(QPoint{}, raster_)"),
            std::string::npos);
  EXPECT_NE(hud.find("const QFont raster_font{font(), this}"),
            std::string::npos);
  EXPECT_NE(hud.find("mask_shape_revision_ == shape_revision_"),
            std::string::npos);
  EXPECT_NE(hud.find("QEvent::DevicePixelRatioChange"), std::string::npos);
  EXPECT_NE(hud.find("QEvent::ScreenChangeInternal"), std::string::npos);
  EXPECT_EQ(hud.find("QPainterPathStroker"), std::string::npos);
  EXPECT_EQ(hud.find("addText("), std::string::npos);
  EXPECT_EQ(
      hud.find("setRenderHint(QPainter::Antialiasing, true)"),
      std::string::npos);
  EXPECT_NE(hud.find("setMask(outline_region_)"), std::string::npos);
  EXPECT_EQ(hud.find("setAutoFillBackground(true)"), std::string::npos);
  EXPECT_EQ(hud.find("QPalette::ToolTipBase"), std::string::npos);
  for (const std::string_view forbidden : {
           "SceneDocument", "AssetRegistry", "selection().summary()",
           "selection_overlay_frame()", "commands().available()",
           "overlay_frame_signature(frame)",
       }) {
    EXPECT_EQ(hud.find(forbidden), std::string::npos) << forbidden;
  }

  const std::string panel =
      read_file(source_root() / "src/ui/qt_viewport_panel.cpp");
  const std::string prepare = source_slice(
      panel, "void QtViewportPanel::prepare_viewports(",
      "QtViewportPanel::render_viewports(");
  const auto raw_hover = prepare.find("modeling.volatile_overlay_snapshot()");
  const auto semantic_source =
      prepare.find("ViewportToolOverlaySource dynamic_tool_frame_provider");
  const auto coordinator_call =
      prepare.find("render_bridge.submit_viewport_overlay(");
  const auto hud_call = prepare.find("pane_state.edge_length_hud->present(");
  ASSERT_NE(raw_hover, std::string::npos);
  ASSERT_NE(semantic_source, std::string::npos);
  ASSERT_NE(coordinator_call, std::string::npos);
  ASSERT_NE(hud_call, std::string::npos);
  EXPECT_EQ(prepare.find("modeling.volatile_overlay_frame()"),
            std::string::npos);
  EXPECT_LT(raw_hover, semantic_source);
  EXPECT_LT(semantic_source, coordinator_call);
  EXPECT_LT(coordinator_call, hud_call);
  EXPECT_NE(prepare.find("overlay_submission.dynamic_source_signature"),
            std::string::npos);
  EXPECT_NE(prepare.find("overlay_submission.edge_hud_frame"),
            std::string::npos);
  EXPECT_NE(prepare.find("cached_overlay_mode == ModelingSelectionMode::Edge"),
            std::string::npos);
  EXPECT_NE(prepare.find("ViewportBoolSettingId::ShowHoverDimensions"),
            std::string::npos);
  EXPECT_NE(prepare.find("ViewportFloatSettingId::DimensionFontSize"),
            std::string::npos);
  EXPECT_NE(prepare.find("edge_length_hud_enabled_ && hover_dimensions_visible"),
            std::string::npos);
  EXPECT_NE(prepare.find("active_box_preview_.footprint_count >= 2U"),
            std::string::npos);
  EXPECT_NE(prepare.find("&active_box_preview_, overlays_visible"),
            std::string::npos);
  EXPECT_NE(prepare.find("overlays_visible && tool_overlay_visible"),
            std::string::npos);
  EXPECT_EQ(prepare.find("ViewportOverlayStyleResolver::apply("),
            std::string::npos);
  EXPECT_EQ(prepare.find("ViewportOverlayFrame dynamic_overlay_frame"),
            std::string::npos);
  EXPECT_EQ(prepare.find("ViewportToolOverlayProjection::append_box("),
            std::string::npos);
  // Backface toggles restyle a fresh semantic stream, never a previously
  // filtered render frame whose hover triangles have already been removed.
  const std::string coordinator = read_file(
      source_root() /
      "src/render/overlay/viewport_overlay_submission_coordinator.cpp");
  const std::size_t style_signature = coordinator.find(
      "ViewportOverlayStyleResolver::style_signature(");
  const std::size_t dynamic_gate =
      coordinator.find("if (dynamic_changed)", style_signature);
  ASSERT_NE(style_signature, std::string::npos);
  ASSERT_NE(dynamic_gate, std::string::npos);
  EXPECT_LT(style_signature, dynamic_gate);
  EXPECT_NE(coordinator.find(".style_signature = style_signature"),
            std::string::npos);
  EXPECT_NE(coordinator.find(".raster_scale = request.raster_scale"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     DimensionHoverDoesNotRepeatAMissedBoundarySurfaceTraversal) {
  const std::string picking = read_file(
      source_root() / "src/editor/modeling/runtime/session_picking.cpp");
  const std::string boundary = source_slice(
      picking, "std::optional<PickHitRecord> pick_mesh_boundary(",
      "std::optional<PickHitRecord> nearest_surface_hit(");

  EXPECT_EQ(boundary.find(".surface_hit("), std::string::npos)
      << "The boundary fallback runs only after the authoritative surface "
         "picker missed; repeating that traversal adds hover-frame work.";
  EXPECT_NE(boundary.find(
                "facts.best_edge_context_face_id(nearest->edge, local_ray)"),
            std::string::npos)
      << "Only the winning boundary edge needs an incident-face context.";
}

TEST(ProgrammaticApiGuardrailTests, OutlinerUsesDocumentCommandRootSelectionOwner) {
  const std::string outliner =
      read_file(source_root() / "src/ui/outliner_panel.cpp");
  const std::string scene_aware_delete_header = read_file(
      source_root() /
      "src/editor/commands/modeling/scene_aware_delete_selection_command.hpp");
  const std::string modeling_execution =
      read_file(source_root() /
                "src/editor/commands/modeling_command_execution_service.cpp");

  EXPECT_NE(outliner.find("document_command_selected_root_entities"),
            std::string::npos);
  EXPECT_NE(outliner.find("select_objects_for_inspection"),
            std::string::npos);
  EXPECT_NE(outliner.find("quader_command::kDuplicateSelection"),
            std::string::npos);
  EXPECT_NE(outliner.find("quader_command::kDeleteSelection"),
            std::string::npos);
  EXPECT_NE(outliner.find("editor_->scene().rename("), std::string::npos);
  EXPECT_NE(outliner.find("editor_->scene().move_entities("),
            std::string::npos);
  EXPECT_NE(outliner.find("editor_->scene().set_visibility("),
            std::string::npos);
  EXPECT_NE(outliner.find("editor_->scene().set_locks("),
            std::string::npos);
  EXPECT_NE(scene_aware_delete_header.find("scene_aware_delete_handles_selection"),
            std::string::npos);
  EXPECT_EQ(outliner.find("std::vector<EntityId> selectedRootEntities("),
            std::string::npos);
  EXPECT_EQ(outliner.find("std::vector<EntityId> selected_root_entities("),
            std::string::npos);
  EXPECT_EQ(outliner.find("std::unordered_set<EntityId> selected_set"),
            std::string::npos);
  EXPECT_EQ(outliner.find("editor.scene().duplicate_selected_hierarchy"),
            std::string::npos);
  EXPECT_EQ(outliner.find("document_->rename_entity("), std::string::npos);
  EXPECT_EQ(outliner.find("document_->set_visible("), std::string::npos);
  EXPECT_EQ(outliner.find("document_->set_locked("), std::string::npos);
  EXPECT_EQ(outliner.find("ImGuiKey_G"), std::string::npos);
  EXPECT_EQ(modeling_execution.find("shouldUseSceneAwareDelete"),
            std::string::npos);
  EXPECT_NE(modeling_execution.find("scene_aware_delete_handles_selection"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotExposeRawHistoryOrDocumentCommandServices) {
  const std::vector<std::string_view> forbidden_owner_services = {
      "UndoRedoManager",
      "DocumentCommandService",
      "core/undo_redo_manager.hpp",
      "editor/commands/document_command_service.hpp",
      "undo_redo",
      "document_commands",
  };

  for (const std::filesystem::path &root :
       {source_root() / "src/ui", source_root() / "src/app",
        source_root() / "src/viewport"}) {
    for (const std::filesystem::path &file : source_files_under(root)) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      for (const std::string &line :
           lines_containing(source, forbidden_owner_services)) {
        EXPECT_TRUE(false) << relative << ": " << line;
      }
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotUseModelingActionCompatibilityRoute) {
  const std::vector<std::string_view> forbidden_action_routes = {
      "editor.modeling().executeAction(",
      "editor.modeling().execute_action(",
      "context.editor.modeling().executeAction(",
      "context.editor.modeling().execute_action(",
      "ui_context.editor.modeling().executeAction(",
      "ui_context.editor.modeling().execute_action(",
      ".modeling().executeAction(",
      ".modeling().execute_action(",
  };

  for (const std::filesystem::path &root :
       {source_root() / "src/ui", source_root() / "src/app",
        source_root() / "src/viewport"}) {
    for (const std::filesystem::path &file : source_files_under(root)) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      for (const std::string &line :
           lines_containing(source, forbidden_action_routes)) {
        EXPECT_TRUE(false) << relative << ": " << line;
      }
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, UiAppAndViewportDoNotUseSceneSelectionAsAuthority) {
  const std::vector<std::string_view> forbidden_scene_selection_routes = {
      "editor.scene().select(",
      "context.editor.scene().select(",
      "ui_context.editor.scene().select(",
      ".scene().select(",
  };

  for (const std::filesystem::path &root :
       {source_root() / "src/ui", source_root() / "src/app",
        source_root() / "src/viewport"}) {
    for (const std::filesystem::path &file : source_files_under(root)) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      for (const std::string &line :
           lines_containing(source, forbidden_scene_selection_routes)) {
        EXPECT_TRUE(false) << relative << ": " << line;
      }
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicHistoryDoesNotFallbackToModelingHistory) {
  const std::vector<std::string_view> forbidden_split_history_routes = {
      "ModelingHistoryCommand", "modeling().can_undo(", "modeling().can_redo(",
      "modeling().undo(",       "modeling().redo(",    "modeling.can_undo(",
      "modeling.can_redo(",      "modeling.undo(",      "modeling.redo(",
      "can_undo() ||",           "can_redo() ||",        "|| modeling.can_undo",
      "|| modeling.can_redo",
  };

  for (const std::filesystem::path &root :
       {source_root() / "src/api", source_root() / "src/editor"}) {
    for (const std::filesystem::path &file : source_files_under(root)) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      for (const std::string &line :
           lines_containing(source, forbidden_split_history_routes)) {
        EXPECT_TRUE(false) << relative << ": " << line;
      }
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicHistoryAvailabilityRoutesThroughMutationRunner) {
  const std::string mutation_header =
      read_file(source_root() / "src/api/quader_api_mutation_runner.hpp");
  const std::string mutation_source =
      read_file(source_root() / "src/api/quader_api_mutation_runner.cpp");
  const std::string history_api =
      read_file(source_root() / "src/api/quader_history_api.cpp");
  const std::string query_api =
      read_file(source_root() / "src/api/quader_query_api.cpp");
  const std::string history_service = read_file(
      source_root() / "src/editor/history/editor_history_service.cpp");

  EXPECT_NE(mutation_header.find("bool can_redo() const"), std::string::npos);
  EXPECT_NE(mutation_source.find("QuaderApiMutationRunner::can_redo()"),
            std::string::npos);
  EXPECT_NE(mutation_source.find("can_cancel_active_preview_for_undo()"),
            std::string::npos);
  EXPECT_NE(mutation_source.find("if (!can_redo())"), std::string::npos);
  EXPECT_NE(history_api.find("return mutations_.can_redo();"),
            std::string::npos);
  EXPECT_NE(query_api.find(
                "return QuaderHistoryApi(session_, mutations_).snapshot();"),
            std::string::npos);
  EXPECT_NE(history_service.find(
                "active_transaction_ == nullptr && undo_redo_->can_undo()"),
            std::string::npos);
  EXPECT_NE(history_service.find(
                "active_transaction_ == nullptr && undo_redo_->can_redo()"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ScriptHistoryUsesEditorTransactionAsSourceOfTruth) {
  const std::string script =
      read_file(source_root() / "src/api/quader_script.cpp");
  const std::string mutation_header =
      read_file(source_root() / "src/api/quader_api_mutation_runner.hpp");
  const std::string mutation_source =
      read_file(source_root() / "src/api/quader_api_mutation_runner.cpp");

  EXPECT_NE(script.find("transaction.commit()"), std::string::npos);
  EXPECT_NE(
      script.find("History operations are not supported inside QuaderScript."),
      std::string::npos);
  EXPECT_EQ(script.find("transaction.discard()"), std::string::npos);
  EXPECT_EQ(script.find("recordModelingMementoTransaction"), std::string::npos);
  EXPECT_EQ(mutation_header.find("recordModelingMementoTransaction"),
            std::string::npos);
  EXPECT_EQ(mutation_source.find("recordModelingMementoTransaction"),
            std::string::npos);
  EXPECT_EQ(mutation_source.find("RecordedModelingMementoCommand"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, DocumentBatchTransformPlanningUsesSimulatedLockState) {
  const std::string service = read_file(
      source_root() / "src/editor/commands/document_command_service.cpp");
  const std::string batch_source = source_slice(
      service, "DocumentCommandResult DocumentCommandService::apply_scene_batch",
      "\n}\n\n} // namespace quader");

  EXPECT_NE(batch_source.find("simulated_effectively_locked"), std::string::npos);
  EXPECT_NE(batch_source.find("simulated_parent"), std::string::npos);
  EXPECT_NE(batch_source.find("simulated_locked"), std::string::npos);
  EXPECT_EQ(batch_source.find("scene_.is_effectively_locked(operation.entity)"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     BinaryQdrAndCutHelpersStayWithTheirOwners) {
  const std::string qdr_reader =
      read_file(source_root() / "src/io/qdr/qdr_document_reader.cpp");
  const std::string qdr_writer =
      read_file(source_root() / "src/io/qdr/qdr_document_writer.cpp");
  const std::string qdr_debug_dump =
      read_file(source_root() / "src/io/qdr/qdr_debug_dump.cpp");
  const std::string session_ops = read_file(
      source_root() / "src/editor/modeling/runtime/session_operations.cpp");
  const std::string sdk_adapter = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");

  for (const std::string *codec : {&qdr_reader, &qdr_writer}) {
    EXPECT_EQ(codec->find("nlohmann"), std::string::npos);
    EXPECT_EQ(codec->find("from_json"), std::string::npos);
    EXPECT_EQ(codec->find("legacy"), std::string::npos);
  }
  EXPECT_NE(qdr_reader.find("read_qdr_binary_container"), std::string::npos);
  EXPECT_NE(qdr_writer.find("encode_qdr_scene_binary"), std::string::npos);
  EXPECT_NE(qdr_debug_dump.find("Developer inspection output"),
            std::string::npos);
  EXPECT_EQ(qdr_debug_dump.find("from_json"), std::string::npos);
  EXPECT_EQ(qdr_debug_dump.find("LegacyDefault"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/io/qdr/qdr_project_codecs.cpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/io/qdr/qdr_project_codecs.hpp"));
  EXPECT_EQ(session_ops.find("to_plane_cut_mode("), std::string::npos);
  EXPECT_EQ(session_ops.find("plane_cut_request_from_state("),
            std::string::npos);
  EXPECT_EQ(session_ops.find("plane_cut_request_from_cut_state("),
            std::string::npos);
  EXPECT_EQ(session_ops.find("split_by_plane("), std::string::npos);
  EXPECT_NE(sdk_adapter.find("preview_plane_cut("), std::string::npos);
  EXPECT_NE(sdk_adapter.find("split_by_plane("), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/editor/modeling/runtime/cut_policy.hpp"));
}

TEST(ProgrammaticApiGuardrailTests, EditorHistoryBoundaryHidesCoreUndoRedoManager) {
  const std::vector<std::filesystem::path> editor_history_clients = {
      source_root() / "src/api/quader_api.cpp",
      source_root() / "src/api/quader_scene_api.cpp",
      source_root() / "src/editor/session/scene_session.hpp",
      source_root() / "src/editor/session/scene_session.cpp",
      source_root() / "src/editor/commands/document_command_service.hpp",
      source_root() / "src/editor/commands/document_command_service.cpp",
      source_root() /
          "src/editor/history/scene_transform_preview_transaction.hpp",
  };
  for (const std::filesystem::path &file : editor_history_clients) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    EXPECT_EQ(source.find("UndoRedoManager"), std::string::npos) << relative;
    EXPECT_EQ(source.find("core/undo_redo_manager.hpp"), std::string::npos)
        << relative;
    EXPECT_EQ(source.find("undo_redo()"), std::string::npos) << relative;
  }

  const std::string history_header = read_file(
      source_root() / "src/editor/history/editor_history_service.hpp");
  EXPECT_EQ(history_header.find("core/undo_redo_manager.hpp"),
            std::string::npos);
  EXPECT_EQ(history_header.find("undo_redo()"), std::string::npos);

  const std::string history_source = read_file(
      source_root() / "src/editor/history/editor_history_service.cpp");
  EXPECT_NE(history_source.find("core/undo_redo_manager.hpp"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicBehaviorTestsUseApiOrScenarioDsl) {
    const std::vector<std::string_view> forbidden_substrings = {
            "\"editor/commands/",
            "\"editor/modeling/services/modeling_session_service.hpp\"",
            "\"editor/selection/",
            "\"editor/tools/",
            "\"api/quader_api_session_internal_access.hpp\"",
            "\"scene/scene_document.hpp\"",
            "\"scene/document/scene_session.hpp\"",
            "session.scene().",
            "session.selection().",
            "session.modeling().",
            "session().scene().",
            "session().selection().",
            "session().modeling().",
            "scenario.session()",
            "create_box_from_bounds(",
            "execute_instant_operation",
            "execute_context_action",
    };
    const std::vector<std::string_view> forbidden_identifiers = {
            "ModelingSessionService",
            "DocumentCommandService",
            "SceneDocument",
            "AssetRegistry",
            "SceneSession",
            "QuaderApiSessionInternalAccess",
    };

    for (const std::filesystem::path &file : public_behavior_test_files()) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);

      for (const std::string &line : split_lines(source)) {
        const bool contains_forbidden_identifier =
            std::any_of(forbidden_identifiers.begin(),
                        forbidden_identifiers.end(),
                        [&](std::string_view identifier) {
                          return contains_identifier_token(line, identifier);
                        });
        if (!contains_any(line, forbidden_substrings) &&
            !contains_forbidden_identifier) {
          continue;
        }
        EXPECT_TRUE(allowed_public_behavior_harness_use(relative, line))
            << relative << ": " << line;
      }
    }

    const std::string behavior_scenario =
        read_file(source_root() / "tests/support/behavior_scenario.hpp");
    EXPECT_EQ(behavior_scenario.find("SceneSession& session()"),
              std::string::npos);
    EXPECT_EQ(behavior_scenario.find("const SceneSession& session()"),
              std::string::npos);
    EXPECT_NE(behavior_scenario.find("BehaviorWhen &dispatch_viewport_shortcut"),
              std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     ModelingOperationTestsDoNotRecreateRetiredNativeRuntimeTruth) {
  for (const std::filesystem::path &file : files_with_extension(
           source_root() / "tests/modeling_domain/operation", ".cpp")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    EXPECT_EQ(source.find("EditorSession"), std::string::npos) << relative;
    EXPECT_EQ(source.find("execute_command("), std::string::npos) << relative;
    EXPECT_EQ(source.find("editor_operation_test_helpers.hpp"),
              std::string::npos)
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicApiHeadersExposeAppNamedTypes) {
  const std::vector<std::string_view> absorbed_namespaces = {
      "quader_poly::",
      "quader_editor_core::",
      "quader_io::",
      "quader_host_contract::",
  };

  for (const std::filesystem::path &file : public_api_header_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    for (const std::string &line :
         lines_containing(source, absorbed_namespaces)) {
      EXPECT_TRUE(false) << relative << ": " << line;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesAreDiscoverableAndDocsFolderIsRemoved) {
  const std::vector<std::filesystem::path> files = public_doc_and_example_files();
  EXPECT_FALSE(files.empty());
  EXPECT_FALSE(std::filesystem::exists(source_root() / "docs"));

  for (const std::filesystem::path &file : files) {
    const std::string relative = normalized_relative(file);
    EXPECT_TRUE(std::filesystem::exists(file)) << relative;
    EXPECT_TRUE(starts_with(relative, "examples/")) << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesDoNotUseResultAsChainedObjectHandle) {
  for (const std::filesystem::path &file : public_doc_and_example_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    EXPECT_EQ(source.find(".rename(\"Renamed API Box\")\n            .select()"),
              std::string::npos)
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesDoNotStartFromInternalOwnerHeaders) {
  for (const std::filesystem::path &file : public_doc_and_example_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_FALSE(contains_any(
        source,
        {
            "editor/modeling/services/modeling_session_service.hpp",
            "scene/scene_document.hpp",
            "assets/asset_registry.hpp",
            "editor/commands/document_command_service.hpp",
            "render/scene_render_bridge.hpp",
            "ui/ui_system.hpp",
            "platform/platform_window.hpp",
            "scene/document/scene_session.hpp",
            "editor/session/scene_session.hpp",
            "core/undo_redo_manager.hpp",
            "editor/selection/",
            "mesh/polygon/",
            "quader/modeling/neutral/",
            "viewport/host_viewport_contract_adapter.hpp",
            "quader_editor_core::",
            "quader_host_contract::",
            "quader_io::",
            "quader_poly::",
            "DocumentCommandService",
            "EditorSession",
            "SceneRenderBridge",
            "UndoRedoManager",
            "SceneSession",
        }))
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicCppExamplesAreCompiledOrMarkedDocumentationOnly) {
  const std::string cmake = read_file(source_root() / "CMakeLists.txt");

  for (const std::filesystem::path &file : public_cpp_example_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    EXPECT_TRUE(cmake.find(relative) != std::string::npos ||
                is_documentation_only_example(source))
        << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicCppExamplesUseCallableResultHelpers) {
  const std::vector<std::string_view> raw_result_field_patterns = {
      ".ok)",      ".ok;",        ".ok &&",      ".ok ||",  ".changed)",
      ".changed;", ".changed &&", ".changed ||", ".value.", ".message",
  };

  for (const std::filesystem::path &file : public_cpp_example_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    for (const std::string &line :
         lines_containing(source, raw_result_field_patterns)) {
      ADD_FAILURE() << relative
                    << ": use callable result helpers in public examples: "
                    << line;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests,
     PublicExamplesUseCanonicalTypedCommandSpelling) {
  const std::vector<std::string_view> raw_command_string_patterns = {
      "commands().execute(\"",
      ".shortcut_for(\"",
      ".set_shortcut(\"",
      ".remove_shortcut(\"",
      ".reset_shortcut(\"",
  };
  const std::vector<std::string_view> legacy_command_route_patterns = {
      "\"history.",
      "\"modeling.action.",
      "\"selection.",
  };
  constexpr std::string_view kTypedCommandToken = "quader_command::k";
  constexpr std::string_view kRequiredNamespace = "quader::";

  for (const std::filesystem::path &file : public_doc_and_example_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    for (const std::string &line :
         lines_containing(source, {"QuaderCommand::"})) {
      ADD_FAILURE() << relative
                    << ": use quader::quader_command::k... spelling, not "
                       "QuaderCommand::...: "
                    << line;
    }

    for (const std::string &line :
         lines_containing(source, {kTypedCommandToken})) {
      for (std::size_t position = line.find(kTypedCommandToken);
           position != std::string::npos;
           position = line.find(kTypedCommandToken,
                                position + kTypedCommandToken.size())) {
        const bool fully_qualified =
            position >= kRequiredNamespace.size() &&
            line.substr(position - kRequiredNamespace.size(),
                        kRequiredNamespace.size()) == kRequiredNamespace;
        EXPECT_TRUE(fully_qualified)
            << relative
            << ": public examples should spell typed commands as "
               "quader::quader_command::k...: "
            << line;
      }
    }

    for (const std::string &line :
         lines_containing(source, raw_command_string_patterns)) {
      ADD_FAILURE() << relative
                    << ": public examples should use typed command IDs: "
                    << line;
    }
    for (const std::string &line :
         lines_containing(source, legacy_command_route_patterns)) {
      ADD_FAILURE() << relative
                    << ": public examples should not advertise compatibility "
                       "command alias routes: "
                    << line;
    }
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicExamplesStartFromEditorApiOrScriptSurface) {
  const std::vector<std::string_view> public_entry_surfaces = {
      "QuaderEditorApi",
      "QuaderApi",
      "QuaderObjectHandle",
      "QuaderScript",
      "quader::modeling",
      "public `quader` scripting object",
      "BehaviorScenario",
  };

  for (const std::filesystem::path &file : public_doc_and_example_files()) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    EXPECT_TRUE(contains_any(source, public_entry_surfaces)) << relative;
  }
}

TEST(ProgrammaticApiGuardrailTests, PublicModelingCommandBridgeIsDescriptorDriven) {
  const std::string commands_api =
      read_file(source_root() / "src/api/quader_commands_api.cpp");
  const std::string command_descriptor = read_file(
      source_root() / "src/editor/commands/command_tool_descriptor.hpp");
  const std::string command_catalog = read_file(
      source_root() / "src/editor/commands/modeling_command_tool_catalog.cpp");

  EXPECT_EQ(commands_api.find("ModelingActionCommandBridgeDescriptor"),
            std::string::npos);
  EXPECT_EQ(commands_api.find("modelingActionCommandBridgeCatalog"),
            std::string::npos);
  EXPECT_EQ(commands_api.find("modeling_action_command_bridge_catalog"),
            std::string::npos);
  EXPECT_EQ(commands_api.find("fixedCommandIdForModelingAction"),
            std::string::npos);
  EXPECT_EQ(commands_api.find("fixed_command_id_for_modeling_action"),
            std::string::npos);
  EXPECT_EQ(commands_api.find("modeling.action."), std::string::npos);
  EXPECT_EQ(commands_api.find("publicCompatibilityAlias"), std::string::npos);
  EXPECT_EQ(commands_api.find("public_compatibility_alias"),
            std::string::npos);
  EXPECT_EQ(commands_api.find("registerAlias"), std::string::npos);
  EXPECT_EQ(commands_api.find("register_alias"), std::string::npos);
  EXPECT_EQ(commands_api.find("canonicalCommandId"), std::string::npos);
  EXPECT_EQ(commands_api.find("canonical_command_id"), std::string::npos);
  EXPECT_EQ(commands_api.find("executeModelingAction"), std::string::npos);
  EXPECT_EQ(commands_api.find("execute_modeling_action"), std::string::npos);
  EXPECT_EQ(commands_api.find("executeToolActivation"), std::string::npos);
  EXPECT_EQ(commands_api.find("execute_tool_activation"), std::string::npos);
  EXPECT_NE(commands_api.find("tool->public_command_target_id()"),
            std::string::npos);
  EXPECT_NE(command_descriptor.find("public_command_id"), std::string::npos);
  EXPECT_NE(command_descriptor.find("registers_public_command"),
            std::string::npos);
  EXPECT_EQ(command_descriptor.find("publicCompatibilityAlias"),
            std::string::npos);
  EXPECT_EQ(command_descriptor.find("public_compatibility_alias"),
            std::string::npos);
  EXPECT_NE(command_catalog.find(".public_command_id = entry.public_command_id"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, PublicCommandTargetsUseSharedPrefixOwner) {
  const std::string commands_api =
      read_file(source_root() / "src/api/quader_commands_api.cpp");
  const std::string command_tool =
      read_file(source_root() / "src/editor/commands/command_tool.hpp");
  const std::string command_registry_header = read_file(
      source_root() / "src/editor/commands/editor_command_registry.hpp");
  const std::string command_registry_source = read_file(
      source_root() / "src/editor/commands/editor_command_registry.cpp");
  const std::string viewport_adapter = read_file(
      source_root() / "src/viewport/host_viewport_contract_adapter.cpp");
  const std::string command_targets =
      read_file(source_root() / "src/core/public_command_targets.hpp");

  EXPECT_NE(command_targets.find("kQuaderModelingCommandTargetPrefix"),
            std::string::npos);
  EXPECT_NE(command_targets.find("kQuaderToolActivationCommandTargetPrefix"),
            std::string::npos);
  EXPECT_NE(
      command_targets.find("kQuaderViewportSelectionModeTargetPrefix"),
      std::string::npos);
  EXPECT_NE(commands_api.find("quader_modeling_command_target_id"),
            std::string::npos);
  EXPECT_NE(commands_api.find("quader_tool_activation_command_target_id"),
            std::string::npos);
  EXPECT_NE(commands_api.find("keybinding_target_id_for_selection_mode"),
            std::string::npos);
  EXPECT_NE(commands_api.find("quader_viewport_selection_mode_target_id"),
            std::string::npos);
  EXPECT_NE(command_tool.find("quader_modeling_command_target_id"),
            std::string::npos);
  EXPECT_NE(viewport_adapter.find("quader_tool_id_from_activation_command_target"),
            std::string::npos);

  EXPECT_EQ(commands_api.find("canonicalTargetId"), std::string::npos);
  EXPECT_EQ(commands_api.find("canonical_target_id"), std::string::npos);
  EXPECT_EQ(command_registry_header.find("registerPrefix"), std::string::npos);
  EXPECT_EQ(command_registry_header.find("register_prefix"), std::string::npos);
  EXPECT_EQ(command_registry_header.find("PrefixRoute"), std::string::npos);
  EXPECT_EQ(command_registry_header.find("prefix_route"), std::string::npos);
  EXPECT_EQ(command_registry_source.find("registerPrefix"), std::string::npos);
  EXPECT_EQ(command_registry_source.find("register_prefix"), std::string::npos);
  EXPECT_EQ(command_registry_source.find("PrefixRoute"), std::string::npos);
  EXPECT_EQ(command_registry_source.find("prefix_route"), std::string::npos);
  EXPECT_EQ(commands_api.find("registerPrefix(\"tools.activate."),
            std::string::npos);
  EXPECT_EQ(commands_api.find("register_prefix(\"tools.activate."),
            std::string::npos);
  EXPECT_EQ(command_tool.find("std::string command_id(\"modeling."),
            std::string::npos);
  EXPECT_EQ(viewport_adapter.find("std::string command_id(\"tools.activate."),
            std::string::npos);
  EXPECT_EQ(viewport_adapter.find(
                "constexpr std::string_view toolPrefix = \"tools.activate."),
            std::string::npos);
  EXPECT_EQ(viewport_adapter.find(
                "constexpr std::string_view tool_prefix = \"tools.activate."),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, DuplicateShortcutTargetsSceneAwarePublicCommand) {
  const std::string host_contract =
      read_file(source_root() / "src/editor/metadata/host_contract.cpp");
  const std::string viewport_adapter = read_file(
      source_root() / "src/viewport/host_viewport_contract_adapter.cpp");

  EXPECT_NE(host_contract.find("{\"duplicate_selection\", \"Duplicate Selection\""),
            std::string::npos);
  EXPECT_NE(host_contract.find("shortcut(\"duplicate_selection\""),
            std::string::npos);
  EXPECT_NE(viewport_adapter.find("{\"duplicate_selection\", "
                                  "quader_command::kDuplicateSelection.string()"),
            std::string::npos);
  EXPECT_EQ(host_contract.find("shortcut(\"duplicate_meshes\""),
            std::string::npos);
  EXPECT_EQ(viewport_adapter.find("{\"duplicate_meshes\", {}"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, HostViewportContractAdapterKeepsImplementationOutOfHeader) {
  const std::string root_c_make = read_file(source_root() / "CMakeLists.txt");
  const std::string adapter_header = read_file(
      source_root() / "src/viewport/host_viewport_contract_adapter.hpp");
  const std::string adapter_source = read_file(
      source_root() / "src/viewport/host_viewport_contract_adapter.cpp");

  EXPECT_NE(root_c_make.find("src/viewport/host_viewport_contract_adapter.cpp"),
            std::string::npos);
  EXPECT_NE(
      adapter_source.find("std::span<const ViewportShortcutTargetDescriptor>\n"
                          "viewport_shortcut_target_catalog()"),
      std::string::npos);
  EXPECT_NE(adapter_source.find("#include \"api/quader_command_ids.hpp\""),
            std::string::npos);
  EXPECT_NE(adapter_source.find("#include <editor/metadata/host_contract.hpp>"),
            std::string::npos);
  EXPECT_NE(
      adapter_header.find("std::span<const ViewportShortcutTargetDescriptor>\n"
                          "viewport_shortcut_target_catalog();"),
      std::string::npos);

  EXPECT_EQ(adapter_header.find("inline "), std::string::npos);
  EXPECT_EQ(adapter_header.find("#include \"api/quader_command_ids.hpp\""),
            std::string::npos);
  EXPECT_EQ(
      adapter_header.find(
          "#include \"editor/commands/modeling_command_tool_catalog.hpp\""),
      std::string::npos);
  EXPECT_EQ(adapter_header.find("#include <editor/metadata/host_contract.hpp>"),
            std::string::npos);
  EXPECT_EQ(adapter_header.find("quader_host_contract::"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ApiStatusStampsUseSharedInternalHelper) {
  const std::string status_helper =
      read_file(source_root() / "src/api/quader_api_status_internal.hpp");
  const std::string mutation_runner =
      read_file(source_root() / "src/api/quader_api_mutation_runner.cpp");
  const std::string tools_api =
      read_file(source_root() / "src/api/quader_tools_api.cpp");
  const std::string query_api =
      read_file(source_root() / "src/api/quader_query_api.cpp");
  const std::string modeling_api =
      read_file(source_root() / "src/api/quader_modeling_api.cpp");
  const std::string project_api =
      read_file(source_root() / "src/api/quader_project_api.cpp");

  EXPECT_NE(status_helper.find("quader_api_current_revisions"), std::string::npos);
  EXPECT_NE(status_helper.find("session.modeling().content_revision()"),
            std::string::npos);
  EXPECT_NE(status_helper.find("session.modeling().selection_revision()"),
            std::string::npos);
  EXPECT_EQ(status_helper.find("selection_summary()"), std::string::npos);
  EXPECT_NE(mutation_runner.find("quader_api_void_status"), std::string::npos);
  EXPECT_NE(tools_api.find("quader_api_void_status"), std::string::npos);
  EXPECT_NE(query_api.find("quader_api_current_revisions"), std::string::npos);
  EXPECT_NE(modeling_api.find("quader_api_preview_mutation_kind"),
            std::string::npos);
  EXPECT_NE(project_api.find("quader_api_failure_status"), std::string::npos);

  EXPECT_EQ(tools_api.find("QuaderToolsApi::current_revisions"),
            std::string::npos);
  EXPECT_EQ(
      tools_api.find(".modeling_content_revision = summary.content_revision"),
      std::string::npos);
  EXPECT_EQ(
      query_api.find(".modeling_content_revision = summary.content_revision"),
      std::string::npos);
  EXPECT_EQ(
      modeling_api.find(".modeling_content_revision = summary.content_revision"),
      std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     ProjectMaterialBrowserUsesThePublicProjectAssetsApi) {
  const std::string editor_api =
      read_file(source_root() / "src/api/quader_editor_api.hpp");
  const std::string project_assets_api =
      read_file(source_root() / "src/api/quader_project_assets_api.hpp");
  const std::string ui_system =
      read_file(source_root() / "src/ui/ui_system.cpp");
  const std::string browser =
      read_file(source_root() / "src/ui/qt_material_browser_window.cpp");

  EXPECT_NE(editor_api.find("QuaderProjectAssetsApi &project_assets()"),
            std::string::npos);
  EXPECT_NE(project_assets_api.find("materials() const"), std::string::npos);
  EXPECT_EQ(project_assets_api.find("active_material() const"),
            std::string::npos);
  EXPECT_EQ(project_assets_api.find("set_active_material("),
            std::string::npos);
  EXPECT_NE(project_assets_api.find("material_preview("),
            std::string::npos);
  EXPECT_NE(project_assets_api.find("material_presentation_revision() const"),
            std::string::npos);
  EXPECT_NE(project_assets_api.find("material_presentations() const"),
            std::string::npos);
  EXPECT_NE(project_assets_api.find("import_textures_as_materials("),
            std::string::npos);

  EXPECT_NE(
      ui_system.find("editor_->project_assets().material_presentations()"),
      std::string::npos);
  EXPECT_NE(ui_system.find(
                "editor_->project_assets().material_presentation_revision()"),
            std::string::npos);
  EXPECT_EQ(ui_system.find("editor_->project_assets().active_material()"),
            std::string::npos);
  EXPECT_EQ(ui_system.find("editor_->project_assets().set_active_material("),
            std::string::npos);
  EXPECT_NE(
      ui_system.find("editor_->project_assets().import_textures_as_materials("),
      std::string::npos);
  EXPECT_NE(ui_system.find("editor_->modeling().apply_material_to_selected_faces("),
            std::string::npos);
  EXPECT_EQ(ui_system.find("material_catalog()"), std::string::npos);
  EXPECT_EQ(browser.find("ProjectSession"), std::string::npos);
  EXPECT_EQ(browser.find("AssetRegistry"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     MaterialReferenceIntentUsesTypedPublicApiAndMenuOnlyPresentation) {
  const std::filesystem::path root = source_root();
  const std::string editor_api =
      read_file(root / "src/api/quader_editor_api.hpp");
  const std::string api_types =
      read_file(root / "src/api/quader_api_types.hpp");
  const std::string materials_api =
      read_file(root / "src/api/quader_materials_api.hpp");
  const std::string materials_api_source =
      read_file(root / "src/api/quader_materials_api.cpp");
  const std::string ui_system =
      read_file(root / "src/ui/ui_system.cpp");
  const std::string main_menu =
      read_file(root / "src/ui/qt_main_menu.cpp");

  EXPECT_NE(editor_api.find("#include \"api/quader_materials_api.hpp\""),
            std::string::npos);
  EXPECT_NE(editor_api.find("QuaderMaterialsApi &materials()"),
            std::string::npos);
  EXPECT_NE(editor_api.find("const QuaderMaterialsApi &materials() const"),
            std::string::npos);

  for (std::string_view reference_type : {
           "enum class QuaderMaterialReferenceKind",
           "struct QuaderMaterialReference final",
       }) {
    EXPECT_NE(api_types.find(reference_type), std::string::npos)
        << reference_type;
  }
  for (std::string_view typed_surface : {
           "enum class QuaderMaterialUsageScope",
           "class QuaderMaterialsApi final",
           "used_materials(QuaderMaterialUsageScope scope) const",
           "replace_material_references(",
       }) {
    EXPECT_NE(materials_api.find(typed_surface), std::string::npos)
        << typed_surface;
  }
  EXPECT_NE(materials_api.find("QuaderApiResult<std::vector<"
                               "QuaderMaterialReference>>"),
            std::string::npos);
  EXPECT_NE(materials_api.find("QuaderApiVoidResult"), std::string::npos);
  EXPECT_EQ(materials_api.find("assets/material_reference.hpp"),
            std::string::npos);
  EXPECT_EQ(materials_api.find("ProjectMaterialCatalog"),
            std::string::npos);
  EXPECT_EQ(materials_api.find("MaterialDocument"), std::string::npos);

  EXPECT_NE(materials_api_source.find("MaterialReferenceService service("),
            std::string::npos);
  EXPECT_NE(materials_api_source.find("service.used_materials("),
            std::string::npos);
  EXPECT_NE(materials_api_source.find("service.prepare_replacement("),
            std::string::npos);
  EXPECT_NE(materials_api_source.find("run_modeling_undoable_command("),
            std::string::npos);
  EXPECT_NE(ui_system.find("editor_->materials().used_materials("),
            std::string::npos);
  EXPECT_NE(
      ui_system.find("editor_->materials().replace_material_references("),
      std::string::npos);

  const std::string find_replace_action = source_slice(
      main_menu, "material_find_replace_action = materials->addAction(",
      "(void)add_presented_submenu(*menu, \"ui.menu.object.visibility\"");
  ASSERT_FALSE(find_replace_action.empty());
  EXPECT_NE(find_replace_action.find(
                "ui.menu.object.materials.find_replace"),
            std::string::npos);
  EXPECT_NE(find_replace_action.find("context.show_material_find_replace"),
            std::string::npos);
  EXPECT_EQ(find_replace_action.find("add_command_action("),
            std::string::npos);
  EXPECT_EQ(find_replace_action.find("quader_command::"),
            std::string::npos);
  EXPECT_EQ(find_replace_action.find("commands()."), std::string::npos);

  for (const std::filesystem::path &command_surface : {
           root / "src/api/quader_command_ids.hpp",
           root / "src/api/quader_commands_api.cpp",
           root / "src/editor/commands/editor_command_registry.cpp",
           root / "tests/support/command_behavior_contract.cpp",
           root / "tests/baselines/command_behavior.json",
       }) {
    const std::string source = read_file(command_surface);
    EXPECT_EQ(source.find("material.find_replace"), std::string::npos)
        << command_surface.string();
    EXPECT_EQ(source.find("material_find_replace"), std::string::npos)
        << command_surface.string();
    EXPECT_EQ(source.find("kEditFindReplace"), std::string::npos)
        << command_surface.string();
  }
}

TEST(ProgrammaticApiGuardrailTests,
     StandardMaterialEditorUsesTypedPublicDocumentApi) {
  const std::string editor_api =
      read_file(source_root() / "src/api/quader_editor_api.hpp");
  const std::string documents_api = read_file(
      source_root() / "src/api/quader_standard_material_documents_api.hpp");
  const std::string ui_system =
      read_file(source_root() / "src/ui/ui_system.cpp");
  const std::string editor_window = read_file(
      source_root() / "src/ui/qt_standard_material_editor_window.cpp");

  EXPECT_NE(editor_api.find("standard_material_documents()"),
            std::string::npos);
  for (const std::string_view operation : {
           "new_material()", "preview() const", "open(ProjectAssetId",
           "set_base_color(",
           "set_normal_convention(", "import_texture(", "save_as(",
       }) {
    EXPECT_NE(documents_api.find(operation), std::string::npos) << operation;
  }
  EXPECT_NE(ui_system.find("editor_->standard_material_documents()"),
            std::string::npos);
  EXPECT_EQ(editor_window.find("ProjectSession"), std::string::npos);
  EXPECT_EQ(editor_window.find("ProjectMaterialCatalog"), std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     RemovedExtendToolSurfaceCannotBeReintroduced) {
  const std::filesystem::path root = source_root();
  for (const std::filesystem::path &removed_path : {
           root / "src/editor/interactions/extend_tool.cpp",
           root / "src/editor/interactions/extend_tool.hpp",
           root / "src/editor/tools/interactive/extend_tool.hpp",
       }) {
    EXPECT_FALSE(std::filesystem::exists(removed_path))
        << removed_path.string();
  }

  for (const std::pair<std::filesystem::path, std::string_view> &surface : {
           std::pair{root / "src/api/quader_viewport_input_types.hpp",
                     std::string_view{"enum class ViewportToolId"}},
           std::pair{root / "src/api/quader_api_types.hpp",
                     std::string_view{"enum class QuaderViewportTool"}},
           std::pair{root / "src/editor/modeling/modeling_types.hpp",
                     std::string_view{"enum class ModelingViewportTool"}},
           std::pair{root / "src/editor/interactions/tool_identity.hpp",
                     std::string_view{"enum class ViewportInteractionTool"}},
       }) {
    const std::string source = read_file(surface.first);
    const std::string enum_surface =
        source_slice(source, surface.second, "};");
    ASSERT_FALSE(enum_surface.empty()) << surface.first.string();
    EXPECT_EQ(enum_surface.find("Extend"), std::string::npos)
        << surface.first.string();
  }

  const std::string command_ids =
      read_file(root / "src/api/quader_command_ids.hpp");
  const std::string sdk_header = read_file(
      root / "src/sdk/core/modeling/include/quader/modeling/api/modeling_api.hpp");
  const std::string sdk_tools =
      read_file(root / "src/sdk/core/modeling/src/tools_api.cpp");
  const std::string modeling_cmake =
      read_file(root / "cmake/QuaderModeling.cmake");
  const std::string english_locale =
      read_file(root / "resources/locales/en-US.lang");
  const std::string italian_locale =
      read_file(root / "resources/locales/it-IT.lang");

  EXPECT_EQ(command_ids.find("tools.activate.extend"), std::string::npos);
  EXPECT_EQ(sdk_header.find("activate_extend_tool"), std::string::npos);
  EXPECT_EQ(sdk_tools.find("\"extend\""), std::string::npos);
  EXPECT_EQ(modeling_cmake.find("extend_tool"), std::string::npos);
  EXPECT_EQ(english_locale.find("viewport.tool.extend.label"),
            std::string::npos);
  EXPECT_EQ(italian_locale.find("viewport.tool.extend.label"),
            std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests,
     ActiveSceneIdentityDoesNotBuildSceneDescriptionsForViewportPolling) {
  const std::string source = read_file(
      source_root() / "src/api/quader_scene_documents_api.cpp");
  const std::string identity = source_slice(
      source, "QuaderSceneDocumentsApi::active_scene_id() const",
      "\n}");
  ASSERT_FALSE(identity.empty());
  EXPECT_EQ(identity.find("active_scene()"), std::string::npos);
  EXPECT_EQ(identity.find("open_scenes()"), std::string::npos);
  EXPECT_EQ(identity.find("dirty()"), std::string::npos);
  EXPECT_EQ(identity.find("QuaderSceneDocumentInfo"), std::string::npos);
}
