////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_material_library.hpp"
#include "render/editable_mesh_surface_cache.hpp"
#include "assets/mesh_geometry_projection.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/png_texture_loader.hpp"
#include "render/texture_binding_service.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
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
#include <unordered_set>
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
    editable_.shutdown();
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

  std::optional<SurfaceGridImage> render_debug_surface(
      const RenderMaterialBindingSnapshot &material, float debug_mode,
      const RenderStandardMaterialTextureTransformSnapshot &transform = {}) {
    material_ = material;
    SurfaceGridOptions grid;
    grid.debug_mode = debug_mode;
    SurfaceGridImage image;
    if (!render_surface({0.0f, 0.0f, 1.0f}, false, transform, {}, false,
                        0.0f, 1.0f, 0.0f, &grid, &image)) {
      return std::nullopt;
    }
    return image;
  }

  std::optional<SurfaceGridImage> render_current_debug_view(float debug_mode) {
    set_debug_view(debug_mode);
    return read_current_mesh();
  }

  void set_debug_view(float debug_mode) {
    view_->setMaterialGlobal(
        0u, filament::math::float4{debug_mode, 0.0f, 0.0f, 1.0f});
  }

  utils::Entity current_surface_entity() const {
    return proxy_ ? proxy_->entity : utils::Entity{};
  }

  std::vector<filament::MaterialInstance *> current_surface_materials() const {
    return proxy_ ? proxy_->material_instances
                  : std::vector<filament::MaterialInstance *>{};
  }

  TextureBindingService::CacheStats texture_counters() const {
    return textures_.cache_stats();
  }

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

  MeshRenderableFactory::GeometryCacheStats geometry_counters() const {
    return factory_.geometry_cache_stats();
  }

  std::size_t preview_proxy_count() const { return previews_.proxy_count(); }

  void drain_preview_retirement(std::size_t budget) {
    previews_.drain_retired_destroy_queue(budget);
  }

  void clear_previews() { previews_.clear(); }

  std::optional<SurfaceGridImage> render_standard_previews(
      const std::vector<RenderToolPreviewMeshRecord> &meshes, bool show_grid = false,
      QVec3 camera_target = {}, std::optional<float> debug_mode = {}) {
    std::vector<MeshRenderableProxy> proxies;
    bool complete = true;
    for (const auto &mesh : meshes) {
      const auto snapshot = render_mesh_asset_snapshot(kInvalidAssetId, mesh.mesh, mesh.geometry_projection);
      const auto positions = mesh.dynamic_position_payload
          ? std::span<const QVec3>(*mesh.dynamic_position_payload)
          : std::span<const QVec3>{};
      const std::size_t count = mesh.instance_transforms.empty()
          ? 1u : mesh.instance_transforms.size();
      for (std::size_t index = 0u; index < count; ++index) {
        const auto &transform = mesh.instance_transforms.empty()
            ? mesh.transform : mesh.instance_transforms[index];
        auto proxy = factory_.create(mesh.source_entity, snapshot, &mesh.material,
            transform, mesh.cast_shadows, mesh.double_sided,
            kViewportRenderedSceneLayerMask, false, false,
            kViewportDefaultRenderableChannel, true, positions);
        complete = complete && static_cast<bool>(proxy.entity);
        factory_.update_texture_mapping(proxy,
            index < mesh.instance_sampling_from_local.size()
                ? mesh.instance_sampling_from_local[index] : mesh.sampling_from_local);
        ViewportSettings settings = ViewportSettings::defaults();
        settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, show_grid);
        factory_.update_surface_grid(proxy, 0.25f, settings);
        proxies.push_back(std::move(proxy));
      }
    }
    camera_->setProjection(filament::Camera::Projection::ORTHO,
                           -1.5, 1.5, -1.5, 1.5, 0.1, 20.0);
    camera_->lookAt({camera_target.x, camera_target.y, static_cast<double>(camera_target.z) + 4.0},
                    {camera_target.x, camera_target.y, camera_target.z}, {0.0, 1.0, 0.0});
    set_debug_view(debug_mode.value_or(show_grid ? 0.0f : 6.0f));
    auto image = complete ? read_current_mesh() : std::nullopt;
    for (auto &proxy : proxies) factory_.destroy(proxy);
    return image;
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

  std::optional<SurfaceGridImage> render_editable_meshes(
      const RenderSceneSnapshot &base,
      const std::vector<RenderToolPreviewMeshRecord> &meshes,
      const RenderToolPreviewSuppressionSnapshot &suppression = {},
      bool show_grid = false, QVec3 camera_target = {},
      std::optional<float> debug_mode = {}) {
    if (!editable_.initialized()) editable_.initialize(factory_, materials_);
    if (!previews_.initialized()) previews_.initialize(factory_, materials_, 64u);
    if (editable_.prepare(base, meshes, suppression) == ToolPreviewSubmissionResult::Failed) return {};
    if (previews_.set_meshes(editable_.prepared_fallback_meshes()) == ToolPreviewSubmissionResult::Failed) {
      editable_.discard_prepared();
      return {};
    }
    editable_.publish_prepared();
    ViewportSettings settings = ViewportSettings::defaults();
    settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, show_grid);
    editable_.update_surface_grid(0.25f, settings);
    previews_.update_surface_grid(0.25f, settings);
    set_debug_view(debug_mode.value_or(show_grid ? 0.0f : 6.0f));
    camera_->setProjection(filament::Camera::Projection::ORTHO,
                           -1.5, 1.5, -1.5, 1.5, 0.1, 20.0);
    camera_->lookAt({camera_target.x, camera_target.y, static_cast<double>(camera_target.z) + 4.0},
                    {camera_target.x, camera_target.y, camera_target.z}, {0.0, 1.0, 0.0});
    return read_current_mesh();
  }

  EditableMeshSurfaceWorkCounters editable_counters() const { return editable_.work_counters(); }
  const RenderToolPreviewSuppressionSnapshot &editable_exclusion() const { return editable_.scene_exclusion(); }
  void clear_editable() { editable_.clear(); }

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
  EditableMeshSurfaceCache editable_;
  RenderMaterialBindingSnapshot material_;
  std::optional<MeshRenderableProxy> proxy_;
  std::array<MeshVertex, 4u> vertices_{};
  const std::array<std::uint32_t, 6u> indices_{0u, 1u, 2u, 0u, 2u, 3u};
  std::uint64_t mesh_revision_ = 0u;
};

void expect_same_pixels(const SurfaceGridImage &actual,
                        const SurfaceGridImage &expected) {
  ASSERT_EQ(actual.rgba.size(), expected.rgba.size());
  float maximum_error = 0.0f;
  for (std::size_t index = 0u; index < actual.rgba.size(); ++index) {
    maximum_error = std::max(maximum_error,
        std::abs(actual.rgba[index] - expected.rgba[index]));
  }
  EXPECT_LE(maximum_error, 0.004f);
}

float maximum_pixel_difference(const SurfaceGridImage &first,
                               const SurfaceGridImage &second) {
  float difference = 0.0f;
  for (std::size_t index = 0u; index < first.rgba.size(); ++index) {
    difference = std::max(difference,
        std::abs(first.rgba[index] - second.rgba[index]));
  }
  return difference;
}

std::optional<RenderMaterialBindingSnapshot> uv_checker_reference_material(
    RenderMaterialBindingSnapshot material) {
  QFile file(QDir(qEnvironmentVariable("QT_PLUGIN_PATH"))
                 .filePath("textures/uv_checker.png"));
  if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
  const QByteArray encoded = file.readAll();
  if (encoded.isEmpty()) return std::nullopt;
  const auto *begin = reinterpret_cast<const std::byte *>(encoded.constData());
  RenderTextureAssetSnapshot texture;
  texture.slot = material_texture_slot::kBaseColor;
  texture.project_texture_id = *ProjectAssetId::parse(
      "9ef0a00c-dfe6-4093-ad8d-f86f1d510850");
  texture.content_revision = 1u;
  texture.texture.color_space = TextureColorSpace::Srgb;
  texture.encoded_file_bytes = std::make_shared<const std::vector<std::byte>>(
      begin, begin + encoded.size());
  texture.present = true;
  material.primary_material.textures = {std::move(texture)};
  material.primary_material.document.base_color = {1.0f, 1.0f, 1.0f};
  material.primary_material.document.roughness = 1.0f;
  material.primary_material.document.metallic = 0.0f;
  material.primary_material.normal_strength = 0.0f;
  return material;
}

