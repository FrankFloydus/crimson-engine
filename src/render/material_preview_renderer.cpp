////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/material_preview_renderer.hpp"

#include "core/logger.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_environment_library.hpp"
#include "render/material_binding_service.hpp"
#include "render/material_preview_mesh_library.hpp"
#include "render/png_texture_loader.hpp"
#include "render/render_layers.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr std::size_t kPreviewMeshCount = 5u;
constexpr std::size_t kPreviewTextureCount = 4u;
constexpr float kPreviewFovDegrees = 45.0f;
constexpr float kPreviewNearPlane = 0.05f;
constexpr float kPreviewFarPlane = 100.0f;
constexpr float kPreviewTextureAnisotropy = 8.0f;

/// Interleaved GPU vertex representation for every retained preview mesh.
struct PreviewGpuVertex final {
  filament::math::float3 position;
  filament::math::float4 tangent_frame;
  filament::math::float2 uv;
  filament::math::float4 surface_grid_projection;
  filament::math::float4 uv_carrier{};
  filament::math::float3 uv_geometric_normal;
  filament::math::float4 quad_uv_coordinates{};
  filament::math::float4 quad_uv_correction{};
};

/// Retains the fill and wire resources for one generated preview mesh.
struct PreviewGpuMesh final {
  filament::VertexBuffer *vertices = nullptr;
  filament::IndexBuffer *triangles = nullptr;
  filament::IndexBuffer *lines = nullptr;
  utils::Entity fill_entity;
  utils::Entity wire_entity;
};

std::size_t mesh_index(MaterialPreviewMeshKind kind) noexcept {
  switch (kind) {
  case MaterialPreviewMeshKind::Sphere:
    return 0u;
  case MaterialPreviewMeshKind::Box:
    return 1u;
  case MaterialPreviewMeshKind::RoundedBox:
    return 2u;
  case MaterialPreviewMeshKind::Cylinder:
    return 3u;
  case MaterialPreviewMeshKind::Plane:
    return 4u;
  }
  return 0u;
}

filament::math::float4 packed_tangent_frame(
    const MaterialPreviewVertex &vertex) {
  const filament::math::mat3f tangent_frame{
      filament::math::float3{vertex.tangent_frame.tangent.x,
                             vertex.tangent_frame.tangent.y,
                             vertex.tangent_frame.tangent.z},
      filament::math::float3{vertex.tangent_frame.bitangent.x,
                             vertex.tangent_frame.bitangent.y,
                             vertex.tangent_frame.bitangent.z},
      filament::math::float3{vertex.normal.x, vertex.normal.y,
                             vertex.normal.z},
  };
  const filament::math::quatf packed =
      filament::math::mat3f::packTangentFrame(tangent_frame, sizeof(float));
  return {packed.x, packed.y, packed.z, packed.w};
}

glm::mat4 environment_rig_rotation(float rotation_radians) {
  return glm::mat4_cast(to_glm(quat_from_euler_radians(
      {0.0f, -rotation_radians, 0.0f})));
}

filament::math::mat4f filament_transform(
    const QQuat &rotation, float environment_rotation_radians) {
  // Filament's stock Skybox has no rotation API. Counter-rotating the camera
  // and test mesh preserves the inspected mesh pose while rotating both the
  // visible skybox and the IBL/key-light frame relative to the mesh.
  const glm::mat4 source =
      environment_rig_rotation(environment_rotation_radians) *
      to_glm(compose_transform(QTransform{
          .rotation = rotation,
      }));
  filament::math::mat4f result;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      result[column][row] = source[column][row];
    }
  }
  return result;
}

std::vector<std::uint32_t>
wire_indices(const MaterialPreviewMeshData &mesh) {
  std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
  for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
    const std::array<std::uint32_t, 3> triangle{
        mesh.indices[index], mesh.indices[index + 1u],
        mesh.indices[index + 2u]};
    for (std::size_t edge = 0u; edge < triangle.size(); ++edge) {
      const std::uint32_t first = triangle[edge];
      const std::uint32_t second = triangle[(edge + 1u) % triangle.size()];
      edges.emplace(std::min(first, second), std::max(first, second));
    }
  }
  std::vector<std::uint32_t> result;
  result.reserve(edges.size() * 2u);
  for (const auto &[first, second] : edges) {
    result.push_back(first);
    result.push_back(second);
  }
  return result;
}

