////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/session/scene_session.hpp"
#include "math/glm_bridge.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr float kSourceHeight = 16.0F;
constexpr float kRayMinDistance = 0.0001F;
constexpr float kRayMaxDistance = 64.0F;
constexpr float kParityTolerance = 0.0001F;

/// Configures one deterministic Snap-to-Floor performance run.
struct BenchmarkOptions final {
  std::filesystem::path output_path;
  int surface_segments = 128;
  int vertex_grid = 128;
  int cold_samples = 5;
  int warmup_batches = 2;
  int warm_samples = 10;
  bool brute_force_parity = false;
  int parity_rays = 64;
};

/// Stores aggregate latency statistics for one benchmark phase.
struct TimingSummary final {
  std::size_t samples = 0u;
  double total_ms = 0.0;
  double average_ms = 0.0;
  double minimum_ms = 0.0;
  double median_ms = 0.0;
  double p95_ms = 0.0;
  double maximum_ms = 0.0;
};

/// Stores cumulative build-counter deltas observed across one phase.
struct BuildCounterDelta final {
  std::uint64_t snapshot_builds = 0u;
  std::uint64_t tlas_builds = 0u;
  std::uint64_t blas_builds = 0u;
};

/// Stores observable output from one dense downward-projection batch.
struct ProjectionBatch final {
  std::uint64_t checksum = kFnvOffset;
  std::uint64_t rays = 0u;
  std::uint64_t hits = 0u;
  std::uint64_t misses = 0u;
};

/// Reports optional indexed-versus-linear correctness evidence.
struct ParityReport final {
  bool enabled = false;
  std::uint64_t rays_checked = 0u;
  std::uint64_t mismatches = 0u;
  std::uint64_t indexed_checksum = kFnvOffset;
  std::uint64_t brute_force_checksum = kFnvOffset;
  std::string first_mismatch;
};

/// Owns the stable public-editor fixture used by every measured query.
struct BenchmarkFixture final {
  quader::SceneSession session;
  quader::QuaderEditorApi editor;
  quader::QuaderMeshHandle target;
  quader::QuaderMeshHandle source;
  quader::QuaderMeshAsset target_mesh;
  std::vector<quader::QVec3> ray_origins;
  quader::SceneSurfaceParticipantId target_participant;
  quader::SceneSurfaceParticipantId source_participant;

  BenchmarkFixture()
      : editor(quader::QuaderApiSessionInternalAccess::from_scene_session(
            session)) {}
};

/// Aggregates timings, counters, checksums, and validation outcomes.
struct BenchmarkReport final {
  BenchmarkOptions options;
  std::size_t target_triangles = 0u;
  std::size_t source_triangles = 0u;
  std::size_t rays_per_batch = 0u;
  std::size_t participant_count = 0u;
  std::size_t indexed_triangle_count = 0u;
  std::size_t unique_geometry_count = 0u;
  TimingSummary cold_build;
  BuildCounterDelta cold_build_delta;
  std::uint64_t cold_index_checksum = kFnvOffset;
  bool cold_rebuild_verified = false;
  TimingSummary warm_projection;
  BuildCounterDelta warm_build_delta;
  std::uint64_t warm_batch_checksum = kFnvOffset;
  std::uint64_t warm_aggregate_checksum = kFnvOffset;
  std::uint64_t warm_measured_rays = 0u;
  std::uint64_t warm_measured_hits = 0u;
  std::uint64_t warm_measured_misses = 0u;
  bool warm_index_reused = false;
  bool warm_scratch_reused = false;
  bool warm_checksum_stable = false;
  bool every_ray_hit = false;
  ParityReport parity;
  bool valid = false;
};

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string message) {
  if (!condition) {
    fail(std::move(message));
  }
}

