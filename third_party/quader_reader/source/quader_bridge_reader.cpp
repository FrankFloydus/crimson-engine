////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "quader_bridge_reader.hpp"

#include "assets/material_document.hpp"
#include "assets/material_diffuse_model.hpp"
#include "assets/mesh_collision_geometry_payload.hpp"
#include "core/qstr.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_material_catalog.hpp"
#include "io/project/project_material_reference_resolver.hpp"
#include "io/project/project_asset_resolver.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/shared_file_read.hpp"
#include "scene/light_properties.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/neutral/geometry/circular_uv_interpolation.hpp>
#include <quader/modeling/neutral/geometry/quad_uv_interpolation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace quader;

/** Owns strings independently of native value representations. */
struct SceneEntryStorage final {
  std::string id;
  std::string path;
};

/** Owns the exported, detached geometry of one concrete placement. */
struct MeshStorage final {
  std::string name;
  SceneMeshOccurrenceProjectionRecord placement;
  std::vector<QuaderBridgeVertex> vertices;
  std::vector<QuaderBridgeUvInterpolationVertex> uv_interpolation;
  std::vector<uint32_t> indices;
  std::vector<QuaderBridgeSurface> surfaces;
  std::vector<QuaderBridgeSourceFace> source_faces;
  uint32_t collision_status = QUADER_BRIDGE_COLLISION_DISABLED;
  std::shared_ptr<const std::vector<QuaderBridgeCollisionPoint>> collision_points;
};

/** Retains authored light values without applying a target renderer policy. */
struct LightStorage final {
  std::string name;
  EntityId entity = kInvalidEntityId;
  QMat4 world_transform;
  LightComponent light;
  bool effectively_visible = true;
};

/** One concrete hierarchy node, independent of definition storage ownership. */
struct NodeStorage final {
  std::string name;
  EntityId entity = kInvalidEntityId;
  EntityId occurrence_entity = kInvalidEntityId;
  EntityId parent_entity = kInvalidEntityId;
  EntityId parent_occurrence_entity = kInvalidEntityId;
  QMat4 world_transform;
  bool visible = true;
};

/** Retains the source descriptor without touching its image files. */
struct MaterialDescriptor final {
  std::string name;
  std::string source_relative_path;
  QuaderBridgeSourceFingerprint source_fingerprint{};
  ProjectStandardMaterialDefinition standard;
  std::array<std::string, QUADER_BRIDGE_TEXTURE_COUNT> texture_ids;
  std::array<std::string, QUADER_BRIDGE_TEXTURE_COUNT> texture_paths;
  std::array<uint32_t, QUADER_BRIDGE_TEXTURE_COUNT> texture_srgb{};
  std::array<bool, QUADER_BRIDGE_TEXTURE_COUNT> texture_present{};
};

/** Keeps identity available even if source material assets are unavailable. */
struct MaterialStorage final {
  MaterialReference reference;
  std::string id;
  std::unique_ptr<MaterialDescriptor> descriptor;
};

[[nodiscard]] std::string utf8_path(const std::filesystem::path &path) {
  const std::u8string value = path.generic_u8string();
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path native_path(const char *value) {
  const auto *begin = reinterpret_cast<const char8_t *>(value);
  return std::filesystem::path(std::u8string(begin, begin + std::strlen(value)));
}

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

[[noreturn]] void fail(const ProjectIoError &error) {
  fail(error.message + (error.path.empty() ? std::string{}
                                          : " [" + utf8_path(error.path) + "]"));
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    fail(std::string(message));
  }
}

/** Catches every native exception before returning through the C ABI. */
template <typename Callback>
int guarded(QuaderBridgeError *error, Callback &&callback) noexcept {
  if (error != nullptr) {
    error->message[0] = '\0';
  }
  const auto assign_error = [error](const char *message) noexcept {
    if (error != nullptr) {
      const std::size_t available = std::strlen(message);
      std::size_t length =
          (std::min)(available, sizeof(error->message) - 1u);
      // Preserve a complete UTF-8 prefix when a path-rich error is truncated.
      if (length != available) {
        while (length > 0u &&
               (static_cast<unsigned char>(message[length]) & 0xc0u) == 0x80u) {
          --length;
        }
      }
      std::memcpy(error->message, message, length);
      error->message[length] = '\0';
    }
  };
  try {
    std::forward<Callback>(callback)();
    return 1;
  } catch (const std::exception &exception) {
    assign_error(exception.what());
  } catch (...) {
    assign_error("Unknown native bridge reader failure.");
  }
  return 0;
}

[[nodiscard]] uint32_t count32(std::size_t value) {
  require(value < UINT32_MAX, "Bridge snapshot exceeds its supported size.");
  return static_cast<uint32_t>(value);
}


} // namespace

/** Validated immutable project catalog owned entirely by the reader DLL. */
struct QuaderBridgeProject final {
  quader::ProjectWorkspaceStorage storage;
  std::string id;
  std::vector<SceneEntryStorage> entries;
  uint32_t default_scene_index = QUADER_BRIDGE_NO_SCENE;
};

/** Independent read-only scene and all borrowed export views. */
struct QuaderBridgeScene final {
  quader::ProjectWorkspaceStorage storage;
  quader::QdrProjectReadResult source;
  std::string id;
  std::string relative_path;
  std::vector<MeshStorage> meshes;
  std::vector<LightStorage> lights;
  std::vector<NodeStorage> nodes;
  std::vector<MaterialStorage> materials;
  std::unordered_map<std::string, uint32_t> material_indices;
  std::optional<quader::ProjectMaterialCatalog> material_catalog;
};

/** Independent encoded texture allocation with its portable image extension. */
struct QuaderBridgeBlob final {
  std::vector<std::byte> bytes;
  std::string extension;
};