TEST(RenderNormalMappingRuntimeTests,
     UvDebugUsesPackagedCheckerAndRetainsSurfaceResourcesAcrossViewSwitches) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize(kGridTargetSize));
  ASSERT_TRUE(reference.initialize(kGridTargetSize));
  auto material = harness.preview_material();
  material.primary_material.document.base_color = {0.07f, 0.3f, 0.8f};
  const auto checker = uv_checker_reference_material(material);
  ASSERT_TRUE(checker);
  std::array<RenderStandardMaterialTextureTransformSnapshot, 2u> transforms;
  transforms[1].scale = {0.77f, 1.19f};
  transforms[1].offset = {0.21f, 0.13f};
  transforms[1].rotation_degrees = 29.0f;
  std::optional<SurfaceGridImage> first_checker;
  for (const auto &transform : transforms) {
    const auto ordinary = harness.render_debug_surface(material, 3.0f, transform);
    const auto expected = reference.render_debug_surface(*checker, 3.0f, transform);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(expected);
    const auto entity = harness.current_surface_entity();
    const auto instances = harness.current_surface_materials();
    const auto geometry_before = harness.geometry_counters();
    const auto textures_before = harness.texture_counters();
    const auto actual = harness.render_current_debug_view(7.0f);
    ASSERT_TRUE(actual);
    expect_same_pixels(*actual, *expected);
    EXPECT_GT(maximum_pixel_difference(*actual, *ordinary), 0.1f);
    for (auto *instance : instances) {
      instance->setParameter("baseColorFactor", filament::math::float4{
          material.primary_material.document.base_color.x,
          material.primary_material.document.base_color.y,
          material.primary_material.document.base_color.z, 0.0f});
      instance->setParameter("instanceEditDim", 1.0f);
      instance->setParameter("instanceEditContextStrength", 1.0f);
      instance->setParameter("instanceEditContextTintAmount", 1.0f);
    }
    const auto alpha_dimmed = harness.render_current_debug_view(7.0f);
    ASSERT_TRUE(alpha_dimmed);
    expect_same_pixels(*alpha_dimmed, *expected);
    for (auto *instance : instances) {
      instance->setParameter("baseColorFactor", filament::math::float4{
          material.primary_material.document.base_color.x,
          material.primary_material.document.base_color.y,
          material.primary_material.document.base_color.z, 1.0f});
      instance->setParameter("instanceEditDim", 0.0f);
    }
    const auto restored = harness.render_current_debug_view(3.0f);
    ASSERT_TRUE(restored);
    expect_same_pixels(*restored, *ordinary);
    EXPECT_EQ(harness.current_surface_entity(), entity);
    EXPECT_EQ(harness.current_surface_materials(), instances);
    EXPECT_EQ(harness.geometry_counters().upload_count, geometry_before.upload_count);
    EXPECT_EQ(harness.geometry_counters().resource_count, geometry_before.resource_count);
    EXPECT_EQ(harness.texture_counters().upload_count, textures_before.upload_count);
    EXPECT_EQ(harness.texture_counters().resource_count, textures_before.resource_count);
    if (first_checker) {
      EXPECT_GT(maximum_pixel_difference(*actual, *first_checker), 0.1f);
    } else {
      first_checker = actual;
    }
  }
}