std::uint64_t parse_u64(std::string_view text, std::string_view name) {
  try {
    std::size_t parsed = 0u;
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

int parse_count(std::string_view text, std::string_view name,
                bool allow_zero) {
  const std::uint64_t value = parse_u64(text, name);
  if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
      (!allow_zero && value == 0u)) {
    throw std::invalid_argument("Invalid " + std::string(name) + ": " +
                                std::string(text));
  }
  return static_cast<int>(value);
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
          << "Usage: qdr_snap_to_floor_performance [options]\n"
          << "  --surface-segments N   Terrain cells per axis (default 128)\n"
          << "  --vertex-grid N        Projected vertices per axis (default 128)\n"
          << "  --cold-samples N       Fresh index builds (default 5)\n"
          << "  --warmup-batches N     Untimed dense batches (default 2)\n"
          << "  --warm-samples N       Timed dense batches (default 10)\n"
          << "  --brute-force-parity   Audit sampled rays outside timings\n"
          << "  --parity-rays N        Oracle sample count (default 64)\n"
          << "  --output file.json     Also write the JSON report\n";
      std::exit(0);
    }
    if (argument == "--surface-segments") {
      options.surface_segments =
          parse_count(require_value(argument), argument, false);
    } else if (argument == "--vertex-grid") {
      options.vertex_grid =
          parse_count(require_value(argument), argument, false);
    } else if (argument == "--cold-samples") {
      options.cold_samples =
          parse_count(require_value(argument), argument, false);
    } else if (argument == "--warmup-batches") {
      options.warmup_batches =
          parse_count(require_value(argument), argument, false);
    } else if (argument == "--warm-samples") {
      options.warm_samples =
          parse_count(require_value(argument), argument, false);
    } else if (argument == "--brute-force-parity") {
      options.brute_force_parity = true;
    } else if (argument == "--parity-rays") {
      options.parity_rays =
          parse_count(require_value(argument), argument, false);
    } else if (argument == "--output") {
      options.output_path =
          std::filesystem::path(std::string(require_value(argument)));
    } else {
      throw std::invalid_argument("Unknown argument: " +
                                  std::string(argument));
    }
  }

  require(options.surface_segments >= 2 && options.surface_segments <= 1024,
          "--surface-segments must be in [2, 1024].");
  require(options.vertex_grid >= 2 &&
              options.vertex_grid <= options.surface_segments,
          "--vertex-grid must be in [2, surface-segments].");
  if (!options.output_path.empty()) {
    options.output_path =
        std::filesystem::absolute(options.output_path).lexically_normal();
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
  require(!samples.empty(), "A benchmark phase produced no timing samples.");
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

std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8u);
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

float terrain_height(std::size_t x, std::size_t z) {
  const std::uint64_t mixed =
      (static_cast<std::uint64_t>(x) * 17u +
       static_cast<std::uint64_t>(z) * 31u +
       ((static_cast<std::uint64_t>(x) + 3u) *
        (static_cast<std::uint64_t>(z) + 5u)) %
           37u) %
      41u;
  return static_cast<float>(static_cast<int>(mixed) - 20) * 0.0625F;
}

quader::QuaderPolygonDocument make_terrain_document(int segments) {
  quader_poly::Document document;
  const std::size_t side = static_cast<std::size_t>(segments) + 1u;
  std::vector<quader_poly::ElementId> vertices(side * side);
  const auto index = [side](std::size_t x, std::size_t z) {
    return x * side + z;
  };
  for (std::size_t x = 0u; x < side; ++x) {
    for (std::size_t z = 0u; z < side; ++z) {
      vertices[index(x, z)] = quader_poly::add_vertex(
          document, {static_cast<float>(x), terrain_height(x, z),
                     static_cast<float>(z)});
    }
  }
  for (std::size_t x = 0u; x + 1u < side; ++x) {
    for (std::size_t z = 0u; z + 1u < side; ++z) {
      const std::array face{
          vertices[index(x, z)], vertices[index(x + 1u, z)],
          vertices[index(x + 1u, z + 1u)],
          vertices[index(x, z + 1u)],
      };
      require(quader_poly::add_face(document, face) !=
                  quader_poly::kInvalidElementId,
              "Could not create a terrain face.");
    }
  }
  return quader::QuaderPolygonDocumentInternalAccess::from_backend(
      std::move(document));
}

