////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_object_overlay_cache.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/render_layers.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat4.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr std::size_t kMaximumBatchVertices = 262144u;
constexpr std::size_t kMaximumBatchIndices = 786432u;
constexpr double kMaximumBatchOriginDistance = 4096.0;

filament::math::mat4f make_transform(const QMat4 &transform) {
  const glm::mat4 glm_transform = to_glm(transform);
  filament::math::mat4f result;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      result[column][row] = glm_transform[column][row];
    }
  }
  return result;
}

filament::MaterialInstance::DepthFunc
to_filament_depth_func(OverlayDepthFunc depth_func) {
  switch (depth_func) {
  case OverlayDepthFunc::Always:
    return filament::MaterialInstance::DepthFunc::A;
  case OverlayDepthFunc::Equal:
    return filament::MaterialInstance::DepthFunc::E;
  case OverlayDepthFunc::Less:
    return filament::MaterialInstance::DepthFunc::L;
  case OverlayDepthFunc::GreaterOrEqual:
    return filament::MaterialInstance::DepthFunc::GE;
  }
  return filament::MaterialInstance::DepthFunc::A;
}

filament::MaterialInstance::CullingMode
to_filament_culling_mode(OverlayCullingMode culling_mode) {
  switch (culling_mode) {
  case OverlayCullingMode::None:
    return filament::MaterialInstance::CullingMode::NONE;
  case OverlayCullingMode::Back:
    return filament::MaterialInstance::CullingMode::BACK;
  }
  return filament::MaterialInstance::CullingMode::NONE;
}

filament::MaterialInstance::TransparencyMode
to_filament_transparency_mode(OverlayTransparencyMode transparency_mode) {
  switch (transparency_mode) {
  case OverlayTransparencyMode::Default:
    return filament::MaterialInstance::TransparencyMode::DEFAULT;
  case OverlayTransparencyMode::TwoPassesOneSide:
    return filament::MaterialInstance::TransparencyMode::TWO_PASSES_ONE_SIDE;
  case OverlayTransparencyMode::TwoPassesTwoSides:
    return filament::MaterialInstance::TransparencyMode::TWO_PASSES_TWO_SIDES;
  }
  return filament::MaterialInstance::TransparencyMode::DEFAULT;
}

void configure_overlay_material(filament::MaterialInstance &material,
                                const OverlayRenderState &state,
                                bool stencil_enabled) {
  material.setColorWrite(state.color_write);
  material.setDepthWrite(state.depth_write);
  material.setDepthCulling(state.depth_culling);
  material.setDepthFunc(to_filament_depth_func(state.depth_func));
  material.setCullingMode(to_filament_culling_mode(state.culling_mode));
  material.setTransparencyMode(
      to_filament_transparency_mode(state.transparency_mode));
  material.setPolygonOffset(state.polygon_offset_scale,
                            state.polygon_offset_constant);
  apply_overlay_face_fill_stencil(material, state.face_fill_class,
                                  state.face_fill_pass, stencil_enabled);
}

void set_face_color(filament::MaterialInstance &material, std::uint32_t color) {
  // Preserve the old vertex attribute's 8-bit linear quantization exactly.
  const std::uint32_t linear = rgba8_srgb_to_linear(color);
  material.setParameter("useUniformColor", true);
  material.setParameter("uniformColor", filament::math::float4{
      static_cast<float>(linear & 0xffu) / 255.0f,
      static_cast<float>((linear >> 8u) & 0xffu) / 255.0f,
      static_cast<float>((linear >> 16u) & 0xffu) / 255.0f,
      static_cast<float>((linear >> 24u) & 0xffu) / 255.0f});
}

filament::Box bounds_for_vertices(std::span<const OverlayFaceFillVertex> vertices) {
  QVec3 minimum{std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()};
  QVec3 maximum{std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()};
  for (const auto &vertex : vertices) {
    minimum.x = std::min(minimum.x, vertex.position.x);
    minimum.y = std::min(minimum.y, vertex.position.y);
    minimum.z = std::min(minimum.z, vertex.position.z);
    maximum.x = std::max(maximum.x, vertex.position.x);
    maximum.y = std::max(maximum.y, vertex.position.y);
    maximum.z = std::max(maximum.z, vertex.position.z);
  }
  filament::Box result;
  result.set(to_filament(minimum), to_filament(maximum));
  return result;
}

