////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_signature_builder.hpp"

#include <array>
#include <bit>
#include <functional>
#include <optional>
#include <string>
#include <algorithm>
#include <vector>

namespace quader {
namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
std::size_t overlay_layer_index(ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(layer);
}

void add_overlay_element_signature(RenderSignatureBuilder &signature,
                                   const ViewportOverlayElementRef &element) {
  const auto add_signed = [&signature](int value) {
    signature.add_value(
        static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
  };
  add_signed(element.object_id);
  add_signed(element.vertex_id);
  add_signed(element.edge_a);
  add_signed(element.edge_b);
  add_signed(element.face_id);
}

void add_frame_triangle_signature(RenderSignatureBuilder &signature,
                                  const ViewportOverlayTriangle &triangle) {
  signature.add_value(static_cast<std::uint32_t>(triangle.layer))
      .add_value(triangle.color)
      .add_value(static_cast<std::uint32_t>(triangle.style_role))
      .add_value(static_cast<std::uint32_t>(triangle.source))
      .add_vec3(triangle.a)
      .add_vec3(triangle.b)
      .add_vec3(triangle.c);
  add_overlay_element_signature(signature, triangle.element);
}

void add_layer_triangle_signature(RenderSignatureBuilder &signature,
                                  const ViewportOverlayTriangle &triangle) {
  signature.add_value(static_cast<std::uint32_t>(triangle.layer))
      .add_value(triangle.color)
      .add_value(static_cast<std::uint32_t>(triangle.style_role))
      .add_value(static_cast<std::uint32_t>(triangle.source))
      .add_vec3(triangle.a)
      .add_vec3(triangle.b)
      .add_vec3(triangle.c);
  add_overlay_element_signature(signature, triangle.element);
}

void add_line_signature(RenderSignatureBuilder &signature,
                        const ViewportOverlayLine &line) {
  signature.add_value(static_cast<std::uint32_t>(line.layer))
      .add_value(line.color)
      .add_float(line.width_pixels)
      .add_value(static_cast<std::uint32_t>(line.style_role))
      .add_value(static_cast<std::uint32_t>(line.source))
      .add_vec3(line.start)
      .add_vec3(line.end);
  add_overlay_element_signature(signature, line.element);
  for (const auto &support : line.surface_support) {
    signature.add_value(static_cast<std::uint64_t>(static_cast<std::int64_t>(support.vertex_id)));
    if (support.vertex_id >= 0) signature.add_vec3(support.position);
  }
  if (line.occlusion_style != ViewportOverlayLineOcclusionStyle::Default) {
    signature.add_value(static_cast<std::uint32_t>(line.occlusion_style))
        .add_float(line.occluded_dash_period_pixels);
  }
}

void add_point_signature(RenderSignatureBuilder &signature,
                         const ViewportOverlayPoint &point) {
  signature.add_value(static_cast<std::uint32_t>(point.layer))
      .add_value(point.color)
      .add_float(point.size_pixels)
      .add_value(static_cast<std::uint32_t>(point.style_role))
      .add_value(static_cast<std::uint32_t>(point.source))
      .add_bool(point.outline_growth_pixels.has_value());
  if (point.outline_growth_pixels.has_value()) {
    signature.add_float(*point.outline_growth_pixels);
  }
  signature.add_vec3(point.position);
  add_overlay_element_signature(signature, point.element);
}

void add_icon_signature(RenderSignatureBuilder &signature,
                        const ViewportOverlayIcon &icon) {
  signature.add_value(static_cast<std::uint32_t>(icon.layer))
      .add_value(static_cast<std::uint32_t>(icon.kind))
      .add_value(icon.color)
      .add_float(icon.size_pixels)
      .add_value(static_cast<std::uint32_t>(icon.source))
      .add_vec3(icon.position);
  add_overlay_element_signature(signature, icon.element);
}

} // namespace

RenderSignatureBuilder::RenderSignatureBuilder()
    : hash_(kFnvOffset) {}

RenderSignatureBuilder::RenderSignatureBuilder(std::uint64_t seed)
    : hash_(seed) {}

RenderSignatureBuilder &RenderSignatureBuilder::add_value(std::uint64_t value) {
  hash_ ^= value;
  hash_ *= kFnvPrime;
  return *this;
}

RenderSignatureBuilder &RenderSignatureBuilder::add_size(std::size_t value) {
  return add_value(static_cast<std::uint64_t>(value));
}

RenderSignatureBuilder &RenderSignatureBuilder::add_bool(bool value) {
  return add_value(value ? 1u : 0u);
}

RenderSignatureBuilder &RenderSignatureBuilder::add_float(float value) {
  return add_value(std::bit_cast<std::uint32_t>(value));
}

RenderSignatureBuilder &
RenderSignatureBuilder::add_string(std::string_view value) {
  return add_value(std::hash<std::string>{}(std::string(value)));
}

RenderSignatureBuilder &RenderSignatureBuilder::add_vec2(QVec2 value) {
  add_float(value.x);
  return add_float(value.y);
}

RenderSignatureBuilder &RenderSignatureBuilder::add_vec3(QVec3 value) {
  add_float(value.x);
  add_float(value.y);
  return add_float(value.z);
}

RenderSignatureBuilder &RenderSignatureBuilder::add_mat4(const QMat4 &value) {
  for (const float entry : value.values) {
    add_float(entry);
  }
  return *this;
}

namespace {

void add_material_document_signature(RenderSignatureBuilder &signature,
                                     const AssetRegistry &assets,
                                     MaterialReference reference) {
  signature.add_value(static_cast<std::uint64_t>(reference.kind()));
  if (reference.project_owned()) {
    signature.add_string(reference.project_asset().str());
    return;
  }
  const AssetId material_id = reference.embedded_asset();
  const MaterialDocument *material = assets.material(material_id);
  signature.add_value(material_id);
  if (!material) {
    signature.add_value(0);
    return;
  }

  signature.add_value(material->revision.value);
  for (const MaterialTextureSlotDescriptor &descriptor :
       material_texture_slot_descriptors()) {
    const std::optional<AssetId> texture_id =
        material_texture_slot_asset(*material, descriptor.id);
    if (!texture_id.has_value()) {
      continue;
    }
    signature.add_string(descriptor.id)
        .add_value(*texture_id)
        .add_value(assets.texture_revision(*texture_id).value);
  }
}

} // namespace

std::uint64_t
RenderSignatureBuilder::material_signature(const AssetRegistry &assets,
                                           const MaterialComponent *material) {
  if (!material) {
    return 0;
  }

  RenderSignatureBuilder signature;
  add_material_document_signature(signature, assets, material->material);
  signature.add_size(material->material_slots.size());
  std::vector<std::uint32_t> slots;
  slots.reserve(material->material_slots.size());
  for (const auto &[slot, materialId] : material->material_slots) {
    (void)materialId;
    slots.push_back(slot);
  }
  std::sort(slots.begin(), slots.end());
  for (const std::uint32_t slot : slots) {
    signature.add_value(slot);
    add_material_document_signature(signature, assets,
                                    material->material_slots.at(slot));
  }
  return signature.value();
}

std::uint64_t
RenderSignatureBuilder::mesh_asset_signature(std::uint64_t seed,
                                             const MeshAsset &mesh) {
  const MeshGeometryPayload &geometry = mesh.geometry();
  RenderSignatureBuilder signature(seed);
  signature.add_size(geometry.vertices.size());
  for (const MeshVertex &vertex : geometry.vertices) {
    signature.add_vec3(vertex.position)
        .add_vec3(vertex.normal)
        .add_vec2(vertex.uv)
        .add_vec3(vertex.uv_geometric_normal)
        .add_value(vertex.rgba);
    for (const float coefficient : vertex.uv_carrier) {
      signature.add_float(coefficient);
    }
  }
  signature.add_size(geometry.indices.size());
  for (const std::uint32_t index : geometry.indices) {
    signature.add_value(index);
  }
  const std::vector<MeshPrimitiveSpan> primitive_spans =
      normalized_mesh_primitive_spans(mesh);
  signature.add_size(primitive_spans.size());
  for (const MeshPrimitiveSpan &primitive : primitive_spans) {
    signature.add_value(primitive.index_offset)
        .add_value(primitive.index_count)
        .add_value(primitive.material_slot);
  }
  return signature.value();
}

std::uint64_t
RenderSignatureBuilder::light_signature(const LightComponent &light) {
  RenderSignatureBuilder signature;
  signature.add_value(static_cast<std::uint32_t>(light.type))
      .add_vec3(light.color)
      .add_float(light.intensity)
      .add_float(light.range)
      .add_bool(light.casts_shadows)
      .add_float(light.distance_attenuation);
  return signature.value();
}

std::uint64_t RenderSignatureBuilder::light_transform_signature(
    const QMat4 &world_transform) {
  RenderSignatureBuilder signature;
  signature.add_mat4(world_transform);
  return signature.value();
}

std::uint64_t RenderSignatureBuilder::overlay_frame_signature(
    const ViewportOverlayFrame &frame) {
  return overlay_frame_signatures(frame).frame_signature;
}

OverlayFrameSignatures RenderSignatureBuilder::overlay_frame_signatures(
    const ViewportOverlayFrame &frame) {
  OverlayFrameSignatures result;
  RenderSignatureBuilder frame_signature;
  RenderSignatureBuilder component_point_geometry_signature;
  std::array<RenderSignatureBuilder, kViewportOverlayLayerCount>
      layer_signatures;
  for (std::size_t layer_index = 0; layer_index < layer_signatures.size();
       ++layer_index) {
    layer_signatures[layer_index].add_value(
        static_cast<std::uint32_t>(layer_index));
  }

  frame_signature.add_size(frame.triangles.size());
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    add_frame_triangle_signature(frame_signature, triangle);
    const std::size_t layer_index = overlay_layer_index(triangle.layer);
    ++result.triangle_counts[layer_index];
    add_layer_triangle_signature(layer_signatures[layer_index], triangle);
    if (triangle.layer == ViewportOverlayLayer::SourceWireDepthStamp) {
      result.has_source_wire_depth_stamp = true;
    }
  }