quader::QuaderPolygonDocument
make_source_document(int segments, int vertex_grid,
                     std::vector<quader::QVec3> &ray_origins) {
  quader_poly::Document document;
  const std::size_t side = static_cast<std::size_t>(vertex_grid);
  const std::size_t segment_count = static_cast<std::size_t>(segments);
  std::vector<quader_poly::ElementId> vertices(side * side);
  ray_origins.clear();
  ray_origins.reserve(side * side);
  const auto index = [side](std::size_t x, std::size_t z) {
    return x * side + z;
  };
  for (std::size_t x = 0u; x < side; ++x) {
    const std::size_t cell_x = (x * segment_count) / side;
    for (std::size_t z = 0u; z < side; ++z) {
      const std::size_t cell_z = (z * segment_count) / side;
      const quader::QVec3 position{
          static_cast<float>(cell_x) + 0.3125F, kSourceHeight,
          static_cast<float>(cell_z) + 0.6875F};
      ray_origins.push_back(position);
      vertices[index(x, z)] = quader_poly::add_vertex(document, position);
    }
  }
  for (std::size_t x = 0u; x + 1u < side; ++x) {
    for (std::size_t z = 0u; z + 1u < side; ++z) {
      const std::array face{
          vertices[index(x, z)], vertices[index(x + 1u, z)],
          vertices[index(x + 1u, z + 1u)],
          vertices[index(x, z + 1u)],
      };
      require(quader_poly::add_face(document, face) !=
                  quader_poly::kInvalidElementId,
              "Could not create a source-grid face.");
    }
  }
  return quader::QuaderPolygonDocumentInternalAccess::from_backend(
      std::move(document));
}

void populate_fixture(BenchmarkFixture &fixture,
                      const BenchmarkOptions &options) {
  const quader::QuaderApiResult<quader::QuaderMeshHandle> target =
      fixture.editor.modeling().create_mesh_handle(
          make_terrain_document(options.surface_segments),
          {.name = "Snap Performance Terrain"});
  require(target.ok(), "Could not create target terrain: " +
                           target.error_message());
  fixture.target = target.value();

  const quader::QuaderApiResult<quader::QuaderMeshHandle> source =
      fixture.editor.modeling().create_mesh_handle(
          make_source_document(options.surface_segments, options.vertex_grid,
                               fixture.ray_origins),
          {.name = "Snap Performance Dense Vertices"});
  require(source.ok(), "Could not create source mesh: " +
                           source.error_message());
  fixture.source = source.value();

  const std::optional<quader::QuaderMeshAsset> target_mesh =
      fixture.target.compiled_mesh();
  require(target_mesh.has_value(), "Target terrain has no compiled mesh.");
  fixture.target_mesh = *target_mesh;

  const quader::QuaderModelingObjectRef target_ref = fixture.target.ref();
  const quader::QuaderModelingObjectRef source_ref = fixture.source.ref();
  fixture.target_participant = {
      .display_entity = target_ref.entity,
      .geometry_entity = target_ref.entity,
      .definition_entity = quader::kInvalidEntityId,
  };
  fixture.source_participant = {
      .display_entity = source_ref.entity,
      .geometry_entity = source_ref.entity,
      .definition_entity = quader::kInvalidEntityId,
  };
}

BuildCounterDelta counter_delta(const quader::SceneSurfaceQueryStats &before,
                                const quader::SceneSurfaceQueryStats &after) {
  require(after.snapshot_build_count >= before.snapshot_build_count &&
              after.tlas_build_count >= before.tlas_build_count &&
              after.blas_build_count >= before.blas_build_count,
          "Scene surface query counters moved backwards.");
  return {
      .snapshot_builds =
          after.snapshot_build_count - before.snapshot_build_count,
      .tlas_builds = after.tlas_build_count - before.tlas_build_count,
      .blas_builds = after.blas_build_count - before.blas_build_count,
  };
}

