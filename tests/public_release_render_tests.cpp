////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_preferences_api.hpp"
#include "api/quader_viewport_api.hpp"
#include "assets/asset_registry.hpp"
#include "assets/default_material_catalog.hpp"
#include "core/release_features.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_session.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_document.hpp"
#include "support/behavior_scenario.hpp"
#include "support/texture_lifetime_harness.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <gtest/gtest.h>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace quader::test {
namespace {

MeshAsset public_release_triangle() {
  MeshAsset mesh;
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      MeshVertex{.position = {0.0f, 0.0f, 0.0f}},
      MeshVertex{.position = {1.0f, 0.0f, 0.0f}},
      MeshVertex{.position = {0.0f, 1.0f, 0.0f}},
  };
  geometry.indices = {0u, 1u, 2u};
  geometry.primitives = {
      MeshPrimitiveSpan{.index_offset = 0u, .index_count = 3u,
                        .material_slot = 7u}};
  return mesh;
}

void expect_packaged_default(const RenderMaterialBindingSnapshot &material) {
  ASSERT_TRUE(material.present);
  ASSERT_TRUE(material.primary_material.present);
  EXPECT_TRUE(material.material_slots.empty());
  EXPECT_TRUE(material.component.material_slots.empty());
  EXPECT_FALSE(material.primary_material.reference.project_owned());
  EXPECT_FLOAT_EQ(material.primary_material.document.base_color.x, 1.0f);
  EXPECT_FLOAT_EQ(material.primary_material.document.base_color.y, 1.0f);
  EXPECT_FLOAT_EQ(material.primary_material.document.base_color.z, 1.0f);
  EXPECT_FLOAT_EQ(material.primary_material.document.metallic, 0.0f);
  const auto *albedo = material.primary_material.texture_for_slot(
      material_texture_slot::kBaseColor);
  ASSERT_NE(albedo, nullptr);
  EXPECT_TRUE(albedo->present);
  EXPECT_FALSE(albedo->project_owned());
  EXPECT_EQ(albedo->texture.source_path.generic_string(),
            kDefaultMaterialAlbedoTexturePackagePath);
  EXPECT_EQ(material.material_for_slot(7u), &material.primary_material);
}

/// A restricted presentation must never resolve mutable project material data.
class CountingProjectMaterialResolver final
    : public RenderProjectMaterialResolver {
public:
  std::uint64_t revision() const noexcept override { return 3u; }
  const RenderMaterialAssetSnapshot *
  default_material_override() const noexcept override {
    return default_override;
  }
  std::optional<RenderProjectMaterialSnapshot>
  resolve(ProjectAssetId) const override {
    ++resolve_count;
    return std::nullopt;
  }
  mutable int resolve_count = 0;
  const RenderMaterialAssetSnapshot *default_override = nullptr;
};

TEST(PublicReleaseRenderTests, DefaultImagePreferenceDoesNotResolveAuthoredMaterials) {
  ProjectSession project;
  ProjectSessionRenderMaterialResolver preference(project);
  const auto path = std::filesystem::temp_directory_path() / "quader-chosen.png";
  preference.set_default_material_texture(path);
  CountingProjectMaterialResolver resolver;
  resolver.default_override = preference.default_material_override();
  AssetRegistry assets;
  MaterialComponent component;
  component.material = MaterialReference::project(ProjectAssetId::generate());
  component.material_slots[7u] =
      MaterialReference::project(ProjectAssetId::generate());
  for (const MaterialComponent *source : {&component,
                                         static_cast<MaterialComponent *>(nullptr)}) {
    const auto binding =
        build_render_material_binding_snapshot(assets, source, &resolver);
    ASSERT_TRUE(binding.present);
    EXPECT_TRUE(binding.material_slots.empty());
    EXPECT_TRUE(binding.component.material_slots.empty());
    ASSERT_EQ(binding.primary_material.textures.size(), 1u);
    EXPECT_EQ(binding.primary_material.textures.front().texture.source_path, path);
    EXPECT_FALSE(binding.primary_material.reference.project_owned());
  }
  EXPECT_EQ(resolver.resolve_count, 0);
  EXPECT_FALSE(release_has_feature(ReleaseFeature::Materials));
}