std::vector<RenderToolPreviewMeshRecord> visible_dynamic_occurrences(
    const RenderMaterialBindingSnapshot &material) {
  MeshAsset mesh;
  MeshGeometryPayload geometry;
  geometry.vertices = {
      {.position = {-1.0f, -1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {-0.5f, 0.5f},
       .rgba = 0xffffffffu, .source_vertex_id = 1u,
       .uv_carrier = {1, 0, 0, 1}, .uv_geometric_normal = {0, 0, 1}},
      {.position = {1.0f, -1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {0.5f, 0.5f},
       .rgba = 0xffffffffu, .source_vertex_id = 2u,
       .uv_carrier = {1, 0, 0, 1}, .uv_geometric_normal = {0, 0, 1}},
      {.position = {1.0f, 1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {0.5f, -0.5f},
       .rgba = 0xffffffffu, .source_vertex_id = 3u,
       .uv_carrier = {1, 0, 0, 1}, .uv_geometric_normal = {0, 0, 1}},
      {.position = {-1.0f, 1.0f, 0.0f}, .normal = {0, 0, 1}, .uv = {-0.5f, -0.5f},
       .rgba = 0xffffffffu, .source_vertex_id = 4u,
       .uv_carrier = {1, 0, 0, 1}, .uv_geometric_normal = {0, 0, 1}}};
  geometry.indices = {0u, 1u, 2u, 0u, 2u, 3u};
  geometry.primitives = {{.index_count = 6u}};
  mesh.replace_geometry(std::move(geometry));
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : mesh.geometry().vertices) positions->push_back(vertex.position);
  std::array<QMat4, 6u> transforms;
  for (std::size_t index = 0u; index < transforms.size(); ++index) {
    auto &transform = transforms[index];
    transform.values[0] = 0.28f + 0.025f * static_cast<float>(index % 3u);
    transform.values[5] = 0.22f + 0.025f * static_cast<float>(index % 2u);
    transform.values[10] = 0.7f;
    transform.values[12] = (static_cast<float>(index % 3u) - 1.0f) * 0.95f;
    transform.values[13] = index < 3u ? -0.55f : 0.55f;
  }
  QMat4 quarter_turn;
  quarter_turn.values[0] = 0.0f;
  quarter_turn.values[1] = 1.0f;
  quarter_turn.values[4] = -1.0f;
  quarter_turn.values[5] = 0.0f;
  QMat4 mirrored_sampling;
  mirrored_sampling.values[0] = -1.0f;
  std::vector<RenderToolPreviewMeshRecord> records(3u);
  for (std::size_t index = 0u; index < records.size(); ++index) {
    auto &record = records[index];
    record.source_object_id = static_cast<int>(index + 1u);
    record.source_entity = static_cast<EntityId>(index + 1u);
    record.mesh = mesh;
    record.material = material;
    record.material_reference = material.component.material;
    record.double_sided = false;
    record.dynamic_positions = true;
    record.dynamic_position_payload = positions;
    record.layout_key = index + 1u;
    record.position_revision = index + 1u;
  }
  records[0].instance_transforms = {transforms[0], transforms[1], transforms[2]};
  records[0].instance_sampling_from_local = {QMat4{}, quarter_turn};
  records[0].sampling_from_local = mirrored_sampling;
  records[1].instance_transforms = {transforms[3], transforms[4]};
  records[1].instance_sampling_from_local = {quarter_turn, mirrored_sampling};
  records[2].transform = transforms[5];
  return records;
}

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
  MeshAsset detached = changed;
  (void)detached.edit_geometry();
  ASSERT_NE(detached.geometry_payload(), changed.geometry_payload());
  previews.back().mesh = detached;
  previews.back().dynamic_position_payload =
      std::make_shared<const std::vector<QVec3>>(*positions);
  const auto prepared_image = harness.render_preview_meshes(previews);
  ASSERT_TRUE(prepared_image.has_value());
  const auto updated = harness.preview_counters();
  EXPECT_EQ(updated.dynamic_geometry_preparations - before.dynamic_geometry_preparations,
            1u);
  EXPECT_EQ(updated.canonical_dynamic_geometry_preparations -
                before.canonical_dynamic_geometry_preparations, 1u);
  EXPECT_EQ(updated.reused_dynamic_geometry_preparations -
                before.reused_dynamic_geometry_preparations, 0u);
  EXPECT_EQ(updated.active_dynamic_geometry_families, 1u);
  EXPECT_EQ(updated.active_dynamic_geometry_participants, kGroupCount);
  EXPECT_EQ(updated.parallel_dynamic_geometry_batches,
            before.parallel_dynamic_geometry_batches);
  ASSERT_TRUE(harness.render_preview_meshes(previews).has_value());
  EXPECT_EQ(harness.preview_counters().dynamic_geometry_preparations,
            updated.dynamic_geometry_preparations);
  ++previews.back().position_revision;
  ASSERT_TRUE(harness.render_preview_meshes(previews).has_value());
  EXPECT_EQ(harness.preview_counters().dynamic_geometry_preparations,
            updated.dynamic_geometry_preparations);
  EXPECT_EQ(harness.preview_counters().parallel_dynamic_geometry_batches,
            updated.parallel_dynamic_geometry_batches);
  EXPECT_EQ(harness.preview_counters().canonical_dynamic_geometry_preparations,
            updated.canonical_dynamic_geometry_preparations);
  EXPECT_EQ(harness.preview_counters().reused_dynamic_geometry_preparations,
            updated.reused_dynamic_geometry_preparations);

  const auto before_unique = harness.preview_counters();
  for (std::size_t index = 0u; index < previews.size(); ++index) {
    auto unique_positions = std::make_shared<std::vector<QVec3>>(*positions);
    unique_positions->front().z += static_cast<float>(index) * 0.001f;
    previews[index].dynamic_position_payload = std::move(unique_positions);
    ++previews[index].position_revision;
  }
  const auto unique_image = harness.render_preview_meshes(previews);
  ASSERT_TRUE(unique_image.has_value());
  const auto unique = harness.preview_counters();
  EXPECT_EQ(unique.active_dynamic_geometry_families, kGroupCount);
  EXPECT_EQ(unique.dynamic_family_group_creations - before_unique.dynamic_family_group_creations,
            kGroupCount);
  EXPECT_EQ(unique.canonical_dynamic_geometry_preparations -
                before_unique.canonical_dynamic_geometry_preparations, 0u);
  EXPECT_EQ(unique.reused_dynamic_geometry_preparations,
            before_unique.reused_dynamic_geometry_preparations);
  for (std::size_t index = 0u; index < previews.size(); ++index) {
    auto unique_positions = std::make_shared<std::vector<QVec3>>(*positions);
    unique_positions->front().z += static_cast<float>(index) * 0.002f;
    previews[index].dynamic_position_payload = std::move(unique_positions);
    ++previews[index].position_revision;
  }
  ASSERT_TRUE(harness.render_preview_meshes(previews).has_value());
  const auto warmed = harness.preview_counters();
  EXPECT_EQ(warmed.canonical_dynamic_geometry_preparations -
                unique.canonical_dynamic_geometry_preparations, kGroupCount);
  EXPECT_EQ(warmed.dynamic_family_group_creations, unique.dynamic_family_group_creations);
  if (std::thread::hardware_concurrency() > 1u) {
    EXPECT_EQ(warmed.parallel_dynamic_geometry_batches -
                  unique.parallel_dynamic_geometry_batches, 1u);
  }

  const auto snapshot = render_mesh_asset_snapshot(1u, changed);
  const auto canonical = harness.render_mesh_snapshot(snapshot);
  ASSERT_TRUE(canonical.has_value());
  ASSERT_EQ(canonical->rgba.size(), prepared_image->rgba.size());
  float changed_pixel = 0.0f;
  for (std::size_t index = 0u; index < canonical->rgba.size(); ++index) {
    EXPECT_NEAR(prepared_image->rgba[index], canonical->rgba[index], 0.004f) << index;
    EXPECT_NEAR(unique_image->rgba[index], canonical->rgba[index], 0.004f) << index;
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
  const auto detached_snapshot = render_mesh_asset_snapshot(2u, detached);
  auto reused = MeshRenderableFactory::prepare_dynamic_geometry_update(
      detached_snapshot, other_positions, upload);
  ASSERT_NE(reused, nullptr);
  EXPECT_FALSE(harness.update_prepared_mesh(snapshot, other_positions, reused));
  auto different_positions = other_positions;
  different_positions.front().z += 0.1f;
  EXPECT_EQ(MeshRenderableFactory::prepare_dynamic_geometry_update(
                snapshot, different_positions, upload), nullptr);
  const auto rejects_changed_geometry = [&](auto change) {
    MeshAsset different = changed;
    change(different.edit_geometry());
    EXPECT_EQ(MeshRenderableFactory::prepare_dynamic_geometry_update(
                  render_mesh_asset_snapshot(2u, different), other_positions, upload), nullptr);
  };
  rejects_changed_geometry([](auto &value) { value.vertices.front().normal.x += 0.1f; });
  rejects_changed_geometry([](auto &value) { value.vertices.front().uv.x += 0.1f; });
  rejects_changed_geometry([](auto &value) { value.vertices.front().uv_carrier[0] += 0.1f; });
  rejects_changed_geometry([](auto &value) { value.vertices.front().uv_geometric_normal.x += 0.1f; });
  rejects_changed_geometry([](auto &value) { ++value.vertices.front().rgba; });
  rejects_changed_geometry([](auto &value) { ++value.vertices.front().source_vertex_id; });
  rejects_changed_geometry([](auto &value) { std::swap(value.indices[0], value.indices[1]); });
  rejects_changed_geometry([](auto &value) { value.primitives.front().index_count = 3u; });
  ASSERT_TRUE(harness.update_prepared_mesh(snapshot, *positions, upload));
  EXPECT_FALSE(harness.update_prepared_mesh(snapshot, *positions, upload));
  EXPECT_EQ(MeshRenderableFactory::prepare_dynamic_geometry_update(
                snapshot, *positions, upload), nullptr);
  upload.reset();
  ASSERT_TRUE(harness.update_prepared_mesh(detached_snapshot, other_positions, reused));
  EXPECT_FALSE(harness.update_prepared_mesh(detached_snapshot, other_positions, reused));
  const auto dynamic_image = harness.read_current_mesh();
  ASSERT_TRUE(dynamic_image.has_value());
  for (std::size_t index = 0u; index < canonical->rgba.size(); ++index) {
    EXPECT_NEAR(dynamic_image->rgba[index], canonical->rgba[index], 0.004f) << index;
  }
}

TEST(RenderNormalMappingRuntimeTests,
     DynamicFamiliesPreserveVisibleOccurrencesAcrossSplitMergeAndRejectedUpdates) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = visible_dynamic_occurrences(harness.preview_material());
  const auto original = records;
  const auto check_reference = [&]() {
    const auto actual = harness.render_preview_meshes(records);
    const auto expected = reference.render_standard_previews(records);
    EXPECT_TRUE(actual.has_value());
    EXPECT_TRUE(expected.has_value());
    if (actual && expected) expect_same_pixels(*actual, *expected);
    return actual;
  };
  const auto initial = check_reference();
  ASSERT_TRUE(initial.has_value());
  const auto initial_work = harness.preview_counters();
  const auto initial_geometry = harness.geometry_counters();
  EXPECT_EQ(initial_work.active_dynamic_geometry_families, 1u);
  EXPECT_EQ(initial_work.active_dynamic_geometry_participants, 3u);
  EXPECT_EQ(initial_geometry.dynamic_geometry_create_count, 1u);
  EXPECT_EQ(harness.preview_proxy_count(), 3u);
  for (const std::size_t y : {20u, 43u}) {
    for (const std::size_t x : {11u, 32u, 52u}) {
      const std::size_t offset = (y * kTargetSize + x) * 4u;
      EXPECT_GT(initial->rgba[offset] + initial->rgba[offset + 1u] +
                    initial->rgba[offset + 2u], 0.5f) << x << "," << y;
    }
  }
  auto identity_sampling = records;
  for (auto &record : identity_sampling) {
    record.sampling_from_local = {};
    record.instance_sampling_from_local.clear();
  }
  const auto unsampled = reference.render_standard_previews(identity_sampling);
  ASSERT_TRUE(unsampled.has_value());
  EXPECT_GT(maximum_pixel_difference(*initial, *unsampled), 0.1f);
  ASSERT_TRUE(check_reference().has_value());
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_create_count,
            initial_geometry.dynamic_geometry_create_count);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_upload_count,
            initial_geometry.dynamic_geometry_upload_count);

  // A different coordinate payload splits the three lower occurrences from
  // the three upper occurrences without changing their authored topology.
  auto split_positions = std::make_shared<std::vector<QVec3>>(
      *records.front().dynamic_position_payload);
  (*split_positions)[2] = {0.45f, 0.6f, 0.25f};
  records.front().dynamic_position_payload = std::move(split_positions);
  ++records.front().position_revision;
  const auto split = check_reference();
  ASSERT_TRUE(split.has_value());
  EXPECT_GT(maximum_pixel_difference(*initial, *split), 0.1f);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 2u);
  // The accepted count-six slot remains intact while count-three slots grow.
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_create_count,
            initial_geometry.dynamic_geometry_create_count + 2u);
  const auto split_geometry = harness.geometry_counters();
  records = original;
  const auto merged = check_reference();
  ASSERT_TRUE(merged.has_value());
  expect_same_pixels(*merged, *initial);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_create_count,
            split_geometry.dynamic_geometry_create_count);

  std::reverse(records.begin(), records.end());
  const auto reordered = check_reference();
  ASSERT_TRUE(reordered.has_value());
  expect_same_pixels(*reordered, *initial);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_create_count,
            split_geometry.dynamic_geometry_create_count);
  records.front().material.primary_material.normal_strength = 0.15f;
  ++records.front().material.primary_material.revision;
  const auto material_split = check_reference();
  ASSERT_TRUE(material_split.has_value());
  EXPECT_GT(maximum_pixel_difference(*material_split, *initial), 0.1f);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 2u);
  const auto accepted_work = harness.preview_counters();
  const auto accepted_geometry = harness.geometry_counters();
  ASSERT_TRUE(check_reference().has_value());
  EXPECT_EQ(harness.preview_counters().dynamic_family_group_creations,
            accepted_work.dynamic_family_group_creations);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_create_count,
            accepted_geometry.dynamic_geometry_create_count);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_upload_count,
            accepted_geometry.dynamic_geometry_upload_count);

  auto rejected = records;
  auto changed_positions = std::make_shared<std::vector<QVec3>>(
      *rejected.front().dynamic_position_payload);
  changed_positions->front().x += 0.5f;
  rejected.front().dynamic_position_payload = std::move(changed_positions);
  ++rejected.front().position_revision;
  auto malformed_positions = std::make_shared<std::vector<QVec3>>(
      *rejected.back().dynamic_position_payload);
  malformed_positions->pop_back();
  rejected.back().dynamic_position_payload = std::move(malformed_positions);
  ++rejected.back().position_revision;
  EXPECT_FALSE(harness.render_preview_meshes(rejected).has_value());
  const auto preserved = harness.read_current_mesh();
  ASSERT_TRUE(preserved.has_value());
  expect_same_pixels(*preserved, *material_split);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families,
            accepted_work.active_dynamic_geometry_families);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_upload_count,
            accepted_geometry.dynamic_geometry_upload_count);
  ASSERT_TRUE(check_reference().has_value());

  const auto ended = harness.render_preview_meshes({});
  ASSERT_TRUE(ended.has_value());
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 0u);
  EXPECT_GT(harness.preview_counters().retired_proxy_count, 0u);
  const auto empty_reference = reference.render_standard_previews({});
  ASSERT_TRUE(empty_reference.has_value());
  expect_same_pixels(*ended, *empty_reference);
  for (std::size_t iteration = 0u;
       iteration < 64u && harness.preview_counters().retired_proxy_count > 0u;
       ++iteration) {
    harness.drain_preview_retirement(2u);
  }
  EXPECT_EQ(harness.preview_counters().retired_proxy_count, 0u);
  EXPECT_EQ(harness.preview_proxy_count(), 0u);
  records = original;
  ASSERT_TRUE(check_reference().has_value());
  harness.clear_previews();
  EXPECT_EQ(harness.preview_proxy_count(), 0u);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 0u);
  EXPECT_EQ(harness.preview_counters().retired_proxy_count, 0u);
  const auto cleared = harness.read_current_mesh();
  ASSERT_TRUE(cleared.has_value());
  expect_same_pixels(*cleared, *empty_reference);
}