bool valid_selected_mesh(const RenderOverlayMeshSnapshot &mesh) {
  return mesh.has_triangle_geometry() && mesh.mesh_id != kInvalidAssetId;
}

OverlayFaceFillVertex face_vertex(QVec3 position) {
  return {{position.x, position.y, position.z}, {}, 0xffffffffu};
}

} // namespace

SelectedObjectOverlayCache::~SelectedObjectOverlayCache() {
  shutdown();
}

void SelectedObjectOverlayCache::initialize(
    filament::Engine &engine, filament::Scene &scene,
    filament::Material &face_fill_material) {
  engine_ = &engine;
  scene_ = &scene;
  face_fill_material_ = &face_fill_material;
}

void SelectedObjectOverlayCache::shutdown() {
  if (engine_ == nullptr) {
    return;
  }
  destroy_face_batches();
  synced_states_.clear();
  synced_source_stamp_ = {};
  synced_face_style_signature_ = 0u;
  has_synced_ = false;
  engine_ = nullptr;
  scene_ = nullptr;
  face_fill_material_ = nullptr;
  visible_ = true;
}

void SelectedObjectOverlayCache::sync(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return;
  }
  if (has_synced_ && !render_source_dependencies_match(
                         synced_source_stamp_, snapshot,
                         RenderSourceDependency::SceneBinding)) {
    destroy_face_batches();
    synced_states_.clear();
    has_synced_ = false;
  }
  if (!snapshot.visible) {
    set_visible(false);
    return;
  }
  set_visible(true);
  const bool face_style_changed = selected_object_face_style_changed(
      has_synced_, synced_face_style_signature_, snapshot.style);
  const bool sources_match = has_synced_ &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          kRenderOverlaySelectedObjectSourceDependencies) &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot, RenderSourceDependency::TransformRevision);
  if (sources_match) {
    if (face_style_changed) {
      update_face_colors(snapshot.style);
      flush_resource_commands();
    }
    synced_source_stamp_ = snapshot;
    synced_face_style_signature_ = snapshot.style.face_signature;
    return;
  }

  auto states = selected_entity_states(snapshot);
  if (!has_synced_ || states != synced_states_) {
    rebuild_face_batches(snapshot);
  } else if (face_style_changed) {
    update_face_colors(snapshot.style);
  }
  flush_resource_commands();
  synced_states_ = std::move(states);
  synced_source_stamp_ = snapshot;
  synced_face_style_signature_ = snapshot.style.face_signature;
  has_synced_ = true;
}

std::vector<SelectedObjectOverlayCache::SelectedEntityState>
SelectedObjectOverlayCache::selected_entity_states(
    const RenderOverlaySelectedObjectSnapshot &snapshot) const {
  QDR_PROFILE_FUNCTION();
  std::vector<SelectedEntityState> states;
  states.reserve(snapshot.meshes.size());
  for (const auto &mesh : snapshot.meshes) {
    if (valid_selected_mesh(mesh)) {
      states.push_back({
          .entity = mesh.entity,
          .mesh_id = mesh.mesh_id,
          .mesh_revision = mesh.mesh_revision,
          .geometry_content_id = mesh.geometry_content_id,
          .world_transform = mesh.world_transform.values,
          .vertex_count = mesh.vertices.size(),
          .index_count = mesh.indices.size(),
          .active_selected = mesh.active_selected});
    }
  }
  return states;
}

