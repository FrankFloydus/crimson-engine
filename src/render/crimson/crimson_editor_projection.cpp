////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_editor_projection.hpp"

#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_line_mesh_builder.hpp"
#include "render/overlay/overlay_icon_image.hpp"
#include "render/overlay/collision_shape_overlay_planner.hpp"
#include "assets/mesh_collision_geometry_payload.hpp"
#include "render/scene_render_extraction.hpp"
#include "core/color_space.hpp"
#include "math/glm_bridge.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <span>
#include <utility>
#include <vector>
#include <unordered_map>

namespace quader {
namespace {

using crimson::shared::EditorBatch;
using crimson::shared::EditorGeometry;
using crimson::shared::EditorPosition;
using crimson::shared::EditorPrimitive;
using crimson::shared::EditorStencil;
using crimson::shared::EditorStencilOperation;
using crimson::shared::EditorStencilTest;
using crimson::shared::EditorVertex;

crimson::shared::Float4 neutral(OverlayFloat4 value) { return {value.x,value.y,value.z,value.w}; }
crimson::shared::Float4 neutral(OverlayFloat3 value) { return {value.x,value.y,value.z,1.0f}; }

template<class T>
crimson::shared::PinnedStream<T> pin_stream(std::vector<T> values,
    const crimson::shared::PinnedStream<T> &previous, std::uint64_t &revision,
    std::uint64_t &counter) {
  static_assert(std::is_trivially_copyable_v<T>);
  if (values.size() == previous.values.size() &&
      (values.empty() || std::memcmp(values.data(),previous.values.data(),values.size()*sizeof(T)) == 0)) return previous;
  auto owner = std::make_shared<const std::vector<T>>(std::move(values));
  ++counter;
  return {*owner,owner,++revision};
}

EditorStencil stencil_for(const OverlayRenderState &state) {
  if (state.face_fill_class != OverlayFaceFillClass::None) {
    const auto priority = static_cast<std::uint8_t>(state.face_fill_class);
    const bool mask = state.face_fill_pass == OverlayFaceFillPass::Mask;
    return {mask ? EditorStencilTest::ReferenceGreaterOrEqual : EditorStencilTest::Equal,
        mask ? EditorStencilOperation::Replace : EditorStencilOperation::Invert,
        priority,mask ? kOverlayFaceFillPriorityMask : kOverlayFaceFillStencilMask,
        mask ? kOverlayFaceFillPriorityMask : kOverlayFaceFillConsumedMask};
  }
  if (state.silhouette_pass == OverlaySilhouettePass::Visible)
    return {EditorStencilTest::Always,EditorStencilOperation::Replace,
        kOverlaySilhouetteVisibleStencilMask,0u,kOverlaySilhouetteVisibleStencilMask};
  if (state.silhouette_pass == OverlaySilhouettePass::Occluded)
    return {EditorStencilTest::Equal,EditorStencilOperation::Keep,0u,kOverlaySilhouetteVisibleStencilMask,0u};
  if (state.suppressed_by_active_wire)
    return {EditorStencilTest::Equal,EditorStencilOperation::Keep,0u,kOverlayActiveWireStencilMask,0u};
  return {};
}

EditorBatch presentation(const OverlayRenderState &state) {
  EditorBatch result;
  result.depth_write = state.depth_write;
  result.color_write = state.color_write;
  result.cull_back = state.culling_mode == OverlayCullingMode::Back;
  result.polygon_offset = state.polygon_offset_scale==-1.0f && state.polygon_offset_constant==-1.0f;
  result.transparency = state.transparency_mode==OverlayTransparencyMode::TwoPassesTwoSides ? crimson::shared::EditorTransparency::TwoSides
      : state.transparency_mode==OverlayTransparencyMode::TwoPassesOneSide ? crimson::shared::EditorTransparency::OneSide : crimson::shared::EditorTransparency::Default;
  result.order = state.blend_order;
  result.stencil = stencil_for(state);
  if (state.depth_culling) {
    switch (state.depth_func) {
    case OverlayDepthFunc::Always: break;
    case OverlayDepthFunc::Equal: result.depth = crimson::shared::EditorDepth::Equal; break;
    case OverlayDepthFunc::Less: result.depth = crimson::shared::EditorDepth::Less; break;
    case OverlayDepthFunc::GreaterOrEqual: result.depth = crimson::shared::EditorDepth::GreaterOrEqual; break;
    }
  }
  return result;
}

struct PackedBatch final {
  EditorBatch batch;
  std::vector<std::size_t> point_indices;
};

template<class Vertex>
void pack_geometry(PackedBatch &output, const OverlayMesh<Vertex> &mesh,
    const PackedBatch *previous, std::uint64_t &revision,
    CrimsonEditorProjectionCounters &work) {
  std::vector<EditorPosition> positions;
  std::vector<EditorVertex> attributes;
  positions.reserve(mesh.vertices.size());
  attributes.reserve(mesh.vertices.size());
  for (const auto &vertex : mesh.vertices) {
    if constexpr (std::is_same_v<Vertex,OverlayFaceFillVertex>) {
      positions.push_back({neutral(vertex.position),{},{},{}});
      attributes.push_back({{},{vertex.uv.x,vertex.uv.y},vertex.color});
    } else if constexpr (std::is_same_v<Vertex,OverlayLineVertex>) {
      positions.push_back({neutral(vertex.start),neutral(vertex.end),neutral(vertex.support0),neutral(vertex.support1)});
      attributes.push_back({neutral(vertex.control),{},vertex.color});
    } else if constexpr (std::is_same_v<Vertex,OverlayPointVertex>) {
      positions.push_back({neutral(vertex.center),{},{},{}});
      attributes.push_back({{},{vertex.uv.x,vertex.uv.y},vertex.color});
    } else if constexpr (std::is_same_v<Vertex,OverlayIconVertex>) {
      const auto p=vertex.position;
      // Quader's device-domain plan is GL [-1,+1] forward depth. The shared
      // editor API consumes the same pixels in native [0,1] reversed depth.
      positions.push_back({{p.x,p.y,(p.w-p.z)*.5f,p.w},{},{},{}});
      attributes.push_back({{},{vertex.uv.x,vertex.uv.y},vertex.color});
    }
  }
  const EditorGeometry old = previous ? previous->batch.geometry : EditorGeometry{};
  output.batch.geometry.positions = pin_stream(std::move(positions),old.positions,revision,work.position_packs);
  output.batch.geometry.vertices = pin_stream(std::move(attributes),old.vertices,revision,work.attribute_packs);
  output.batch.geometry.indices = pin_stream(mesh.indices,old.indices,revision,work.index_packs);
  if constexpr (std::is_same_v<Vertex,OverlayPointVertex>) {
    output.batch.geometry.visibility = pin_stream(std::vector<float>(mesh.vertices.size(),1.0f),
        old.visibility,revision,work.visibility_packs);
  }
}

} // namespace

struct CrimsonEditorProjection::Impl final {
  struct Layer final {
    std::vector<PackedBatch> batches;
    std::uint64_t signature = 0u;
    bool valid = false;
  };
  struct Stream final {
    std::shared_ptr<const ViewportOverlayFrame> source;
    std::shared_ptr<const ViewportOverlayFrame> effective;
    std::uint64_t preview_epoch = UINT64_MAX;
    OverlayFrameSignatures signatures;
    OverlayWireSurfaceSupportCache supports;
    OverlayDepthStampPointVisibilityFilter point_visibility;
    std::array<Layer,kViewportOverlayLayerCount> layers;
  };
  struct Pane final {
    Stream persistent;
    Stream dynamic;
    Stream wireframe;
    std::shared_ptr<const RenderOverlaySceneWireframeSnapshot> wireframe_source;
    std::vector<PackedBatch> shaded_wires;
    ViewportOverlayCamera camera;
    std::uint8_t mask = 0u;
    std::uint64_t camera_signature = 0u;
    bool dirty = true;
    std::shared_ptr<const crimson::shared::EditorFrame> frame;
    std::vector<PackedBatch> ordinary, active;
    std::vector<RenderOverlayMeshSnapshot> selection_sources;
    RenderSourceStamp selected_stamp, hierarchy_stamp;
    const void *selected_identity = nullptr;
    const void *hierarchy_identity = nullptr;
    const void *selected_meshes = nullptr;
    const void *hierarchy_meshes = nullptr;
    std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview;
    ViewportPersistentOverlayStyleSnapshot style;
    crimson::shared::Matrix4 clip;
    crimson::shared::Rect crop;
    crimson::shared::Extent backing;
    bool selected_visible = false;
    bool selection_valid = false;
    std::shared_ptr<const RenderOverlaySelectedObjectSnapshot> selected_base;
    std::shared_ptr<const RenderOverlaySelectionSnapshot> hierarchy_base;
    const RenderOverlaySelectionSnapshot* filtered_scene_source=nullptr;
    RenderSourceStamp filtered_scene_stamp;
    std::uint64_t filtered_scene_epoch=UINT64_MAX;
    std::shared_ptr<const RenderOverlaySelectionSnapshot> filtered_scene;
    std::shared_ptr<const crimson::shared::EditorGrid> grid;
    bool overlays_visible = true;
    bool scene_visible = true;
    std::uint64_t scene_generation = 0u;
  };
  std::array<Pane,ViewportOverlaySubmissionCoordinator::kMaxViewportSlots> panes;
  ViewportOverlaySubmissionCoordinator coordinator;
  CrimsonEditorProjectionCounters work;
  std::uint64_t revision = 0u;
  std::shared_ptr<const RenderOverlaySceneWireframeSnapshot> wireframe;
  std::shared_ptr<const RenderToolPreviewSceneSnapshot> wireframe_preview;
  const void *wireframe_identity = nullptr;
  std::shared_ptr<const RenderOverlaySceneWireframeSnapshot> wireframe_base;
  std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview;
  std::uint64_t preview_epoch = 0u;
  std::uint64_t suppression_epoch = 0u;
  ViewportOverlayFrame preview_overlay;
  std::unordered_map<int,std::unordered_map<int,QVec3>> preview_positions;
  struct CollisionGeometry final {
    PackedBatch fill, wire;
  };
  CollisionShapeOverlayPlanner collision_planner;
  std::shared_ptr<const RenderOverlayCollisionSnapshot> collision_base;
  RenderOverlaySelectedObjectSnapshot collision_presentation;
  std::shared_ptr<const RenderToolPreviewSceneSnapshot> collision_preview;
  std::unordered_map<std::uint64_t,CollisionGeometry> collision_geometry;
  bool collision_visible = false;
  ViewportSettingColor collision_color;