TEST(RenderNormalMappingRuntimeTests,
     InactiveDynamicFamiliesBoundBothGroupAndMaterialBearingProxyCounts) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan coverage";
  }
  NormalMappingHarness harness;
  ASSERT_TRUE(harness.initialize());
  auto single = visible_dynamic_occurrences(harness.preview_material()).front();
  single.instance_transforms.clear();
  single.instance_sampling_from_local.clear();
  single.sampling_from_local = {};
  single.transform = {};

  // Four vertices suffice to create 129 alternating material partitions. One
  // inactive family therefore owns 387 draw/material proxies, exceeding the
  // proxy budget while remaining well below the 64-family budget.
  auto partitioned = single;
  auto &geometry = partitioned.mesh.edit_geometry();
  geometry.indices.clear();
  geometry.primitives.clear();
  constexpr std::uint32_t kPartitionCount = 129u;
  for (std::uint32_t index = 0u; index < kPartitionCount; ++index) {
    geometry.indices.insert(geometry.indices.end(), {0u, 1u, 2u});
    geometry.primitives.push_back({.index_offset = index * 3u,
        .index_count = 3u, .material_slot = index % 2u});
  }
  ++partitioned.position_revision;
  ASSERT_TRUE(harness.render_preview_meshes({partitioned}).has_value());
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
  EXPECT_EQ(harness.preview_proxy_count(), 3u * kPartitionCount);
  ASSERT_TRUE(harness.render_preview_meshes({single}).has_value());
  EXPECT_EQ(harness.preview_counters().inactive_dynamic_geometry_families, 0u);
  EXPECT_EQ(harness.preview_counters().retired_proxy_count, 3u * kPartitionCount);
  for (std::size_t iteration = 0u;
       iteration < 512u && harness.preview_counters().retired_proxy_count > 0u;
       ++iteration) harness.drain_preview_retirement(32u);
  EXPECT_EQ(harness.preview_counters().retired_proxy_count, 0u);
  EXPECT_EQ(harness.preview_proxy_count(), 3u);
  harness.clear_previews();

  // Conversely 65 small families own only 195 proxies. The family cap still
  // evicts one when an incompatible topology becomes the sole active family.
  constexpr std::size_t kSmallFamilyCount = 65u;
  std::vector<RenderToolPreviewMeshRecord> small(kSmallFamilyCount, single);
  for (std::size_t index = 0u; index < small.size(); ++index) {
    auto &record = small[index];
    record.source_object_id = static_cast<int>(index + 1u);
    record.source_entity = static_cast<EntityId>(index + 1u);
    auto positions = std::make_shared<std::vector<QVec3>>(*record.dynamic_position_payload);
    positions->front().z += static_cast<float>(index) * 0.001f;
    record.dynamic_position_payload = std::move(positions);
    record.position_revision = index + 1u;
    record.transform.values[12] = static_cast<float>(index) * 4.0f;
  }
  ASSERT_TRUE(harness.render_preview_meshes(small).has_value());
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, kSmallFamilyCount);
  EXPECT_EQ(harness.preview_proxy_count(), kSmallFamilyCount * 3u);
  auto triangle = single;
  auto &triangle_geometry = triangle.mesh.edit_geometry();
  triangle_geometry.indices = {0u, 1u, 2u};
  triangle_geometry.primitives = {{.index_count = 3u}};
  ++triangle.position_revision;
  ASSERT_TRUE(harness.render_preview_meshes({triangle}).has_value());
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
  EXPECT_EQ(harness.preview_counters().inactive_dynamic_geometry_families, 64u);
  EXPECT_EQ(harness.preview_counters().retired_proxy_count, 3u);
  harness.clear_previews();
  EXPECT_EQ(harness.preview_proxy_count(), 0u);
  EXPECT_EQ(harness.preview_counters().retired_proxy_count, 0u);
}

TEST(RenderNormalMappingRuntimeTests,
     AlphaTexturedDynamicOccurrencesPreserveOrdinaryDepthSorting) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan coverage";
  }
  for (const bool encoded : {false, true}) {
    SCOPED_TRACE(encoded ? "encoded PNG" : "decoded RGBA");
    NormalMappingHarness harness;
    NormalMappingHarness reference;
    ASSERT_TRUE(harness.initialize());
    ASSERT_TRUE(reference.initialize());
    auto material = harness.preview_material();
    RenderTextureAssetSnapshot albedo;
    albedo.slot = material_texture_slot::kBaseColor;
    albedo.project_texture_id = *ProjectAssetId::parse("ee24919a-9c38-4d99-bc8c-b62b016694c0");
    albedo.content_revision = 1u;
    albedo.texture.color_space = TextureColorSpace::Linear;
    albedo.width = 1u;
    albedo.height = 1u;
    albedo.present = true;
    const auto set_alpha = [&](std::uint8_t alpha) {
      albedo.decoded_rgba8.reset();
      albedo.encoded_file_bytes.reset();
      if (encoded) {
        QImage image(1, 1, QImage::Format_RGBA8888);
        image.fill(qRgba(255, 255, 255, alpha));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        EXPECT_TRUE(buffer.open(QIODevice::WriteOnly));
        EXPECT_TRUE(image.save(&buffer, "PNG"));
        const auto *first = reinterpret_cast<const std::byte *>(bytes.constData());
        albedo.encoded_file_bytes = std::make_shared<const std::vector<std::byte>>(first, first + bytes.size());
      } else {
        albedo.decoded_rgba8 = std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{std::byte{255u}, std::byte{255u},
                                             std::byte{255u}, std::byte{alpha}});
      }
    };
    set_alpha(128u);
    material.primary_material.textures.push_back(albedo);
    auto records = visible_dynamic_occurrences(material);
    records.resize(2u);
    for (auto &record : records) {
      record.instance_transforms.clear();
      record.instance_sampling_from_local.clear();
      record.sampling_from_local = {};
      record.transform = {};
    }
    // The nearest occurrence is submitted first. Ordinary transparent draws
    // sort back-to-front; instance order inside one family cannot substitute
    // for this camera-dependent ordering when the front surface writes depth.
    records.back().transform.values[14] = -0.5f;
    const auto compare = [&]() {
      const auto actual = harness.render_preview_meshes(records);
      const auto expected = reference.render_standard_previews(records);
      EXPECT_TRUE(actual.has_value());
      EXPECT_TRUE(expected.has_value());
      if (actual && expected) expect_same_pixels(*actual, *expected);
    };
    compare();
    EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 0u);
    const auto alpha_image = harness.read_current_mesh();
    ASSERT_TRUE(alpha_image.has_value());
    const std::size_t center = (kTargetSize / 2u * kTargetSize + kTargetSize / 2u) * 4u;
    EXPECT_GT(alpha_image->rgba[center + 2u], 0.5f);
    std::swap(records.front().transform, records.back().transform);
    compare();

    // A new texture revision starts unknown, then the existing upload records
    // its opaque fact. A subsequent coordinate sample can use GPU families.
    const auto revise_alpha = [&](std::uint8_t alpha) {
      set_alpha(alpha);
      ++albedo.content_revision;
      for (auto &record : records) {
        record.material.primary_material.textures.back() = albedo;
        ++record.position_revision;
      }
    };
    revise_alpha(255u);
    compare();
    const auto opaque_uploads = harness.geometry_counters().dynamic_geometry_upload_count;
    for (auto &record : records) ++record.position_revision;
    compare();
    EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
    EXPECT_GT(harness.geometry_counters().dynamic_geometry_upload_count, opaque_uploads);
    // The old opaque fact must not survive a new alpha-bearing image revision.
    revise_alpha(128u);
    compare();
    EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 0u);
  }
}

