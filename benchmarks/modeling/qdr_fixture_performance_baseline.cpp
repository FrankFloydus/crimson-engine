////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

/// Configures the fixture path, output path, warmups, and sample counts for one run.
struct BenchmarkOptions {
  std::filesystem::path qdr_path;
  std::filesystem::path output_path;
  int load_samples = 1;
  int select_warmup = 2;
  int select_samples = 10;
  int navigation_warmup = 50;
  int navigation_frames = 1000;
  int snapshot_frames = 1000;
  int overlay_frames = 200;
  int transform_warmup = 10;
  int transform_samples = 200;
};

/// Stores aggregate latency statistics for one measured benchmark phase.
struct TimingSummary {
  std::size_t samples = 0;
  double total_ms = 0.0;
  double average_ms = 0.0;
  double minimum_ms = 0.0;
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double maximum_ms = 0.0;
};

/// Counts authored and compiled geometry primitives for a set of meshes.
struct GeometryCounts {
  std::uint64_t meshes = 0;
  std::uint64_t authored_meshes = 0;
  std::uint64_t authored_vertices = 0;
  std::uint64_t polygon_faces = 0;
  std::uint64_t face_corners = 0;
  std::uint64_t compiled_vertices = 0;
  std::uint64_t triangles = 0;
  std::uint64_t indices = 0;
};

/// Describes scene, asset, selection, and geometry scale discovered in the fixture.
struct FixtureCounts {
  std::uint64_t entities = 0;
  std::uint64_t objects = 0;
  std::uint64_t mesh_entities = 0;
  std::uint64_t light_entities = 0;
  std::uint64_t empty_entities = 0;
  std::uint64_t locked_entities = 0;
  std::uint64_t unselectable_entities = 0;
  std::uint64_t initially_selected_entities = 0;
  std::uint64_t select_all_entities = 0;
  std::uint64_t material_assets = 0;
  std::uint64_t texture_assets = 0;
  std::uint64_t referenced_mesh_assets = 0;
  std::uint64_t orphan_mesh_assets = 0;
  GeometryCounts stored_geometry;
  GeometryCounts referenced_geometry;
  GeometryCounts selectable_geometry;
  std::vector<quader::EntityId> initial_selection;
  quader::EntityId initial_active = quader::kInvalidEntityId;
  std::vector<quader::EntityId> selectable_entities;
};

/// Preserves the selected entities and active entity restored between benchmark samples.
struct SelectionBaseline {
  std::vector<quader::EntityId> selected;
  quader::EntityId active = quader::kInvalidEntityId;
};

/// Captures one entity's optional transform for exact post-benchmark restoration checks.
struct EntityTransformSnapshot {
  quader::EntityId entity = quader::kInvalidEntityId;
  bool present = false;
  quader::TransformComponent transform;
};

/// Records per-frame timings and observable results from the navigation workload.
struct NavigationRun {
  std::vector<double> frame_ms;
  std::uint64_t checksum = kFnvOffset;
  std::uint64_t changed_frames = 0;
  std::uint64_t consumed_frames = 0;
};

/// Records timings and reuse observables from repeated render-snapshot extraction.
struct SnapshotRun {
  std::vector<double> frame_ms;
  std::uint64_t checksum = kFnvOffset;
  std::uint64_t reused_frames = 0;
  std::uint64_t zero_dirty_frames = 0;
};

/// Records timings and rendered primitive counts from repeated overlay construction.
struct OverlayRun {
  std::vector<double> frame_ms;
  std::uint64_t checksum = kFnvOffset;
  std::uint64_t frame_signature = 0;
  std::uint64_t frames = 0;
  std::uint64_t active_camera_layers = 0;
  std::uint64_t triangles = 0;
  std::uint64_t lines = 0;
  std::uint64_t points = 0;
  std::uint64_t icons = 0;
  std::uint64_t selected_mesh_bounds = 0;
  std::uint64_t selected_lights = 0;
};

/// Records preview timings, checksums, and restoration results for transform updates.
struct TransformRun {
  std::vector<double> update_ms;
  std::vector<double> scene_snapshot_refresh_ms;
  std::vector<double> modeling_overlay_refresh_ms;
  std::vector<double> overlay_snapshot_refresh_ms;
  std::vector<double> overlay_frame_build_ms;
  std::vector<double> overlay_camera_build_ms;
  std::vector<double> frame_cpu_ms;
  std::uint64_t preview_checksum = kFnvOffset;
  std::uint64_t frame_checksum = kFnvOffset;
  std::uint64_t changed_updates = 0;
  std::uint64_t changed_transforms = 0;
  std::uint64_t scene_snapshot_refreshes = 0;
  std::uint64_t overlay_local_bounds_builds = 0;
  std::uint64_t overlay_local_bounds_reuses = 0;
  std::uint64_t overlay_transform_refreshes = 0;
  std::uint64_t overlay_world_bounds_builds = 0;
  std::uint64_t overlay_world_bounds_translation_reuses = 0;
  std::uint64_t overlay_lines = 0;
  std::uint64_t before_transform_checksum = 0;
  std::uint64_t preview_transform_checksum = 0;
  std::uint64_t restored_transform_checksum = 0;
  bool restored_exactly = false;
};

/// Aggregates fixture metadata, phase timings, checksums, and validation outcomes.
struct BenchmarkReport {
  BenchmarkOptions options;
  std::uintmax_t fixture_bytes = 0;
  std::uint64_t fixture_checksum = 0;
  bool input_unchanged = false;
  FixtureCounts fixture;
  TimingSummary load;
  TimingSummary session_open;
  TimingSummary select_all;
  TimingSummary navigation;
  TimingSummary snapshot_reuse;
  TimingSummary overlay_build;
  TimingSummary transform_update;
  TimingSummary transform_scene_snapshot_refresh;
  TimingSummary transform_modeling_overlay_refresh;
  TimingSummary transform_overlay_snapshot_refresh;
  TimingSummary transform_overlay_frame_build;
  TimingSummary transform_overlay_camera_build;
  TimingSummary transform_frame_cpu;
  std::uint64_t select_all_checksum = 0;
  std::uint64_t session_initial_selection_count = 0;
  std::uint64_t selection_benchmark_baseline_count = 0;
  bool selection_restored_after_samples = false;
  NavigationRun navigation_observables;
  bool navigation_replay_verified = false;
  SnapshotRun snapshot_observables;
  OverlayRun overlay_observables;
  TransformRun transform_observables;
};

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string message) {
  if (!condition) {
    fail(std::move(message));
  }
}

std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8U);
  for (const char character : value) {
    switch (character) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped += character;
      break;
    }
  }
  return escaped;
}

std::uint64_t parse_u64(std::string_view text, std::string_view name) {
  try {
    std::size_t parsed = 0;
    const std::uint64_t value = std::stoull(std::string(text), &parsed);
    if (parsed != text.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return value;
  } catch (const std::exception &) {
    throw std::invalid_argument("Invalid " + std::string(name) + ": " +
                                std::string(text));
  }
}

int parse_count(std::string_view text, std::string_view name, bool allow_zero) {
  const std::uint64_t value = parse_u64(text, name);
  if ((!allow_zero && value == 0U) ||
      value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Invalid " + std::string(name) + ": " +
                                std::string(text));
  }
  return static_cast<int>(value);
}

std::filesystem::path normalized_absolute_path(
    const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (error) {
    absolute = path;
  }
  const std::filesystem::path normalized =
      std::filesystem::weakly_canonical(absolute, error);
  return error ? absolute.lexically_normal() : normalized;
}

