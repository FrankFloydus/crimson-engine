////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_scene_projection.hpp"
#include "render/native_instance_identity.hpp"

#include "core/color_space.hpp"
#include "render/mesh_quad_uv_interpolation.hpp"
#include "render/scene_render_extraction.hpp"
#include "assets/curve_asset_payload.hpp"
#include "scene/light_properties.hpp"

#include <lodepng.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstring>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <span>
#include <tuple>
#include <utility>

namespace quader {
namespace {

using Float3 = crimson::shared::Float3;
using VertexAttributes = crimson::shared::VertexAttributes;
using DrawRange = crimson::shared::DrawRange;

struct MeshKey final {
  std::uint64_t kind = 0u;
  std::uint64_t first = 0u;
  std::uint64_t second = 0u;
  std::uint64_t third = 0u;
  friend auto operator<=>(const MeshKey &, const MeshKey &) = default;
};

struct GeometrySource final {
  RenderMeshAssetSnapshot mesh;
  std::uint64_t positions_revision = 0u;
  std::shared_ptr<const std::vector<QVec3>> positions_pin;
};

struct PackedGeometry final {
  GeometrySource source;
  std::shared_ptr<const std::vector<Float3>> positions;
  std::shared_ptr<const std::vector<VertexAttributes>> attributes;
  std::shared_ptr<const std::vector<std::uint32_t>> indices;
  std::uint64_t positions_revision = 0u;
  std::uint64_t attributes_revision = 0u;
  std::uint64_t indices_revision = 0u;
};

struct MeshEntry final {
  std::uint64_t id = 0u;
  std::shared_ptr<const std::vector<DrawRange>> ranges;
  std::uint64_t ranges_revision = 0u;
};

struct SurfaceEntry final {
  RenderMaterialAssetSnapshot source;
  crimson::shared::SurfaceUpdate surface;
};

bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(QVec2 value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool same_stream(std::span<const MeshVertex> left,
                 std::span<const MeshVertex> right) {
  return left.data() == right.data() && left.size() == right.size();
}

template <class T>
bool same_span(std::span<const T> left, std::span<const T> right) {
  return left.data() == right.data() && left.size() == right.size();
}

bool same_geometry_source(const GeometrySource &left,
                          const GeometrySource &right) {
  return left.mesh.mesh_revision == right.mesh.mesh_revision &&
         left.mesh.geometry_payload == right.mesh.geometry_payload &&
         left.mesh.geometry_projection == right.mesh.geometry_projection &&
         left.mesh.geometry_token == right.mesh.geometry_token &&
         left.positions_revision == right.positions_revision &&
         left.positions_pin == right.positions_pin &&
         same_stream(left.mesh.vertices, right.mesh.vertices) &&
         same_span(left.mesh.positions, right.mesh.positions) &&
         same_span(left.mesh.indices, right.mesh.indices) &&
         same_span(left.mesh.primitives, right.mesh.primitives) &&
         same_span(left.mesh.circular_uv_interpolations,
                   right.mesh.circular_uv_interpolations);
}

bool same_attributes(const VertexAttributes &left,
                      const VertexAttributes &right) {
  return left.normal == right.normal && left.tangent == right.tangent &&
         left.uv0 == right.uv0 && left.uv1 == right.uv1 &&
         left.rgba == right.rgba &&
         left.interpolation_coordinates == right.interpolation_coordinates &&
         left.interpolation_correction == right.interpolation_correction &&
         left.interpolation_mode == right.interpolation_mode &&
         left.uv_carrier == right.uv_carrier &&
         left.geometric_normal == right.geometric_normal;
}

QVec3 effective_position(const RenderMeshAssetSnapshot &mesh,
                         std::size_t index) {
  return mesh.positions.empty() ? mesh.vertices[index].position
                                : mesh.positions[index];
}

std::vector<crimson::shared::Float4>
derive_tangents(const RenderMeshAssetSnapshot &mesh) {
  std::vector<QVec3> directions(mesh.vertices.size());
  std::vector<QVec3> perpendiculars(mesh.vertices.size());
  for (std::size_t offset = 0u; offset < mesh.indices.size(); offset += 3u) {
    const auto first = mesh.indices[offset];
    const auto second = mesh.indices[offset + 1u];
    const auto third = mesh.indices[offset + 2u];
    const QVec3 edge_u = effective_position(mesh, second) - effective_position(mesh, first);
    const QVec3 edge_v = effective_position(mesh, third) - effective_position(mesh, first);
    const QVec2 uv_u = mesh.vertices[second].uv - mesh.vertices[first].uv;
    const QVec2 uv_v = mesh.vertices[third].uv - mesh.vertices[first].uv;
    const float determinant = uv_u.x * uv_v.y - uv_u.y * uv_v.x;
    if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f) continue;
    const QVec3 direction = (edge_u * uv_v.y - edge_v * uv_u.y) / determinant;
    const QVec3 perpendicular = (edge_v * uv_u.x - edge_u * uv_v.x) / determinant;
    if (!finite(direction) || !finite(perpendicular)) continue;
    for (const auto vertex : {first, second, third}) {
      directions[vertex] += direction;
      perpendiculars[vertex] += perpendicular;
    }
  }
  std::vector<crimson::shared::Float4> tangents;
  tangents.reserve(mesh.vertices.size());
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    QVec3 normal = mesh.vertices[index].normal;
    normal = length(normal) > 0.000001f ? normalize(normal) : QVec3{0.0f, 1.0f, 0.0f};
    QVec3 tangent = directions[index] - normal * dot(normal, directions[index]);
    if (length(tangent) <= 0.000001f) {
      const QVec3 reference = std::abs(normal.y) < 0.9f
                                 ? QVec3{0.0f, 1.0f, 0.0f}
                                 : QVec3{1.0f, 0.0f, 0.0f};
      tangent = cross(reference, normal);
    }
    tangent = normalize(tangent);
    const float sign = dot(cross(normal, tangent), perpendiculars[index]) < 0.0f
                           ? -1.0f : 1.0f;
    tangents.push_back({tangent.x, tangent.y, tangent.z, sign});
  }
  return tangents;
}

bool validate_geometry(const RenderMeshAssetSnapshot &mesh, std::string &error) {
  if (mesh.vertices.empty() || mesh.indices.empty() ||
      mesh.indices.size() > std::numeric_limits<std::uint32_t>::max() ||
      mesh.indices.size() % 3u != 0u ||
      (!mesh.positions.empty() && mesh.positions.size() != mesh.vertices.size())) {
    error = "Crimson mesh streams have invalid vertex, position or triangle counts";
    return false;
  }
  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    const auto &vertex = mesh.vertices[index];
    if (!finite(effective_position(mesh, index)) || !finite(vertex.normal) ||
        !finite(vertex.uv) || !finite(vertex.uv_geometric_normal) ||
        !std::ranges::all_of(vertex.uv_carrier, [](float value) { return std::isfinite(value); })) {
      error = "Crimson mesh contains a nonfinite position or attribute";
      return false;
    }
  }
  if (!std::ranges::all_of(mesh.indices, [&](std::uint32_t index) { return index < mesh.vertices.size(); })) {
    error = "Crimson mesh contains an out-of-range triangle index";
    return false;
  }
  return true;
}