TEST(PublicReleaseRenderTests, DefaultImageRepublishesStationaryPreviewOnlyOnChange) {
  ProjectSession project;
  ProjectSessionRenderMaterialResolver resolver(project);
  SceneRenderBridge bridge;
  bridge.set_project_material_resolver(&resolver);
  SceneSession scene;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(scene)};
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ModelingSessionService &modeling = scene.modeling();
  const ModelingBoxToolEvent start{
      .screen_position = {400.0f, 300.0f},
      .ray = {.origin = {4.0f, 4.0f, 4.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  ModelingBoxToolEvent footprint_end = start;
  footprint_end.ray.origin = {6.0f, 4.0f, 7.0f};
  ASSERT_TRUE(modeling.begin_box_footprint(start).ok);
  ASSERT_TRUE(modeling.update_box_footprint(footprint_end).ok);
  ASSERT_TRUE(modeling.begin_box_height(start).ok);
  ModelingBoxToolEvent height_end = start;
  height_end.screen_position.y = 240.0f;
  const ModelingBoxPreview preview = modeling.update_box_height(height_end);
  ASSERT_TRUE(preview.ok);
  ASSERT_TRUE(preview.valid);
  ASSERT_GT(preview.distance, 0.0);
  const auto scene_revision = editor.query().revisions().scene_revision;
  std::size_t publications = 0u;
  std::filesystem::path displayed_texture;
  const auto publish = [&](std::vector<RenderToolPreviewMeshRecord> meshes) {
    ++publications;
    EXPECT_FALSE(meshes.empty());
    if (!meshes.empty()) {
      const auto &textures = meshes.front().material.primary_material.textures;
      EXPECT_EQ(textures.size(), 1u);
      if (!textures.empty()) displayed_texture = textures.front().texture.source_path;
    }
    return ToolPreviewSubmissionResult::Published;
  };
  const auto sync = [&] {
    return QuaderRenderApiRuntimeAccess::sync_tool_previews(
        editor.render(), bridge, publish).ok();
  };
  ASSERT_TRUE(sync());
  EXPECT_EQ(publications, 1u);
  ASSERT_TRUE(sync());
  EXPECT_EQ(publications, 1u);
  const auto path = std::filesystem::temp_directory_path() / "quader-chosen.png";
  resolver.set_default_material_texture(path);
  ASSERT_TRUE(sync());
  EXPECT_EQ(publications, 2u);
  EXPECT_EQ(displayed_texture, path);
  resolver.set_default_material_texture(path);
  ASSERT_TRUE(sync());
  EXPECT_EQ(publications, 2u);
  resolver.set_default_material_texture({});
  ASSERT_TRUE(sync());
  EXPECT_EQ(publications, 3u);
  EXPECT_EQ(displayed_texture.generic_string(),
            kDefaultMaterialAlbedoTexturePackagePath);
  EXPECT_EQ(editor.query().revisions().scene_revision, scene_revision);
  EXPECT_DOUBLE_EQ(modeling.box_preview().distance, preview.distance);
  EXPECT_TRUE(editor.query().modeling_objects().empty());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(PublicReleaseRenderTests, DefaultImageCacheRetiresOldTexturesAndReusesStableFrames) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  QImage red(4, 4, QImage::Format_RGBA8888);
  red.fill(Qt::red);
  QImage blue(4, 4, QImage::Format_RGBA8888);
  blue.fill(Qt::blue);
  const auto red_path = directory.filePath(QStringLiteral("red.png"));
  const auto blue_path = directory.filePath(QStringLiteral("blue.png"));
  ASSERT_TRUE(red.save(red_path));
  ASSERT_TRUE(blue.save(blue_path));
  ProjectSession project;
  ProjectSessionRenderMaterialResolver resolver(project);
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  harness.materials().set_project_material_resolver(&resolver);
  SceneDocument document;
  AssetRegistry assets;
  const auto mesh_id = assets.add_mesh(public_release_triangle());
  const auto entity = document.create_entity("Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(entity, {.mesh = mesh_id}));
  const auto sync = [&] {
    const auto snapshot = build_render_scene_snapshot(
        document, assets, {.project_material_resolver = &resolver});
    EXPECT_EQ(snapshot.meshes.size(), 1u);
    if (!snapshot.meshes.empty()) harness.cache().sync_mesh(snapshot.meshes.front());
  };
  resolver.set_default_material_texture(red_path.toStdWString());
  sync();
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().upload_count, 1u);
  for (int frame = 0; frame < 4; ++frame) sync();
  EXPECT_EQ(harness.stats().upload_count, 1u);
  for (const auto &path : {blue_path, red_path, blue_path}) {
    resolver.set_default_material_texture(path.toStdWString());
    sync();
    ASSERT_TRUE(harness.frame());
    EXPECT_EQ(harness.stats().resource_count, 2u);
    harness.drain_retired();
    ASSERT_TRUE(harness.frame());
    EXPECT_EQ(harness.stats().resource_count, 1u);
  }
  EXPECT_EQ(harness.stats().upload_count, 4u);
  harness.cache().shutdown();
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);

  ToolPreviewRenderableCache previews;
  previews.initialize(harness.factory(), harness.materials(),
                      harness.engine().getMaxAutomaticInstances());
  const MeshAsset preview_mesh = public_release_triangle();
  const auto submit_preview = [&] {
    return previews.set_meshes(build_render_tool_preview_snapshot(
        {ToolPreviewMesh{.source_entity = entity, .mesh = preview_mesh}},
        assets, nullptr, &resolver));
  };
  const auto previous_uploads = harness.stats().upload_count;
  resolver.set_default_material_texture(red_path.toStdWString());
  ASSERT_EQ(submit_preview(), ToolPreviewSubmissionResult::Published);
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().upload_count, previous_uploads + 1u);
  EXPECT_EQ(submit_preview(), ToolPreviewSubmissionResult::Unchanged);
  EXPECT_EQ(harness.stats().upload_count, previous_uploads + 1u);
  resolver.set_default_material_texture(blue_path.toStdWString());
  ASSERT_EQ(submit_preview(), ToolPreviewSubmissionResult::Published);
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().upload_count, previous_uploads + 2u);
  EXPECT_EQ(harness.stats().resource_count, 2u)
      << "The visible blue generation and hidden red predecessor own different textures.";
  EXPECT_EQ(submit_preview(), ToolPreviewSubmissionResult::Unchanged);
  EXPECT_EQ(harness.stats().upload_count, previous_uploads + 2u);
  previews.shutdown();
  ASSERT_TRUE(harness.frame());
  EXPECT_EQ(harness.stats().resource_count, 0u);
  EXPECT_EQ(harness.stats().upload_count, harness.stats().destroy_count);
}