BenchmarkOptions parse_args(int argc, char **argv) {
  BenchmarkOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto require_value = [&](std::string_view name) -> std::string_view {
      if (index + 1 >= argc) {
        throw std::invalid_argument("Missing value for " + std::string(name));
      }
      ++index;
      return argv[index];
    };

    if (argument == "--help" || argument == "-h") {
      std::cout
          << "Usage: qdr_fixture_performance_baseline --qdr project.qdr "
             "[--output report.json]\n"
          << "       [--load-samples N] [--select-warmup N] "
             "[--select-samples N]\n"
          << "       [--navigation-warmup N] [--navigation-frames N] "
             "[--snapshot-frames N] [--overlay-frames N]\n"
          << "       [--transform-warmup N] [--transform-samples N]\n";
      std::exit(0);
    }
    if (argument == "--qdr") {
      options.qdr_path = std::filesystem::path(require_value(argument));
    } else if (argument == "--output") {
      options.output_path = std::filesystem::path(require_value(argument));
    } else if (argument == "--load-samples") {
      options.load_samples = parse_count(require_value(argument), argument,
                                         false);
    } else if (argument == "--select-warmup") {
      options.select_warmup = parse_count(require_value(argument), argument,
                                          true);
    } else if (argument == "--select-samples") {
      options.select_samples = parse_count(require_value(argument), argument,
                                           false);
    } else if (argument == "--navigation-warmup") {
      options.navigation_warmup = parse_count(require_value(argument),
                                              argument, true);
    } else if (argument == "--navigation-frames") {
      options.navigation_frames = parse_count(require_value(argument),
                                              argument, false);
    } else if (argument == "--snapshot-frames") {
      options.snapshot_frames = parse_count(require_value(argument), argument,
                                            false);
    } else if (argument == "--overlay-frames") {
      options.overlay_frames = parse_count(require_value(argument), argument,
                                           false);
    } else if (argument == "--transform-warmup") {
      options.transform_warmup = parse_count(require_value(argument), argument,
                                             true);
    } else if (argument == "--transform-samples") {
      options.transform_samples = parse_count(require_value(argument), argument,
                                              false);
    } else {
      throw std::invalid_argument("Unknown argument: " +
                                  std::string(argument));
    }
  }

  if (options.qdr_path.empty()) {
    throw std::invalid_argument("--qdr is required.");
  }
  options.qdr_path = normalized_absolute_path(options.qdr_path);
  if (!options.output_path.empty()) {
    options.output_path = normalized_absolute_path(options.output_path);
    if (options.output_path == options.qdr_path) {
      throw std::invalid_argument(
          "--output must not resolve to the input QDR path.");
    }
  }
  return options;
}

double elapsed_milliseconds(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

double nearest_rank_percentile(const std::vector<double> &sorted,
                               double percentile) {
  require(!sorted.empty(), "Cannot summarize an empty timing sample.");
  const double rank = std::ceil(percentile * static_cast<double>(sorted.size()));
  const std::size_t index = static_cast<std::size_t>(
      std::clamp(rank, 1.0, static_cast<double>(sorted.size())) - 1.0);
  return sorted[index];
}

TimingSummary summarize_timings(const std::vector<double> &samples) {
  require(!samples.empty(), "A benchmark scenario produced no timing samples.");
  std::vector<double> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  double total = 0.0;
  for (const double sample : samples) {
    total += sample;
  }
  return {
      .samples = samples.size(),
      .total_ms = total,
      .average_ms = total / static_cast<double>(samples.size()),
      .minimum_ms = sorted.front(),
      .median_ms = nearest_rank_percentile(sorted, 0.50),
      .p95_ms = nearest_rank_percentile(sorted, 0.95),
      .p99_ms = nearest_rank_percentile(sorted, 0.99),
      .maximum_ms = sorted.back(),
  };
}

void hash_byte(std::uint64_t &checksum, std::uint8_t value) {
  checksum ^= value;
  checksum *= kFnvPrime;
}

void hash_u64(std::uint64_t &checksum, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    hash_byte(checksum,
              static_cast<std::uint8_t>((value >> (byte * 8)) & 0xffU));
  }
}

void hash_float(std::uint64_t &checksum, float value) {
  hash_u64(checksum, std::bit_cast<std::uint32_t>(value));
}

void hash_vec3(std::uint64_t &checksum, quader::QVec3 value) {
  hash_float(checksum, value.x);
  hash_float(checksum, value.y);
  hash_float(checksum, value.z);
}

std::string checksum_hex(std::uint64_t checksum) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(16) << std::setfill('0')
         << checksum;
  return stream.str();
}

std::uint64_t file_checksum(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    fail("Could not read fixture bytes for checksum: " + path.string());
  }
  std::uint64_t checksum = kFnvOffset;
  std::array<char, 64U * 1024U> buffer{};
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = stream.gcount();
    for (std::streamsize index = 0; index < count; ++index) {
      hash_byte(checksum,
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(buffer[index])));
    }
  }
  return checksum;
}

GeometryCounts geometry_counts(const quader::AssetRegistry &assets,
                               const std::vector<quader::AssetId> &mesh_ids) {
  GeometryCounts counts;
  counts.meshes = mesh_ids.size();
  for (const quader::AssetId mesh_id : mesh_ids) {
    const quader::MeshAsset *mesh = assets.mesh(mesh_id);
    require(mesh != nullptr,
            "Fixture references missing mesh asset " +
                std::to_string(mesh_id) + ".");
    const quader::MeshGeometryPayload &geometry = mesh->geometry();
    counts.compiled_vertices += geometry.vertices.size();
    counts.indices += geometry.indices.size();
    require(geometry.indices.size() % 3U == 0U,
            "Mesh asset " + std::to_string(mesh_id) +
                " has a non-triangular compiled index count.");
    for (const std::uint32_t index : geometry.indices) {
      require(index < geometry.vertices.size(),
              "Mesh asset " + std::to_string(mesh_id) +
                  " has an out-of-range compiled index.");
    }
    counts.triangles += geometry.indices.size() / 3U;
    if (mesh->has_authored_polygon_document &&
        mesh->authored_polygon_document != nullptr) {
      ++counts.authored_meshes;
      const quader_poly::Document &document =
          *mesh->authored_polygon_document;
      counts.authored_vertices += document.vertices.size();
      counts.polygon_faces += document.faces.size();
      for (const quader_poly::Face &face : document.faces) {
        counts.face_corners += face.vertices.size();
      }
    }
  }
  return counts;
}

std::vector<quader::EntityId>
sorted_entities(std::vector<quader::EntityId> entities) {
  std::sort(entities.begin(), entities.end());
  return entities;
}

