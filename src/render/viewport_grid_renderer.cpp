////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_grid_renderer.hpp"

#include "core/color_space.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/instance_edit_style.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_settings_defaults.hpp"

#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr float kGroundGridFadeStart = 96.0f;
constexpr float kGroundGridFadeEnd = 1536.0f;
constexpr float kParentGridWorldSize = 2.0f;
// Clip-depth bias keeps the plane visible on coplanar faces without disabling
// mesh occlusion. The perspective material applies it in homogeneous space.
constexpr float kGroundGridDepthBias = 0.005f;
constexpr float kHalfPi = 1.57079632679f;
// The grid belongs below overlay fills and handles. Filament's default
// priority is 4; priority 3 avoids tying the first overlay blend order, while
// global ordering prevents camera distance from overriding this layer order.
constexpr std::uint8_t kGroundGridRenderablePriority = 3u;
constexpr std::uint16_t kGroundGridBlendOrder = 0u;

/**
 * Represents a Ground Grid Vertex value used by the render extraction and Filament resource layer.
 */
struct GroundGridVertex {
    filament::math::float3 position;
};

filament::Box ground_grid_bounds(float width, float height) {
  filament::Box box;
  const float half_x = width * 0.5f;
  const float half_z = height * 0.5f;
  box.set({-half_x, -0.01f, -half_z}, {half_x, 0.01f, half_z});
  return box;
}

filament::math::float4 setting_color(ViewportSettingColor color) {
  return {
      srgb_to_linear(color.r),
      srgb_to_linear(color.g),
      srgb_to_linear(color.b),
      color.a,
  };
}

RenderSignatureBuilder &
add_setting_color(RenderSignatureBuilder &signature,
                  ViewportSettingColor color) {
  return signature.add_float(color.r)
      .add_float(color.g)
      .add_float(color.b)
      .add_float(color.a);
}

std::uint64_t ground_grid_presentation_signature(
    const ViewportCameraController &controller, std::uint32_t viewport_width,
    std::uint32_t viewport_height, float grid_world_spacing,
    const ViewportSettings &settings, bool instance_edit_context,
    QVec2 raster_extent_scale) {
  const ViewportCameraPose pose = controller.pose();
  const ViewportCameraState &state = controller.state();
  RenderSignatureBuilder signature;
  signature.add_vec3(pose.eye)
      .add_vec3(pose.target)
      .add_vec3(pose.up)
      .add_value(static_cast<std::uint32_t>(state.projection))
      .add_float(state.orthographic_size)
      .add_float(state.near_plane)
      .add_float(state.far_plane)
      .add_float(settings.float_value(
          state.projection == ViewportProjection::Orthographic
              ? ViewportFloatSettingId::Grid2DIntensity
              : ViewportFloatSettingId::Grid3DIntensity))
      .add_value(viewport_width)
      .add_value(viewport_height)
      .add_vec2(raster_extent_scale)
      .add_float(std::max(grid_world_spacing, 0.0001f))
      .add_bool(instance_edit_context)
      .add_float(instance_edit_context
                     ? std::clamp(settings.float_value(
                                      ViewportFloatSettingId::InstanceEditGridFade),
                                  0.0f, 1.0f)
                     : 0.0f);
  add_setting_color(
      signature, settings.color(ViewportColorSettingId::GridMinor));
  add_setting_color(
      signature, settings.color(ViewportColorSettingId::GridMajor));
  add_setting_color(
      signature, settings.color(ViewportColorSettingId::GridXAxis));
  add_setting_color(
      signature, settings.color(ViewportColorSettingId::GridYAxis));
  add_setting_color(
      signature, settings.color(ViewportColorSettingId::GridZAxis));
  return signature
      .add_float(
          settings.float_value(ViewportFloatSettingId::GridMinorLineSize))
      .add_float(
          settings.float_value(ViewportFloatSettingId::GridMajorLineSize))
      .add_float(
          settings.float_value(ViewportFloatSettingId::GridAxisLineSize))
      .value();
}

filament::math::mat4f ground_grid_transform(const ViewportGridPlacement &grid) {
  const filament::math::float3 origin = quader::to_filament(grid.origin);
  if (grid.rotation == ViewportGridRotation::NegativeHalfPiAroundZ) {
    return filament::math::mat4f::translation(origin) *
           filament::math::mat4f::rotation(
               -kHalfPi, filament::math::float3{0.0f, 0.0f, 1.0f});
  }
  if (grid.rotation == ViewportGridRotation::PositiveHalfPiAroundX) {
    return filament::math::mat4f::translation(origin) *
           filament::math::mat4f::rotation(
               kHalfPi, filament::math::float3{1.0f, 0.0f, 0.0f});
  }
  return filament::math::mat4f::translation(origin);
}

