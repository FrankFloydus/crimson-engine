////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_file_system.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_types.hpp"
#include "io/project/recent_project_store.hpp"
#include "quader_app_version.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace quader;

namespace {

std::filesystem::path unique_temp_directory(std::string_view label) {
  const auto tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() /
         ("quader-project-" + std::string(label) + "-" +
          std::to_string(tick));
}

/// Owns an isolated temporary directory for project-storage tests.
class ScopedProjectTestDirectory final {
public:
  explicit ScopedProjectTestDirectory(std::string_view label)
      : path_(unique_temp_directory(label)) {
    std::error_code error;
    std::filesystem::create_directories(path_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ~ScopedProjectTestDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

void write_text_file(const std::filesystem::path &path,
                     std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  stream.close();
  ASSERT_TRUE(stream.good()) << path.string();
}

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

TEST(ProjectIoResultTests, VoidSuccessCarriesNonFatalWarning) {
  ProjectIoResult<void> result =
      ProjectIoResult<void>::success("obsolete backup remains");

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.warning(), "obsolete backup remains");
  EXPECT_EQ(result.take_warning(), "obsolete backup remains");
  EXPECT_TRUE(result.warning().empty());
}

TEST(ProjectWorkspaceTypes, EnforcesUuidAndGenerationalIdentities) {
  const ProjectId project_id = ProjectId::generate();
  const SceneId scene_id = SceneId::generate();
  ASSERT_TRUE(project_id.valid());
  ASSERT_TRUE(scene_id.valid());

  const ProjectIoResult<ProjectId> parsed_project =
      ProjectId::parse(project_id.str());
  const ProjectIoResult<SceneId> parsed_scene = SceneId::parse(scene_id.str());
  ASSERT_TRUE(parsed_project.ok()) << parsed_project.error().message;
  ASSERT_TRUE(parsed_scene.ok()) << parsed_scene.error().message;
  EXPECT_EQ(parsed_project.value(), project_id);
  EXPECT_EQ(parsed_scene.value(), scene_id);
  EXPECT_FALSE(ProjectId::parse("not-a-uuid").ok());

  const SceneSessionId first = SceneSessionId::from_parts(4u, 1u);
  const SceneSessionId recycled = SceneSessionId::from_parts(4u, 2u);
  EXPECT_TRUE(first.valid());
  EXPECT_TRUE(recycled.valid());
  EXPECT_NE(first, recycled);
  EXPECT_FALSE(SceneSessionId::from_parts(4u, 0u).valid());

  EXPECT_FALSE(ProjectRelativePath::parse("../outside.qdr").ok());
  const ProjectIoResult<ProjectRelativePath> normalized =
      ProjectRelativePath::parse("Scenes/./room.qdr");
  ASSERT_TRUE(normalized.ok());
  EXPECT_EQ(normalized.value().portable_string(), "Scenes/room.qdr");
}

TEST(ProjectWorkspaceTypes, UnicodeNativePathsRoundTripThroughPortableUtf8AndManifest) {
  const std::filesystem::path native(L"Scenes/Caf\u00e9 \u6771\u4eac.qdr");
  const std::string expected_utf8 = "Scenes/Caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac.qdr";
  const auto relative = ProjectRelativePath::parse(native);
  ASSERT_TRUE(relative.ok()) << relative.error().message;
  EXPECT_EQ(relative.value().portable_string(), expected_utf8);
  const auto decoded = ProjectRelativePath::parse_portable(expected_utf8);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  EXPECT_EQ(decoded.value().native_path(), native);
  EXPECT_EQ(decoded.value(), relative.value());

  auto manifest = ProjectManifest::create(ProjectId::generate(), "Unicode Scenes");
  ASSERT_TRUE(manifest.ok()) << manifest.error().message;
  const SceneId scene_id = SceneId::generate();
  ASSERT_TRUE(manifest.value().register_scene({scene_id, relative.value()}).ok());
  ASSERT_TRUE(manifest.value().set_workspace({relative.value()}, relative.value()).ok());
  const auto restored = ProjectManifestStore::deserialize(
      ProjectManifestStore::serialize(manifest.value()));
  ASSERT_TRUE(restored.ok()) << restored.error().message;
  const auto *scene = restored.value().find_scene(scene_id);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(scene->path.native_path(), native);
  EXPECT_EQ(restored.value().active_scene_path(), relative.value());
}

TEST(ProjectWorkspaceTypes, InvalidPortableUtf8ReturnsAnError) {
  ProjectIoResult<ProjectRelativePath> result =
      ProjectIoResult<ProjectRelativePath>::success({});
  ASSERT_NO_THROW(result = ProjectRelativePath::parse_portable("Scenes/invalid\xff.qdr"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().code, ProjectIoErrorCode::InvalidRelativePath);
}

TEST(ProjectManifestStore, CreatesOnlyInsideAnExactlyEmptyFolder) {
  ScopedProjectTestDirectory temporary("create");
  const std::filesystem::path project_root = temporary.path() / "Museum";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));

  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;
  EXPECT_EQ(created.value().manifest.name(), "Museum");
  EXPECT_EQ(created.value().manifest.last_saved_app_version(), kQuaderAppVersion);
  EXPECT_EQ(created.value().manifest_path.filename(), "Museum.qproj");
  EXPECT_TRUE(std::filesystem::is_regular_file(created.value().manifest_path));
  EXPECT_TRUE(
      std::filesystem::is_directory(created.value().file_system.root() / "Scenes"));

  ProjectIoResult<ProjectWorkspaceStorage> reopened =
      ProjectManifestStore::open(created.value().manifest_path);
  ASSERT_TRUE(reopened.ok()) << reopened.error().message;
  EXPECT_EQ(reopened.value().manifest.id(), created.value().manifest.id());
  EXPECT_EQ(reopened.value().manifest.last_saved_app_version(), kQuaderAppVersion);
  EXPECT_TRUE(reopened.value().manifest.scenes().empty());
  EXPECT_TRUE(reopened.value().manifest.open_scene_paths().empty());

  const std::filesystem::path occupied_root = temporary.path() / "Occupied";
  ASSERT_TRUE(std::filesystem::create_directory(occupied_root));
  write_text_file(occupied_root / "keep.txt", "user-owned");
  const ProjectIoResult<ProjectWorkspaceStorage> rejected =
      ProjectManifestStore::create_in_empty_directory(occupied_root);
  ASSERT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.error().code, ProjectIoErrorCode::RootNotEmpty);
  EXPECT_TRUE(std::filesystem::is_regular_file(occupied_root / "keep.txt"));
}

TEST(ProjectManifestStore, AdoptsChosenFolderWithoutReplacingExistingContent) {
  ScopedProjectTestDirectory temporary("adopt-folder");
  const auto root = temporary.path() / "Existing Assets";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  ASSERT_TRUE(std::filesystem::create_directory(root / "Scenes"));
  write_text_file(root / "source.obj", "authored model bytes");
  write_text_file(root / "Scenes" / "notes.txt", "authored scene notes");

  const auto created = ProjectManifestStore::create_in_directory(root, true);
  ASSERT_TRUE(created.ok()) << created.error().message;
  EXPECT_EQ(created.value().file_system.root(), std::filesystem::canonical(root));
  EXPECT_EQ(created.value().manifest_path, root / "Existing Assets.qproj");
  EXPECT_FALSE(created.value().created_scenes_directory);
  EXPECT_EQ(read_text_file(root / "source.obj"), "authored model bytes");
  EXPECT_EQ(read_text_file(root / "Scenes" / "notes.txt"), "authored scene notes");

  const auto found = ProjectManifestStore::manifest_in_directory(root);
  ASSERT_TRUE(found.ok()) << found.error().message;
  ASSERT_TRUE(found.value().has_value());
  EXPECT_EQ(*found.value(), created.value().manifest_path);
  const auto duplicate = ProjectManifestStore::create_in_directory(root, true);
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(duplicate.error().code, ProjectIoErrorCode::DuplicateAsset);
}

TEST(ProjectManifestStore, FolderDiscoveryRejectsAmbiguousAndNonFileManifests) {
  ScopedProjectTestDirectory temporary("folder-manifests");
  const auto root = temporary.path();
  auto found = ProjectManifestStore::manifest_in_directory(root);
  ASSERT_TRUE(found.ok()) << found.error().message;
  EXPECT_FALSE(found.value().has_value());
  write_text_file(root / "first.qproj", "keep first");
  write_text_file(root / "second.QPROJ", "keep second");
  found = ProjectManifestStore::manifest_in_directory(root);
  ASSERT_FALSE(found.ok());
  EXPECT_EQ(found.error().code, ProjectIoErrorCode::DuplicateAsset);
  EXPECT_FALSE(ProjectManifestStore::create_in_directory(root, true).ok());
  EXPECT_FALSE(std::filesystem::exists(root / "Scenes"));
  ASSERT_TRUE(std::filesystem::remove(root / "second.QPROJ"));
  ASSERT_TRUE(std::filesystem::remove(root / "first.qproj"));
  ASSERT_TRUE(std::filesystem::create_directory(root / "directory.qproj"));
  found = ProjectManifestStore::manifest_in_directory(root);
  ASSERT_FALSE(found.ok());
  EXPECT_EQ(found.error().code, ProjectIoErrorCode::FileNotRegular);
}

TEST(ProjectManifestStore, InitialPublicationNeverReplacesCompetingManifest) {
  ScopedProjectTestDirectory temporary("manifest-create-new");
  const auto created = ProjectManifestStore::create_in_directory(temporary.path(), true);
  ASSERT_TRUE(created.ok()) << created.error().message;
  write_text_file(created.value().manifest_path, "external replacement");
  const auto refused = ProjectManifestStore::save_atomic(created.value(), true);
  ASSERT_FALSE(refused.ok());
  std::ifstream input(created.value().manifest_path, std::ios::binary);
  const std::string retained{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  EXPECT_EQ(retained, "external replacement");
}

TEST(ProjectFileSystem, RejectsPortableCaseCollisionsAndLinkEscapes) {
  ScopedProjectTestDirectory temporary("paths");
  const std::filesystem::path project_root = temporary.path() / "Paths";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;

  const std::filesystem::path existing_scene =
      created.value().file_system.root() / "Scenes" / "Scene.qdr";
  write_text_file(existing_scene, "QDRB");
  ProjectIoResult<ProjectRelativePath> mismatched_case =
      ProjectRelativePath::parse("Scenes/scene.qdr");
  ASSERT_TRUE(mismatched_case.ok());
  const ProjectIoResult<std::filesystem::path> case_result =
      created.value().file_system.resolve(mismatched_case.value());
  ASSERT_FALSE(case_result.ok());
  EXPECT_EQ(case_result.error().code, ProjectIoErrorCode::PathCaseCollision);
  const ProjectIoResult<ProjectRelativePath> absolute_case_result =
      created.value().file_system.relative_from_absolute(
          created.value().file_system.root() / "Scenes" / "scene.qdr");
  ASSERT_FALSE(absolute_case_result.ok());
  EXPECT_EQ(absolute_case_result.error().code,
            ProjectIoErrorCode::PathCaseCollision);

  ProjectIoResult<ProjectRelativePath> first =
      ProjectRelativePath::parse("Scenes/Room.qdr");
  ProjectIoResult<ProjectRelativePath> second =
      ProjectRelativePath::parse("scenes/room.qdr");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const std::vector<ProjectRelativePath> colliding_paths{
      first.value(), second.value()};
  const ProjectIoResult<void> collision =
      created.value().file_system.validate_no_case_collisions(colliding_paths);
  ASSERT_FALSE(collision.ok());
  EXPECT_EQ(collision.error().code, ProjectIoErrorCode::PathCaseCollision);

  const std::filesystem::path outside = temporary.path() / "Outside";
  ASSERT_TRUE(std::filesystem::create_directory(outside));
  const ProjectIoResult<ProjectRelativePath> outside_result =
      created.value().file_system.relative_from_absolute(outside / "escape.qdr");
  ASSERT_FALSE(outside_result.ok());
  EXPECT_EQ(outside_result.error().code, ProjectIoErrorCode::PathOutsideProject);
  const std::filesystem::path link =
      created.value().file_system.root() / "Scenes" / "External";
  std::error_code link_error;
  std::filesystem::create_directory_symlink(outside, link, link_error);
  if (!link_error) {
    ProjectIoResult<ProjectRelativePath> escaped =
        ProjectRelativePath::parse("Scenes/External/escape.qdr");
    ASSERT_TRUE(escaped.ok());
    const ProjectIoResult<std::filesystem::path> escape_result =
        created.value().file_system.resolve(escaped.value());
    ASSERT_FALSE(escape_result.ok());
    EXPECT_EQ(escape_result.error().code, ProjectIoErrorCode::SymlinkEscape);
    const ProjectIoResult<ProjectRelativePath> absolute_escape_result =
        created.value().file_system.relative_from_absolute(link / "escape.qdr");
    ASSERT_FALSE(absolute_escape_result.ok());
    EXPECT_EQ(absolute_escape_result.error().code,
              ProjectIoErrorCode::PathOutsideProject);
  }

  const std::filesystem::path internal_link =
      created.value().file_system.root() / "SceneAlias";
  link_error.clear();
  std::filesystem::create_directory_symlink(
      created.value().file_system.root() / "Scenes", internal_link,
      link_error);
  if (!link_error) {
    const ProjectIoResult<ProjectRelativePath> relative_alias =
        created.value().file_system.relative_from_absolute(
            internal_link / "Scene.qdr");
    ASSERT_TRUE(relative_alias.ok()) << relative_alias.error().message;
    EXPECT_EQ(relative_alias.value().portable_string(), "SceneAlias/Scene.qdr");
    ProjectIoResult<ProjectRelativePath> original =
        ProjectRelativePath::parse("Scenes/Scene.qdr");
    ProjectIoResult<ProjectRelativePath> alias =
        ProjectRelativePath::parse("SceneAlias/Scene.qdr");
    ASSERT_TRUE(original.ok());
    ASSERT_TRUE(alias.ok());
    const ProjectIoResult<bool> same_location =
        created.value().file_system.refers_to_same_location(original.value(),
                                                             alias.value());
    ASSERT_TRUE(same_location.ok()) << same_location.error().message;
    EXPECT_TRUE(same_location.value());
    const std::array<ProjectRelativePath, 2> alias_paths{original.value(),
                                                         alias.value()};
    const ProjectIoResult<void> alias_collision =
        created.value().file_system.validate_no_path_aliases(alias_paths);
    ASSERT_FALSE(alias_collision.ok());
    EXPECT_EQ(alias_collision.error().code,
              ProjectIoErrorCode::PathCaseCollision);
  }
}

TEST(ProjectFileSystem, ScopedWritesAllowExistingAndNewFilesAtAnyDepth) {
  ScopedProjectTestDirectory temporary("scoped-writes");
  const std::filesystem::path scenes = temporary.path() / "Scenes";
  ASSERT_TRUE(std::filesystem::create_directory(scenes));
  const auto opened = ProjectFileSystem::open(temporary.path());
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  const ProjectFileSystem &files = opened.value();
  const auto directory = ProjectRelativePath::parse("Scenes");
  ASSERT_TRUE(directory.ok());

  write_text_file(scenes / "Existing.qdr", "QDRB");
  for (const char *name : {"Scenes/Existing.qdr", "Scenes/New.qdr"}) {
    const auto path = ProjectRelativePath::parse(name);
    ASSERT_TRUE(path.ok());
    const auto resolved =
        files.resolve_for_write_in_directory(path.value(), directory.value());
    ASSERT_TRUE(resolved.ok()) << resolved.error().message;
    EXPECT_EQ(resolved.value(), files.root() / path.value().native_path());
  }

  std::filesystem::path nested = "Scenes";
  for (std::size_t level = 0u; level < 16u; ++level) {
    nested /= "L" + std::to_string(level);
  }
  ASSERT_TRUE(std::filesystem::create_directories(files.root() / nested));
  const auto path = ProjectRelativePath::parse(nested / "Deep.qdr");
  ASSERT_TRUE(path.ok());
  const auto resolved =
      files.resolve_for_write_in_directory(path.value(), directory.value());
  ASSERT_TRUE(resolved.ok()) << resolved.error().message;
  EXPECT_EQ(resolved.value(), files.root() / nested / "Deep.qdr");
  EXPECT_FALSE(std::filesystem::exists(resolved.value()));

  const auto nested_directory = ProjectRelativePath::parse("Scenes/L0/L1");
  ASSERT_TRUE(nested_directory.ok());
  EXPECT_TRUE(files.resolve_for_write_in_directory(path.value(),
                                                   nested_directory.value())
                  .ok());
}

TEST(ProjectFileSystem, ScopedWritesRequireStrictComponentContainment) {
  ScopedProjectTestDirectory temporary("scoped-boundary");
  for (const char *folder : {"Scenes", "ScenesBackup", "Materials"}) {
    ASSERT_TRUE(std::filesystem::create_directory(temporary.path() / folder));
  }
  const auto opened = ProjectFileSystem::open(temporary.path());
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  const ProjectFileSystem &files = opened.value();
  const auto directory = ProjectRelativePath::parse("Scenes");
  ASSERT_TRUE(directory.ok());

  for (const char *name :
       {"Root.qdr", "ScenesBackup/Room.qdr", "Materials/Room.qdr"}) {
    const auto path = ProjectRelativePath::parse(name);
    ASSERT_TRUE(path.ok());
    EXPECT_TRUE(files.resolve_for_write(path.value()).ok());
    const auto rejected =
        files.resolve_for_write_in_directory(path.value(), directory.value());
    ASSERT_FALSE(rejected.ok()) << name;
    EXPECT_EQ(rejected.error().code, ProjectIoErrorCode::PathOutsideProject);
  }

  EXPECT_FALSE(files.resolve_for_write_in_directory(directory.value(),
                                                   directory.value())
                   .ok());
  const auto invalid =
      files.resolve_for_write_in_directory({}, directory.value());
  EXPECT_FALSE(invalid.ok());
  EXPECT_EQ(invalid.error().code, ProjectIoErrorCode::InvalidRelativePath);
  for (const char *name :
       {".", "Scenes/../Root.qdr", "Scenes/Sub/../../Root.qdr"}) {
    EXPECT_FALSE(ProjectRelativePath::parse(name).ok()) << name;
  }
}

TEST(ProjectFileSystem, ScopedWritesRequireExistingParentsAndExactEntryCase) {
  ScopedProjectTestDirectory temporary("scoped-write-case");
  ASSERT_TRUE(std::filesystem::create_directories(temporary.path() / "Scenes" /
                                                 "Floor"));
  const auto opened = ProjectFileSystem::open(temporary.path());
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  const ProjectFileSystem &files = opened.value();
  const auto directory = ProjectRelativePath::parse("Scenes");
  ASSERT_TRUE(directory.ok());
  write_text_file(files.root() / "Scenes" / "Floor" / "Room.qdr", "QDRB");

  for (const char *name : {"scenes/Floor/Room.qdr", "Scenes/floor/Room.qdr",
                           "Scenes/Floor/room.qdr"}) {
    const auto path = ProjectRelativePath::parse(name);
    ASSERT_TRUE(path.ok());
    const auto rejected =
        files.resolve_for_write_in_directory(path.value(), directory.value());
    ASSERT_FALSE(rejected.ok()) << name;
    EXPECT_EQ(rejected.error().code, ProjectIoErrorCode::PathCaseCollision);
  }

  const auto existing = ProjectRelativePath::parse("Scenes/Floor/Room.qdr");
  const auto wrong_directory = ProjectRelativePath::parse("scenes");
  ASSERT_TRUE(existing.ok());
  ASSERT_TRUE(wrong_directory.ok());
  const auto rejected_directory = files.resolve_for_write_in_directory(
      existing.value(), wrong_directory.value());
  ASSERT_FALSE(rejected_directory.ok());
  EXPECT_EQ(rejected_directory.error().code,
            ProjectIoErrorCode::PathCaseCollision);

  const auto missing = ProjectRelativePath::parse("Scenes/Missing/Room.qdr");
  ASSERT_TRUE(missing.ok());
  const auto rejected_parent =
      files.resolve_for_write_in_directory(missing.value(), directory.value());
  ASSERT_FALSE(rejected_parent.ok());
  EXPECT_EQ(rejected_parent.error().code, ProjectIoErrorCode::DirectoryNotFound);
  EXPECT_FALSE(std::filesystem::exists(files.root() / "Scenes" / "Missing"));

  const auto folder = ProjectRelativePath::parse("Scenes/Floor");
  ASSERT_TRUE(folder.ok());
  const auto rejected_file =
      files.resolve_for_write_in_directory(folder.value(), directory.value());
  ASSERT_FALSE(rejected_file.ok());
  EXPECT_EQ(rejected_file.error().code, ProjectIoErrorCode::FileNotRegular);
}

TEST(ProjectFileSystem, ScopedWritesRejectNestedLinksOutsideRequestedDirectory) {
  ScopedProjectTestDirectory temporary("scoped-link-escape");
  const std::filesystem::path project_root = temporary.path() / "Project";
  ASSERT_TRUE(std::filesystem::create_directories(project_root / "Scenes"));
  ASSERT_TRUE(std::filesystem::create_directory(project_root / "Assets"));
  ASSERT_TRUE(std::filesystem::create_directory(project_root / "ScenesBackup"));
  ASSERT_TRUE(std::filesystem::create_directory(temporary.path() / "Outside"));
  const auto opened = ProjectFileSystem::open(project_root);
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  const ProjectFileSystem &files = opened.value();
  const auto directory = ProjectRelativePath::parse("Scenes");
  ASSERT_TRUE(directory.ok());

  const std::array<std::filesystem::path, 3> targets{
      project_root / "Assets", project_root / "ScenesBackup",
      temporary.path() / "Outside"};
  for (std::size_t index = 0u; index < targets.size(); ++index) {
    const std::string link_name = "Link" + std::to_string(index);
    std::error_code error;
    std::filesystem::create_directory_symlink(
        targets[index], files.root() / "Scenes" / link_name, error);
    if (error && index == 0u) {
      GTEST_SKIP() << "Directory links unavailable: " << error.message();
    }
    ASSERT_FALSE(error) << error.message();
    const auto path =
        ProjectRelativePath::parse("Scenes/" + link_name + "/Room.qdr");
    ASSERT_TRUE(path.ok());
    if (index < 2u) {
      EXPECT_TRUE(files.resolve_for_write(path.value()).ok());
    }
    const auto rejected =
        files.resolve_for_write_in_directory(path.value(), directory.value());
    ASSERT_FALSE(rejected.ok()) << targets[index].string();
    EXPECT_EQ(rejected.error().code, ProjectIoErrorCode::SymlinkEscape);
  }

  write_text_file(files.root() / "Assets" / "Existing.qdr", "QDRB");
  std::error_code error;
  std::filesystem::create_symlink(files.root() / "Assets" / "Existing.qdr",
                                  files.root() / "Scenes" / "Alias.qdr", error);
  ASSERT_FALSE(error) << error.message();
  const auto file_alias = ProjectRelativePath::parse("Scenes/Alias.qdr");
  ASSERT_TRUE(file_alias.ok());
  const auto rejected_file = files.resolve_for_write_in_directory(
      file_alias.value(), directory.value());
  ASSERT_FALSE(rejected_file.ok());
  EXPECT_EQ(rejected_file.error().code, ProjectIoErrorCode::SymlinkEscape);
}

TEST(ProjectFileSystem, ScopedWritesRejectRedirectedAllowedDirectory) {
  ScopedProjectTestDirectory temporary("scoped-root-link");
  for (const bool outside_project : {false, true}) {
    const std::filesystem::path project_root =
        temporary.path() / (outside_project ? "External" : "Internal");
    ASSERT_TRUE(std::filesystem::create_directory(project_root));
    const std::filesystem::path target =
        outside_project ? temporary.path() / "Outside" : project_root / "Other";
    ASSERT_TRUE(std::filesystem::create_directory(target));
    std::error_code error;
    std::filesystem::create_directory_symlink(target, project_root / "Scenes",
                                              error);
    if (error && !outside_project) {
      GTEST_SKIP() << "Directory links unavailable: " << error.message();
    }
    ASSERT_FALSE(error) << error.message();
    const auto opened = ProjectFileSystem::open(project_root);
    ASSERT_TRUE(opened.ok()) << opened.error().message;
    const auto directory = ProjectRelativePath::parse("Scenes");
    const auto path = ProjectRelativePath::parse("Scenes/Room.qdr");
    ASSERT_TRUE(directory.ok());
    ASSERT_TRUE(path.ok());
    if (!outside_project) {
      EXPECT_TRUE(opened.value().resolve_for_write(path.value()).ok());
    }
    const auto rejected = opened.value().resolve_for_write_in_directory(
        path.value(), directory.value());
    ASSERT_FALSE(rejected.ok());
    EXPECT_EQ(rejected.error().code, ProjectIoErrorCode::SymlinkEscape);
  }
}

TEST(ProjectFileSystem, ScopedWritesRejectLinksToCaseDistinctSiblingDirectories) {
  ScopedProjectTestDirectory temporary("scoped-case-distinct-link");
  const std::filesystem::path scenes = temporary.path() / "Scenes";
  const std::filesystem::path sibling = temporary.path() / "scenes";
  ASSERT_TRUE(std::filesystem::create_directory(scenes));
  std::error_code error;
  const bool created_sibling = std::filesystem::create_directory(sibling, error);
  ASSERT_FALSE(error) << error.message();
  if (!created_sibling) {
    GTEST_SKIP() << "The temporary directory does not support case-distinct "
                   "sibling folders.";
  }
  ASSERT_FALSE(std::filesystem::equivalent(scenes, sibling));
  std::filesystem::create_directory_symlink(sibling, scenes / "External", error);
  if (error) {
    GTEST_SKIP() << "Directory links unavailable: " << error.message();
  }

  const auto opened = ProjectFileSystem::open(temporary.path());
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  const auto directory = ProjectRelativePath::parse("Scenes");
  const auto path = ProjectRelativePath::parse("Scenes/External/Room.qdr");
  ASSERT_TRUE(directory.ok());
  ASSERT_TRUE(path.ok());
  EXPECT_TRUE(opened.value().resolve_for_write(path.value()).ok());
  const auto rejected = opened.value().resolve_for_write_in_directory(
      path.value(), directory.value());
  ASSERT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.error().code, ProjectIoErrorCode::SymlinkEscape);
  EXPECT_FALSE(std::filesystem::exists(sibling / "Room.qdr"));
}

TEST(ProjectFileSystem, ScopedWritesAllowInternalLinksAndAliasedProjectRoots) {
  ScopedProjectTestDirectory temporary("scoped-valid-links");
  const std::filesystem::path project_root = temporary.path() / "Project";
  ASSERT_TRUE(std::filesystem::create_directories(project_root / "Scenes" /
                                                 "Nested" / "Deeper"));
  std::error_code error;
  std::filesystem::create_directory_symlink(
      project_root / "Scenes" / "Nested", project_root / "Scenes" / "Internal",
      error);
  if (error) {
    GTEST_SKIP() << "Directory links unavailable: " << error.message();
  }
  const std::filesystem::path project_alias = temporary.path() / "ProjectAlias";
  std::filesystem::create_directory_symlink(project_root, project_alias, error);
  ASSERT_FALSE(error) << error.message();
  std::filesystem::create_directory_symlink(
      project_root / "Scenes", project_root / "SceneAlias", error);
  ASSERT_FALSE(error) << error.message();

  const auto opened = ProjectFileSystem::open(project_alias);
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  const ProjectFileSystem &files = opened.value();
  EXPECT_TRUE(std::filesystem::equivalent(files.root(), project_root));
  const auto directory = ProjectRelativePath::parse("Scenes");
  ASSERT_TRUE(directory.ok());
  for (const char *name :
       {"Scenes/Room.qdr", "Scenes/Internal/Deeper/Room.qdr"}) {
    const auto path = ProjectRelativePath::parse(name);
    ASSERT_TRUE(path.ok());
    const auto resolved =
        files.resolve_for_write_in_directory(path.value(), directory.value());
    ASSERT_TRUE(resolved.ok()) << resolved.error().message;
    EXPECT_EQ(resolved.value(), files.root() / path.value().native_path());

    const auto converted =
        files.relative_from_absolute(project_alias / path.value().native_path());
    ASSERT_TRUE(converted.ok()) << converted.error().message;
    EXPECT_TRUE(files.resolve_for_write_in_directory(converted.value(),
                                                     directory.value())
                    .ok());
  }

  const auto outside_alias = ProjectRelativePath::parse("SceneAlias/Room.qdr");
  ASSERT_TRUE(outside_alias.ok());
  EXPECT_TRUE(files.resolve_for_write(outside_alias.value()).ok());
  const auto rejected = files.resolve_for_write_in_directory(
      outside_alias.value(), directory.value());
  ASSERT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.error().code, ProjectIoErrorCode::PathOutsideProject);
}