FixtureCounts analyze_fixture(const quader::SceneContentDocument &document) {
  const quader::SceneContentDocumentIdentityReport identity =
      document.validate_object_identity_invariants();
  require(identity.ok(), "Fixture object identity validation failed.");
  const quader::SceneContentDocumentAssetReferenceReport references =
      document.validate_asset_references();
  require(references.ok(), "Fixture asset-reference validation failed.");

  FixtureCounts counts;
  const quader::SceneDocument &scene = document.scene();
  const quader::AssetRegistry &assets = document.assets();
  const std::vector<quader::EntityId> entities = scene.entities();
  counts.entities = entities.size();
  counts.objects = document.object_records().size();
  counts.material_assets = assets.material_count();
  counts.texture_assets = assets.texture_count();
  counts.initial_selection = sorted_entities(scene.selected_entities());
  counts.initially_selected_entities = counts.initial_selection.size();
  counts.initial_active = scene.primary_selection();

  std::set<quader::AssetId> referenced_meshes;
  std::set<quader::AssetId> selectable_meshes;
  for (const quader::EntityId entity : entities) {
    switch (scene.kind(entity)) {
    case quader::EntityKind::Mesh:
      ++counts.mesh_entities;
      break;
    case quader::EntityKind::Light:
      ++counts.light_entities;
      break;
    case quader::EntityKind::Empty:
      ++counts.empty_entities;
      break;
    case quader::EntityKind::Generic:
      break;
    }
    if (scene.is_effectively_locked(entity)) {
      ++counts.locked_entities;
    }
    const quader::VisibilityComponent *visibility = scene.visibility(entity);
    const bool selectable = visibility == nullptr || visibility->selectable;
    if (!selectable) {
      ++counts.unselectable_entities;
    }
    if (const quader::MeshComponent *mesh = scene.mesh(entity)) {
      referenced_meshes.insert(mesh->mesh);
    }
    const bool included_in_select_all =
        !scene.is_scene_root(entity) && !scene.is_effectively_locked(entity) &&
        selectable;
    if (included_in_select_all) {
      counts.selectable_entities.push_back(entity);
      if (const quader::MeshComponent *mesh = scene.mesh(entity)) {
        selectable_meshes.insert(mesh->mesh);
      }
    }
  }

  counts.selectable_entities =
      sorted_entities(std::move(counts.selectable_entities));
  counts.select_all_entities = counts.selectable_entities.size();
  const std::vector<quader::AssetId> all_meshes = assets.mesh_ids();
  const std::vector<quader::AssetId> referenced(referenced_meshes.begin(),
                                                 referenced_meshes.end());
  const std::vector<quader::AssetId> selectable(selectable_meshes.begin(),
                                                selectable_meshes.end());
  counts.referenced_mesh_assets = referenced.size();
  require(referenced.size() <= all_meshes.size(),
          "Fixture references more meshes than it stores.");
  counts.orphan_mesh_assets = all_meshes.size() - referenced.size();
  counts.stored_geometry = geometry_counts(assets, all_meshes);
  counts.referenced_geometry = geometry_counts(assets, referenced);
  counts.selectable_geometry = geometry_counts(assets, selectable);
  return counts;
}

bool same_float_bits(float left, float right) {
  return std::bit_cast<std::uint32_t>(left) ==
         std::bit_cast<std::uint32_t>(right);
}

bool same_transform(const quader::TransformComponent &left,
                    const quader::TransformComponent &right) {
  return same_float_bits(left.position.x, right.position.x) &&
         same_float_bits(left.position.y, right.position.y) &&
         same_float_bits(left.position.z, right.position.z) &&
         same_float_bits(left.rotation_euler.x, right.rotation_euler.x) &&
         same_float_bits(left.rotation_euler.y, right.rotation_euler.y) &&
         same_float_bits(left.rotation_euler.z, right.rotation_euler.z) &&
         same_float_bits(left.scale.x, right.scale.x) &&
         same_float_bits(left.scale.y, right.scale.y) &&
         same_float_bits(left.scale.z, right.scale.z) &&
         left.custom_pivot == right.custom_pivot;
}

std::vector<EntityTransformSnapshot>
capture_transforms(const quader::SceneDocument &scene) {
  const std::vector<quader::EntityId> entities =
      sorted_entities(scene.entities());
  std::vector<EntityTransformSnapshot> snapshots;
  snapshots.reserve(entities.size());
  for (const quader::EntityId entity : entities) {
    EntityTransformSnapshot snapshot{.entity = entity};
    if (const quader::TransformComponent *transform = scene.transform(entity)) {
      snapshot.present = true;
      snapshot.transform = *transform;
    }
    snapshots.push_back(snapshot);
  }
  return snapshots;
}

bool same_transforms(const std::vector<EntityTransformSnapshot> &left,
                     const std::vector<EntityTransformSnapshot> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].entity != right[index].entity ||
        left[index].present != right[index].present ||
        (left[index].present &&
         !same_transform(left[index].transform, right[index].transform))) {
      return false;
    }
  }
  return true;
}

std::uint64_t
transform_checksum(const std::vector<EntityTransformSnapshot> &snapshots) {
  std::uint64_t checksum = kFnvOffset;
  for (const EntityTransformSnapshot &snapshot : snapshots) {
    hash_u64(checksum, snapshot.entity);
    hash_u64(checksum, snapshot.present ? 1U : 0U);
    if (!snapshot.present) {
      continue;
    }
    hash_vec3(checksum, snapshot.transform.position);
    hash_vec3(checksum, snapshot.transform.rotation_euler);
    hash_vec3(checksum, snapshot.transform.scale);
    hash_u64(checksum, snapshot.transform.custom_pivot ? 1U : 0U);
  }
  return checksum;
}

std::uint64_t changed_transform_count(
    const std::vector<EntityTransformSnapshot> &before,
    const std::vector<EntityTransformSnapshot> &after) {
  require(before.size() == after.size(),
          "Scene entity count changed during transform preview.");
  std::uint64_t changed = 0;
  for (std::size_t index = 0; index < before.size(); ++index) {
    require(before[index].entity == after[index].entity &&
                before[index].present == after[index].present,
            "Scene transform ownership changed during preview.");
    if (before[index].present &&
        !same_transform(before[index].transform, after[index].transform)) {
      ++changed;
    }
  }
  return changed;
}

std::uint64_t entity_checksum(const std::vector<quader::EntityId> &entities) {
  std::uint64_t checksum = kFnvOffset;
  for (const quader::EntityId entity : entities) {
    hash_u64(checksum, entity);
  }
  return checksum;
}

void require_api_result(const quader::QuaderApiStatus &result,
                        std::string_view operation) {
  if (!result.succeeded()) {
    fail(std::string(operation) + " failed: " + result.error_message());
  }
}

void run_select_all_cycle(quader::QuaderEditorApi &editor,
                          quader::SceneDocument &scene,
                          const FixtureCounts &fixture,
                          const SelectionBaseline &baseline,
                          std::vector<double> *timings,
                          std::uint64_t &selection_checksum) {
  const Clock::time_point begin = Clock::now();
  const quader::QuaderApiVoidResult selected = editor.selection().select_all();
  const Clock::time_point end = Clock::now();
  if (timings != nullptr) {
    timings->push_back(elapsed_milliseconds(begin, end));
  }
  require_api_result(selected, "Select all");
  require(selected.did_change(), "Select all unexpectedly made no change.");
  const std::vector<quader::EntityId> current =
      sorted_entities(scene.selected_entities());
  require(current == fixture.selectable_entities,
          "Select all did not select the exact eligible entity set.");
  hash_u64(selection_checksum, entity_checksum(current));
  hash_u64(selection_checksum, scene.primary_selection());

  const quader::QuaderApiVoidResult undo = editor.history().undo();
  require_api_result(undo, "Undo select all");
  require(sorted_entities(scene.selected_entities()) == baseline.selected,
          "Undo did not restore the initial fixture selection.");
  require(scene.primary_selection() == baseline.active,
          "Undo did not restore the initial active entity.");
}

