////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
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
         relative == "src/api/quader_api_session_internal_access.hpp" ||
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
  (void)relative;
  (void)line;
  return false;
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
  EXPECT_EQ(handle.find("ProjectSession"), std::string::npos);
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
           std::string_view{"kSelectionClear"},
           std::string_view{"kSelectionSelectAll"},
           std::string_view{"kSelectionInvert"},
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
      {"edit.clear_selection", "kEditClearSelection", {"kSelectionClear"}},
      {"edit.select_all", "kEditSelectAll", {"kSelectionSelectAll"}},
      {"edit.invert_selection", "kEditInvertSelection", {"kSelectionInvert"}},
      {"edit.delete", "kDeleteSelection", {"kModelingDeleteSelection"}},
      {"edit.apply_material",
       "kApplyMaterial",
       {"kModelingApplyActiveMaterial"}},
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
             "ProjectSession",
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

TEST(ProgrammaticApiGuardrailTests, ReadOnlyApiPathsUseConstProjectSessionAccess) {
  const std::string internal_access = read_file(
      source_root() / "src/api/quader_api_session_internal_access.hpp");
  const std::string query_api =
      read_file(source_root() / "src/api/quader_query_api.cpp");
  const std::string render_api =
      read_file(source_root() / "src/api/quader_render_api.cpp");
  const std::string history_api =
      read_file(source_root() / "src/api/quader_history_api.cpp");

  EXPECT_NE(internal_access.find("const ProjectSession &"),
            std::string::npos);
  EXPECT_NE(internal_access.find("read_project_session("),
            std::string::npos);
  for (const std::string &source : {query_api, render_api, history_api}) {
    EXPECT_NE(source.find("read_project_session("), std::string::npos);
    EXPECT_EQ(source.find("ProjectSession &project_session("),
              std::string::npos);
  }
  EXPECT_NE(render_api.find(".read_view()"), std::string::npos);
  EXPECT_EQ(render_api.find("build_render_scene_snapshot(session.scene, session.assets)"),
            std::string::npos);
  EXPECT_EQ(render_api.find("bridge.sync(build_render_scene_snapshot("),
            std::string::npos);
  EXPECT_NE(render_api.find("bridge.sync(session.scene, session.assets)"),
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
                "project_session(session_).selection().edit_scene_selection("),
            std::string::npos);
  EXPECT_NE(service_source.find("EditorSelectionService::edit_scene_selection("),
            std::string::npos);
  EXPECT_NE(service_source.find("resolve_scene_selection_edit("),
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
  EXPECT_NE(mutation_runner.find("selection_mode_uses_scene_document("),
            std::string::npos);
  EXPECT_EQ(service_source.find("sceneDocumentOwnsSelectionMode"),
            std::string::npos);
  EXPECT_EQ(delete_source.find(
                "scene_aware_delete_uses_document_route(ModelingSelectionMode mode) "
                "{\n    return mode == ModelingSelectionMode::Object"),
            std::string::npos);
  EXPECT_NE(selection_owner.find("user_selectable_scene_entities_for_mode"),
            std::string::npos);
  EXPECT_NE(selection_owner.find("scene.is_scene_root(entity)"),
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
  EXPECT_NE(command.find("apply_scene_selection(scene(), before_, mode_)"),
            std::string::npos);
  EXPECT_NE(command.find("apply_scene_selection(scene(), after_, mode_)"),
            std::string::npos);

  EXPECT_NE(document_service.find("SceneSelectionApplicationMode mode"),
            std::string::npos);
  EXPECT_NE(document_service.find("scene_, std::move(entities), active,\n"
                                  "                                      mode"),
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
  EXPECT_NE(modeling_selection_header.find("box_pick_object_ids("),
            std::string::npos);
  EXPECT_NE(modeling_selection_source.find(
                "rectangle_hits_from_projected_candidates"),
            std::string::npos);
  EXPECT_NE(modeling_session_header.find("pick_object_entity("),
            std::string::npos);
  EXPECT_NE(modeling_session_header.find("box_pick_object_entities("),
            std::string::npos);
  EXPECT_NE(modeling_session_source.find("selection_service_.pick_object_id("),
            std::string::npos);
  EXPECT_NE(modeling_session_source.find("selection_service_.box_pick_object_ids("),
            std::string::npos);
  EXPECT_NE(editor_selection_source.find("modeling_.pick_object_entity(request)"),
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
      "modeling.apply_active_material(",
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
      "context.modeling.apply_active_material(",
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
     QtToolSettingsWindowWritesSettingsThroughPublicToolsApi) {
  const std::string panel =
      read_file(source_root() / "src/ui/qt_tool_settings_window.cpp");

  EXPECT_NE(panel.find("editor_->tools().set_setting("), std::string::npos);
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
  EXPECT_NE(settings_source.find("ui_panel_stable_id(UIPanelId::Outliner)"),
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
  EXPECT_NE(outliner.find("editor_->scene().set_visible("),
            std::string::npos);
  EXPECT_NE(outliner.find("editor_->scene().set_locked("),
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

TEST(ProgrammaticApiGuardrailTests, RepeatedCodecAndCutHelpersStayWithTheirOwners) {
  const std::string codecs_header =
      read_file(source_root() / "src/io/qdr/qdr_project_codecs.hpp");
  const std::string codecs_source =
      read_file(source_root() / "src/io/qdr/qdr_project_codecs.cpp");
  const std::string qdr_reader =
      read_file(source_root() / "src/io/qdr/qdr_document_reader.cpp");
  const std::string qdr_writer =
      read_file(source_root() / "src/io/qdr/qdr_document_writer.cpp");
  const std::string session_ops = read_file(
      source_root() / "src/editor/modeling/runtime/session_operations.cpp");

  EXPECT_NE(codecs_header.find("qdr_scene_tone_mapping_name"), std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_scene_tone_mapping_from_name"),
            std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_transform_component_to_json"),
            std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_transform_component_from_json"),
            std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_mesh_component_to_json"), std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_mesh_component_from_json"), std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_material_component_to_json"),
            std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_material_component_from_json"),
            std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_light_component_to_json"), std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_light_component_from_json"), std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_scene_settings_component_to_json"),
            std::string::npos);
  EXPECT_NE(codecs_header.find("qdr_scene_settings_component_from_json"),
            std::string::npos);
  EXPECT_NE(codecs_source.find("qdr_scene_tone_mapping_name"), std::string::npos);
  EXPECT_NE(codecs_source.find("qdr_scene_tone_mapping_from_name"),
            std::string::npos);
  EXPECT_NE(codecs_source.find("qdr_transform_component_to_json"),
            std::string::npos);
  EXPECT_NE(codecs_source.find("qdr_scene_settings_component_from_json"),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("SceneToneMapping sceneToneMappingFromName"),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("SceneToneMapping scene_tone_mapping_from_name"),
            std::string::npos);
  EXPECT_EQ(qdr_writer.find("sceneToneMappingName(SceneToneMapping"),
            std::string::npos);
  EXPECT_EQ(qdr_writer.find("scene_tone_mapping_name(SceneToneMapping"),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("TransformComponent transform_component_from_json"),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("transformComponentFromJson"), std::string::npos);
  EXPECT_EQ(qdr_reader.find("MeshComponent mesh_component_from_json"),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("meshComponentFromJson"), std::string::npos);
  EXPECT_EQ(qdr_reader.find("MaterialComponent material_component_from_json"),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("materialComponentFromJson"), std::string::npos);
  EXPECT_EQ(qdr_reader.find("LightComponent light_component_from_json"),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("lightComponentFromJson"), std::string::npos);
  EXPECT_EQ(qdr_writer.find(
                "nlohmann::ordered_json transform_component_to_json"),
            std::string::npos);
  EXPECT_EQ(qdr_writer.find("transformComponentToJson"), std::string::npos);
  EXPECT_EQ(qdr_writer.find("nlohmann::ordered_json mesh_component_to_json"),
            std::string::npos);
  EXPECT_EQ(qdr_writer.find("meshComponentToJson"), std::string::npos);
  EXPECT_EQ(qdr_writer.find(
                "nlohmann::ordered_json material_component_to_json"),
            std::string::npos);
  EXPECT_EQ(qdr_writer.find("materialComponentToJson"), std::string::npos);
  EXPECT_EQ(qdr_writer.find("nlohmann::ordered_json light_component_to_json"),
            std::string::npos);
  EXPECT_EQ(qdr_writer.find("lightComponentToJson"), std::string::npos);
  EXPECT_EQ(session_ops.find("to_plane_cut_mode("), std::string::npos);
  EXPECT_EQ(session_ops.find("plane_cut_request_from_state("),
            std::string::npos);
  EXPECT_NE(session_ops.find("plane_cut_request_from_cut_state(state)"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/editor/modeling/runtime/cut_policy.hpp"));
}

TEST(ProgrammaticApiGuardrailTests, EditorHistoryBoundaryHidesCoreUndoRedoManager) {
  const std::vector<std::filesystem::path> editor_history_clients = {
      source_root() / "src/api/quader_api.cpp",
      source_root() / "src/api/quader_scene_api.cpp",
      source_root() / "src/scene/document/project_session.hpp",
      source_root() / "src/editor/session/project_session.hpp",
      source_root() / "src/editor/session/project_session.cpp",
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
    const std::vector<std::string_view> forbidden = {
            "\"editor/commands/",
            "\"editor/modeling/services/modeling_session_service.hpp\"",
            "\"editor/selection/",
            "\"editor/tools/",
            "\"api/quader_api_session_internal_access.hpp\"",
            "\"scene/scene_document.hpp\"",
            "\"scene/document/project_session.hpp\"",
            "ModelingSessionService",
            "DocumentCommandService",
            "SceneDocument",
            "AssetRegistry",
            "ProjectSession",
            "QuaderApiSessionInternalAccess",
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

    for (const std::filesystem::path &file : public_behavior_test_files()) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);

      for (const std::string &line : lines_containing(source, forbidden)) {
        EXPECT_TRUE(allowed_public_behavior_harness_use(relative, line))
            << relative << ": " << line;
      }
    }

    const std::string behavior_scenario =
        read_file(source_root() / "tests/support/behavior_scenario.hpp");
    EXPECT_EQ(behavior_scenario.find("ProjectSession& session()"),
              std::string::npos);
    EXPECT_EQ(behavior_scenario.find("const ProjectSession& session()"),
              std::string::npos);
    EXPECT_NE(behavior_scenario.find("BehaviorWhen &dispatch_viewport_shortcut"),
              std::string::npos);
}

TEST(ProgrammaticApiGuardrailTests, ModelingOperationTestsUseSharedRuntimeHelpers) {
  const std::vector<std::string_view> duplicated_helpers = {
      "find_object(const quader_editor_core::EditorSession&",
      "require_object(const quader_editor_core::EditorSession&",
      "selected_face_count(const",
      "selected_vertex_count(const",
      "selected_edge_count(const",
  };

  for (const std::filesystem::path &file : files_with_extension(
           source_root() / "tests/modeling_domain/operation", ".cpp")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    for (const std::string &line :
         lines_containing(source, duplicated_helpers)) {
      ADD_FAILURE() << relative
                    << ": move duplicated runtime test helper to "
                       "editor_operation_test_helpers.hpp: "
                    << line;
    }
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
            "scene/document/project_session.hpp",
            "editor/session/project_session.hpp",
            "core/undo_redo_manager.hpp",
            "editor/selection/",
            "mesh/polygon/",
            "viewport/host_viewport_contract_adapter.hpp",
            "quader_editor_core::",
            "quader_host_contract::",
            "quader_io::",
            "quader_poly::",
            "DocumentCommandService",
            "EditorSession",
            "SceneRenderBridge",
            "UndoRedoManager",
            "ProjectSession",
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
  EXPECT_NE(commands_api.find("quader_modeling_command_target_id"),
            std::string::npos);
  EXPECT_NE(commands_api.find("quader_tool_activation_command_target_id"),
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
