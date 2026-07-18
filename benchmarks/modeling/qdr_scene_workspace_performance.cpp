////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/mesh_asset.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "scene/document/scene_content_document.hpp"

#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <algorithm>
#include <array>
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

/// Selects benchmark fixture scale and sample defaults.
enum class FixtureProfile {
  Quick,
  Standard,
};

/// Selects the geometry and asset distribution under measurement.
enum class FixtureKind {
  RepetitiveGeometry,
  DenseHighEntropyGeometry,
  ManySmallObjects,
  ReferencedAndUnreferencedAssets,
};

/// Carries parsed command-line options for one benchmark run.
struct BenchmarkOptions final {
  FixtureProfile profile = FixtureProfile::Standard;
  std::optional<FixtureKind> scenario;
  int warmups = 1;
  int samples = 5;
  std::filesystem::path output_path;
};

/// Summarizes one timing series using p50, p95, and maximum latency.
struct TimingSummary final {
  std::size_t samples = 0u;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double maximum_ms = 0.0;
};

/// Records the logical and authored size of one generated fixture.
struct FixtureCounts final {
  std::size_t entity_count = 0u;
  std::size_t mesh_entity_count = 0u;
  std::size_t logical_geometry_count = 0u;
  std::size_t referenced_geometry_count = 0u;
  std::size_t unreferenced_geometry_count = 0u;
  std::size_t authored_vertex_count = 0u;
  std::size_t authored_face_count = 0u;
};

/// Collects binary scene save phase timings and byte counts.
struct SaveMeasurements final {
  std::vector<double> validation_ms;
  std::vector<double> snapshot_extraction_ms;
  std::vector<double> geometry_deduplication_ms;
  std::vector<double> geometry_encode_ms;
  std::vector<double> metadata_encode_ms;
  std::vector<double> container_build_and_compression_ms;
  std::vector<double> encode_total_ms;
  std::vector<double> atomic_write_ms;
  std::vector<double> total_ms;
  std::uint64_t file_bytes = 0u;
  std::uint64_t uncompressed_geometry_bytes = 0u;
  std::uint64_t stored_geometry_bytes = 0u;
  std::size_t logical_geometry_count = 0u;
  std::size_t unique_geometry_count = 0u;
  std::size_t compressed_geometry_count = 0u;
};

/// Collects binary scene load phase timings and decode counts.
struct LoadMeasurements final {
  std::vector<double> byte_read_ms;
  std::vector<double> envelope_verification_ms;
  std::vector<double> decompression_ms;
  std::vector<double> metadata_flatbuffer_access_ms;
  std::vector<double> geometry_decode_ms;
  std::vector<double> geometry_compile_ms;
  std::vector<double> hydration_ms;
  std::vector<double> read_total_ms;
  std::vector<double> session_installation_ms;
  std::vector<double> total_ms;
  std::uint64_t file_bytes = 0u;
  std::uint64_t stored_chunk_bytes = 0u;
  std::uint64_t raw_chunk_bytes = 0u;
  std::uint64_t decompressed_bytes = 0u;
  std::size_t compressed_chunk_count = 0u;
  std::size_t logical_geometry_count = 0u;
  std::size_t unique_geometry_decoded_count = 0u;
  std::size_t unique_geometry_compiled_count = 0u;
};

/// Collects resident tab-switch and clean-close latency samples.
struct WorkspaceMeasurements final {
  std::vector<double> tab_switch_ms;
  std::vector<double> clean_scene_close_ms;
};

/// Groups fixture metadata with all measurements for one scenario.
struct ScenarioReport final {
  FixtureKind kind = FixtureKind::RepetitiveGeometry;
  FixtureCounts fixture;
  SaveMeasurements save;
  LoadMeasurements load;
  WorkspaceMeasurements workspace;
};

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string message) {
  if (!condition) {
    fail(std::move(message));
  }
}

double elapsed_ms(Clock::time_point started) {
  return std::chrono::duration<double, std::milli>(Clock::now() - started)
      .count();
}

int parse_count(std::string_view value, std::string_view option,
                bool allow_zero) {
  try {
    std::size_t parsed = 0u;
    const unsigned long long result =
        std::stoull(std::string(value), &parsed);
    if (parsed != value.size() || (!allow_zero && result == 0u) ||
        result > static_cast<unsigned long long>(
                     std::numeric_limits<int>::max())) {
      throw std::invalid_argument("range");
    }
    return static_cast<int>(result);
  } catch (const std::exception &) {
    throw std::invalid_argument("Invalid value for " + std::string(option) +
                                ": " + std::string(value));
  }
}

std::optional<FixtureKind> parse_scenario(std::string_view value) {
  if (value == "all") {
    return std::nullopt;
  }
  if (value == "repetitive") {
    return FixtureKind::RepetitiveGeometry;
  }
  if (value == "dense") {
    return FixtureKind::DenseHighEntropyGeometry;
  }
  if (value == "many-small") {
    return FixtureKind::ManySmallObjects;
  }
  if (value == "referenced-unreferenced") {
    return FixtureKind::ReferencedAndUnreferencedAssets;
  }
  throw std::invalid_argument("Unknown scenario: " + std::string(value));
}