NavigationRun run_navigation(int frame_count, bool collect_timings) {
  quader::ViewportInputRouter router;
  quader::ViewportCameraController camera;
  quader::ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {1920.0F, 1080.0F};
  input.mouse_screen = {200.0F, 150.0F};
  input.navigation_bindings.orbit_pressed = true;
  input.navigation_bindings.orbit_down = true;
  const quader::ViewportInputResult begin = router.handle_frame(input, camera);
  require(begin.consumed && begin.navigation_active,
          "Continuous orbit navigation did not begin.");

  NavigationRun run;
  if (collect_timings) {
    run.frame_ms.reserve(static_cast<std::size_t>(frame_count));
  }
  for (int frame = 0; frame < frame_count; ++frame) {
    input.navigation_bindings = {};
    input.navigation_bindings.orbit_down = true;
    input.mouse_screen = {
        200.0F + static_cast<float>(frame + 1) * 0.50F,
        150.0F + static_cast<float>(frame + 1) * 0.125F,
    };
    const Clock::time_point frame_begin = Clock::now();
    const quader::ViewportInputResult result =
        router.handle_frame(input, camera);
    const Clock::time_point frame_end = Clock::now();
    if (collect_timings) {
      run.frame_ms.push_back(elapsed_milliseconds(frame_begin, frame_end));
    }
    require(result.camera_changed && result.navigation_active,
            "A continuous navigation frame did not update the camera.");
    run.changed_frames += result.camera_changed ? 1U : 0U;
    run.consumed_frames += result.consumed ? 1U : 0U;
    const quader::ViewportCameraState &state = camera.state();
    const quader::ViewportCameraPose pose = camera.pose();
    hash_u64(run.checksum, static_cast<std::uint64_t>(frame));
    hash_vec3(run.checksum, state.target);
    hash_float(run.checksum, state.yaw_radians);
    hash_float(run.checksum, state.pitch_radians);
    hash_float(run.checksum, state.distance);
    hash_u64(run.checksum,
             static_cast<std::uint64_t>(state.projection));
    hash_vec3(run.checksum, pose.eye);
    hash_vec3(run.checksum, pose.target);
    hash_vec3(run.checksum, pose.up);
  }

  input.navigation_bindings = {};
  input.navigation_bindings.orbit_released = true;
  const quader::ViewportInputResult released = router.handle_frame(input, camera);
  require(released.consumed && !released.navigation_active &&
              !router.navigating(),
          "Continuous orbit navigation did not end cleanly.");
  return run;
}

std::uint64_t snapshot_checksum(const quader::RenderSceneSnapshot &snapshot) {
  std::uint64_t checksum = kFnvOffset;
  hash_u64(checksum, snapshot.scene_revision);
  hash_u64(checksum, snapshot.selection_revision);
  hash_u64(checksum, snapshot.visibility_revision);
  hash_u64(checksum, snapshot.transform_revision);
  hash_u64(checksum, snapshot.mesh_binding_revision);
  hash_u64(checksum, snapshot.render_component_revision);
  hash_u64(checksum, snapshot.asset_revision);
  hash_u64(checksum, snapshot.meshes.size());
  hash_u64(checksum, snapshot.lights.size());
  hash_u64(checksum, snapshot.reused_previous_records ? 1U : 0U);
  for (const quader::RenderSceneMeshRecord &mesh : snapshot.meshes) {
    hash_u64(checksum, mesh.entity_id);
    hash_u64(checksum, mesh.mesh.mesh_id);
    hash_u64(checksum, mesh.transform_revision);
    hash_u64(checksum, mesh.mesh_dirty ? 1U : 0U);
    hash_u64(checksum, mesh.material_dirty ? 1U : 0U);
    hash_u64(checksum, mesh.transform_dirty ? 1U : 0U);
    hash_u64(checksum, mesh.binding_dirty ? 1U : 0U);
  }
  for (const quader::RenderSceneLightRecord &light : snapshot.lights) {
    hash_u64(checksum, light.entity_id);
    hash_u64(checksum, light.rebuild_revision);
    hash_u64(checksum, light.transform_revision);
    hash_u64(checksum, light.light_dirty ? 1U : 0U);
    hash_u64(checksum, light.transform_dirty ? 1U : 0U);
  }
  return checksum;
}

bool snapshot_has_zero_dirty_records(
    const quader::RenderSceneSnapshot &snapshot) {
  if (snapshot.dirty.any_dirty()) {
    return false;
  }
  for (const quader::RenderSceneMeshRecord &mesh : snapshot.meshes) {
    if (mesh.mesh_dirty || mesh.material_dirty || mesh.transform_dirty ||
        mesh.binding_dirty) {
      return false;
    }
  }
  for (const quader::RenderSceneLightRecord &light : snapshot.lights) {
    if (light.light_dirty || light.transform_dirty) {
      return false;
    }
  }
  return true;
}

SnapshotRun run_snapshot_reuse(const quader::SceneDocument &scene,
                               const quader::AssetRegistry &assets,
                               int frame_count) {
  quader::RenderSceneSnapshot previous =
      quader::build_render_scene_snapshot(scene, assets);
  SnapshotRun run;
  run.frame_ms.reserve(static_cast<std::size_t>(frame_count));
  std::optional<std::uint64_t> stable_frame_checksum;
  for (int frame = 0; frame < frame_count; ++frame) {
    const Clock::time_point frame_begin = Clock::now();
    quader::RenderSceneSnapshot current = quader::build_render_scene_snapshot(
        scene, assets,
        quader::RenderSceneSnapshotBuildOptions{
            .previous = &previous,
            .frame_index = static_cast<std::uint64_t>(frame + 1),
        });
    const Clock::time_point frame_end = Clock::now();
    run.frame_ms.push_back(elapsed_milliseconds(frame_begin, frame_end));
    require(current.reused_previous_records,
            "An unchanged render snapshot did not reuse previous records.");
    require(snapshot_has_zero_dirty_records(current),
            "An unchanged render snapshot reported dirty work.");
    ++run.reused_frames;
    ++run.zero_dirty_frames;
    const std::uint64_t current_checksum = snapshot_checksum(current);
    if (!stable_frame_checksum.has_value()) {
      stable_frame_checksum = current_checksum;
    } else {
      require(*stable_frame_checksum == current_checksum,
              "Unchanged render snapshot checksum drifted between frames.");
    }
    hash_u64(run.checksum, current_checksum);
    hash_u64(run.checksum, static_cast<std::uint64_t>(frame));
    previous = std::move(current);
  }
  return run;
}

void hash_overlay_mesh(std::uint64_t &checksum,
                       const quader::OverlayFaceFillMesh &mesh) {
  hash_u64(checksum, mesh.vertices.size());
  hash_u64(checksum, mesh.indices.size());
}

void hash_overlay_mesh(std::uint64_t &checksum,
                       const quader::OverlayLineMesh &mesh) {
  hash_u64(checksum, mesh.vertices.size());
  hash_u64(checksum, mesh.indices.size());
}

void hash_overlay_mesh(std::uint64_t &checksum,
                       const quader::OverlayPointMesh &mesh) {
  hash_u64(checksum, mesh.vertices.size());
  hash_u64(checksum, mesh.indices.size());
}

void hash_overlay_plan(std::uint64_t &checksum,
                       const quader::OverlayLayerBuildPlan &plan) {
  hash_u64(checksum, plan.face_fill_batches.size());
  hash_u64(checksum, plan.line_batches.size());
  hash_u64(checksum, plan.edit_wire_batches.size());
  hash_u64(checksum, plan.point_batches.size());
  hash_u64(checksum, plan.icon_batches.size());
  for (const quader::OverlayFaceFillBatch &batch : plan.face_fill_batches) {
    hash_overlay_mesh(checksum, batch.mesh);
  }
  for (const quader::OverlayLineBatch &batch : plan.line_batches) {
    hash_overlay_mesh(checksum, batch.mesh);
  }
  for (const quader::OverlayEditWireBatch &batch : plan.edit_wire_batches) {
    hash_overlay_mesh(checksum, batch.mesh);
  }
  for (const quader::OverlayPointBatch &batch : plan.point_batches) {
    hash_overlay_mesh(checksum, batch.mesh);
  }
  for (const quader::OverlayIconBatch &batch : plan.icon_batches) {
    hash_overlay_mesh(checksum, batch.mesh);
  }
}

