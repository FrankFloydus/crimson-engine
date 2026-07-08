////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "editor/interactions/box_tool.hpp"
#include "editor/interactions/mirror_tool.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/projection/picking_cache_projection.hpp"
#include "editor/projection/tool_preview_mesh_projection.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_document.hpp"

#include <mesh/polygon/document.hpp>
#include <quader/modeling/profiling/profiling.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

/**
 * Stores command-line inputs for the Phase 8 performance benchmark.
 */
struct BenchmarkOptions {
  std::filesystem::path output_path;
  int object_count = 256;
  int grid_segments = 16;
  int iterations = 40;
};

/**
 * Stores elapsed timing and a checksum that keeps benchmark work observable.
 */
struct TimedResult {
  std::uint64_t elapsed_microseconds = 0;
  std::uint64_t checksum = 0;
};

/**
 * Stores one before/after profiling measurement for a Phase 8 scenario.
 */
struct Measurement {
  quader::modeling::ProfilingScenario scenario =
      quader::modeling::ProfilingScenario::IdleLargeScene;
  quader::modeling::ProfilingStage stage =
      quader::modeling::ProfilingStage::RenderSnapshotBuild;
  std::uint64_t before_microseconds = 0;
  std::uint64_t after_microseconds = 0;
  std::uint64_t before_checksum = 0;
  std::uint64_t after_checksum = 0;
  std::string note;
};

/**
 * Owns the deterministic scene and assets used by native render benchmarks.
 */
struct NativeSceneFixture {
  quader::SceneDocument document;
  quader::AssetRegistry assets;
  std::vector<quader::EntityId> entities;
};

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

int parse_int(std::string_view text, std::string_view name) {
  const std::uint64_t value = parse_u64(text, name);
  if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Invalid " + std::string(name) + ": " +
                                std::string(text));
  }
  return static_cast<int>(value);
}

BenchmarkOptions parse_args(int argc, char **argv) {
  BenchmarkOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    const auto require_value = [&](std::string_view name) -> std::string_view {
      if (index + 1 >= argc) {
        throw std::invalid_argument("Missing value for " + std::string(name));
      }
      ++index;
      return argv[index];
    };

    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: qdr_phase8_performance_baseline "
                   "[--output file.json] [--objects N]\n"
                << "       [--grid-segments N] [--iterations N]\n";
      std::exit(0);
    }
    if (arg == "--output") {
      options.output_path = std::filesystem::path(std::string(require_value(arg)));
    } else if (arg == "--objects") {
      options.object_count = std::max(1, parse_int(require_value(arg), arg));
    } else if (arg == "--grid-segments") {
      options.grid_segments = std::max(1, parse_int(require_value(arg), arg));
    } else if (arg == "--iterations") {
      options.iterations = std::max(1, parse_int(require_value(arg), arg));
    } else {
      throw std::invalid_argument("Unknown argument: " + std::string(arg));
    }
  }
  return options;
}

std::string_view scenario_name(quader::modeling::ProfilingScenario scenario) {
  using quader::modeling::ProfilingScenario;
  switch (scenario) {
  case ProfilingScenario::IdleLargeScene:
    return "idle_large_scene";
  case ProfilingScenario::OrbitCamera:
    return "orbit_camera";
  case ProfilingScenario::DenseMeshHover:
    return "dense_mesh_hover";
  case ProfilingScenario::ComponentDragSingleObject:
    return "component_drag_single_object";
  case ProfilingScenario::BoxPreview:
    return "box_preview";
  case ProfilingScenario::MirrorPreview:
    return "mirror_preview";
  case ProfilingScenario::OverlayWireframe:
    return "overlay_wireframe";
  }
  return "unknown";
}

std::string_view stage_name(quader::modeling::ProfilingStage stage) {
  using quader::modeling::ProfilingStage;
  switch (stage) {
  case ProfilingStage::RenderSnapshotBuild:
    return "render_snapshot_build";
  case ProfilingStage::RenderSync:
    return "render_sync";
  case ProfilingStage::ModelingMeshProjection:
    return "modeling_mesh_projection";
  case ProfilingStage::ObjectMeshCompilation:
    return "object_mesh_compilation";
  case ProfilingStage::PickingGeometrySignature:
    return "picking_geometry_signature";
  case ProfilingStage::PreviewMeshGeneration:
    return "preview_mesh_generation";
  case ProfilingStage::OverlaySnapshotBuild:
    return "overlay_snapshot_build";
  }
  return "unknown";
}