  void update(Stream &stream, ViewportOverlayFrame frame) {
    stream.source = std::make_shared<const ViewportOverlayFrame>(std::move(frame));
    stream.effective = stream.source;
    stream.preview_epoch = UINT64_MAX;
    stream.signatures = OverlayRenderableBuilder::frame_signatures(*stream.source);
    stream.supports.prepare(*stream.source,stream.signatures);
  }

  void build(Stream &stream, const ViewportOverlayCamera &camera, std::uint16_t bias) {
    if (!stream.source) return;
    if (stream.preview_epoch!=preview_epoch) {
      if (preview_positions.empty()) stream.effective=stream.source;
      else {
        auto projected=std::make_shared<ViewportOverlayFrame>(*stream.source);
        const auto position=[&](QVec3 &value,int object,int vertex) {
          const auto source=preview_positions.find(object);
          if (source==preview_positions.end()) return;
          const auto point=source->second.find(vertex);
          if (point!=source->second.end()) value=point->second;
        };
        for (auto &line : projected->lines) {
          position(line.start,line.element.object_id,line.element.edge_a);
          position(line.end,line.element.object_id,line.element.edge_b);
          for (auto &support : line.surface_support) position(support.position,line.element.object_id,support.vertex_id);
        }
        for (auto &triangle : projected->triangles) {
          position(triangle.a,triangle.element.object_id,triangle.element.vertex_id);
          position(triangle.b,triangle.element.object_id,triangle.element.edge_a);
          position(triangle.c,triangle.element.object_id,triangle.element.edge_b);
        }
        for (auto &point : projected->points) position(point.position,point.element.object_id,point.element.vertex_id);
        stream.effective=std::move(projected);
      }
      stream.signatures=OverlayRenderableBuilder::frame_signatures(*stream.effective);
      stream.supports.prepare(*stream.effective,stream.signatures);
      stream.preview_epoch=preview_epoch;
    }
    for (const auto layer : kViewportOverlayDrawOrder) {
      const auto index = static_cast<std::size_t>(layer);
      auto &cache = stream.layers[index];
      const auto signature = OverlayRenderableBuilder::layer_build_signature(stream.signatures,camera,layer,true);
      if (cache.valid && cache.signature == signature) continue;
      auto plan = OverlayRenderableBuilder::build_layer(*stream.effective,camera,layer,
          &stream.signatures,bias,nullptr,true,true,&stream.supports);
      ++work.layer_builds;
      std::vector<PackedBatch> batches;
      const auto append = [&](const auto &mesh, EditorBatch batch,
                              std::vector<std::size_t> points = {}) {
        if (mesh.empty()) return;
        PackedBatch candidate{std::move(batch),std::move(points)};
        const PackedBatch *previous = batches.size() < cache.batches.size() ? &cache.batches[batches.size()] : nullptr;
        pack_geometry(candidate,mesh,previous,revision,work);
        batches.push_back(std::move(candidate));
      };
      for (const auto &face : plan.face_fill_batches) append(face.mesh,presentation(face.state));
      for (const auto &line : plan.line_batches) {
        auto batch = presentation(line.state);
        batch.primitive = EditorPrimitive::Ribbon;
        batch.width_pixels = line.width_pixels;
        batch.near_plane = line.near_plane;
        batch.far_plane = camera.far_plane;
        batch.depth_bias = line.depth_bias;
        append(line.mesh,batch);
        if (line.occluded_state) {
          batch = presentation(*line.occluded_state);
          batch.primitive = EditorPrimitive::Ribbon;
          batch.width_pixels = line.width_pixels;
          batch.near_plane = line.near_plane;
          batch.far_plane = camera.far_plane;
          batch.depth_bias = line.depth_bias;
          batch.alpha = line.occluded_alpha_multiplier;
          if (line.occlusion_style == ViewportOverlayLineOcclusionStyle::Dashed)
            batch.dash_period_pixels = line.occluded_dash_period_pixels;
          append(line.mesh,batch);
        }
      }
      for (const auto &wire : plan.edit_wire_batches) {
        auto batch = presentation(wire.state);
        batch.primitive = EditorPrimitive::EditRibbon;
        batch.width_pixels = wire.width_pixels;
        batch.near_plane = wire.near_plane;
        batch.far_plane = wire.far_plane;
        batch.surface_depth = true;
        if (!wire.active_selection_composition) { append(wire.mesh,batch); continue; }
        batch.ribbon_class = 1u;
        batch.stencil = {EditorStencilTest::Equal,EditorStencilOperation::Keep,0u,kOverlayActiveWireStencilMask,0u};
        append(wire.mesh,batch);
        if (!wire.active_selection_mask) continue;
        batch.ribbon_class = 2u;
        if (wire.supplemental_active_selection)
          batch.stencil = {EditorStencilTest::Equal,EditorStencilOperation::Invert,0u,kOverlayActiveWireConsumedStencilMask,kOverlayActiveWireConsumedStencilMask};
        else batch.stencil = {EditorStencilTest::Always,EditorStencilOperation::Replace,kOverlayActiveWireConsumedStencilMask,0u,kOverlayActiveWireConsumedStencilMask};
        append(wire.mesh,batch);
        batch.color_write = false;
        batch.order = kOverlayActiveWireMaskBlendOrder;
        batch.stencil = {EditorStencilTest::Always,EditorStencilOperation::Replace,kOverlayActiveWireStencilMask,0u,kOverlayActiveWireStencilMask};
        append(wire.mesh,batch);
      }
      for (const auto &point : plan.point_batches) {
        auto batch = presentation(point.state);
        batch.primitive = EditorPrimitive::Point;
        batch.width_pixels = point.size_pixels;
        batch.inner_radius = point.inner_radius;
        batch.depth_bias = point.depth_bias;
        batch.near_plane = point.near_plane;
        batch.far_plane = camera.far_plane;
        append(point.mesh,batch,point.visibility_point_indices);
      }
      for(const auto& icon:plan.icon_batches) {
        const auto& image=overlay_icon_image(icon.icon);
        if(!image.rgba8||image.rgba8->empty())continue;
        auto batch=presentation(icon.state);
        batch.primitive=EditorPrimitive::Icon;
        batch.width_pixels=icon.size_pixels;
        const auto bytes=std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(image.rgba8->data()),image.rgba8->size());
        batch.image={0x515549434f4e0000ull+std::uint64_t(icon.icon),1,{image.width,image.height},true,{bytes,image.rgba8,1}};
        append(icon.mesh,batch);
      }
      cache.batches = std::move(batches);
      cache.signature = signature;
      cache.valid = true;
    }
    if (stream.signatures.has_source_wire_depth_stamp && !stream.effective->points.empty()) {
      const auto visible = stream.point_visibility.evaluate(*stream.effective,stream.signatures,camera);
      for (auto &layer : stream.layers) for (auto &packed : layer.batches) {
        if (packed.point_indices.empty()) continue;
        std::vector<float> flags;
        flags.reserve(packed.point_indices.size()*4u);
        for (const auto index : packed.point_indices)
          flags.insert(flags.end(),4u,index < visible.size() && visible[index] ? 1.0f : 0.0f);
        packed.batch.geometry.visibility = pin_stream(std::move(flags),packed.batch.geometry.visibility,revision,work.visibility_packs);
      }
    }
  }
};