namespace {

/** Registers effective identity without reading material or texture files. */
[[nodiscard]] uint32_t material_index(QuaderBridgeScene &scene,
                                    MaterialReference reference) {
  if (!reference.valid()) {
    return QUADER_BRIDGE_NO_MATERIAL;
  }
  if (reference.project_owned()) {
    reference = MaterialReference::project(
        ProjectMaterialReferenceResolver::canonical_material_id(
            scene.storage.manifest, reference.project_asset()));
  }
  const std::string id = reference.project_owned()
                             ? std::string(reference.project_asset().str())
                             : std::to_string(reference.embedded_asset());
  const std::string key = (reference.project_owned() ? "project:" : "embedded:") + id;
  const auto found = scene.material_indices.find(key);
  if (found != scene.material_indices.end()) {
    return found->second;
  }
  const uint32_t index = count32(scene.materials.size());
  scene.materials.push_back({.reference = reference, .id = id});
  scene.material_indices.emplace(key, index);
  return index;
}

std::vector<QuaderBridgeUvInterpolationVertex>
build_circular_mesh_uv_interpolation(const MeshGeometryPayload &geometry) {
  if (geometry.circular_uv_interpolations.empty() || geometry.indices.empty() ||
      geometry.indices.size() % 3u != 0u ||
      std::ranges::any_of(geometry.indices, [&](auto index) {
        return index >= geometry.vertices.size();
      })) return {};
  std::vector<bool> valid_charts;
  valid_charts.reserve(geometry.circular_uv_interpolations.size());
  for (const auto &chart : geometry.circular_uv_interpolations) {
    valid_charts.push_back(quader_geometry::valid_circular_uv_frame(
        quader_geometry::QVec3f(chart.center),
        quader_geometry::QVec3f(chart.axis_u),
        quader_geometry::QVec3f(chart.axis_v), chart.uv_from_polar));
  }
  for (std::size_t triangle = 0u; triangle < geometry.indices.size(); triangle += 3u) {
    const std::array descriptors{
        geometry.vertices[geometry.indices[triangle]].circular_uv_index,
        geometry.vertices[geometry.indices[triangle + 1u]].circular_uv_index,
        geometry.vertices[geometry.indices[triangle + 2u]].circular_uv_index};
    if (descriptors[0] == descriptors[1] && descriptors[1] == descriptors[2]) continue;
    for (const auto descriptor : descriptors) {
      if (descriptor != 0u && descriptor <= valid_charts.size()) {
        valid_charts[descriptor - 1u] = false;
      }
    }
  }
  if (std::ranges::none_of(valid_charts, [](bool valid) { return valid; })) return {};
  std::vector<QuaderBridgeUvInterpolationVertex> result(geometry.vertices.size());
  for (std::size_t index = 0u; index < geometry.vertices.size(); ++index) {
    const auto &vertex = geometry.vertices[index];
    const auto descriptor = vertex.circular_uv_index;
    if (descriptor == 0u || descriptor > valid_charts.size() ||
        !valid_charts[descriptor - 1u]) continue;
    const auto &chart = geometry.circular_uv_interpolations[descriptor - 1u];
    const auto coordinates = quader_geometry::circular_uv_coordinates(
        quader_geometry::QVec3f(vertex.position),
        quader_geometry::QVec3f(chart.center),
        quader_geometry::QVec3f(chart.axis_u),
        quader_geometry::QVec3f(chart.axis_v));
    if (!coordinates) {
      valid_charts[descriptor - 1u] = false;
      continue;
    }
    result[index].mode = QUADER_BRIDGE_UV_INTERPOLATION_CIRCULAR;
    std::ranges::copy(*coordinates, result[index].coordinates);
    std::ranges::copy(chart.uv_from_polar, result[index].correction);
  }
  for (std::size_t index = 0u; index < result.size(); ++index) {
    if (result[index].mode != QUADER_BRIDGE_UV_INTERPOLATION_NONE &&
        !valid_charts[geometry.vertices[index].circular_uv_index - 1u]) {
      result[index] = {};
    }
  }
  if (std::ranges::none_of(result, [](const auto &vertex) {
        return vertex.mode == QUADER_BRIDGE_UV_INTERPOLATION_CIRCULAR;
      })) return {};
  return result;
}

std::optional<std::array<uint32_t, 4>>
quad_interpolation_corners(std::span<const uint32_t, 6> triangles) {
  const auto first = triangles.first<3>();
  const auto second = triangles.last<3>();
  for (const auto triangle : {first, second}) {
    if (triangle[0] == triangle[1] || triangle[0] == triangle[2] ||
        triangle[1] == triangle[2]) return {};
  }
  const auto unique_first = std::ranges::find_if(first, [&](auto index) {
    return std::ranges::find(second, index) == second.end();
  });
  const auto unique_second = std::ranges::find_if(second, [&](auto index) {
    return std::ranges::find(first, index) == first.end();
  });
  if (unique_first == first.end() || unique_second == second.end() ||
      std::ranges::count_if(first, [&](auto index) {
        return std::ranges::find(second, index) != second.end();
      }) != 2) return {};
  const auto first_corner = static_cast<std::size_t>(unique_first - first.begin());
  const auto second_corner = static_cast<std::size_t>(unique_second - second.begin());
  const std::array corners{*unique_first, first[(first_corner + 1u) % 3u],
                           *unique_second, first[(first_corner + 2u) % 3u]};
  if (second[(second_corner + 1u) % 3u] != corners[3] ||
      second[(second_corner + 2u) % 3u] != corners[1]) return {};
  return corners;
}

std::vector<QuaderBridgeUvInterpolationVertex>
build_mesh_uv_interpolation(const MeshGeometryPayload &geometry) {
  auto result = build_circular_mesh_uv_interpolation(geometry);
  if (std::ranges::none_of(geometry.primitives, [](const auto &primitive) {
        return primitive.index_count == 6u;
      })) return result;
  std::vector<std::size_t> owners(geometry.vertices.size(),
                                  std::numeric_limits<std::size_t>::max());
  std::vector<bool> shared(geometry.vertices.size(), false);
  for (std::size_t primitive_index = 0u; primitive_index < geometry.primitives.size();
       ++primitive_index) {
    const auto &primitive = geometry.primitives[primitive_index];
    const auto end = static_cast<std::uint64_t>(primitive.index_offset) + primitive.index_count;
    if (end > geometry.indices.size()) return result;
    for (std::uint64_t offset = primitive.index_offset; offset < end; ++offset) {
      const auto index = geometry.indices[static_cast<std::size_t>(offset)];
      if (index >= geometry.vertices.size()) return result;
      if (owners[index] == std::numeric_limits<std::size_t>::max()) {
        owners[index] = primitive_index;
      } else if (owners[index] != primitive_index) {
        shared[index] = true;
      }
    }
  }
  for (const auto &primitive : geometry.primitives) {
    if (primitive.index_count != 6u) continue;
    const auto corners = quad_interpolation_corners(
        std::span(geometry.indices).subspan(primitive.index_offset, 6u).first<6>());
    if (!corners) continue;
    std::array<quader_geometry::QVec3f, 4> positions{};
    std::array<quader_geometry::QVec2f, 4> uvs{};
    const auto &chart = geometry.vertices[corners->front()];
    bool eligible = true;
    for (std::size_t corner = 0u; corner < corners->size(); ++corner) {
      const auto index = (*corners)[corner];
      const auto &vertex = geometry.vertices[index];
      if (shared[index] || vertex.circular_uv_index != 0u || vertex.source_vertex_id == 0u ||
          vertex.uv_carrier != chart.uv_carrier ||
          vertex.uv_geometric_normal.x != chart.uv_geometric_normal.x ||
          vertex.uv_geometric_normal.y != chart.uv_geometric_normal.y ||
          vertex.uv_geometric_normal.z != chart.uv_geometric_normal.z ||
          !std::isfinite(vertex.uv_geometric_normal.x) ||
          !std::isfinite(vertex.uv_geometric_normal.y) ||
          !std::isfinite(vertex.uv_geometric_normal.z) ||
          !std::ranges::all_of(vertex.uv_carrier, [](float value) { return std::isfinite(value); })) {
        eligible = false;
        break;
      }
      for (std::size_t previous = 0u; previous < corner; ++previous) {
        if (vertex.source_vertex_id == geometry.vertices[(*corners)[previous]].source_vertex_id) {
          eligible = false;
        }
      }
      positions[corner] = quader_geometry::QVec3f(vertex.position);
      uvs[corner] = quader_geometry::QVec2f(vertex.uv);
    }
    if (!eligible) continue;
    const auto interpolation = quader_geometry::build_quad_uv_interpolation(positions, uvs);
    if (!interpolation) continue;
    if (result.empty()) result.resize(geometry.vertices.size());
    for (std::size_t corner = 0u; corner < corners->size(); ++corner) {
      const auto coordinates = interpolation->coordinates[corner];
      result[(*corners)[corner]] = {
          .mode = QUADER_BRIDGE_UV_INTERPOLATION_QUAD,
          .coordinates = {coordinates.x, coordinates.y, coordinates.z, 1.0f},
          .correction = {interpolation->geometry_warp.x, interpolation->geometry_warp.y,
                         interpolation->uv_correction.x, interpolation->uv_correction.y}};
    }
  }
  return result;
}

void build_mesh_snapshot(QuaderBridgeScene &target) {
  const SceneDocument &scene = target.source.document.scene();
  SceneMeshOccurrenceProjectionWorkStats work;
  const SceneMeshOccurrenceProjectionSnapshot occurrences =
      build_scene_mesh_occurrence_projection(
          scene, &work, SceneMeshOccurrenceVisibility::IncludeHidden);
  require(work.singular_definition_count == 0u,
          "Cannot export an instance whose definition has a singular transform.");
  target.meshes.reserve(occurrences.records().size());
  std::unordered_map<const MeshGeometryPayload *,
                     std::shared_ptr<const std::vector<QuaderBridgeCollisionPoint>>>
      collision_points_by_geometry;
  for (const SceneMeshOccurrenceProjectionRecord &placement : occurrences.records()) {
    const MeshComponent *component = scene.mesh(placement.geometry_entity);
    require(component != nullptr, "Mesh occurrence has no geometry binding.");
    const MeshAsset *source = target.source.document.assets().mesh(component->mesh);
    require(source != nullptr, "Cannot decode the source mesh geometry.");
    const std::optional<QMat4> sampling = effective_texture_sampling_from_local(
        scene, placement.geometry_entity,
        placement.formal_instance() ? placement.display_entity : kInvalidEntityId);
    require(sampling.has_value(), "Cannot resolve mesh occurrence texture mapping.");
    MeshAsset exported = *source;
    if (sampling->values != QMat4{}.values) {
      require(MeshProjectionService::initialize_compiled_texture_mapping(exported),
              "Cannot reconstruct the source mesh texture mapping.");
      MeshProjectionService::apply_mesh_texture_mapping(exported, *sampling);
    }
    const MeshGeometryPayload &geometry = exported.geometry();
    (void)count32(geometry.vertices.size());
    (void)count32(geometry.indices.size());
    require(scene_matrix_is_finite(placement.world_from_geometry),
            "Source mesh transform contains non-finite values.");
    MeshStorage mesh;
    const NameComponent *display_name = scene.name(placement.display_entity);
    const NameComponent *geometry_name = scene.name(placement.geometry_entity);
    const NameComponent *export_name =
        placement.formal_instance() ? geometry_name : display_name;
    mesh.name = export_name != nullptr ? export_name->name : "Mesh";
    mesh.placement = placement;
    if (component->generate_collisions) {
      mesh.collision_status = mesh_collision_supported(*component)
                                  ? QUADER_BRIDGE_COLLISION_TRIMESH_STATIC
                                  : QUADER_BRIDGE_COLLISION_UNSUPPORTED;
      if (mesh.collision_status == QUADER_BRIDGE_COLLISION_TRIMESH_STATIC) {
        // Derive from the original geometry, before detached UV/material
        // projection. Instances borrow one immutable local collision buffer.
        const MeshGeometryPayload &collision_source = source->geometry();
        auto found = collision_points_by_geometry.find(&collision_source);
        if (found == collision_points_by_geometry.end()) {
          const auto collision = mesh_collision_geometry_payload(collision_source);
          require(collision != nullptr, "Cannot generate the source mesh collision.");
          require(collision->error_message.empty(), collision->error_message);
          (void)count32(collision->faces.size());
          auto points = std::make_shared<std::vector<QuaderBridgeCollisionPoint>>();
          points->reserve(collision->faces.size());
          for (const QVec3 &point : collision->faces) {
            points->push_back({.position = {point.x, point.y, point.z}});
          }
          found = collision_points_by_geometry.emplace(&collision_source,
                                                       std::move(points)).first;
        }
        mesh.collision_points = found->second;
      }
    }
    mesh.uv_interpolation = build_mesh_uv_interpolation(source->geometry());
    mesh.vertices.reserve(geometry.vertices.size());
    for (const MeshVertex &vertex : geometry.vertices) {
      QuaderBridgeVertex output{
          .position = {vertex.position.x, vertex.position.y, vertex.position.z},
          .normal = {vertex.normal.x, vertex.normal.y, vertex.normal.z},
          .uv = {vertex.uv.x, vertex.uv.y}};
      const auto finite = [](float value) { return std::isfinite(value); };
      require(std::ranges::all_of(output.position, finite) &&
                  std::ranges::all_of(output.normal, finite) &&
                  std::ranges::all_of(output.uv, finite),
              "Source mesh has non-finite vertex attributes.");
      mesh.vertices.push_back(output);
    }
    const MaterialComponent *binding = scene.material(placement.geometry_entity);
    const std::vector<MeshPrimitiveSpan> spans = normalized_mesh_primitive_spans(exported);
    std::unordered_map<uint32_t, std::size_t> surface_by_material;
    std::vector<std::vector<uint32_t>> surface_indices;
    std::vector<std::vector<uint32_t>> surface_triangle_sources;
    for (const MeshPrimitiveSpan &span : spans) {
      require(span.index_count % 3u == 0u,
              "Source material surface does not contain complete triangles.");
      const uint32_t material = material_index(
          target, binding != nullptr ? binding->material_for_slot(span.material_slot)
                                     : MaterialReference{});
      auto [found, inserted] = surface_by_material.emplace(material, surface_indices.size());
      if (inserted) {
        surface_indices.emplace_back();
        surface_triangle_sources.emplace_back();
        mesh.surfaces.push_back({.material_index = material});
      }
      std::vector<uint32_t> &indices = surface_indices[found->second];
      for(uint32_t offset=0;offset<span.index_count;offset+=3)
        surface_triangle_sources[found->second].push_back((span.index_offset+offset)/3u);
      for (uint32_t offset = 0u; offset < span.index_count; ++offset) {
        const uint32_t index = geometry.indices[span.index_offset + offset];
        require(index < geometry.vertices.size(), "Source mesh index is out of bounds.");
        indices.push_back(index);
      }
    }
    mesh.indices.reserve(geometry.indices.size());
    std::vector<uint32_t> triangle_remap(geometry.indices.size()/3u,UINT32_MAX);
    for (std::size_t index = 0u; index < mesh.surfaces.size(); ++index) {
      mesh.surfaces[index].index_offset = count32(mesh.indices.size());
      mesh.surfaces[index].index_count = count32(surface_indices[index].size());
      for(std::size_t triangle=0;triangle<surface_triangle_sources[index].size();++triangle)
        triangle_remap[surface_triangle_sources[index][triangle]]=mesh.surfaces[index].index_offset/3u+count32(triangle);
      mesh.indices.insert(mesh.indices.end(), surface_indices[index].begin(),
                          surface_indices[index].end());
    }
    for(const auto& primitive:geometry.primitives)if(primitive.index_count==6u&&
        std::uint64_t(primitive.index_offset)+6u<=geometry.indices.size()) {
      const auto corners=quad_interpolation_corners(std::span(geometry.indices).subspan(primitive.index_offset,6u).first<6>());
      if(!corners)continue;
      const auto first=triangle_remap[primitive.index_offset/3u],second=triangle_remap[primitive.index_offset/3u+1u];
      if(first==UINT32_MAX||second==UINT32_MAX)continue;
      mesh.source_faces.push_back({.vertices={(*corners)[0],(*corners)[1],(*corners)[2],(*corners)[3]},
          .triangles={first,second},.corner_count=4u});
    }
    target.meshes.push_back(std::move(mesh));
  }
  (void)count32(target.meshes.size());
  (void)count32(target.materials.size());
}

void build_light_snapshot(QuaderBridgeScene &target) {
  const SceneDocument &scene = target.source.document.scene();
  const auto hierarchy = scene.hierarchy_projection();
  for (const SceneHierarchyProjectionRecord &placement : hierarchy->records()) {
    const LightComponent *component = scene.light(placement.entity);
    if (component == nullptr ||
        placement.containing_instance_definition != kInvalidEntityId ||
        scene.instance_occurrence(placement.entity) != nullptr) {
      continue;
    }
    const NameComponent *name = scene.name(placement.entity);
    target.lights.push_back({
        .name = name != nullptr ? name->name : "Light",
        .entity = placement.entity,
        .world_transform = placement.world_transform,
        .light = *component,
        .effectively_visible = placement.effectively_visible});
  }
  (void)count32(target.lights.size());
}

/** Preserves local flags so toggling an imported group reveals its children. */
[[nodiscard]] bool node_is_locally_visible(const SceneDocument &scene,
                                         EntityId entity) {
  if (const VisibilityComponent *visibility = scene.visibility(entity);
      visibility != nullptr && !visibility->visible) {
    return false;
  }
  const SceneLayerId layer_id = scene.entity_layer(entity);
  const SceneLayerRecord *layer = scene.layer(layer_id);
  return layer_id == kInvalidSceneLayerId || (layer != nullptr && layer->visible);
}

void build_node_snapshot(QuaderBridgeScene &target) {
  const SceneDocument &scene = target.source.document.scene();
  const auto hierarchy = scene.hierarchy_projection();
  std::unordered_map<EntityId,
                     std::vector<const SceneHierarchyProjectionRecord *>> members;
  for (const SceneHierarchyProjectionRecord &placement : hierarchy->records()) {
    if (placement.containing_instance_definition != kInvalidEntityId &&
        placement.entity != placement.containing_instance_definition) {
      members[placement.containing_instance_definition].push_back(&placement);
    }
  }
  const auto append = [&](const SceneHierarchyProjectionRecord &placement,
                          EntityId occurrence, EntityId parent,
                          EntityId parent_occurrence, const QMat4 &world) {
    require(scene_matrix_is_finite(world),
            "Source hierarchy transform contains non-finite values.");
    const NameComponent *name = scene.name(placement.entity);
    target.nodes.push_back({
        .name = name != nullptr ? name->name : "Entity",
        .entity = placement.entity,
        .occurrence_entity = occurrence,
        .parent_entity = parent,
        .parent_occurrence_entity = parent_occurrence,
        .world_transform = world,
        .visible = node_is_locally_visible(scene, placement.entity)});
  };
  for (const SceneHierarchyProjectionRecord &placement : hierarchy->records()) {
    if (placement.containing_instance_definition != kInvalidEntityId ||
        scene.is_instance_definition_entity(placement.entity)) {
      continue;
    }
    append(placement, kInvalidEntityId, placement.parent, kInvalidEntityId,
           placement.world_transform);
    const InstanceOccurrenceComponent *occurrence =
        scene.instance_occurrence(placement.entity);
    if (occurrence == nullptr) {
      continue;
    }
    const SceneHierarchyProjectionRecord *definition =
        hierarchy->find(occurrence->definition);
    require(definition != nullptr, "Instance hierarchy has no definition.");
    const auto definition_from_world =
        try_scene_matrix_inverse(definition->world_transform);
    require(definition_from_world.has_value(),
            "Cannot export an instance whose definition has a singular transform.");
    for (const SceneHierarchyProjectionRecord *member : members[occurrence->definition]) {
      const bool root_member = member->parent == occurrence->definition;
      append(*member, placement.entity,
             root_member ? placement.entity : member->parent,
             root_member ? kInvalidEntityId : placement.entity,
             scene_transform_multiply(
                 placement.world_transform,
                 scene_transform_multiply(*definition_from_world,
                                          member->world_transform)));
    }
  }
  (void)count32(target.nodes.size());
}

[[nodiscard]] ProjectMaterialCatalogEntry read_project_material(
    QuaderBridgeScene &scene, ProjectAssetId id) {
  std::optional<ProjectIoError> stale_dependency_error;
  for (const ProjectAssetReference &dependency : scene.source.project_asset_dependencies) {
    if (dependency.stable_id != id) {
      continue;
    }
    require(dependency.kind == ProjectAssetKind::Material,
            "Source material dependency has an incompatible asset kind.");
    auto decoded = QmatCodec::read(scene.storage.file_system, dependency.path);
    if (!decoded.ok()) {
      // A moved asset can leave an older scene's path behind. Other read errors
      // retain their meaning: malformed files and unsafe paths never fall back.
      if (decoded.error().code != ProjectIoErrorCode::FileNotFound) {
        fail(decoded.error());
      }
      stale_dependency_error = decoded.error();
      break;
    }
    if (decoded.value().material.id != id) {
      stale_dependency_error = ProjectIoError{
          .code = ProjectIoErrorCode::AssetNotFound,
          .message = "Source material identity does not match its scene dependency.",
          .path = dependency.path.native_path()};
      break;
    }
    return {.material = std::move(decoded.value().material),
            .path = dependency.path,
            .file_size = decoded.value().file_size,
            .content_revision = decoded.value().content_revision};
  }
  // Legacy, relocated and permanently remapped references share one lazy scan.
  // The catalog validates identity uniqueness and project-path containment.
  if (!scene.material_catalog.has_value()) {
    auto catalog = ProjectMaterialCatalog::scan(
        scene.storage.file_system, nullptr, &scene.storage.manifest);
    if (!catalog.ok()) {
      fail(catalog.error());
    }
    scene.material_catalog = catalog.take_value();
  }
  const ProjectMaterialCatalogEntry *entry = scene.material_catalog->find(id);
  if (entry == nullptr) {
    if (stale_dependency_error.has_value()) {
      fail(*stale_dependency_error);
    }
    fail("Source project material is unavailable: " + std::string(id.str()));
  }
  return *entry;
}

[[nodiscard]] MaterialDescriptor &material_descriptor(QuaderBridgeScene &scene,
                                                      uint32_t index) {
  require(index < scene.materials.size(), "Material index is out of bounds.");
  MaterialStorage &storage = scene.materials[index];
  if (storage.descriptor) {
    return *storage.descriptor;
  }
  auto descriptor = std::make_unique<MaterialDescriptor>();
  if (storage.reference.project_owned()) {
    const ProjectMaterialCatalogEntry decoded =
        read_project_material(scene, storage.reference.project_asset());
    const ProjectMaterialDocument &material = decoded.material;
    const ProjectStandardMaterialDefinition *standard = material.standard_material();
    require(standard != nullptr, "Only Standard source materials are supported.");
    descriptor->name = material.name;
    descriptor->source_relative_path = decoded.path.portable_string();
    descriptor->source_fingerprint = {
        .byte_count = static_cast<uint64_t>(decoded.file_size),
        .content_hash = decoded.content_revision};
    descriptor->standard = *standard;
    for (uint32_t slot = 0u; slot < QUADER_BRIDGE_TEXTURE_COUNT; ++slot) {
      const auto *texture = standard->texture(static_cast<StandardMaterialTextureSlot>(slot));
      if (texture != nullptr && texture->has_value()) {
        if (scene.storage.manifest.is_texture_deleted((*texture)->id)) {
          descriptor->standard.texture(static_cast<StandardMaterialTextureSlot>(slot))->reset();
          continue;
        }
        auto hint = ProjectRelativePath::parse_portable((*texture)->project_relative_path);
        if (!hint.ok()) {
          fail(hint.error());
        }
        auto path = resolve_project_texture_path(
            scene.storage.manifest, (*texture)->id, hint.value());
        if (!path.ok()) {
          fail(path.error());
        }
        const auto *catalog_texture = scene.storage.manifest.find_texture((*texture)->id);
        const ProjectAssetId canonical_id =
            catalog_texture != nullptr ? catalog_texture->id : (*texture)->id;
        descriptor->texture_present[slot] = true;
        descriptor->texture_ids[slot] = std::string(canonical_id.str());
        descriptor->texture_paths[slot] = path.value().portable_string();
        descriptor->texture_srgb[slot] =
            (*texture)->color_space == ProjectTextureColorSpace::Srgb ? 1u : 0u;
      }
    }
  } else {
    const AssetRegistry &assets = scene.source.document.assets();
    const MaterialDocument *material = assets.material(storage.reference.embedded_asset());
    require(material != nullptr, "Source embedded material is unavailable.");
    require(unsupported_material_texture_slots(*material).empty(),
            "Source embedded material has unsupported texture inputs.");
    descriptor->name = material->name;
    descriptor->source_relative_path = scene.relative_path;
    descriptor->source_fingerprint = {
        .byte_count = scene.source.source_byte_count,
        .content_hash = scene.source.source_content_hash};
    descriptor->standard.base_color =
        {material->base_color.x, material->base_color.y, material->base_color.z};
    descriptor->standard.roughness = material->roughness;
    descriptor->standard.metallic = material->metallic;
    descriptor->standard.diffuse_model = material->diffuse_model;
    constexpr std::array<const char *, QUADER_BRIDGE_TEXTURE_COUNT> kSlots{
        material_texture_slot::kBaseColor, material_texture_slot::kRoughness,
        material_texture_slot::kMetallic, material_texture_slot::kNormal};
    for (uint32_t slot = 0u; slot < QUADER_BRIDGE_TEXTURE_COUNT; ++slot) {
      const std::optional<AssetId> id = material_texture_slot_asset(*material, kSlots[slot]);
      if (!id.has_value()) {
        continue;
      }
      const TextureAsset *texture = assets.texture(*id);
      require(texture != nullptr, "Source embedded texture is unavailable.");
      descriptor->texture_present[slot] = true;
      descriptor->texture_ids[slot] = std::to_string(*id);
      descriptor->texture_paths[slot] = utf8_path(texture->source_path);
      descriptor->texture_srgb[slot] =
          texture->color_space == TextureColorSpace::Srgb ? 1u : 0u;
    }
  }
  storage.descriptor = std::move(descriptor);
  return *storage.descriptor;
}

int open_project(const char *path, QuaderBridgeProject **out,
                 QuaderBridgeError *error) noexcept {
  if (out != nullptr) {
    *out = nullptr;
  }
  return guarded(error, [&] {
    require(path != nullptr && *path != '\0' && out != nullptr,
            "A project manifest path and output handle are required.");
    auto opened = ProjectManifestStore::open(native_path(path));
    if (!opened.ok()) {
      fail(opened.error());
    }
    auto project = std::make_unique<QuaderBridgeProject>();
    project->storage = opened.take_value();
    project->id = std::string(project->storage.manifest.id().str());
    const auto entries = project->storage.manifest.scenes();
    (void)count32(entries.size());
    project->entries.reserve(entries.size());
    const auto &active = project->storage.manifest.active_scene_path();
    for (const ProjectSceneCatalogEntry &entry : entries) {
      if (active.has_value() && entry.path == *active) {
        project->default_scene_index = count32(project->entries.size());
      }
      project->entries.push_back({.id = std::string(entry.id.str()),
                                  .path = entry.path.portable_string()});
    }
    if (!entries.empty() && project->default_scene_index == QUADER_BRIDGE_NO_SCENE) {
      project->default_scene_index = 0u;
    }
    *out = project.release();
  });
}

void release_project(QuaderBridgeProject *project) noexcept { delete project; }

int project_view(const QuaderBridgeProject *project, QuaderBridgeProjectView *out,
                 QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(project != nullptr && out != nullptr, "Project and output view are required.");
    *out = {.id = project->id.c_str(),
            .name = project->storage.manifest.name().c_str(),
            .scene_count = count32(project->entries.size()),
            .default_scene_index = project->default_scene_index};
  });
}