TEST(RenderNormalMappingRuntimeTests,
     ReflectedSingleSidedDynamicOccurrencesKeepOrdinaryCullingAndSampling) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = visible_dynamic_occurrences(harness.preview_material());
  // One logical participant per occurrence exercises the ordinary fallback's
  // per-renderable front-face determinant, including a reflected middle mesh.
  records.front().instance_transforms.resize(1u);
  records[1].instance_transforms.resize(1u);
  records[1].instance_transforms.front().values[0] *= -1.0f;
  const auto actual = harness.render_preview_meshes(records);
  const auto expected = reference.render_standard_previews(records);
  ASSERT_TRUE(actual.has_value());
  ASSERT_TRUE(expected.has_value());
  expect_same_pixels(*actual, *expected);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_participants, 2u);
  EXPECT_EQ(harness.preview_counters().ordinary_reflected_records, 1u);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_create_count, 2u);
  const std::size_t reflected_offset = (43u * kTargetSize + 11u) * 4u;
  EXPECT_GT(actual->rgba[reflected_offset] + actual->rgba[reflected_offset + 1u] +
                actual->rgba[reflected_offset + 2u], 0.5f);
}

TEST(RenderNormalMappingRuntimeTests,
     MixedPreviewLanesIgnoreUnusedAlphaSlotsAndRejectTheWholeCandidate) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = visible_dynamic_occurrences(harness.preview_material());
  RenderTextureAssetSnapshot alpha;
  alpha.slot = material_texture_slot::kBaseColor;
  alpha.project_texture_id = *ProjectAssetId::parse("a8290ed9-6988-41e5-80f5-b00cd7cb5fc4");
  alpha.content_revision = 1u;
  alpha.width = 1u;
  alpha.height = 1u;
  alpha.present = true;
  alpha.texture.color_space = TextureColorSpace::Linear;
  alpha.decoded_rgba8 = std::make_shared<const std::vector<std::byte>>(
      std::initializer_list<std::byte>{std::byte{255u}, std::byte{255u}, std::byte{255u}, std::byte{128u}});
  auto alpha_material = records.front().material.primary_material;
  alpha_material.textures.push_back(alpha);
  // Slot 77 never occurs in a primitive; both its unknown and decoded-alpha
  // states must leave the first record's opaque occurrences family eligible.
  records.front().material.material_slots.emplace_back(77u, alpha_material);
  records[1].instance_transforms.front().values[0] *= -1.0f;
  records.back().material.primary_material = alpha_material;
  const auto compare = [&]() {
    const auto actual = harness.render_preview_meshes(records);
    const auto expected = reference.render_standard_previews(records);
    EXPECT_TRUE(actual.has_value());
    EXPECT_TRUE(expected.has_value());
    if (actual && expected) expect_same_pixels(*actual, *expected);
    return actual;
  };
  ASSERT_TRUE(compare().has_value());
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_participants, 1u);
  EXPECT_EQ(harness.preview_counters().ordinary_unknown_opacity_records, 1u);
  EXPECT_EQ(harness.preview_counters().ordinary_reflected_records, 1u);
  for (auto &record : records) ++record.position_revision;
  ASSERT_TRUE(compare().has_value());
  EXPECT_EQ(harness.preview_counters().ordinary_transparent_records, 1u);

  // Change both lanes in one accepted generation, including a reflected
  // occurrence's transform and UV sampling, then compare ordinary rendering.
  for (auto &record : records) {
    auto positions = std::make_shared<std::vector<QVec3>>(*record.dynamic_position_payload);
    positions->front().x += 0.35f;
    record.dynamic_position_payload = std::move(positions);
    ++record.position_revision;
    record.sampling_from_local.values[12] += 0.2f;
  }
  records[1].instance_transforms.back().values[13] += 0.1f;
  const auto accepted = compare();
  ASSERT_TRUE(accepted.has_value());
  const auto accepted_geometry = harness.geometry_counters();
  ASSERT_TRUE(compare().has_value());
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_upload_count,
            accepted_geometry.dynamic_geometry_upload_count);
  for (const std::size_t malformed_index : {0u, 2u}) {
    SCOPED_TRACE(malformed_index);
    auto rejected = records;
    for (auto &record : rejected) {
      auto positions = std::make_shared<std::vector<QVec3>>(*record.dynamic_position_payload);
      positions->front().y += 0.5f;
      record.dynamic_position_payload = std::move(positions);
    }
    auto malformed = std::make_shared<std::vector<QVec3>>(*rejected[malformed_index].dynamic_position_payload);
    if (malformed_index == 0u) malformed->pop_back();
    else malformed->clear();
    rejected[malformed_index].dynamic_position_payload = std::move(malformed);
    // Reusing a revision does not authorize a replacement payload. Both a
    // short family stream and an empty ordinary stream must be validated.
    EXPECT_FALSE(harness.render_preview_meshes(rejected).has_value());
    const auto preserved = harness.read_current_mesh();
    ASSERT_TRUE(preserved.has_value());
    expect_same_pixels(*preserved, *accepted);
    EXPECT_EQ(harness.geometry_counters().dynamic_geometry_upload_count,
              accepted_geometry.dynamic_geometry_upload_count);
    EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
  }
  for (auto &record : records) {
    auto positions = std::make_shared<std::vector<QVec3>>(*record.dynamic_position_payload);
    positions->front().y += 0.5f;
    record.dynamic_position_payload = std::move(positions);
  }
  const auto replaced = compare();
  ASSERT_TRUE(replaced.has_value());
  EXPECT_GT(maximum_pixel_difference(*replaced, *accepted), 0.1f);
  const auto replaced_geometry = harness.geometry_counters();
  EXPECT_GT(replaced_geometry.dynamic_geometry_upload_count,
            accepted_geometry.dynamic_geometry_upload_count);
  ASSERT_TRUE(compare().has_value());
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_upload_count,
            replaced_geometry.dynamic_geometry_upload_count);
  harness.clear_previews();
  EXPECT_EQ(harness.preview_proxy_count(), 0u);
}