BenchmarkOptions parse_args(int argc, char **argv) {
  BenchmarkOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value_after = [&](std::string_view option) -> std::string_view {
      if (index + 1 >= argc) {
        throw std::invalid_argument("Missing value for " +
                                    std::string(option));
      }
      ++index;
      return argv[index];
    };

    if (argument == "--help" || argument == "-h") {
      std::cout
          << "Usage: qdr_scene_workspace_performance [options]\n"
          << "  --profile quick|standard\n"
          << "  --scenario all|repetitive|dense|many-small|"
             "referenced-unreferenced\n"
          << "  --warmups N\n"
          << "  --samples N\n"
          << "  --output report.json\n";
      std::exit(0);
    }
    if (argument == "--profile") {
      const std::string_view value = value_after(argument);
      if (value == "quick") {
        options.profile = FixtureProfile::Quick;
      } else if (value == "standard") {
        options.profile = FixtureProfile::Standard;
      } else {
        throw std::invalid_argument("Unknown fixture profile: " +
                                    std::string(value));
      }
    } else if (argument == "--scenario") {
      options.scenario = parse_scenario(value_after(argument));
    } else if (argument == "--warmups") {
      options.warmups = parse_count(value_after(argument), argument, true);
    } else if (argument == "--samples") {
      options.samples = parse_count(value_after(argument), argument, false);
    } else if (argument == "--output") {
      options.output_path =
          std::filesystem::absolute(value_after(argument)).lexically_normal();
    } else {
      throw std::invalid_argument("Unknown argument: " +
                                  std::string(argument));
    }
  }
  return options;
}

std::string_view fixture_name(FixtureKind kind) {
  switch (kind) {
  case FixtureKind::RepetitiveGeometry:
    return "repetitive_geometry";
  case FixtureKind::DenseHighEntropyGeometry:
    return "dense_high_entropy_geometry";
  case FixtureKind::ManySmallObjects:
    return "many_small_objects";
  case FixtureKind::ReferencedAndUnreferencedAssets:
    return "referenced_and_unreferenced_assets";
  }
  return "unknown";
}

std::string_view profile_name(FixtureProfile profile) {
  return profile == FixtureProfile::Quick ? "quick" : "standard";
}

std::vector<FixtureKind>
selected_fixtures(const BenchmarkOptions &options) {
  if (options.scenario.has_value()) {
    return {*options.scenario};
  }
  return {
      FixtureKind::RepetitiveGeometry,
      FixtureKind::DenseHighEntropyGeometry,
      FixtureKind::ManySmallObjects,
      FixtureKind::ReferencedAndUnreferencedAssets,
  };
}

double percentile_nearest_rank(const std::vector<double> &sorted,
                               double percentile) {
  require(!sorted.empty(), "Cannot summarize an empty timing series.");
  const double rank =
      std::ceil(percentile * static_cast<double>(sorted.size()));
  const std::size_t index = static_cast<std::size_t>(
      std::clamp(rank, 1.0, static_cast<double>(sorted.size())) - 1.0);
  return sorted[index];
}

TimingSummary summarize(const std::vector<double> &samples) {
  require(!samples.empty(), "Benchmark timing series is empty.");
  std::vector<double> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  return {
      .samples = sorted.size(),
      .p50_ms = percentile_nearest_rank(sorted, 0.50),
      .p95_ms = percentile_nearest_rank(sorted, 0.95),
      .maximum_ms = sorted.back(),
  };
}

/// Generates repeatable pseudo-random fixture coordinates.
class DeterministicRandom final {
public:
  explicit DeterministicRandom(std::uint64_t state) : state_(state) {}

  std::uint32_t next_u32() {
    state_ ^= state_ << 13u;
    state_ ^= state_ >> 7u;
    state_ ^= state_ << 17u;
    return static_cast<std::uint32_t>(state_ >> 16u);
  }

  float next_float(float extent) {
    constexpr float kDenominator = 16777215.0f;
    const float normalized =
        static_cast<float>(next_u32() & 0x00ffffffu) / kDenominator;
    return (normalized * 2.0f - 1.0f) * extent;
  }

private:
  std::uint64_t state_;
};