CrimsonEditorProjection::CrimsonEditorProjection() : impl_(std::make_unique<Impl>()) {}
CrimsonEditorProjection::~CrimsonEditorProjection() = default;

ViewportOverlaySubmissionResult CrimsonEditorProjection::submit(
    ViewportOverlaySubmissionRequest request, const ViewportSettings &settings) {
  request.gpu_mesh_outlines = true;
  request.tool_preview_overlay_identity = reinterpret_cast<std::uintptr_t>(impl_.get());
  request.tool_preview_overlay_revision = impl_->preview_epoch;
  request.tool_preview_overlay_frame=&impl_->preview_overlay;
  if(impl_->preview){
    const auto& preview=*impl_->preview;
    request.selection_dimension_preview_curves=preview.curves;
    request.array_dimension_preview_lights=preview.lights;
    request.selection_dimension_preview_groups=preview.group_markers;
    request.selection_dimension_preview_suppression=&preview.suppression;
    if(request.scene_overlay_snapshot&&!preview.suppression.empty()&&request.viewport_index<impl_->panes.size()){
      auto& pane=impl_->panes[request.viewport_index];const auto* source=request.scene_overlay_snapshot;
      if(!request.selection_dimension_scene_snapshot)request.selection_dimension_scene_snapshot=source;
      const RenderSourceStamp stamp=*source;
      if(pane.filtered_scene_source!=source||pane.filtered_scene_stamp!=stamp||pane.filtered_scene_epoch!=impl_->suppression_epoch){
        auto filtered=std::make_shared<RenderOverlaySelectionSnapshot>(*source);
        const auto suppressed=[&](EntityId entity,EntityId occurrence=kInvalidEntityId,EntityId definition=kInvalidEntityId,EntityId member=kInvalidEntityId){for(const auto id:{entity,occurrence,definition,member})if(id!=kInvalidEntityId&&std::ranges::find(preview.suppression.entities,id)!=preview.suppression.entities.end())return true;return std::ranges::any_of(preview.suppression.occurrences,[&](const auto& entry){return entry.occurrence==occurrence&&(entry.source_entity==definition||entry.source_entity==member);});};
        std::erase_if(filtered->hierarchy_selection_wires,[&](const auto& mesh){return suppressed(mesh.entity,mesh.occurrence,mesh.definition,mesh.member);});
        std::erase_if(filtered->selected_mesh_bounds,[&](const auto& bounds){return suppressed(bounds.entity);});
        std::erase_if(filtered->group_bounds,[&](const auto& bounds){return suppressed(bounds.entity);});
        std::erase_if(filtered->selected_lights,[&](const auto& light){return suppressed(light.entity);});
        std::erase_if(filtered->point_light_gizmos,[&](const auto& light){return suppressed(light.entity);});
        std::erase_if(filtered->curves,[&](const auto& curve){return suppressed(curve.entity);});
        pane.filtered_scene=std::move(filtered);pane.filtered_scene_source=source;pane.filtered_scene_stamp=stamp;pane.filtered_scene_epoch=impl_->suppression_epoch;
      }
      request.scene_overlay_snapshot=pane.filtered_scene.get();
    }
  }
  return impl_->coordinator.submit(std::move(request),settings,*this);
}
void CrimsonEditorProjection::set_active_viewports(std::uint8_t mask) {
  impl_->coordinator.set_active_viewports(mask);
}
void CrimsonEditorProjection::set_preview(std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview) {
  if (impl_->preview==preview) return;
  const RenderToolPreviewSuppressionSnapshot empty;
  const auto& before=impl_->preview?impl_->preview->suppression:empty;
  const auto& after=preview?preview->suppression:empty;
  if(before.entities!=after.entities||before.occurrences!=after.occurrences){++impl_->suppression_epoch;impl_->coordinator.invalidate_all();}
  impl_->preview=std::move(preview);
  impl_->preview_overlay=impl_->preview?build_render_tool_preview_overlay_frame(*impl_->preview):ViewportOverlayFrame{};
  ++impl_->preview_epoch;
  impl_->preview_positions.clear();
  if (impl_->preview) {
    const auto &records=impl_->preview->accepted_overlay_coordinates.empty() ? impl_->preview->meshes : impl_->preview->accepted_overlay_coordinates;
    for (const auto &record : records) {
      if (record.source_object_id<0 || !record.dynamic_positions || !record.dynamic_position_payload) continue;
      const auto &vertices=record.mesh.geometry().vertices;
      if (vertices.size()!=record.dynamic_position_payload->size()) continue;
      auto &positions=impl_->preview_positions[record.source_object_id];
      positions.reserve(vertices.size());
      for (std::size_t index=0u; index<vertices.size(); ++index) {
        const auto source=vertices[index].source_vertex_id;
        if (source>static_cast<std::uint32_t>(INT32_MAX)) continue;
        positions.try_emplace(static_cast<int>(source),transform_point(record.transform,(*record.dynamic_position_payload)[index]));
      }
    }
  }
  for (auto &pane : impl_->panes) {
    pane.dirty=true;
    if (pane.selection_valid) {
      const auto selected=pane.selected_base;
      const auto hierarchy=pane.hierarchy_base;
      (void)update_selected(pane.mask,selected.get(),hierarchy.get(),pane.style,pane.selected_visible,impl_->preview);
    }
  }
  const auto wireframe=impl_->wireframe_base;
  if (wireframe) sync_wireframe(*wireframe,impl_->preview);
  const auto collision=impl_->collision_base;
  if (collision) sync_collision(*collision,impl_->collision_presentation,impl_->preview);
}
void CrimsonEditorProjection::sync_collision(const RenderOverlayCollisionSnapshot &snapshot,
    const RenderOverlaySelectedObjectSnapshot &presentation,
    std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview) {
  const auto same_color=[](ViewportSettingColor left,ViewportSettingColor right) {
    return left.r==right.r && left.g==right.g && left.b==right.b && left.a==right.a;
  };
  const bool source_matches=impl_->collision_base &&
      static_cast<const RenderSourceStamp&>(*impl_->collision_base)==static_cast<const RenderSourceStamp&>(snapshot) &&
      impl_->collision_base->visible==snapshot.visible && same_color(impl_->collision_base->color,snapshot.color) &&
      impl_->collision_preview==preview;
  const bool raster_matches=viewport_persistent_overlay_raster_groups_match(
      impl_->collision_presentation.raster_groups,presentation.raster_groups) &&
      impl_->collision_presentation.style.raster_scale==presentation.style.raster_scale;
  if (source_matches && raster_matches) return;
  impl_->collision_base=std::make_shared<const RenderOverlayCollisionSnapshot>(snapshot);
  impl_->collision_presentation.style=presentation.style;
  impl_->collision_presentation.raster_groups=presentation.raster_groups;
  impl_->collision_preview=std::move(preview);
  impl_->collision_visible=snapshot.visible;
  impl_->collision_color=snapshot.color;
  const auto filtered=impl_->collision_preview
      ? filter_render_overlay_collision_snapshot(snapshot,impl_->collision_preview->suppression) : snapshot;
  (void)impl_->collision_planner.prepare(filtered,1u);
  if (snapshot.visible) {
    for (const auto &chunk : impl_->collision_planner.plan().chunks) {
      const auto token=chunk.key.geometry_payload;
      if (impl_->collision_geometry.contains(token)) continue;
      auto &buffers=impl_->collision_geometry[token];
      const auto payload=impl_->collision_planner.geometry(token);
      const auto wires=impl_->collision_planner.wire_geometry(token);
      if (!payload || !wires) continue;
      // Native collision tint and opacity are uniforms. Retain neutral white
      // attributes along with the immutable triangle and edge geometry.
      std::vector<crimson::shared::EditorPosition> positions;
      std::vector<crimson::shared::EditorVertex> attributes;
      std::vector<std::uint32_t> indices;
      positions.reserve(payload->faces.size());
      attributes.reserve(payload->faces.size());
      indices.reserve(payload->faces.size());
      for (const auto position : payload->faces) {
        positions.push_back({{position.x,position.y,position.z,1.0f},{},{},{}});
        attributes.push_back({{},{},0xffffffffu});
        indices.push_back(static_cast<std::uint32_t>(indices.size()));
      }
      auto &fill=buffers.fill.batch.geometry;
      fill.positions=pin_stream(std::move(positions),fill.positions,impl_->revision,impl_->work.position_packs);
      fill.vertices=pin_stream(std::move(attributes),fill.vertices,impl_->revision,impl_->work.attribute_packs);
      fill.indices=pin_stream(std::move(indices),fill.indices,impl_->revision,impl_->work.index_packs);
      OverlayLineMesh mesh;
      for (const auto &segment : wires->segments) {
        ViewportOverlayLine line;
        line.start=segment.start; line.end=segment.end; line.color=rgba8(255u,255u,255u,255u);
        OverlayLineMeshBuilder::append_line_quad(mesh,line);
      }
      const auto previous=buffers.wire;
      pack_geometry(buffers.wire,mesh,&previous,impl_->revision,impl_->work);
    }
    std::erase_if(impl_->collision_geometry,[&](const auto &entry) {
      return !impl_->collision_planner.geometry(entry.first);
    });
  }
  for (auto &pane : impl_->panes) pane.dirty=true;
}
void CrimsonEditorProjection::sync_wireframe(const RenderOverlaySceneWireframeSnapshot &snapshot,
    std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview) {
  const auto &previous=impl_->wireframe;
  if (previous && impl_->wireframe_identity==&snapshot && static_cast<const RenderSourceStamp&>(*previous)==static_cast<const RenderSourceStamp&>(snapshot)
      && previous->layer_mask==snapshot.layer_mask && previous->wireframe_on_shaded_layer_mask==snapshot.wireframe_on_shaded_layer_mask
      && viewport_persistent_overlay_wire_styles_match(previous->style,snapshot.style)
      && viewport_persistent_overlay_raster_groups_match(previous->raster_groups,snapshot.raster_groups)
      && previous->selected_mesh_wire_visible==snapshot.selected_mesh_wire_visible
      && previous->selected_mesh_outline_only==snapshot.selected_mesh_outline_only && impl_->wireframe_preview==preview) return;
  auto candidate=std::make_shared<RenderOverlaySceneWireframeSnapshot>(snapshot);
  impl_->wireframe_base=std::make_shared<const RenderOverlaySceneWireframeSnapshot>(snapshot);
  if (preview && !preview->suppression.empty()) {
    std::erase_if(candidate->meshes,[&](const auto &mesh) {
      for (const auto entity : {mesh.entity,mesh.occurrence,mesh.definition,mesh.member})
        if (entity!=kInvalidEntityId && std::ranges::find(preview->suppression.entities,entity)!=preview->suppression.entities.end()) return true;
      return std::ranges::any_of(preview->suppression.occurrences,[&](const auto &record) {
        return record.occurrence==mesh.occurrence && (record.source_entity==mesh.member || record.source_entity==mesh.definition);
      });
    });
  }
  if (preview && (snapshot.layer_mask || snapshot.wireframe_on_shaded_layer_mask)) {
    auto wires=build_render_tool_preview_wireframe_snapshot(preview->meshes,snapshot.wireframe_on_shaded_layer_mask!=0u,snapshot.layer_mask!=0u);
    candidate->meshes.insert(candidate->meshes.end(),wires.meshes.begin(),wires.meshes.end());
  }
  impl_->wireframe=std::move(candidate);
  impl_->wireframe_preview=std::move(preview);
  impl_->wireframe_identity=&snapshot;
  for (auto &pane : impl_->panes) pane.dirty=true;
}
bool CrimsonEditorProjection::update_selected(std::uint8_t mask,
    const RenderOverlaySelectedObjectSnapshot *snapshot,
    const RenderOverlaySelectionSnapshot *hierarchy,
    const ViewportPersistentOverlayStyleSnapshot &style, bool visible,
    std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview) {
  auto found=std::find_if(impl_->panes.begin(),impl_->panes.end(),[mask](const auto &pane) { return pane.mask==mask; });
  if (found==impl_->panes.end()) found=std::find_if(impl_->panes.begin(),impl_->panes.end(),[](const auto &pane) { return pane.mask==0u; });
  if (found==impl_->panes.end()) return false;
  auto &pane=*found;
  pane.mask=mask;
  visible=visible && snapshot && snapshot->visible;
  const RenderSourceStamp selected_stamp=snapshot ? static_cast<const RenderSourceStamp&>(*snapshot) : RenderSourceStamp{};
  const RenderSourceStamp hierarchy_stamp=hierarchy ? static_cast<const RenderSourceStamp&>(*hierarchy) : RenderSourceStamp{};
  const void *selected_meshes=snapshot ? snapshot->meshes.data() : nullptr;
  const void *hierarchy_meshes=hierarchy ? hierarchy->hierarchy_selection_wires.data() : nullptr;
  const bool same_source=pane.selection_valid && pane.selected_identity==snapshot && pane.hierarchy_identity==hierarchy
      && pane.selected_stamp==selected_stamp && pane.hierarchy_stamp==hierarchy_stamp
      && pane.selected_meshes==selected_meshes && pane.hierarchy_meshes==hierarchy_meshes
      && pane.preview==preview && pane.selected_visible==visible;
  const bool same_style=pane.selection_valid && viewport_persistent_overlay_wire_styles_match(pane.style,style)
      && pane.style.face_signature==style.face_signature;
  if (same_source && same_style) return false;
  pane.style=style;
  pane.dirty=true;
  if (!same_source) {
    pane.selected_base=snapshot ? std::make_shared<const RenderOverlaySelectedObjectSnapshot>(*snapshot) : nullptr;
    pane.hierarchy_base=hierarchy ? std::make_shared<const RenderOverlaySelectionSnapshot>(*hierarchy) : nullptr;
    std::vector<RenderOverlayMeshSnapshot> sources;
    if (visible) {
      for (const auto &mesh : snapshot->meshes) if (mesh.selected) sources.push_back(mesh);
      if (hierarchy) for (auto mesh : hierarchy->hierarchy_selection_wires) { mesh.selected=true; sources.push_back(std::move(mesh)); }
    }
    if (preview && !preview->suppression.empty()) {
      std::vector<RenderOverlayMeshSnapshot> projected;
      const auto suppressed=[&](const RenderOverlayMeshSnapshot &mesh) {
        for (const auto entity : {mesh.entity,mesh.occurrence,mesh.definition,mesh.member})
          if (entity!=kInvalidEntityId && std::ranges::find(preview->suppression.entities,entity)!=preview->suppression.entities.end()) return true;
        return std::ranges::any_of(preview->suppression.occurrences,[&](const auto &record) {
          return record.occurrence==mesh.occurrence && (record.source_entity==mesh.member || record.source_entity==mesh.definition);
        });
      };
      for (const auto &source : sources) {
        if (!suppressed(source)) { projected.push_back(source); continue; }
        for (const auto &record : preview->meshes) {
          if (!record.suppress_source || (record.source_entity!=source.entity && record.source_entity!=source.member && record.source_entity!=source.definition)) continue;
          auto geometry=record.geometry_projection ? record.geometry_projection : record.mesh.geometry_projection();
          if (!geometry && record.dynamic_position_payload) {
            auto positions=std::make_shared<MeshGeometryProjection>();
            positions->source_geometry=record.mesh.geometry_payload();
            positions->positions=record.dynamic_position_payload;
            geometry=std::move(positions);
          }
          const auto mesh=render_mesh_asset_snapshot(kInvalidAssetId,record.mesh,geometry);
          for (const auto &transform : record.instance_transforms.empty() ? std::span<const QMat4>(&record.transform,1u) : std::span<const QMat4>(record.instance_transforms)) {
            auto replacement=source;
            replacement.mesh_revision=preview->revision;
            replacement.geometry_payload=mesh.geometry_payload;
            replacement.geometry_projection=mesh.geometry_projection;
            replacement.vertices=mesh.vertices;
            replacement.indices=mesh.indices;
            replacement.world_transform=transform;
            projected.push_back(std::move(replacement));
          }
        }
      }
      sources=std::move(projected);
    }
    std::vector<PackedBatch> ordinary, active;
    for (const auto &source : sources) {
      if (!source.has_triangle_geometry()) continue;
      auto &batches=source.active_selected ? active : ordinary;
      const auto &previous=source.active_selected ? pane.active : pane.ordinary;
      PackedBatch packed;
      packed.batch.world_from_local.values=source.world_transform.values;
      bool reused=false;
      // Stable source identities gate packing; transform-only changes update
      // the matrix while retaining all immutable local geometry streams.
      for (std::size_t index=0u; index<pane.selection_sources.size(); ++index) {
        const auto &old=pane.selection_sources[index];
        if (old.entity==source.entity && old.occurrence==source.occurrence && old.member==source.member
            && old.geometry_payload==source.geometry_payload
            && old.geometry_projection==source.geometry_projection && old.vertices.data()==source.vertices.data()
            && old.indices.data()==source.indices.data()) {
          const auto &old_batches=old.active_selected ? pane.active : pane.ordinary;
          std::size_t slot=0u;
          for (std::size_t prior=0u; prior<index; ++prior)
            if (pane.selection_sources[prior].has_triangle_geometry() && pane.selection_sources[prior].active_selected==old.active_selected) ++slot;
          if (slot<old_batches.size()) { packed.batch.geometry=old_batches[slot].batch.geometry; reused=true; }
          break;
        }
      }
      if (!reused) {
        OverlayFaceFillMesh mesh;
        mesh.vertices.reserve(source.vertices.size());
        for (std::size_t index=0u; index<source.vertices.size(); ++index) {
          const auto position=source.geometry_projection && source.geometry_projection->positions && index<source.geometry_projection->positions->size()
              ? (*source.geometry_projection->positions)[index] : source.vertices[index].position;
          mesh.vertices.push_back({{position.x,position.y,position.z},{},0xffffffffu});
        }
        mesh.indices.assign(source.indices.begin(),source.indices.end());
        pack_geometry(packed,mesh,batches.size()<previous.size() ? &previous[batches.size()] : nullptr,impl_->revision,impl_->work);
      }
      batches.push_back(std::move(packed));
    }
    pane.ordinary=std::move(ordinary);
    pane.active=std::move(active);
    pane.selection_sources=std::move(sources);
    pane.selected_stamp=selected_stamp;
    pane.hierarchy_stamp=hierarchy_stamp;
    pane.selected_identity=snapshot;
    pane.hierarchy_identity=hierarchy;
    pane.selected_meshes=selected_meshes;
    pane.hierarchy_meshes=hierarchy_meshes;
    pane.preview=std::move(preview);
    pane.selected_visible=visible;
    pane.selection_valid=true;
  }
  return true;
}
void CrimsonEditorProjection::clear() { impl_ = std::make_unique<Impl>(); }
CrimsonEditorProjectionCounters CrimsonEditorProjection::counters() const { return impl_->work; }