filament::Texture *create_solid_texture(filament::Engine &engine,
                                        std::array<unsigned char, 4> rgba,
                                        TextureColorSpace color_space) {
  filament::Texture *texture =
      filament::Texture::Builder()
          .width(1u)
          .height(1u)
          .levels(1u)
          .sampler(filament::Texture::Sampler::SAMPLER_2D)
          .format(color_space == TextureColorSpace::Srgb
                      ? filament::Texture::InternalFormat::SRGB8_A8
                      : filament::Texture::InternalFormat::RGBA8)
          .usage(filament::Texture::Usage::UPLOADABLE |
                 filament::Texture::Usage::SAMPLEABLE)
          .build(engine);
  if (texture != nullptr) {
    texture->setImage(
        engine, 0u,
        FilamentBufferDescriptorFactory::rgba_pixels(rgba.data(),
                                                     rgba.size()));
  }
  return texture;
}

void disable_surface_grid(filament::MaterialInstance &instance) {
  instance.setParameter("surfaceGridMinorColor",
                        filament::math::float4{0.0f, 0.0f, 0.0f, 0.0f});
  instance.setParameter("surfaceGridMajorColor",
                        filament::math::float4{0.0f, 0.0f, 0.0f, 0.0f});
  instance.setParameter("surfaceGridSize", 1.0f);
  instance.setParameter("surfaceGridMajorSize", 4.0f);
  instance.setParameter("surfaceGridMinorLineThickness", 1.0f);
  instance.setParameter("surfaceGridMajorLineThickness", 1.0f);
  instance.setParameter("surfaceGridEnabled", 0.0f);
}

bool finite_color(QVec3 color) noexcept {
  return std::isfinite(color.x) && std::isfinite(color.y) &&
         std::isfinite(color.z);
}

} // namespace

/// Owns the isolated Filament scene and resources behind the preview facade.
struct MaterialPreviewRenderer::Impl final {
  ~Impl() { shutdown(); }

