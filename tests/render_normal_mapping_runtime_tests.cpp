////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_material_library.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/png_texture_loader.hpp"
#include "render/texture_binding_service.hpp"

#include <gtest/gtest.h>

#include <QString>

#include <backend/PixelBufferDescriptor.h>
#include <filament/Camera.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::uint32_t kTargetSize = 64u;
constexpr std::array<std::byte, 4u> kNormalTexel{
    std::byte{166u}, std::byte{205u}, std::byte{220u}, std::byte{255u}};

QVec3 transform_direction(const QMat4 &matrix, QVec3 direction) {
  const auto &m = matrix.values;
  return {m[0u] * direction.x + m[4u] * direction.y + m[8u] * direction.z,
          m[1u] * direction.x + m[5u] * direction.y + m[9u] * direction.z,
          m[2u] * direction.x + m[6u] * direction.y + m[10u] * direction.z};
}

float orientation_sign(const QMat4 &matrix) {
  return dot(transform_direction(matrix, {1.0f, 0.0f, 0.0f}),
             cross(transform_direction(matrix, {0.0f, 1.0f, 0.0f}),
                   transform_direction(matrix, {0.0f, 0.0f, 1.0f}))) < 0.0f
             ? -1.0f
             : 1.0f;
}

/// Measures the production shader without a window or presentation swapchain.
class NormalMappingHarness final {
public:
  ~NormalMappingHarness() {
    if (engine_ == nullptr) {
      return;
    }
    (void)engine_->flushAndWait(10'000'000'000ull);
    engine_->pumpMessageQueues();
    if (proxy_) {
      factory_.destroy(*proxy_);
    }
    factory_.shutdown();
    materials_.shutdown();
    textures_.shutdown();
    library_.shutdown();
    if (view_) {
      engine_->destroy(view_);
    }
    if (target_) {
      engine_->destroy(target_);
    }
    for (filament::Texture *texture : {color_, depth_, neutral_}) {
      if (texture) {
        engine_->destroy(texture);
      }
    }
    if (camera_entity_) {
      engine_->destroyCameraComponent(camera_entity_);
      utils::EntityManager::get().destroy(camera_entity_);
    }
    if (renderer_) {
      engine_->destroy(renderer_);
    }
    if (scene_) {
      engine_->destroy(scene_);
    }
    filament::Engine::destroy(&engine_);
  }

  bool initialize() {
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) {
      return false;
    }
    const std::filesystem::path resources(
        qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
    if (!library_.initialize(*engine_, resources)) {
      return false;
    }
    const std::array<std::byte, 4u> gray{
        std::byte{181u}, std::byte{181u}, std::byte{181u}, std::byte{255u}};
    neutral_ = load_rgba_texture(*engine_, gray, 1u, 1u);
    if (neutral_ == nullptr) {
      return false;
    }
    textures_.initialize(*engine_, resources, *neutral_);
    materials_.initialize(library_.quader_mesh(), library_.quader_mesh_lit(),
                          textures_);
    materials_.bind_scene({.scene_session_id = 1u});
    scene_ = engine_->createScene();
    factory_.initialize(*engine_, *scene_, materials_);
    renderer_ = engine_->createRenderer();
    view_ = engine_->createView();
    color_ = filament::Texture::Builder()
                 .width(kTargetSize)
                 .height(kTargetSize)
                 .levels(1u)
                 .format(filament::Texture::InternalFormat::RGBA16F)
                 .usage(filament::Texture::Usage::COLOR_ATTACHMENT |
                        filament::Texture::Usage::SAMPLEABLE |
                        filament::Texture::Usage::BLIT_SRC)
                 .build(*engine_);
    depth_ = filament::Texture::Builder()
                 .width(kTargetSize)
                 .height(kTargetSize)
                 .levels(1u)
                 .format(filament::Texture::InternalFormat::DEPTH24)
                 .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
                 .build(*engine_);
    target_ = filament::RenderTarget::Builder()
                  .texture(filament::RenderTarget::AttachmentPoint::COLOR,
                           color_)
                  .texture(filament::RenderTarget::AttachmentPoint::DEPTH,
                           depth_)
                  .build(*engine_);
    camera_entity_ = utils::EntityManager::get().create();
    camera_ = engine_->createCamera(camera_entity_);
    camera_->setProjection(45.0, 1.0, 0.1, 20.0);
    view_->setCamera(camera_);
    view_->setScene(scene_);
    view_->setRenderTarget(target_);
    view_->setViewport({0u, 0u, kTargetSize, kTargetSize});
    view_->setVisibleLayers(0xffu, kViewportRenderedSceneLayerMask);
    view_->setPostProcessingEnabled(false);
    view_->setShadowingEnabled(false);
    view_->setAntiAliasing(filament::View::AntiAliasing::NONE);
    view_->setDithering(filament::View::Dithering::NONE);
    view_->setTemporalAntiAliasingOptions({.enabled = false});
    view_->setMultiSampleAntiAliasingOptions({.enabled = false});
    view_->setMaterialGlobal(0u, filament::math::float4{6.0f, 0.0f, 0.0f, 1.0f});
    renderer_->setClearOptions({.clearColor = {0.0f, 0.0f, 0.0f, 1.0f},
                               .clear = true});

    material_.present = true;
    material_.primary_material.present = true;
    material_.component.material = MaterialReference::project(
        *ProjectAssetId::parse("ba7a9abf-49e5-40ea-87de-bbb76220493f"));
    material_.primary_material.reference = material_.component.material;
    RenderTextureAssetSnapshot texture;
    texture.slot = material_texture_slot::kNormal;
    texture.project_texture_id = *ProjectAssetId::parse(
        "cf884f27-fcbe-410b-99d9-f14926590162");
    texture.content_revision = 1u;
    texture.texture.color_space = TextureColorSpace::Linear;
    texture.decoded_rgba8 = std::make_shared<const std::vector<std::byte>>(
        kNormalTexel.begin(), kNormalTexel.end());
    texture.width = 1u;
    texture.height = 1u;
    texture.present = true;
    material_.primary_material.textures.push_back(std::move(texture));
    return true;
  }

  std::optional<QVec3>
  render_normal(QVec3 normal, bool mirror_authored_u,
                const RenderStandardMaterialTextureTransformSnapshot &transform,
                const QMat4 &world_transform) {
    const auto pixel = render_surface(normal, mirror_authored_u, transform,
                                       world_transform, false, 1.0f, 1.0f, 0.0f);
    if (!pixel) {
      return std::nullopt;
    }
    return normalize(*pixel * 2.0f - QVec3{1.0f, 1.0f, 1.0f});
  }

  std::optional<QVec3> render_shaded(QVec3 normal, float normal_strength,
                                     float roughness = 1.0f,
                                     float metallic = 0.0f) {
    return render_surface(normal, false, {}, {}, true, normal_strength,
                           roughness, metallic);
  }

private:
  std::optional<QVec3> render_surface(
      QVec3 normal, bool mirror_authored_u,
      const RenderStandardMaterialTextureTransformSnapshot &transform,
      const QMat4 &world_transform, bool shaded, float normal_strength,
      float roughness, float metallic) {
    if (proxy_) {
      factory_.destroy(*proxy_);
      proxy_.reset();
    }
    const QVec3 u = normal.x != 0.0f ? QVec3{0.0f, 1.0f, 0.0f}
                                    : QVec3{1.0f, 0.0f, 0.0f};
    const QVec3 v = cross(normal, u);
    const std::array<QVec2, 4u> corners{
        QVec2{0.0f, 0.0f}, QVec2{1.0f, 0.0f}, QVec2{1.0f, 1.0f},
        QVec2{0.0f, 1.0f}};
    for (std::size_t index = 0u; index < vertices_.size(); ++index) {
      const QVec2 uv = corners[index];
      vertices_[index] = {
          .position = u * (uv.x * 2.0f - 1.0f) + v * (uv.y * 2.0f - 1.0f),
          .normal = normal,
          .uv = {mirror_authored_u ? 1.0f - uv.x : uv.x, uv.y},
          .uv_geometric_normal = normal};
    }
    const RenderMeshAssetSnapshot mesh{.mesh_id = 1u,
                                       .mesh_revision = ++mesh_revision_,
                                       .vertices = vertices_,
                                       .indices = indices_,
                                       .present = true};
    material_.primary_material.texture_transform = transform;
    material_.primary_material.normal_strength = normal_strength;
    material_.primary_material.document.roughness = roughness;
    material_.primary_material.document.metallic = metallic;
    proxy_.emplace(factory_.create(1u, mesh, &material_, world_transform, false,
                                   true, kViewportRenderedSceneLayerMask, shaded));
    if (!proxy_->entity || proxy_->material_instances.empty()) {
      return std::nullopt;
    }
    for (filament::MaterialInstance *instance : proxy_->material_instances) {
      instance->setParameter("surfaceGridEnabled", 0.0f);
    }
    const QVec3 world_u = transform_direction(world_transform, u);
    const QVec3 world_v = transform_direction(world_transform, v);
    const QVec3 world_normal = normalize(cross(world_u, world_v)) *
                               orientation_sign(world_transform);
    const QVec3 world_up = normalize(world_v);
    camera_->lookAt(
        {world_normal.x * 4.0, world_normal.y * 4.0, world_normal.z * 4.0},
        {0.0, 0.0, 0.0}, {world_up.x, world_up.y, world_up.z});
    view_->setMaterialGlobal(
        0u, filament::math::float4{shaded ? 0.0f : 6.0f, 0.0f, 0.0f, 1.0f});
    for (int frame = 0; frame < 3; ++frame) {
      renderer_->renderStandaloneView(view_);
    }
    struct Readback final {
      std::array<float, 4u> pixel{};
      std::atomic<bool> complete{false};
    };
    auto readback = std::make_shared<Readback>();
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        target_, kTargetSize / 2u, kTargetSize / 2u, 1u, 1u,
        filament::backend::PixelBufferDescriptor(
            readback->pixel.data(), sizeof(readback->pixel),
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::FLOAT,
            [](void *, std::size_t, void *user) {
              auto *holder = static_cast<std::shared_ptr<Readback> *>(user);
              (*holder)->complete.store(true, std::memory_order_release);
              delete holder;
            },
            owner));
    for (int attempt = 0; attempt < 10 &&
                          !readback->complete.load(std::memory_order_acquire);
         ++attempt) {
      if (!engine_->flushAndWait(1'000'000'000ull)) {
        return std::nullopt;
      }
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    const auto &pixel = readback->pixel;
    return QVec3{pixel[0u], pixel[1u], pixel[2u]};
  }

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::View *view_ = nullptr;
  filament::Camera *camera_ = nullptr;
  filament::RenderTarget *target_ = nullptr;
  filament::Texture *color_ = nullptr;
  filament::Texture *depth_ = nullptr;
  filament::Texture *neutral_ = nullptr;
  utils::Entity camera_entity_;
  FilamentMaterialLibrary library_;
  TextureBindingService textures_;
  MaterialBindingService materials_;
  MeshRenderableFactory factory_;
  RenderMaterialBindingSnapshot material_;
  std::optional<MeshRenderableProxy> proxy_;
  std::array<MeshVertex, 4u> vertices_{};
  const std::array<std::uint32_t, 6u> indices_{0u, 1u, 2u, 0u, 2u, 3u};
  std::uint64_t mesh_revision_ = 0u;
};

TEST(RenderNormalMappingRuntimeTests,
     PackedAndTransformedChartsPreserveNormalDirectionsOnEveryFaceAxis) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan "
                   "normal-map readback coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize());

  struct ChartCase final {
    const char *name;
    RenderStandardMaterialTextureTransformSnapshot transform;
    QVec2 tangent_in_face;
    QVec2 normal_y_in_face;
    bool near_identity = false;
  };
  // These signed axis expectations come from the authored chart: normal-map
  // +X follows increasing U, while +Y follows decreasing sampled V (image up).
  // Quarter-turns and reflections permit exact expectations without copying
  // either the CPU tangent packer or shader derivative reconstruction.
  const std::array<ChartCase, 6u> charts{{
      {"identity", {}, {1.0f, 0.0f}, {0.0f, -1.0f}, true},
      {"epsilon scale", {.scale = {1.0001f, 1.0f}},
       {1.0f, 0.0f}, {0.0f, -1.0f}, true},
      {"epsilon rotation", {.rotation_degrees = 0.001f},
       {1.0f, 0.0f}, {0.0f, -1.0f}, true},
      {"quarter turn", {.rotation_degrees = 90.0f},
       {0.0f, -1.0f}, {-1.0f, 0.0f}},
      {"mirror U", {.scale = {-1.0f, 1.0f}},
       {-1.0f, 0.0f}, {0.0f, -1.0f}},
      {"mirror V", {.scale = {1.0f, -1.0f}},
       {1.0f, 0.0f}, {0.0f, 1.0f}},
  }};
  const std::array<QVec3, 6u> normals{
      QVec3{1.0f, 0.0f, 0.0f}, QVec3{-1.0f, 0.0f, 0.0f},
      QVec3{0.0f, 1.0f, 0.0f}, QVec3{0.0f, -1.0f, 0.0f},
      QVec3{0.0f, 0.0f, 1.0f}, QVec3{0.0f, 0.0f, -1.0f}};
  const QVec3 sampled = normalize(QVec3{
      static_cast<float>(std::to_integer<unsigned>(kNormalTexel[0u])) *
              (2.0f / 255.0f) - 1.0f,
      static_cast<float>(std::to_integer<unsigned>(kNormalTexel[1u])) *
              (2.0f / 255.0f) - 1.0f,
      static_cast<float>(std::to_integer<unsigned>(kNormalTexel[2u])) *
              (2.0f / 255.0f) - 1.0f});
  std::array<QMat4, 4u> occurrences;
  occurrences[1u].values[0u] = 1.5f;
  occurrences[1u].values[5u] = 0.75f;
  occurrences[1u].values[10u] = 2.0f;
  occurrences[2u] = occurrences[1u];
  occurrences[2u].values[0u] = -1.5f;
  // A sheared, nonuniform occurrence rotated a quarter-turn about Z.
  occurrences[3u].values = {0.0f, 1.5f, 0.0f, 0.0f,
                            -0.75f, 0.3f, 0.0f, 0.0f,
                            0.0f, 0.0f, 2.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, 1.0f};
  for (std::size_t occurrence = 0u; occurrence < occurrences.size();
       ++occurrence) {
    SCOPED_TRACE(occurrence);
    const QMat4 &world = occurrences[occurrence];
    for (std::size_t face = 0u; face < normals.size(); ++face) {
      SCOPED_TRACE(face);
      const QVec3 n = normals[face];
      const QVec3 u = n.x != 0.0f ? QVec3{0.0f, 1.0f, 0.0f}
                                 : QVec3{1.0f, 0.0f, 0.0f};
      const QVec3 v = cross(n, u);
      const QVec3 world_u = transform_direction(world, u);
      const QVec3 world_v = transform_direction(world, v);
      const QVec3 world_n = normalize(cross(world_u, world_v)) *
                            orientation_sign(world);
      for (const bool mirrored : {false, true}) {
        SCOPED_TRACE(mirrored ? "authored U reflected" : "authored U forward");
        QVec3 baseline;
        for (const ChartCase &chart : charts) {
          SCOPED_TRACE(chart.name);
          const auto actual =
              harness.render_normal(n, mirrored, chart.transform, world);
          ASSERT_TRUE(actual.has_value());
          const QVec3 authored_u = mirrored ? world_u * -1.0f : world_u;
          const QVec3 tangent = normalize(
              authored_u * chart.tangent_in_face.x +
              world_v * chart.tangent_in_face.y);
          const QVec3 image_up = authored_u * chart.normal_y_in_face.x +
                                 world_v * chart.normal_y_in_face.y;
          // The tangent-space axes remain unit and perpendicular after an
          // occurrence stretches or shears the authored face in world space.
          const QVec3 normal_y =
              normalize(image_up - tangent * dot(tangent, image_up));
          const QVec3 expected = tangent * sampled.x + normal_y * sampled.y +
                                 world_n * sampled.z;
          EXPECT_LT(length(*actual - expected), 0.006f)
              << "actual (" << actual->x << ", " << actual->y << ", "
              << actual->z << "), expected (" << expected.x << ", "
              << expected.y << ", " << expected.z << ')';
          if (&chart == charts.data()) {
            baseline = *actual;
          } else if (chart.near_identity) {
            EXPECT_LT(length(*actual - baseline), 0.002f)
                << "Entering the derivative mapping path must not flip a normal";
          }
        }
      }
    }
  }
}

TEST(RenderNormalMappingRuntimeTests,
     ShadedRetainsDirectionalShapeAndNormalDetailWithoutPbrVariation) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan "
                   "Shaded color readback coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize());