#ifdef _WIN32
TEST(ProjectFileSystem, ConvertsShortRootAliasesForExistingAndUnsavedScenes) {
  ScopedProjectTestDirectory temporary("short-root-alias");
  const std::filesystem::path project_root =
      temporary.path() / "Long Project Directory";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;
  const ProjectFileSystem &files = created.value().file_system;

  const DWORD required = GetShortPathNameW(files.root().c_str(), nullptr, 0);
  ASSERT_NE(required, 0u) << GetLastError();
  std::wstring short_name(required, L'\0');
  const DWORD written =
      GetShortPathNameW(files.root().c_str(), short_name.data(), required);
  ASSERT_NE(written, 0u) << GetLastError();
  ASSERT_LT(written, required);
  short_name.resize(written);
  const std::filesystem::path alias_root(short_name);
  if (alias_root == files.root()) {
    GTEST_SKIP() << "The temporary volume does not generate short path aliases.";
  }
  ASSERT_TRUE(std::filesystem::equivalent(alias_root, files.root()));

  const std::filesystem::path opened = files.root() / "Scenes" / "Opened.qdr";
  write_text_file(opened, "QDRB");
  const ProjectIoResult<ProjectRelativePath> existing =
      files.relative_from_absolute(alias_root / "Scenes" / "Opened.qdr");
  ASSERT_TRUE(existing.ok()) << existing.error().message;
  EXPECT_EQ(existing.value().portable_string(), "Scenes/Opened.qdr");
  const ProjectIoResult<std::filesystem::path> resolved_existing =
      files.resolve_existing_file(existing.value());
  ASSERT_TRUE(resolved_existing.ok()) << resolved_existing.error().message;
  EXPECT_TRUE(std::filesystem::equivalent(resolved_existing.value(), opened));

  const std::filesystem::path unsaved = files.root() / "Scenes" / "Saved.qdr";
  ASSERT_FALSE(std::filesystem::exists(unsaved));
  const ProjectIoResult<ProjectRelativePath> destination =
      files.relative_from_absolute(alias_root / "Scenes" / "Saved.qdr");
  ASSERT_TRUE(destination.ok()) << destination.error().message;
  EXPECT_EQ(destination.value().portable_string(), "Scenes/Saved.qdr");
  const ProjectIoResult<std::filesystem::path> resolved_destination =
      files.resolve_for_write(destination.value());
  ASSERT_TRUE(resolved_destination.ok()) << resolved_destination.error().message;
  EXPECT_EQ(resolved_destination.value(), unsaved);
}
#endif