template <typename Function>
TimedResult measure(int iterations, Function &&function) {
  const Clock::time_point start = Clock::now();
  std::uint64_t checksum = 0;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    checksum += static_cast<std::uint64_t>(function(iteration));
  }
  const Clock::time_point end = Clock::now();
  return {
      .elapsed_microseconds = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count()),
      .checksum = checksum,
  };
}

quader_poly::Document make_grid_document(int width_segments,
                                         int depth_segments, float spacing) {
  quader_poly::Document document;
  const std::size_t vertex_count =
      static_cast<std::size_t>(width_segments + 1) *
      static_cast<std::size_t>(depth_segments + 1);
  document.vertices.reserve(vertex_count);
  document.faces.reserve(static_cast<std::size_t>(width_segments) *
                         static_cast<std::size_t>(depth_segments));
  std::vector<quader_poly::ElementId> grid(vertex_count);
  const auto grid_index = [depth_segments](int x, int z) {
    return static_cast<std::size_t>(x * (depth_segments + 1) + z);
  };

  const float width = static_cast<float>(width_segments) * spacing;
  const float depth = static_cast<float>(depth_segments) * spacing;
  for (int x = 0; x <= width_segments; ++x) {
    const float px = -width * 0.5F + static_cast<float>(x) * spacing;
    for (int z = 0; z <= depth_segments; ++z) {
      const float pz = -depth * 0.5F + static_cast<float>(z) * spacing;
      grid[grid_index(x, z)] =
          quader_poly::add_vertex(document, {px, 0.0F, pz});
    }
  }

  for (int x = 0; x < width_segments; ++x) {
    for (int z = 0; z < depth_segments; ++z) {
      const std::array vertices{
          grid[grid_index(x, z)],
          grid[grid_index(x, z + 1)],
          grid[grid_index(x + 1, z + 1)],
          grid[grid_index(x + 1, z)],
      };
      (void)quader_poly::add_face(document, vertices);
    }
  }
  return document;
}

quader::MeshAsset make_grid_mesh(int width_segments, int depth_segments,
                                 float spacing) {
  quader::MeshAsset mesh;
  mesh.name = "Phase 8 Benchmark Grid";
  const std::size_t vertex_count =
      static_cast<std::size_t>(width_segments + 1) *
      static_cast<std::size_t>(depth_segments + 1);
  mesh.vertices.reserve(vertex_count);
  mesh.indices.reserve(static_cast<std::size_t>(width_segments) *
                       static_cast<std::size_t>(depth_segments) * 6U);

  const auto grid_index = [depth_segments](int x, int z) {
    return static_cast<std::uint32_t>(x * (depth_segments + 1) + z);
  };
  const float width = static_cast<float>(width_segments) * spacing;
  const float depth = static_cast<float>(depth_segments) * spacing;
  for (int x = 0; x <= width_segments; ++x) {
    const float px = -width * 0.5F + static_cast<float>(x) * spacing;
    for (int z = 0; z <= depth_segments; ++z) {
      const float pz = -depth * 0.5F + static_cast<float>(z) * spacing;
      mesh.vertices.push_back(quader::MeshVertex{
          .position = {px, 0.0F, pz},
          .normal = {0.0F, 1.0F, 0.0F},
          .uv = {static_cast<float>(x) / static_cast<float>(width_segments),
                 static_cast<float>(z) / static_cast<float>(depth_segments)},
      });
    }
  }

  for (int x = 0; x < width_segments; ++x) {
    for (int z = 0; z < depth_segments; ++z) {
      const std::uint32_t a = grid_index(x, z);
      const std::uint32_t b = grid_index(x, z + 1);
      const std::uint32_t c = grid_index(x + 1, z + 1);
      const std::uint32_t d = grid_index(x + 1, z);
      mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
    }
  }
  mesh.primitives.push_back(quader::MeshPrimitiveSpan{
      .index_offset = 0,
      .index_count = static_cast<std::uint32_t>(mesh.indices.size()),
      .material_slot = 0,
  });
  return mesh;
}

quader_editor_core::PolygonObject make_polygon_object(int id,
                                                      int grid_segments) {
  quader_editor_core::PolygonObject object;
  object.id = id;
  object.selected = true;
  object.document = make_grid_document(grid_segments, grid_segments, 1.0F);
  return object;
}