std::shared_ptr<quader_poly::Document>
make_authored_triangle_soup(std::size_t triangle_count, bool high_entropy,
                            std::uint64_t seed) {
  auto document = std::make_shared<quader_poly::Document>();
  document->vertices.reserve(triangle_count * 3u);
  document->faces.reserve(triangle_count);
  DeterministicRandom random(seed);
  for (std::size_t triangle = 0u; triangle < triangle_count; ++triangle) {
    const quader_poly::ElementId first =
        static_cast<quader_poly::ElementId>(triangle * 3u + 1u);
    quader::QVec3 origin;
    quader::QVec3 second;
    quader::QVec3 third;
    if (high_entropy) {
      origin = {random.next_float(1000.0f), random.next_float(1000.0f),
                random.next_float(1000.0f)};
      second = {origin.x + 0.25f + std::fabs(random.next_float(2.0f)),
                origin.y, origin.z + random.next_float(0.1f)};
      third = {origin.x,
               origin.y + 0.25f + std::fabs(random.next_float(2.0f)),
               origin.z + random.next_float(0.1f)};
    } else {
      const float x = static_cast<float>(triangle % 32u);
      const float y = static_cast<float>((triangle / 32u) % 32u);
      const float z = static_cast<float>((triangle / 1024u) % 4u);
      origin = {x, y, z};
      second = {x + 1.0f, y, z};
      third = {x, y + 1.0f, z};
    }
    document->vertices.push_back({.id = first, .position = origin});
    document->vertices.push_back(
        {.id = static_cast<quader_poly::ElementId>(first + 1u),
         .position = second});
    document->vertices.push_back(
        {.id = static_cast<quader_poly::ElementId>(first + 2u),
         .position = third});
    quader_poly::Face face;
    face.id = static_cast<quader_poly::ElementId>(triangle + 1u);
    face.vertices = {first, static_cast<quader_poly::ElementId>(first + 1u),
                     static_cast<quader_poly::ElementId>(first + 2u)};
    face.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    face.normal_shading = quader_poly::SurfaceShading::Smooth;
    document->faces.push_back(std::move(face));
  }
  document->next_vertex_id = static_cast<quader_poly::ElementId>(
      document->vertices.size() + 1u);
  document->next_face_id =
      static_cast<quader_poly::ElementId>(document->faces.size() + 1u);
  return document;
}

std::shared_ptr<quader_poly::Document>
make_authored_fan(std::size_t vertex_count, std::size_t face_count,
                  float phase, float z_offset) {
  require(vertex_count >= 3u && face_count <= vertex_count - 1u,
          "Authored fan dimensions are invalid.");
  auto document = std::make_shared<quader_poly::Document>();
  document->vertices.reserve(vertex_count);
  document->faces.reserve(face_count);
  document->vertices.push_back(
      {.id = 1u, .position = {0.0f, 0.0f, z_offset}});
  constexpr float kTau = 6.28318530717958647692f;
  const std::size_t ring_count = vertex_count - 1u;
  for (std::size_t index = 0u; index < ring_count; ++index) {
    const float angle =
        phase + kTau * static_cast<float>(index) /
                    static_cast<float>(ring_count);
    document->vertices.push_back({
        .id = static_cast<quader_poly::ElementId>(index + 2u),
        .position = {std::cos(angle), std::sin(angle), z_offset},
    });
  }
  for (std::size_t index = 0u; index < face_count; ++index) {
    quader_poly::Face face;
    face.id = static_cast<quader_poly::ElementId>(index + 1u);
    face.vertices = {
        1u,
        static_cast<quader_poly::ElementId>(index + 2u),
        static_cast<quader_poly::ElementId>((index + 1u) % ring_count + 2u),
    };
    face.uvs = {{0.5f, 0.5f}, {1.0f, 0.5f}, {0.5f, 1.0f}};
    face.normal_shading = quader_poly::SurfaceShading::Smooth;
    document->faces.push_back(std::move(face));
  }
  document->next_vertex_id =
      static_cast<quader_poly::ElementId>(vertex_count + 1u);
  document->next_face_id =
      static_cast<quader_poly::ElementId>(face_count + 1u);
  return document;
}

quader::MeshAsset make_authored_mesh(
    std::string name, const std::shared_ptr<quader_poly::Document> &document) {
  quader::MeshAsset mesh;
  mesh.name = std::move(name);
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document = document;
  return mesh;
}

quader::MeshAsset make_small_compiled_mesh(std::string name) {
  quader::MeshAsset mesh;
  mesh.name = std::move(name);
  quader::MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.0f, 0.0f}},
      {.position = {0.0f, 1.0f, 0.0f},
       .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 1.0f}},
  };
  geometry.indices = {0u, 1u, 2u};
  geometry.primitives = {{.index_offset = 0u,
                          .index_count = 3u,
                          .material_slot = 0u}};
  return mesh;
}

quader::AssetId add_mesh(quader::SceneContentDocument &document,
                         quader::AssetId id, quader::MeshAsset mesh) {
  const quader::AssetId added =
      document.assets().add_mesh(id, std::move(mesh));
  require(added == id, "Synthetic fixture could not add mesh asset.");
  return added;
}

void add_mesh_entity(quader::SceneContentDocument &document,
                     quader::AssetId mesh, std::size_t index) {
  const quader::EntityId entity = document.scene().create_entity(
      "Mesh " + std::to_string(index), quader::EntityKind::Mesh);
  require(entity != quader::kInvalidEntityId,
          "Synthetic fixture could not add mesh entity.");
  document.scene().set_mesh(entity, quader::MeshComponent{.mesh = mesh});
}