TEST(ProjectFileSystem, ResolvesPhysicalAndDialogPathsOncePerRequest) {
  ScopedProjectTestDirectory temporary("single-path-resolution");
  ASSERT_TRUE(std::filesystem::create_directory(temporary.path() / "Scenes"));
  write_text_file(temporary.path() / "Scenes" / "Existing.qdr", "QDRB");
  const auto opened = ProjectFileSystem::open(temporary.path());
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  const ProjectFileSystem &files = opened.value();

  for (const char *name : {"Scenes/Existing.qdr", "Scenes/Unsaved.qdr"}) {
    const auto path = ProjectRelativePath::parse(name);
    ASSERT_TRUE(path.ok());
    const auto absolute = files.root() / path.value().native_path();
    ProjectFileSystemWorkStats work;
    ProjectFileSystem batch = files.begin_resolution_batch(&work);

    const auto physical = batch.resolve_physical_location(path.value());
    ASSERT_TRUE(physical.ok()) << physical.error().message;
    EXPECT_EQ(physical.value(), absolute);
    EXPECT_EQ(work.physical_path_resolutions, 1u);

    batch = files.begin_resolution_batch(&work);
    const auto converted = batch.relative_from_absolute(absolute);
    ASSERT_TRUE(converted.ok()) << converted.error().message;
    EXPECT_EQ(converted.value(), path.value());
    EXPECT_EQ(work.physical_path_resolutions, 1u);
  }
}