void add_delta(BuildCounterDelta &target, const BuildCounterDelta &value) {
  target.snapshot_builds += value.snapshot_builds;
  target.tlas_builds += value.tlas_builds;
  target.blas_builds += value.blas_builds;
}

std::uint64_t
surface_index_checksum(const quader::SceneSurfaceQuerySnapshot &snapshot,
                       const quader::SceneSurfaceQueryStats &stats) {
  std::uint64_t checksum = kFnvOffset;
  hash_u64(checksum, stats.participant_count);
  hash_u64(checksum, stats.triangle_count);
  hash_u64(checksum, stats.unique_geometry_count);
  for (const quader::SceneSurfaceParticipant &participant :
       snapshot.participants()) {
    hash_u64(checksum, participant.id.display_entity);
    hash_u64(checksum, participant.id.geometry_entity);
    hash_u64(checksum, participant.id.definition_entity);
    hash_vec3(checksum, participant.world_bounds.min);
    hash_vec3(checksum, participant.world_bounds.max);
    hash_u64(checksum, participant.triangle_count);
  }
  return checksum;
}

void hash_surface_hit(std::uint64_t &checksum, std::size_t ray_index,
                      const std::optional<quader::SceneSurfaceHit> &hit) {
  hash_u64(checksum, ray_index);
  hash_u64(checksum, hit.has_value() ? 1u : 0u);
  if (!hit.has_value()) {
    return;
  }
  hash_u64(checksum, hit->participant.display_entity);
  hash_u64(checksum, hit->participant.geometry_entity);
  hash_u64(checksum, hit->participant.definition_entity);
  hash_u64(checksum, hit->primitive_index);
  hash_float(checksum, hit->distance);
  hash_vec3(checksum, hit->world_position);
}

ProjectionBatch project_dense_vertices(
    const quader::SceneSurfaceQuerySnapshot &snapshot,
    std::span<const quader::QVec3> origins,
    std::span<const quader::SceneSurfaceParticipantId> exclusions,
    quader::SceneSurfaceQueryScratch &scratch) {
  ProjectionBatch result;
  for (std::size_t index = 0u; index < origins.size(); ++index) {
    const std::optional<quader::SceneSurfaceHit> hit = snapshot.nearest_hit(
        {
            .world_origin = origins[index],
            .world_direction = {0.0F, -1.0F, 0.0F},
            .min_distance = kRayMinDistance,
            .max_distance = kRayMaxDistance,
            .excluded_participants = exclusions,
        },
        scratch);
    ++result.rays;
    if (hit.has_value()) {
      ++result.hits;
    } else {
      ++result.misses;
    }
    hash_surface_hit(result.checksum, index, hit);
  }
  return result;
}

/// Carries one linear-oracle hit used outside measured intervals.
struct BruteForceHit final {
  float distance = 0.0F;
  quader::QVec3 world_position{};
  std::size_t primitive_index = 0u;
};