TEST(PublicReleaseRenderTests, RejectsExcludedShadingAndSanitizesRestoredModes) {
  ASSERT_EQ(current_product_release(), kPublicRelease01);
  QuaderViewportApi viewports;
  constexpr auto kPane = ViewportPaneId::Perspective;
  ASSERT_TRUE(viewports.set_shading_mode(kPane, QuaderShadingMode::Solid).ok());
  constexpr std::array kBlocked{
      QuaderShadingMode::Rendered, QuaderShadingMode::DiffuseOnly,
      QuaderShadingMode::SpecularOnly, QuaderShadingMode::Albedo,
      QuaderShadingMode::Roughness, QuaderShadingMode::Metalness,
      QuaderShadingMode::Normal};
  for (const auto mode : kBlocked) {
    const auto result = viewports.set_shading_mode(kPane, mode);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(viewports.shading_mode(kPane), QuaderShadingMode::Solid);
  }
  for (const auto mode : {QuaderShadingMode::Wireframe, QuaderShadingMode::Shaded,
                          QuaderShadingMode::Solid}) {
    EXPECT_TRUE(viewports.set_shading_mode(kPane, mode).ok());
    EXPECT_EQ(viewports.shading_mode(kPane), mode);
  }
  ViewportWorkspaceService workspace;
  workspace.pane(kPane).perspective_shading = ViewportShadingMode::Rendered;
  EXPECT_EQ(workspace.shading_mode(kPane), ViewportShadingMode::Shaded);
  EXPECT_FALSE(workspace.set_shading_mode(kPane, ViewportShadingMode::Albedo));
  workspace.pane(ViewportPaneId::Top).orthographic_shading =
      ViewportShadingMode::Normal;
  EXPECT_EQ(workspace.shading_mode(ViewportPaneId::Top),
            ViewportShadingMode::Wireframe);
}