bool suppressed(const RenderToolPreviewSuppressionSnapshot &suppression,
                 EntityId entity, EntityId occurrence = kInvalidEntityId,
                 EntityId definition = kInvalidEntityId,
                 EntityId member = kInvalidEntityId) {
  for (const EntityId candidate : {entity, occurrence, definition, member}) {
    if (candidate != kInvalidEntityId &&
        std::ranges::find(suppression.entities, candidate) != suppression.entities.end()) return true;
  }
  return std::ranges::any_of(suppression.occurrences, [&](const auto &entry) {
    return entry.occurrence == occurrence &&
           (entry.source_entity == member || entry.source_entity == definition);
  });
}

std::string material_key(const RenderMaterialAssetSnapshot &material) {
  return material.reference.project_owned()
             ? "project:" + std::string(material.reference.project_asset().str())
             : "asset:" + std::to_string(material.material_id);
}

bool same_texture_source(const RenderTextureAssetSnapshot &left,
                          const RenderTextureAssetSnapshot &right) {
  return left.content_revision == right.content_revision && left.texture_id == right.texture_id &&
         left.project_texture_id == right.project_texture_id &&
         left.texture.revision.value == right.texture.revision.value &&
         left.texture.source_path == right.texture.source_path &&
         left.texture.color_space == right.texture.color_space &&
         left.encoded_file_bytes == right.encoded_file_bytes &&
         left.decoded_rgba8 == right.decoded_rgba8 && left.width == right.width &&
         left.height == right.height && left.present == right.present;
}

} // namespace

struct CrimsonSceneProjectionCache::State final {
  explicit State(std::filesystem::path root) : resources(std::move(root)) {}
  std::filesystem::path resources;
  std::uint64_t next_identity = 1u;
  std::uint64_t next_revision = 1u;
  CrimsonSceneProjectionStats counters;
  std::map<MeshKey, std::shared_ptr<const PackedGeometry>> geometry;
  std::map<std::tuple<std::uintptr_t, std::size_t, std::uintptr_t, std::size_t>,
           std::weak_ptr<const PackedGeometry>> stream_families;
  std::map<MeshKey, MeshEntry> meshes;
  std::map<MeshKey, std::uint64_t> instances;
  std::map<MeshKey,EntityId> preview_light_ids;
  EntityId next_light_id=std::numeric_limits<EntityId>::max();
  std::map<std::string, SurfaceEntry> surfaces;