std::optional<BruteForceHit>
brute_force_target_hit(const BenchmarkFixture &fixture,
                       quader::QVec3 origin) {
  const quader::QMat4 world_from_local = fixture.session.scene()
                                               .world_transform_matrix(
                                                   fixture.target.ref().entity);
  const quader_geometry::QRay3<float> ray{
      {origin.x, origin.y, origin.z}, {0.0F, -1.0F, 0.0F}};
  std::optional<BruteForceHit> best;
  for (std::size_t offset = 0u;
       offset + 2u < fixture.target_mesh.indices.size(); offset += 3u) {
    const std::uint32_t first = fixture.target_mesh.indices[offset];
    const std::uint32_t second = fixture.target_mesh.indices[offset + 1u];
    const std::uint32_t third = fixture.target_mesh.indices[offset + 2u];
    if (first >= fixture.target_mesh.vertices.size() ||
        second >= fixture.target_mesh.vertices.size() ||
        third >= fixture.target_mesh.vertices.size()) {
      continue;
    }
    const auto world_point = [&](std::uint32_t vertex_index) {
      return quader::transform_point(
          world_from_local,
          fixture.target_mesh.vertices[vertex_index].position);
    };
    const quader::QVec3 a = world_point(first);
    const quader::QVec3 b = world_point(second);
    const quader::QVec3 c = world_point(third);
    const quader_geometry::QRayTriangleHit<float> hit =
        quader_geometry::intersect_ray_triangle<float>(
            ray, {{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}});
    if (!hit.hit || hit.distance < kRayMinDistance ||
        hit.distance > kRayMaxDistance) {
      continue;
    }
    const std::size_t primitive_index = offset / 3u;
    const bool closer = !best.has_value() ||
                        hit.distance < best->distance - 0.000001F;
    const bool tied = best.has_value() &&
                      std::abs(hit.distance - best->distance) <= 0.000001F;
    if (closer || (tied && primitive_index < best->primitive_index)) {
      best = BruteForceHit{
          .distance = hit.distance,
          .world_position = {hit.point.x, hit.point.y, hit.point.z},
          .primitive_index = primitive_index,
      };
    }
  }
  return best;
}

void hash_brute_force_hit(std::uint64_t &checksum, std::size_t ray_index,
                          const BenchmarkFixture &fixture,
                          const std::optional<BruteForceHit> &hit) {
  hash_u64(checksum, ray_index);
  hash_u64(checksum, hit.has_value() ? 1u : 0u);
  if (!hit.has_value()) {
    return;
  }
  hash_u64(checksum, fixture.target_participant.display_entity);
  hash_u64(checksum, fixture.target_participant.geometry_entity);
  hash_u64(checksum, fixture.target_participant.definition_entity);
  hash_u64(checksum, hit->primitive_index);
  hash_float(checksum, hit->distance);
  hash_vec3(checksum, hit->world_position);
}

ParityReport run_parity_audit(
    const BenchmarkOptions &options, const BenchmarkFixture &fixture,
    const quader::SceneSurfaceQuerySnapshot &snapshot,
    std::span<const quader::SceneSurfaceParticipantId> exclusions) {
  ParityReport report;
  report.enabled = options.brute_force_parity;
  if (!report.enabled) {
    return report;
  }
  const std::size_t sample_count = std::min(
      fixture.ray_origins.size(), static_cast<std::size_t>(options.parity_rays));
  quader::SceneSurfaceQueryScratch parity_scratch;
  for (std::size_t sample = 0u; sample < sample_count; ++sample) {
    const std::size_t ray_index =
        (sample * fixture.ray_origins.size()) / sample_count;
    const quader::QVec3 origin = fixture.ray_origins[ray_index];
    const std::optional<quader::SceneSurfaceHit> indexed = snapshot.nearest_hit(
        {
            .world_origin = origin,
            .world_direction = {0.0F, -1.0F, 0.0F},
            .min_distance = kRayMinDistance,
            .max_distance = kRayMaxDistance,
            .excluded_participants = exclusions,
        },
        parity_scratch);
    const std::optional<BruteForceHit> brute =
        brute_force_target_hit(fixture, origin);
    hash_surface_hit(report.indexed_checksum, ray_index, indexed);
    hash_brute_force_hit(report.brute_force_checksum, ray_index, fixture,
                         brute);
    ++report.rays_checked;

    bool matches = indexed.has_value() == brute.has_value();
    if (matches && indexed.has_value()) {
      const quader::QVec3 delta =
          indexed->world_position - brute->world_position;
      matches = indexed->participant == fixture.target_participant &&
                indexed->primitive_index == brute->primitive_index &&
                std::abs(indexed->distance - brute->distance) <=
                    kParityTolerance &&
                quader::length_squared(delta) <=
                    kParityTolerance * kParityTolerance;
    }
    if (!matches) {
      ++report.mismatches;
      if (report.first_mismatch.empty()) {
        report.first_mismatch = "ray " + std::to_string(ray_index) +
                                " differed from the linear oracle";
      }
    }
  }
  return report;
}

