////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "command_observable_state.hpp"

#include "api/quader_editor_api.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader::test {
namespace {

constexpr std::array<CommandObservableDomain,
                     kCommandObservableDomainCount>
    kObservableDomains = {
        CommandObservableDomain::Documents,
        CommandObservableDomain::Scene,
        CommandObservableDomain::Geometry,
        CommandObservableDomain::Selection,
        CommandObservableDomain::Overlay,
        CommandObservableDomain::Assets,
        CommandObservableDomain::History,
        CommandObservableDomain::Tools,
        CommandObservableDomain::Viewports,
        CommandObservableDomain::Preferences,
        CommandObservableDomain::Revisions,
};

std::size_t domain_index(CommandObservableDomain domain) {
  const auto found =
      std::find(kObservableDomains.begin(), kObservableDomains.end(), domain);
  if (found == kObservableDomains.end()) {
    throw std::out_of_range("Unknown command observable domain.");
  }
  return static_cast<std::size_t>(found - kObservableDomains.begin());
}

std::string bool_text(bool value) { return value ? "true" : "false"; }

template <typename Enum>
std::string enum_text(Enum value) {
  using Underlying = std::underlying_type_t<Enum>;
  return std::to_string(static_cast<Underlying>(value));
}

template <typename UInt>
std::string fixed_hex(UInt value) {
  static_assert(std::is_unsigned_v<UInt>);
  std::ostringstream stream;
  stream << std::hex << std::setfill('0')
         << std::setw(static_cast<int>(sizeof(UInt) * 2u)) << value;
  return stream.str();
}

std::string float_bits(float value) {
  return "f32:" + fixed_hex(std::bit_cast<std::uint32_t>(value));
}

std::string double_bits(double value) {
  return "f64:" + fixed_hex(std::bit_cast<std::uint64_t>(value));
}

std::string vec2_text(QVec2 value) {
  return "(" + float_bits(value.x) + "," + float_bits(value.y) + ")";
}

std::string vec3_text(QVec3 value) {
  return "(" + float_bits(value.x) + "," + float_bits(value.y) + "," +
         float_bits(value.z) + ")";
}

std::string transform_text(const QuaderTransform &value) {
  return "position:" + vec3_text(value.position) + ",rotation:" +
         vec3_text(value.rotation_euler) + ",scale:" +
         vec3_text(value.scale);
}

std::string matrix_text(const QMat4 &value) {
  std::string result = "(";
  for (std::size_t index = 0; index < value.values.size(); ++index) {
    if (index != 0u) {
      result += ',';
    }
    result += float_bits(value.values[index]);
  }
  result += ')';
  return result;
}

std::string quote(std::string_view value) {
  std::ostringstream stream;
  stream << '"';
  for (const unsigned char character : value) {
    switch (character) {
    case '\\':
      stream << "\\\\";
      break;
    case '"':
      stream << "\\\"";
      break;
    case '\n':
      stream << "\\n";
      break;
    case '\r':
      stream << "\\r";
      break;
    case '\t':
      stream << "\\t";
      break;
    default:
      if (character < 0x20u || character == 0x7fu) {
        stream << "\\x" << std::hex << std::setfill('0') << std::setw(2)
               << static_cast<unsigned int>(character) << std::dec;
      } else {
        stream << static_cast<char>(character);
      }
      break;
    }
  }
  stream << '"';
  return stream.str();
}

std::string path_segment(std::string_view value) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const unsigned char character : value) {
    const bool ascii_alphanumeric =
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9');
    if (ascii_alphanumeric || character == '-' || character == '_' ||
        character == '.') {
      stream << static_cast<char>(character);
    } else {
      stream << '%' << std::setw(2) << static_cast<unsigned int>(character);
    }
  }
  return stream.str();
}

std::string portable_filename(std::string_view source_path) {
  if (source_path.empty()) {
    return {};
  }
  std::filesystem::path path{std::string(source_path)};
  return path.filename().generic_string();
}

std::string stable_fingerprint(std::string_view value) {
  // Two independent 64-bit byte accumulators keep the baseline compact while
  // avoiding implementation-defined std::hash behavior.
  std::uint64_t first = 14695981039346656037ull;
  std::uint64_t second = 0x9e3779b97f4a7c15ull;
  for (const unsigned char byte : value) {
    first ^= byte;
    first *= 1099511628211ull;
    second ^= static_cast<std::uint64_t>(byte) + 0x9e3779b97f4a7c15ull +
              (second << 6u) + (second >> 2u);
    second *= 0xbf58476d1ce4e5b9ull;
  }
  first ^= static_cast<std::uint64_t>(value.size());
  second ^= static_cast<std::uint64_t>(value.size()) *
            0x94d049bb133111ebull;
  return fixed_hex(first) + fixed_hex(second);
}

/// Writes a stable line-oriented payload without locale-sensitive formatting.
class CanonicalWriter final {
public:
  void line(std::string_view key, std::string_view value) {
    text_.append(key);
    text_.push_back('=');
    text_.append(value);
    text_.push_back('\n');
  }

  [[nodiscard]] std::string finish() && { return std::move(text_); }

private:
  std::string text_;
};

std::string optional_vec3_text(const std::optional<QVec3> &value) {
  return value.has_value() ? vec3_text(*value) : "none";
}

std::string bounds_text(const QuaderBounds &value) {
  return "min:" + vec3_text(value.min) + ",max:" + vec3_text(value.max);
}

std::string mesh_content_key(const QuaderMeshAsset &mesh) {
  CanonicalWriter writer;
  writer.line("name", quote(mesh.name));
  writer.line("stable_cpu_topology", bool_text(mesh.has_stable_cpu_topology));
  writer.line("authored_polygon_document",
              bool_text(mesh.has_authored_polygon_document));
  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    const QuaderMeshVertex &vertex = mesh.vertices[index];
    writer.line("v[" + std::to_string(index) + "]",
                "p:" + vec3_text(vertex.position) + ",n:" +
                    vec3_text(vertex.normal) + ",uv:" + vec2_text(vertex.uv) +
                    ",rgba:" + fixed_hex(vertex.rgba));
  }
  for (std::size_t index = 0; index < mesh.indices.size(); ++index) {
    writer.line("i[" + std::to_string(index) + "]",
                std::to_string(mesh.indices[index]));
  }
  for (std::size_t index = 0; index < mesh.primitives.size(); ++index) {
    const QuaderMeshPrimitiveSpan &primitive = mesh.primitives[index];
    writer.line("p[" + std::to_string(index) + "]",
                "offset:" + std::to_string(primitive.index_offset) +
                    ",count:" + std::to_string(primitive.index_count) +
                    ",material_slot:" +
                    std::to_string(primitive.material_slot));
  }
  return std::move(writer).finish();
}

std::string texture_content_key(const QuaderTextureAsset &texture) {
  return "name:" + quote(texture.name) + ",usage:" +
         enum_text(texture.usage) + ",color_space:" +
         enum_text(texture.color_space) + ",width:" +
         std::to_string(texture.width) + ",height:" +
         std::to_string(texture.height) + ",source_name:" +
         quote(portable_filename(texture.source_path)) + ",generated:" +
         bool_text(texture.generated);
}

std::string material_content_key(const QuaderMaterialAsset &material) {
  std::vector<std::pair<std::string, AssetId>> slots(
      material.texture_slots.begin(), material.texture_slots.end());
  std::sort(slots.begin(), slots.end());
  std::string result =
      "name:" + quote(material.name) + ",base_color:" +
      vec3_text(material.base_color) + ",roughness:" +
      float_bits(material.roughness) + ",metallic:" +
      float_bits(material.metallic) + ",node_graph_reserved:" +
      bool_text(material.node_graph_reserved);
  for (const auto &[slot, texture] : slots) {
    result += ",slot:" + quote(slot) + "->" + std::to_string(texture);
  }
  return result;
}

/// Captures and semantically labels every observable editor domain in one revision.
struct CaptureData final {
  explicit CaptureData(const QuaderEditorApi &source_editor)
      : editor(source_editor), snapshot(editor.snapshot()),
        entities(snapshot.entities), objects(snapshot.modeling_objects),
        overlay(editor.render().selection_overlay_frame()),
        previews(editor.render().tool_preview_meshes()) {
    discover_hidden_instance_entities();
    build_entity_labels();
    build_semantic_entity_labels();
    build_modeling_object_labels();
    load_compiled_meshes();
    discover_referenced_assets();
    build_layer_labels();
    build_asset_labels();
  }

  [[nodiscard]] std::string entity_label(EntityId entity) const {
    if (entity == kInvalidEntityId) {
      return "none";
    }
    const auto found = entity_labels.find(entity);
    return found != entity_labels.end() ? found->second : "unknown-entity";
  }