int scene_entry(const QuaderBridgeProject *project, uint32_t index,
                QuaderBridgeSceneEntry *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(project != nullptr && out != nullptr, "Project and output entry are required.");
    require(index < project->entries.size(), "Scene index is out of bounds.");
    *out = {.id = project->entries[index].id.c_str(),
            .relative_path = project->entries[index].path.c_str()};
  });
}

int open_scene(const QuaderBridgeProject *project, const char *scene_id,
               QuaderBridgeScene **out, QuaderBridgeError *error) noexcept {
  if (out != nullptr) {
    *out = nullptr;
  }
  return guarded(error, [&] {
    require(project != nullptr && scene_id != nullptr && out != nullptr,
            "Project, scene identity, and output handle are required.");
    const auto id = SceneId::parse(scene_id);
    if (!id.ok()) {
      fail(id.error());
    }
    require(project->storage.manifest.find_scene(id.value()) != nullptr,
            "Selected scene is not in the validated project catalog.");
    auto reopened = ProjectManifestStore::open(project->storage.manifest_path);
    if (!reopened.ok()) {
      fail(reopened.error());
    }
    require(reopened.value().manifest.id() == project->storage.manifest.id(),
            "Project identity changed since validation.");
    const ProjectSceneCatalogEntry *entry = reopened.value().manifest.find_scene(id.value());
    require(entry != nullptr, "Selected scene is no longer in the project catalog.");
    const auto path = reopened.value().file_system.resolve_existing_file(entry->path);
    if (!path.ok()) {
      fail(path.error());
    }
    auto scene = std::make_unique<QuaderBridgeScene>();
    scene->relative_path = entry->path.portable_string();
    scene->storage = reopened.take_value();
    scene->source = QdrDocumentReader::read_project_document(path.value());
    if (!scene->source.ok) {
      fail("Cannot read scene " + utf8_path(path.value()) + ": " + scene->source.message);
    }
    require(SceneId::from_bytes(scene->source.scene_uuid) == id.value(),
            "Scene identity does not match its project catalog entry.");
    scene->id = std::string(id.value().str());
    build_mesh_snapshot(*scene);
    build_light_snapshot(*scene);
    build_node_snapshot(*scene);
    *out = scene.release();
  });
}