void SelectedObjectOverlayCache::rebuild_face_batches(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  destroy_face_batches();
  const bool combine_meshes = engine_->getBackend() == filament::Engine::Backend::VULKAN;
  if (!combine_meshes) {
    // Without stencil, alpha composition depends on per-mesh sorting. Preserve
    // each original local bound and transform on the legacy backend path.
    for (const auto &mesh : snapshot.meshes) {
      if (!valid_selected_mesh(mesh)) {
        continue;
      }
      std::vector<OverlayFaceFillVertex> vertices;
      vertices.reserve(mesh.vertices.size());
      for (const auto &vertex : mesh.vertices) {
        vertices.push_back(face_vertex(vertex.position));
      }
      std::vector<std::uint32_t> indices;
      indices.reserve(mesh.indices.size());
      for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
        const auto a = mesh.indices[index];
        const auto b = mesh.indices[index + 1u];
        const auto c = mesh.indices[index + 2u];
        if (a < mesh.vertices.size() && b < mesh.vertices.size() && c < mesh.vertices.size()) {
          indices.insert(indices.end(), {a, b, c});
        }
      }
      create_face_batch(std::move(vertices), std::move(indices),
                        mesh.active_selected, snapshot.style, mesh.world_transform);
    }
    return;
  }

  // Stencil makes same-class, same-color triangles order independent. The
  // active class stays separate so coverage priority remains authored policy.
  for (const bool active_selected : {false, true}) {
    std::vector<OverlayFaceFillVertex> vertices;
    std::vector<std::uint32_t> indices;
    QMat4 batch_transform;
    bool has_origin = false;
    const auto submit = [&] {
      create_face_batch(std::move(vertices), std::move(indices),
                        active_selected, snapshot.style, batch_transform);
      vertices.clear();
      indices.clear();
      has_origin = false;
    };
    for (const auto &mesh : snapshot.meshes) {
      if (!valid_selected_mesh(mesh) || mesh.active_selected != active_selected) {
        continue;
      }
      const glm::dmat4 world{to_glm(mesh.world_transform)};
      const bool reversed_winding = glm::determinant(glm::dmat3{world}) < 0.0;
      if (has_origin) {
        for (std::size_t axis = 12u; axis < 15u; ++axis) {
          const double separation = static_cast<double>(mesh.world_transform.values[axis]) -
                                    static_cast<double>(batch_transform.values[axis]);
          if (std::abs(separation) > kMaximumBatchOriginDistance) {
            // Distant clusters need their own origin; otherwise a small mesh
            // could collapse when narrowed beside geometry far across a scene.
            submit();
            break;
          }
        }
      }
      const auto retain_origin = [&] {
        if (!has_origin) {
          batch_transform.values[12] = mesh.world_transform.values[12];
          batch_transform.values[13] = mesh.world_transform.values[13];
          batch_transform.values[14] = mesh.world_transform.values[14];
          has_origin = true;
        }
      };
      const auto transformed_vertex = [&](std::uint32_t index) {
        // Subtract the cluster's origin before narrowing to float, preserving
        // small local geometry when the whole cluster is far from world zero.
        const glm::dvec4 position = world * glm::dvec4{to_glm(mesh.vertices[index].position), 1.0};
        const glm::dvec3 origin{batch_transform.values[12], batch_transform.values[13],
                               batch_transform.values[14]};
        return face_vertex(from_glm(glm::vec3{glm::dvec3{position} / position.w - origin}));
      };
      if (mesh.vertices.size() > kMaximumBatchVertices ||
          mesh.indices.size() > kMaximumBatchIndices) {
        submit();
        // Split an oversized single mesh without allocating an unbounded index
        // remapping table. Ordinary meshes retain their shared indexed vertices.
        for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
          const auto a = mesh.indices[index];
          const auto b = mesh.indices[index + 1u];
          const auto c = mesh.indices[index + 2u];
          if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) {
            continue;
          }
          if (vertices.size() + 3u > kMaximumBatchVertices) {
            submit();
          }
          retain_origin();
          const auto base = static_cast<std::uint32_t>(vertices.size());
          vertices.insert(vertices.end(), {
              transformed_vertex(a), transformed_vertex(b), transformed_vertex(c)});
          indices.insert(indices.end(), {base, base + (reversed_winding ? 2u : 1u),
                                          base + (reversed_winding ? 1u : 2u)});
        }
        submit();
        continue;
      }
      if (vertices.size() + mesh.vertices.size() > kMaximumBatchVertices ||
          indices.size() + mesh.indices.size() > kMaximumBatchIndices) {
        submit();
      }
      retain_origin();
      const auto base = static_cast<std::uint32_t>(vertices.size());
      for (std::uint32_t index = 0u; index < mesh.vertices.size(); ++index) {
        vertices.push_back(transformed_vertex(index));
      }
      for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
        const auto a = mesh.indices[index];
        const auto b = mesh.indices[index + 1u];
        const auto c = mesh.indices[index + 2u];
        if (a < mesh.vertices.size() && b < mesh.vertices.size() && c < mesh.vertices.size()) {
          // Filament normally flips front-face state for reflected entities;
          // the shared batch transform has positive determinant instead.
          indices.insert(indices.end(), {base + a, base + (reversed_winding ? c : b),
                                          base + (reversed_winding ? b : c)});
        }
      }
    }
    submit();
  }
}