  [[nodiscard]] std::string semantic_entity_label(EntityId entity) const {
    if (entity == kInvalidEntityId) {
      return "none";
    }
    const auto found = semantic_entity_labels.find(entity);
    return found != semantic_entity_labels.end() ? found->second
                                                  : "unknown-entity";
  }

  [[nodiscard]] std::string modeling_label(int object) const {
    if (object < 0) {
      return "none";
    }
    const auto found = modeling_labels.find(object);
    return found != modeling_labels.end() ? found->second
                                          : "unknown-modeling-object";
  }

  [[nodiscard]] std::string mesh_label(AssetId mesh) const {
    if (mesh == kInvalidAssetId) {
      return "none";
    }
    const auto found = mesh_labels.find(mesh);
    return found != mesh_labels.end() ? found->second : "unknown-mesh";
  }

  [[nodiscard]] std::string material_label(AssetId material) const {
    if (material == kInvalidAssetId) {
      return "none";
    }
    const auto found = material_labels.find(material);
    return found != material_labels.end() ? found->second
                                          : "unknown-material";
  }

  [[nodiscard]] std::string texture_label(AssetId texture) const {
    if (texture == kInvalidAssetId) {
      return "none";
    }
    const auto found = texture_labels.find(texture);
    return found != texture_labels.end() ? found->second : "unknown-texture";
  }

  [[nodiscard]] std::string layer_label(QuaderLayerId layer) const {
    if (layer == kInvalidQuaderLayerId) {
      return "none";
    }
    const auto found = layer_labels.find(layer);
    return found != layer_labels.end() ? found->second : "unknown-layer";
  }

  const QuaderEditorApi &editor;
  QuaderApiSnapshot snapshot;
  std::vector<QuaderEntitySnapshot> entities;
  std::vector<QuaderModelingObjectRecord> objects;
  QuaderOverlayFrame overlay;
  std::vector<QuaderToolPreviewMesh> previews;
  std::unordered_map<EntityId, std::string> entity_labels;
  std::unordered_map<EntityId, std::string> semantic_entity_labels;
  std::unordered_map<QuaderModelingObjectId, std::string> modeling_labels;
  std::unordered_map<QuaderLayerId, std::string> layer_labels;
  std::unordered_map<AssetId, std::string> mesh_labels;
  std::unordered_map<AssetId, std::string> material_labels;
  std::unordered_map<AssetId, std::string> texture_labels;
  std::map<QuaderModelingObjectId, std::optional<QuaderMeshAsset>>
      compiled_meshes;
  std::map<AssetId, std::optional<QuaderMeshSnapshot>> mesh_snapshots;
  std::map<AssetId, std::optional<QuaderMaterialAsset>> materials;
  std::map<AssetId, std::optional<QuaderTextureAsset>> textures;
  std::map<AssetId, std::vector<std::string>> mesh_owners;
  std::map<AssetId, std::vector<std::string>> material_owners;
  std::map<AssetId, std::vector<std::string>> texture_owners;

private:
  void discover_hidden_instance_entities() {
    std::unordered_set<EntityId> known;
    for (const QuaderEntitySnapshot &entity : entities) {
      known.insert(entity.id);
    }

    const auto add_with_ancestors = [this, &known](EntityId first) {
      EntityId current = first;
      std::unordered_set<EntityId> chain;
      while (current != kInvalidEntityId && chain.insert(current).second) {
        if (known.contains(current)) {
          break;
        }
        const std::optional<QuaderEntitySnapshot> entity =
            editor.query().entity(current);
        if (!entity.has_value()) {
          break;
        }
        known.insert(current);
        entities.push_back(*entity);
        current = entity->parent;
      }
    };

    std::vector<EntityId> definitions;
    for (const QuaderEntitySnapshot &entity : entities) {
      if (entity.has_instance_occurrence) {
        definitions.push_back(entity.instance_definition);
      }
    }
    for (EntityId definition : definitions) {
      add_with_ancestors(definition);
    }
    for (const QuaderModelingObjectRecord &object : objects) {
      add_with_ancestors(object.entity);
    }
    const QuaderInstanceEditState edit_state =
        editor.modeling().instance_edit_state();
    if (edit_state.active) {
      add_with_ancestors(edit_state.occurrence.id);
      add_with_ancestors(edit_state.definition.id);
    }
  }

  void build_entity_labels() {
    std::unordered_map<EntityId, const QuaderEntitySnapshot *> by_id;
    for (std::size_t index = 0; index < entities.size(); ++index) {
      by_id.emplace(entities[index].id, &entities[index]);
    }

    std::unordered_set<EntityId> resolving;
    const auto resolve = [&](const auto &self, EntityId id) -> std::string {
      const auto existing = entity_labels.find(id);
      if (existing != entity_labels.end()) {
        return existing->second;
      }
      const auto found = by_id.find(id);
      if (found == by_id.end()) {
        return "unknown-entity";
      }
      if (!resolving.insert(id).second) {
        return "cycle";
      }

      const QuaderEntitySnapshot &entity = *found->second;
      std::size_t ordinal = 0u;
      for (const QuaderEntitySnapshot &candidate : entities) {
        if (candidate.id == entity.id) {
          break;
        }
        if (candidate.parent == entity.parent && candidate.name == entity.name &&
            candidate.kind == entity.kind) {
          ++ordinal;
        }
      }
      const std::string parent =
          entity.parent == kInvalidEntityId
              ? std::string{}
              : self(self, entity.parent);
      std::string label = parent + "/" + enum_text(entity.kind) + ":" +
                          path_segment(entity.name) + "#" +
                          std::to_string(ordinal);
      resolving.erase(id);
      entity_labels.emplace(id, label);
      return label;
    };

    for (const QuaderEntitySnapshot &entity : entities) {
      (void)resolve(resolve, entity.id);
    }
  }

  void build_semantic_entity_labels() {
    using SemanticGroupKey = std::pair<int, std::string>;
    std::map<SemanticGroupKey, std::vector<const QuaderEntitySnapshot *>>
        groups;
    for (const QuaderEntitySnapshot &entity : entities) {
      groups[{static_cast<int>(entity.kind), entity.name}].push_back(&entity);
    }

    for (auto &group : groups) {
      std::vector<const QuaderEntitySnapshot *> &duplicates = group.second;
      std::sort(duplicates.begin(), duplicates.end(), [](const auto *left,
                                                         const auto *right) {
        return std::tie(left->editor_object_id, left->id) <
               std::tie(right->editor_object_id, right->id);
      });
      for (std::size_t ordinal = 0; ordinal < duplicates.size(); ++ordinal) {
        const QuaderEntitySnapshot &entity = *duplicates[ordinal];
        semantic_entity_labels.emplace(
            entity.id, enum_text(entity.kind) + ":" +
                           path_segment(entity.name) + "#" +
                           std::to_string(ordinal));
      }
    }
  }

  void build_modeling_object_labels() {
    for (const QuaderModelingObjectRecord &object : objects) {
      modeling_labels.insert_or_assign(object.modeling_object_id,
                                       semantic_entity_label(object.entity));
    }
  }

  void load_compiled_meshes() {
    for (const QuaderModelingObjectRecord &object : objects) {
      const std::optional<QuaderModelingObjectRef> ref =
          editor.query().modeling_object_for_id(object.modeling_object_id);
      compiled_meshes.emplace(
          object.modeling_object_id,
          ref.has_value() ? editor.query().compiled_object_mesh(*ref)
                          : std::optional<QuaderMeshAsset>{});
    }
  }

  void add_mesh_owner(AssetId mesh, std::string owner) {
    if (mesh != kInvalidAssetId) {
      mesh_owners[mesh].push_back(std::move(owner));
    }
  }

  void add_material_owner(AssetId material, std::string owner) {
    if (material != kInvalidAssetId) {
      material_owners[material].push_back(std::move(owner));
    }
  }