void release_scene(QuaderBridgeScene *scene) noexcept { delete scene; }

int scene_view(const QuaderBridgeScene *scene, QuaderBridgeSceneView *out,
               QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and output view are required.");
    *out = {.id = scene->id.c_str(), .relative_path = scene->relative_path.c_str(),
            .mesh_count = count32(scene->meshes.size()),
            .material_count = count32(scene->materials.size()),
            .light_count = count32(scene->lights.size()),
            .camera_aperture = kLightPreviewAperture,
            .camera_shutter_seconds = kLightPreviewShutterSeconds,
            .camera_iso = kLightPreviewSensitivity,
            .node_count = count32(scene->nodes.size())};
  });
}

int node_view(const QuaderBridgeScene *scene, uint32_t index,
              QuaderBridgeNodeView *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr,
            "Scene and output hierarchy node are required.");
    require(index < scene->nodes.size(), "Hierarchy node index is out of bounds.");
    const NodeStorage &node = scene->nodes[index];
    *out = {.name = node.name.c_str(),
            .entity = node.entity,
            .occurrence_entity = node.occurrence_entity,
            .parent_entity = node.parent_entity,
            .parent_occurrence_entity = node.parent_occurrence_entity,
            .visible = node.visible ? 1u : 0u,
            .order = index};
    std::ranges::copy(node.world_transform.values, out->transform);
  });
}