void SelectedObjectOverlayCache::create_face_batch(
    std::vector<OverlayFaceFillVertex> vertices,
    std::vector<std::uint32_t> indices, bool active_selected,
    const ViewportPersistentOverlayStyleSnapshot &style,
    const QMat4 &world_transform) {
  if (vertices.empty() || indices.empty()) {
    return;
  }
  FaceBatch batch;
  batch.active_selected = active_selected;
  batch.face_color = active_selected ? style.active_selection_face_color
                                     : style.selection_face_color;
  const auto fill_class = visible_overlay_face_fill_class(
      active_selected ? OverlayFaceFillClass::ActiveSelection
                      : OverlayFaceFillClass::Selection, batch.face_color);
  const auto bounds = bounds_for_vertices(vertices);
  batch.vertex_buffer = filament::VertexBuffer::Builder()
      .vertexCount(static_cast<std::uint32_t>(vertices.size())).bufferCount(1)
      .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3,
                 offsetof(OverlayFaceFillVertex, position), sizeof(OverlayFaceFillVertex))
      .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2,
                 offsetof(OverlayFaceFillVertex, uv), sizeof(OverlayFaceFillVertex))
      .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4,
                 offsetof(OverlayFaceFillVertex, color), sizeof(OverlayFaceFillVertex))
      .normalized(filament::VertexAttribute::COLOR)
      .build(*engine_);
  batch.vertex_buffer->setBufferAt(
      *engine_, 0, FilamentBufferDescriptorFactory::vector(std::move(vertices)));
  batch.index_buffer = filament::IndexBuffer::Builder()
      .indexCount(static_cast<std::uint32_t>(indices.size()))
      .bufferType(filament::IndexBuffer::IndexType::UINT)
      .build(*engine_);
  batch.index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::vector(std::move(indices)));

  const bool stencil_enabled = engine_->getBackend() == filament::Engine::Backend::VULKAN;
  const std::uint8_t layer_mask = visible_ ? kViewportOverlayLayerMask : 0u;
  std::size_t proxy_index = 0u;
  for (const auto layer : {ViewportOverlayLayer::SelectedXrayFace,
                           ViewportOverlayLayer::SelectedFaceMask}) {
    const auto blend_order = static_cast<std::uint16_t>(viewport_overlay_draw_order(layer) * 64);
    for (const auto pass : {OverlayFaceFillPass::Mask, OverlayFaceFillPass::Color}) {
      auto state = OverlayRenderableBuilder::make_state(
          layer, ViewportOverlayGeometryKind::Triangles, blend_order);
      state.face_fill_class = fill_class;
      state.face_fill_pass = pass;
      if (pass == OverlayFaceFillPass::Mask) {
        state.color_write = false;
        state.blend_order = overlay_face_fill_mask_blend_order();
      }
      auto &proxy = batch.proxies[proxy_index++];
      proxy.face_fill_pass = pass;
      proxy.entity = utils::EntityManager::get().create();
      proxy.material_instance = face_fill_material_->createInstance();
      configure_overlay_material(*proxy.material_instance, state, stencil_enabled);
      set_face_color(*proxy.material_instance, batch.face_color);
      filament::RenderableManager::Builder(1)
          .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                    batch.vertex_buffer, batch.index_buffer)
          .material(0, proxy.material_instance)
          .blendOrder(0, state.blend_order)
          .globalBlendOrderEnabled(0, true)
          .layerMask(kViewportAllLayerMask, layer_mask)
          .boundingBox(bounds)
          .culling(false)
          .castShadows(false)
          .receiveShadows(false)
          .build(*engine_, proxy.entity);
      auto &transforms = engine_->getTransformManager();
      transforms.create(proxy.entity);
      transforms.setTransform(transforms.getInstance(proxy.entity), make_transform(world_transform));
      scene_->addEntity(proxy.entity);
    }
  }
  batches_.push_back(std::move(batch));
  record_resource_change();
}