  const std::array<QVec3, 6u> normals{
      QVec3{1.0f, 0.0f, 0.0f}, QVec3{-1.0f, 0.0f, 0.0f},
      QVec3{0.0f, 1.0f, 0.0f}, QVec3{0.0f, -1.0f, 0.0f},
      QVec3{0.0f, 0.0f, 1.0f}, QVec3{0.0f, 0.0f, -1.0f}};
  // Frozen white-material reference levels preserve the chosen appearance:
  // sky-facing surfaces are brightest, ground-facing surfaces remain visible,
  // and the two upright axes remain distinguishable. No scene lights exist.
  const std::array<float, 6u> reference_levels{
      0.637f, 0.637f, 1.027f, 0.267f, 0.467f, 0.467f};
  std::array<QVec3, 6u> flat_colors;
  std::array<QVec3, 6u> mapped_colors;
  std::size_t faces_with_visible_normal_detail = 0u;
  for (std::size_t face = 0u; face < normals.size(); ++face) {
    SCOPED_TRACE(face);
    const auto flat = harness.render_shaded(normals[face], 0.0f);
    const auto mapped = harness.render_shaded(normals[face], 1.0f);
    ASSERT_TRUE(flat.has_value());
    ASSERT_TRUE(mapped.has_value());
    flat_colors[face] = *flat;
    mapped_colors[face] = *mapped;
    EXPECT_NEAR(flat->x, reference_levels[face], 0.003f);
    EXPECT_NEAR(flat->y, reference_levels[face], 0.003f);
    EXPECT_NEAR(flat->z, reference_levels[face], 0.003f);
    if (length(*mapped - *flat) > 0.05f) {
      ++faces_with_visible_normal_detail;
    }
  }
  EXPECT_GE(faces_with_visible_normal_detail, 4u);
  EXPECT_GT(flat_colors[2u].x, flat_colors[3u].x * 3.0f);
  EXPECT_GT(flat_colors[0u].x, flat_colors[4u].x + 0.15f);

  const auto polished_metal = harness.render_shaded(normals[2u], 1.0f, 0.0f, 1.0f);
  ASSERT_TRUE(polished_metal.has_value());
  EXPECT_LT(length(*polished_metal - mapped_colors[2u]), 0.001f)
      << "Shaded material roughness and metallic must not change modeling light";
  const auto restored_flat = harness.render_shaded(normals[2u], 0.0f);
  ASSERT_TRUE(restored_flat.has_value());
  EXPECT_LT(length(*restored_flat - flat_colors[2u]), 0.001f);
}

} // namespace
} // namespace quader
