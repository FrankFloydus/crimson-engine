////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/recent_scene_store.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace quader;

namespace {

ProjectRelativePath recent_scene_test_path(std::string_view path) {
  auto parsed = ProjectRelativePath::parse_portable(path);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.ok() ? parsed.take_value() : ProjectRelativePath{};
}

void write_recent_scene_test_file(const std::filesystem::path &path,
                                  std::string_view contents) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  stream.close();
  ASSERT_TRUE(stream.good()) << path.string();
}

std::string read_recent_scene_test_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

class RecentSceneStoreTests : public ::testing::Test {
protected:
  RecentSceneStoreTests()
      : root_(std::filesystem::temp_directory_path() /
              ("quader-recent-scenes-" +
               std::string(ProjectId::generate().str()))),
        store_(root_ / "Config" / "recent-scenes.json") {}

  void SetUp() override {
    std::filesystem::create_directories(root_ / "Project");
    auto created =
        ProjectManifestStore::create_in_empty_directory(root_ / "Project");
    ASSERT_TRUE(created.ok()) << created.error().message;
    workspace_ = created.take_value();
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  void add_scene(ProjectWorkspaceStorage &workspace, const SceneId &id,
                 const ProjectRelativePath &path) {
    const auto resolved = workspace.file_system.resolve_for_write(path);
    ASSERT_TRUE(resolved.ok()) << resolved.error().message;
    write_recent_scene_test_file(resolved.value(), "QDRB");
    const auto registered = workspace.manifest.register_scene({id, path});
    ASSERT_TRUE(registered.ok()) << registered.error().message;
    const auto saved = ProjectManifestStore::save_atomic(workspace);
    ASSERT_TRUE(saved.ok()) << saved.error().message;
  }

  void remember(const SceneId &id, const ProjectRelativePath &path,
                std::string name = "Scene") {
    const auto saved = store_.remember(workspace_.manifest,
                                       workspace_.manifest_path, id,
                                       std::move(name), path);
    ASSERT_TRUE(saved.ok()) << saved.error().message;
  }

  std::filesystem::path root_;
  RecentSceneStore store_;
  ProjectWorkspaceStorage workspace_;
};

TEST_F(RecentSceneStoreTests, MissingStorageStartsEmptyWithoutCreatingAFile) {
  const auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  EXPECT_TRUE(loaded.value().empty());
  EXPECT_EQ(store_.storage_path(), root_ / "Config" / "recent-scenes.json");
  EXPECT_FALSE(std::filesystem::exists(store_.storage_path()));
}

TEST_F(RecentSceneStoreTests, KeepsFiveMostRecentScenesAndPromotesReopenedScene) {
  std::vector<SceneId> ids;
  std::vector<ProjectRelativePath> paths;
  for (int index = 0; index < 7; ++index) {
    ids.push_back(SceneId::generate());
    paths.push_back(recent_scene_test_path("Scenes/Scene" +
                                          std::to_string(index) + ".qdr"));
    add_scene(workspace_, ids.back(), paths.back());
    remember(ids.back(), paths.back(), "Scene " + std::to_string(index));
  }
  const RecentSceneStore reopened(store_.storage_path());
  const auto initial = reopened.load();
  ASSERT_TRUE(initial.ok()) << initial.error().message;
  ASSERT_EQ(initial.value().size(), 5u);
  EXPECT_EQ(RecentSceneStore::kMaximumEntries, 5u);
  for (std::size_t index = 0; index < initial.value().size(); ++index) {
    EXPECT_EQ(initial.value()[index].scene_id, ids[6u - index]);
    EXPECT_TRUE(initial.value()[index].available);
  }

  remember(ids[3], paths[3], "Renamed Scene");
  const auto promoted = reopened.load();
  ASSERT_TRUE(promoted.ok()) << promoted.error().message;
  ASSERT_EQ(promoted.value().size(), 5u);
  EXPECT_EQ(promoted.value()[0].scene_id, ids[3]);
  EXPECT_EQ(promoted.value()[0].scene_name, "Renamed Scene");
  EXPECT_EQ(promoted.value()[0].project_name, workspace_.manifest.name());
  EXPECT_EQ(promoted.value()[1].scene_id, ids[6]);
  EXPECT_EQ(promoted.value()[2].scene_id, ids[5]);
  EXPECT_EQ(promoted.value()[3].scene_id, ids[4]);
  EXPECT_EQ(promoted.value()[4].scene_id, ids[2]);
}

TEST_F(RecentSceneStoreTests, CanonicalManifestAndScenePathsDoNotDuplicateRows) {
  const auto first_id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, first_id, path);
  remember(first_id, path);

