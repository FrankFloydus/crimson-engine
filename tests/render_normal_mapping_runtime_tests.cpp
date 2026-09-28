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
#include "render/tool_preview_renderable_cache.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <QDir>
#include <QImage>
#include <QString>

#include <backend/PixelBufferDescriptor.h>
#include <filament/Camera.h>
#include <filament/LightManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Texture.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::uint32_t kTargetSize = 64u;
constexpr std::uint32_t kGridTargetSize = 256u;
constexpr std::array<std::byte, 4u> kNormalTexel{
    std::byte{166u}, std::byte{205u}, std::byte{220u}, std::byte{255u}};

struct SurfaceGridOptions final {
  bool enabled = true;
  bool textured = false;
  bool lit = true;
  bool oblique = false;
  float albedo = 1.0f;
  float minor_alpha = 1.0f;
  float major_alpha = 1.0f;
  float debug_mode = 0.0f;
};

struct SurfaceGridImage final {
  std::vector<float> rgba;

  QVec3 pixel(int x, int y) const {
    const auto offset = (static_cast<std::size_t>(y) * kGridTargetSize +
                         static_cast<std::size_t>(x)) * 4u;
    return {rgba[offset], rgba[offset + 1u], rgba[offset + 2u]};
  }

  bool save(const QString &path) const {
    QImage image(kGridTargetSize, kGridTargetSize, QImage::Format_RGB32);
    const auto encoded = [](float value) {
      value = std::clamp(value, 0.0f, 1.0f);
      const float srgb = value <= 0.0031308f
                             ? value * 12.92f
                             : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
      return static_cast<int>(std::lround(srgb * 255.0f));
    };
    for (int y = 0; y < static_cast<int>(kGridTargetSize); ++y) {
      for (int x = 0; x < static_cast<int>(kGridTargetSize); ++x) {
        const QVec3 color = pixel(x, y);
        image.setPixel(x, static_cast<int>(kGridTargetSize) - 1 - y,
                       qRgb(encoded(color.x), encoded(color.y),
                            encoded(color.z)));
      }
    }
    return image.save(path);
  }
};

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
    previews_.shutdown();
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
    if (grid_light_) {
      engine_->destroy(grid_light_);
      utils::EntityManager::get().destroy(grid_light_);
    }
    if (renderer_) {
      engine_->destroy(renderer_);
    }
    if (scene_) {
      engine_->destroy(scene_);
    }
    filament::Engine::destroy(&engine_);
  }

  bool initialize(std::uint32_t target_size = kTargetSize) {
    target_size_ = target_size;
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) {
      return false;
    }
    const std::filesystem::path resources(
        qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
    const QString grid_materials =
        target_size_ == kGridTargetSize
            ? qEnvironmentVariable("QUADER_VULKAN_GRID_MATERIAL_DIR")
            : QString{};
    const std::filesystem::path material_resources =
        grid_materials.isEmpty()
            ? resources
            : std::filesystem::path(grid_materials.toStdWString());
    if (!library_.initialize(*engine_, material_resources)) {
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
                 .width(target_size_)
                 .height(target_size_)
                 .levels(1u)
                 .format(filament::Texture::InternalFormat::RGBA16F)
                 .usage(filament::Texture::Usage::COLOR_ATTACHMENT |
                        filament::Texture::Usage::SAMPLEABLE |
                        filament::Texture::Usage::BLIT_SRC)
                 .build(*engine_);
    depth_ = filament::Texture::Builder()
                 .width(target_size_)
                 .height(target_size_)
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
    view_->setViewport({0u, 0u, target_size_, target_size_});
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

  std::optional<SurfaceGridImage> render_grid(bool shaded,
                                             const SurfaceGridOptions &grid) {
    if (target_size_ != kGridTargetSize) {
      return std::nullopt;
    }
    if (!grid_light_) {
      grid_light_ = utils::EntityManager::get().create();
      filament::LightManager::Builder(filament::LightManager::Type::DIRECTIONAL)
          .color({1.0f, 1.0f, 1.0f})
          .intensity(65000.0f)
          .direction({0.0f, 0.0f, -1.0f})
          .castShadows(false)
          .build(*engine_, grid_light_);
      if (!engine_->getLightManager().hasComponent(grid_light_)) {
        return std::nullopt;
      }
    }
    if (grid.lit) {
      scene_->addEntity(grid_light_);
    } else {
      scene_->remove(grid_light_);
    }
    material_.primary_material.document.base_color =
        QVec3{grid.albedo, grid.albedo, grid.albedo};
    material_.primary_material.textures.clear();
    if (grid.textured) {
      constexpr std::uint32_t kTextureSize = 64u;
      auto pixels = std::make_shared<std::vector<std::byte>>(
          kTextureSize * kTextureSize * 4u);
      for (std::uint32_t y = 0u; y < kTextureSize; ++y) {
        for (std::uint32_t x = 0u; x < kTextureSize; ++x) {
          // Alternating dark/light tiles with deterministic grain and mortar
          // exercise detail that can camouflage a single-color grid line.
          const bool dark = ((x / 16u) + (y / 16u)) % 2u == 0u;
          const unsigned grain = (x * 17u + y * 29u + x * y * 3u) % 17u;
          const unsigned value = x % 16u == 0u || y % 16u == 0u
                                     ? 40u
                                     : (dark ? 3u : 220u) + grain;
          const std::size_t offset = (y * kTextureSize + x) * 4u;
          for (std::size_t channel = 0u; channel < 3u; ++channel) {
            (*pixels)[offset + channel] = static_cast<std::byte>(value);
          }
          (*pixels)[offset + 3u] = std::byte{255u};
        }
      }
      RenderTextureAssetSnapshot texture;
      texture.slot = material_texture_slot::kBaseColor;
      texture.project_texture_id = *ProjectAssetId::parse(
          "e83ddff2-3a3e-49c1-b5fd-9f60ec5b93f7");
      texture.content_revision = 1u;
      texture.texture.color_space = TextureColorSpace::Linear;
      texture.decoded_rgba8 = std::move(pixels);
      texture.width = kTextureSize;
      texture.height = kTextureSize;
      texture.present = true;
      material_.primary_material.textures.push_back(std::move(texture));
    }
    SurfaceGridImage image;
    if (!render_surface({0.0f, 0.0f, 1.0f}, false, {}, {}, shaded,
                        0.0f, 1.0f, 0.0f, &grid, &image)) {
      return std::nullopt;
    }
    return image;
  }

  RenderMaterialBindingSnapshot preview_material() const { return material_; }

  std::optional<SurfaceGridImage> render_preview_meshes(
      const std::vector<RenderToolPreviewMeshRecord> &meshes) {
    if (!previews_.initialized()) previews_.initialize(factory_, materials_, 64u);
    if (previews_.set_meshes(meshes) == ToolPreviewSubmissionResult::Failed) return std::nullopt;
    camera_->setProjection(filament::Camera::Projection::ORTHO,
                           -1.5, 1.5, -1.5, 1.5, 0.1, 20.0);
    camera_->lookAt({0.0, 0.0, 4.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    return read_current_mesh();
  }

  ToolPreviewCacheWorkCounters preview_counters() const {
    return previews_.work_counters();
  }

  std::optional<SurfaceGridImage> render_mesh_snapshot(
      const RenderMeshAssetSnapshot &mesh, bool dynamic_geometry = false,
      std::span<const QVec3> positions = {}) {
    previews_.shutdown();
    if (proxy_) factory_.destroy(*proxy_);
    proxy_.emplace(factory_.create(1u, mesh, &material_, {}, false, true,
        kViewportRenderedSceneLayerMask, false, false,
        kViewportDefaultRenderableChannel, dynamic_geometry, positions));
    if (!proxy_->entity) return std::nullopt;
    for (auto *instance : proxy_->material_instances) {
      instance->setParameter("surfaceGridEnabled", 0.0f);
    }
    camera_->setProjection(filament::Camera::Projection::ORTHO,
                           -1.5, 1.5, -1.5, 1.5, 0.1, 20.0);
    camera_->lookAt({0.0, 0.0, 4.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
    return read_current_mesh();
  }

  bool update_prepared_mesh(const RenderMeshAssetSnapshot &mesh,
      std::span<const QVec3> positions,
      std::shared_ptr<MeshRenderableFactory::DynamicGeometryUpdate> prepared) {
    return proxy_.has_value() && factory_.update_dynamic_geometry(
        *proxy_, mesh, positions, nullptr, std::move(prepared));
  }

  std::optional<SurfaceGridImage> read_current_mesh() {
    SurfaceGridImage image;
    if (!read_surface(&image)) return std::nullopt;
    return image;
  }

  QVec2 project_grid_position(QVec3 world_position) const {
    const auto clip = camera_->getProjectionMatrix() * camera_->getViewMatrix() *
        filament::math::double4{world_position.x, world_position.y,
                                world_position.z, 1.0};
    return {static_cast<float>((clip.x / clip.w * 0.5 + 0.5) * kGridTargetSize),
            static_cast<float>((clip.y / clip.w * 0.5 + 0.5) * kGridTargetSize)};
  }

  std::optional<SurfaceGridImage> render_current_grid_view(bool visible) {
    // Retain the exact same proxy, vertex buffers, and material instances.
    view_->setMaterialGlobal(
        0u, filament::math::float4{0.0f, 0.0f, visible ? 0.0f : 1.0f, 1.0f});
    SurfaceGridImage image;
    if (!read_surface(&image).has_value()) {
      return std::nullopt;
    }
    return image;
  }

private:
  std::optional<QVec3> render_surface(
      QVec3 normal, bool mirror_authored_u,
      const RenderStandardMaterialTextureTransformSnapshot &transform,
      const QMat4 &world_transform, bool shaded, float normal_strength,
      float roughness, float metallic,
      const SurfaceGridOptions *grid = nullptr,
      SurfaceGridImage *image = nullptr) {
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
      if (grid != nullptr) {
        ViewportSettings settings = ViewportSettings::defaults();
        settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid,
                                 grid->enabled);
        auto minor = settings.color(ViewportColorSettingId::MeshGridMinor);
        auto major = settings.color(ViewportColorSettingId::MeshGridMajor);
        minor.a = grid->minor_alpha;
        major.a = grid->major_alpha;
        settings.set_color(ViewportColorSettingId::MeshGridMinor, minor);
        settings.set_color(ViewportColorSettingId::MeshGridMajor, major);
        materials_.apply_mesh_surface_grid(*instance, 0.25f, settings);
      } else {
        instance->setParameter("surfaceGridEnabled", 0.0f);
      }
    }
    const QVec3 world_u = transform_direction(world_transform, u);
    const QVec3 world_v = transform_direction(world_transform, v);
    const QVec3 world_normal = normalize(cross(world_u, world_v)) *
                               orientation_sign(world_transform);
    const QVec3 world_up = normalize(world_v);
    camera_->lookAt(
        {world_normal.x * 4.0, world_normal.y * 4.0, world_normal.z * 4.0},
        {0.0, 0.0, 0.0}, {world_up.x, world_up.y, world_up.z});
    if (grid != nullptr) {
      if (grid->oblique) {
        camera_->setProjection(45.0, 1.0, 0.1, 20.0);
        camera_->lookAt({0.8, -1.4, 2.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
      } else {
        camera_->setProjection(filament::Camera::Projection::ORTHO,
                                -1.0, 1.0, -1.0, 1.0, 0.1, 20.0);
      }
    }
    view_->setMaterialGlobal(
        0u, filament::math::float4{grid != nullptr ? grid->debug_mode
                                                 : (shaded ? 0.0f : 6.0f),
                                  0.0f, 0.0f, 1.0f});
    return read_surface(image);
  }

  std::optional<QVec3> read_surface(SurfaceGridImage *image) {
    for (int frame = 0; frame < 3; ++frame) {
      renderer_->renderStandaloneView(view_);
    }
    struct Readback final {
      std::vector<float> pixels;
      std::atomic<bool> complete{false};
    };
    auto readback = std::make_shared<Readback>();
    const std::uint32_t read_size = image != nullptr ? target_size_ : 1u;
    readback->pixels.resize(static_cast<std::size_t>(read_size) * read_size * 4u);
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        target_, image != nullptr ? 0u : target_size_ / 2u,
        image != nullptr ? 0u : target_size_ / 2u, read_size, read_size,
        filament::backend::PixelBufferDescriptor(
            readback->pixels.data(), readback->pixels.size() * sizeof(float),
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
    const QVec3 result{readback->pixels[0u], readback->pixels[1u],
                       readback->pixels[2u]};
    if (image != nullptr) {
      image->rgba = std::move(readback->pixels);
    }
    return result;
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
  utils::Entity grid_light_;
  std::uint32_t target_size_ = kTargetSize;
  FilamentMaterialLibrary library_;
  TextureBindingService textures_;
  MaterialBindingService materials_;
  MeshRenderableFactory factory_;
  ToolPreviewRenderableCache previews_;
  RenderMaterialBindingSnapshot material_;
  std::optional<MeshRenderableProxy> proxy_;
  std::array<MeshVertex, 4u> vertices_{};
  const std::array<std::uint32_t, 6u> indices_{0u, 1u, 2u, 0u, 2u, 3u};
  std::uint64_t mesh_revision_ = 0u;
};

TEST(RenderNormalMappingRuntimeTests,
     PreparedDynamicUpdatesMatchCanonicalPixelsAndRetainUnchangedGroups) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize());
  MeshAsset original;
  MeshGeometryPayload geometry;
  geometry.vertices = {
      {.position = {-1.0f, -1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {0, 0},
       .rgba = 0xffffffffu, .source_vertex_id = 1u},
      {.position = {1.0f, -1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {1, 0},
       .rgba = 0xffffffffu, .source_vertex_id = 2u},
      {.position = {1.0f, 1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {1, 1},
       .rgba = 0xffffffffu, .source_vertex_id = 3u},
      {.position = {-1.0f, 1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {0, 1},
       .rgba = 0xffffffffu, .source_vertex_id = 4u}};
  geometry.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry.primitives = {{.index_count = 6u}};
  original.replace_geometry(std::move(geometry));
  MeshAsset changed = original;
  auto &deformed = changed.edit_geometry();
  deformed.indices = {0u, 1u, 3u, 1u, 2u, 3u};
  for (std::size_t index = 0u; index < deformed.vertices.size(); ++index) {
    auto &vertex = deformed.vertices[index];
    vertex.normal = normalize(QVec3{0.2f + 0.15f * static_cast<float>(index), 0.3f, 1.0f});
    vertex.uv = {vertex.uv.y, 1.0f - vertex.uv.x};
    vertex.uv_geometric_normal = vertex.normal;
  }
  deformed.vertices[2].position = {0.65f, 0.8f, 0.25f};
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : changed.geometry().vertices) positions->push_back(vertex.position);

  constexpr std::size_t kGroupCount = 64u;
  std::vector<RenderToolPreviewMeshRecord> previews(kGroupCount);
  for (std::size_t index = 0u; index < previews.size(); ++index) {
    auto &preview = previews[index];
    preview.source_object_id = static_cast<int>(index + 1u);
    preview.source_entity = static_cast<EntityId>(index + 1u);
    preview.mesh = original;
    preview.material = harness.preview_material();
    preview.material_reference = preview.material.component.material;
    preview.dynamic_positions = true;
    preview.layout_key = 1u;
    preview.position_revision = 1u;
    if (index != 0u) preview.transform.values[12] = 4.0f * static_cast<float>(index);
  }
  const auto baseline = harness.render_preview_meshes(previews);
  ASSERT_TRUE(baseline.has_value());
  const auto before = harness.preview_counters();
  for (auto &preview : previews) {
    preview.mesh = changed;
    preview.position_revision = 2u;
    preview.dynamic_position_payload = positions;
  }
  const auto prepared_image = harness.render_preview_meshes(previews);
  ASSERT_TRUE(prepared_image.has_value());
  const auto updated = harness.preview_counters();
  EXPECT_EQ(updated.dynamic_geometry_preparations - before.dynamic_geometry_preparations,
            kGroupCount);
  if (std::thread::hardware_concurrency() > 1u) {
    EXPECT_EQ(updated.parallel_dynamic_geometry_batches - before.parallel_dynamic_geometry_batches, 1u);
  }
  ASSERT_TRUE(harness.render_preview_meshes(previews).has_value());
  EXPECT_EQ(harness.preview_counters().dynamic_geometry_preparations,
            updated.dynamic_geometry_preparations);
  ++previews.back().position_revision;
  ASSERT_TRUE(harness.render_preview_meshes(previews).has_value());
  EXPECT_EQ(harness.preview_counters().dynamic_geometry_preparations,
            updated.dynamic_geometry_preparations + 1u);
  EXPECT_EQ(harness.preview_counters().parallel_dynamic_geometry_batches,
            updated.parallel_dynamic_geometry_batches);

  const auto snapshot = render_mesh_asset_snapshot(1u, changed);
  const auto canonical = harness.render_mesh_snapshot(snapshot);
  ASSERT_TRUE(canonical.has_value());
  ASSERT_EQ(canonical->rgba.size(), prepared_image->rgba.size());
  float changed_pixel = 0.0f;
  for (std::size_t index = 0u; index < canonical->rgba.size(); ++index) {
    EXPECT_NEAR(prepared_image->rgba[index], canonical->rgba[index], 0.004f) << index;
    changed_pixel = std::max(changed_pixel,
        std::abs(prepared_image->rgba[index] - baseline->rgba[index]));
  }
  EXPECT_GT(changed_pixel, 0.1f);

  const auto original_snapshot = render_mesh_asset_snapshot(1u, original);
  ASSERT_TRUE(harness.render_mesh_snapshot(original_snapshot, true).has_value());
  std::shared_ptr<MeshRenderableFactory::DynamicGeometryUpdate> upload;
  std::jthread worker([&] {
    upload = MeshRenderableFactory::prepare_dynamic_geometry_update(snapshot, *positions);
  });
  worker.join();
  ASSERT_NE(upload, nullptr);
  EXPECT_FALSE(harness.update_prepared_mesh(original_snapshot, *positions, upload));
  const std::vector<QVec3> other_positions = *positions;
  EXPECT_FALSE(harness.update_prepared_mesh(snapshot, other_positions, upload));
  ASSERT_TRUE(harness.update_prepared_mesh(snapshot, *positions, upload));
  EXPECT_FALSE(harness.update_prepared_mesh(snapshot, *positions, upload));
  const auto dynamic_image = harness.read_current_mesh();
  ASSERT_TRUE(dynamic_image.has_value());
  for (std::size_t index = 0u; index < canonical->rgba.size(); ++index) {
    EXPECT_NEAR(dynamic_image->rgba[index], canonical->rgba[index], 0.004f) << index;
  }
}

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

float surface_grid_luminance(QVec3 color) {
  return dot(color, QVec3{0.2126f, 0.7152f, 0.0722f});
}

float grid_neighborhood_change(const SurfaceGridImage &off,
                               const SurfaceGridImage &on, int x, int y) {
  float strongest_change = 0.0f;
  // Find the rasterized line without assuming its exact subpixel coverage.
  for (int offset = -3; offset <= 3; ++offset) {
    const float change = surface_grid_luminance(
        on.pixel(x + offset, y) - off.pixel(x + offset, y));
    if (std::abs(change) > std::abs(strongest_change)) {
      strongest_change = change;
    }
  }
  return strongest_change;
}

float grid_neighborhood_contrast(const SurfaceGridImage &off,
                                const SurfaceGridImage &on, int x, int y) {
  return std::abs(grid_neighborhood_change(off, on, x, y));
}

float image_maximum_difference(const SurfaceGridImage &left,
                               const SurfaceGridImage &right) {
  float difference = 0.0f;
  for (std::size_t index = 0u; index < left.rgba.size(); ++index) {
    difference = std::max(difference,
                           std::abs(left.rgba[index] - right.rgba[index]));
  }
  return difference;
}

void save_grid_capture(const SurfaceGridImage &image, const QString &label) {
  const QString directory = qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(directory));
    ASSERT_TRUE(image.save(QDir(directory).filePath(label + ".png")));
  }
}

TEST(RenderSurfaceGridRuntimeTests,
     PerViewVisibilityDoesNotMutateSharedMeshOrMaterialGrid) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "per-view surface-grid readback coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize(kGridTargetSize));
  for (const bool shaded : {true, false}) {
    SCOPED_TRACE(shaded ? "shaded" : "rendered");
    SurfaceGridOptions grid;
    grid.albedo = 0.02f;
    grid.enabled = false;
    const auto disabled_material = harness.render_grid(shaded, grid);
    grid.enabled = true;
    const auto visible = harness.render_grid(shaded, grid);
    const auto hidden_view = harness.render_current_grid_view(false);
    const auto restored_view = harness.render_current_grid_view(true);
    ASSERT_TRUE(disabled_material.has_value());
    ASSERT_TRUE(visible.has_value());
    ASSERT_TRUE(hidden_view.has_value());
    ASSERT_TRUE(restored_view.has_value());
    EXPECT_GT(grid_neighborhood_contrast(*hidden_view, *visible, 64, 48), 0.04f);
    EXPECT_LT(image_maximum_difference(*disabled_material, *hidden_view), 0.0001f);
    EXPECT_LT(image_maximum_difference(*visible, *restored_view), 0.0001f);
  }
}

TEST(RenderSurfaceGridRuntimeTests,
     FixedGrayMinorLinesLightenDarkSurfacesAndDarkenBrightSurfaces) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "surface-grid contrast readback coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize(kGridTargetSize));
  struct SurfaceCase final {
    const char *name;
    float albedo;
    bool textured;
  };
  constexpr std::array<SurfaceCase, 4u> kSurfaces{{
      {"dark", 0.02f, false}, {"gray", 0.45f, false},
      {"bright", 0.95f, false}, {"textured", 1.0f, true}}};
  for (const bool shaded : {true, false}) {
    for (const SurfaceCase &surface : kSurfaces) {
      const QString label = QString(shaded ? "shaded-" : "rendered-") +
                            surface.name;
      SCOPED_TRACE(label.toStdString());
      SurfaceGridOptions grid;
      grid.textured = surface.textured;
      grid.albedo = surface.albedo;
      grid.enabled = false;
      const auto off = harness.render_grid(shaded, grid);
      grid.enabled = true;
      const auto on = harness.render_grid(shaded, grid);
      ASSERT_TRUE(off.has_value());
      ASSERT_TRUE(on.has_value());
      save_grid_capture(*off, label + "-off");
      save_grid_capture(*on, label + "-on");

      float weakest_minor = 1.0f;
      float strongest_off_grid = 0.0f;
      std::size_t lightened_segments = 0u;
      std::size_t darkened_segments = 0u;
      // Orthographic world [-1, 1] maps each 0.25-unit minor interval to
      // 32 pixels. Skip the center major slot and sample between crossings.
      for (const int x : {32, 64, 96, 160, 192, 224}) {
        for (const int y : {16, 48, 80, 112, 144, 176, 208, 240}) {
          const float change = grid_neighborhood_change(*off, *on, x, y);
          weakest_minor = std::min(weakest_minor, std::abs(change));
          lightened_segments += change > 0.04f ? 1u : 0u;
          darkened_segments += change < -0.04f ? 1u : 0u;
          strongest_off_grid = std::max(strongest_off_grid,
              length(on->pixel(x + 12, y) - off->pixel(x + 12, y)));
        }
      }
      if (surface.albedo < 0.03f) {
        EXPECT_EQ(lightened_segments, 48u);
        EXPECT_EQ(darkened_segments, 0u);
      } else if (!surface.textured && surface.albedo > 0.9f) {
        EXPECT_EQ(darkened_segments, 48u);
        EXPECT_EQ(lightened_segments, 0u);
      } else if (surface.textured) {
        EXPECT_GE(lightened_segments, 8u);
        EXPECT_GE(darkened_segments, 8u);
      }
      // A fixed gray line can match a middle-gray surface. That is ordinary
      // alpha blending, so this case records appearance without a contrast floor.
      EXPECT_LT(strongest_off_grid, 0.0001f)
          << "The grid must not tint the material between lines";
      RecordProperty((label + "-minimum-minor-contrast").toStdString(),
                      std::to_string(weakest_minor));
      if (!surface.textured && surface.albedo < 0.03f && shaded) {
        EXPECT_LT(surface_grid_luminance(off->pixel(48, 48)), 0.06f)
            << "The dark fixture must exercise the near-black grid failure";
      }
    }
  }
}

TEST(RenderSurfaceGridRuntimeTests,
     AlphaBlendsLinearlyWithoutLeakingMinorLinesIntoMajorSlots) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "surface-grid opacity readback coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize(kGridTargetSize));
  for (const bool shaded : {true, false}) {
    SCOPED_TRACE(shaded ? "shaded" : "rendered");
    SurfaceGridOptions grid;
    grid.albedo = 0.02f;
    grid.enabled = false;
    const auto off = harness.render_grid(shaded, grid);
    grid.enabled = true;
    const auto full = harness.render_grid(shaded, grid);
    grid.minor_alpha = 0.0f;
    grid.major_alpha = 0.0f;
    const auto hidden = harness.render_grid(shaded, grid);
    grid.minor_alpha = 1.0f;
    const auto minor_only = harness.render_grid(shaded, grid);
    grid.minor_alpha = 0.0f;
    grid.major_alpha = 1.0f;
    const auto major_only = harness.render_grid(shaded, grid);
    grid.minor_alpha = 0.5f;
    grid.major_alpha = 0.5f;
    const auto half = harness.render_grid(shaded, grid);
    ASSERT_TRUE(off.has_value());
    ASSERT_TRUE(full.has_value());
    ASSERT_TRUE(hidden.has_value());
    ASSERT_TRUE(minor_only.has_value());
    ASSERT_TRUE(major_only.has_value());
    ASSERT_TRUE(half.has_value());
    EXPECT_LT(image_maximum_difference(*off, *hidden), 0.0001f);
    float interpolation_error = 0.0f;
    for (std::size_t index = 0u; index < off->rgba.size(); ++index) {
      const float halfway = (off->rgba[index] + full->rgba[index]) * 0.5f;
      interpolation_error = std::max(interpolation_error,
          std::abs(half->rgba[index] - halfway));
    }
    EXPECT_LT(interpolation_error, 0.001f)
        << "Half opacity must interpolate the measured off/on colors";
    for (const int y : {16, 48, 80, 112, 144, 176, 208, 240}) {
      EXPECT_GT(grid_neighborhood_contrast(*off, *minor_only, 64, y), 0.04f);
      EXPECT_LT(grid_neighborhood_contrast(*off, *minor_only, 128, y), 0.0001f)
          << "An invisible major line keeps its entire slot";
      EXPECT_LT(grid_neighborhood_contrast(*off, *major_only, 64, y), 0.0001f);
      EXPECT_GT(grid_neighborhood_contrast(*off, *major_only, 128, y), 0.04f);
    }
    const QString label = shaded ? "shaded-" : "rendered-";
    save_grid_capture(*minor_only, label + "major-hidden");
    save_grid_capture(*major_only, label + "minor-hidden");
    save_grid_capture(*half, label + "half-alpha");
  }
}

TEST(RenderSurfaceGridRuntimeTests,
     RenderedGridSurvivesUnlitSurfacesAndLeavesDiagnosticsUnchanged) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "surface-grid lighting readback coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize(kGridTargetSize));
  SurfaceGridOptions grid;
  grid.lit = false;
  grid.albedo = 0.02f;
  grid.enabled = false;
  const auto off = harness.render_grid(false, grid);
  grid.enabled = true;
  const auto on = harness.render_grid(false, grid);
  ASSERT_TRUE(off.has_value());
  ASSERT_TRUE(on.has_value());
  EXPECT_LT(surface_grid_luminance(off->pixel(48, 48)), 0.0001f);
  EXPECT_GT(grid_neighborhood_contrast(*off, *on, 64, 48), 0.04f);
  EXPECT_GT(grid_neighborhood_contrast(*off, *on, 128, 48), 0.04f);
  save_grid_capture(*off, "rendered-unlit-off");
  save_grid_capture(*on, "rendered-unlit-on");

  for (const float debug_mode : {3.0f, 6.0f}) {
    SCOPED_TRACE(debug_mode);
    grid.debug_mode = debug_mode;
    grid.enabled = false;
    const auto debug_off = harness.render_grid(false, grid);
    grid.enabled = true;
    const auto debug_on = harness.render_grid(false, grid);
    ASSERT_TRUE(debug_off.has_value());
    ASSERT_TRUE(debug_on.has_value());
    EXPECT_LT(image_maximum_difference(*debug_off, *debug_on), 0.0001f)
        << "Material diagnostics must continue to show their sampled values";
  }
}

TEST(RenderSurfaceGridRuntimeTests,
     FixedGrayGridRemainsVisibleOnAnObliqueTexturedPlane) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "perspective surface-grid readback coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize(kGridTargetSize));
  for (const bool shaded : {true, false}) {
    SCOPED_TRACE(shaded ? "shaded" : "rendered");
    SurfaceGridOptions grid;
    grid.textured = true;
    grid.oblique = true;
    grid.enabled = false;
    const auto off = harness.render_grid(shaded, grid);
    grid.enabled = true;
    const auto on = harness.render_grid(shaded, grid);
    ASSERT_TRUE(off.has_value());
    ASSERT_TRUE(on.has_value());
    std::size_t visible_segments = 0u;
    float strongest_contrast = 0.0f;
    for (const float x : {-0.5f, -0.25f, 0.25f, 0.5f}) {
      for (const float y : {-0.625f, -0.375f, -0.125f, 0.125f, 0.375f, 0.625f}) {
        const QVec2 pixel = harness.project_grid_position({x, y, 0.0f});
        const int center_x = static_cast<int>(std::floor(pixel.x));
        const int center_y = static_cast<int>(std::floor(pixel.y));
        ASSERT_GE(center_x, 3);
        ASSERT_GE(center_y, 3);
        ASSERT_LT(center_x + 3, static_cast<int>(kGridTargetSize));
        ASSERT_LT(center_y + 3, static_cast<int>(kGridTargetSize));
        float contrast = 0.0f;
        for (int row = -2; row <= 2; ++row) {
          contrast = std::max(contrast, grid_neighborhood_contrast(
              *off, *on, center_x, center_y + row));
        }
        visible_segments += contrast > 0.03f ? 1u : 0u;
        strongest_contrast = std::max(strongest_contrast, contrast);
      }
    }
    // Perspective and the texture may bring some samples close to grid gray.
    // Require visibility across multiple segments, not artificial adaptation.
    EXPECT_GE(visible_segments, 8u);
    EXPECT_GT(strongest_contrast, 0.08f);
    for (const QVec2 center : {QVec2{-0.375f, -0.375f}, QVec2{0.375f, -0.375f},
                               QVec2{-0.375f, 0.375f}, QVec2{0.375f, 0.375f}}) {
      const QVec2 pixel = harness.project_grid_position({center.x, center.y, 0.0f});
      const int x = static_cast<int>(std::floor(pixel.x));
      const int y = static_cast<int>(std::floor(pixel.y));
      EXPECT_LT(length(on->pixel(x, y) - off->pixel(x, y)), 0.0001f);
    }
    const QString label = shaded ? "shaded-oblique-textured" : "rendered-oblique-textured";
    save_grid_capture(*off, label + "-off");
    save_grid_capture(*on, label + "-on");
    RecordProperty((label + "-visible-segments").toStdString(),
                    std::to_string(visible_segments));
  }
}

} // namespace
} // namespace quader