BenchmarkReport run_benchmark(const BenchmarkOptions &options) {
  BenchmarkReport report;
  report.options = options;
  BenchmarkFixture fixture;
  populate_fixture(fixture, options);
  report.target_triangles = fixture.target_mesh.indices.size() / 3u;
  const std::optional<quader::QuaderMeshAsset> source_mesh =
      fixture.source.compiled_mesh();
  require(source_mesh.has_value(), "Source grid has no compiled mesh.");
  report.source_triangles = source_mesh->indices.size() / 3u;
  report.rays_per_batch = fixture.ray_origins.size();
  const std::array exclusions{fixture.source_participant};

  std::vector<double> cold_samples;
  cold_samples.reserve(static_cast<std::size_t>(options.cold_samples));
  std::optional<std::uint64_t> expected_index_checksum;
  bool cold_counters_match = true;
  for (int sample = 0; sample < options.cold_samples; ++sample) {
    quader::SceneSurfaceQuery query(
        fixture.session.scene(), fixture.session.assets(),
        fixture.session.viewport_local_visibility());
    const quader::SceneSurfaceQueryStats before = query.stats();
    const Clock::time_point begin = Clock::now();
    const quader::SceneSurfaceQuerySnapshot snapshot = query.snapshot();
    const Clock::time_point end = Clock::now();
    require(snapshot.valid(), "Cold surface snapshot is invalid.");
    cold_samples.push_back(elapsed_milliseconds(begin, end));
    const quader::SceneSurfaceQueryStats after = query.stats();
    const BuildCounterDelta delta = counter_delta(before, after);
    add_delta(report.cold_build_delta, delta);
    cold_counters_match =
        cold_counters_match && delta.snapshot_builds == 1u &&
        delta.tlas_builds == 1u &&
        delta.blas_builds == after.unique_geometry_count;
    const std::uint64_t checksum = surface_index_checksum(snapshot, after);
    if (!expected_index_checksum.has_value()) {
      expected_index_checksum = checksum;
      report.participant_count = after.participant_count;
      report.indexed_triangle_count = after.triangle_count;
      report.unique_geometry_count = after.unique_geometry_count;
    } else {
      cold_counters_match =
          cold_counters_match && checksum == *expected_index_checksum;
    }
  }
  report.cold_build = summarize_timings(cold_samples);
  report.cold_index_checksum =
      expected_index_checksum.value_or(kFnvOffset);
  report.cold_rebuild_verified =
      cold_counters_match &&
      report.cold_build_delta.snapshot_builds ==
          static_cast<std::uint64_t>(options.cold_samples) &&
      report.cold_build_delta.tlas_builds ==
          static_cast<std::uint64_t>(options.cold_samples) &&
      report.cold_build_delta.blas_builds ==
          static_cast<std::uint64_t>(options.cold_samples) *
              report.unique_geometry_count;

  quader::SceneSurfaceQuery warm_query(
      fixture.session.scene(), fixture.session.assets(),
      fixture.session.viewport_local_visibility());
  quader::SceneSurfaceQuerySnapshot warm_snapshot = warm_query.snapshot();
  require(warm_snapshot.valid(), "Warm surface snapshot is invalid.");
  const quader::SceneSurfaceQueryStats warm_before = warm_query.stats();
  require(surface_index_checksum(warm_snapshot, warm_before) ==
              report.cold_index_checksum,
          "Cold and warm surface indexes disagree.");

  std::optional<std::uint64_t> expected_batch_checksum;
  bool batch_checksums_match = true;
  quader::SceneSurfaceQueryScratch warm_scratch;
  for (int warmup = 0; warmup < options.warmup_batches; ++warmup) {
    warm_snapshot = warm_query.snapshot();
    const ProjectionBatch batch = project_dense_vertices(
        warm_snapshot, fixture.ray_origins, exclusions, warm_scratch);
    if (!expected_batch_checksum.has_value()) {
      expected_batch_checksum = batch.checksum;
    } else {
      batch_checksums_match =
          batch_checksums_match && batch.checksum == *expected_batch_checksum;
    }
    batch_checksums_match =
        batch_checksums_match && batch.hits == batch.rays && batch.misses == 0u;
  }

  std::vector<double> warm_samples;
  warm_samples.reserve(static_cast<std::size_t>(options.warm_samples));
  for (int sample = 0; sample < options.warm_samples; ++sample) {
    const Clock::time_point begin = Clock::now();
    warm_snapshot = warm_query.snapshot();
    const ProjectionBatch batch = project_dense_vertices(
        warm_snapshot, fixture.ray_origins, exclusions, warm_scratch);
    const Clock::time_point end = Clock::now();
    warm_samples.push_back(elapsed_milliseconds(begin, end));
    if (!expected_batch_checksum.has_value()) {
      expected_batch_checksum = batch.checksum;
    } else {
      batch_checksums_match =
          batch_checksums_match && batch.checksum == *expected_batch_checksum;
    }
    report.warm_measured_rays += batch.rays;
    report.warm_measured_hits += batch.hits;
    report.warm_measured_misses += batch.misses;
    hash_u64(report.warm_aggregate_checksum, batch.checksum);
  }
  report.warm_projection = summarize_timings(warm_samples);
  report.warm_batch_checksum =
      expected_batch_checksum.value_or(kFnvOffset);
  report.warm_checksum_stable = batch_checksums_match;
  report.every_ray_hit = report.warm_measured_hits ==
                             report.warm_measured_rays &&
                         report.warm_measured_misses == 0u;
  const quader::SceneSurfaceQueryStats warm_after = warm_query.stats();
  report.warm_build_delta = counter_delta(warm_before, warm_after);
  report.warm_index_reused =
      report.warm_build_delta.snapshot_builds == 0u &&
      report.warm_build_delta.tlas_builds == 0u &&
      report.warm_build_delta.blas_builds == 0u &&
      warm_after.participant_count == warm_before.participant_count &&
      warm_after.triangle_count == warm_before.triangle_count &&
      warm_after.unique_geometry_count == warm_before.unique_geometry_count;
  report.warm_scratch_reused = options.warmup_batches > 0;

  report.parity = run_parity_audit(options, fixture, warm_snapshot, exclusions);
  const bool parity_valid =
      !report.parity.enabled || report.parity.mismatches == 0u;
  report.valid = report.cold_rebuild_verified && report.warm_index_reused &&
                 report.warm_scratch_reused && report.warm_checksum_stable &&
                 report.every_ray_hit && parity_valid;
  return report;
}