TEST(PublicReleaseRenderTests, PreferencesRejectBeforeCallingRuntimeHooks) {
  QuaderPreferencesApi preferences;
  int writes = 0;
  QuaderPreferencesRuntimeHooks hooks;
  const auto write = [&](bool) {
    ++writes;
    return QuaderApiVoidResult{.ok = true, .changed = true};
  };
  hooks.set_mesh_grid_visible = write;
  hooks.set_texture_lock_enabled = write;
  hooks.set_texture_lock_scale_enabled = write;
  hooks.mesh_grid_visible = [] { return true; };
  hooks.texture_lock_enabled = [] { return false; };
  hooks.texture_lock_scale_enabled = [] { return true; };
  preferences.set_runtime_hooks(std::move(hooks));
  for (const auto &result : {
           preferences.set_mesh_grid_visible(true),
           preferences.set_texture_lock_enabled(false),
           preferences.set_texture_lock_scale_enabled(true)}) {
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
  }
  EXPECT_EQ(writes, 0);
  EXPECT_FALSE(preferences.mesh_grid_visible());
  EXPECT_TRUE(preferences.texture_lock_enabled());
  EXPECT_FALSE(preferences.texture_lock_scale_enabled());
}

TEST(PublicReleaseRenderTests, HiddenSettingsStayInactiveAndPreserveStoredOverrides) {
  const auto path = std::filesystem::temp_directory_path() /
      ("quader-public-settings-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
  {
    std::ofstream file(path);
    file << "[Viewport]\n"
         << "QVertexWorks.Quader.Viewport.ShowMeshGrid=true\n"
         << "QVertexWorks.Quader.Viewport.TextureLock=false\n"
         << "QVertexWorks.Quader.Viewport.TextureLockScale=true\n";
  }
  auto settings = ViewportSettings::load(path);
  EXPECT_FALSE(settings.bool_value(ViewportBoolSettingId::ShowMeshGrid));
  EXPECT_FALSE(settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, true));
  EXPECT_FALSE(settings.set_bool_value(ViewportBoolSettingId::TextureLock, false));
  EXPECT_FALSE(settings.set_bool_value(ViewportBoolSettingId::TextureLockScale, true));
  EXPECT_FALSE(settings.set_color(ViewportColorSettingId::MeshGridMinor,
                                  {1.0f, 0.0f, 0.0f, 1.0f}));
  for (const auto &descriptor : viewport_bool_settings()) {
    EXPECT_NE(descriptor.id, ViewportBoolSettingId::ShowMeshGrid);
    EXPECT_NE(descriptor.id, ViewportBoolSettingId::TextureLock);
    EXPECT_NE(descriptor.id, ViewportBoolSettingId::TextureLockScale);
  }
  for (const auto &descriptor : viewport_color_settings()) {
    EXPECT_NE(descriptor.id, ViewportColorSettingId::MeshGridMinor);
    EXPECT_NE(descriptor.id, ViewportColorSettingId::MeshGridMajor);
  }
  for (const auto &descriptor : viewport_settings_sections()) {
    EXPECT_NE(descriptor.id, ViewportSettingsSectionId::Texturing);
  }
  (void)settings.reset_to_defaults();
  EXPECT_TRUE(settings.save(path));
  std::ifstream saved(path);
  const std::string text{std::istreambuf_iterator<char>(saved), {}};
  EXPECT_NE(text.find("Viewport.ShowMeshGrid=true"), std::string::npos);
  EXPECT_NE(text.find("Viewport.TextureLock=false"), std::string::npos);
  EXPECT_NE(text.find("Viewport.TextureLockScale=true"), std::string::npos);
  saved.close();
  std::error_code error;
  std::filesystem::remove(path, error);
}