  const auto replaced_id = SceneId::generate();
  const auto manifest_alias = workspace_.file_system.root() / "Scenes" / ".." /
                              workspace_.manifest_path.filename();
  const auto saved = store_.remember(
      workspace_.manifest, manifest_alias, replaced_id, "Replaced Scene",
      recent_scene_test_path("Scenes/./Scene.qdr"));
  ASSERT_TRUE(saved.ok()) << saved.error().message;
  const auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_EQ(loaded.value().front().scene_id, replaced_id);
  EXPECT_EQ(loaded.value().front().scene_path, path);
  EXPECT_EQ(loaded.value().front().manifest_path,
            std::filesystem::canonical(workspace_.manifest_path));
}

TEST_F(RecentSceneStoreTests, SaveAsReplacesSameSceneIdentityWithinTheProject) {
  const auto id = SceneId::generate();
  const auto original = recent_scene_test_path("Scenes/Original.qdr");
  const auto renamed = recent_scene_test_path("Scenes/Renamed.qdr");
  add_scene(workspace_, id, original);
  remember(id, original);
  ASSERT_TRUE(workspace_.manifest.unregister_scene(id).ok());
  add_scene(workspace_, id, renamed);
  remember(id, renamed, "Renamed");

  const auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_EQ(loaded.value().front().scene_id, id);
  EXPECT_EQ(loaded.value().front().scene_path, renamed);
  EXPECT_EQ(loaded.value().front().scene_name, "Renamed");
}

TEST_F(RecentSceneStoreTests, HardLinkedScenePathsReplaceTheSamePhysicalFile) {
  const auto original_id = SceneId::generate();
  const auto original_path = recent_scene_test_path("Scenes/Original.qdr");
  add_scene(workspace_, original_id, original_path);
  remember(original_id, original_path, "Original");
  const auto linked_path = recent_scene_test_path("Scenes/Linked.qdr");
  std::error_code error;
  std::filesystem::create_hard_link(
      workspace_.file_system.root() / original_path.native_path(),
      workspace_.file_system.root() / linked_path.native_path(), error);
  if (error) {
    GTEST_SKIP() << "Hard links are unavailable: " << error.message();
  }

  const auto replacement_id = SceneId::generate();
  remember(replacement_id, linked_path, "Linked Scene");
  const auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_EQ(loaded.value().front().scene_id, replacement_id);
  EXPECT_EQ(loaded.value().front().scene_path, linked_path);
  EXPECT_EQ(loaded.value().front().scene_name, "Linked Scene");
  EXPECT_TRUE(loaded.value().front().available);
}

TEST_F(RecentSceneStoreTests, CopiedProjectsWithIdenticalIdsRemainDistinct) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  remember(id, path);

  std::filesystem::create_directory(root_ / "Copy");
  auto created = ProjectManifestStore::create_in_empty_directory(root_ / "Copy");
  ASSERT_TRUE(created.ok()) << created.error().message;
  auto copied = created.take_value();
  auto manifest = ProjectManifest::create(workspace_.manifest.id(), "Copy");
  ASSERT_TRUE(manifest.ok()) << manifest.error().message;
  copied.manifest = manifest.take_value();
  add_scene(copied, id, path);
  const auto saved = store_.remember(copied.manifest, copied.manifest_path, id,
                                    "Copied Scene", path);
  ASSERT_TRUE(saved.ok()) << saved.error().message;

  const auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 2u);
  EXPECT_EQ(loaded.value()[0].scene_id, id);
  EXPECT_EQ(loaded.value()[1].scene_id, id);
  EXPECT_NE(loaded.value()[0].manifest_path, loaded.value()[1].manifest_path);
  EXPECT_TRUE(loaded.value()[0].available);
  EXPECT_TRUE(loaded.value()[1].available);
}