  bool initialize(filament::Engine &new_engine,
                  filament::Material &shaded_mesh_material,
                  filament::Material &rendered_mesh_material,
                  filament::Texture &solid_color_texture,
                  const std::filesystem::path &resource_root) {
    shutdown();
    engine = &new_engine;
    scene = engine->createScene();
    view = engine->createView();
    camera_entity = utils::EntityManager::get().create();
    camera = engine->createCamera(camera_entity);
    shaded_instance = shaded_mesh_material.createInstance();
    rendered_instance = rendered_mesh_material.createInstance();
    wire_instance = shaded_mesh_material.createInstance();
    if (scene == nullptr || view == nullptr || camera == nullptr ||
        shaded_instance == nullptr || rendered_instance == nullptr ||
        wire_instance == nullptr) {
      LOG_ERROR("Unable to create Standard Material preview resources");
      shutdown();
      return false;
    }

    shaded_instance->setCullingMode(
        filament::MaterialInstance::CullingMode::NONE);
    rendered_instance->setCullingMode(
        filament::MaterialInstance::CullingMode::NONE);
    wire_instance->setCullingMode(
        filament::MaterialInstance::CullingMode::NONE);
    disable_surface_grid(*shaded_instance);
    disable_surface_grid(*rendered_instance);
    disable_surface_grid(*wire_instance);
    bind_mesh_texture_mappings(*shaded_instance);
    bind_mesh_texture_mappings(*rendered_instance);
    bind_mesh_texture_mappings(*wire_instance);
    bind_default_shaded_lighting(*shaded_instance);
    bind_default_shaded_lighting(*wire_instance);
    wire_instance->setParameter("shadedLightingEnabled", 0.0f);

    fallback_textures[0] = create_solid_texture(
        *engine, {255u, 255u, 255u, 255u}, TextureColorSpace::Srgb);
    fallback_textures[1] = create_solid_texture(
        *engine, {255u, 255u, 255u, 255u}, TextureColorSpace::Linear);
    fallback_textures[2] = create_solid_texture(
        *engine, {255u, 255u, 255u, 255u}, TextureColorSpace::Linear);
    fallback_textures[3] = create_solid_texture(
        *engine, {128u, 128u, 255u, 255u}, TextureColorSpace::Linear);
    if (std::ranges::any_of(fallback_textures,
                            [](filament::Texture *texture) {
                              return texture == nullptr;
                            })) {
      LOG_ERROR("Unable to create Standard Material preview fallback textures");
      shutdown();
      return false;
    }

    filament::TextureSampler solid_sampler(
        filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::REPEAT);
    solid_sampler.setAnisotropy(kPreviewTextureAnisotropy);
    shaded_instance->setParameter("solidColorTexture", &solid_color_texture,
                                  solid_sampler);
    wire_instance->setParameter("solidColorTexture", &solid_color_texture,
                                solid_sampler);

    view->setName("Standard Material Preview");
    view->setScene(scene);
    view->setCamera(camera);
    view->setPostProcessingEnabled(true);
    view->setAntiAliasing(filament::AntiAliasing::FXAA);
    view->setDithering(filament::Dithering::TEMPORAL);
    view->setShadowingEnabled(false);
    view->setVisibleLayers(kViewportAllLayerMask, kViewportAllLayerMask);
    view->setMaterialGlobal(
        0u, filament::math::float4{0.0f, 0.0f, 0.0f, 1.0f});
    camera->setExposure(16.0f, 1.0f / 125.0f, 100.0f);

    environment.initialize(*engine, *scene, resource_root);
    apply_environment();
    create_key_light();

    for (MaterialPreviewMeshKind kind :
         {MaterialPreviewMeshKind::Sphere, MaterialPreviewMeshKind::Box,
          MaterialPreviewMeshKind::RoundedBox,
          MaterialPreviewMeshKind::Cylinder, MaterialPreviewMeshKind::Plane}) {
      if (!create_mesh(kind)) {
        LOG_ERROR("Unable to upload a Standard Material preview test mesh");
        shutdown();
        return false;
      }
    }

    resize(640u, 480u);
    apply_material();
    apply_state();
    return true;
  }

  void shutdown() {
    if (engine == nullptr) {
      return;
    }

    environment.shutdown();
    if (scene != nullptr && key_light) {
      scene->remove(key_light);
    }
    if (key_light) {
      if (engine->getLightManager().hasComponent(key_light)) {
        engine->getLightManager().destroy(key_light);
      }
      utils::EntityManager::get().destroy(key_light);
      key_light.clear();
    }
    for (PreviewGpuMesh &mesh : meshes) {
      destroy_mesh(mesh);
    }
    destroy_material_instance(wire_instance);
    destroy_material_instance(shaded_instance);
    destroy_material_instance(rendered_instance);
    for (filament::Texture *&texture : uploaded_textures) {
      destroy_texture(texture);
    }
    for (filament::Texture *&texture : fallback_textures) {
      destroy_texture(texture);
    }
    if (view != nullptr) {
      view->setColorGrading(nullptr);
      view->setCamera(nullptr);
      view->setScene(nullptr);
      engine->destroy(view);
      view = nullptr;
    }
    if (camera != nullptr) {
      engine->destroyCameraComponent(camera_entity);
      camera = nullptr;
    }
    if (camera_entity) {
      utils::EntityManager::get().destroy(camera_entity);
      camera_entity.clear();
    }
    if (scene != nullptr) {
      engine->destroy(scene);
      scene = nullptr;
    }
    texture_keys = {};
    width = 1u;
    height = 1u;
    engine = nullptr;
  }