TEST(ProjectFileSystem, RechecksPhysicalLinkTargetsWithinResolutionBatch) {
  ScopedProjectTestDirectory temporary("retargeted-path-resolution");
  const auto project_root = temporary.path() / "Project";
  const auto internal = project_root / "Scenes";
  const auto outside = temporary.path() / "Outside";
  ASSERT_TRUE(std::filesystem::create_directories(internal));
  ASSERT_TRUE(std::filesystem::create_directory(outside));
  const auto link = project_root / "Alias";
  std::error_code error;
  std::filesystem::create_directory_symlink(internal, link, error);
  if (error) {
    GTEST_SKIP() << "Directory links unavailable: " << error.message();
  }
  const auto opened = ProjectFileSystem::open(project_root);
  ASSERT_TRUE(opened.ok()) << opened.error().message;
  ProjectFileSystemWorkStats work;
  const auto batch = opened.value().begin_resolution_batch(&work);
  const auto path = ProjectRelativePath::parse("Alias/Room.qdr");
  ASSERT_TRUE(path.ok());

  const auto physical = batch.resolve_physical_location(path.value());
  ASSERT_TRUE(physical.ok()) << physical.error().message;
  EXPECT_EQ(physical.value(), opened.value().root() / "Scenes" / "Room.qdr");
  EXPECT_EQ(work.physical_path_resolutions, 1u);

  ASSERT_TRUE(std::filesystem::remove(link, error));
  ASSERT_FALSE(error) << error.message();
  std::filesystem::create_directory_symlink(outside, link, error);
  ASSERT_FALSE(error) << error.message();
  const auto escaped = batch.resolve_physical_location(path.value());
  ASSERT_FALSE(escaped.ok());
  EXPECT_EQ(escaped.error().code, ProjectIoErrorCode::SymlinkEscape);
  EXPECT_EQ(work.physical_path_resolutions, 2u);
}