TEST(RenderNormalMappingRuntimeTests,
     ProjectedPreviewStreamsRetainTopologyAndMatchCanonicalChangedAttributes) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for windowless Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = visible_dynamic_occurrences(harness.preview_material());
  records[1].instance_transforms.front().values[0] *= -1.0f;
  const auto original = records.front().mesh.geometry_payload();
  const auto install_projection = [&](float displacement, bool change_attributes) {
    auto projection = std::make_shared<MeshGeometryProjection>();
    projection->source_geometry = original;
    auto positions = std::make_shared<std::vector<QVec3>>();
    for (const auto &vertex : original->vertices) positions->push_back(vertex.position);
    positions->front().x += displacement;
    projection->positions = std::move(positions);
    if (change_attributes) {
      auto attributes = std::make_shared<std::vector<MeshVertex>>(original->vertices);
      for (auto &vertex : *attributes) vertex.uv_carrier = {0, -1, 1, 0};
      projection->vertex_attributes = std::move(attributes);
      projection->triangle_indices = std::make_shared<const std::vector<std::uint32_t>>(
          std::initializer_list<std::uint32_t>{0u, 1u, 3u, 1u, 2u, 3u});
    }
    for (auto &record : records) {
      record.dynamic_position_payload.reset();
      record.geometry_projection = projection;
      ++record.position_revision;
    }
  };
  const auto compare = [&]() {
    auto canonical = records;
    for (auto &record : canonical) {
      const auto &projection = *record.geometry_projection;
      MeshGeometryPayload geometry;
      const auto attributes = projection.vertices();
      geometry.vertices.assign(attributes.begin(), attributes.end());
      for (std::size_t index = 0u; index < geometry.vertices.size(); ++index) {
        geometry.vertices[index].position = (*projection.positions)[index];
      }
      const auto indices = projection.indices();
      geometry.indices.assign(indices.begin(), indices.end());
      geometry.primitives = original->primitives;
      record.mesh.replace_geometry(std::move(geometry));
      record.geometry_projection.reset();
    }
    const auto actual = harness.render_preview_meshes(records);
    const auto expected = reference.render_standard_previews(canonical);
    EXPECT_TRUE(actual.has_value());
    EXPECT_TRUE(expected.has_value());
    if (actual && expected) expect_same_pixels(*actual, *expected);
    return actual;
  };
  install_projection(0.0f, false);
  const auto initial = compare();
  ASSERT_TRUE(initial.has_value());
  install_projection(0.6f, true);
  const auto changed = compare();
  ASSERT_TRUE(changed.has_value());
  EXPECT_GT(maximum_pixel_difference(*initial, *changed), 0.1f);
  EXPECT_EQ(records.front().mesh.geometry_payload(), original);
  EXPECT_EQ(records.front().geometry_projection->source_geometry, original);
  const auto accepted_geometry = harness.geometry_counters();
  ASSERT_TRUE(compare().has_value());
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_upload_count,
            accepted_geometry.dynamic_geometry_upload_count);
  EXPECT_EQ(harness.geometry_counters().dynamic_geometry_create_count,
            accepted_geometry.dynamic_geometry_create_count);
  EXPECT_EQ(harness.preview_counters().active_dynamic_geometry_families, 1u);
  EXPECT_EQ(harness.preview_counters().ordinary_reflected_records, 1u);
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

std::vector<RenderToolPreviewMeshRecord> unique_editable_surfaces(
    const RenderMaterialBindingSnapshot &material, std::size_t count) {
  auto example = visible_dynamic_occurrences(material).front();
  std::vector<RenderToolPreviewMeshRecord> result;
  result.reserve(count);
  for (std::size_t index = 0u; index < count; ++index) {
    auto record = example;
    record.source_entity = static_cast<EntityId>(index + 1u);
    record.source_object_id = static_cast<int>(index + 1u);
    record.instance_transforms.clear();
    record.instance_sampling_from_local.clear();
    record.dynamic_position_payload.reset();
    record.suppress_source = true;
    record.transform = {};
    record.sampling_from_local = {};
    const float scale = count > 10u ? 0.12f : (index % 2u == 0u ? 0.8f : 1.2f);
    record.transform.values[0] = scale;
    record.transform.values[5] = scale;
    record.transform.values[10] = scale;
    if (index % 2u != 0u) {
      record.transform.values[0] = 0.0f;
      record.transform.values[1] = scale;
      record.transform.values[4] = -scale;
      record.transform.values[5] = 0.0f;
      record.sampling_from_local.values[0] = 0.0f;
      record.sampling_from_local.values[1] = 1.0f;
      record.sampling_from_local.values[4] = -1.0f;
      record.sampling_from_local.values[5] = 0.0f;
    }
    if (index % 3u == 2u) {
      record.transform.values[0] = 0.8f * scale;
      record.transform.values[1] = 0.0f;
      record.transform.values[2] = -0.6f * scale;
      record.transform.values[4] = 0.0f;
      record.transform.values[5] = scale;
      record.transform.values[8] = 0.6f * scale;
      record.transform.values[10] = 0.8f * scale;
    }
    const std::size_t columns = count > 10u ? 9u : 3u;
    const float spacing = count > 10u ? 0.31f : 0.9f;
    record.transform.values[12] = (static_cast<float>(index % columns) - static_cast<float>(columns - 1u) * 0.5f) * spacing;
    record.transform.values[13] = count > 10u ? (static_cast<float>(index / columns) - 3.5f) * spacing
                                            : (index < columns ? -0.6f : 0.6f);
    auto geometry = record.mesh.geometry();
    for (auto &vertex : geometry.vertices) {
      if (count <= 10u) vertex.position = vertex.position * 0.28f;
      vertex.normal = normalize(QVec3{0.03f * static_cast<float>(vertex.source_vertex_id), 0.08f, 1.0f});
    }
    geometry.vertices[0].position.x += 0.0008f * static_cast<float>(index);
    geometry.vertices[2].position.z += 0.0004f * static_cast<float>(index);
    record.mesh.replace_geometry(std::move(geometry));
    record.position_revision = index + 1u;
    record.layout_key = index + 1u;
    result.push_back(std::move(record));
  }
  return result;
}

RenderSceneSnapshot editable_base_scene(const std::vector<RenderToolPreviewMeshRecord> &records,
                                        std::uint64_t revision) {
  RenderSceneSnapshot result;
  result.scene_binding.scene_session_id = 1u;
  result.scene_revision = revision;
  result.asset_revision = revision;
  for (const auto &record : records) {
    result.meshes.push_back({.entity_id = record.source_entity,
        .mesh = render_mesh_asset_snapshot(record.source_entity, record.mesh, record.geometry_projection),
        .mesh_component = {.cast_shadows = record.cast_shadows, .double_sided = record.double_sided},
        .material = record.material, .world_transform = record.transform,
        .sampling_from_local = record.sampling_from_local});
    result.live_mesh_entities.insert(record.source_entity);
  }
  return result;
}

TEST(EditableMeshSurfaceRuntimeTests, LargeWorldAnchorsPreserveSmallGeometryAndRetainedUploads) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = unique_editable_surfaces(harness.preview_material(), 3u);
  constexpr float kWorldOffset = 100000000.0f;
  records[0].transform.values[12] = kWorldOffset;
  records[0].transform.values[13] = -0.6f;
  records[1].transform.values[12] = kWorldOffset;
  records[1].transform.values[13] = 0.6f;
  records[2].transform.values[12] = kWorldOffset + 16384.0f;
  records[2].transform.values[13] = 0.0f;
  auto base = editable_base_scene(records, 1u);
  QVec3 camera_target{kWorldOffset, 0.0f, 0.0f};
  const auto expected = reference.render_standard_previews(records, false, camera_target);
  // The retained surface cache enrolls objects through an editable preview;
  // untouched ordinary objects remain owned by the ordinary scene renderer.
  const auto actual = harness.render_editable_meshes(base, records, {}, false, camera_target);
  ASSERT_TRUE(expected);
  ASSERT_TRUE(actual);
  ASSERT_EQ(harness.editable_counters().active_batch_count, 2u);
  std::size_t covered_pixels = 0u;
  for (std::uint32_t y = 0u; y < kTargetSize; ++y) {
    for (std::uint32_t x = 0u; x < kTargetSize; ++x) {
      covered_pixels += length(expected->pixel(x, y)) > 0.1f ? 1u : 0u;
    }
  }
  ASSERT_GT(covered_pixels, 30u);
  expect_same_pixels(*actual, *expected);
  const auto committed = harness.render_editable_meshes(base, {}, {}, false, camera_target);
  ASSERT_TRUE(committed);
  ASSERT_EQ(harness.editable_counters().active_batch_count, 2u);
  EXPECT_EQ(harness.editable_counters().active_preview_object_count, 0u);
  EXPECT_EQ(harness.editable_counters().active_retained_object_count, 3u);
  expect_same_pixels(*committed, *expected);
  const auto retained = harness.geometry_counters();
  ASSERT_TRUE(harness.render_editable_meshes(base, {}, {}, false, camera_target));
  EXPECT_EQ(harness.geometry_counters().editable_surface_palette_upload_count,
            retained.editable_surface_palette_upload_count);

  // Reanchor a retained chunk after it leaves the bounded palette neighborhood.
  // The canonical vertex/index streams must survive the entity-origin change.
  for (auto &record : records) record.transform.values[12] += 8192.0f;
  camera_target.x += 8192.0f;
  base = editable_base_scene(records, 2u);
  const auto moved = harness.render_editable_meshes(base, {}, {}, false, camera_target);
  const auto moved_reference = reference.render_standard_previews(records, false, camera_target);
  ASSERT_TRUE(moved);
  ASSERT_TRUE(moved_reference);
  expect_same_pixels(*moved, *moved_reference);
  EXPECT_EQ(harness.geometry_counters().editable_surface_geometry_create_count,
            retained.editable_surface_geometry_create_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            retained.editable_surface_position_upload_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_index_upload_count,
            retained.editable_surface_index_upload_count);
}