void SelectedObjectOverlayCache::update_face_colors(
    const ViewportPersistentOverlayStyleSnapshot &style) {
  for (auto &batch : batches_) {
    const auto color = batch.active_selected ? style.active_selection_face_color
                                             : style.selection_face_color;
    if (batch.face_color == color) {
      continue;
    }
    const auto fill_class = visible_overlay_face_fill_class(
        batch.active_selected ? OverlayFaceFillClass::ActiveSelection
                              : OverlayFaceFillClass::Selection, color);
    for (const auto &proxy : batch.proxies) {
      set_face_color(*proxy.material_instance, color);
      apply_overlay_face_fill_stencil(*proxy.material_instance, fill_class,
          proxy.face_fill_pass, engine_->getBackend() == filament::Engine::Backend::VULKAN);
    }
    batch.face_color = color;
    record_resource_change();
  }
}

void SelectedObjectOverlayCache::destroy_face_batches() {
  // Detach the complete retiring set first. Filament's enabled material lifetime
  // validation otherwise rescans the remaining selected renderables per material.
  for (const auto &batch : batches_) {
    for (const auto &proxy : batch.proxies) {
      engine_->getRenderableManager().destroy(proxy.entity);
    }
    record_resource_change();
  }
  for (auto &batch : batches_) {
    for (auto &proxy : batch.proxies) {
      scene_->remove(proxy.entity);
      engine_->getTransformManager().destroy(proxy.entity);
      engine_->destroy(proxy.material_instance);
      utils::EntityManager::get().destroy(proxy.entity);
    }
    // All borrowers have been detached before either shared buffer is retired.
    engine_->destroy(batch.vertex_buffer);
    engine_->destroy(batch.index_buffer);
    record_resource_change();
  }
  batches_.clear();
  flush_resource_commands();
}

void SelectedObjectOverlayCache::set_visible(bool visible) {
  if (visible_ == visible) {
    return;
  }
  visible_ = visible;
  auto &renderables = engine_->getRenderableManager();
  const std::uint8_t layer_mask = visible_ ? kViewportOverlayLayerMask : 0u;
  for (const auto &batch : batches_) {
    for (const auto &proxy : batch.proxies) {
      renderables.setLayerMask(renderables.getInstance(proxy.entity),
                               kViewportAllLayerMask, layer_mask);
    }
  }
}

void SelectedObjectOverlayCache::record_resource_change() {
  // Each batch emits a fixed four-proxy sequence; upload storage lives outside
  // the command arena. Bound submissions even on the legacy per-mesh path.
  constexpr std::size_t kResourceChangesPerSubmission = 64u;
  if (++pending_resource_changes_ >= kResourceChangesPerSubmission) {
    flush_resource_commands();
  }
}

void SelectedObjectOverlayCache::flush_resource_commands() {
  if (engine_ != nullptr && pending_resource_changes_ != 0u) {
    engine_->flush();
    pending_resource_changes_ = 0u;
  }
}

} // namespace quader