TEST(PublicReleaseRenderTests, ProjectDefaultAndPerFaceOverridesUsePackagedMaterial) {
  AssetRegistry assets;
  CountingProjectMaterialResolver resolver;
  const auto project_default = ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(project_default.has_value());
  MaterialComponent component;
  component.material = MaterialReference::project(*project_default);
  component.material_slots[7u] = MaterialReference::project(ProjectAssetId::generate());
  expect_packaged_default(
      build_render_material_binding_snapshot(assets, &component, &resolver));
  expect_packaged_default(
      build_render_material_binding_snapshot(assets, nullptr, &resolver));
  EXPECT_EQ(resolver.resolve_count, 0);
}

TEST(PublicReleaseRenderTests, MeshInstancesAndPreviewsShareDefaultWithoutMutatingAssets) {
  SceneDocument document;
  AssetRegistry assets;
  MaterialDocument authored;
  authored.base_color = {0.2f, 0.8f, 0.1f};
  authored.metallic = 0.9f;
  const AssetId material_id = assets.add_material(authored);
  MaterialComponent material{.material = material_id};
  material.material_slots[7u] = material_id;
  const AssetId mesh_id = assets.add_mesh(public_release_triangle());
  const EntityId mesh = document.create_entity("Mesh", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(mesh, {.mesh = mesh_id}));
  ASSERT_TRUE(document.set_material(mesh, material));
  const EntityId definition = document.create_entity("Definition", EntityKind::Empty);
  ASSERT_TRUE(document.set_instance_definition(definition));
  const EntityId member = document.create_entity("Member", EntityKind::Mesh);
  ASSERT_TRUE(document.set_mesh(member, {.mesh = mesh_id}));
  ASSERT_TRUE(document.set_material(member, material));
  ASSERT_TRUE(document.set_parent(member, definition, false));
  const EntityId occurrence = document.create_entity("Occurrence", EntityKind::Instance);
  ASSERT_TRUE(document.set_instance_occurrence(occurrence, {.definition = definition}));
  const auto asset_revision = assets.revision();
  const auto scene_revision = document.revision();
  const auto snapshot = build_render_scene_snapshot(document, assets);
  ASSERT_FALSE(snapshot.meshes.empty());
  for (const auto &record : snapshot.meshes) {
    expect_packaged_default(record.material);
  }
  ASSERT_FALSE(snapshot.instance_primitives.empty());
  for (const auto &record : snapshot.instance_primitives) {
    expect_packaged_default(record.material);
  }
  const auto unchanged = build_render_scene_snapshot(
      document, assets, {.previous = &snapshot});
  EXPECT_TRUE(unchanged.reused_previous_records);
  const auto previews = build_render_tool_preview_snapshot(
      {ToolPreviewMesh{.source_entity = mesh, .material = material,
                       .mesh = public_release_triangle()}}, assets);
  ASSERT_EQ(previews.size(), 1u);
  expect_packaged_default(previews.front().material);
  EXPECT_EQ(assets.revision(), asset_revision);
  EXPECT_EQ(document.revision(), scene_revision);
  EXPECT_FLOAT_EQ(assets.material(material_id)->base_color.x, 0.2f);
  EXPECT_EQ(*document.material(mesh), material);
}