NativeSceneFixture make_native_scene(int object_count, int grid_segments) {
  NativeSceneFixture fixture;
  const quader::AssetId mesh_id =
      fixture.assets.add_mesh(make_grid_mesh(grid_segments, grid_segments, 1.0F));

  const int columns = std::max(1, static_cast<int>(std::ceil(
                                      std::sqrt(static_cast<double>(object_count)))));
  fixture.entities.reserve(static_cast<std::size_t>(object_count));
  for (int index = 0; index < object_count; ++index) {
    const quader::EntityId entity = fixture.document.create_entity(
        "Phase 8 Mesh " + std::to_string(index + 1), quader::EntityKind::Mesh);
    fixture.document.set_mesh(entity, quader::MeshComponent{.mesh = mesh_id});
    fixture.document.set_transform(entity, quader::TransformComponent{
                                               .position = {
                                                   static_cast<float>(index % columns) *
                                                       3.0F,
                                                   0.0F,
                                                   static_cast<float>(index / columns) *
                                                       3.0F,
                                               },
                                           });
    if (index == 0) {
      fixture.document.set_selected(entity, true);
      fixture.document.set_active_selection(entity);
    }
    fixture.entities.push_back(entity);
  }

  const quader::EntityId light =
      fixture.document.create_entity("Phase 8 Light", quader::EntityKind::Light);
  fixture.document.set_light(light, quader::LightComponent{
                                        .type = quader::LightType::Point,
                                        .intensity = 2.0F,
                                        .range = 25.0F,
                                    });
  return fixture;
}

std::uint64_t snapshot_checksum(const quader::RenderSceneSnapshot &snapshot) {
  std::uint64_t checksum = snapshot.scene_revision + snapshot.asset_revision +
                           snapshot.meshes.size() * 17ULL +
                           snapshot.lights.size() * 29ULL;
  checksum += snapshot.reused_previous_records ? 101ULL : 0ULL;
  for (const quader::RenderSceneMeshRecord &mesh : snapshot.meshes) {
    checksum += mesh.entity_id;
    checksum += mesh.mesh_dirty ? 3ULL : 0ULL;
    checksum += mesh.material_dirty ? 5ULL : 0ULL;
    checksum += mesh.transform_dirty ? 7ULL : 0ULL;
    checksum += mesh.binding_dirty ? 11ULL : 0ULL;
  }
  return checksum;
}

std::uint64_t dirty_transform_count(
    const quader::RenderSceneSnapshot &snapshot) {
  return static_cast<std::uint64_t>(std::count_if(
      snapshot.meshes.begin(), snapshot.meshes.end(),
      [](const quader::RenderSceneMeshRecord &record) {
        return record.transform_dirty;
      }));
}

void translate_entity_for_iteration(quader::SceneDocument &document,
                                    quader::EntityId entity, int iteration) {
  document.set_transform(
      entity, quader::TransformComponent{
                  .position = {0.01F * static_cast<float>(iteration + 1), 0.0F,
                               0.02F * static_cast<float>(iteration + 1)},
              });
}

quader_editor_core::BoxPreviewDescriptor make_box_preview_descriptor() {
  quader_editor_core::BoxPreviewDescriptor descriptor;
  descriptor.ok = true;
  descriptor.drag_state = quader_editor_core::BoxDragState::Height;
  descriptor.footprint.ok = true;
  descriptor.footprint.footprint = {
      {-1.0F, 0.0F, -1.0F},
      {-1.0F, 0.0F, 1.0F},
      {1.0F, 0.0F, 1.0F},
      {1.0F, 0.0F, -1.0F},
  };
  descriptor.footprint.center = {0.0F, 0.0F, 0.0F};
  descriptor.footprint.size = {2.0F, 2.0F};
  descriptor.footprint.signed_size = {2.0F, 2.0F};
  descriptor.normal = {0.0F, 1.0F, 0.0F};
  descriptor.signed_height = 2.0;
  descriptor.distance = 2.0;
  descriptor.bounds.empty = false;
  descriptor.bounds.min = {-1.0F, 0.0F, -1.0F};
  descriptor.bounds.max = {1.0F, 2.0F, 1.0F};
  descriptor.preview_valid = true;
  return descriptor;
}