/**
 * Rolls back partially-created ground-grid render resources until the renderer owns them.
 */
struct GroundGridCreationGuard {
    filament::Engine& engine;
    filament::Scene& scene;
    filament::VertexBuffer *vertex_buffer = nullptr;
    filament::IndexBuffer *index_buffer = nullptr;
    filament::MaterialInstance *material_instance = nullptr;
    utils::Entity entity;
    bool scene_added = false;
    bool renderable_created = false;
    bool transform_created = false;
    bool released = false;

    ~GroundGridCreationGuard() {
        if (released) {
            return;
        }
        if (entity) {
          if (scene_added) {
            scene.remove(entity);
          }
          if (renderable_created) {
            engine.getRenderableManager().destroy(entity);
          }
          if (transform_created) {
            engine.getTransformManager().destroy(entity);
          }
        }
        if (material_instance) {
          engine.destroy(material_instance);
        }
        if (vertex_buffer) {
          engine.destroy(vertex_buffer);
        }
        if (index_buffer) {
          engine.destroy(index_buffer);
        }
        if (entity) {
            utils::EntityManager::get().destroy(entity);
            entity.clear();
        }
    }

    void release() {
        released = true;
    }
};

} // namespace

ViewportGridRenderer::~ViewportGridRenderer() {
    shutdown();
}

void ViewportGridRenderer::initialize(filament::Engine &engine,
                                      filament::Scene &scene,
                                      filament::Material &ground_grid_material,
                                      std::uint8_t grid_layer_mask) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  ground_grid_material_ = &ground_grid_material;
  presentation_signature_.reset();
  grid_layer_mask_ =
      grid_layer_mask == 0u ? viewport_grid_layer_mask(0) : grid_layer_mask;
  create_ground_grid();
}

void ViewportGridRenderer::shutdown() {
    presentation_signature_.reset();
    if (!engine_) {
        return;
    }

    destroy_ground_grid();
    engine_ = nullptr;
    scene_ = nullptr;
    ground_grid_material_ = nullptr;
}