  std::shared_ptr<const PackedGeometry> pack(
      MeshKey key, GeometrySource input, std::string &error) {
    const auto found = geometry.find(key);
    auto previous = found == geometry.end() ? nullptr : found->second;
    if (previous && same_geometry_source(previous->source, input)) return previous;
    if (!validate_geometry(input.mesh, error)) return {};
    const auto family_key = std::tuple{
        reinterpret_cast<std::uintptr_t>(input.mesh.indices.data()), input.mesh.indices.size(),
        reinterpret_cast<std::uintptr_t>(input.mesh.primitives.data()), input.mesh.primitives.size()};
    if (!previous) {
      if (const auto family = stream_families.find(family_key); family != stream_families.end())
        previous = family->second.lock();
    }
    auto candidate = std::make_shared<PackedGeometry>();
    candidate->source = std::move(input);
    const auto &mesh = candidate->source.mesh;
    auto positions = std::make_shared<std::vector<Float3>>();
    positions->reserve(mesh.vertices.size());
    for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
      const QVec3 position = effective_position(mesh, index);
      positions->push_back({position.x, position.y, position.z});
    }
    ++counters.position_packs;
    if (previous && *positions == *previous->positions) {
      candidate->positions = previous->positions;
      candidate->positions_revision = previous->positions_revision;
    } else {
      candidate->positions = std::move(positions);
      candidate->positions_revision = next_revision++;
    }
    const auto tangents = derive_tangents(mesh);
    const auto interpolation = build_mesh_quad_uv_interpolations(mesh.vertices,
        mesh.indices, mesh.primitives, mesh.positions, mesh.circular_uv_interpolations);
    auto attributes = std::make_shared<std::vector<VertexAttributes>>();
    attributes->reserve(mesh.vertices.size());
    for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
      const auto &vertex = mesh.vertices[index];
      const auto &mapping = interpolation[index];
      VertexAttributes attribute;
      attribute.normal = {vertex.normal.x, vertex.normal.y, vertex.normal.z};
      attribute.tangent = tangents[index];
      attribute.uv0 = {vertex.uv.x, vertex.uv.y};
      attribute.rgba = vertex.rgba;
      attribute.uv_carrier = vertex.uv_carrier;
      attribute.geometric_normal = {vertex.uv_geometric_normal.x,
          vertex.uv_geometric_normal.y, vertex.uv_geometric_normal.z};
      if (mapping.circular) {
        attribute.interpolation_coordinates = mapping.circular_coordinates;
        attribute.interpolation_correction = mapping.uv_from_polar;
        attribute.interpolation_mode = 1.0f;
      } else if (mapping.enabled) {
        attribute.interpolation_coordinates = {mapping.coordinates.x,
            mapping.coordinates.y, mapping.coordinates.z, 0.0f};
        attribute.interpolation_correction = {mapping.geometry_warp.x,
            mapping.geometry_warp.y, mapping.uv_correction.x, mapping.uv_correction.y};
        attribute.interpolation_mode = 2.0f;
      }
      attributes->push_back(attribute);
    }
    ++counters.attribute_packs;
    if (previous && std::ranges::equal(*attributes, *previous->attributes, same_attributes)) {
      candidate->attributes = previous->attributes;
      candidate->attributes_revision = previous->attributes_revision;
    } else {
      candidate->attributes = std::move(attributes);
      candidate->attributes_revision = next_revision++;
    }
    if (previous && std::ranges::equal(mesh.indices, *previous->indices)) {
      candidate->indices = previous->indices;
      candidate->indices_revision = previous->indices_revision;
    } else {
      candidate->indices = std::make_shared<const std::vector<std::uint32_t>>(
          mesh.indices.begin(), mesh.indices.end());
      candidate->indices_revision = next_revision++;
      ++counters.index_packs;
    }
    geometry[key] = candidate;
    stream_families[family_key] = candidate;
    return candidate;
  }

  bool surface(const RenderMaterialAssetSnapshot *input,
               crimson::shared::SurfaceUpdate &output, std::string &error) {
    if (input == nullptr || !input->present) input = &packaged_default_render_material();
    const std::string key = material_key(*input);
    auto found = surfaces.find(key);
    if (found != surfaces.end() && render_material_payloads_equal(&found->second.source, input)) {
      output = found->second.surface;
      return true;
    }
    SurfaceEntry entry;
    entry.source = *input;
    entry.surface.id = found == surfaces.end() ? next_identity++ : found->second.surface.id;
    entry.surface.revision = next_revision++;
    const auto color = input->document.base_color;
    entry.surface.base_color = {color.x, color.y, color.z, 1.0f};
    const float solid = srgb_to_linear(181.0f / 255.0f);
    entry.surface.solid_color = {solid, solid, solid, 1.0f};
    const auto &transform = input->texture_transform;
    entry.surface.uv_center = {transform.center.x, transform.center.y};
    entry.surface.uv_scale = {transform.scale.x, transform.scale.y};
    entry.surface.uv_offset = {transform.offset.x, transform.offset.y};
    entry.surface.uv_rotation_radians = transform.rotation_degrees * std::numbers::pi_v<float> / 180.0f;
    if (const auto *texture = input->texture_for_slot(material_texture_slot::kBaseColor);
        texture != nullptr && texture->present) {
      auto &image = entry.surface.image;
      image.id = entry.surface.id;
      image.revision = entry.surface.revision;
      image.srgb = texture->texture.color_space == TextureColorSpace::Srgb;
      const auto *old_texture = found == surfaces.end() ? nullptr
          : found->second.source.texture_for_slot(material_texture_slot::kBaseColor);
      if (old_texture != nullptr && same_texture_source(*old_texture, *texture)) {
        image = found->second.surface.image;
      } else if (texture->decoded_rgba8 && texture->width && texture->height) {
        const auto &pixels = *texture->decoded_rgba8;
        const std::uint64_t expected = static_cast<std::uint64_t>(texture->width) * texture->height * 4u;
        if (expected != pixels.size()) {
          error = "Crimson default image dimensions disagree with its pinned pixels";
          return false;
        }
        image.extent = {texture->width, texture->height};
        image.rgba8 = {{reinterpret_cast<const std::uint8_t *>(pixels.data()), pixels.size()},
            texture->decoded_rgba8, image.revision};
      } else {
        auto pixels = std::make_shared<std::vector<unsigned char>>();
        unsigned width = 0u;
        unsigned height = 0u;
        unsigned result = 0u;
        if (texture->encoded_file_bytes && !texture->encoded_file_bytes->empty()) {
          result = lodepng::decode(*pixels, width, height,
              reinterpret_cast<const unsigned char *>(texture->encoded_file_bytes->data()),
              texture->encoded_file_bytes->size());
        } else {
          std::filesystem::path path = texture->texture.source_path;
          if (path.is_relative()) path = resources / path;
          result = lodepng::decode(*pixels, width, height, path.string());
        }
        ++counters.image_decodes;
        if (result != 0u || !width || !height) {
          error = "Crimson could not decode the pinned default image: " + std::string(lodepng_error_text(result));
          return false;
        }
        image.extent = {width, height};
        image.rgba8 = {*pixels, pixels, image.revision};
      }
    }
    output = entry.surface;
    surfaces[key] = std::move(entry);
    return true;
  }

  bool append_mesh(CrimsonSceneProjection &output, const MeshKey &key,
                   std::shared_ptr<const PackedGeometry> packed,
                   std::span<const MeshPrimitiveSpan> primitives,
                   std::uint64_t &mesh_id, std::string &error) {
    auto &entry = meshes[key];
    if (entry.id == 0u) entry.id = next_identity++;
    auto ranges = std::make_shared<std::vector<DrawRange>>();
    if (primitives.empty()) {
      ranges->push_back({0u, static_cast<std::uint32_t>(packed->indices->size()), 0u});
    } else {
      for (const auto &primitive : primitives) {
        if (!primitive.index_count) continue;
        if (primitive.index_count % 3u != 0u || primitive.index_offset > packed->indices->size() ||
            primitive.index_count > packed->indices->size() - primitive.index_offset) {
          error = "Crimson primitive range escapes its pinned triangle stream";
          return false;
        }
        ranges->push_back({primitive.index_offset, primitive.index_count, primitive.material_slot});
      }
    }
    const auto same_range = [](const DrawRange &left, const DrawRange &right) {
      return left.first_index == right.first_index && left.index_count == right.index_count &&
             left.material_slot == right.material_slot;
    };
    if (!entry.ranges || !std::ranges::equal(*ranges, *entry.ranges, same_range)) {
      entry.ranges = std::move(ranges);
      entry.ranges_revision = next_revision++;
    }
    mesh_id = entry.id;
    const auto existing = std::ranges::find(output.meshes, entry.id, &crimson::shared::MeshUpdate::id);
    if (existing != output.meshes.end() &&
        (existing->positions.pin != packed->positions || existing->attributes.pin != packed->attributes ||
         existing->indices.pin != packed->indices || existing->ranges.pin != entry.ranges)) {
      error = "Crimson scene contains conflicting streams for one mesh identity";
      return false;
    }
    if (existing == output.meshes.end()) {
      output.meshes.push_back({entry.id,
          {*packed->positions, packed->positions, packed->positions_revision},
          {*packed->attributes, packed->attributes, packed->attributes_revision},
          {*packed->indices, packed->indices, packed->indices_revision},
          {*entry.ranges, entry.ranges, entry.ranges_revision}});
    }
    return true;
  }

  bool append_instance(CrimsonSceneProjection &output, const MeshKey &key,
                       std::uint64_t mesh_id, const RenderMaterialBindingSnapshot &material,
                       const QMat4 &world, const QMat4 &sampling, bool double_sided,bool cast_shadows,
                       std::uint64_t transform_revision,
                       std::uint64_t sampling_revision, std::string &error,bool context_dimmed=false) {
    auto &identity = instances[key];
    if (!identity) identity = next_identity++;
    crimson::shared::InstanceUpdate instance;
    instance.id = identity;
    instance.mesh = mesh_id;
    instance.world_from_local.values = world.values;
    instance.sampling_from_local.values = sampling.values;
    instance.sampling_basis.values = {1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    instance.transform_revision = transform_revision;
    instance.sampling_revision = sampling_revision;
    instance.appearance_revision = material.revision;
    instance.double_sided = double_sided;
    instance.context_dimmed=context_dimmed;
    crimson::shared::SurfaceUpdate primary;
    if (!surface(&material.primary_material, primary, error)) return false;
    instance.surface = primary.id;
    const auto add_surface = [&](crimson::shared::SurfaceUpdate value,const RenderMaterialAssetSnapshot& source) {
      const auto existing = std::ranges::find(output.surfaces, value.id, &crimson::shared::SurfaceUpdate::id);
      if (existing != output.surfaces.end() && existing->revision != value.revision) {
        error = "Crimson scene contains conflicting snapshots for one material identity";
        return false;
      }
      if (existing == output.surfaces.end()) {
        output.source_materials.emplace_back(value.id,source.present?source:packaged_default_render_material());
        output.surfaces.push_back(std::move(value));
      }
      return true;
    };
    if (!add_surface(primary,material.primary_material)) return false;
    for (const auto &[slot, source] : material.material_slots) {
      crimson::shared::SurfaceUpdate value;
      if (!surface(&source, value, error)) return false;
      instance.surface_bindings.push_back({slot, value.id});
      if (!add_surface(std::move(value),source)) return false;
    }
    output.instance_cast_shadows.emplace_back(instance.id,cast_shadows);
    output.instances.push_back(std::move(instance));
    return true;
  }
};