  bool create_mesh(MaterialPreviewMeshKind kind) {
    const MaterialPreviewMeshData &source = mesh_library.mesh(kind);
    if (source.vertices.empty() || source.indices.empty() ||
        source.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    PreviewGpuMesh &mesh = meshes[mesh_index(kind)];
    std::vector<PreviewGpuVertex> vertices;
    vertices.reserve(source.vertices.size());
    for (const MaterialPreviewVertex &vertex : source.vertices) {
      vertices.push_back({
          .position = {vertex.position.x, vertex.position.y,
                       vertex.position.z},
          .tangent_frame = packed_tangent_frame(vertex),
          .uv = {vertex.uv.x, vertex.uv.y},
          .surface_grid_projection = {vertex.normal.x, vertex.normal.y,
                                      vertex.normal.z, 0.0f},
          .uv_geometric_normal = {vertex.normal.x, vertex.normal.y, vertex.normal.z},
      });
    }
    const std::vector<std::uint32_t> lines = wire_indices(source);

    mesh.vertices =
        filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(vertices.size()))
            .bufferCount(1u)
            .attribute(filament::VertexAttribute::POSITION, 0u,
                       AttributeType::FLOAT3,
                       offsetof(PreviewGpuVertex, position),
                       sizeof(PreviewGpuVertex))
            .attribute(filament::VertexAttribute::TANGENTS, 0u,
                       AttributeType::FLOAT4,
                       offsetof(PreviewGpuVertex, tangent_frame),
                       sizeof(PreviewGpuVertex))
            .attribute(filament::VertexAttribute::UV0, 0u,
                       AttributeType::FLOAT2, offsetof(PreviewGpuVertex, uv),
                       sizeof(PreviewGpuVertex))
            .attribute(
                filament::VertexAttribute::CUSTOM0, 0u,
                AttributeType::FLOAT4,
                offsetof(PreviewGpuVertex, surface_grid_projection),
                sizeof(PreviewGpuVertex))
            .attribute(filament::VertexAttribute::CUSTOM1, 0u,
                       AttributeType::FLOAT4,
                       offsetof(PreviewGpuVertex, uv_carrier),
                       sizeof(PreviewGpuVertex))
            .attribute(filament::VertexAttribute::CUSTOM2, 0u,
                       AttributeType::FLOAT3,
                       offsetof(PreviewGpuVertex, uv_geometric_normal),
                       sizeof(PreviewGpuVertex))
            .attribute(filament::VertexAttribute::CUSTOM3, 0u,
                       AttributeType::FLOAT4,
                       offsetof(PreviewGpuVertex, quad_uv_coordinates),
                       sizeof(PreviewGpuVertex))
            .attribute(filament::VertexAttribute::CUSTOM4, 0u,
                       AttributeType::FLOAT4,
                       offsetof(PreviewGpuVertex, quad_uv_correction),
                       sizeof(PreviewGpuVertex))
            .build(*engine);
    mesh.triangles =
        filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(source.indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine);
    mesh.lines = filament::IndexBuffer::Builder()
                     .indexCount(static_cast<std::uint32_t>(lines.size()))
                     .bufferType(filament::IndexBuffer::IndexType::UINT)
                     .build(*engine);
    if (mesh.vertices == nullptr || mesh.triangles == nullptr ||
        mesh.lines == nullptr) {
      return false;
    }
    mesh.vertices->setBufferAt(
        *engine, 0u, FilamentBufferDescriptorFactory::vector(vertices));
    mesh.triangles->setBuffer(
        *engine, FilamentBufferDescriptorFactory::span(
                     std::span<const std::uint32_t>(source.indices)));
    mesh.lines->setBuffer(
        *engine, FilamentBufferDescriptorFactory::span(
                     std::span<const std::uint32_t>(lines)));

    mesh.fill_entity = utils::EntityManager::get().create();
    mesh.wire_entity = utils::EntityManager::get().create();
    const filament::Box bounds{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    filament::RenderableManager::Builder(1u)
        .boundingBox(bounds)
        .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                  mesh.vertices, mesh.triangles)
        .material(0u, rendered_instance)
        .castShadows(false)
        .receiveShadows(false)
        .build(*engine, mesh.fill_entity);
    filament::RenderableManager::Builder(1u)
        .boundingBox(bounds)
        .geometry(0u, filament::RenderableManager::PrimitiveType::LINES,
                  mesh.vertices, mesh.lines)
        .material(0u, wire_instance)
        .castShadows(false)
        .receiveShadows(false)
        .build(*engine, mesh.wire_entity);
    engine->getTransformManager().create(mesh.fill_entity);
    engine->getTransformManager().create(mesh.wire_entity);
    return engine->getRenderableManager().hasComponent(mesh.fill_entity) &&
           engine->getRenderableManager().hasComponent(mesh.wire_entity);
  }