void ViewportGridRenderer::update(const ViewportCameraController &controller,
                                  std::uint32_t viewport_width,
                                  std::uint32_t viewport_height,
                                  float grid_world_spacing,
                                  const ViewportSettings &settings,
                                  bool visible, bool instance_edit_context,
                                  QVec2 raster_extent_scale) {
  if (!engine_ || !scene_ || !ground_grid_entity_) {
    return;
  }

  set_ground_grid_visible(visible);
  if (!visible) {
    presentation_signature_.reset();
    return;
  }

  const std::uint64_t presentation_signature =
      ground_grid_presentation_signature(
          controller, std::max(1u, viewport_width),
          std::max(1u, viewport_height), grid_world_spacing, settings,
          instance_edit_context, raster_extent_scale);
  if (presentation_signature_ == presentation_signature) {
    return;
  }

  const ViewportCameraPose pose = controller.pose();
  ViewportGridPlacement grid = ViewportGridPlacementPolicy::placement(
      controller, std::max(1u, viewport_width), std::max(1u, viewport_height));
  if (controller.state().projection == ViewportProjection::Orthographic) {
    // The native crop can reveal pixels outside the visible camera rectangle.
    // Expand the retained plane in both axes to cover rolled grid projections.
    const float extent_scale = std::max(
        {1.0f, raster_extent_scale.x, raster_extent_scale.y});
    grid.plane_size.x *= extent_scale;
    grid.plane_size.y *= extent_scale;
  }
  update_ground_grid_mesh(grid.plane_size.x, grid.plane_size.y);

  auto &transform_manager = engine_->getTransformManager();
  const filament::TransformManager::Instance transform =
      transform_manager.getInstance(ground_grid_entity_);
  if (transform) {
    transform_manager.setTransform(transform, ground_grid_transform(grid));
  }

  if (!ground_grid_material_instance_) {
    return;
  }

  ground_grid_material_instance_->setDepthCulling(
      controller.state().projection != ViewportProjection::Orthographic);
  ground_grid_material_instance_->setParameter(
      "analyticGridEnabled",
      controller.state().projection == ViewportProjection::Perspective ? 1.0f
                                                                       : 0.0f);
  ground_grid_material_instance_->setParameter("depthBiasAmount",
                                               kGroundGridDepthBias);
  ground_grid_material_instance_->setParameter(
      "gridIntensity",
      settings.float_value(
          controller.state().projection == ViewportProjection::Orthographic
              ? ViewportFloatSettingId::Grid2DIntensity
              : ViewportFloatSettingId::Grid3DIntensity));
  const float spacing = std::max(grid_world_spacing, 0.0001f);
  const float parent_spacing = std::max(kParentGridWorldSize, spacing);
  const InstanceEditStyle instance_edit_style =
      InstanceEditStyle::from_settings(settings);
  const auto grid_color = [&](ViewportColorSettingId id) {
    return setting_color(instance_edit_style.resolve_grid_color(
        settings.color(id), instance_edit_context));
  };
  ground_grid_material_instance_->setParameter(
      "gridColor", grid_color(ViewportColorSettingId::GridMinor));
  ground_grid_material_instance_->setParameter(
      "majorGridColor", grid_color(ViewportColorSettingId::GridMajor));
  ground_grid_material_instance_->setParameter(
      "originUColor", grid_color(grid.axes.u_color));
  ground_grid_material_instance_->setParameter(
      "originVColor", grid_color(grid.axes.v_color));
  ground_grid_material_instance_->setParameter(
      "cameraPosition",
      filament::math::float3{pose.eye.x, pose.eye.y, pose.eye.z});
  ground_grid_material_instance_->setParameter(
      "gridUAxis", quader::to_filament(grid.axes.u_axis));
  ground_grid_material_instance_->setParameter(
      "gridVAxis", quader::to_filament(grid.axes.v_axis));
  ground_grid_material_instance_->setParameter("gridSize", spacing);
  ground_grid_material_instance_->setParameter("parentGridSize",
                                               parent_spacing);
  ground_grid_material_instance_->setParameter(
      "lineThickness",
      settings.float_value(ViewportFloatSettingId::GridMinorLineSize));
  ground_grid_material_instance_->setParameter(
      "majorLineThickness",
      settings.float_value(ViewportFloatSettingId::GridMajorLineSize));
  ground_grid_material_instance_->setParameter(
      "axisLineThickness",
      settings.float_value(ViewportFloatSettingId::GridAxisLineSize));
  ground_grid_material_instance_->setParameter(
      "analyticLineThickness",
      filament::math::float3{
          settings.float_value(ViewportFloatSettingId::GridMinorLineSize) /
              kDefaultViewportGridMinorLineSize,
          settings.float_value(ViewportFloatSettingId::GridMajorLineSize) /
              kDefaultViewportGridMajorLineSize,
          settings.float_value(ViewportFloatSettingId::GridAxisLineSize) /
              kDefaultViewportGridAxisLineSize});
  ground_grid_material_instance_->setParameter("fadeStart",
                                               kGroundGridFadeStart);
  ground_grid_material_instance_->setParameter("fadeEnd", kGroundGridFadeEnd);
  ground_grid_material_instance_->setParameter(
      "planeHalfSize", std::max(grid.plane_size.x, grid.plane_size.y) * 0.5f);
  ground_grid_material_instance_->setParameter(
      "cameraSize", std::max(0.0001f, controller.state().orthographic_size));
  ground_grid_material_instance_->setParameter(
      "viewportHeight", static_cast<float>(std::max(1u, viewport_height)));
  ground_grid_material_instance_->setParameter("visibleGridSize", spacing);
  ground_grid_material_instance_->setParameter("minGridWorldSpacing", 0.001f);
  presentation_signature_ = presentation_signature;
}