TEST(ProjectFileSystem,
     ResolutionBatchEnumeratesOneLargeSceneDirectoryOnlyOnce) {
  ScopedProjectTestDirectory temporary("path-batch-scale");
  const std::filesystem::path project_root =
      temporary.path() / "PathBatchScale";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;

  constexpr std::size_t kSceneCount = 512u;
  std::vector<ProjectRelativePath> scene_paths;
  scene_paths.reserve(kSceneCount);
  for (std::size_t index = 0u; index < kSceneCount; ++index) {
    const std::string portable =
        "Scenes/Scene_" + std::to_string(index) + ".qdr";
    write_text_file(project_root / portable, "QDRB");
    ProjectIoResult<ProjectRelativePath> parsed =
        ProjectRelativePath::parse_portable(portable);
    ASSERT_TRUE(parsed.ok()) << parsed.error().message;
    scene_paths.push_back(parsed.take_value());
  }

  ProjectFileSystemWorkStats work;
  ProjectFileSystem batch =
      created.value().file_system.begin_resolution_batch(&work);
  const ProjectIoResult<void> validated =
      batch.validate_no_path_aliases(scene_paths);
  ASSERT_TRUE(validated.ok()) << validated.error().message;

  EXPECT_EQ(work.directory_case_enumerations, 2u);
  EXPECT_EQ(work.component_case_lookups, kSceneCount * 2u);
  EXPECT_EQ(work.physical_path_resolutions, kSceneCount);
  EXPECT_LE(work.directory_case_entries_visited, kSceneCount + 4u)
      << "case validation must index the root and Scenes directory once, "
         "not enumerate all siblings once per scene";
}