TEST(PublicReleaseRenderTests, CameraClipSettingsWorkButMixedEnvironmentEditsAreAtomic) {
  auto scenario = BehaviorScenario::new_project();
  auto &editor = scenario.editor();
  auto settings = editor.query().scene_settings();
  settings.camera.near_plane = 0.1f;
  settings.camera.far_plane = 2500.0f;
  ASSERT_TRUE(editor.scene().set_scene_settings(settings).ok());
  const auto before = editor.query().revisions();
  const auto original = editor.query().scene_settings();
  settings.camera.far_plane = 3000.0f;
  settings.environment.intensity += 1.0f;
  const auto rejected = editor.scene().set_scene_settings(settings);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_FLOAT_EQ(editor.query().scene_settings().camera.far_plane,
                  original.camera.far_plane);
  EXPECT_FLOAT_EQ(editor.query().scene_settings().environment.intensity,
                  original.environment.intensity);
  EXPECT_EQ(editor.query().revisions().scene_settings_revision,
            before.scene_settings_revision);
  EXPECT_EQ(editor.query().revisions().scene_revision, before.scene_revision);
}

TEST(PublicReleaseRenderTests, CameraExposureEditsAreUnavailableWithoutChangingHistory) {
  auto scenario = BehaviorScenario::new_project();
  auto &editor = scenario.editor();
  const auto original = editor.query().scene_settings();
  const auto before = editor.query().revisions();
  const bool could_undo = editor.history().can_undo();
  const std::array<std::function<void(QuaderSceneSettings &)>, 5> changes{
      [](auto &settings) { settings.camera.use_exposure = !settings.camera.use_exposure; },
      [](auto &settings) { settings.camera.aperture += 1.0f; },
      [](auto &settings) { settings.camera.shutter_speed *= 2.0f; },
      [](auto &settings) { settings.camera.sensitivity += 50.0f; },
      [](auto &settings) {
        settings.camera.near_plane = 0.2f;
        settings.camera.far_plane = 2500.0f;
        settings.camera.aperture += 1.0f;
      },
  };
  for (const auto &change : changes) {
    auto settings = original;
    change(settings);
    const auto result = editor.scene().set_scene_settings(settings);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    const auto actual = editor.query().scene_settings();
    EXPECT_EQ(actual.camera.use_exposure, original.camera.use_exposure);
    EXPECT_FLOAT_EQ(actual.camera.aperture, original.camera.aperture);
    EXPECT_FLOAT_EQ(actual.camera.shutter_speed, original.camera.shutter_speed);
    EXPECT_FLOAT_EQ(actual.camera.sensitivity, original.camera.sensitivity);
    EXPECT_FLOAT_EQ(actual.camera.near_plane, original.camera.near_plane);
    EXPECT_FLOAT_EQ(actual.camera.far_plane, original.camera.far_plane);
    EXPECT_EQ(editor.query().revisions().scene_settings_revision,
              before.scene_settings_revision);
    EXPECT_EQ(editor.query().revisions().scene_revision, before.scene_revision);
    EXPECT_EQ(editor.history().can_undo(), could_undo);
  }
}