OverlayRun run_overlay_workload(quader::QuaderEditorApi &editor,
                                quader::SceneSession &session,
                                const FixtureCounts &fixture,
                                int frame_count) {
  const quader::QuaderApiVoidResult selected = editor.selection().select_all();
  require_api_result(selected, "Select all for overlay workload");
  require(sorted_entities(session.scene().selected_entities()) ==
              fixture.selectable_entities,
          "Overlay workload did not start from the expected all selection.");

  quader::ViewportOverlayFrame base_frame =
      session.modeling().selection_overlay_frame(
          {.suppress_persistent_object_selection = true});
  const quader::RenderOverlaySelectionSnapshot scene_snapshot =
      quader::build_render_overlay_selection_snapshot(session.scene(),
                                                      session.assets());
  const quader::ViewportOverlayFrame frame =
      quader::ViewportSceneOverlayBuilder::build_selection_frame(
          std::move(base_frame), scene_snapshot, true,
          quader::RenderOverlaySelectionMode::Object);
  const quader::OverlayFrameSignatures signatures =
      quader::OverlayRenderableBuilder::frame_signatures(frame);

  std::vector<quader::ViewportOverlayLayer> active_layers;
  for (std::size_t index = 0; index < quader::kViewportOverlayLayerCount;
       ++index) {
    const auto layer = static_cast<quader::ViewportOverlayLayer>(index);
    if (quader::OverlayRenderableBuilder::layer_build_depends_on_camera(layer) &&
        quader::OverlayRenderableBuilder::layer_has_renderable_primitives(
            signatures, layer)) {
      active_layers.push_back(layer);
    }
  }
  require(!active_layers.empty(),
          "All-selection overlay produced no camera-dependent layers.");

  OverlayRun run;
  run.frame_ms.reserve(static_cast<std::size_t>(frame_count));
  run.frame_signature = signatures.frame_signature;
  run.active_camera_layers = active_layers.size();
  run.triangles = frame.triangles.size();
  run.lines = frame.lines.size();
  run.points = frame.points.size();
  run.icons = frame.icons.size();
  run.selected_mesh_bounds = scene_snapshot.selected_mesh_bounds.size();
  run.selected_lights = scene_snapshot.selected_lights.size();

  quader::ViewportCameraController camera;
  camera.set_viewport_size(1920.0F, 1080.0F);
  camera.begin_drag({200.0F, 150.0F}, quader::ViewportDragMode::Orbit);
  for (int overlay_frame = 0; overlay_frame < frame_count; ++overlay_frame) {
    camera.update_drag(
        {200.0F + static_cast<float>(overlay_frame + 1) * 0.50F,
         150.0F + static_cast<float>(overlay_frame + 1) * 0.125F});
    const quader::ViewportOverlayCamera overlay_camera =
        quader::ViewportCameraAdapter::overlay_camera(camera,
                                                      {1920.0F, 1080.0F});
    std::vector<quader::OverlayLayerBuildPlan> plans;
    plans.reserve(active_layers.size());
    const Clock::time_point frame_begin = Clock::now();
    for (const quader::ViewportOverlayLayer layer : active_layers) {
      plans.push_back(quader::OverlayRenderableBuilder::build_layer(
          frame, overlay_camera, layer, &signatures));
    }
    const Clock::time_point frame_end = Clock::now();
    run.frame_ms.push_back(elapsed_milliseconds(frame_begin, frame_end));
    ++run.frames;
    hash_u64(run.checksum,
             quader::OverlayRenderableBuilder::camera_signature(
                 overlay_camera));
    for (std::size_t index = 0; index < plans.size(); ++index) {
      hash_u64(run.checksum,
               static_cast<std::uint64_t>(active_layers[index]));
      hash_overlay_plan(run.checksum, plans[index]);
    }
  }
  camera.end_drag();
  return run;
}

quader::QuaderRay stress_viewport_forward_ray() {
  return {
      .origin = {0.0F, 0.0F, 10.0F},
      .direction = {0.0F, 0.0F, -1.0F},
  };
}