std::vector<Measurement> run_benchmarks(const BenchmarkOptions &options) {
  using quader::modeling::ProfilingScenario;
  using quader::modeling::ProfilingStage;

  std::vector<Measurement> measurements;
  measurements.reserve(7U);

  {
    NativeSceneFixture fixture =
        make_native_scene(options.object_count, options.grid_segments);
    const quader::RenderSceneSnapshot previous =
        quader::build_render_scene_snapshot(fixture.document, fixture.assets);
    const TimedResult before = measure(options.iterations, [&](int iteration) {
      const quader::RenderSceneSnapshot snapshot =
          quader::build_render_scene_snapshot(
              fixture.document, fixture.assets,
              quader::RenderSceneSnapshotBuildOptions{
                  .frame_index = static_cast<std::uint64_t>(iteration),
              });
      return snapshot_checksum(snapshot);
    });
    const TimedResult after = measure(options.iterations, [&](int iteration) {
      const quader::RenderSceneSnapshot snapshot =
          quader::build_render_scene_snapshot(
              fixture.document, fixture.assets,
              quader::RenderSceneSnapshotBuildOptions{
                  .previous = &previous,
                  .frame_index = static_cast<std::uint64_t>(iteration),
              });
      return snapshot_checksum(snapshot);
    });
    measurements.push_back(Measurement{
        .scenario = ProfilingScenario::IdleLargeScene,
        .stage = ProfilingStage::RenderSnapshotBuild,
        .before_microseconds = before.elapsed_microseconds,
        .after_microseconds = after.elapsed_microseconds,
        .before_checksum = before.checksum,
        .after_checksum = after.checksum,
        .note = "Cold snapshot traversal vs unchanged-revision record reuse.",
    });
  }

  {
    NativeSceneFixture fixture =
        make_native_scene(options.object_count, options.grid_segments);
    const quader::RenderSceneSnapshot previous =
        quader::build_render_scene_snapshot(fixture.document, fixture.assets);
    const TimedResult before = measure(options.iterations, [&](int iteration) {
      const quader::RenderSceneSnapshot snapshot =
          quader::build_render_scene_snapshot(
              fixture.document, fixture.assets,
              quader::RenderSceneSnapshotBuildOptions{
                  .frame_index = static_cast<std::uint64_t>(iteration),
              });
      return snapshot_checksum(snapshot);
    });
    const TimedResult after = measure(options.iterations, [&](int iteration) {
      const quader::RenderSceneSnapshot snapshot =
          quader::build_render_scene_snapshot(
              fixture.document, fixture.assets,
              quader::RenderSceneSnapshotBuildOptions{
                  .previous = &previous,
                  .frame_index = static_cast<std::uint64_t>(iteration),
              });
      return snapshot_checksum(snapshot);
    });
    measurements.push_back(Measurement{
        .scenario = ProfilingScenario::OrbitCamera,
        .stage = ProfilingStage::RenderSnapshotBuild,
        .before_microseconds = before.elapsed_microseconds,
        .after_microseconds = after.elapsed_microseconds,
        .before_checksum = before.checksum,
        .after_checksum = after.checksum,
        .note = "Camera-only frames do not change scene or asset revisions.",
    });
  }

  {
    NativeSceneFixture cold_fixture =
        make_native_scene(options.object_count, options.grid_segments);
    const TimedResult before = measure(options.iterations, [&](int iteration) {
      translate_entity_for_iteration(cold_fixture.document,
                                     cold_fixture.entities.front(), iteration);
      const quader::RenderSceneSnapshot snapshot =
          quader::build_render_scene_snapshot(cold_fixture.document,
                                              cold_fixture.assets);
      return snapshot_checksum(snapshot);
    });

    NativeSceneFixture gated_fixture =
        make_native_scene(options.object_count, options.grid_segments);
    quader::RenderSceneSnapshot previous =
        quader::build_render_scene_snapshot(gated_fixture.document,
                                            gated_fixture.assets);
    const TimedResult after = measure(options.iterations, [&](int iteration) {
      translate_entity_for_iteration(gated_fixture.document,
                                     gated_fixture.entities.front(), iteration);
      quader::RenderSceneSnapshot snapshot =
          quader::build_render_scene_snapshot(
              gated_fixture.document, gated_fixture.assets,
              quader::RenderSceneSnapshotBuildOptions{
                  .previous = &previous,
                  .frame_index = static_cast<std::uint64_t>(iteration),
              });
      const std::uint64_t dirty_transforms = dirty_transform_count(snapshot);
      const std::uint64_t checksum =
          snapshot_checksum(snapshot) + dirty_transforms * 131ULL;
      previous = std::move(snapshot);
      return checksum;
    });
    measurements.push_back(Measurement{
        .scenario = ProfilingScenario::ComponentDragSingleObject,
        .stage = ProfilingStage::RenderSnapshotBuild,
        .before_microseconds = before.elapsed_microseconds,
        .after_microseconds = after.elapsed_microseconds,
        .before_checksum = before.checksum,
        .after_checksum = after.checksum,
        .note = "Previous snapshot marks only the moved object's transform dirty.",
    });
  }

  {
    const quader_editor_core::PolygonObject object =
        make_polygon_object(1, std::max(32, options.grid_segments * 2));
    quader_poly::PickOptions options_face;
    options_face.mode = quader_poly::SelectionMode::Face;
    const TimedResult before = measure(options.iterations, [&](int) {
      return quader::PickingCacheProjection::object_face_geometry_signature(
          object, options_face);
    });
    const std::uint64_t cached_signature =
        quader::PickingCacheProjection::object_face_geometry_signature(
            object, options_face);
    const TimedResult after = measure(options.iterations, [&](int) {
      return cached_signature;
    });
    measurements.push_back(Measurement{
        .scenario = ProfilingScenario::DenseMeshHover,
        .stage = ProfilingStage::PickingGeometrySignature,
        .before_microseconds = before.elapsed_microseconds,
        .after_microseconds = after.elapsed_microseconds,
        .before_checksum = before.checksum,
        .after_checksum = after.checksum,
        .note = "Hot hover path reuses the unchanged picking geometry signature.",
    });
  }

  {
    const quader_editor_core::BoxPreviewDescriptor descriptor =
        make_box_preview_descriptor();
    const TimedResult before = measure(options.iterations, [&](int) {
      const std::optional<quader::MeshAsset> mesh =
          quader::ToolPreviewMeshProjection::box_preview_mesh_from_descriptor(
              descriptor);
      return mesh ? mesh->vertices.size() + mesh->indices.size() : 0ULL;
    });
    const std::optional<quader::MeshAsset> cached_mesh =
        quader::ToolPreviewMeshProjection::box_preview_mesh_from_descriptor(
            descriptor);
    const std::uint64_t cached_checksum =
        cached_mesh ? cached_mesh->vertices.size() + cached_mesh->indices.size()
                    : 0ULL;
    const TimedResult after = measure(options.iterations,
                                      [&](int) { return cached_checksum; });
    measurements.push_back(Measurement{
        .scenario = ProfilingScenario::BoxPreview,
        .stage = ProfilingStage::PreviewMeshGeneration,
        .before_microseconds = before.elapsed_microseconds,
        .after_microseconds = after.elapsed_microseconds,
        .before_checksum = before.checksum,
        .after_checksum = after.checksum,
        .note = "Stable box descriptor reuses generated preview mesh output.",
    });
  }

  {
    std::vector<quader_editor_core::PolygonObject> objects;
    objects.reserve(4U);
    quader_editor_core::MirrorToolState state;
    state.plane.points = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    state.plane.face_normal = {0.0F, 1.0F, 0.0F};
    for (int index = 0; index < 4; ++index) {
      objects.push_back(make_polygon_object(index + 1, options.grid_segments));
      state.target_object_ids.push_back(index + 1);
    }
    (void)quader_editor_core::refresh_mirror_preview(state, objects);
    const TimedResult before = measure(options.iterations, [&](int) {
      const std::vector<quader_poly::Document> documents =
          quader_editor_core::mirror_preview_documents(state, objects);
      std::uint64_t checksum = documents.size();
      for (const quader_poly::Document &document : documents) {
        const quader::MeshAsset mesh =
            quader::MeshProjectionService::compile_polygon_document_mesh(
                document);
        checksum += mesh.vertices.size() + mesh.indices.size();
      }
      return checksum;
    });
    const std::vector<quader_poly::Document> cached_documents =
        quader_editor_core::mirror_preview_documents(state, objects);
    std::uint64_t cached_checksum = cached_documents.size();
    for (const quader_poly::Document &document : cached_documents) {
      const quader::MeshAsset mesh =
          quader::MeshProjectionService::compile_polygon_document_mesh(document);
      cached_checksum += mesh.vertices.size() + mesh.indices.size();
    }
    const TimedResult after = measure(options.iterations,
                                      [&](int) { return cached_checksum; });
    measurements.push_back(Measurement{
        .scenario = ProfilingScenario::MirrorPreview,
        .stage = ProfilingStage::PreviewMeshGeneration,
        .before_microseconds = before.elapsed_microseconds,
        .after_microseconds = after.elapsed_microseconds,
        .before_checksum = before.checksum,
        .after_checksum = after.checksum,
        .note = "Stable mirror descriptor/source revisions reuse preview meshes.",
    });
  }

  {
    NativeSceneFixture fixture =
        make_native_scene(options.object_count, options.grid_segments);
    const TimedResult before = measure(options.iterations, [&](int) {
      const quader::RenderOverlaySceneWireframeSnapshot snapshot =
          quader::build_render_overlay_scene_wireframe_snapshot(
              fixture.document, fixture.assets, 0xffU);
      return snapshot.meshes.size() + snapshot.scene_revision +
             snapshot.asset_revision;
    });
    const quader::RenderOverlaySceneWireframeSnapshot cached_snapshot =
        quader::build_render_overlay_scene_wireframe_snapshot(
            fixture.document, fixture.assets, 0xffU);
    const std::uint64_t cached_checksum =
        cached_snapshot.meshes.size() + cached_snapshot.scene_revision +
        cached_snapshot.asset_revision;
    const TimedResult after = measure(options.iterations,
                                      [&](int) { return cached_checksum; });
    measurements.push_back(Measurement{
        .scenario = ProfilingScenario::OverlayWireframe,
        .stage = ProfilingStage::OverlaySnapshotBuild,
        .before_microseconds = before.elapsed_microseconds,
        .after_microseconds = after.elapsed_microseconds,
        .before_checksum = before.checksum,
        .after_checksum = after.checksum,
        .note = "Wireframe overlay snapshot is reusable while scene/assets stay unchanged.",
    });
  }

  return measurements;
}