TEST(ProjectManifestStore, RoundTripsCatalogTabOrderAndMissingScenes) {
  ScopedProjectTestDirectory temporary("roundtrip");
  const std::filesystem::path project_root = temporary.path() / "Gallery";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;

  ProjectIoResult<ProjectRelativePath> first_path =
      ProjectRelativePath::parse("Scenes/Main.qdr");
  ProjectIoResult<ProjectRelativePath> second_path =
      ProjectRelativePath::parse("Alternate/Lighting.qdr");
  ASSERT_TRUE(first_path.ok());
  ASSERT_TRUE(second_path.ok());
  const SceneId first_id = SceneId::generate();
  const SceneId second_id = SceneId::generate();
  ASSERT_TRUE(created.value().manifest
                  .register_scene({.id = first_id, .path = first_path.value()})
                  .ok());
  ASSERT_TRUE(created.value().manifest
                  .register_scene({.id = second_id, .path = second_path.value()})
                  .ok());
  ASSERT_TRUE(created.value().manifest
                  .set_workspace({second_path.value(), first_path.value()},
                                 first_path.value())
                  .ok());

  ProjectIoResult<void> saved =
      ProjectManifestStore::save_atomic(created.value());
  ASSERT_TRUE(saved.ok()) << saved.error().message;
  ProjectIoResult<ProjectWorkspaceStorage> reopened =
      ProjectManifestStore::open(created.value().manifest_path);
  ASSERT_TRUE(reopened.ok()) << reopened.error().message;
  ASSERT_EQ(reopened.value().manifest.scenes().size(), 2u);
  ASSERT_EQ(reopened.value().manifest.open_scene_paths().size(), 2u);
  EXPECT_EQ(reopened.value().manifest.open_scene_paths()[0],
            second_path.value());
  EXPECT_EQ(reopened.value().manifest.open_scene_paths()[1],
            first_path.value());
  ASSERT_TRUE(reopened.value().manifest.active_scene_path().has_value());
  EXPECT_EQ(*reopened.value().manifest.active_scene_path(), first_path.value());

  // Catalog entries may refer to missing scenes; opening the project itself
  // remains successful so the session layer can skip and report them together.
  EXPECT_FALSE(std::filesystem::exists(
      reopened.value().file_system.root() / second_path.value().native_path()));
}