  void destroy_mesh(PreviewGpuMesh &mesh) {
    destroy_entity(mesh.fill_entity);
    destroy_entity(mesh.wire_entity);
    if (mesh.lines != nullptr) {
      engine->destroy(mesh.lines);
      mesh.lines = nullptr;
    }
    if (mesh.triangles != nullptr) {
      engine->destroy(mesh.triangles);
      mesh.triangles = nullptr;
    }
    if (mesh.vertices != nullptr) {
      engine->destroy(mesh.vertices);
      mesh.vertices = nullptr;
    }
  }

  void destroy_entity(utils::Entity &entity) {
    if (!entity) {
      return;
    }
    if (scene != nullptr) {
      scene->remove(entity);
    }
    if (engine->getRenderableManager().hasComponent(entity)) {
      engine->getRenderableManager().destroy(entity);
    }
    if (engine->getTransformManager().hasComponent(entity)) {
      engine->getTransformManager().destroy(entity);
    }
    utils::EntityManager::get().destroy(entity);
    entity.clear();
  }

  void destroy_texture(filament::Texture *&texture) {
    if (texture != nullptr) {
      engine->destroy(texture);
      texture = nullptr;
    }
  }

  void destroy_material_instance(filament::MaterialInstance *&instance) {
    if (instance != nullptr) {
      engine->destroy(instance);
      instance = nullptr;
    }
  }

  void create_key_light() {
    key_light = utils::EntityManager::get().create();
    filament::LightManager::Builder(
        filament::LightManager::Type::DIRECTIONAL)
        .color({1.0f, 0.98f, 0.95f})
        .intensity(65000.0f)
        .direction({-0.45f, -0.75f, -0.5f})
        .castShadows(false)
        .build(*engine, key_light);
    if (engine->getLightManager().hasComponent(key_light)) {
      scene->addEntity(key_light);
    }
  }

  void resize(std::uint32_t new_width, std::uint32_t new_height) {
    width = std::max(1u, new_width);
    height = std::max(1u, new_height);
    if (view != nullptr) {
      view->setRenderTarget(nullptr);
      view->setViewport({0, 0, width, height});
    }
    update_camera();
  }

  void update_camera() {
    if (camera == nullptr) {
      return;
    }
    const QVec2 pan = state.pan_offset();
    const glm::mat4 rig = environment_rig_rotation(
        state.environment_rotation_radians());
    const glm::vec3 target = glm::vec3(rig * glm::vec4{
        pan.x, pan.y, 0.0f, 1.0f});
    const glm::vec3 eye = target + glm::vec3(rig * glm::vec4{
        0.0f, 0.0f, state.camera_distance(), 0.0f});
    camera->lookAt({eye.x, eye.y, eye.z},
                   {target.x, target.y, target.z}, {0.0f, 1.0f, 0.0f});
    const double aspect = static_cast<double>(width) /
                          static_cast<double>(std::max(1u, height));
    camera->setProjection(kPreviewFovDegrees, aspect, kPreviewNearPlane,
                          kPreviewFarPlane);
  }

  void apply_state() {
    if (engine == nullptr || scene == nullptr) {
      return;
    }
    update_camera();
    apply_environment();
    const filament::math::mat4f transform =
        filament_transform(state.object_rotation(),
                           state.environment_rotation_radians());
    for (PreviewGpuMesh &mesh : meshes) {
      set_transform(mesh.fill_entity, transform);
      set_transform(mesh.wire_entity, transform);
      scene->remove(mesh.fill_entity);
      scene->remove(mesh.wire_entity);
    }

    filament::RenderableManager &renderables =
        engine->getRenderableManager();
    filament::MaterialInstance *fill_material =
        state.shading_mode() == MaterialPreviewShadingMode::Rendered
            ? rendered_instance
            : shaded_instance;
    for (PreviewGpuMesh &mesh : meshes) {
      const filament::RenderableManager::Instance fill =
          renderables.getInstance(mesh.fill_entity);
      if (fill) {
        renderables.setMaterialInstanceAt(fill, 0u, fill_material);
      }
    }
    PreviewGpuMesh &active = meshes[mesh_index(state.mesh_kind())];
    scene->addEntity(state.shading_mode() ==
                             MaterialPreviewShadingMode::Wireframe
                         ? active.wire_entity
                         : active.fill_entity);
  }