void append_timing_json(std::ostringstream &stream,
                        const TimingSummary &summary, int indentation) {
  const std::string indent(static_cast<std::size_t>(indentation), ' ');
  stream << indent << "\"samples\": " << summary.samples << ",\n"
         << indent << "\"total_ms\": " << summary.total_ms << ",\n"
         << indent << "\"average_ms\": " << summary.average_ms << ",\n"
         << indent << "\"minimum_ms\": " << summary.minimum_ms << ",\n"
         << indent << "\"median_ms\": " << summary.median_ms << ",\n"
         << indent << "\"p95_ms\": " << summary.p95_ms << ",\n"
         << indent << "\"maximum_ms\": " << summary.maximum_ms;
}

std::string report_json(const BenchmarkReport &report) {
  const auto boolean = [](bool value) { return value ? "true" : "false"; };
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(3);
  stream << "{\n"
         << "  \"schema\": \"quader.snap_to_floor.performance.v1\",\n"
         << "  \"timing_policy\": "
            "\"informational_no_absolute_pass_fail_threshold\",\n"
         << "  \"fixture\": {\n"
         << "    \"surface_segments\": " << report.options.surface_segments
         << ",\n"
         << "    \"vertex_grid\": " << report.options.vertex_grid << ",\n"
         << "    \"target_triangles\": " << report.target_triangles
         << ",\n"
         << "    \"source_triangles\": " << report.source_triangles
         << ",\n"
         << "    \"rays_per_batch\": " << report.rays_per_batch << ",\n"
         << "    \"participants\": " << report.participant_count << ",\n"
         << "    \"indexed_triangles\": " << report.indexed_triangle_count
         << ",\n"
         << "    \"unique_geometries\": " << report.unique_geometry_count
         << "\n"
         << "  },\n"
         << "  \"cold_surface_index_build\": {\n";
  append_timing_json(stream, report.cold_build, 4);
  stream << ",\n"
         << "    \"snapshot_build_delta\": "
         << report.cold_build_delta.snapshot_builds << ",\n"
         << "    \"tlas_build_delta\": "
         << report.cold_build_delta.tlas_builds << ",\n"
         << "    \"blas_build_delta\": "
         << report.cold_build_delta.blas_builds << ",\n"
         << "    \"index_checksum\": \""
         << checksum_hex(report.cold_index_checksum) << "\",\n"
         << "    \"fresh_rebuild_verified\": "
         << boolean(report.cold_rebuild_verified) << "\n"
         << "  },\n"
         << "  \"warm_dense_vertex_projection\": {\n";
  append_timing_json(stream, report.warm_projection, 4);
  stream << ",\n"
         << "    \"warmup_batches\": " << report.options.warmup_batches
         << ",\n"
         << "    \"measured_rays\": " << report.warm_measured_rays
         << ",\n"
         << "    \"hits\": " << report.warm_measured_hits << ",\n"
         << "    \"misses\": " << report.warm_measured_misses << ",\n"
         << "    \"snapshot_build_delta\": "
         << report.warm_build_delta.snapshot_builds << ",\n"
         << "    \"tlas_build_delta\": "
         << report.warm_build_delta.tlas_builds << ",\n"
         << "    \"blas_build_delta\": "
         << report.warm_build_delta.blas_builds << ",\n"
         << "    \"batch_checksum\": \""
         << checksum_hex(report.warm_batch_checksum) << "\",\n"
         << "    \"aggregate_checksum\": \""
         << checksum_hex(report.warm_aggregate_checksum) << "\",\n"
         << "    \"index_reused\": " << boolean(report.warm_index_reused)
         << ",\n"
         << "    \"caller_owned_scratch_instances\": 1,\n"
         << "    \"scratch_primed_and_reused\": "
         << boolean(report.warm_scratch_reused) << ",\n"
         << "    \"checksum_stable\": "
         << boolean(report.warm_checksum_stable) << ",\n"
         << "    \"every_ray_hit\": " << boolean(report.every_ray_hit)
         << "\n"
         << "  },\n"
         << "  \"brute_force_parity\": {\n"
         << "    \"enabled\": " << boolean(report.parity.enabled) << ",\n"
         << "    \"outside_measured_intervals\": true,\n"
         << "    \"rays_checked\": " << report.parity.rays_checked << ",\n"
         << "    \"mismatches\": " << report.parity.mismatches << ",\n"
         << "    \"indexed_checksum\": \""
         << checksum_hex(report.parity.indexed_checksum) << "\",\n"
         << "    \"brute_force_checksum\": \""
         << checksum_hex(report.parity.brute_force_checksum) << "\",\n"
         << "    \"first_mismatch\": \""
         << json_escape(report.parity.first_mismatch) << "\"\n"
         << "  },\n"
         << "  \"valid\": " << boolean(report.valid) << "\n"
         << "}\n";
  return stream.str();
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
    return report.valid ? 0 : 2;
  } catch (const std::exception &exception) {
    std::cerr << "qdr_snap_to_floor_performance failed: " << exception.what()
              << '\n';
    return 1;
  }
}