FixtureCounts analyze_fixture(const quader::SceneContentDocument &document) {
  FixtureCounts counts;
  const std::vector<quader::EntityId> entities = document.scene().entities();
  counts.entity_count = entities.size();
  std::set<quader::AssetId> referenced;
  for (const quader::EntityId entity : entities) {
    if (const quader::MeshComponent *mesh = document.scene().mesh(entity)) {
      ++counts.mesh_entity_count;
      referenced.insert(mesh->mesh);
    }
  }
  const std::vector<quader::AssetId> mesh_ids = document.assets().mesh_ids();
  counts.logical_geometry_count = mesh_ids.size();
  counts.referenced_geometry_count = referenced.size();
  counts.unreferenced_geometry_count =
      mesh_ids.size() - std::min(mesh_ids.size(), referenced.size());
  for (const quader::AssetId id : mesh_ids) {
    const quader::MeshAsset *mesh = document.assets().mesh(id);
    require(mesh != nullptr, "Synthetic fixture lost a mesh asset.");
    if (mesh->authored_polygon_document) {
      counts.authored_vertex_count +=
          mesh->authored_polygon_document->vertices.size();
      counts.authored_face_count +=
          mesh->authored_polygon_document->faces.size();
    }
  }
  return counts;
}

void finish_fixture(quader::SceneContentDocument &document) {
  (void)document.synchronize_object_records_with_scene();
  const quader::SceneContentDocumentIdentityReport identities =
      document.validate_object_identity_invariants();
  require(identities.ok(),
          identities.issues.empty()
              ? "Synthetic fixture identity validation failed."
              : identities.issues.front().message);
  const quader::SceneContentDocumentAssetReferenceReport references =
      document.validate_asset_references();
  require(references.ok(),
          references.issues.empty()
              ? "Synthetic fixture asset validation failed."
              : references.issues.front().message);
}

quader::SceneContentDocument make_fixture(FixtureKind kind,
                                          FixtureProfile profile) {
  quader::SceneContentDocument document(quader::SceneContentDocumentRecord{
      .id = static_cast<std::uint64_t>(kind) + 1u,
      .name = std::string(fixture_name(kind)),
  });
  const bool quick = profile == FixtureProfile::Quick;
  constexpr quader::AssetId kFirstMesh = 100u;

  switch (kind) {
  case FixtureKind::RepetitiveGeometry: {
    if (quick) {
      const auto topology =
          make_authored_triangle_soup(200u, false, 0x12345678u);
      for (std::size_t index = 0u; index < 4u; ++index) {
        add_mesh(document, kFirstMesh + index,
                 make_authored_mesh("Repeated " + std::to_string(index),
                                    topology));
      }
      for (std::size_t index = 0u; index < 2u; ++index) {
        add_mesh_entity(document, kFirstMesh + index, index);
      }
      break;
    }

    // Mirrors the measured 2.70 MB legacy fixture: 1,108 logical meshes,
    // 416 distinct referenced meshes, and four repeated topology payloads.
    // The group counts reproduce its 19,148 authored vertices and 14,361
    // authored faces while exercising binary semantic deduplication.
    const std::array<std::shared_ptr<quader_poly::Document>, 4u> topologies = {
        make_authored_fan(20u, 15u, 0.0f, 0.0f),
        make_authored_fan(20u, 15u, 0.125f, 0.05f),
        make_authored_fan(8u, 6u, 0.25f, 0.1f),
        make_authored_fan(8u, 6u, 0.375f, 0.15f),
    };
    constexpr std::array<std::size_t, 4u> kTopologyRepetitions = {
        448u, 409u, 128u, 123u};
    std::size_t logical_index = 0u;
    for (std::size_t topology_index = 0u;
         topology_index < topologies.size(); ++topology_index) {
      for (std::size_t repetition = 0u;
           repetition < kTopologyRepetitions[topology_index]; ++repetition) {
        add_mesh(document, kFirstMesh + logical_index,
                 make_authored_mesh(
                     "Repeated " + std::to_string(logical_index),
                     topologies[topology_index]));
        ++logical_index;
      }
    }
    for (std::size_t index = 0u; index < 416u; ++index) {
      add_mesh_entity(document, kFirstMesh + index, index);
    }
    const quader::EntityId light = document.scene().create_entity(
        "Fixture Light", quader::EntityKind::Light);
    require(light != quader::kInvalidEntityId,
            "Synthetic fixture could not add its light entity.");
    document.scene().set_light(light, quader::LightComponent{});
    break;
  }
  case FixtureKind::DenseHighEntropyGeometry: {
    const std::size_t triangle_count = quick ? 500u : 8000u;
    const std::size_t logical_count = quick ? 1u : 2u;
    for (std::size_t index = 0u; index < logical_count; ++index) {
      const auto topology = make_authored_triangle_soup(
          triangle_count, true, 0x9e3779b97f4a7c15ULL + index * 7919u);
      add_mesh(document, kFirstMesh + index,
               make_authored_mesh("Dense " + std::to_string(index),
                                  topology));
      add_mesh_entity(document, kFirstMesh + index, index);
    }
    break;
  }
  case FixtureKind::ManySmallObjects: {
    const std::size_t logical_count = quick ? 4u : 24u;
    const std::size_t entity_count = quick ? 100u : 2500u;
    for (std::size_t index = 0u; index < logical_count; ++index) {
      add_mesh(document, kFirstMesh + index,
               make_small_compiled_mesh("Small " + std::to_string(index)));
    }
    for (std::size_t index = 0u; index < entity_count; ++index) {
      add_mesh_entity(document, kFirstMesh + index % logical_count, index);
    }
    break;
  }
  case FixtureKind::ReferencedAndUnreferencedAssets: {
    const std::size_t logical_count = quick ? 4u : 12u;
    const std::size_t referenced_count = quick ? 2u : 5u;
    const std::size_t triangle_count = quick ? 100u : 512u;
    for (std::size_t index = 0u; index < logical_count; ++index) {
      const auto topology = make_authored_triangle_soup(
          triangle_count, true, 0xd1b54a32d192ed03ULL + index * 104729u);
      add_mesh(document, kFirstMesh + index,
               make_authored_mesh("Asset " + std::to_string(index),
                                  topology));
    }
    for (std::size_t index = 0u; index < referenced_count; ++index) {
      add_mesh_entity(document, kFirstMesh + index, index);
    }
    break;
  }
  }

  finish_fixture(document);
  return document;
}