CrimsonSceneProjectionCache::CrimsonSceneProjectionCache(std::filesystem::path resources)
    : state_(std::make_unique<State>(std::move(resources))) {}
CrimsonSceneProjectionCache::~CrimsonSceneProjectionCache() = default;

bool CrimsonSceneProjectionCache::prepare(
    std::shared_ptr<const RenderSceneSnapshot> source,
    std::shared_ptr<const RenderToolPreviewSceneSnapshot> preview,
    std::uint64_t generation,
    std::shared_ptr<const CrimsonSceneProjection> &projection,
    std::string &error) {
  error.clear();
  if (!source || !generation || (preview && !preview->complete)) {
    error = "Crimson requires a complete pinned scene and preview generation";
    return false;
  }
  const auto valid_transform=[](const QMat4& matrix){return std::ranges::all_of(matrix.values,[](float value){return std::isfinite(value);});};
  if(preview)for(const auto& curve:preview->curves){
    if(!curve.sample_payload||!curve.sample_payload->error_message.empty()||curve.instance_transforms.empty()||!std::ranges::all_of(curve.instance_transforms,valid_transform)||
       !std::ranges::all_of(curve.sample_payload->samples,[](const auto& sample){return std::isfinite(sample.position.x)&&std::isfinite(sample.position.y)&&std::isfinite(sample.position.z)&&std::isfinite(sample.tangent.x)&&std::isfinite(sample.tangent.y)&&std::isfinite(sample.tangent.z)&&std::isfinite(sample.parameter);})){error="Crimson Curve preview requires complete finite pinned samples and placements";return false;}
  }
  const auto valid_light=[&](const LightComponent& light,const QMat4& world){return valid_transform(world)&&light_component_metadata_is_valid(light)&&
      (light.type==LightType::Directional||light.type==LightType::Point||light.type==LightType::Spot)&&finite(light.color)&&std::isfinite(light.intensity)&&std::isfinite(light.range)&&std::isfinite(light.distance_attenuation);};
  struct CacheTransaction final {
    std::unique_ptr<State> &current;
    std::unique_ptr<State> previous;
    bool committed = false;
    ~CacheTransaction() {
      if (committed) return;
      const auto counters = current ? current->counters : previous->counters;
      current = std::move(previous);
      current->counters = counters;
    }
  } transaction{state_, std::move(state_)};
  state_ = std::make_unique<State>(*transaction.previous);
  std::set<MeshKey> used_geometry;
  auto candidate = std::make_shared<CrimsonSceneProjection>();
  candidate->source = std::move(source);
  candidate->preview = std::move(preview);
  candidate->generation = generation;
  const RenderToolPreviewSuppressionSnapshot empty_suppression;
  const auto &suppression = candidate->preview ? candidate->preview->suppression : empty_suppression;
  std::set<EntityId> occupied_ids;for(const auto& light:candidate->source->lights)occupied_ids.insert(light.entity_id);
  for(const auto& light:candidate->source->lights){if(suppressed(suppression,light.entity_id))continue;if(!valid_light(light.light,light.world_transform)){error="Crimson scene Light contains nonfinite or invalid authored properties";return false;}candidate->lights.push_back(light);}
  std::set<MeshKey> used_light_keys;
  if(candidate->preview)for(std::size_t index=0;index<candidate->preview->lights.size();++index){const auto& record=candidate->preview->lights[index];if(record.instance_transforms.empty()){error="Crimson Light preview has no pinned placements";return false;}for(std::size_t placement=0;placement<record.instance_transforms.size();++placement){const auto& world=record.instance_transforms[placement];if(!valid_light(record.light,world)){error="Crimson Light preview contains nonfinite or invalid authored properties";return false;}const MeshKey key{3u,record.source_entity,record.layout_key,index};const MeshKey occurrence{key.kind,key.first,key.second,static_cast<std::uint64_t>(index)<<32|placement};used_light_keys.insert(occurrence);auto& id=state_->preview_light_ids[occurrence];if(!id||occupied_ids.contains(id)){while(!state_->next_light_id||occupied_ids.contains(state_->next_light_id))--state_->next_light_id;id=state_->next_light_id--;}occupied_ids.insert(id);candidate->lights.push_back({id,record.light,world,candidate->preview->revision,candidate->preview->revision,true,true});}}
  for (const auto &record : candidate->source->meshes) {
    if (suppressed(suppression, record.entity_id)) continue;
    if (record.mesh.vertices.empty() && record.mesh.indices.empty()) continue;
    const MeshKey key{0u, record.mesh.mesh_id};
    used_geometry.insert(key);
    auto packed = state_->pack(key, {record.mesh}, error);
    if (!packed) return false;
    std::uint64_t mesh_id = 0u;
    if (!state_->append_mesh(*candidate, key, packed, record.mesh.primitives, mesh_id, error) ||
        !state_->append_instance(*candidate, {0u, record.entity_id}, mesh_id,
            record.material, record.world_transform, record.sampling_from_local,
            record.double_sided(),record.cast_shadows(), record.transform_revision,
            candidate->source->texture_mapping_revision, error)) return false;
    if(!record.entity_uuid.empty())candidate->native_instances[candidate->instances.back().id]={record.entity_uuid,{},native_source_instance_id(record.entity_uuid)};
  }
  for (const auto &record : candidate->source->instance_primitives) {
    if (record.mesh.vertices.empty() && record.mesh.indices.empty()) continue;
    if (std::ranges::none_of(record.occurrences, [&](const auto &occurrence) {
          return !suppressed(suppression, occurrence.entity_id, occurrence.entity_id,
                             record.definition_entity, record.member_entity);
        })) continue;
    const MeshKey geometry_key{0u, record.mesh.mesh_id};
    used_geometry.insert(geometry_key);
    auto packed = state_->pack(geometry_key, {record.mesh}, error);
    if (!packed) return false;
    const MeshKey key{1u, record.mesh.mesh_id, record.primitive.index_offset, record.primitive.index_count};
    std::uint64_t mesh_id = 0u;
    if (!state_->append_mesh(*candidate, key, packed, {&record.primitive, 1u}, mesh_id, error)) return false;
    for (const auto &occurrence : record.occurrences) {
      if (suppressed(suppression, occurrence.entity_id, occurrence.entity_id,
                     record.definition_entity, record.member_entity)) continue;
      if (!state_->append_instance(*candidate,
          {1u, occurrence.entity_id, occurrence.occurrence, mesh_id}, mesh_id,
          record.material, occurrence.world_transform, occurrence.sampling_from_local,
          record.key.double_sided,record.key.cast_shadows, candidate->source->transform_revision,
          candidate->source->texture_mapping_revision, error,occurrence.dimmed)) return false;
      if(!occurrence.entity_uuid.empty()){crimson::shared::Matrix4 relative;relative.values=record.member_from_definition.values;const auto member=record.definition_entity!=kInvalidEntityId?record.member_uuid:std::string{};candidate->native_instances[candidate->instances.back().id]={occurrence.entity_uuid,relative,native_source_instance_id(occurrence.entity_uuid,member,record.primitive.index_offset,record.primitive.index_count),member,record.primitive.index_offset,record.primitive.index_count};}
    }
  }
  if (candidate->preview) {
    for (std::size_t index = 0u; index < candidate->preview->meshes.size(); ++index) {
      const auto &record = candidate->preview->meshes[index];
      auto mesh = render_mesh_asset_snapshot(kInvalidAssetId, record.mesh, record.geometry_projection);
      if (!record.geometry_projection && record.dynamic_position_payload) mesh.positions = *record.dynamic_position_payload;
      if (mesh.vertices.empty() && mesh.indices.empty()) continue;
      MeshKey key{2u, record.source_entity, record.layout_key, index};
      if (record.suppress_source && record.source_entity != kInvalidEntityId) {
        const auto original = std::ranges::find(candidate->source->meshes,
            record.source_entity, &RenderSceneMeshRecord::entity_id);
        if (original != candidate->source->meshes.end()) {
          const MeshKey base_key{0u, original->mesh.mesh_id};
          // Reuse the effective source owner only when no sibling still draws
          // its committed geometry. Shared assets with a partial preview need
          // distinct position streams; their identical static streams still
          // share immutable pins through the family cache.
          if (!used_geometry.contains(base_key)) key = base_key;
        }
      }
      used_geometry.insert(key);
      auto packed = state_->pack(key, {mesh, record.position_revision, record.dynamic_position_payload}, error);
      if (!packed) return false;
      std::uint64_t mesh_id = 0u;
      if (!state_->append_mesh(*candidate, key, packed, mesh.primitives, mesh_id, error)) return false;
      const std::size_t count = record.instance_transforms.empty() ? 1u : record.instance_transforms.size();
      if (!record.instance_sampling_from_local.empty() && record.instance_sampling_from_local.size() != count) {
        error = "Crimson preview sampling and placement populations disagree";
        return false;
      }
      for (std::size_t placement = 0u; placement < count; ++placement) {
        const auto &world = record.instance_transforms.empty() ? record.transform : record.instance_transforms[placement];
        const auto &sampling = record.instance_sampling_from_local.empty() ? record.sampling_from_local : record.instance_sampling_from_local[placement];
        if (!state_->append_instance(*candidate, {2u, mesh_id, placement}, mesh_id,
            record.material, world, sampling, record.double_sided,record.cast_shadows,
            candidate->preview->revision, candidate->preview->revision, error)) return false;
      }
    }
  }
  std::set<std::uint64_t> mesh_ids;
  std::set<std::uint64_t> instance_ids;
  std::set<std::uint64_t> surface_ids;
  for (const auto &mesh : candidate->meshes) mesh_ids.insert(mesh.id);
  for (const auto &instance : candidate->instances) instance_ids.insert(instance.id);
  for (const auto &surface : candidate->surfaces) surface_ids.insert(surface.id);
  std::erase_if(state_->geometry, [&](const auto &entry) { return !used_geometry.contains(entry.first); });
  std::erase_if(state_->stream_families, [](const auto &entry) { return entry.second.expired(); });
  std::erase_if(state_->meshes, [&](const auto &entry) { return !mesh_ids.contains(entry.second.id); });
  std::erase_if(state_->instances, [&](const auto &entry) { return !instance_ids.contains(entry.second); });
  std::erase_if(state_->surfaces, [&](const auto &entry) { return !surface_ids.contains(entry.second.surface.id); });
  std::erase_if(state_->preview_light_ids,[&](const auto& entry){return !used_light_keys.contains(entry.first);});
  projection = std::move(candidate);
  transaction.committed = true;
  return true;
}