TEST(RecentProjectStore, UnicodeProjectRootsRemainAvailableAcrossStoreReload) {
  ScopedProjectTestDirectory temporary("recent-unicode");
  const std::filesystem::path project_root =
      temporary.path() / std::filesystem::path(L"Caf\u00e9 \u6771\u4eac");
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  const auto project = ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(project.ok()) << project.error().message;
  const auto storage_path = temporary.path() / "Config" / "recent-projects.json";
  const RecentProjectStore store(storage_path);
  const auto remembered = store.remember(project.value().manifest,
                                         project.value().manifest_path);
  ASSERT_TRUE(remembered.ok()) << remembered.error().message;

  const RecentProjectStore reopened_store(storage_path);
  const auto loaded = reopened_store.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  const auto &entry = loaded.value().front();
  EXPECT_TRUE(entry.available);
  EXPECT_EQ(entry.project_id, project.value().manifest.id());
  EXPECT_EQ(entry.name, "Caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac");
  EXPECT_EQ(entry.manifest_path, project.value().manifest_path);
  EXPECT_TRUE(std::filesystem::is_regular_file(entry.manifest_path));
}

TEST(ProjectManifestStore, OptionalAppVersionPreservesUnknownAndRecordedValues) {
  auto created = ProjectManifest::create(ProjectId::generate(), "Versioned");
  ASSERT_TRUE(created.ok());
  EXPECT_TRUE(created.value().last_saved_app_version().empty());
  auto encoded = nlohmann::json::parse(ProjectManifestStore::serialize(created.value()));
  EXPECT_FALSE(encoded.contains("lastSavedAppVersion"));
  const auto unknown = ProjectManifestStore::deserialize(encoded.dump());
  ASSERT_TRUE(unknown.ok());
  EXPECT_TRUE(unknown.value().last_saved_app_version().empty());

  encoded["lastSavedAppVersion"] = "0.1.25";
  auto recorded = ProjectManifestStore::deserialize(encoded.dump());
  ASSERT_TRUE(recorded.ok());
  EXPECT_EQ(recorded.value().last_saved_app_version(), "0.1.25");
  EXPECT_EQ(nlohmann::json::parse(ProjectManifestStore::serialize(recorded.value()))
                .at("lastSavedAppVersion"),
            "0.1.25");
  recorded.value().mark_saved_by_current_app();
  EXPECT_EQ(recorded.value().last_saved_app_version(), kQuaderAppVersion);
  const auto stamped = ProjectManifestStore::deserialize(
      ProjectManifestStore::serialize(recorded.value()));
  ASSERT_TRUE(stamped.ok());
  EXPECT_EQ(stamped.value().last_saved_app_version(), kQuaderAppVersion);
  EXPECT_EQ(stamped.value().loaded_version(), kQuaderProjectManifestVersion);

  encoded["lastSavedAppVersion"] = 42;
  EXPECT_FALSE(ProjectManifestStore::deserialize(encoded.dump()).ok());
  encoded["lastSavedAppVersion"] = std::string(65u, '1');
  EXPECT_FALSE(ProjectManifestStore::deserialize(encoded.dump()).ok());
}

TEST(RecentProjectStore, ReadsSavedAppVersionWithoutRestampingOrCachingIt) {
  ScopedProjectTestDirectory temporary("recent-app-version");
  const auto root = temporary.path() / "Versioned";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  auto project = ProjectManifestStore::create_in_empty_directory(root);
  ASSERT_TRUE(project.ok()) << project.error().message;
  auto encoded = nlohmann::json::parse(
      ProjectManifestStore::serialize(project.value().manifest));
  encoded["lastSavedAppVersion"] = "0.1.25";
  auto recorded = ProjectManifestStore::deserialize(encoded.dump());
  ASSERT_TRUE(recorded.ok());
  project.value().manifest = recorded.take_value();
  ASSERT_TRUE(ProjectManifestStore::save_atomic(project.value()).ok());
  const RecentProjectStore store(temporary.path() / "recent-projects.json");
  ASSERT_TRUE(store.remember(project.value().manifest,
                             project.value().manifest_path).ok());
  auto loaded = store.load();
  ASSERT_TRUE(loaded.ok());
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_TRUE(loaded.value().front().available);
  EXPECT_EQ(loaded.value().front().last_saved_app_version, "0.1.25");
  const auto unchanged = ProjectManifestStore::open_metadata(project.value().manifest_path);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_EQ(unchanged.value().manifest.last_saved_app_version(), "0.1.25");

  project.value().manifest.mark_saved_by_current_app();
  ASSERT_TRUE(ProjectManifestStore::save_atomic(project.value()).ok());
  loaded = store.load();
  ASSERT_TRUE(loaded.ok());
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_EQ(loaded.value().front().last_saved_app_version, kQuaderAppVersion);

  encoded.erase("lastSavedAppVersion");
  write_text_file(project.value().manifest_path, encoded.dump());
  loaded = store.load();
  ASSERT_TRUE(loaded.ok());
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_TRUE(loaded.value().front().available);
  EXPECT_TRUE(loaded.value().front().last_saved_app_version.empty());
}