TEST(EditableMeshSurfaceRuntimeTests, UniqueGeometryRetainsBuffersAndRejectsWholeCandidate) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = unique_editable_surfaces(harness.preview_material(), 65u);
  auto base = editable_base_scene(records, 1u);
  // Ordinary objects often arrive through extraction's shared-geometry lane.
  for (const auto &record : base.meshes) {
    base.instance_primitives.push_back({.key = {.cast_shadows = record.cast_shadows(),
        .double_sided = record.double_sided()}, .mesh = record.mesh,
        .primitive = record.mesh.primitives.front(), .material = record.material,
        .occurrences = {{.entity_id = record.entity_id, .occurrence = record.entity_id,
            .world_transform = record.world_transform, .sampling_from_local = record.sampling_from_local}}});
  }
  base.meshes.clear();
  std::unordered_set<std::uint64_t> geometries;
  for (const auto &record : records) geometries.insert(record.mesh.geometry_content_id().value);
  ASSERT_EQ(geometries.size(), 65u);
  const auto initial = harness.render_editable_meshes(base, records);
  const auto expected = reference.render_standard_previews(records);
  ASSERT_TRUE(initial);
  ASSERT_TRUE(expected);
  expect_same_pixels(*initial, *expected);
  EXPECT_EQ(harness.editable_counters().active_batch_count, 2u);
  EXPECT_EQ(harness.editable_counters().active_preview_object_count, 65u);
  const auto geometry_before = harness.geometry_counters();
  ASSERT_TRUE(harness.render_editable_meshes(base, records));
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            geometry_before.editable_surface_position_upload_count);
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : records.front().mesh.geometry().vertices) positions->push_back(vertex.position);
  positions->front().x += 0.4f;
  records.front().dynamic_position_payload = positions;
  ++records.front().position_revision;
  const auto moved = harness.render_editable_meshes(base, records);
  const auto moved_reference = reference.render_standard_previews(records);
  ASSERT_TRUE(moved);
  ASSERT_TRUE(moved_reference);
  expect_same_pixels(*moved, *moved_reference);
  EXPECT_EQ(harness.geometry_counters().editable_surface_geometry_create_count,
            geometry_before.editable_surface_geometry_create_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_index_upload_count,
            geometry_before.editable_surface_index_upload_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            geometry_before.editable_surface_position_upload_count + 1u);
  const auto accepted = harness.geometry_counters();
  auto same_revision_invalid = records;
  same_revision_invalid.front().dynamic_position_payload =
      std::make_shared<const std::vector<QVec3>>(1u);
  EXPECT_FALSE(harness.render_editable_meshes(base, same_revision_invalid));
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            accepted.editable_surface_position_upload_count);
  auto rejected = records;
  auto changed = std::make_shared<std::vector<QVec3>>(*positions);
  changed->front().x += 0.4f;
  rejected.front().dynamic_position_payload = changed;
  ++rejected.front().position_revision;
  rejected.back().dynamic_position_payload = std::make_shared<const std::vector<QVec3>>(1u);
  ++rejected.back().position_revision;
  EXPECT_FALSE(harness.render_editable_meshes(base, rejected));
  const auto preserved = harness.read_current_mesh();
  ASSERT_TRUE(preserved);
  expect_same_pixels(*preserved, *moved);
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            accepted.editable_surface_position_upload_count);
  records.front().transform.values[12] += 0.03f;
  ASSERT_TRUE(harness.render_editable_meshes(base, records));
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            accepted.editable_surface_position_upload_count);
  EXPECT_GT(harness.geometry_counters().editable_surface_palette_upload_count,
            accepted.editable_surface_palette_upload_count);
}

TEST(EditableMeshSurfaceRuntimeTests, ProjectionCommitCancelAndUnsupportedSubsetPreserveVisiblePeers) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = unique_editable_surfaces(harness.preview_material(), 6u);
  auto base = editable_base_scene(records, 1u);
  const auto original = harness.render_editable_meshes(base, records);
  ASSERT_TRUE(original);
  auto projection = std::make_shared<MeshGeometryProjection>();
  projection->source_geometry = records.front().mesh.geometry_payload();
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : projection->vertices()) positions->push_back(vertex.position);
  (*positions)[2].z += 0.13f;
  (*positions)[2].x -= 0.07f;
  projection->positions = positions;
  auto attributes = std::make_shared<std::vector<MeshVertex>>(projection->vertices().begin(), projection->vertices().end());
  for (auto &vertex : *attributes) { vertex.normal = normalize(QVec3{0.2f, 0.3f, 1.0f}); vertex.uv.x += 0.13f; }
  projection->vertex_attributes = attributes;
  projection->triangle_indices = std::make_shared<const std::vector<std::uint32_t>>(
      std::initializer_list<std::uint32_t>{0u, 1u, 3u, 1u, 2u, 3u});
  records.front().geometry_projection = projection;
  ++records.front().position_revision;
  const auto initial_uploads = harness.geometry_counters();
  const auto changed = harness.render_editable_meshes(base, records);
  auto canonical = records;
  canonical.front().mesh.share_geometry(projection->materialized_geometry());
  canonical.front().geometry_projection.reset();
  const auto expected = reference.render_standard_previews(canonical);
  ASSERT_TRUE(changed);
  ASSERT_TRUE(expected);
  expect_same_pixels(*changed, *expected);
  EXPECT_EQ(harness.geometry_counters().editable_surface_geometry_create_count,
            initial_uploads.editable_surface_geometry_create_count);
  EXPECT_GT(harness.geometry_counters().editable_surface_index_upload_count,
            initial_uploads.editable_surface_index_upload_count);
  EXPECT_GT(harness.geometry_counters().editable_surface_attribute_upload_count,
            initial_uploads.editable_surface_attribute_upload_count);
  const auto accepted_uploads = harness.geometry_counters();
  for (auto &record : canonical) record.cast_shadows = true;
  base = editable_base_scene(canonical, 2u);
  const auto committed = harness.render_editable_meshes(base, {});
  ASSERT_TRUE(committed);
  expect_same_pixels(*committed, *changed);
  EXPECT_EQ(harness.geometry_counters().editable_surface_geometry_create_count,
            accepted_uploads.editable_surface_geometry_create_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            accepted_uploads.editable_surface_position_upload_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_index_upload_count,
            accepted_uploads.editable_surface_index_upload_count);
  EXPECT_EQ(harness.editable_exclusion().entities.size(), 6u);
  auto subset = canonical.front();
  auto subset_positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : subset.mesh.geometry().vertices) subset_positions->push_back(vertex.position);
  subset_positions->front().x += 0.08f;
  subset.dynamic_position_payload = subset_positions;
  subset.cast_shadows = false;
  ++subset.position_revision;
  const auto subset_image = harness.render_editable_meshes(base, {subset});
  auto subset_reference_records = canonical;
  subset_reference_records.front() = subset;
  const auto subset_reference = reference.render_standard_previews(subset_reference_records);
  ASSERT_TRUE(subset_image);
  ASSERT_TRUE(subset_reference);
  expect_same_pixels(*subset_image, *subset_reference);
  const auto restored = harness.render_editable_meshes(base, {});
  ASSERT_TRUE(restored);
  expect_same_pixels(*restored, *committed);
  auto unsupported = canonical.front();
  unsupported.transform.values[0] *= -1.0f;
  const auto mixed = harness.render_editable_meshes(base, {unsupported});
  auto mixed_reference_records = canonical;
  mixed_reference_records.front() = unsupported;
  const auto mixed_reference = reference.render_standard_previews(mixed_reference_records);
  ASSERT_TRUE(mixed);
  ASSERT_TRUE(mixed_reference);
  expect_same_pixels(*mixed, *mixed_reference);
  EXPECT_EQ(harness.editable_counters().active_retained_object_count, 5u);
  EXPECT_EQ(harness.editable_counters().fallback_reflected_count, 1u);
  EXPECT_EQ(harness.editable_exclusion().entities.size(), 5u);
  const auto canceled = harness.render_editable_meshes(base, {});
  ASSERT_TRUE(canceled);
  expect_same_pixels(*canceled, *committed);
  EXPECT_EQ(harness.editable_exclusion().entities.size(), 6u);
  RenderToolPreviewSuppressionSnapshot suppression;
  suppression.entities.push_back(canonical.back().source_entity);
  const auto suppressed = harness.render_editable_meshes(base, {}, suppression);
  canonical.pop_back();
  const auto suppression_reference = reference.render_standard_previews(canonical);
  ASSERT_TRUE(suppressed);
  ASSERT_TRUE(suppression_reference);
  expect_same_pixels(*suppressed, *suppression_reference);
  EXPECT_EQ(harness.editable_exclusion().entities.size(), 5u);
}