void ViewportGridRenderer::create_ground_grid() {
  presentation_signature_.reset();
  if (!engine_ || !scene_ || !ground_grid_material_) {
    return;
  }

  const float half = kViewportGridDefaultPlaneSize * 0.5f;
  const std::vector<GroundGridVertex> vertices = {
      {{-half, 0.0f, -half}},
      {{half, 0.0f, -half}},
      {{half, 0.0f, half}},
      {{-half, 0.0f, half}},
  };
  const std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};

  GroundGridCreationGuard creation_guard{*engine_, *scene_};
  creation_guard.vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(static_cast<std::uint32_t>(vertices.size()))
          .bufferCount(1)
          .attribute(
              filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3,
              offsetof(GroundGridVertex, position), sizeof(GroundGridVertex))
          .build(*engine_);
  if (creation_guard.vertex_buffer == nullptr) {
    return;
  }
  creation_guard.vertex_buffer->setBufferAt(
      *engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));

  creation_guard.index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(static_cast<std::uint32_t>(indices.size()))
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine_);
  if (creation_guard.index_buffer == nullptr) {
    return;
  }
  creation_guard.index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::vector(indices));

  creation_guard.material_instance = ground_grid_material_->createInstance();
  if (creation_guard.material_instance == nullptr) {
    return;
  }
  creation_guard.entity = utils::EntityManager::get().create();
  filament::RenderableManager::Builder(1)
      .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                creation_guard.vertex_buffer, creation_guard.index_buffer)
      .material(0, creation_guard.material_instance)
      .priority(kGroundGridRenderablePriority)
      .blendOrder(0, kGroundGridBlendOrder)
      .globalBlendOrderEnabled(0, true)
      .layerMask(kViewportAllLayerMask, grid_layer_mask_)
      .boundingBox(ground_grid_bounds(kViewportGridDefaultPlaneSize,
                                     kViewportGridDefaultPlaneSize))
      .castShadows(false)
      .receiveShadows(false)
      .build(*engine_, creation_guard.entity);
  creation_guard.renderable_created = true;

  scene_->addEntity(creation_guard.entity);
  creation_guard.scene_added = true;
  engine_->getTransformManager().create(creation_guard.entity);
  creation_guard.transform_created = true;
  ground_grid_vertex_buffer_ = creation_guard.vertex_buffer;
  ground_grid_index_buffer_ = creation_guard.index_buffer;
  ground_grid_material_instance_ = creation_guard.material_instance;
  ground_grid_entity_ = creation_guard.entity;
  ground_grid_mesh_width_ = kViewportGridDefaultPlaneSize;
  ground_grid_mesh_height_ = kViewportGridDefaultPlaneSize;
  visible_ = true;
  creation_guard.release();
}

void ViewportGridRenderer::set_ground_grid_visible(bool visible) {
  if (visible_ == visible) {
    return;
  }
  visible_ = visible;
  if (!engine_ || !ground_grid_entity_) {
    return;
  }

  auto &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance renderable =
      renderables.getInstance(ground_grid_entity_);
  if (renderable) {
    renderables.setLayerMask(renderable, kViewportAllLayerMask,
                             visible ? grid_layer_mask_ : 0u);
  }
}

void ViewportGridRenderer::update_ground_grid_mesh(float width, float height) {
  if (!engine_ || !ground_grid_vertex_buffer_) {
    return;
  }

  width = std::max(0.0001f, width);
  height = std::max(0.0001f, height);
  if (std::fabs(ground_grid_mesh_width_ - width) <= 0.0001f &&
      std::fabs(ground_grid_mesh_height_ - height) <= 0.0001f) {
    return;
  }

  const float half_x = width * 0.5f;
  const float half_z = height * 0.5f;
  const std::vector<GroundGridVertex> vertices = {
      {{-half_x, 0.0f, -half_z}},
      {{half_x, 0.0f, -half_z}},
      {{half_x, 0.0f, half_z}},
      {{-half_x, 0.0f, half_z}},
  };
  ground_grid_vertex_buffer_->setBufferAt(
      *engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));
  auto &renderables = engine_->getRenderableManager();
  const auto renderable = renderables.getInstance(ground_grid_entity_);
  if (renderable) {
    renderables.setAxisAlignedBoundingBox(renderable,
                                         ground_grid_bounds(width, height));
  }
  ground_grid_mesh_width_ = width;
  ground_grid_mesh_height_ = height;
}

void ViewportGridRenderer::destroy_ground_grid() {
  presentation_signature_.reset();
  if (!engine_) {
    return;
  }
  if (ground_grid_entity_) {
    if (scene_) {
      scene_->remove(ground_grid_entity_);
    }
    engine_->getRenderableManager().destroy(ground_grid_entity_);
    engine_->getTransformManager().destroy(ground_grid_entity_);
  }
  if (ground_grid_material_instance_) {
    engine_->destroy(ground_grid_material_instance_);
    ground_grid_material_instance_ = nullptr;
  }
  if (ground_grid_vertex_buffer_) {
    engine_->destroy(ground_grid_vertex_buffer_);
    ground_grid_vertex_buffer_ = nullptr;
  }
  ground_grid_mesh_width_ = 0.0f;
  ground_grid_mesh_height_ = 0.0f;
  if (ground_grid_index_buffer_) {
    engine_->destroy(ground_grid_index_buffer_);
    ground_grid_index_buffer_ = nullptr;
  }
  if (ground_grid_entity_) {
    utils::EntityManager::get().destroy(ground_grid_entity_);
    ground_grid_entity_.clear();
  }
}

} // namespace quader