bool crimson_tool_preview_snapshots_equal(const RenderToolPreviewSceneSnapshot &left,
                            const RenderToolPreviewSceneSnapshot &right) {
    if (left.revision != right.revision || left.suppression.signature != right.suppression.signature ||
        left.suppression.entities != right.suppression.entities ||
        left.suppression.occurrences != right.suppression.occurrences ||
        left.meshes.size() != right.meshes.size() ||
        left.accepted_overlay_coordinates.size() != right.accepted_overlay_coordinates.size() ||
        left.curves.size()!=right.curves.size() || left.lights.size()!=right.lights.size() || left.group_markers.size()!=right.group_markers.size()) return false;
    const auto same_matrix = [](const QMat4 &first, const QMat4 &second) { return first.values == second.values; };
    const auto point=[](QVec3 a,QVec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
    const auto same_mesh = [&](const RenderToolPreviewMeshRecord &first, const RenderToolPreviewMeshRecord &second) {
      if (first.source_entity != second.source_entity || first.source_object_id != second.source_object_id ||
          first.visibility_entity != second.visibility_entity || first.layout_key != second.layout_key ||
          first.position_revision != second.position_revision || first.material_revision != second.material_revision ||
          first.mesh.geometry_payload() != second.mesh.geometry_payload() ||
          first.geometry_projection != second.geometry_projection ||
          first.dynamic_position_payload != second.dynamic_position_payload ||
          first.double_sided != second.double_sided || first.cast_shadows != second.cast_shadows || first.wireframe_on_shaded != second.wireframe_on_shaded || first.wireframe_on_shaded_color.has_value()!=second.wireframe_on_shaded_color.has_value() || (first.wireframe_on_shaded_color&&!point(*first.wireframe_on_shaded_color,*second.wireframe_on_shaded_color)) || !point(first.wireframe_tint,second.wireframe_tint) || first.suppress_source != second.suppress_source ||
          !same_matrix(first.transform, second.transform) ||
          !same_matrix(first.sampling_from_local, second.sampling_from_local) ||
          !std::ranges::equal(first.instance_transforms, second.instance_transforms, same_matrix) ||
          !std::ranges::equal(first.instance_sampling_from_local, second.instance_sampling_from_local, same_matrix) ||
          !render_material_payloads_equal(&first.material.primary_material, &second.material.primary_material) ||
          first.material.material_slots.size() != second.material.material_slots.size()) return false;
      for (std::size_t index = 0u; index < first.material.material_slots.size(); ++index) {
        const auto &first_slot = first.material.material_slots[index];
        const auto &second_slot = second.material.material_slots[index];
        if (first_slot.first != second_slot.first || !render_material_payloads_equal(&first_slot.second, &second_slot.second)) return false;
      }
      return true;
    };
    const auto same_curve=[&](const auto& a,const auto& b){return a.source_entity==b.source_entity&&a.visibility_entity==b.visibility_entity&&a.curve==b.curve&&a.curve_revision==b.curve_revision&&a.sample_payload==b.sample_payload&&a.material==b.material&&a.solidify==b.solidify&&a.solidify_resolution==b.solidify_resolution&&a.solidify_radius==b.solidify_radius&&a.suppress_source==b.suppress_source&&a.layout_key==b.layout_key&&std::ranges::equal(a.instance_transforms,b.instance_transforms,same_matrix)&&a.selection_dimension_bounds.has_value()==b.selection_dimension_bounds.has_value()&&(!a.selection_dimension_bounds||(point(a.selection_dimension_bounds->min,b.selection_dimension_bounds->min)&&point(a.selection_dimension_bounds->max,b.selection_dimension_bounds->max)));};
    const auto same_light=[&](const auto& a,const auto& b){return a.source_entity==b.source_entity&&a.visibility_entity==b.visibility_entity&&a.light==b.light&&a.suppress_source==b.suppress_source&&a.layout_key==b.layout_key&&std::ranges::equal(a.instance_transforms,b.instance_transforms,same_matrix);};
    const auto same_group=[&](const auto& a,const auto& b){return a.source_entity==b.source_entity&&point(a.local_min,b.local_min)&&point(a.local_max,b.local_max)&&a.layout_key==b.layout_key&&std::ranges::equal(a.instance_transforms,b.instance_transforms,same_matrix);};
    if(!std::ranges::equal(left.curves,right.curves,same_curve)||!std::ranges::equal(left.lights,right.lights,same_light)||!std::ranges::equal(left.group_markers,right.group_markers,same_group))return false;
    return std::ranges::equal(left.meshes, right.meshes, same_mesh) &&
           std::ranges::equal(left.accepted_overlay_coordinates, right.accepted_overlay_coordinates, same_mesh);
  }

CrimsonSceneProjectionStats CrimsonSceneProjectionCache::stats() const noexcept {
  return state_->counters;
}

void CrimsonSceneProjectionCache::clear() {
  const auto resources = state_->resources;
  state_ = std::make_unique<State>(resources);
}

} // namespace quader