TEST_F(RecentSceneStoreTests, UnicodeManifestPathsRoundTripAsUtf8) {
  const auto project_root =
      root_ / std::filesystem::path(L"Quader Caf\u00e9 \u65e5\u672c") / "Project";
  ASSERT_TRUE(std::filesystem::create_directories(project_root));
  auto created = ProjectManifestStore::create_in_empty_directory(project_root);
  ASSERT_TRUE(created.ok()) << created.error().message;
  auto unicode_workspace = created.take_value();
  const auto id = SceneId::generate();
  const auto scene_path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(unicode_workspace, id, scene_path);
  const auto saved = store_.remember(
      unicode_workspace.manifest, unicode_workspace.manifest_path, id,
      "Unicode Scene", scene_path);
  ASSERT_TRUE(saved.ok()) << saved.error().message;

  const auto canonical =
      std::filesystem::canonical(unicode_workspace.manifest_path);
  const auto utf8_path = canonical.generic_u8string();
  const std::string expected_utf8(utf8_path.begin(), utf8_path.end());
  const auto json =
      nlohmann::json::parse(read_recent_scene_test_file(store_.storage_path()));
  const auto persisted_path = json["scenes"][0]["manifestPath"].get<std::string>();
  EXPECT_EQ(persisted_path, expected_utf8);
  EXPECT_EQ(persisted_path.find('\0'), std::string::npos);

  const RecentSceneStore reopened(store_.storage_path());
  const auto loaded = reopened.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_EQ(loaded.value().front().manifest_path, canonical);
  EXPECT_EQ(loaded.value().front().scene_id, id);
  EXPECT_TRUE(loaded.value().front().available);
}

TEST_F(RecentSceneStoreTests, UnchangedFrontEntryDoesNotRewriteStorage) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  remember(id, path);
  const auto original_bytes = read_recent_scene_test_file(store_.storage_path());
  const auto old_time = std::filesystem::file_time_type::clock::now() -
                        std::chrono::hours(24);
  std::filesystem::last_write_time(store_.storage_path(), old_time);
  const auto stored_time = std::filesystem::last_write_time(store_.storage_path());

  remember(id, path);
  EXPECT_EQ(std::filesystem::last_write_time(store_.storage_path()), stored_time);
  EXPECT_EQ(read_recent_scene_test_file(store_.storage_path()), original_bytes);

  remember(id, path, "New Display Name");
  const auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_EQ(loaded.value().front().scene_name, "New Display Name");
}

TEST_F(RecentSceneStoreTests, MissingSceneOrProjectIsRetainedAsUnavailable) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  remember(id, path);
  ASSERT_TRUE(std::filesystem::remove(workspace_.file_system.root() /
                                      path.native_path()));
  auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_FALSE(loaded.value().front().available);

  write_recent_scene_test_file(workspace_.file_system.root() / path.native_path(),
                               "QDRB");
  ASSERT_TRUE(std::filesystem::remove(workspace_.manifest_path));
  loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_FALSE(loaded.value().front().available);
}

TEST_F(RecentSceneStoreTests, MalformedStorageIsReportedAndNeverOverwritten) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  remember(id, path);
  auto invalid_identity =
      nlohmann::json::parse(read_recent_scene_test_file(store_.storage_path()));
  invalid_identity["scenes"][0]["sceneId"] = "invalid-uuid";
  auto escaped_path = invalid_identity;
  escaped_path["scenes"][0]["sceneId"] = id.str();
  escaped_path["scenes"][0]["scenePath"] = "../Outside.qdr";
  auto recovery_path = escaped_path;
  recovery_path["scenes"][0]["scenePath"] =
      ".QUADER/AUTOSAVE/scene/copy.qdr";
  const std::vector<std::string> invalid_documents{
      "{truncated", "{\"format\":\"wrong\",\"version\":1,\"scenes\":[]}",
      "{\"format\":\"quader.recent-scenes\",\"version\":999,\"scenes\":[]}",
      invalid_identity.dump(), escaped_path.dump(), recovery_path.dump()};
  for (const auto &contents : invalid_documents) {
    SCOPED_TRACE(contents);
    write_recent_scene_test_file(store_.storage_path(), contents);
    EXPECT_FALSE(store_.load().ok());
    EXPECT_FALSE(store_
                     .remember(workspace_.manifest, workspace_.manifest_path, id,
                               "Scene", path)
                     .ok());
    EXPECT_EQ(read_recent_scene_test_file(store_.storage_path()), contents);
  }
}