std::array<std::byte, 16> deterministic_uuid(FixtureKind kind,
                                              std::uint8_t variant) {
  std::array<std::byte, 16> uuid{};
  for (std::size_t index = 0u; index < uuid.size(); ++index) {
    uuid[index] = static_cast<std::byte>(
        1u + static_cast<unsigned>(kind) * 32u +
        static_cast<unsigned>(variant) * 17u +
        static_cast<unsigned>(index));
  }
  return uuid;
}

void write_bytes_atomic(const std::filesystem::path &path,
                        const std::vector<std::byte> &bytes) {
  const std::string_view binary(
      reinterpret_cast<const char *>(bytes.data()), bytes.size());
  const quader_io::QdrAtomicWriteResult written =
      quader_io::write_qdr_text_atomic(path, binary);
  require(written.ok,
          written.message.empty() ? "Atomic QDR write failed."
                                  : written.message);
}

/// Owns the benchmark's isolated temporary workspace directory.
class ScopedBenchmarkDirectory final {
public:
  ScopedBenchmarkDirectory() {
    const std::filesystem::path base =
        std::filesystem::temp_directory_path();
    for (std::uint64_t attempt = 0u; attempt < 100u; ++attempt) {
      const std::uint64_t stamp = static_cast<std::uint64_t>(
          Clock::now().time_since_epoch().count());
      path_ = base / ("quader-qdr-performance-" + std::to_string(stamp) +
                      "-" + std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(path_, error)) {
        return;
      }
    }
    fail("Could not create the QDR benchmark temporary directory.");
  }