  void apply_environment() {
    SceneEnvironmentSettings settings;
    settings.environment_id = std::string(state.environment_id());
    settings.show_background = state.show_environment_background();
    // The camera/test-mesh counter-rig supplies the effective IBL rotation and
    // is also able to rotate Filament's otherwise fixed stock skybox.
    settings.rotation_y = 0.0f;
    environment.apply(settings);
  }

  void set_transform(utils::Entity entity,
                     const filament::math::mat4f &transform) {
    if (!entity) {
      return;
    }
    filament::TransformManager &transforms = engine->getTransformManager();
    const filament::TransformManager::Instance instance =
        transforms.getInstance(entity);
    if (instance) {
      transforms.setTransform(instance, transform);
    }
  }

  void apply_material() {
    if (engine == nullptr || shaded_instance == nullptr ||
        rendered_instance == nullptr || wire_instance == nullptr) {
      return;
    }
    update_textures();
    static const RenderStandardMaterialEvaluationSnapshot kFallbackMaterial;
    const RenderStandardMaterialEvaluationSnapshot *typed =
        material.standard_material();
    const RenderStandardMaterialEvaluationSnapshot &standard =
        typed != nullptr ? *typed : kFallbackMaterial;
    const QVec3 base_color = finite_color(standard.base_color)
                                 ? QVec3{
                                       std::clamp(standard.base_color.x, 0.0f,
                                                  1.0f),
                                       std::clamp(standard.base_color.y, 0.0f,
                                                  1.0f),
                                       std::clamp(standard.base_color.z, 0.0f,
                                                  1.0f),
                                   }
                                 : QVec3{1.0f, 1.0f, 1.0f};
    const filament::math::float4 base_factor{base_color.x, base_color.y,
                                             base_color.z, 1.0f};
    filament::TextureSampler sampler(
        filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::REPEAT);
    sampler.setAnisotropy(kPreviewTextureAnisotropy);

    shaded_instance->setParameter("baseColorTexture", texture(0u), sampler);
    shaded_instance->setParameter("baseColorFactor", base_factor);
    bind_standard_material_texture_transform(
        *shaded_instance, standard.texture_transform);
    rendered_instance->setParameter("baseColorTexture", texture(0u), sampler);
    rendered_instance->setParameter("roughnessTexture", texture(1u), sampler);
    rendered_instance->setParameter("metallicTexture", texture(2u), sampler);
    rendered_instance->setParameter("normalTexture", texture(3u), sampler);
    rendered_instance->setParameter("baseColorFactor", base_factor);
    bind_standard_material_texture_transform(
        *rendered_instance, standard.texture_transform);
    rendered_instance->setParameter(
        "roughnessFactor",
        std::isfinite(standard.roughness)
            ? std::clamp(standard.roughness, 0.0f, 1.0f)
            : 1.0f);
    rendered_instance->setParameter(
        "metallicFactor",
        std::isfinite(standard.metallic)
            ? std::clamp(standard.metallic, 0.0f, 1.0f)
            : 0.0f);
    rendered_instance->setParameter(
        "normalStrength",
        std::isfinite(standard.normal_strength)
            ? std::clamp(standard.normal_strength, -16.0f, 16.0f)
            : 1.0f);
    rendered_instance->setParameter("normalYSign",
                                    standard.directx_normal ? -1.0f : 1.0f);
    rendered_instance->setParameter(
        "diffuseModel", standard.diffuse_model == MaterialDiffuseModel::Lambert
                            ? 1
                            : 0);
    wire_instance->setParameter("baseColorTexture", fallback_textures[0],
                                sampler);
    wire_instance->setParameter(
        "baseColorFactor",
        filament::math::float4{0.82f, 0.82f, 0.82f, 1.0f});
    bind_standard_material_texture_transform(
        *wire_instance, RenderStandardMaterialTextureTransformSnapshot{});
  }