int mesh_view(const QuaderBridgeScene *scene, uint32_t index,
              QuaderBridgeMeshView *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and output mesh are required.");
    require(index < scene->meshes.size(), "Mesh index is out of bounds.");
    const MeshStorage &mesh = scene->meshes[index];
    *out = {.name = mesh.name.c_str(),
            .display_entity = mesh.placement.display_entity,
            .geometry_entity = mesh.placement.geometry_entity,
            .definition_entity = mesh.placement.definition_entity,
            .visible = mesh.placement.effectively_visible ? 1u : 0u,
            .vertices = mesh.vertices.data(), .vertex_count = count32(mesh.vertices.size()),
            .indices = mesh.indices.data(), .index_count = count32(mesh.indices.size()),
            .surfaces = mesh.surfaces.data(), .surface_count = count32(mesh.surfaces.size()),
            .collision_status = mesh.collision_status,
            .collision_points = mesh.collision_points ? mesh.collision_points->data() : nullptr,
            .collision_point_count = mesh.collision_points
                                         ? count32(mesh.collision_points->size()) : 0u};
    std::ranges::copy(mesh.placement.world_from_geometry.values, out->transform);
  });
}

int mesh_uv_interpolation_view(const QuaderBridgeScene *scene, uint32_t mesh_index,
                               QuaderBridgeMeshUvInterpolationView *out,
                               QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr,
            "Scene and UV interpolation output are required.");
    require(mesh_index < scene->meshes.size(), "Mesh index is out of bounds.");
    const auto &vertices = scene->meshes[mesh_index].uv_interpolation;
    *out = {.vertices = vertices.empty() ? nullptr : vertices.data(),
            .vertex_count = count32(vertices.size())};
  });
}