  void discover_referenced_assets() {
    for (const QuaderEntitySnapshot &entity : entities) {
      const std::string owner = semantic_entity_label(entity.id);
      if (entity.has_mesh) {
        add_mesh_owner(entity.mesh, owner);
      }
      if (entity.has_material) {
        add_material_owner(entity.material, owner);
      }
    }
    for (const QuaderModelingObjectRecord &object : objects) {
      const std::optional<QuaderModelingObjectRef> ref =
          editor.query().modeling_object_for_id(object.modeling_object_id);
      if (!ref.has_value()) {
        continue;
      }
      add_mesh_owner(ref->mesh, semantic_entity_label(object.entity));
      add_material_owner(ref->material,
                         semantic_entity_label(object.entity));
    }
    for (std::size_t index = 0; index < previews.size(); ++index) {
      const QuaderToolPreviewMesh &preview = previews[index];
      std::string owner =
          "tool-preview[source:" +
          semantic_entity_label(preview.source_entity) + ";mesh:{" +
          mesh_content_key(preview.mesh) + "};transform:" +
          matrix_text(preview.transform) + ";instance_count:" +
          std::to_string(preview.instance_transforms.size());
      for (std::size_t transform_index = 0;
           transform_index < preview.instance_transforms.size();
           ++transform_index) {
        owner += ";instance[" + std::to_string(transform_index) + "]:" +
                 matrix_text(preview.instance_transforms[transform_index]);
      }
      owner += "]";
      add_material_owner(preview.material, owner);
      add_mesh_owner(preview.mesh.id, std::move(owner));
    }

    for (const auto &[mesh, owners] : mesh_owners) {
      (void)owners;
      mesh_snapshots.emplace(mesh, editor.query().mesh(mesh));
    }
    for (const auto &[material, owners] : material_owners) {
      (void)owners;
      materials.emplace(material, editor.query().material(material));
    }
    for (const auto &[material_id, material] : materials) {
      if (!material.has_value()) {
        continue;
      }
      std::vector<std::string> owners = material_owners[material_id];
      std::sort(owners.begin(), owners.end());
      std::string owner_key;
      for (const std::string &owner : owners) {
        owner_key += owner + ";";
      }
      for (const auto &[slot, texture] : material->texture_slots) {
        if (texture != kInvalidAssetId) {
          texture_owners[texture].push_back("material:" + owner_key +
                                            "slot:" + quote(slot));
        }
      }
    }
    for (const auto &[texture, owners] : texture_owners) {
      (void)owners;
      textures.emplace(texture, editor.query().texture(texture));
    }
  }

