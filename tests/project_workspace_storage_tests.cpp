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

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

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

TEST(ProjectManifestStore, CreatesOnlyInsideAnExactlyEmptyFolder) {
  ScopedProjectTestDirectory temporary("create");
  const std::filesystem::path project_root = temporary.path() / "Museum";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));

  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;
  EXPECT_EQ(created.value().manifest.name(), "Museum");
  EXPECT_EQ(created.value().manifest_path.filename(), "Museum.qproj");
  EXPECT_TRUE(std::filesystem::is_regular_file(created.value().manifest_path));
  EXPECT_TRUE(
      std::filesystem::is_directory(created.value().file_system.root() / "Scenes"));

  ProjectIoResult<ProjectWorkspaceStorage> reopened =
      ProjectManifestStore::open(created.value().manifest_path);
  ASSERT_TRUE(reopened.ok()) << reopened.error().message;
  EXPECT_EQ(reopened.value().manifest.id(), created.value().manifest.id());
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
  }

  const std::filesystem::path internal_link =
      created.value().file_system.root() / "SceneAlias";
  link_error.clear();
  std::filesystem::create_directory_symlink(
      created.value().file_system.root() / "Scenes", internal_link,
      link_error);
  if (!link_error) {
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

} // namespace