TEST(PublicReleaseRenderTests, VulkanShadedMeshInstanceAndPreviewUsePackagedDefaultPixels) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_TEXTURE_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_TEXTURE_TESTS=1 for windowless "
                    "public-release Shaded pixel verification.";
  }
  ASSERT_EQ(current_product_release(), kPublicRelease01);
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN));
  harness.initialize_scene_bridge();
  const auto resource_root = std::filesystem::path(
      qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
  ASSERT_TRUE(std::filesystem::is_regular_file(
      resource_root / kDefaultMaterialAlbedoTexturePackagePath));

  MeshAsset mesh;
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      MeshVertex{.position = {-1.0f, -1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {0.0f, 0.0f}},
      MeshVertex{.position = {1.0f, -1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {1.0f, 0.0f}},
      MeshVertex{.position = {1.0f, 1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {1.0f, 1.0f}},
      MeshVertex{.position = {-1.0f, 1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {0.0f, 1.0f}},
  };
  geometry.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry.primitives = {
      MeshPrimitiveSpan{.index_offset = 0u, .index_count = 6u}};
  const auto mesh_snapshot = render_mesh_asset_snapshot(1u, mesh);
  AssetRegistry assets;
  const auto default_binding =
      build_render_material_binding_snapshot(assets, nullptr);
  RenderMaterialBindingSnapshot authored_binding;
  authored_binding.present = true;
  authored_binding.primary_material.present = true;
  authored_binding.primary_material.document.base_color = {1.0f, 0.0f, 0.0f};
  authored_binding.primary_material.document.metallic = 1.0f;
  authored_binding.primary_material.normal_strength = 0.0f;

  const auto capture_mesh = [&](const RenderMaterialBindingSnapshot &material,
                                bool shared_instance) {
    auto source = harness.factory().create(
        1u, mesh_snapshot, &material, {}, false, true,
        kViewportShadedSceneLayerMask, true);
    EXPECT_TRUE(source.entity);
    EXPECT_EQ(harness.stats().resource_count, 1u)
        << "The packaged albedo must be uploaded instead of using white fallback.";
    std::optional<MeshRenderableProxy> instance;
    if (shared_instance) {
      instance.emplace(harness.factory().create_shared_instance(
          2u, mesh_snapshot, source, {}, kViewportShadedSceneLayerMask));
      EXPECT_TRUE(instance->entity);
      harness.factory().remove_from_scene(source);
    }
    QImage image = harness.capture_scene(1);
    if (shared_instance) {
      harness.factory().destroy(*instance);
    }
    harness.factory().destroy(source);
    return image;
  };
  const QImage baseline = capture_mesh(default_binding, false);
  ASSERT_FALSE(baseline.isNull());
  std::size_t nonzero_pixels = 0u;
  std::size_t black_pixels = 0u;
  int maximum_channel = 0;
  for (int y = 0; y < baseline.height(); ++y) {
    for (int x = 0; x < baseline.width(); ++x) {
      const QRgb pixel = baseline.pixel(x, y);
      const int maximum = std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)});
      maximum_channel = std::max(maximum_channel, maximum);
      nonzero_pixels += maximum > 0 ? 1u : 0u;
      black_pixels += maximum == 0 ? 1u : 0u;
    }
  }
  // Readback is linear RGBA8: the dark packaged albedo appears around 1-5,
  // before display encoding. Require both surface coverage and contrast with
  // the black clear color, rather than applying an sRGB brightness threshold.
  ASSERT_GT(nonzero_pixels, 100u) << "Shaded must display a visible surface.";
  ASSERT_GT(black_pixels, 100u) << "The surface must remain framed by background.";
  ASSERT_GE(maximum_channel, 2) << "Shaded surface must contrast with black.";
  const auto expect_same_pixels = [&](const QImage &image) {
    ASSERT_FALSE(image.isNull());
    ASSERT_EQ(image.size(), baseline.size());
    std::size_t changed_pixels = 0u;
    for (int y = 0; y < baseline.height(); ++y) {
      for (int x = 0; x < baseline.width(); ++x) {
        const QRgb expected = baseline.pixel(x, y);
        const QRgb actual = image.pixel(x, y);
        if (std::abs(qRed(expected) - qRed(actual)) > 1 ||
            std::abs(qGreen(expected) - qGreen(actual)) > 1 ||
            std::abs(qBlue(expected) - qBlue(actual)) > 1) {
          ++changed_pixels;
        }
      }
    }
    EXPECT_EQ(changed_pixels, 0u);
  };
  expect_same_pixels(capture_mesh(authored_binding, false));
  expect_same_pixels(capture_mesh(authored_binding, true));

  const AssetId authored_material =
      assets.add_material(authored_binding.primary_material.document);
  const auto preview = build_render_tool_preview_snapshot(
      {ToolPreviewMesh{.source_entity = 1u,
                       .material = {.material = authored_material},
                       .mesh = mesh}}, assets);
  ASSERT_NE(harness.bridge().set_tool_preview_meshes(preview),
            ToolPreviewSubmissionResult::Failed);
  expect_same_pixels(harness.capture_scene(1));
}