  void build_layer_labels() {
    /// Couples one transient layer ID with its deterministic semantic sort key.
    struct Entry final {
      QuaderLayerId id = kInvalidQuaderLayerId;
      std::string key;
    };
    std::vector<Entry> entries;
    entries.reserve(snapshot.layers.size());
    for (const QuaderLayerSnapshot &layer : snapshot.layers) {
      std::vector<std::string> members;
      members.reserve(layer.members.size());
      for (EntityId member : layer.members) {
        members.push_back(entity_label(member));
      }
      std::sort(members.begin(), members.end());
      std::string key = quote(layer.name) + ",visible:" +
                        bool_text(layer.visible) + ",locked:" +
                        bool_text(layer.locked) + ",color:" +
                        vec3_text(layer.wireframe_color);
      for (const std::string &member : members) {
        key += ",member:" + member;
      }
      entries.push_back({.id = layer.id, .key = std::move(key)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &left,
                                                  const Entry &right) {
      return std::tie(left.key, left.id) < std::tie(right.key, right.id);
    });
    for (std::size_t index = 0; index < entries.size(); ++index) {
      layer_labels.emplace(entries[index].id,
                           "layer[" + std::to_string(index) + "]");
    }
  }

  template <typename OptionalMap, typename OwnerMap, typename KeyFunction>
  static std::unordered_map<AssetId, std::string>
  make_asset_labels(const OptionalMap &values, const OwnerMap &owners,
                    std::string_view prefix, KeyFunction key_function) {
    /// Couples one transient asset ID with its owner-derived semantic sort key.
    struct Entry final {
      AssetId id = kInvalidAssetId;
      std::string key;
    };
    std::vector<Entry> entries;
    entries.reserve(owners.size());
    for (const auto &[id, owner_values] : owners) {
      std::vector<std::string> sorted_owners = owner_values;
      std::sort(sorted_owners.begin(), sorted_owners.end());
      sorted_owners.erase(
          std::unique(sorted_owners.begin(), sorted_owners.end()),
          sorted_owners.end());
      std::string key;
      for (const std::string &owner : sorted_owners) {
        key += "owner:" + owner + ";";
      }
      const auto value = values.find(id);
      key += value != values.end() && value->second.has_value()
                 ? key_function(*value->second)
                 : std::string("missing");
      entries.push_back({.id = id, .key = std::move(key)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &left,
                                                  const Entry &right) {
      return std::tie(left.key, left.id) < std::tie(right.key, right.id);
    });

    std::unordered_map<AssetId, std::string> result;
    for (std::size_t index = 0; index < entries.size(); ++index) {
      result.emplace(entries[index].id,
                     std::string(prefix) + "[" + std::to_string(index) +
                         "]");
    }
    return result;
  }

  void build_asset_labels() {
    mesh_labels = make_asset_labels(
        mesh_snapshots, mesh_owners, "mesh",
        [](const QuaderMeshSnapshot &mesh) {
          return "vertices:" + std::to_string(mesh.vertex_count) +
                 ",indices:" + std::to_string(mesh.index_count) +
                 ",primitives:" + std::to_string(mesh.primitive_count) +
                 ",stable:" + bool_text(mesh.stable_cpu_topology) +
                 ",has_bounds:" + bool_text(mesh.has_bounds) +
                 (mesh.has_bounds ? ",bounds:" + bounds_text(mesh.bounds)
                                  : std::string{});
        });
    material_labels = make_asset_labels(
        materials, material_owners, "material",
        [](const QuaderMaterialAsset &material) {
          return material_content_key(material);
        });
    texture_labels = make_asset_labels(
        textures, texture_owners, "texture",
        [](const QuaderTextureAsset &texture) {
          return texture_content_key(texture);
        });
  }
};

std::string scene_settings_text(const QuaderSceneSettings &settings) {
  const QuaderSceneEnvironmentSettings &environment = settings.environment;
  const QuaderSceneCameraSettings &camera = settings.camera;
  const QuaderScenePostProcessSettings &post_process = settings.post_process;
  return "environment_id:" + quote(environment.environment_id) +
         ",environment_enabled:" + bool_text(environment.enabled) +
         ",show_background:" + bool_text(environment.show_background) +
         ",clear_color:" + vec3_text(environment.clear_color) +
         ",environment_intensity:" + float_bits(environment.intensity) +
         ",background_intensity:" +
         float_bits(environment.background_intensity) +
         ",rotation_y:" + float_bits(environment.rotation_y) +
         ",aperture:" + float_bits(camera.aperture) +
         ",shutter_speed:" + float_bits(camera.shutter_speed) +
         ",sensitivity:" + float_bits(camera.sensitivity) +
         ",near_plane:" + float_bits(camera.near_plane) +
         ",far_plane:" + float_bits(camera.far_plane) +
         ",post_process_enabled:" + bool_text(post_process.enabled) +
         ",anti_aliasing:" + bool_text(post_process.anti_aliasing) +
         ",ambient_occlusion:" +
         bool_text(post_process.ambient_occlusion) +
         ",ambient_occlusion_intensity:" +
         float_bits(post_process.ambient_occlusion_intensity) +
         ",ambient_occlusion_radius:" +
         float_bits(post_process.ambient_occlusion_radius) +
         ",bloom:" + bool_text(post_process.bloom) +
         ",bloom_strength:" + float_bits(post_process.bloom_strength) +
         ",dithering:" + bool_text(post_process.dithering) +
         ",tone_mapping:" + enum_text(post_process.tone_mapping);
}

std::string light_text(const QuaderLightComponent &light) {
  return "type:" + enum_text(light.type) + ",color:" +
         vec3_text(light.color) + ",intensity:" + float_bits(light.intensity) +
         ",range:" + float_bits(light.range) + ",casts_shadows:" +
         bool_text(light.casts_shadows);
}

std::string capture_documents(const CaptureData &data) {
  CanonicalWriter writer;
  const QuaderProjectApi &project = data.editor.project();
  writer.line("project.has_project", bool_text(project.has_project()));
  writer.line("project.dirty", bool_text(project.dirty()));
  if (project.has_project()) {
    // Persistent project IDs, UUIDs, roots, manifests, and recent-project paths
    // are deliberately omitted. The project name is stable user content.
    writer.line("project.name", quote(project.metadata().name));
  }

  const std::vector<QuaderSceneDocumentInfo> documents =
      data.editor.scene_documents().open_scenes();
  writer.line("documents.count", std::to_string(documents.size()));
  for (std::size_t index = 0; index < documents.size(); ++index) {
    const QuaderSceneDocumentInfo &document = documents[index];
    const std::string prefix = "documents[" + std::to_string(index) + "]";
    writer.line(prefix + ".name", quote(document.name));
    writer.line(prefix + ".path",
                document.path.has_value()
                    ? quote(document.path->portable_string())
                    : "none");
    writer.line(prefix + ".dirty", bool_text(document.dirty));
    writer.line(prefix + ".active", bool_text(document.active));
  }
  return std::move(writer).finish();
}

std::string capture_scene(const CaptureData &data) {
  CanonicalWriter writer;
  std::vector<const QuaderEntitySnapshot *> entities;
  entities.reserve(data.entities.size());
  for (const QuaderEntitySnapshot &entity : data.entities) {
    entities.push_back(&entity);
  }
  std::sort(entities.begin(), entities.end(), [&data](const auto *left,
                                                       const auto *right) {
    return data.entity_label(left->id) < data.entity_label(right->id);
  });
  writer.line("entities.count", std::to_string(entities.size()));
  for (const QuaderEntitySnapshot *entity : entities) {
    const std::string prefix = "entity[" + data.entity_label(entity->id) + "]";
    writer.line(prefix + ".name", quote(entity->name));
    writer.line(prefix + ".kind", enum_text(entity->kind));
    writer.line(prefix + ".parent", data.entity_label(entity->parent));
    writer.line(prefix + ".transform", transform_text(entity->transform));
    writer.line(prefix + ".visible", bool_text(entity->visible));
    writer.line(prefix + ".selectable", bool_text(entity->selectable));
    writer.line(prefix + ".locked", bool_text(entity->locked));
    writer.line(prefix + ".layer", data.layer_label(entity->layer_id));
    writer.line(prefix + ".has_mesh", bool_text(entity->has_mesh));
    writer.line(prefix + ".mesh", data.mesh_label(entity->mesh));
    writer.line(prefix + ".has_material", bool_text(entity->has_material));
    writer.line(prefix + ".material",
                data.material_label(entity->material));
    writer.line(prefix + ".has_light", bool_text(entity->has_light));
    if (entity->has_light) {
      writer.line(prefix + ".light", light_text(entity->light));
    }
    writer.line(prefix + ".scene_root", bool_text(entity->is_scene_root));
    writer.line(prefix + ".has_scene_settings",
                bool_text(entity->has_scene_settings));
    if (entity->has_scene_settings) {
      writer.line(prefix + ".scene_settings",
                  scene_settings_text(entity->scene_settings));
    }
    writer.line(prefix + ".has_modeling_object",
                bool_text(entity->has_modeling_object));
    writer.line(prefix + ".instance_definition",
                bool_text(entity->is_instance_definition));
    writer.line(prefix + ".instance_occurrence",
                bool_text(entity->has_instance_occurrence));
    if (entity->has_instance_occurrence) {
      writer.line(prefix + ".definition",
                  data.entity_label(entity->instance_definition));
    }
  }

  std::vector<const QuaderLayerSnapshot *> layers;
  layers.reserve(data.snapshot.layers.size());
  for (const QuaderLayerSnapshot &layer : data.snapshot.layers) {
    layers.push_back(&layer);
  }
  std::sort(layers.begin(), layers.end(), [&data](const auto *left,
                                                   const auto *right) {
    return data.layer_label(left->id) < data.layer_label(right->id);
  });
  writer.line("layers.count", std::to_string(layers.size()));
  for (const QuaderLayerSnapshot *layer : layers) {
    const std::string prefix = data.layer_label(layer->id);
    writer.line(prefix + ".name", quote(layer->name));
    writer.line(prefix + ".visible", bool_text(layer->visible));
    writer.line(prefix + ".locked", bool_text(layer->locked));
    writer.line(prefix + ".wireframe_color",
                vec3_text(layer->wireframe_color));
    std::vector<std::string> members;
    members.reserve(layer->members.size());
    for (EntityId member : layer->members) {
      members.push_back(data.entity_label(member));
    }
    std::sort(members.begin(), members.end());
    for (std::size_t index = 0; index < members.size(); ++index) {
      writer.line(prefix + ".member[" + std::to_string(index) + "]",
                  members[index]);
    }
  }

  std::vector<const QuaderModelingObjectRecord *> objects;
  objects.reserve(data.objects.size());
  for (const QuaderModelingObjectRecord &object : data.objects) {
    objects.push_back(&object);
  }
  std::sort(objects.begin(), objects.end(), [&data](const auto *left,
                                                     const auto *right) {
    return data.entity_label(left->entity) < data.entity_label(right->entity);
  });
  writer.line("modeling_objects.count", std::to_string(objects.size()));
  for (const QuaderModelingObjectRecord *object : objects) {
    const std::string prefix =
        "modeling_object[" + data.entity_label(object->entity) + "]";
    writer.line(prefix + ".name", quote(object->name));
    writer.line(prefix + ".mesh", data.mesh_label(object->mesh));
  }

  const QuaderInstanceEditState edit_state =
      data.editor.modeling().instance_edit_state();
  writer.line("instance_edit.active", bool_text(edit_state.active));
  if (edit_state.active) {
    writer.line("instance_edit.occurrence",
                data.entity_label(edit_state.occurrence.id));
    writer.line("instance_edit.definition",
                data.entity_label(edit_state.definition.id));
    writer.line("instance_edit.scene_name", quote(edit_state.scene_name));
    writer.line("instance_edit.instance_name",
                quote(edit_state.instance_name));
  }
  return std::move(writer).finish();
}

void write_mesh_geometry(CanonicalWriter &writer, std::string_view prefix,
                         const QuaderMeshAsset &mesh) {
  writer.line(std::string(prefix) + ".name", quote(mesh.name));
  writer.line(std::string(prefix) + ".stable_cpu_topology",
              bool_text(mesh.has_stable_cpu_topology));
  writer.line(std::string(prefix) + ".authored_polygon_document",
              bool_text(mesh.has_authored_polygon_document));
  writer.line(std::string(prefix) + ".vertex_count",
              std::to_string(mesh.vertices.size()));
  writer.line(std::string(prefix) + ".index_count",
              std::to_string(mesh.indices.size()));
  writer.line(std::string(prefix) + ".primitive_count",
              std::to_string(mesh.primitives.size()));
  for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
    const QuaderMeshVertex &vertex = mesh.vertices[index];
    writer.line(std::string(prefix) + ".vertex[" +
                    std::to_string(index) + "]",
                "position:" + vec3_text(vertex.position) + ",normal:" +
                    vec3_text(vertex.normal) + ",uv:" + vec2_text(vertex.uv) +
                    ",rgba:" + fixed_hex(vertex.rgba));
  }
  for (std::size_t index = 0; index < mesh.indices.size(); ++index) {
    writer.line(std::string(prefix) + ".index[" + std::to_string(index) +
                    "]",
                std::to_string(mesh.indices[index]));
  }
  for (std::size_t index = 0; index < mesh.primitives.size(); ++index) {
    const QuaderMeshPrimitiveSpan &primitive = mesh.primitives[index];
    writer.line(std::string(prefix) + ".primitive[" +
                    std::to_string(index) + "]",
                "index_offset:" + std::to_string(primitive.index_offset) +
                    ",index_count:" +
                    std::to_string(primitive.index_count) +
                    ",material_slot:" +
                    std::to_string(primitive.material_slot));
  }
}

std::string capture_geometry(const CaptureData &data) {
  CanonicalWriter writer;
  std::vector<const QuaderModelingObjectRecord *> objects;
  objects.reserve(data.objects.size());
  for (const QuaderModelingObjectRecord &object : data.objects) {
    objects.push_back(&object);
  }
  std::sort(objects.begin(), objects.end(), [&data](const auto *left,
                                                     const auto *right) {
    return data.semantic_entity_label(left->entity) <
           data.semantic_entity_label(right->entity);
  });
  writer.line("geometry.object_count", std::to_string(objects.size()));
  for (const QuaderModelingObjectRecord *object : objects) {
    const std::string prefix =
        "geometry[" + data.semantic_entity_label(object->entity) + "]";
    writer.line(prefix + ".asset", data.mesh_label(object->mesh));
    const auto found = data.compiled_meshes.find(object->modeling_object_id);
    const bool present = found != data.compiled_meshes.end() &&
                         found->second.has_value();
    writer.line(prefix + ".compiled", bool_text(present));
    if (present) {
      write_mesh_geometry(writer, prefix, *found->second);
    }
  }
  return std::move(writer).finish();
}

std::string overlay_element_text(const CaptureData &data,
                                 const QuaderOverlayElementRef &element) {
  return "object:" + data.modeling_label(element.object_id) +
         ",vertex:" + std::to_string(element.vertex_id) + ",edge_a:" +
         std::to_string(element.edge_a) + ",edge_b:" +
         std::to_string(element.edge_b) + ",face:" +
         std::to_string(element.face_id);
}

std::string capture_selection(const CaptureData &data) {
  CanonicalWriter writer;
  const QuaderSelectionSummary &selection = data.snapshot.selection;
  writer.line("selection.mode", enum_text(selection.mode));
  writer.line("selection.has_selection", bool_text(selection.has_selection));
  writer.line("selection.active_kind", enum_text(selection.active_kind));
  writer.line("selection.active_object",
              data.modeling_label(selection.active_object_id));
  writer.line("selection.active_vertex",
              std::to_string(selection.active_vertex_id));
  writer.line("selection.active_edge_a",
              std::to_string(selection.active_edge_a));
  writer.line("selection.active_edge_b",
              std::to_string(selection.active_edge_b));
  writer.line("selection.active_face",
              std::to_string(selection.active_face_id));
  writer.line("selection.object_count",
              std::to_string(selection.selected_object_count));
  writer.line("selection.vertex_count",
              std::to_string(selection.selected_vertex_count));
  writer.line("selection.edge_count",
              std::to_string(selection.selected_edge_count));
  writer.line("selection.face_count",
              std::to_string(selection.selected_face_count));

  std::vector<std::string> selected_entities;
  std::vector<std::string> active_entities;
  for (const QuaderEntitySnapshot &entity : data.entities) {
    if (entity.selected) {
      selected_entities.push_back(data.semantic_entity_label(entity.id));
    }
    if (entity.active) {
      active_entities.push_back(data.semantic_entity_label(entity.id));
    }
  }
  std::sort(selected_entities.begin(), selected_entities.end());
  std::sort(active_entities.begin(), active_entities.end());
  for (std::size_t index = 0; index < selected_entities.size(); ++index) {
    writer.line("selection.scene_selected[" + std::to_string(index) + "]",
                selected_entities[index]);
  }
  for (std::size_t index = 0; index < active_entities.size(); ++index) {
    writer.line("selection.scene_active[" + std::to_string(index) + "]",
                active_entities[index]);
  }

  std::set<std::string> selected_components;
  if (selection.mode == QuaderSelectionMode::Vertex) {
    for (const QuaderOverlayPoint &point : data.overlay.points) {
      if (point.layer == QuaderOverlayLayer::SelectedVertex &&
          point.element.vertex_id >= 0) {
        selected_components.insert(
            data.modeling_label(point.element.object_id) + ":vertex:" +
            std::to_string(point.element.vertex_id));
      }
    }
  } else if (selection.mode == QuaderSelectionMode::Edge) {
    for (const QuaderOverlayLine &line : data.overlay.lines) {
      if (line.layer == QuaderOverlayLayer::SelectedEdge &&
          line.element.edge_a >= 0 && line.element.edge_b >= 0) {
        const int first = std::min(line.element.edge_a, line.element.edge_b);
        const int second = std::max(line.element.edge_a, line.element.edge_b);
        selected_components.insert(data.modeling_label(line.element.object_id) +
                                   ":edge:" + std::to_string(first) + "," +
                                   std::to_string(second));
      }
    }
  } else if (selection.mode == QuaderSelectionMode::Face) {
    const auto add_face = [&data, &selected_components](
                              const QuaderOverlayElementRef &element) {
      if (element.face_id >= 0) {
        selected_components.insert(data.modeling_label(element.object_id) +
                                   ":face:" +
                                   std::to_string(element.face_id));
      }
    };
    for (const QuaderOverlayTriangle &triangle : data.overlay.triangles) {
      if (triangle.layer == QuaderOverlayLayer::SelectedVisibleFace ||
          triangle.layer == QuaderOverlayLayer::SelectedXrayFace ||
          triangle.layer == QuaderOverlayLayer::SelectedFaceMask) {
        add_face(triangle.element);
      }
    }
    for (const QuaderOverlayLine &line : data.overlay.lines) {
      if (line.layer == QuaderOverlayLayer::SelectedFaceEdge) {
        add_face(line.element);
      }
    }
  }
  std::size_t component_index = 0u;
  for (const std::string &component : selected_components) {
    writer.line("selection.component[" +
                    std::to_string(component_index++) + "]",
                component);
  }

  writer.line("selection.center",
              optional_vec3_text(data.editor.query().selection_center()));
  writer.line(
      "selection.transform_pivot",
      optional_vec3_text(data.editor.query().selection_transform_pivot()));
  const QuaderHoverSummary hover = data.editor.selection().hover_summary();
  writer.line("hover.kind", enum_text(hover.target.kind));
  writer.line("hover.object", data.modeling_label(hover.target.object_id));
  writer.line("hover.vertex", std::to_string(hover.target.vertex_id));
  writer.line("hover.edge_a", std::to_string(hover.target.edge_a));
  writer.line("hover.edge_b", std::to_string(hover.target.edge_b));
  writer.line("hover.face", std::to_string(hover.target.face_id));
  writer.line("hover.target_selected", bool_text(hover.target_selected));
  return std::move(writer).finish();
}

std::string capture_overlay(const CaptureData &data) {
  CanonicalWriter writer;
  writer.line("overlay.hover_target_selected",
              bool_text(data.overlay.hover_target_selected));

  std::vector<std::string> lines;
  lines.reserve(data.overlay.lines.size());
  for (const QuaderOverlayLine &line : data.overlay.lines) {
    lines.push_back("layer:" + enum_text(line.layer) +
                    ",start:" + vec3_text(line.start) +
                    ",end:" + vec3_text(line.end) +
                    ",color:" + fixed_hex(line.color) +
                    ",width:" + float_bits(line.width_pixels) +
                    ",style:" + enum_text(line.style_role) +
                    ",source:" + enum_text(line.source) + ",element:{" +
                    overlay_element_text(data, line.element) + "}");
  }
  std::sort(lines.begin(), lines.end());
  writer.line("overlay.line_count", std::to_string(lines.size()));
  for (std::size_t index = 0; index < lines.size(); ++index) {
    writer.line("overlay.line[" + std::to_string(index) + "]", lines[index]);
  }

  std::vector<std::string> triangles;
  triangles.reserve(data.overlay.triangles.size());
  for (const QuaderOverlayTriangle &triangle : data.overlay.triangles) {
    triangles.push_back(
        "layer:" + enum_text(triangle.layer) + ",a:" +
        vec3_text(triangle.a) + ",b:" + vec3_text(triangle.b) +
        ",c:" + vec3_text(triangle.c) +
        ",color:" + fixed_hex(triangle.color) +
        ",style:" + enum_text(triangle.style_role) +
        ",source:" + enum_text(triangle.source) + ",element:{" +
        overlay_element_text(data, triangle.element) + "}");
  }
  std::sort(triangles.begin(), triangles.end());
  writer.line("overlay.triangle_count", std::to_string(triangles.size()));
  for (std::size_t index = 0; index < triangles.size(); ++index) {
    writer.line("overlay.triangle[" + std::to_string(index) + "]",
                triangles[index]);
  }

  std::vector<std::string> points;
  points.reserve(data.overlay.points.size());
  for (const QuaderOverlayPoint &point : data.overlay.points) {
    points.push_back(
        "layer:" + enum_text(point.layer) +
        ",position:" + vec3_text(point.position) +
        ",color:" + fixed_hex(point.color) +
        ",size:" + float_bits(point.size_pixels) +
        ",style:" + enum_text(point.style_role) +
        ",source:" + enum_text(point.source) +
        ",outline_growth:" +
        (point.outline_growth_pixels.has_value()
             ? float_bits(*point.outline_growth_pixels)
             : std::string("none")) +
        ",element:{" + overlay_element_text(data, point.element) + "}");
  }
  std::sort(points.begin(), points.end());
  writer.line("overlay.point_count", std::to_string(points.size()));
  for (std::size_t index = 0; index < points.size(); ++index) {
    writer.line("overlay.point[" + std::to_string(index) + "]",
                points[index]);
  }
  return std::move(writer).finish();
}

std::string project_material_text(const QuaderProjectMaterialInfo &material) {
  // QMAT byte size and raw-byte content revision are storage artifacts: the
  // same decoded material has different values in LF and CRLF checkouts.
  // Fingerprint only public, user-observable material semantics here.
  return "name:" + quote(material.name) +
         ",kind:" + enum_text(material.kind) +
         ",relative_path:" + quote(material.project_relative_path) +
         ",base_color:(" + float_bits(material.base_color[0]) + "," +
         float_bits(material.base_color[1]) + "," +
         float_bits(material.base_color[2]) + ")";
}

std::string capture_assets(const CaptureData &data) {
  CanonicalWriter writer;
  writer.line("assets.mesh_count",
              std::to_string(data.snapshot.assets.mesh_count));
  writer.line("assets.material_count",
              std::to_string(data.snapshot.assets.material_count));
  writer.line("assets.texture_count",
              std::to_string(data.snapshot.assets.texture_count));

  std::vector<std::pair<std::string, const QuaderMeshSnapshot *>> meshes;
  for (const auto &[id, mesh] : data.mesh_snapshots) {
    if (mesh.has_value()) {
      meshes.emplace_back(data.mesh_label(id), &*mesh);
    } else {
      meshes.emplace_back(data.mesh_label(id), nullptr);
    }
  }
  std::sort(meshes.begin(), meshes.end(), [](const auto &left,
                                              const auto &right) {
    return left.first < right.first;
  });
  for (const auto &[label, mesh] : meshes) {
    const std::string prefix = "asset." + label;
    writer.line(prefix + ".present", bool_text(mesh != nullptr));
    if (mesh != nullptr) {
      writer.line(prefix + ".vertex_count",
                  std::to_string(mesh->vertex_count));
      writer.line(prefix + ".index_count",
                  std::to_string(mesh->index_count));
      writer.line(prefix + ".primitive_count",
                  std::to_string(mesh->primitive_count));
      writer.line(prefix + ".stable_cpu_topology",
                  bool_text(mesh->stable_cpu_topology));
      writer.line(prefix + ".has_bounds", bool_text(mesh->has_bounds));
      if (mesh->has_bounds) {
        writer.line(prefix + ".bounds", bounds_text(mesh->bounds));
      }
    }
  }

  std::vector<std::pair<std::string, const QuaderMaterialAsset *>> materials;
  for (const auto &[id, material] : data.materials) {
    materials.emplace_back(data.material_label(id),
                           material.has_value() ? &*material : nullptr);
  }
  std::sort(materials.begin(), materials.end(), [](const auto &left,
                                                    const auto &right) {
    return left.first < right.first;
  });
  for (const auto &[label, material] : materials) {
    const std::string prefix = "asset." + label;
    writer.line(prefix + ".present", bool_text(material != nullptr));
    if (material == nullptr) {
      continue;
    }
    writer.line(prefix + ".name", quote(material->name));
    writer.line(prefix + ".base_color", vec3_text(material->base_color));
    writer.line(prefix + ".roughness", float_bits(material->roughness));
    writer.line(prefix + ".metallic", float_bits(material->metallic));
    writer.line(prefix + ".node_graph_reserved",
                bool_text(material->node_graph_reserved));
    std::vector<std::pair<std::string, AssetId>> slots(
        material->texture_slots.begin(), material->texture_slots.end());
    std::sort(slots.begin(), slots.end());
    for (const auto &[slot, texture] : slots) {
      writer.line(prefix + ".texture_slot[" + quote(slot) + "]",
                  data.texture_label(texture));
    }
  }

  std::vector<std::pair<std::string, const QuaderTextureAsset *>> textures;
  for (const auto &[id, texture] : data.textures) {
    textures.emplace_back(data.texture_label(id),
                          texture.has_value() ? &*texture : nullptr);
  }
  std::sort(textures.begin(), textures.end(), [](const auto &left,
                                                  const auto &right) {
    return left.first < right.first;
  });
  for (const auto &[label, texture] : textures) {
    const std::string prefix = "asset." + label;
    writer.line(prefix + ".present", bool_text(texture != nullptr));
    if (texture != nullptr) {
      writer.line(prefix + ".content", texture_content_key(*texture));
    }
  }

  const QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
      project_materials = data.editor.project_assets().materials();
  writer.line("project_assets.available", bool_text(project_materials.ok()));
  if (project_materials.ok()) {
    std::vector<std::string> serialized;
    serialized.reserve(project_materials.value().size());
    for (const QuaderProjectMaterialInfo &material :
         project_materials.value()) {
      serialized.push_back(project_material_text(material));
    }
    std::sort(serialized.begin(), serialized.end());
    writer.line("project_assets.material_count",
                std::to_string(serialized.size()));
    for (std::size_t index = 0; index < serialized.size(); ++index) {
      writer.line("project_assets.material[" + std::to_string(index) + "]",
                  serialized[index]);
    }
  }
  const QuaderApiResult<QuaderProjectMaterialInfo> active_material =
      data.editor.project_assets().active_material();
  writer.line("project_assets.active_available",
              bool_text(active_material.ok()));
  if (active_material.ok()) {
    writer.line("project_assets.active",
                project_material_text(active_material.value()));
  }
  return std::move(writer).finish();
}

std::string capture_history(const CaptureData &data) {
  CanonicalWriter writer;
  const QuaderHistorySnapshot &history = data.snapshot.history;
  writer.line("history.can_undo", bool_text(history.can_undo));
  writer.line("history.can_redo", bool_text(history.can_redo));
  writer.line("history.undo_label", quote(history.undo_label));
  writer.line("history.redo_label", quote(history.redo_label));
  return std::move(writer).finish();
}

void write_tool_status(CanonicalWriter &writer,
                       const QuaderToolStatus &status) {
  writer.line("tool.active_tool", enum_text(status.active_tool));
  writer.line("tool.modal_owns_viewport",
              bool_text(status.modal_tool_owns_viewport));
  writer.line("tool.insert_edge_loop.active",
              bool_text(status.insert_edge_loop_active));
  writer.line("tool.insert_edge_loop.dragging",
              bool_text(status.insert_edge_loop_dragging));
  writer.line("tool.insert_edge_loop.has_preview",
              bool_text(status.insert_edge_loop_has_preview));
  writer.line("tool.knife.active", bool_text(status.knife_active));
  writer.line("tool.knife.has_current", bool_text(status.knife_has_current));
  writer.line("tool.knife.can_commit", bool_text(status.knife_can_commit));
  writer.line("tool.cut.active", bool_text(status.cut_active));
  writer.line("tool.cut.can_preview", bool_text(status.cut_can_preview));
  writer.line("tool.cut.preview_valid", bool_text(status.cut_preview_valid));
  writer.line("tool.cut.dragging", bool_text(status.cut_dragging));
  writer.line("tool.cut.dragging_new_point",
              bool_text(status.cut_dragging_new_point));
  writer.line("tool.cut.dragging_line",
              bool_text(status.cut_dragging_line));
  writer.line("tool.cut.dragging_point_index",
              std::to_string(status.cut_dragging_point_index));
  writer.line("tool.cut.point_count", std::to_string(status.cut_point_count));
  writer.line("tool.cut.captured_point_count",
              std::to_string(status.cut_captured_point_count));
  writer.line("tool.cut.has_line", bool_text(status.cut_has_line));
  writer.line("tool.cut.mode", enum_text(status.cut_mode));
  for (std::size_t index = 0; index < status.cut_points.size(); ++index) {
    writer.line("tool.cut.point[" + std::to_string(index) + "]",
                vec3_text(status.cut_points[index]));
  }
  writer.line("tool.mirror.active", bool_text(status.mirror_active));
  writer.line("tool.mirror.can_preview",
              bool_text(status.mirror_can_preview));
  writer.line("tool.mirror.preview_valid",
              bool_text(status.mirror_preview_valid));
  writer.line("tool.mirror.dragging", bool_text(status.mirror_dragging));
  writer.line("tool.mirror.dragging_new_point",
              bool_text(status.mirror_dragging_new_point));
  writer.line("tool.mirror.dragging_line",
              bool_text(status.mirror_dragging_line));
  writer.line("tool.mirror.dragging_point_index",
              std::to_string(status.mirror_dragging_point_index));
  writer.line("tool.mirror.point_count",
              std::to_string(status.mirror_point_count));
  writer.line("tool.mirror.captured_point_count",
              std::to_string(status.mirror_captured_point_count));
  writer.line("tool.mirror.has_line", bool_text(status.mirror_has_line));
  for (std::size_t index = 0; index < status.mirror_points.size(); ++index) {
    writer.line("tool.mirror.point[" + std::to_string(index) + "]",
                vec3_text(status.mirror_points[index]));
  }
  writer.line("tool.poly.active", bool_text(status.poly_active));
  writer.line("tool.poly.closed", bool_text(status.poly_closed));
  writer.line("tool.poly.has_current", bool_text(status.poly_has_current));
  writer.line("tool.poly.can_commit", bool_text(status.poly_can_commit));
  writer.line("tool.poly.point_count",
              std::to_string(status.poly_point_count));
}

std::string preview_sort_key(const CaptureData &data,
                             const QuaderToolPreviewMesh &preview) {
  std::string result =
      data.semantic_entity_label(preview.source_entity) + ";" +
                       data.material_label(preview.material) + ";mesh:{" +
                       mesh_content_key(preview.mesh) + "};transform:" +
                       matrix_text(preview.transform) + ";instance_count:" +
                       std::to_string(preview.instance_transforms.size());
  for (std::size_t index = 0; index < preview.instance_transforms.size();
       ++index) {
    result += ";instance[" + std::to_string(index) + "]:" +
              matrix_text(preview.instance_transforms[index]);
  }
  return result;
}

std::string capture_tools(const CaptureData &data) {
  CanonicalWriter writer;
  write_tool_status(writer, data.snapshot.tool_status);
  writer.line("tools.active_tool_id", quote(data.editor.tools().active_tool()));
  writer.line("tools.primitive_active",
              bool_text(data.editor.tools().has_active_primitive_creation()));
  if (data.editor.tools().has_active_primitive_creation()) {
    writer.line("tools.primitive_type",
                quote(data.editor.tools().active_primitive_type()));
  }

  const QuaderToolSettingsTarget settings =
      data.editor.tools().active_settings();
  writer.line("tools.settings.active", bool_text(settings.active));
  writer.line("tools.settings.preview_active",
              bool_text(settings.preview_active));
  writer.line("tools.settings.tool_id", quote(settings.tool_id));
  writer.line("tools.settings.apply_visible",
              bool_text(settings.apply_visible));
  writer.line("tools.settings.can_apply", bool_text(settings.can_apply));
  writer.line("tools.settings.can_cancel", bool_text(settings.can_cancel));
  std::vector<const QuaderToolSettingControl *> controls;
  controls.reserve(settings.controls.size());
  for (const QuaderToolSettingControl &control : settings.controls) {
    controls.push_back(&control);
  }
  std::sort(controls.begin(), controls.end(), [](const auto *left,
                                                 const auto *right) {
    return std::tie(left->id, left->kind) < std::tie(right->id, right->kind);
  });
  for (std::size_t index = 0; index < controls.size(); ++index) {
    const QuaderToolSettingControl &control = *controls[index];
    const std::string prefix =
        "tools.settings.control[" + std::to_string(index) + "]";
    writer.line(prefix + ".kind", enum_text(control.kind));
    writer.line(prefix + ".id", quote(control.id));
    writer.line(prefix + ".bool_value", bool_text(control.bool_value));
    writer.line(prefix + ".number_value",
                double_bits(control.number_value));
    writer.line(prefix + ".min", double_bits(control.min_value));
    writer.line(prefix + ".max", double_bits(control.max_value));
    writer.line(prefix + ".step", double_bits(control.step));
    writer.line(prefix + ".choice_value", quote(control.choice_value));
    std::vector<const QuaderToolSettingChoiceOption *> choices;
    choices.reserve(control.choices.size());
    for (const QuaderToolSettingChoiceOption &choice : control.choices) {
      choices.push_back(&choice);
    }
    std::sort(choices.begin(), choices.end(), [](const auto *left,
                                                 const auto *right) {
      return left->id < right->id;
    });
    for (std::size_t choice_index = 0; choice_index < choices.size();
         ++choice_index) {
      writer.line(prefix + ".choice[" + std::to_string(choice_index) + "]",
                  "id:" + quote(choices[choice_index]->id) + ",value:" +
                      double_bits(choices[choice_index]->value));
    }
  }

  std::vector<const QuaderToolPreviewMesh *> previews;
  previews.reserve(data.previews.size());
  for (const QuaderToolPreviewMesh &preview : data.previews) {
    previews.push_back(&preview);
  }
  std::sort(previews.begin(), previews.end(), [&data](const auto *left,
                                                       const auto *right) {
    return preview_sort_key(data, *left) < preview_sort_key(data, *right);
  });
  writer.line("tools.preview_count", std::to_string(previews.size()));
  for (std::size_t index = 0; index < previews.size(); ++index) {
    const QuaderToolPreviewMesh &preview = *previews[index];
    const std::string prefix =
        "tools.preview[" + std::to_string(index) + "]";
    writer.line(prefix + ".source_entity",
                data.semantic_entity_label(preview.source_entity));
    writer.line(prefix + ".material",
                data.material_label(preview.material));
    writer.line(prefix + ".transform", matrix_text(preview.transform));
    writer.line(prefix + ".instance_count",
                std::to_string(preview.instance_transforms.size()));
    for (std::size_t transform_index = 0;
         transform_index < preview.instance_transforms.size();
         ++transform_index) {
      writer.line(prefix + ".instance_transform[" +
                      std::to_string(transform_index) + "]",
                  matrix_text(preview.instance_transforms[transform_index]));
    }
    write_mesh_geometry(writer, prefix + ".mesh", preview.mesh);
  }
  return std::move(writer).finish();
}

std::string capture_viewports(const CaptureData &data) {
  CanonicalWriter writer;
  const QuaderViewportApi &viewports = data.editor.viewports();
  const ViewportWorkspaceService &workspace =
      QuaderViewportApiRuntimeAccess::workspace(viewports);
  writer.line("viewport.layout", enum_text(viewports.layout()));
  writer.line("viewport.active_pane_count",
              std::to_string(viewports.active_pane_count()));
  writer.line("viewport.focused_pane", enum_text(viewports.focused_pane()));
  writer.line("viewport.grid_preset", std::to_string(viewports.grid_preset()));
  writer.line("viewport.local_visibility",
              bool_text(viewports.local_visibility_active()));
  constexpr std::array<ViewportPaneId, 4> kPanes = {
      ViewportPaneId::Perspective,
      ViewportPaneId::Top,
      ViewportPaneId::Front,
      ViewportPaneId::Side,
  };
  for (ViewportPaneId pane : kPanes) {
    const std::string prefix = "viewport.pane[" + enum_text(pane) + "]";
    writer.line(prefix + ".view", enum_text(viewports.view(pane)));
    writer.line(prefix + ".shading",
                enum_text(viewports.shading_mode(pane)));
    const ViewportCameraPose pose = workspace.camera(pane).pose();
    writer.line(prefix + ".camera.eye", vec3_text(pose.eye));
    writer.line(prefix + ".camera.target", vec3_text(pose.target));
    writer.line(prefix + ".camera.up", vec3_text(pose.up));
  }
  return std::move(writer).finish();
}

std::string capture_preferences(const CaptureData &data) {
  CanonicalWriter writer;
  const QuaderPreferencesApi &preferences = data.editor.preferences();
  // Locale and localized labels are intentionally absent so behavior
  // baselines remain identical across developer machines.
  writer.line("preferences.snapping",
              bool_text(preferences.snapping_enabled()));
  writer.line("preferences.backface_selection",
              bool_text(preferences.backface_selection_enabled()));
  writer.line("preferences.grid_visible",
              bool_text(preferences.grid_visible()));
  writer.line("preferences.gizmos_visible",
              bool_text(preferences.gizmos_visible()));
  writer.line("preferences.mesh_grid_visible",
              bool_text(preferences.mesh_grid_visible()));
  writer.line("preferences.overlays_visible",
              bool_text(preferences.overlays_visible()));
  writer.line("preferences.grid_snap",
              bool_text(preferences.grid_snap_enabled()));
  writer.line("preferences.vertex_snap",
              bool_text(preferences.vertex_snap_enabled()));
  writer.line("preferences.rotation_snap",
              bool_text(preferences.rotation_snap_enabled()));
  writer.line("preferences.rotation_snap_degrees",
              double_bits(preferences.rotation_snap_degrees()));
  writer.line("preferences.navigation_gizmo_visible",
              bool_text(preferences.navigation_gizmo_visible()));

  std::vector<QuaderKeybindingCatalogEntry> catalog =
      data.editor.commands().keybinding_catalog();
  std::sort(catalog.begin(), catalog.end(), [](const auto &left,
                                               const auto &right) {
    return std::tie(left.action_id, left.scope_id, left.target_id) <
           std::tie(right.action_id, right.scope_id, right.target_id);
  });
  writer.line("preferences.keybinding_count",
              std::to_string(catalog.size()));
  for (std::size_t index = 0; index < catalog.size(); ++index) {
    const QuaderKeybindingCatalogEntry &entry = catalog[index];
    const std::string prefix =
        "preferences.keybinding[" + std::to_string(index) + "]";
    writer.line(prefix + ".action_id", quote(entry.action_id));
    writer.line(prefix + ".key.action_id", quote(entry.key.action_id));
    writer.line(prefix + ".key.scope_id", quote(entry.key.scope_id));
    writer.line(prefix + ".target_id", quote(entry.target_id));
    for (std::size_t target_index = 0;
         target_index < entry.target_ids.size(); ++target_index) {
      writer.line(prefix + ".target_id[" +
                      std::to_string(target_index) + "]",
                  quote(entry.target_ids[target_index]));
    }
    writer.line(prefix + ".context", enum_text(entry.context));
    writer.line(prefix + ".scope_id", quote(entry.scope_id));
    writer.line(prefix + ".when", quote(entry.when));
    writer.line(prefix + ".shortcut", quote(entry.shortcut));
    writer.line(prefix + ".default_shortcut",
                quote(entry.default_shortcut));
    writer.line(prefix + ".source", enum_text(entry.source));
    writer.line(prefix + ".customized", bool_text(entry.customized));
    writer.line(prefix + ".unassigned", bool_text(entry.unassigned));
  }
  return std::move(writer).finish();
}

void write_revision_stamp(CanonicalWriter &writer, std::string_view prefix,
                          const QuaderRevisionStamp &revisions) {
  writer.line(std::string(prefix) + ".project",
              std::to_string(revisions.project_revision));
  writer.line(std::string(prefix) + ".scene",
              std::to_string(revisions.scene_revision));
  writer.line(std::string(prefix) + ".scene_layer",
              std::to_string(revisions.scene_layer_revision));
  writer.line(std::string(prefix) + ".asset",
              std::to_string(revisions.asset_revision));
  writer.line(std::string(prefix) + ".modeling_content",
              std::to_string(revisions.modeling_content_revision));
  writer.line(std::string(prefix) + ".modeling_selection",
              std::to_string(revisions.modeling_selection_revision));
}

std::string capture_revisions(const CaptureData &data) {
  CanonicalWriter writer;
  write_revision_stamp(writer, "revisions", data.snapshot.revisions);
  writer.line("revisions.asset_snapshot",
              std::to_string(data.snapshot.assets.revision));
  writer.line("revisions.selection_content",
              std::to_string(data.snapshot.selection.content_revision));
  writer.line("revisions.selection",
              std::to_string(data.snapshot.selection.selection_revision));
  writer.line("revisions.instance_edit",
              std::to_string(
                  data.editor.modeling().instance_edit_revision()));
  writer.line("revisions.tool_settings",
              std::to_string(
                  data.editor.tools().active_settings_revision()));

  std::vector<std::pair<std::string, std::uint64_t>> mesh_revisions;
  for (const auto &[id, mesh] : data.mesh_snapshots) {
    if (mesh.has_value()) {
      mesh_revisions.emplace_back(data.mesh_label(id), mesh->revision);
    }
  }
  std::sort(mesh_revisions.begin(), mesh_revisions.end());
  for (const auto &[label, revision] : mesh_revisions) {
    writer.line("revisions." + label, std::to_string(revision));
  }

  std::vector<std::pair<std::string, std::uint64_t>> material_revisions;
  for (const auto &[id, material] : data.materials) {
    if (material.has_value()) {
      material_revisions.emplace_back(data.material_label(id),
                                      material->revision);
    }
  }
  std::sort(material_revisions.begin(), material_revisions.end());
  for (const auto &[label, revision] : material_revisions) {
    writer.line("revisions." + label, std::to_string(revision));
  }

  std::vector<std::pair<std::string, std::uint64_t>> texture_revisions;
  for (const auto &[id, texture] : data.textures) {
    if (texture.has_value()) {
      texture_revisions.emplace_back(data.texture_label(id),
                                     texture->revision);
    }
  }
  std::sort(texture_revisions.begin(), texture_revisions.end());
  for (const auto &[label, revision] : texture_revisions) {
    writer.line("revisions." + label, std::to_string(revision));
  }

  std::vector<std::pair<std::string, std::uint64_t>> compiled_revisions;
  for (const QuaderModelingObjectRecord &object : data.objects) {
    const auto found = data.compiled_meshes.find(object.modeling_object_id);
    if (found != data.compiled_meshes.end() && found->second.has_value()) {
      compiled_revisions.emplace_back(
          data.semantic_entity_label(object.entity), found->second->revision);
    }
  }
  std::sort(compiled_revisions.begin(), compiled_revisions.end());
  for (const auto &[label, revision] : compiled_revisions) {
    writer.line("revisions.compiled[" + label + "]",
                std::to_string(revision));
  }

  return std::move(writer).finish();
}

std::vector<std::string_view> lines_of(std::string_view value) {
  std::vector<std::string_view> lines;
  std::size_t start = 0u;
  while (start < value.size()) {
    const std::size_t end = value.find('\n', start);
    if (end == std::string_view::npos) {
      lines.push_back(value.substr(start));
      break;
    }
    lines.push_back(value.substr(start, end - start));
    start = end + 1u;
  }
  return lines;
}

std::string abbreviated(std::string_view value) {
  constexpr std::size_t kLimit = 240u;
  if (value.size() <= kLimit) {
    return std::string(value);
  }
  return std::string(value.substr(0u, kLimit)) + "...";
}

} // namespace