int light_view(const QuaderBridgeScene *scene, uint32_t index,
               QuaderBridgeLightView *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and output light are required.");
    require(index < scene->lights.size(), "Light index is out of bounds.");
    const LightStorage &source = scene->lights[index];
    const LightComponent &light = source.light;
    uint32_t type = QUADER_BRIDGE_LIGHT_DIRECTIONAL;
    switch (light.type) {
    case LightType::Directional:
      type = QUADER_BRIDGE_LIGHT_DIRECTIONAL;
      break;
    case LightType::Point:
      type = QUADER_BRIDGE_LIGHT_POINT;
      break;
    case LightType::Spot:
      type = QUADER_BRIDGE_LIGHT_SPOT;
      break;
    default:
      fail("Source light has an unsupported type.");
    }
    require(scene_matrix_is_finite(source.world_transform),
            "Source light transform contains non-finite values.");
    require(std::isfinite(light.color.x) && std::isfinite(light.color.y) &&
                std::isfinite(light.color.z) && std::isfinite(light.intensity) &&
                std::isfinite(light.range) && std::isfinite(light.distance_attenuation),
            "Source light contains non-finite lighting values.");
    const LightEngineValues engine = light_engine_values(light);
    *out = {.name = source.name.c_str(),
            .entity = source.entity,
            .type = type,
            .visible = source.effectively_visible ? 1u : 0u,
            .color = {light.color.x, light.color.y, light.color.z},
            .intensity = light.intensity,
            .range = light.range,
            .distance_attenuation = light.distance_attenuation,
            .casts_shadows = light.casts_shadows ? 1u : 0u,
            .preview_energy = engine.preview_energy,
            .illuminance_lux = engine.illuminance_lux,
            .luminous_intensity_candela = engine.luminous_intensity_candela};
    std::ranges::copy(source.world_transform.values, out->transform);
  });
}