  ~ScopedBenchmarkDirectory() {
    if (path_.filename().string().starts_with("quader-qdr-performance-")) {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    }
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

SaveMeasurements measure_save(const BenchmarkOptions &options,
                              FixtureKind kind,
                              const quader::SceneContentDocument &document,
                              const std::filesystem::path &target) {
  SaveMeasurements result;
  for (int iteration = 0;
       iteration < options.warmups + options.samples; ++iteration) {
    const bool measured = iteration >= options.warmups;
    double validation_ms = 0.0;
    double snapshot_ms = 0.0;
    double encode_total_ms = 0.0;
    double atomic_write_ms = 0.0;
    quader::QdrSceneBinaryEncodeResult encoded_stats;
    const Clock::time_point total_started = Clock::now();
    {
      const Clock::time_point phase_started = Clock::now();
      const quader::SceneContentDocumentIdentityReport identities =
          document.validate_object_identity_invariants();
      const quader::SceneContentDocumentAssetReferenceReport references =
          document.validate_asset_references();
      require(identities.ok() && references.ok(),
              "Save validation rejected a synthetic fixture.");
      validation_ms = elapsed_ms(phase_started);

      const Clock::time_point snapshot_started = Clock::now();
      quader::QdrProjectWriteSnapshotResult snapshot_result =
          quader::qdr_project_write_snapshot_from_document(document);
      require(snapshot_result.ok(), snapshot_result.error().message);
      const quader::QdrProjectWriteSnapshot snapshot =
          snapshot_result.take_value();
      snapshot_ms = elapsed_ms(snapshot_started);

      const Clock::time_point encode_started = Clock::now();
      encoded_stats =
          quader::QdrDocumentWriter::serialize_project_document_with_stats(
              snapshot, deterministic_uuid(kind, 0u));
      encode_total_ms = elapsed_ms(encode_started);

      const Clock::time_point write_started = Clock::now();
      write_bytes_atomic(target, encoded_stats.bytes);
      atomic_write_ms = elapsed_ms(write_started);
    }
    const double total_ms = elapsed_ms(total_started);
    if (!measured) {
      continue;
    }
    result.validation_ms.push_back(validation_ms);
    result.snapshot_extraction_ms.push_back(snapshot_ms);
    result.geometry_deduplication_ms.push_back(
        encoded_stats.geometry_deduplication_ms);
    result.geometry_encode_ms.push_back(encoded_stats.geometry_encode_ms);
    result.metadata_encode_ms.push_back(encoded_stats.metadata_encode_ms);
    result.container_build_and_compression_ms.push_back(
        encoded_stats.container_build_and_compression_ms);
    result.encode_total_ms.push_back(encode_total_ms);
    result.atomic_write_ms.push_back(atomic_write_ms);
    result.total_ms.push_back(total_ms);
    result.file_bytes = encoded_stats.bytes.size();
    result.uncompressed_geometry_bytes =
        encoded_stats.uncompressed_geometry_bytes;
    result.stored_geometry_bytes = encoded_stats.stored_geometry_bytes;
    result.logical_geometry_count = encoded_stats.logical_geometry_count;
    result.unique_geometry_count = encoded_stats.unique_geometry_count;
    result.compressed_geometry_count =
        encoded_stats.compressed_geometry_count;
  }
  return result;
}

LoadMeasurements measure_load(const BenchmarkOptions &options,
                              const std::filesystem::path &target) {
  LoadMeasurements result;
  for (int iteration = 0;
       iteration < options.warmups + options.samples; ++iteration) {
    const bool measured = iteration >= options.warmups;
    const Clock::time_point total_started = Clock::now();
    const Clock::time_point read_started = Clock::now();
    quader::QdrProjectReadResult read =
        quader::QdrDocumentReader::read_project_document(target);
    const double read_total_ms = elapsed_ms(read_started);
    require(read.ok, read.message.empty() ? "QDR load failed." : read.message);
    const quader::QdrProjectReadStats stats = read.stats;
    const Clock::time_point install_started = Clock::now();
    auto session =
        std::make_unique<quader::SceneSession>(std::move(read.document));
    const double install_ms = elapsed_ms(install_started);
    const double total_ms = elapsed_ms(total_started);
    require(session->document().validate_asset_references().ok(),
            "Installed scene session has invalid asset references.");
    if (!measured) {
      continue;
    }
    result.byte_read_ms.push_back(stats.byte_read_ms);
    result.envelope_verification_ms.push_back(
        stats.container_access_and_verification_ms);
    result.decompression_ms.push_back(stats.decompression_ms);
    result.metadata_flatbuffer_access_ms.push_back(stats.metadata_access_ms);
    result.geometry_decode_ms.push_back(stats.geometry_decode_ms);
    result.geometry_compile_ms.push_back(stats.geometry_compile_ms);
    result.hydration_ms.push_back(stats.hydration_ms);
    result.read_total_ms.push_back(read_total_ms);
    result.session_installation_ms.push_back(install_ms);
    result.total_ms.push_back(total_ms);
    result.file_bytes = stats.file_bytes;
    result.stored_chunk_bytes = stats.stored_chunk_bytes;
    result.raw_chunk_bytes = stats.raw_chunk_bytes;
    result.decompressed_bytes = stats.decompressed_bytes;
    result.compressed_chunk_count = stats.compressed_chunk_count;
    result.logical_geometry_count = stats.logical_geometry_count;
    result.unique_geometry_decoded_count =
        stats.unique_geometry_decoded_count;
    result.unique_geometry_compiled_count =
        stats.unique_geometry_compiled_count;
  }
  return result;
}

quader::ProjectRelativePath relative_path(std::string_view portable) {
  quader::ProjectIoResult<quader::ProjectRelativePath> parsed =
      quader::ProjectRelativePath::parse_portable(portable);
  require(parsed.ok(), parsed.error().message);
  return parsed.take_value();
}

WorkspaceMeasurements measure_workspace(
    const BenchmarkOptions &options, FixtureKind kind,
    const quader::SceneContentDocument &document,
    const std::filesystem::path &scenario_directory) {
  WorkspaceMeasurements result;
  const std::filesystem::path workspace_root =
      scenario_directory / "workspace";
  std::error_code directory_error;
  require(std::filesystem::create_directory(workspace_root, directory_error),
          "Could not create the workspace benchmark project directory: " +
              directory_error.message());

  quader::ProjectSession project;
  const quader::ProjectSessionResult created = project.create(workspace_root);
  require(created.ok, created.message);
  const quader::SceneSessionId initial = project.active_scene_id();
  require(project.close_scene(initial).ok,
          "Could not close the initial clean benchmark scene.");

  quader::QdrProjectWriteSnapshotResult snapshot_result =
      quader::qdr_project_write_snapshot_from_document(document);
  require(snapshot_result.ok(), snapshot_result.error().message);
  const quader::QdrProjectWriteSnapshot snapshot =
      snapshot_result.take_value();
  constexpr std::array<std::string_view, 3> kPaths{
      "Scenes/first.qdr", "Scenes/second.qdr", "Scenes/close.qdr"};
  for (std::size_t index = 0u; index < kPaths.size(); ++index) {
    quader::QdrSceneBinaryEncodeResult encoded =
        quader::QdrDocumentWriter::serialize_project_document_with_stats(
            snapshot,
            deterministic_uuid(kind, static_cast<std::uint8_t>(index + 1u)));
    write_bytes_atomic(workspace_root / std::filesystem::path(kPaths[index]),
                       encoded.bytes);
  }

  const quader::ProjectSessionResult first_open =
      project.open_scene(relative_path(kPaths[0]));
  const quader::ProjectSessionResult second_open =
      project.open_scene(relative_path(kPaths[1]));
  require(first_open.ok && first_open.scene_session_id.has_value(),
          first_open.message);
  require(second_open.ok && second_open.scene_session_id.has_value(),
          second_open.message);
  const quader::SceneSessionId first = *first_open.scene_session_id;
  const quader::SceneSessionId second = *second_open.scene_session_id;

  for (int iteration = 0;
       iteration < options.warmups + options.samples; ++iteration) {
    const quader::SceneSessionId target =
        project.active_scene_id() == first ? second : first;
    const Clock::time_point started = Clock::now();
    const quader::ProjectSessionResult activated = project.activate(target);
    const double duration = elapsed_ms(started);
    require(activated.ok && activated.changed, activated.message);
    if (iteration >= options.warmups) {
      result.tab_switch_ms.push_back(duration);
    }
  }

  for (int iteration = 0;
       iteration < options.warmups + options.samples; ++iteration) {
    const quader::ProjectSessionResult opened =
        project.open_scene(relative_path(kPaths[2]));
    require(opened.ok && opened.scene_session_id.has_value(), opened.message);
    const Clock::time_point started = Clock::now();
    const quader::ProjectSessionResult closed =
        project.close_scene(*opened.scene_session_id);
    const double duration = elapsed_ms(started);
    require(closed.ok, closed.message);
    if (iteration >= options.warmups) {
      result.clean_scene_close_ms.push_back(duration);
    }
  }

  require(project.close().ok, "Could not close the benchmark project.");
  return result;
}

ScenarioReport run_scenario(const BenchmarkOptions &options, FixtureKind kind,
                            const std::filesystem::path &root) {
  const std::filesystem::path scenario_directory =
      root / std::filesystem::path(fixture_name(kind));
  std::error_code directory_error;
  require(std::filesystem::create_directory(scenario_directory,
                                            directory_error),
          "Could not create scenario directory: " +
              directory_error.message());
  quader::SceneContentDocument document = make_fixture(kind, options.profile);
  const FixtureCounts counts = analyze_fixture(document);
  const std::filesystem::path scene_path = scenario_directory / "scene.qdr";
  SaveMeasurements save =
      measure_save(options, kind, document, scene_path);
  LoadMeasurements load = measure_load(options, scene_path);
  WorkspaceMeasurements workspace =
      measure_workspace(options, kind, document, scenario_directory);
  return {
      .kind = kind,
      .fixture = counts,
      .save = std::move(save),
      .load = std::move(load),
      .workspace = std::move(workspace),
  };
}

void indent(std::ostream &output, int depth) {
  for (int index = 0; index < depth; ++index) {
    output << "  ";
  }
}

void write_timing(std::ostream &output, int depth, std::string_view name,
                  const std::vector<double> &samples, bool trailing_comma) {
  const TimingSummary timing = summarize(samples);
  indent(output, depth);
  output << '\"' << name << "\": {\"samples\": " << timing.samples
         << ", \"p50_ms\": " << timing.p50_ms
         << ", \"p95_ms\": " << timing.p95_ms
         << ", \"max_ms\": " << timing.maximum_ms << '}';
  if (trailing_comma) {
    output << ',';
  }
  output << '\n';
}

void write_scenario(std::ostream &output, const ScenarioReport &report,
                    bool trailing_comma) {
  output << "    {\n";
  output << "      \"name\": \"" << fixture_name(report.kind) << "\",\n";
  output << "      \"fixture\": {\n";
  output << "        \"entities\": " << report.fixture.entity_count << ",\n";
  output << "        \"mesh_entities\": "
         << report.fixture.mesh_entity_count << ",\n";
  output << "        \"logical_geometry_assets\": "
         << report.fixture.logical_geometry_count << ",\n";
  output << "        \"referenced_geometry_assets\": "
         << report.fixture.referenced_geometry_count << ",\n";
  output << "        \"unreferenced_geometry_assets\": "
         << report.fixture.unreferenced_geometry_count << ",\n";
  output << "        \"authored_vertices\": "
         << report.fixture.authored_vertex_count << ",\n";
  output << "        \"authored_faces\": "
         << report.fixture.authored_face_count << "\n";
  output << "      },\n";

  output << "      \"save\": {\n";
  output << "        \"file_bytes\": " << report.save.file_bytes << ",\n";
  output << "        \"uncompressed_geometry_bytes\": "
         << report.save.uncompressed_geometry_bytes << ",\n";
  output << "        \"stored_geometry_bytes\": "
         << report.save.stored_geometry_bytes << ",\n";
  output << "        \"logical_geometry_count\": "
         << report.save.logical_geometry_count << ",\n";
  output << "        \"unique_geometry_blob_count\": "
         << report.save.unique_geometry_count << ",\n";
  output << "        \"compressed_geometry_blob_count\": "
         << report.save.compressed_geometry_count << ",\n";
  output << "        \"timings\": {\n";
  write_timing(output, 5, "validation_ms", report.save.validation_ms, true);
  write_timing(output, 5, "snapshot_extraction_ms",
               report.save.snapshot_extraction_ms, true);
  write_timing(output, 5, "geometry_deduplication_ms",
               report.save.geometry_deduplication_ms, true);
  write_timing(output, 5, "geometry_encode_ms",
               report.save.geometry_encode_ms, true);
  write_timing(output, 5, "metadata_encode_ms",
               report.save.metadata_encode_ms, true);
  write_timing(output, 5, "container_build_and_compression_ms",
               report.save.container_build_and_compression_ms, true);
  write_timing(output, 5, "encode_total_ms", report.save.encode_total_ms,
               true);
  write_timing(output, 5, "atomic_write_ms", report.save.atomic_write_ms,
               true);
  write_timing(output, 5, "total_ms", report.save.total_ms, false);
  output << "        }\n";
  output << "      },\n";

  output << "      \"load\": {\n";
  output << "        \"file_bytes\": " << report.load.file_bytes << ",\n";
  output << "        \"stored_chunk_bytes\": "
         << report.load.stored_chunk_bytes << ",\n";
  output << "        \"raw_chunk_bytes\": "
         << report.load.raw_chunk_bytes << ",\n";
  output << "        \"decompressed_bytes\": "
         << report.load.decompressed_bytes << ",\n";
  output << "        \"compressed_chunk_count\": "
         << report.load.compressed_chunk_count << ",\n";
  output << "        \"logical_geometry_count\": "
         << report.load.logical_geometry_count << ",\n";
  output << "        \"unique_geometry_decoded_count\": "
         << report.load.unique_geometry_decoded_count << ",\n";
  output << "        \"unique_geometry_compiled_count\": "
         << report.load.unique_geometry_compiled_count << ",\n";
  output << "        \"timings\": {\n";
  write_timing(output, 5, "byte_read_ms", report.load.byte_read_ms, true);
  write_timing(output, 5, "envelope_verification_ms",
               report.load.envelope_verification_ms, true);
  write_timing(output, 5, "decompression_ms",
               report.load.decompression_ms, true);
  write_timing(output, 5, "metadata_flatbuffer_access_ms",
               report.load.metadata_flatbuffer_access_ms, true);
  write_timing(output, 5, "geometry_decode_ms",
               report.load.geometry_decode_ms, true);
  write_timing(output, 5, "geometry_compile_ms",
               report.load.geometry_compile_ms, true);
  write_timing(output, 5, "hydration_ms", report.load.hydration_ms, true);
  write_timing(output, 5, "read_total_ms", report.load.read_total_ms, true);
  write_timing(output, 5, "session_installation_ms",
               report.load.session_installation_ms, true);
  write_timing(output, 5, "total_ms", report.load.total_ms, false);
  output << "        }\n";
  output << "      },\n";

  output << "      \"workspace\": {\n";
  output << "        \"tab_switch_includes_manifest_autosave\": true,\n";
  output << "        \"timings\": {\n";
  write_timing(output, 5, "tab_switch_ms", report.workspace.tab_switch_ms,
               true);
  write_timing(output, 5, "clean_scene_close_ms",
               report.workspace.clean_scene_close_ms, false);
  output << "        }\n";
  output << "      }\n";
  output << "    }";
  if (trailing_comma) {
    output << ',';
  }
  output << '\n';
}

std::string make_report_json(const BenchmarkOptions &options,
                             const std::vector<ScenarioReport> &reports) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(6);
  output << "{\n";
  output << "  \"format\": \"quader.qdr.performance\",\n";
  output << "  \"version\": 1,\n";
#if defined(_DEBUG)
  output << "  \"build\": \"Debug\",\n";
#else
  output << "  \"build\": \"Release\",\n";
#endif
  output << "  \"profile\": \"" << profile_name(options.profile)
         << "\",\n";
  output << "  \"warmups\": " << options.warmups << ",\n";
  output << "  \"samples\": " << options.samples << ",\n";
  output << "  \"scenarios\": [\n";
  for (std::size_t index = 0u; index < reports.size(); ++index) {
    write_scenario(output, reports[index], index + 1u != reports.size());
  }
  output << "  ]\n";
  output << "}\n";
  return output.str();
}

void write_report_file(const std::filesystem::path &path,
                       std::string_view report) {
  if (path.has_parent_path()) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "Could not create benchmark output directory: " +
                        error.message());
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  require(file.is_open(), "Could not open benchmark output file: " +
                              path.string());
  file.write(report.data(), static_cast<std::streamsize>(report.size()));
  file.close();
  require(file.good(), "Could not write benchmark output file: " +
                           path.string());
}

} // namespace

int main(int argc, char **argv) {
  try {
    const BenchmarkOptions options = parse_args(argc, argv);
    ScopedBenchmarkDirectory temporary_directory;
    std::vector<ScenarioReport> reports;
    const std::vector<FixtureKind> fixtures = selected_fixtures(options);
    reports.reserve(fixtures.size());
    for (const FixtureKind kind : fixtures) {
      std::cerr << "Measuring " << fixture_name(kind) << "...\n";
      reports.push_back(
          run_scenario(options, kind, temporary_directory.path()));
    }
    const std::string report = make_report_json(options, reports);
    if (options.output_path.empty()) {
      std::cout << report;
    } else {
      write_report_file(options.output_path, report);
      std::cout << "Wrote " << options.output_path.string() << '\n';
    }
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "qdr_scene_workspace_performance: " << exception.what()
              << '\n';
    return 1;
  }
}