TEST_F(RecentSceneStoreTests, OversizedStorageIsRejectedWithoutOverwritingIt) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  remember(id, path);
  const auto original = read_recent_scene_test_file(store_.storage_path());
  auto too_many = nlohmann::json::parse(original);
  const auto scene = too_many["scenes"][0];
  too_many["scenes"] = nlohmann::json::array();
  for (int index = 0; index < 1001; ++index) {
    too_many["scenes"].push_back(scene);
  }
  const std::vector<std::string> oversized_documents{
      too_many.dump(), std::string(1024u * 1024u, ' ') + original};
  for (const auto &contents : oversized_documents) {
    SCOPED_TRACE(contents.size());
    write_recent_scene_test_file(store_.storage_path(), contents);
    EXPECT_FALSE(store_.load().ok());
    EXPECT_FALSE(store_
                     .remember(workspace_.manifest, workspace_.manifest_path, id,
                               "Scene", path)
                     .ok());
    EXPECT_EQ(read_recent_scene_test_file(store_.storage_path()), contents);
  }
}

TEST_F(RecentSceneStoreTests, SceneLinkEscapingItsProjectIsUnavailable) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  remember(id, path);
  const auto original = read_recent_scene_test_file(store_.storage_path());
  const auto native_path = workspace_.file_system.root() / path.native_path();
  ASSERT_TRUE(std::filesystem::remove(native_path));
  const auto outside = root_ / "Outside.qdr";
  write_recent_scene_test_file(outside, "outside project");
  std::error_code error;
  std::filesystem::create_symlink(outside, native_path, error);
  if (error) {
    GTEST_SKIP() << "File links are unavailable: " << error.message();
  }

  const auto loaded = store_.load();
  ASSERT_TRUE(loaded.ok()) << loaded.error().message;
  ASSERT_EQ(loaded.value().size(), 1u);
  EXPECT_FALSE(loaded.value().front().available);
  EXPECT_EQ(read_recent_scene_test_file(store_.storage_path()), original);
}

TEST_F(RecentSceneStoreTests, RecoveryCopiesNeverEnterRecentScenes) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  remember(id, path);
  const auto original = read_recent_scene_test_file(store_.storage_path());
  for (const auto autosave_path :
       {".quader/autosave/scene/copy.qdr",
        ".QUADER/AUTOSAVE/scene/copy.qdr"}) {
    SCOPED_TRACE(autosave_path);
    const auto rejected = store_.remember(
        workspace_.manifest, workspace_.manifest_path, SceneId::generate(),
        "Recovery", recent_scene_test_path(autosave_path));
    EXPECT_FALSE(rejected.ok());
    EXPECT_EQ(read_recent_scene_test_file(store_.storage_path()), original);
  }
}

TEST_F(RecentSceneStoreTests, StorageFailuresDoNotDestroyExistingFiles) {
  const auto id = SceneId::generate();
  const auto path = recent_scene_test_path("Scenes/Scene.qdr");
  add_scene(workspace_, id, path);
  const auto occupied = root_ / "occupied";
  write_recent_scene_test_file(occupied, "user-owned file");
  const RecentSceneStore blocked(occupied / "recent-scenes.json");
  EXPECT_FALSE(blocked
                   .remember(workspace_.manifest, workspace_.manifest_path, id,
                             "Scene", path)
                   .ok());
  EXPECT_EQ(read_recent_scene_test_file(occupied), "user-owned file");

  const RecentSceneStore directory(root_);
  EXPECT_FALSE(directory.load().ok());
  EXPECT_FALSE(directory
                   .remember(workspace_.manifest, workspace_.manifest_path, id,
                             "Scene", path)
                   .ok());
  EXPECT_TRUE(std::filesystem::is_regular_file(workspace_.manifest_path));
}

} // namespace