int material_identity(const QuaderBridgeScene *scene, uint32_t index,
                      QuaderBridgeMaterialIdentity *out,
                      QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and material identity are required.");
    require(index < scene->materials.size(), "Material index is out of bounds.");
    const MaterialStorage &material = scene->materials[index];
    *out = {.id = material.id.c_str(),
            .kind = static_cast<uint32_t>(material.reference.project_owned()
                                              ? QUADER_BRIDGE_MATERIAL_PROJECT
                                              : QUADER_BRIDGE_MATERIAL_EMBEDDED)};
  });
}

/** Maps the authored material choice without exposing native enum layout. */
[[nodiscard]] uint32_t material_diffuse_model(MaterialDiffuseModel model) {
  switch (model) {
  case MaterialDiffuseModel::Burley:
    return QUADER_BRIDGE_MATERIAL_DIFFUSE_BURLEY;
  case MaterialDiffuseModel::Lambert:
    return QUADER_BRIDGE_MATERIAL_DIFFUSE_LAMBERT;
  }
  fail("Source material has an unsupported diffuse model.");
}

int material_view(QuaderBridgeScene *scene, uint32_t index,
                  QuaderBridgeMaterialView *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and material view are required.");
    const MaterialDescriptor &material = material_descriptor(*scene, index);
    const ProjectStandardMaterialDefinition &standard = material.standard;
    *out = {.name = material.name.c_str(),
            .source_relative_path = material.source_relative_path.c_str(),
            .roughness = standard.roughness, .metallic = standard.metallic,
            .normal_strength = standard.normal_strength,
            .normal_directx = standard.normal_convention == NormalMapConvention::DirectX ? 1u : 0u,
            .uv_rotation_degrees = standard.texture_transform.rotation_degrees,
            .diffuse_model = material_diffuse_model(standard.diffuse_model)};
    std::ranges::copy(standard.base_color, out->base_color);
    std::ranges::copy(standard.texture_transform.center, out->uv_center);
    std::ranges::copy(standard.texture_transform.scale, out->uv_scale);
    std::ranges::copy(standard.texture_transform.offset, out->uv_offset);
    for (uint32_t slot = 0u; slot < QUADER_BRIDGE_TEXTURE_COUNT; ++slot) {
      out->textures[slot] = {.present = material.texture_present[slot] ? 1u : 0u,
                             .id = material.texture_ids[slot].c_str(),
                             .source_path = material.texture_paths[slot].c_str(),
                             .srgb = material.texture_srgb[slot]};
    }
  });
}