ViewportOverlayCameraUpdate CrimsonEditorProjection::update_overlay_cameras(
    std::size_t index, std::uint8_t mask, ViewportOverlayCamera camera) {
  if (index >= impl_->panes.size()) return {};
  auto &pane = impl_->panes[index];
  const auto signature = OverlayRenderableBuilder::camera_signature(camera);
  if (pane.camera_signature != signature || pane.mask != mask) pane.dirty = true;
  pane.mask = mask;
  pane.camera = camera;
  pane.camera_signature = signature;
  return {pane.persistent.source != nullptr,pane.dynamic.source != nullptr};
}
void CrimsonEditorProjection::submit_persistent_overlay(std::size_t index,
    std::uint8_t mask, ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
  if (index >= impl_->panes.size()) return;
  (void)update_overlay_cameras(index,mask,camera);
  auto &pane = impl_->panes[index];
  impl_->update(pane.persistent,std::move(frame));
  pane.dirty = true;
}
void CrimsonEditorProjection::submit_dynamic_overlay(std::size_t index,
    std::uint8_t mask, ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
  if (index >= impl_->panes.size()) return;
  (void)update_overlay_cameras(index,mask,camera);
  auto &pane = impl_->panes[index];
  impl_->update(pane.dynamic,std::move(frame));
  pane.dirty = true;
}
std::shared_ptr<const crimson::shared::EditorFrame> CrimsonEditorProjection::frame(std::uint8_t mask,
    const crimson::shared::Matrix4 &clip,crimson::shared::Rect crop,crimson::shared::Extent backing,
    std::shared_ptr<const crimson::shared::EditorGrid> grid,bool overlays_visible,std::uint64_t scene_generation,
    bool scene_visible) {
  auto found = std::find_if(impl_->panes.begin(),impl_->panes.end(),[mask](const auto &pane) { return pane.mask == mask; });
  if (found==impl_->panes.end() && grid) {
    found=std::find_if(impl_->panes.begin(),impl_->panes.end(),[](const auto &pane) { return pane.mask==0u; });
    if (found!=impl_->panes.end()) found->mask=mask;
  }
  if (found == impl_->panes.end()) return {};
  auto &pane = *found;
  if (pane.scene_generation!=scene_generation) { pane.scene_generation=scene_generation; pane.dirty=true; }
  if (pane.grid!=grid || pane.overlays_visible!=overlays_visible) { pane.grid=grid; pane.overlays_visible=overlays_visible; pane.dirty=true; }
  if (pane.scene_visible!=scene_visible) { pane.scene_visible=scene_visible; pane.dirty=true; }
  if (pane.clip!=clip || pane.crop.x!=crop.x || pane.crop.y!=crop.y || pane.crop.width!=crop.width || pane.crop.height!=crop.height || pane.backing!=backing) {
    pane.clip=clip; pane.crop=crop; pane.backing=backing; pane.dirty=true;
  }
  if (!pane.dirty) return pane.frame;
  const auto camera = OverlayCameraProjector::sanitized_camera(pane.camera);
  if (pane.wireframe_source!=impl_->wireframe) {
    ViewportOverlayFrame wires;
    std::vector<PackedBatch> shaded;
    if (impl_->wireframe) {
      auto snapshot=*impl_->wireframe;
      for (const auto &group : snapshot.raster_groups.groups()) if (group.layer_mask&mask) { snapshot.style=group.style; break; }
      if (snapshot.layer_mask&mask) {
        wires=ViewportSceneOverlayBuilder::build_wireframe_frame({},RenderOverlaySelectionMode::Object,snapshot);
        for (auto &line : wires.lines) {
          if (line.layer==ViewportOverlayLayer::WireframeXray && line.width_pixels<=0.0f)
            line.width_pixels=viewport_overlay_line_width_pixels(line.layer,0.0f)*snapshot.style.raster_scale;
        }
      }
      if (snapshot.wireframe_on_shaded_layer_mask&mask) for (const auto &mesh : snapshot.meshes) {
        if (!mesh.shaded_wire || !mesh.has_wire_geometry()) continue;
        const auto color=mesh.wireframe_on_shaded_color;
        const auto global=snapshot.style.wireframe_on_shaded_color;
        const auto byte=[](float value) { return static_cast<std::uint8_t>(std::lround(std::clamp(value,0.0f,1.0f)*255.0f)); };
        const auto rgba=rgba8(byte(color ? color->x : global.r),byte(color ? color->y : global.g),byte(color ? color->z : global.b),byte(global.a));
        OverlayLineMesh geometry;
        for (const auto &segment : mesh.wire_segments) {
          ViewportOverlayLine line;
          line.start=segment.start; line.end=segment.end; line.color=rgba;
          for (std::size_t support=0u; support<segment.surface_support_count && support<line.surface_support.size(); ++support)
            line.surface_support[support]={segment.surface_support[support],static_cast<int>(support)};
          OverlayLineMeshBuilder::append_line_quad(geometry,line);
        }
        PackedBatch batch;
        batch.batch.primitive=EditorPrimitive::Ribbon;
        batch.batch.depth=crimson::shared::EditorDepth::GreaterOrEqual;
        batch.batch.world_from_local.values=mesh.world_transform.values;
        batch.batch.surface_depth=true;
        batch.batch.width_pixels=mesh.selected ? (mesh.active_selected ? snapshot.style.active_selection_line_width_pixels : snapshot.style.selection_edge_line_width_pixels)
            : viewport_overlay_line_width_pixels(ViewportOverlayLayer::WireframeBase,0.0f)*snapshot.style.raster_scale;
        batch.batch.feather_pixels=kOverlayLineFeatherPixels;
        batch.batch.order=static_cast<std::uint16_t>(viewport_overlay_draw_order(ViewportOverlayLayer::WireframeBase)*64u);
        pack_geometry(batch,geometry,shaded.size()<pane.shaded_wires.size() ? &pane.shaded_wires[shaded.size()] : nullptr,impl_->revision,impl_->work);
        shaded.push_back(std::move(batch));
      }
    }
    impl_->update(pane.wireframe,std::move(wires));
    pane.shaded_wires=std::move(shaded);
    pane.wireframe_source=impl_->wireframe;
  }
  if (overlays_visible) {
    impl_->build(pane.persistent,camera,0u);
    impl_->build(pane.dynamic,camera,1u);
  }
  impl_->build(pane.wireframe,camera,2u);
  auto result = std::make_shared<crimson::shared::EditorFrame>();
  result->revision = ++impl_->revision;
  result->scene_generation=scene_generation;
  result->grid=std::move(grid);
  if (overlays_visible && pane.selected_visible && (!pane.ordinary.empty() || !pane.active.empty()) && crop.width && crop.height && backing.width && backing.height) {
    auto selected=std::make_shared<crimson::shared::EditorSelection>();
    selected->id=mask;
    selected->ordinary_width=std::max(1.0f,pane.style.selection_edge_line_width_pixels);
    selected->active_width=std::max(1.0f,pane.style.active_selection_line_width_pixels);
    selected->feather=kOverlayLineFeatherPixels*std::max(0.01f,pane.style.raster_scale);
    selected->guard_pixels=std::ceil((std::max(selected->ordinary_width,selected->active_width)+selected->feather)*0.5f+2.0f);
    const auto guard=static_cast<std::uint32_t>(selected->guard_pixels);
    selected->extent={crop.width+2u*guard,crop.height+2u*guard};
    selected->crop=crop;
    selected->mask_clip_from_view=clip;
    auto &projection=selected->mask_clip_from_view.values;
    const float scale_x=static_cast<float>(crop.width)/backing.width;
    const float scale_y=static_cast<float>(crop.height)/backing.height;
    const float shift_x=(2.0f*crop.x+crop.width)/backing.width-1.0f;
    const float shift_y=1.0f-(2.0f*crop.y+crop.height)/backing.height;
    for (std::size_t column=0u; column<4u; ++column) {
      projection[column*4u]=(projection[column*4u]-projection[column*4u+3u]*shift_x)/scale_x*static_cast<float>(crop.width)/selected->extent.width;
      projection[column*4u+1u]=(projection[column*4u+1u]-projection[column*4u+3u]*shift_y)/scale_y*static_cast<float>(crop.height)/selected->extent.height;
    }
    const auto color=[](std::uint32_t rgba) -> crimson::shared::Float4 {
      return {srgb_to_linear(static_cast<float>(rgba&0xffu)/255.0f),srgb_to_linear(static_cast<float>((rgba>>8u)&0xffu)/255.0f),
          srgb_to_linear(static_cast<float>((rgba>>16u)&0xffu)/255.0f),static_cast<float>(rgba>>24u)/255.0f};
    };
    selected->ordinary_color=color(pane.style.selection_wire_color);
    selected->active_color=color(pane.style.active_selection_wire_color);
    selected->hidden_alpha=kOverlayOccludedSilhouetteAlphaMultiplier;
    selected->order=static_cast<std::uint16_t>(viewport_overlay_draw_order(ViewportOverlayLayer::SelectedEdge)*64u);
    for (const auto &packed : pane.ordinary) selected->ordinary.push_back(packed.batch);
    for (const auto &packed : pane.active) selected->active.push_back(packed.batch);
    result->selection=std::move(selected);
  }
  for (const auto *stream : {&pane.persistent,&pane.dynamic,&pane.wireframe}) {
    if (!overlays_visible && stream!=&pane.wireframe) continue;
    for (const auto &layer : stream->layers)
    for (const auto &packed : layer.batches) {
      auto batch = packed.batch;
      batch.near_plane = camera.near_plane;
      batch.far_plane = camera.far_plane;
      if(batch.primitive==EditorPrimitive::Icon&&backing.width&&backing.height) {
        auto& transform=batch.world_from_local.values;
        transform[0]=float(crop.width)/backing.width;
        transform[5]=float(crop.height)/backing.height;
        transform[12]=(2.f*crop.x+crop.width)/backing.width-1.f;
        transform[13]=1.f-(2.f*crop.y+crop.height)/backing.height;
      }
      result->batches.push_back(std::move(batch));
    }
  }
  for (const auto &wire : pane.shaded_wires) {
    auto batch=wire.batch;
    batch.near_plane=camera.near_plane; batch.far_plane=camera.far_plane;
    result->batches.push_back(std::move(batch));
  }
  if (overlays_visible && scene_visible && pane.selected_visible) {
    for (const bool active : {false,true}) {
      const auto color=active ? pane.style.active_selection_face_color : pane.style.selection_face_color;
      const auto fill_class=visible_overlay_face_fill_class(active ? OverlayFaceFillClass::ActiveSelection : OverlayFaceFillClass::Selection,color);
      const auto &faces=active ? pane.active : pane.ordinary;
      const auto linear=rgba8_srgb_to_linear(color);
      const crimson::shared::Float3 tint{static_cast<float>(linear&0xffu)/255.0f,
          static_cast<float>((linear>>8u)&0xffu)/255.0f,static_cast<float>((linear>>16u)&0xffu)/255.0f};
      for (const auto layer : {ViewportOverlayLayer::SelectedXrayFace,ViewportOverlayLayer::SelectedFaceMask}) {
        const auto order=static_cast<std::uint16_t>(viewport_overlay_draw_order(layer)*64u);
        for (const auto pass : {OverlayFaceFillPass::Mask,OverlayFaceFillPass::Color}) {
          auto state=OverlayRenderableBuilder::make_state(layer,ViewportOverlayGeometryKind::Triangles,order);
          state.face_fill_class=fill_class;
          state.face_fill_pass=pass;
          if (pass==OverlayFaceFillPass::Mask) {
            state.color_write=false;
            state.blend_order=overlay_face_fill_mask_blend_order();
          }
          for (const auto &face : faces) {
            auto batch=presentation(state);
            batch.geometry=face.batch.geometry;
            batch.world_from_local=face.batch.world_from_local;
            batch.tint=tint;
            batch.alpha=static_cast<float>(linear>>24u)/255.0f;
            result->batches.push_back(std::move(batch));
          }
        }
      }
    }
  }
  if (overlays_visible && impl_->collision_visible) {
    const auto order=static_cast<std::uint16_t>(viewport_overlay_draw_order(ViewportOverlayLayer::CollisionFill)*64u);
    if (scene_visible) for (const auto &chunk : impl_->collision_planner.plan().chunks) {
      const auto found_geometry=impl_->collision_geometry.find(chunk.key.geometry_payload);
      if (found_geometry==impl_->collision_geometry.end()) continue;
      for (const auto &transform : chunk.world_transforms) {
        auto batch=found_geometry->second.fill.batch;
        batch.world_from_local.values=transform.values;
        batch.depth=crimson::shared::EditorDepth::GreaterOrEqual;
        batch.cull_back=true;
        batch.tint={srgb_to_linear(impl_->collision_color.r),srgb_to_linear(impl_->collision_color.g),srgb_to_linear(impl_->collision_color.b)};
        batch.alpha=collision_shape_fill_color(impl_->collision_color).a;
        batch.order=order;
        result->batches.push_back(std::move(batch));
      }
    }
    float scale=impl_->collision_presentation.style.raster_scale;
    for (const auto &group : impl_->collision_presentation.raster_groups.groups())
      if (group.layer_mask&mask) { scale=group.style.raster_scale; break; }
    for (const auto &chunk : impl_->collision_planner.wire_plan().chunks) {
      const auto found_geometry=impl_->collision_geometry.find(chunk.key.geometry_payload);
      if (found_geometry==impl_->collision_geometry.end()) continue;
      for (const auto &transform : chunk.world_transforms) {
        auto batch=found_geometry->second.wire.batch;
        batch.primitive=EditorPrimitive::Ribbon;
        batch.world_from_local.values=transform.values;
        batch.depth=crimson::shared::EditorDepth::GreaterOrEqual;
        batch.tint={srgb_to_linear(impl_->collision_color.r),srgb_to_linear(impl_->collision_color.g),srgb_to_linear(impl_->collision_color.b)};
        batch.width_pixels=scale;
        batch.feather_pixels=kOverlayLineFeatherPixels;
        batch.near_plane=camera.near_plane; batch.far_plane=camera.far_plane;
        batch.alpha=collision_shape_wire_color(impl_->collision_color,chunk.key.material_payload!=0u).a;
        batch.order=static_cast<std::uint16_t>(order+1u);
        result->batches.push_back(std::move(batch));
      }
    }
  }
  pane.frame = std::move(result);
  pane.dirty = false;
  return pane.frame;
}

} // namespace quader