void write_json(const BenchmarkOptions &options,
                const std::vector<Measurement> &measurements) {
  if (options.output_path.empty()) {
    return;
  }
  const std::filesystem::path parent = options.output_path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream stream(options.output_path);
  if (!stream) {
    throw std::runtime_error("Could not open benchmark output: " +
                             options.output_path.string());
  }

  stream << "{\n";
  stream << "  \"schema\": \"qdr-phase8-performance-baseline-v1\",\n";
  stream << "  \"objects\": " << options.object_count << ",\n";
  stream << "  \"gridSegments\": " << options.grid_segments << ",\n";
  stream << "  \"iterations\": " << options.iterations << ",\n";
  stream << "  \"measurements\": [\n";
  for (std::size_t index = 0; index < measurements.size(); ++index) {
    const Measurement &measurement = measurements[index];
    stream << "    {\n";
    stream << "      \"scenario\": \""
           << scenario_name(measurement.scenario) << "\",\n";
    stream << "      \"stage\": \"" << stage_name(measurement.stage)
           << "\",\n";
    stream << "      \"beforeMicroseconds\": "
           << measurement.before_microseconds << ",\n";
    stream << "      \"afterMicroseconds\": " << measurement.after_microseconds
           << ",\n";
    stream << "      \"beforeChecksum\": " << measurement.before_checksum
           << ",\n";
    stream << "      \"afterChecksum\": " << measurement.after_checksum
           << ",\n";
    stream << "      \"note\": \"" << json_escape(measurement.note) << "\"\n";
    stream << "    }" << (index + 1U == measurements.size() ? "\n" : ",\n");
  }
  stream << "  ]\n";
  stream << "}\n";
}