quader::QuaderViewportCamera stress_orthographic_camera() {
  return {
      .orthographic = true,
      .origin = {0.0F, 0.0F, 10.0F},
      .x_axis = {1.0F, 0.0F, 0.0F},
      .y_axis = {0.0F, 1.0F, 0.0F},
      .z_axis = {0.0F, 0.0F, 1.0F},
      .viewport_size = {100.0F, 100.0F},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
}

quader::QuaderTransformDragBeginRequest transform_begin_request() {
  return {
      .tool = quader::QuaderTransformTool::Move,
      .operation = quader::QuaderTransformOperation::Transform,
      .axis = quader::QuaderTransformAxis::X,
      .screen_position = {0.0F, 0.0F},
      .screen_pivot = {0.0F, 0.0F},
      .screen_tip = {10.0F, 0.0F},
      .has_world_direction = true,
      .world_direction = {1.0F, 0.0F, 0.0F},
      .ray = stress_viewport_forward_ray(),
      .camera = stress_orthographic_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
  };
}

quader::QuaderTransformDragUpdateRequest
transform_update_request(float screen_x) {
  return {
      .screen_position = {screen_x, 0.0F},
      .ray = stress_viewport_forward_ray(),
      .camera = stress_orthographic_camera(),
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
      .precision = false,
  };
}

void hash_transform_preview(std::uint64_t &checksum,
                            const quader::QuaderTransformDragPreview &preview,
                            int sample) {
  hash_u64(checksum, static_cast<std::uint64_t>(sample));
  hash_u64(checksum, preview.active ? 1U : 0U);
  hash_u64(checksum, preview.changed ? 1U : 0U);
  hash_u64(checksum, preview.scene_transform ? 1U : 0U);
  hash_vec3(checksum, preview.move_delta);
  hash_float(checksum, static_cast<float>(preview.rotate_angle_degrees));
  hash_vec3(checksum, preview.scale);
  hash_u64(checksum, preview.content_revision);
  hash_u64(checksum, preview.selection_revision);
}

TransformRun run_transform_preview(quader::QuaderEditorApi &editor,
                                   quader::ModelingSessionService &modeling,
                                   quader::SceneDocument &scene,
                                   const quader::AssetRegistry &assets,
                                   const FixtureCounts &fixture,
                                   const BenchmarkOptions &options) {
  const quader::QuaderApiVoidResult selected = editor.selection().select_all();
  require_api_result(selected, "Select all for transform preview");
  require(sorted_entities(scene.selected_entities()) == fixture.selectable_entities,
          "Transform preview did not start from the expected all selection.");

  TransformRun run;
  const std::vector<EntityTransformSnapshot> before = capture_transforms(scene);
  run.before_transform_checksum = transform_checksum(before);
  quader::RenderSceneSnapshot render_snapshot =
      quader::build_render_scene_snapshot(scene, assets);
  quader::RenderOverlaySnapshotCache overlay_snapshot_cache;
  (void)overlay_snapshot_cache.selection_snapshot(scene, assets);
  quader::ViewportCameraController overlay_camera_controller;
  overlay_camera_controller.set_viewport_size(1920.0F, 1080.0F);
  const quader::ViewportOverlayCamera overlay_camera =
      quader::ViewportCameraAdapter::overlay_camera(
          overlay_camera_controller, {1920.0F, 1080.0F});
  const quader::ViewportSettings overlay_settings =
      quader::ViewportSettings::defaults();
  const quader::QuaderApiResult<quader::QuaderTransformDragPreview> begin =
      editor.modeling().begin_transform_drag(transform_begin_request());
  require_api_result(begin, "Begin transform preview");
  require(begin.value().active && begin.value().scene_transform,
          "Transform preview did not enter scene-transform mode.");

  const int total_updates = options.transform_warmup + options.transform_samples;
  run.update_ms.reserve(static_cast<std::size_t>(options.transform_samples));
  run.scene_snapshot_refresh_ms.reserve(
      static_cast<std::size_t>(options.transform_samples));
  run.modeling_overlay_refresh_ms.reserve(
      static_cast<std::size_t>(options.transform_samples));
  run.overlay_snapshot_refresh_ms.reserve(
      static_cast<std::size_t>(options.transform_samples));
  run.overlay_frame_build_ms.reserve(
      static_cast<std::size_t>(options.transform_samples));
  run.overlay_camera_build_ms.reserve(
      static_cast<std::size_t>(options.transform_samples));
  run.frame_cpu_ms.reserve(static_cast<std::size_t>(options.transform_samples));
  for (int update = 0; update < total_updates; ++update) {
    const float screen_x = 1.0F + static_cast<float>(update + 1) * 0.03125F;
    const Clock::time_point update_begin = Clock::now();
    const quader::QuaderApiResult<quader::QuaderTransformDragPreview> result =
        editor.modeling().update_transform_drag(
            transform_update_request(screen_x));
    const Clock::time_point update_end = Clock::now();
    require_api_result(result, "Update transform preview");
    require(result.value().active && result.value().changed,
            "A distinct transform preview update made no change.");

    const Clock::time_point scene_snapshot_begin = Clock::now();
    require(quader::try_refresh_render_scene_snapshot_transforms(
                render_snapshot, scene, assets,
                static_cast<std::uint64_t>(update + 1)),
            "Transform frame could not use the retained render snapshot.");
    const Clock::time_point scene_snapshot_end = Clock::now();

    const Clock::time_point modeling_overlay_begin = Clock::now();
    quader::ViewportOverlayFrame modeling_overlay =
        modeling.selection_overlay_frame(
            {.suppress_persistent_object_selection = true});
    const Clock::time_point modeling_overlay_end = Clock::now();

    const Clock::time_point overlay_snapshot_begin = Clock::now();
    const quader::RenderOverlaySelectionSnapshot &selection_snapshot =
        overlay_snapshot_cache.selection_snapshot(scene, assets);
    const Clock::time_point overlay_snapshot_end = Clock::now();

    const Clock::time_point overlay_frame_begin = Clock::now();
    quader::ViewportOverlayFrame overlay_frame =
        quader::ViewportSceneOverlayBuilder::build_selection_frame(
            std::move(modeling_overlay), selection_snapshot, true,
            quader::RenderOverlaySelectionMode::Object);
    quader::ViewportOverlayStyleResolver::apply(
        overlay_frame, overlay_settings,
        {.selection_mode = quader::ViewportOverlaySelectionContext::Object});
    const quader::OverlayFrameSignatures overlay_signatures =
        quader::OverlayRenderableBuilder::frame_signatures(overlay_frame);
    const Clock::time_point overlay_frame_end = Clock::now();

    const Clock::time_point overlay_camera_begin = Clock::now();
    std::vector<quader::OverlayLayerBuildPlan> overlay_plans;
    for (std::size_t index = 0; index < quader::kViewportOverlayLayerCount;
         ++index) {
      const auto layer = static_cast<quader::ViewportOverlayLayer>(index);
      if (quader::OverlayRenderableBuilder::layer_build_depends_on_camera(
              layer) &&
          quader::OverlayRenderableBuilder::layer_has_renderable_primitives(
              overlay_signatures, layer)) {
        overlay_plans.push_back(quader::OverlayRenderableBuilder::build_layer(
            overlay_frame, overlay_camera, layer, &overlay_signatures));
      }
    }
    const Clock::time_point overlay_camera_end = Clock::now();

    if (update >= options.transform_warmup) {
      run.update_ms.push_back(elapsed_milliseconds(update_begin, update_end));
      run.scene_snapshot_refresh_ms.push_back(elapsed_milliseconds(
          scene_snapshot_begin, scene_snapshot_end));
      run.modeling_overlay_refresh_ms.push_back(elapsed_milliseconds(
          modeling_overlay_begin, modeling_overlay_end));
      run.overlay_snapshot_refresh_ms.push_back(elapsed_milliseconds(
          overlay_snapshot_begin, overlay_snapshot_end));
      run.overlay_frame_build_ms.push_back(
          elapsed_milliseconds(overlay_frame_begin, overlay_frame_end));
      run.overlay_camera_build_ms.push_back(
          elapsed_milliseconds(overlay_camera_begin, overlay_camera_end));
      run.frame_cpu_ms.push_back(
          elapsed_milliseconds(update_begin, overlay_camera_end));
      ++run.changed_updates;
      ++run.scene_snapshot_refreshes;
      hash_transform_preview(run.preview_checksum, result.value(),
                             update - options.transform_warmup);
      hash_u64(run.frame_checksum, overlay_signatures.frame_signature);
      hash_u64(run.frame_checksum, overlay_frame.lines.size());
      for (const quader::OverlayLayerBuildPlan &plan : overlay_plans) {
        hash_overlay_plan(run.frame_checksum, plan);
      }
    }
    run.overlay_lines = overlay_frame.lines.size();
  }
  const quader::RenderOverlaySnapshotCacheStats &overlay_cache_stats =
      overlay_snapshot_cache.stats();
  require(overlay_cache_stats.selection_transform_refresh_count ==
              static_cast<std::uint64_t>(total_updates),
          "A transform frame fell back to full overlay snapshot extraction.");
  run.overlay_local_bounds_builds =
      overlay_cache_stats.selection_local_bounds_build_count;
  run.overlay_local_bounds_reuses =
      overlay_cache_stats.selection_local_bounds_reuse_count;
  run.overlay_transform_refreshes =
      overlay_cache_stats.selection_transform_refresh_count;
  run.overlay_world_bounds_builds =
      overlay_cache_stats.selection_world_bounds_build_count;
  run.overlay_world_bounds_translation_reuses =
      overlay_cache_stats.selection_world_bounds_translation_reuse_count;

  const std::vector<EntityTransformSnapshot> preview = capture_transforms(scene);
  run.preview_transform_checksum = transform_checksum(preview);
  run.changed_transforms = changed_transform_count(before, preview);
  require(run.changed_transforms == fixture.select_all_entities,
          "Transform preview did not affect every selected entity exactly once.");
  require(run.preview_transform_checksum != run.before_transform_checksum,
          "Transform preview checksum did not change.");

  const quader::QuaderApiVoidResult cancel =
      editor.modeling().cancel_transform_drag();
  require_api_result(cancel, "Cancel transform preview");
  require(cancel.did_change(), "Cancel transform preview reported no change.");
  const std::vector<EntityTransformSnapshot> restored =
      capture_transforms(scene);
  run.restored_transform_checksum = transform_checksum(restored);
  run.restored_exactly = same_transforms(before, restored);
  require(run.restored_exactly &&
              run.restored_transform_checksum == run.before_transform_checksum,
          "Cancel did not restore exact scene transform bits.");
  return run;
}

void write_timing_json(std::ostream &stream, std::string_view name,
                       const TimingSummary &timing, bool trailing_comma) {
  stream << "    \"" << name << "\": {\n";
  stream << "      \"unit\": \"ms\",\n";
  stream << "      \"samples\": " << timing.samples << ",\n";
  stream << "      \"total\": " << timing.total_ms << ",\n";
  stream << "      \"average\": " << timing.average_ms << ",\n";
  stream << "      \"minimum\": " << timing.minimum_ms << ",\n";
  stream << "      \"median\": " << timing.median_ms << ",\n";
  stream << "      \"p95\": " << timing.p95_ms << ",\n";
  stream << "      \"p99\": " << timing.p99_ms << ",\n";
  stream << "      \"maximum\": " << timing.maximum_ms << "\n";
  stream << "    }" << (trailing_comma ? ",\n" : "\n");
}

void write_geometry_json(std::ostream &stream, std::string_view name,
                         const GeometryCounts &counts,
                         bool trailing_comma) {
  stream << "      \"" << name << "\": {\n";
  stream << "        \"meshes\": " << counts.meshes << ",\n";
  stream << "        \"authoredMeshes\": " << counts.authored_meshes
         << ",\n";
  stream << "        \"authoredVertices\": " << counts.authored_vertices
         << ",\n";
  stream << "        \"polygonFaces\": " << counts.polygon_faces
         << ",\n";
  stream << "        \"faceCorners\": " << counts.face_corners << ",\n";
  stream << "        \"compiledVertices\": " << counts.compiled_vertices
         << ",\n";
  stream << "        \"triangles\": " << counts.triangles << ",\n";
  stream << "        \"indices\": " << counts.indices << "\n";
  stream << "      }" << (trailing_comma ? ",\n" : "\n");
}

std::string report_json(const BenchmarkReport &report) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(6);
  stream << "{\n";
  stream << "  \"schema\": \"qdr-fixture-performance-baseline-v1\",\n";
  stream << "  \"percentileMethod\": \"nearest-rank\",\n";
  stream << "  \"fixture\": {\n";
  stream << "    \"path\": \""
         << json_escape(report.options.qdr_path.string()) << "\",\n";
  stream << "    \"bytes\": " << report.fixture_bytes << ",\n";
  stream << "    \"fnv1a64\": \""
         << checksum_hex(report.fixture_checksum) << "\",\n";
  stream << "    \"inputUnchanged\": "
         << (report.input_unchanged ? "true" : "false") << ",\n";
  stream << "    \"counts\": {\n";
  stream << "      \"entities\": " << report.fixture.entities << ",\n";
  stream << "      \"objects\": " << report.fixture.objects << ",\n";
  stream << "      \"meshEntities\": " << report.fixture.mesh_entities
         << ",\n";
  stream << "      \"lightEntities\": " << report.fixture.light_entities
         << ",\n";
  stream << "      \"emptyEntities\": " << report.fixture.empty_entities
         << ",\n";
  stream << "      \"lockedEntities\": " << report.fixture.locked_entities
         << ",\n";
  stream << "      \"unselectableEntities\": "
         << report.fixture.unselectable_entities << ",\n";
  stream << "      \"initiallySelectedEntities\": "
         << report.fixture.initially_selected_entities << ",\n";
  stream << "      \"selectAllEntities\": "
         << report.fixture.select_all_entities << ",\n";
  stream << "      \"materialAssets\": " << report.fixture.material_assets
         << ",\n";
  stream << "      \"textureAssets\": " << report.fixture.texture_assets
         << ",\n";
  stream << "      \"referencedMeshAssets\": "
         << report.fixture.referenced_mesh_assets << ",\n";
  stream << "      \"orphanMeshAssets\": "
         << report.fixture.orphan_mesh_assets << "\n";
  stream << "    },\n";
  stream << "    \"geometry\": {\n";
  write_geometry_json(stream, "storedAssets", report.fixture.stored_geometry,
                      true);
  write_geometry_json(stream, "referencedAssets",
                      report.fixture.referenced_geometry, true);
  write_geometry_json(stream, "selectableAssets",
                      report.fixture.selectable_geometry, false);
  stream << "    }\n";
  stream << "  },\n";
  stream << "  \"options\": {\n";
  stream << "    \"loadSamples\": " << report.options.load_samples << ",\n";
  stream << "    \"selectWarmup\": " << report.options.select_warmup
         << ",\n";
  stream << "    \"selectSamples\": " << report.options.select_samples
         << ",\n";
  stream << "    \"navigationWarmup\": "
         << report.options.navigation_warmup << ",\n";
  stream << "    \"navigationFrames\": "
         << report.options.navigation_frames << ",\n";
  stream << "    \"snapshotFrames\": " << report.options.snapshot_frames
         << ",\n";
  stream << "    \"overlayFrames\": " << report.options.overlay_frames
         << ",\n";
  stream << "    \"transformWarmup\": "
         << report.options.transform_warmup << ",\n";
  stream << "    \"transformSamples\": "
         << report.options.transform_samples << "\n";
  stream << "  },\n";
  stream << "  \"timings\": {\n";
  write_timing_json(stream, "loadProject", report.load, true);
  write_timing_json(stream, "openSession", report.session_open, true);
  write_timing_json(stream, "selectAll", report.select_all, true);
  write_timing_json(stream, "navigationFrame", report.navigation, true);
  write_timing_json(stream, "renderSnapshotReuseFrame",
                    report.snapshot_reuse, true);
  write_timing_json(stream, "overlayCameraLayerBuild",
                    report.overlay_build, true);
  write_timing_json(stream, "transformPreviewUpdate",
                    report.transform_update, true);
  write_timing_json(stream, "transformSceneSnapshotRefresh",
                    report.transform_scene_snapshot_refresh, true);
  write_timing_json(stream, "transformModelingOverlayRefresh",
                    report.transform_modeling_overlay_refresh, true);
  write_timing_json(stream, "transformOverlaySnapshotRefresh",
                    report.transform_overlay_snapshot_refresh, true);
  write_timing_json(stream, "transformOverlayFrameBuild",
                    report.transform_overlay_frame_build, true);
  write_timing_json(stream, "transformOverlayCameraBuild",
                    report.transform_overlay_camera_build, true);
  write_timing_json(stream, "transformFrameCpu",
                    report.transform_frame_cpu, false);
  stream << "  },\n";
  stream << "  \"observables\": {\n";
  stream << "    \"selectAllChecksum\": \""
         << checksum_hex(report.select_all_checksum) << "\",\n";
  stream << "    \"sessionInitialSelectionCount\": "
         << report.session_initial_selection_count << ",\n";
  stream << "    \"selectionBenchmarkBaselineCount\": "
         << report.selection_benchmark_baseline_count << ",\n";
  stream << "    \"selectionRestoredAfterSamples\": "
         << (report.selection_restored_after_samples ? "true" : "false")
         << ",\n";
  stream << "    \"navigationChangedFrames\": "
         << report.navigation_observables.changed_frames << ",\n";
  stream << "    \"navigationConsumedFrames\": "
         << report.navigation_observables.consumed_frames << ",\n";
  stream << "    \"navigationChecksum\": \""
         << checksum_hex(report.navigation_observables.checksum) << "\",\n";
  stream << "    \"navigationReplayVerified\": "
         << (report.navigation_replay_verified ? "true" : "false")
         << ",\n";
  stream << "    \"snapshotReusedFrames\": "
         << report.snapshot_observables.reused_frames << ",\n";
  stream << "    \"snapshotZeroDirtyFrames\": "
         << report.snapshot_observables.zero_dirty_frames << ",\n";
  stream << "    \"snapshotChecksum\": \""
         << checksum_hex(report.snapshot_observables.checksum) << "\",\n";
  stream << "    \"overlayFrames\": "
         << report.overlay_observables.frames << ",\n";
  stream << "    \"overlayActiveCameraLayers\": "
         << report.overlay_observables.active_camera_layers << ",\n";
  stream << "    \"overlayTriangles\": "
         << report.overlay_observables.triangles << ",\n";
  stream << "    \"overlayLines\": "
         << report.overlay_observables.lines << ",\n";
  stream << "    \"overlayPoints\": "
         << report.overlay_observables.points << ",\n";
  stream << "    \"overlayIcons\": "
         << report.overlay_observables.icons << ",\n";
  stream << "    \"overlaySelectedMeshBounds\": "
         << report.overlay_observables.selected_mesh_bounds << ",\n";
  stream << "    \"overlaySelectedLights\": "
         << report.overlay_observables.selected_lights << ",\n";
  stream << "    \"overlayFrameSignature\": \""
         << checksum_hex(report.overlay_observables.frame_signature)
         << "\",\n";
  stream << "    \"overlayChecksum\": \""
         << checksum_hex(report.overlay_observables.checksum) << "\",\n";
  stream << "    \"transformChangedUpdates\": "
         << report.transform_observables.changed_updates << ",\n";
  stream << "    \"transformChangedEntities\": "
         << report.transform_observables.changed_transforms << ",\n";
  stream << "    \"transformSceneSnapshotRefreshes\": "
         << report.transform_observables.scene_snapshot_refreshes << ",\n";
  stream << "    \"transformOverlayLocalBoundsBuilds\": "
         << report.transform_observables.overlay_local_bounds_builds << ",\n";
  stream << "    \"transformOverlayLocalBoundsReuses\": "
         << report.transform_observables.overlay_local_bounds_reuses << ",\n";
  stream << "    \"transformOverlaySnapshotRefreshes\": "
         << report.transform_observables.overlay_transform_refreshes
         << ",\n";
  stream << "    \"transformOverlayWorldBoundsBuilds\": "
         << report.transform_observables.overlay_world_bounds_builds
         << ",\n";
  stream << "    \"transformOverlayWorldBoundsTranslationReuses\": "
         << report.transform_observables.overlay_world_bounds_translation_reuses
         << ",\n";
  stream << "    \"transformOverlayLines\": "
         << report.transform_observables.overlay_lines << ",\n";
  stream << "    \"transformFrameChecksum\": \""
         << checksum_hex(report.transform_observables.frame_checksum)
         << "\",\n";
  stream << "    \"transformPreviewChecksum\": \""
         << checksum_hex(report.transform_observables.preview_checksum)
         << "\",\n";
  stream << "    \"transformBeforeChecksum\": \""
         << checksum_hex(
                report.transform_observables.before_transform_checksum)
         << "\",\n";
  stream << "    \"transformDuringPreviewChecksum\": \""
         << checksum_hex(
                report.transform_observables.preview_transform_checksum)
         << "\",\n";
  stream << "    \"transformRestoredChecksum\": \""
         << checksum_hex(
                report.transform_observables.restored_transform_checksum)
         << "\",\n";
  stream << "    \"transformRestoredExactly\": "
         << (report.transform_observables.restored_exactly ? "true"
                                                          : "false")
         << "\n";
  stream << "  }\n";
  stream << "}\n";
  return stream.str();
}