  frame_signature.add_size(frame.lines.size());
  for (const ViewportOverlayLine &line : frame.lines) {
    add_line_signature(frame_signature, line);
    ++result.line_counts[overlay_layer_index(line.layer)];
    add_line_signature(layer_signatures[overlay_layer_index(line.layer)], line);
  }

  frame_signature.add_size(frame.points.size());
  for (const ViewportOverlayPoint &point : frame.points) {
    add_point_signature(frame_signature, point);
    ++result.point_counts[overlay_layer_index(point.layer)];
    add_point_signature(layer_signatures[overlay_layer_index(point.layer)],
                        point);
    if (point.layer == ViewportOverlayLayer::UnselectedVertex ||
        point.layer == ViewportOverlayLayer::SelectedVertex) {
      component_point_geometry_signature
          .add_vec3(point.position)
          .add_value(static_cast<std::uint32_t>(point.source));
      add_overlay_element_signature(component_point_geometry_signature,
                                    point.element);
    }
  }

  frame_signature.add_size(frame.icons.size());
  for (const ViewportOverlayIcon &icon : frame.icons) {
    add_icon_signature(frame_signature, icon);
    ++result.icon_counts[overlay_layer_index(icon.layer)];
    add_icon_signature(layer_signatures[overlay_layer_index(icon.layer)],
                       icon);
  }