TEST(PublicReleaseRenderTests, VulkanDefaultImageChangesExistingMeshesInstancesAndPreviews) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_TEXTURE_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_TEXTURE_TESTS=1 for default-image pixels.";
  }
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  QImage image(4, 4, QImage::Format_RGBA8888);
  image.fill(Qt::red);
  const auto red_path = directory.filePath(QStringLiteral("red.png"));
  ASSERT_TRUE(image.save(red_path));
  image.fill(Qt::blue);
  const auto blue_path = directory.filePath(QStringLiteral("blue.png"));
  ASSERT_TRUE(image.save(blue_path));

  ProjectSession project;
  ProjectSessionRenderMaterialResolver resolver(project);
  TextureLifetimeHarness harness;
  ASSERT_TRUE(harness.initialize(filament::Engine::Backend::VULKAN));
  harness.initialize_scene_bridge();
  harness.bridge().set_project_material_resolver(&resolver);
  MeshAsset mesh;
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      MeshVertex{.position = {-1.0f, -1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {0.0f, 0.0f}},
      MeshVertex{.position = {1.0f, -1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {1.0f, 0.0f}},
      MeshVertex{.position = {1.0f, 1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {1.0f, 1.0f}},
      MeshVertex{.position = {-1.0f, 1.0f, 0.0f},
                 .normal = {0.0f, 0.0f, 1.0f}, .uv = {0.0f, 1.0f}},
  };
  geometry.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry.primitives = {
      MeshPrimitiveSpan{.index_offset = 0u, .index_count = 6u}};
  const auto expect_color = [&](bool red) {
    const QImage pixels = harness.capture_scene(1);
    ASSERT_FALSE(pixels.isNull());
    std::size_t colored = 0u;
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        const QRgb pixel = pixels.pixel(x, y);
        const int wanted = red ? qRed(pixel) : qBlue(pixel);
        const int unwanted = red ? qBlue(pixel) : qRed(pixel);
        colored += wanted > unwanted + 20 ? 1u : 0u;
      }
    }
    EXPECT_GT(colored, 100u);
  };

  for (int kind = 0; kind < 3; ++kind) {
    SCOPED_TRACE(kind == 0 ? "mesh" : kind == 1 ? "instance" : "preview");
    SceneDocument document;
    AssetRegistry assets;
    const auto mesh_id = assets.add_mesh(mesh);
    const auto entity = document.create_entity("Mesh", EntityKind::Mesh);
    ASSERT_TRUE(document.set_mesh(entity, {.mesh = mesh_id}));
    if (kind == 1) {
      const auto definition = document.create_entity("Definition", EntityKind::Empty);
      ASSERT_TRUE(document.set_instance_definition(definition));
      ASSERT_TRUE(document.set_parent(entity, definition, false));
      const auto occurrence = document.create_entity("Occurrence", EntityKind::Instance);
      ASSERT_TRUE(document.set_instance_occurrence(occurrence, {.definition = definition}));
    }
    const RenderSceneBindingKey binding{
        .scene_session_id = static_cast<std::uint64_t>(kind + 1)};
    const auto sync = [&] {
      harness.bridge().sync(document, assets, 0u, binding);
      if (kind == 2) {
        const auto preview = build_render_tool_preview_snapshot(
            {ToolPreviewMesh{.source_entity = entity, .mesh = mesh,
                             .suppress_source = true}},
            assets, nullptr, &resolver);
        EXPECT_NE(harness.bridge().set_tool_preview_meshes(preview),
                  ToolPreviewSubmissionResult::Failed);
      }
    };
    resolver.set_default_material_texture(red_path.toStdWString());
    sync();
    expect_color(true);
    resolver.set_default_material_texture(blue_path.toStdWString());
    sync();
    expect_color(false);
    const auto stable_uploads = harness.bridge().diagnostics().gpu_geometry_upload_count;
    sync();
    expect_color(false);
    EXPECT_EQ(harness.bridge().diagnostics().gpu_geometry_upload_count, stable_uploads);
  }
}

} // namespace
} // namespace quader::test