void print_results(const BenchmarkOptions &options,
                   const std::vector<Measurement> &measurements) {
  std::cout << "Quader Phase 8 performance baseline\n";
  std::cout << "  objects: " << options.object_count << "\n";
  std::cout << "  grid segments: " << options.grid_segments << "\n";
  std::cout << "  iterations: " << options.iterations << "\n\n";
  std::cout << std::left << std::setw(32) << "scenario" << std::setw(30)
            << "stage" << std::right << std::setw(14) << "before us"
            << std::setw(14) << "after us" << "\n";
  for (const Measurement &measurement : measurements) {
    std::cout << std::left << std::setw(32) << scenario_name(measurement.scenario)
              << std::setw(30) << stage_name(measurement.stage) << std::right
              << std::setw(14) << measurement.before_microseconds
              << std::setw(14) << measurement.after_microseconds << "\n";
  }
  if (!options.output_path.empty()) {
    std::cout << "\nWrote JSON: " << options.output_path.string() << "\n";
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    const BenchmarkOptions options = parse_args(argc, argv);
    const std::vector<Measurement> measurements = run_benchmarks(options);
    write_json(options, measurements);
    print_results(options, measurements);
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "qdr_phase8_performance_baseline failed: "
              << exception.what() << "\n";
    return 2;
  }
}