int read_texture(QuaderBridgeScene *scene, uint32_t material_index_value,
                 uint32_t slot, QuaderBridgeBlob **out,
                 QuaderBridgeError *error) noexcept {
  if (out != nullptr) {
    *out = nullptr;
  }
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and texture output are required.");
    require(slot < QUADER_BRIDGE_TEXTURE_COUNT, "Texture slot is out of bounds.");
    const MaterialDescriptor &material = material_descriptor(*scene, material_index_value);
    require(material.texture_present[slot], "Source material has no texture in this slot.");
    const std::string &source_path = material.texture_paths[slot];
    require(!source_path.empty(), "Source texture has no encoded file (generated textures are unsupported).");
    const std::filesystem::path source = native_path(source_path.c_str());
    auto relative = source.is_absolute()
                        ? scene->storage.file_system.relative_from_absolute(source)
                        : ProjectRelativePath::parse(source);
    if (!relative.ok()) {
      fail(relative.error());
    }
    auto resolved = scene->storage.file_system.resolve_existing_file(relative.value());
    if (!resolved.ok()) {
      fail(resolved.error());
    }
    const std::filesystem::path &path = resolved.value();
    auto blob = std::make_unique<QuaderBridgeBlob>();
    blob->extension = QStr::to_ascii_lower(utf8_path(path.extension()));
    require(!blob->extension.empty(), "Source texture has no image file extension.");
    constexpr uint64_t kMaximumBytes = 64u * 1024u * 1024u;
    auto read = quader_io::read_file_bytes_shared(path, kMaximumBytes);
    require(read.error != quader_io::SharedFileReadError::Limit,
            "Source texture is empty or exceeds the 64 MiB supported size.");
    if (!read.ok) {
      fail("Cannot read source texture " + utf8_path(path) + ": " + read.message);
    }
    require(!read.bytes.empty(),
            "Source texture is empty or exceeds the 64 MiB supported size.");
    blob->bytes = std::move(read.bytes);
    *out = blob.release();
  });
}

int blob_view(const QuaderBridgeBlob *blob, QuaderBridgeBlobView *out,
              QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(blob != nullptr && out != nullptr, "Texture blob and output view are required.");
    *out = {.bytes = reinterpret_cast<const uint8_t *>(blob->bytes.data()),
            .byte_count = blob->bytes.size(),
            .extension = blob->extension.c_str()};
  });
}

void release_blob(QuaderBridgeBlob *blob) noexcept { delete blob; }

int scene_source_fingerprint(const QuaderBridgeScene *scene,
                             QuaderBridgeSourceFingerprint *out,
                             QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr,
            "Scene and source fingerprint output are required.");
    *out = {.byte_count = scene->source.source_byte_count,
            .content_hash = scene->source.source_content_hash};
  });
}

int material_source_fingerprint(QuaderBridgeScene *scene, uint32_t index,
                                QuaderBridgeSourceFingerprint *out,
                                QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr,
            "Scene and material fingerprint output are required.");
    *out = material_descriptor(*scene, index).source_fingerprint;
  });
}

constexpr QuaderBridgeApi kApi{
    .abi_version = QUADER_BRIDGE_ABI_VERSION,
    .struct_size = sizeof(QuaderBridgeApi),
    .open_project = open_project, .release_project = release_project,
    .project_view = project_view, .scene_entry = scene_entry,
    .open_scene = open_scene, .release_scene = release_scene,
    .scene_view = scene_view, .mesh_view = mesh_view,
    .material_identity = material_identity, .material_view = material_view,
    .read_texture = read_texture, .blob_view = blob_view, .release_blob = release_blob,
    .scene_source_fingerprint = scene_source_fingerprint,
    .material_source_fingerprint = material_source_fingerprint,
    .light_view = light_view,
    .node_view = node_view};

constexpr QuaderBridgeUvInterpolationApi kUvInterpolationApi{
    .abi_version = QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION,
    .struct_size = sizeof(QuaderBridgeUvInterpolationApi),
    .mesh_uv_interpolation_view = mesh_uv_interpolation_view};

} // namespace

extern "C" QUADER_BRIDGE_EXPORT const QuaderBridgeApi *
quader_bridge_get_api(uint32_t requested_version) {
  return requested_version == QUADER_BRIDGE_ABI_VERSION ? &kApi : nullptr;
}

extern "C" QUADER_BRIDGE_EXPORT const QuaderBridgeUvInterpolationApi *
quader_bridge_get_uv_interpolation_api(uint32_t requested_abi_version) {
  return requested_abi_version == QUADER_BRIDGE_UV_INTERPOLATION_ABI_VERSION
             ? &kUvInterpolationApi : nullptr;
}

extern "C" QUADER_BRIDGE_EXPORT const QuaderBridgeTopologyApi *
quader_bridge_get_topology_api(uint32_t requested_abi_version) {
  static constexpr QuaderBridgeTopologyApi api{
    .abi_version=QUADER_BRIDGE_TOPOLOGY_ABI_VERSION,.struct_size=sizeof(QuaderBridgeTopologyApi),
    .mesh_topology_view=[](const QuaderBridgeScene* scene,uint32_t index,QuaderBridgeTopologyView* out,QuaderBridgeError* error)->int {
      try {
        require(scene&&out&&index<scene->meshes.size(),"Invalid mesh topology request.");
        const auto& faces=scene->meshes[index].source_faces;
        out->faces=faces.empty()?nullptr:faces.data();out->face_count=count32(faces.size());return 1;
      } catch(const std::exception& exception) {
        if(error){const auto text=std::string(exception.what());std::strncpy(error->message,text.c_str(),sizeof(error->message)-1);error->message[sizeof(error->message)-1]='\0';}return 0;
      }
    }};
  return requested_abi_version==QUADER_BRIDGE_TOPOLOGY_ABI_VERSION?&api:nullptr;
}