std::string_view
command_observable_domain_name(CommandObservableDomain domain) {
  switch (domain) {
  case CommandObservableDomain::Documents:
    return "documents";
  case CommandObservableDomain::Scene:
    return "scene";
  case CommandObservableDomain::Geometry:
    return "geometry";
  case CommandObservableDomain::Selection:
    return "selection";
  case CommandObservableDomain::Overlay:
    return "overlay";
  case CommandObservableDomain::Assets:
    return "assets";
  case CommandObservableDomain::History:
    return "history";
  case CommandObservableDomain::Tools:
    return "tools";
  case CommandObservableDomain::Viewports:
    return "viewports";
  case CommandObservableDomain::Preferences:
    return "preferences";
  case CommandObservableDomain::Revisions:
    return "revisions";
  }
  return "unknown";
}

const CommandObservableDomainState &
CommandObservableState::domain(CommandObservableDomain value) const {
  return domains[domain_index(value)];
}

std::string CommandObservableState::combined_fingerprint() const {
  std::string input;
  for (std::size_t index = 0; index < kObservableDomains.size(); ++index) {
    input += command_observable_domain_name(kObservableDomains[index]);
    input += ':';
    input += domains[index].fingerprint;
    input += '\n';
  }
  return stable_fingerprint(input);
}