TEST(EditableMeshSurfaceRuntimeTests, CanonicalStreamsAreSharedAcrossChunksAndUploadOnlyChangedBytes) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = unique_editable_surfaces(harness.preview_material(), 65u);
  const auto geometry = records.front().mesh.geometry_payload();
  for (auto &record : records) record.mesh.share_geometry(geometry);
  const auto base = editable_base_scene(records, 1u);
  const auto initial = harness.render_editable_meshes(base, records);
  const auto initial_reference = reference.render_standard_previews(records);
  ASSERT_TRUE(initial);
  ASSERT_TRUE(initial_reference);
  expect_same_pixels(*initial, *initial_reference);
  ASSERT_EQ(harness.editable_counters().active_batch_count, 2u);
  const auto before = harness.geometry_counters();
  EXPECT_EQ(before.editable_surface_attribute_pack_count, 1u);
  auto positions = std::make_shared<std::vector<QVec3>>();
  for (const auto &vertex : geometry->vertices) positions->push_back(vertex.position + QVec3{0.125f, 0.0f, 0.0f});
  for (auto &record : records) {
    record.dynamic_position_payload = positions;
    ++record.position_revision;
  }
  const auto moved = harness.render_editable_meshes(base, records);
  const auto moved_reference = reference.render_standard_previews(records);
  ASSERT_TRUE(moved);
  ASSERT_TRUE(moved_reference);
  expect_same_pixels(*moved, *moved_reference);
  const auto after = harness.geometry_counters();
  EXPECT_EQ(after.editable_surface_attribute_pack_count, before.editable_surface_attribute_pack_count + 1u);
  EXPECT_EQ(after.editable_surface_geometry_create_count, before.editable_surface_geometry_create_count);
  EXPECT_EQ(after.editable_surface_position_upload_count, before.editable_surface_position_upload_count + 2u);
  EXPECT_EQ(after.editable_surface_attribute_upload_count, before.editable_surface_attribute_upload_count);
  EXPECT_EQ(after.editable_surface_index_upload_count, before.editable_surface_index_upload_count);
}

TEST(EditableMeshSurfaceRuntimeTests, SamplingGridAndUnusedMaterialSlotsMatchOrdinaryDraws) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto records = unique_editable_surfaces(harness.preview_material(), 6u);
  RenderMaterialAssetSnapshot unused;
  unused.present = true;
  RenderTextureAssetSnapshot texture;
  texture.slot = material_texture_slot::kBaseColor;
  texture.texture_id = 98765u;
  texture.present = true;
  unused.textures.push_back(texture);
  records.front().material.material_slots.emplace_back(99u, unused);
  auto base = editable_base_scene(records, 1u);
  for (const bool show_grid : {false, true}) {
    const auto actual = harness.render_editable_meshes(base, records, {}, show_grid);
    const auto expected = reference.render_standard_previews(records, show_grid);
    ASSERT_TRUE(actual);
    ASSERT_TRUE(expected);
    expect_same_pixels(*actual, *expected);
    EXPECT_EQ(harness.editable_counters().eligible_record_count, 6u);
    EXPECT_EQ(harness.editable_counters().fallback_unknown_opacity_count, 0u);
  }
  const auto accepted = harness.geometry_counters();
  records.back().material.primary_material.normal_strength = 0.2f;
  ++records.back().material.primary_material.revision;
  const auto split = harness.render_editable_meshes(base, records);
  const auto split_reference = reference.render_standard_previews(records);
  ASSERT_TRUE(split);
  ASSERT_TRUE(split_reference);
  expect_same_pixels(*split, *split_reference);
  EXPECT_EQ(harness.editable_counters().active_batch_count, 2u);
  EXPECT_GT(harness.geometry_counters().editable_surface_geometry_create_count,
            accepted.editable_surface_geometry_create_count);
  records.back().mesh.edit_geometry().indices = {0u, 1u, 2u};
  records.back().mesh.edit_geometry().primitives = {{.index_count = 3u}};
  ++records.back().position_revision;
  const auto topology = harness.render_editable_meshes(base, records);
  const auto topology_reference = reference.render_standard_previews(records);
  ASSERT_TRUE(topology);
  ASSERT_TRUE(topology_reference);
  expect_same_pixels(*topology, *topology_reference);
  records.pop_back();
  base = editable_base_scene(records, 2u);
  const auto deleted = harness.render_editable_meshes(base, {});
  const auto deleted_reference = reference.render_standard_previews(records);
  ASSERT_TRUE(deleted);
  ASSERT_TRUE(deleted_reference);
  expect_same_pixels(*deleted, *deleted_reference);
  EXPECT_EQ(harness.editable_exclusion().entities.size(), 5u);
}

TEST(EditableMeshSurfaceRuntimeTests,
     UvCheckerMatchesOrdinaryInstancedAndRetainedEditableGeometry) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_NORMAL_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_NORMAL_TESTS=1 for Vulkan coverage";
  }
  NormalMappingHarness harness;
  NormalMappingHarness reference;
  ASSERT_TRUE(harness.initialize());
  ASSERT_TRUE(reference.initialize());
  auto material = harness.preview_material();
  material.primary_material.texture_transform = {
      .scale = {0.7f, 1.2f}, .offset = {0.17f, 0.31f},
      .rotation_degrees = 19.0f};
  const auto checker = uv_checker_reference_material(material);
  ASSERT_TRUE(checker);
  auto instances = visible_dynamic_occurrences(material);
  auto reference_instances = instances;
  for (auto &record : reference_instances) record.material = *checker;
  const auto expected_instances = reference.render_standard_previews(
      reference_instances, false, {}, 3.0f);
  ASSERT_TRUE(expected_instances);
  harness.set_debug_view(7.0f);
  const auto actual_instances = harness.render_preview_meshes(instances);
  ASSERT_TRUE(actual_instances);
  expect_same_pixels(*actual_instances, *expected_instances);
  harness.clear_previews();

  auto records = unique_editable_surfaces(material, 6u);
  auto reference_records = records;
  for (auto &record : reference_records) record.material = *checker;
  const auto expected = reference.render_standard_previews(
      reference_records, false, {}, 3.0f);
  ASSERT_TRUE(expected);
  const auto base = editable_base_scene(records, 1u);
  const auto actual = harness.render_editable_meshes(
      base, records, {}, true, {}, 7.0f);
  ASSERT_TRUE(actual);
  expect_same_pixels(*actual, *expected);
  EXPECT_EQ(harness.editable_counters().eligible_record_count, records.size());
  const auto geometry = harness.geometry_counters();
  const auto textures = harness.texture_counters();
  const auto batches = harness.editable_counters().batch_create_count;
  const auto ordinary = harness.render_current_debug_view(6.0f);
  ASSERT_TRUE(ordinary);
  EXPECT_GT(maximum_pixel_difference(*ordinary, *actual), 0.1f);
  const auto restored = harness.render_current_debug_view(7.0f);
  ASSERT_TRUE(restored);
  expect_same_pixels(*restored, *expected);
  EXPECT_EQ(harness.editable_counters().batch_create_count, batches);
  EXPECT_EQ(harness.geometry_counters().editable_surface_geometry_create_count,
            geometry.editable_surface_geometry_create_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_position_upload_count,
            geometry.editable_surface_position_upload_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_attribute_upload_count,
            geometry.editable_surface_attribute_upload_count);
  EXPECT_EQ(harness.geometry_counters().editable_surface_index_upload_count,
            geometry.editable_surface_index_upload_count);
  EXPECT_EQ(harness.texture_counters().upload_count, textures.upload_count);
  EXPECT_EQ(harness.texture_counters().resource_count, textures.resource_count);
}

} // namespace
} // namespace quader