  void update_textures() {
    static const RenderStandardMaterialEvaluationSnapshot kFallbackMaterial;
    const RenderStandardMaterialEvaluationSnapshot *typed =
        material.standard_material();
    const RenderStandardMaterialEvaluationSnapshot &standard =
        typed != nullptr ? *typed : kFallbackMaterial;
    for (std::size_t index = 0u; index < kPreviewTextureCount; ++index) {
      const MaterialPreviewTextureSnapshot &next = standard.textures[index];
      if (texture_keys[index] == next) {
        continue;
      }
      destroy_texture(uploaded_textures[index]);
      texture_keys[index] = next;
      if (!next.decoded_present()) {
        continue;
      }
      uploaded_textures[index] = load_rgba_texture(
          *engine, std::span<const std::byte>(*next.decoded_rgba8), next.width,
          next.height,
          PngTextureLoadOptions{
              .color_space = next.color_space == TextureColorSpace::Srgb
                                 ? PngTextureColorSpace::Srgb
                                 : PngTextureColorSpace::Linear,
              .generate_mipmaps = true,
              .label = "Standard Material preview texture",
          });
    }
  }

  filament::Texture *texture(std::size_t index) const noexcept {
    return uploaded_textures[index] != nullptr ? uploaded_textures[index]
                                                : fallback_textures[index];
  }

  void render(filament::Renderer &renderer) {
    if (view != nullptr) {
      renderer.render(view);
    }
  }

  filament::Engine *engine = nullptr;
  filament::Scene *scene = nullptr;
  filament::View *view = nullptr;
  utils::Entity camera_entity;
  filament::Camera *camera = nullptr;
  utils::Entity key_light;
  FilamentEnvironmentLibrary environment;
  MaterialPreviewMeshLibrary mesh_library;
  std::array<PreviewGpuMesh, kPreviewMeshCount> meshes;
  filament::MaterialInstance *shaded_instance = nullptr;
  filament::MaterialInstance *rendered_instance = nullptr;
  filament::MaterialInstance *wire_instance = nullptr;
  std::array<filament::Texture *, kPreviewTextureCount> fallback_textures{};
  std::array<filament::Texture *, kPreviewTextureCount> uploaded_textures{};
  std::array<MaterialPreviewTextureSnapshot, kPreviewTextureCount>
      texture_keys;
  MaterialPreviewState state;
  StandardMaterialPreviewSnapshot material;
  std::uint32_t width = 1u;
  std::uint32_t height = 1u;
};

MaterialPreviewRenderer::MaterialPreviewRenderer()
    : impl_(std::make_unique<Impl>()) {}

MaterialPreviewRenderer::~MaterialPreviewRenderer() = default;

bool MaterialPreviewRenderer::initialize(
    filament::Engine &engine, filament::Material &shaded_mesh_material,
    filament::Material &rendered_mesh_material,
    filament::Texture &solid_color_texture,
    const std::filesystem::path &resource_root) {
  return impl_->initialize(engine, shaded_mesh_material,
                           rendered_mesh_material, solid_color_texture,
                           resource_root);
}

void MaterialPreviewRenderer::shutdown() { impl_->shutdown(); }

void MaterialPreviewRenderer::resize(std::uint32_t width,
                                     std::uint32_t height) {
  impl_->resize(width, height);
}

void MaterialPreviewRenderer::set_preview_state(
    const MaterialPreviewState &state) {
  impl_->state = state;
  impl_->apply_state();
}

void MaterialPreviewRenderer::set_material(
    StandardMaterialPreviewSnapshot snapshot) {
  if (impl_->material == snapshot) {
    return;
  }
  impl_->material = std::move(snapshot);
  impl_->apply_material();
}

void MaterialPreviewRenderer::render(filament::Renderer &renderer) {
  impl_->render(renderer);
}

bool MaterialPreviewRenderer::initialized() const noexcept {
  return impl_->engine != nullptr;
}

std::uint32_t MaterialPreviewRenderer::width() const noexcept {
  return impl_->width;
}

std::uint32_t MaterialPreviewRenderer::height() const noexcept {
  return impl_->height;
}

} // namespace quader