CommandObservableState
capture_command_observable_state(const QuaderEditorApi &editor) {
  const CaptureData data(editor);
  std::array<std::string, kCommandObservableDomainCount> canonical = {
      capture_documents(data),   capture_scene(data),
      capture_geometry(data),    capture_selection(data),
      capture_overlay(data),     capture_assets(data),
      capture_history(data),     capture_tools(data),
      capture_viewports(data),   capture_preferences(data),
      capture_revisions(data),
  };

  CommandObservableState result;
  for (std::size_t index = 0; index < canonical.size(); ++index) {
    result.domains[index].fingerprint = stable_fingerprint(canonical[index]);
    result.domains[index].canonical = std::move(canonical[index]);
  }
  return result;
}

CommandObservableStateDiff diff_command_observable_state(
    const CommandObservableState &before, const CommandObservableState &after,
    CommandObservableDomainMask domains) {
  CommandObservableStateDiff result;
  std::ostringstream summary;
  for (std::size_t index = 0; index < kObservableDomains.size(); ++index) {
    const CommandObservableDomain domain = kObservableDomains[index];
    if ((domains & command_observable_domain_mask(domain)) == 0u) {
      continue;
    }
    const CommandObservableDomainState &before_domain = before.domains[index];
    const CommandObservableDomainState &after_domain = after.domains[index];
    if (before_domain.canonical == after_domain.canonical) {
      continue;
    }
    result.changed_domains |= command_observable_domain_mask(domain);
    const std::vector<std::string_view> before_lines =
        lines_of(before_domain.canonical);
    const std::vector<std::string_view> after_lines =
        lines_of(after_domain.canonical);
    const std::size_t shared_count =
        std::min(before_lines.size(), after_lines.size());
    std::size_t mismatch = 0u;
    while (mismatch < shared_count &&
           before_lines[mismatch] == after_lines[mismatch]) {
      ++mismatch;
    }

    summary << command_observable_domain_name(domain) << ": "
            << before_domain.fingerprint << " -> "
            << after_domain.fingerprint << " (" << before_lines.size()
            << " -> " << after_lines.size() << " lines)\n";
    summary << "  first mismatch at line " << (mismatch + 1u) << '\n';
    summary << "  before: "
            << (mismatch < before_lines.size()
                    ? abbreviated(before_lines[mismatch])
                    : std::string("<end>"))
            << '\n';
    summary << "  after:  "
            << (mismatch < after_lines.size()
                    ? abbreviated(after_lines[mismatch])
                    : std::string("<end>"))
            << '\n';
  }
  if (result.empty()) {
    result.summary = "No observable domains changed.";
  } else {
    result.summary = summary.str();
    if (!result.summary.empty() && result.summary.back() == '\n') {
      result.summary.pop_back();
    }
  }
  return result;
}

} // namespace quader::test