  frame_signature.add_bool(frame.hover_target_selected);

  result.frame_signature = frame_signature.value();
  result.component_point_geometry_signature =
      component_point_geometry_signature.value();
  for (std::size_t layer_index = 0; layer_index < layer_signatures.size();
       ++layer_index) {
    result.layer_signatures[layer_index] =
        layer_signatures[layer_index].value();
  }
  return result;
}

std::uint64_t RenderSignatureBuilder::overlay_camera_signature(
    const ViewportOverlayCamera &camera) {
  RenderSignatureBuilder signature;
  signature.add_value(static_cast<std::uint32_t>(camera.projection))
      .add_vec3(camera.eye)
      .add_vec3(camera.forward)
      .add_vec3(camera.right)
      .add_vec3(camera.up)
      .add_vec2(camera.viewport_size)
      .add_float(camera.orthographic_size)
      .add_float(camera.fov_degrees)
      .add_float(camera.near_plane)
      .add_float(camera.far_plane);
  return signature.value();
}

std::uint64_t RenderSignatureBuilder::overlay_layer_signature(
    const ViewportOverlayFrame &frame, ViewportOverlayLayer layer) {
  return overlay_frame_signatures(frame)
      .layer_signatures[overlay_layer_index(layer)];
}

} // namespace quader