BenchmarkReport run_benchmark(const BenchmarkOptions &options) {
  require(std::filesystem::is_regular_file(options.qdr_path),
          "Input QDR is not a regular file: " + options.qdr_path.string());

  BenchmarkReport report;
  report.options = options;
  report.fixture_bytes = std::filesystem::file_size(options.qdr_path);

  std::vector<double> load_samples;
  load_samples.reserve(static_cast<std::size_t>(options.load_samples));
  quader::QdrProjectReadResult loaded;
  for (int sample = 0; sample < options.load_samples; ++sample) {
    const Clock::time_point begin = Clock::now();
    quader::QdrProjectReadResult current =
        quader::QdrDocumentReader::read_project_document(options.qdr_path);
    const Clock::time_point end = Clock::now();
    load_samples.push_back(elapsed_milliseconds(begin, end));
    require(current.ok, "QDR load failed: " + current.message);
    if (sample + 1 == options.load_samples) {
      loaded = std::move(current);
    }
  }
  report.load = summarize_timings(load_samples);
  report.fixture_checksum = file_checksum(options.qdr_path);
  report.fixture = analyze_fixture(loaded.document);
  require(report.fixture.select_all_entities > 0U,
          "Fixture has no selectable entities.");
  require(report.fixture.initial_selection !=
              report.fixture.selectable_entities,
          "Fixture already has every eligible entity selected.");

  quader::SceneSessionService sessions;
  std::unique_ptr<quader::SceneSession> session;
  const Clock::time_point open_begin = Clock::now();
  session = sessions.open_session(std::move(loaded.document));
  const Clock::time_point open_end = Clock::now();
  require(session != nullptr, "Project session creation failed.");
  report.session_open = summarize_timings(
      {elapsed_milliseconds(open_begin, open_end)});

  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(*session));
  report.session_initial_selection_count =
      session->scene().selected_entities().size();
  const quader::QuaderApiVoidResult object_mode =
      editor.selection().set_mode(quader::QuaderSelectionMode::Object);
  require_api_result(object_mode, "Set object selection mode");
  const SelectionBaseline selection_baseline{
      .selected = sorted_entities(session->scene().selected_entities()),
      .active = session->scene().primary_selection(),
  };
  report.selection_benchmark_baseline_count =
      selection_baseline.selected.size();

  report.select_all_checksum = kFnvOffset;
  for (int warmup = 0; warmup < options.select_warmup; ++warmup) {
    run_select_all_cycle(editor, session->scene(), report.fixture,
                         selection_baseline, nullptr,
                         report.select_all_checksum);
  }
  std::vector<double> select_samples;
  select_samples.reserve(static_cast<std::size_t>(options.select_samples));
  for (int sample = 0; sample < options.select_samples; ++sample) {
    run_select_all_cycle(editor, session->scene(), report.fixture,
                         selection_baseline, &select_samples,
                         report.select_all_checksum);
  }
  report.select_all = summarize_timings(select_samples);
  report.selection_restored_after_samples =
      sorted_entities(session->scene().selected_entities()) ==
          selection_baseline.selected &&
      session->scene().primary_selection() == selection_baseline.active;
  require(report.selection_restored_after_samples,
          "Selection benchmark did not restore initial state.");

  if (options.navigation_warmup > 0) {
    (void)run_navigation(options.navigation_warmup, false);
  }
  report.navigation_observables =
      run_navigation(options.navigation_frames, true);
  const NavigationRun navigation_replay =
      run_navigation(options.navigation_frames, false);
  report.navigation_replay_verified =
      navigation_replay.checksum == report.navigation_observables.checksum &&
      navigation_replay.changed_frames ==
          report.navigation_observables.changed_frames &&
      navigation_replay.consumed_frames ==
          report.navigation_observables.consumed_frames;
  require(report.navigation_replay_verified,
          "Navigation checksum was not stable on deterministic replay.");
  report.navigation =
      summarize_timings(report.navigation_observables.frame_ms);

  report.overlay_observables = run_overlay_workload(
      editor, *session, report.fixture, options.overlay_frames);
  report.overlay_build =
      summarize_timings(report.overlay_observables.frame_ms);

  report.snapshot_observables = run_snapshot_reuse(
      session->scene(), session->assets(), options.snapshot_frames);
  report.snapshot_reuse =
      summarize_timings(report.snapshot_observables.frame_ms);

  report.transform_observables = run_transform_preview(
      editor, session->modeling(), session->scene(), session->assets(),
      report.fixture, options);
  report.transform_update =
      summarize_timings(report.transform_observables.update_ms);
  report.transform_scene_snapshot_refresh = summarize_timings(
      report.transform_observables.scene_snapshot_refresh_ms);
  report.transform_modeling_overlay_refresh = summarize_timings(
      report.transform_observables.modeling_overlay_refresh_ms);
  report.transform_overlay_snapshot_refresh = summarize_timings(
      report.transform_observables.overlay_snapshot_refresh_ms);
  report.transform_overlay_frame_build = summarize_timings(
      report.transform_observables.overlay_frame_build_ms);
  report.transform_overlay_camera_build = summarize_timings(
      report.transform_observables.overlay_camera_build_ms);
  report.transform_frame_cpu =
      summarize_timings(report.transform_observables.frame_cpu_ms);

  report.input_unchanged =
      std::filesystem::file_size(options.qdr_path) == report.fixture_bytes &&
      file_checksum(options.qdr_path) == report.fixture_checksum;
  require(report.input_unchanged,
          "Input QDR bytes changed while the benchmark was running.");
  return report;
}

void write_output_file(const std::filesystem::path &path,
                       std::string_view json) {
  if (path.empty()) {
    return;
  }
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    fail("Could not write benchmark output: " + path.string());
  }
  stream.write(json.data(), static_cast<std::streamsize>(json.size()));
  if (!stream) {
    fail("Could not finish benchmark output: " + path.string());
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    const BenchmarkOptions options = parse_args(argc, argv);
    const BenchmarkReport report = run_benchmark(options);
    const std::string json = report_json(report);
    write_output_file(options.output_path, json);
    std::cout << json;
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "qdr_fixture_performance_baseline failed: "
              << exception.what() << '\n';
    return 1;
  }
}