TEST(RecentProjectStore, DeduplicatesAndRetainsUnavailableEntries) {
  ScopedProjectTestDirectory temporary("recent");
  const std::filesystem::path project_root = temporary.path() / "Recent";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;

  const RecentProjectStore store(temporary.path() / "Config" /
                                 "recent_projects.json");
  ASSERT_TRUE(store
                  .remember(created.value().manifest,
                            created.value().manifest_path)
                  .ok());
  ASSERT_TRUE(store
                  .remember(created.value().manifest,
                            created.value().manifest_path)
                  .ok());
  ProjectIoResult<std::vector<RecentProjectEntry>> loaded = store.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_TRUE(loaded.value()[0].available);
  ASSERT_TRUE(loaded.value()[0].manifest_last_modified_unix_ms.has_value());
  EXPECT_GT(*loaded.value()[0].manifest_last_modified_unix_ms, 0);
  ASSERT_TRUE(loaded.value()[0].manifest_size_bytes.has_value());
  EXPECT_EQ(*loaded.value()[0].manifest_size_bytes,
            std::filesystem::file_size(created.value().manifest_path));
  const auto first_last_modified =
      loaded.value()[0].manifest_last_modified_unix_ms;
  loaded = store.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_EQ(loaded.value()[0].manifest_last_modified_unix_ms,
            first_last_modified)
      << "unchanged manifest metadata must remain stable across refreshes";

  std::error_code error;
  std::filesystem::remove(created.value().manifest_path, error);
  ASSERT_FALSE(error) << error.message();
  loaded = store.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_FALSE(loaded.value()[0].available);
  EXPECT_FALSE(
      loaded.value()[0].manifest_last_modified_unix_ms.has_value());
  EXPECT_FALSE(loaded.value()[0].manifest_size_bytes.has_value());

  ASSERT_TRUE(store.remove(created.value().manifest_path).ok());
  loaded = store.load();
  ASSERT_TRUE(loaded.ok());
  EXPECT_TRUE(loaded.value().empty());
}

TEST(RecentProjectStore, RetainsTwentyAvailableProjectsAcrossReorderAndRemoval) {
  ScopedProjectTestDirectory temporary("recent-twenty");
  const auto storage_path = temporary.path() / "Config" / "recent-projects.json";
  const RecentProjectStore store(storage_path);
  std::vector<ProjectWorkspaceStorage> projects;
  std::vector<ProjectId> expected;
  for (int index = 0; index < 21; ++index) {
    const auto root = temporary.path() / ("Project-" + std::to_string(index));
    ASSERT_TRUE(std::filesystem::create_directory(root));
    auto created = ProjectManifestStore::create_in_empty_directory(root);
    ASSERT_TRUE(created.ok()) << created.error().message;
    projects.push_back(created.take_value());
    if (index < 20) {
      ASSERT_TRUE(store.remember(projects.back().manifest,
                                 projects.back().manifest_path).ok());
      expected.insert(expected.begin(), projects.back().manifest.id());
    }
  }

  const auto expect_reloaded_order = [&]() {
    const RecentProjectStore reopened(storage_path);
    const auto loaded = reopened.load();
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    ASSERT_EQ(loaded.value().size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      SCOPED_TRACE(index);
      const auto &entry = loaded.value()[index];
      EXPECT_EQ(entry.project_id, expected[index]);
      EXPECT_TRUE(entry.available);
      EXPECT_TRUE(std::filesystem::is_regular_file(entry.manifest_path));
      ASSERT_TRUE(entry.manifest_size_bytes.has_value());
      EXPECT_EQ(*entry.manifest_size_bytes,
                std::filesystem::file_size(entry.manifest_path));
    }
  };
  ASSERT_EQ(expected.size(), 20u);
  expect_reloaded_order();

  // A twenty-first project evicts only the least recently opened project.
  ASSERT_TRUE(store.remember(projects.back().manifest,
                             projects.back().manifest_path).ok());
  expected.insert(expected.begin(), projects.back().manifest.id());
  expected.pop_back();
  expect_reloaded_order();

  const auto &reopened_project = projects[7];
  ASSERT_TRUE(store.remember(reopened_project.manifest,
                             reopened_project.manifest_path).ok());
  const auto old_position = std::find(expected.begin(), expected.end(),
                                      reopened_project.manifest.id());
  ASSERT_NE(old_position, expected.end());
  expected.erase(old_position);
  expected.insert(expected.begin(), reopened_project.manifest.id());
  ASSERT_EQ(expected.size(), 20u);
  expect_reloaded_order();

  const auto &removed_project = projects[11];
  ASSERT_TRUE(store.remove(removed_project.manifest_path).ok());
  const auto removed_position = std::find(expected.begin(), expected.end(),
                                          removed_project.manifest.id());
  ASSERT_NE(removed_position, expected.end());
  expected.erase(removed_position);
  ASSERT_EQ(expected.size(), 19u);
  expect_reloaded_order();
  EXPECT_TRUE(std::filesystem::is_regular_file(removed_project.manifest_path));

  ASSERT_TRUE(store.remember(projects.front().manifest,
                             projects.front().manifest_path).ok());
  expected.insert(expected.begin(), projects.front().manifest.id());
  ASSERT_EQ(expected.size(), 20u);
  expect_reloaded_order();
}

} // namespace
