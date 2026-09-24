////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_reader.hpp"

#include "assets/default_material_catalog.hpp"
#include "diagnostics/profile.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_scene_binary_codec.hpp"
#include "io/shared_file_read.hpp"

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

template <typename Result = QdrProjectReadResult>
[[nodiscard]] Result
project_error_result(quader_io::QdrDiagnostic diagnostic) {
  Result result;
  result.message = diagnostic.message;
  result.diagnostics.push_back(std::move(diagnostic));
  return result;
}

template <typename Result = QdrProjectReadResult>
[[nodiscard]] Result
project_error_result(std::string_view code, std::string message) {
  return project_error_result<Result>(
      quader_io::qdr_make_error(code, "", std::move(message)));
}

template <typename Result>
[[nodiscard]] Result project_read_result_with_source_path(
    Result result, const std::filesystem::path &path) {
  for (quader_io::QdrDiagnostic &diagnostic : result.diagnostics) {
    if (!diagnostic.source_path.has_value()) {
      diagnostic.source_path = path;
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::byte>
copy_bytes(std::span<const std::byte> bytes) {
  return {bytes.begin(), bytes.end()};
}

/// A release tag describes compatibility, but does not authorize unsupported
/// authored content. Inspect the isolated candidate before it can be published.
[[nodiscard]] bool unsupported_authoring_content(
    const SceneContentDocument &document, ProductRelease release) {
  const bool allow_lights = product_release_has_feature(release, ReleaseFeature::Lights);
  const bool allow_curves = product_release_has_feature(release, ReleaseFeature::Curves);
  const bool allow_materials = product_release_has_feature(release, ReleaseFeature::Materials);
  const auto &scene = document.scene();
  const auto &assets = document.assets();
  if (!product_release_has_feature(release, ReleaseFeature::Layers)) {
    const SceneLayerState layers = scene.layer_state();
    if (!layers.layers.empty() || !layers.assignments.empty()) {
      return true;
    }
  }
  if (!allow_curves && !assets.curve_ids().empty()) {
    return true;
  }
  if (!product_release_has_feature(release, ReleaseFeature::Environment)) {
    auto settings = scene.scene_settings();
    const SceneSettings defaults;
    settings.camera.near_plane = defaults.camera.near_plane;
    settings.camera.far_plane = defaults.camera.far_plane;
    if (!(settings == defaults)) {
      return true;
    }
  }
  const auto permitted_material = [&](const MaterialReference &reference) {
    if (!reference.valid()) {
      return true;
    }
    if (reference.project_owned()) {
      return reference.project_asset().str() == kDefaultProjectMaterialId;
    }
    const auto *material = assets.material(reference.embedded_asset());
    const auto expected = make_default_quader_material();
    if (material == nullptr || material->base_color.x != expected.base_color.x ||
        material->base_color.y != expected.base_color.y ||
        material->base_color.z != expected.base_color.z ||
        material->roughness != expected.roughness ||
        material->metallic != expected.metallic ||
        material->diffuse_model != expected.diffuse_model ||
        material->node_graph_reserved != expected.node_graph_reserved) {
      return false;
    }
    for (const auto &[slot, id] : material->texture_slots) {
      const auto *texture = assets.texture(id);
      const bool albedo = slot == material_texture_slot::kBaseColor;
      const bool roughness = slot == material_texture_slot::kRoughness;
      if (texture == nullptr || texture->generated || (!albedo && !roughness) ||
          texture->source_path.generic_string() !=
              (albedo ? kDefaultMaterialAlbedoTexturePackagePath
                      : kDefaultMaterialRoughnessTexturePackagePath) ||
          texture->usage != (albedo ? TextureUsage::BaseColor : TextureUsage::Roughness) ||
          texture->color_space != (albedo ? TextureColorSpace::Srgb : TextureColorSpace::Linear)) {
        return false;
      }
    }
    return true;
  };
  for (const auto entity : scene.entities()) {
    if ((!allow_lights && (scene.light(entity) != nullptr || scene.kind(entity) == EntityKind::Light)) ||
        (!allow_curves && (scene.curve(entity) != nullptr || scene.kind(entity) == EntityKind::Curve))) {
      return true;
    }
    if (!allow_materials) {
      const auto *material = scene.material(entity);
      if (material == nullptr) {
        continue;
      }
      if (!permitted_material(material->material)) {
        return true;
      }
      for (const auto &[slot, reference] : material->material_slots) {
        if (!permitted_material(reference)) {
          return true;
        }
      }
    }
  }
  return false;
}

[[nodiscard]] QdrProjectReadResult
deserialize_owned_project_document(
    std::vector<std::byte> bytes,
    std::optional<ProductRelease> authoring_release) {
  QdrProjectReadStats stats;
  stats.file_bytes = bytes.size();
  const std::uint64_t source_content_hash =
      quader_io::qdr_binary_content_hash(bytes);
  const auto container_started = std::chrono::steady_clock::now();
  quader_io::QdrBinaryContainerReadResult container = [&]() {
    QDR_PROFILE_SCOPE("QDR validate binary container");
    return quader_io::read_qdr_binary_container(std::move(bytes));
  }();
  stats.container_access_and_verification_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - container_started)
          .count();
  if (!container.ok) {
    if (!container.diagnostics.empty()) {
      QdrProjectReadResult result =
          project_error_result(std::move(container.diagnostics.front()));
      result.stats = stats;
      return result;
    }
    QdrProjectReadResult result = project_error_result(
        "qdr.binary.read_failed",
        container.message.empty() ? "Could not read QDR binary scene."
                                  : std::move(container.message));
    result.stats = stats;
    return result;
  }

  stats.stored_chunk_bytes = container.stored_chunk_bytes;
  stats.raw_chunk_bytes = container.raw_chunk_bytes;
  stats.decompressed_bytes = container.decompressed_bytes;
  stats.compressed_chunk_count = container.compressed_chunk_count;
  stats.decompression_ms = container.decompression_ms;
  if (authoring_release.has_value() &&
      !product_release_can_open(*authoring_release,
                                container.minimum_product_release)) {
    QdrProjectReadResult result = project_error_result(
        "qdr.binary.unsupported_product_release",
        product_release_unavailable_message(container.minimum_product_release));
    result.minimum_product_release = container.minimum_product_release;
    result.scene_uuid = container.scene_uuid;
    result.stats = stats;
    return result;
  }
  const std::array<std::byte, 16> scene_uuid = container.scene_uuid;
  QdrSceneBinaryDecodeResult decoded = [&]() {
    QDR_PROFILE_SCOPE("QDR hydrate binary scene");
    return decode_qdr_scene_binary_with_stats(container);
  }();
  if (authoring_release.has_value() &&
      *authoring_release != kInternalFullRelease &&
      unsupported_authoring_content(decoded.document, *authoring_release)) {
    auto result = project_error_result("qdr.binary.unavailable_feature",
                                       release_feature_unavailable_message());
    result.minimum_product_release = container.minimum_product_release;
    result.scene_uuid = scene_uuid;
    result.stats = stats;
    return result;
  }
  stats.logical_geometry_count = decoded.stats.logical_geometry_count;
  stats.unique_geometry_decoded_count =
      decoded.stats.unique_geometry_decoded_count;
  stats.unique_geometry_compiled_count =
      decoded.stats.unique_geometry_compiled_count;
  stats.metadata_access_ms = decoded.stats.metadata_access_ms;
  stats.geometry_decode_ms = decoded.stats.geometry_decode_ms;
  stats.geometry_compile_ms = decoded.stats.geometry_compile_ms;
  stats.hydration_ms = decoded.stats.hydration_ms;
  stats.decompressed_bytes = 0u;
  stats.decompression_ms = 0.0;
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    if (!chunk.was_decompressed()) {
      continue;
    }
    stats.decompressed_bytes += chunk.raw_size;
    stats.decompression_ms += chunk.decompression_time_ms();
  }
  return {
      .ok = true,
      .document = std::move(decoded.document),
      .message = {},
      .diagnostics = {},
      .scene_uuid = scene_uuid,
      .minimum_product_release = container.minimum_product_release,
      .project_asset_dependencies =
          std::move(decoded.project_asset_dependencies),
      .viewport_camera_pose = std::move(decoded.viewport_camera_pose),
      .stats = stats,
      .source_byte_count = stats.file_bytes,
      .source_content_hash = source_content_hash,
  };
}

[[nodiscard]] QdrProjectDependencyReadResult
deserialize_owned_project_dependencies(std::vector<std::byte> bytes) {
  QdrProjectDependencyReadResult result;
  result.source_byte_count = bytes.size();
  result.source_content_hash = quader_io::qdr_binary_content_hash(bytes);
  result.stats.file_bytes = bytes.size();
  const auto container_started = std::chrono::steady_clock::now();
  const quader_io::QdrBinaryContainerReadResult container =
      quader_io::read_qdr_binary_container(std::move(bytes));
  result.stats.container_access_and_verification_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - container_started)
          .count();
  if (!container.ok) {
    result.message = container.message;
    result.diagnostics = container.diagnostics;
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(quader_io::qdr_make_error(
          "qdr.binary.read_failed", "", result.message));
    }
    return result;
  }
  result.scene_uuid = container.scene_uuid;
  result.stats.stored_chunk_bytes = container.stored_chunk_bytes;
  result.stats.raw_chunk_bytes = container.raw_chunk_bytes;
  result.stats.compressed_chunk_count = container.compressed_chunk_count;
  QdrSceneBinaryDependencyDecodeResult decoded =
      decode_qdr_scene_binary_dependencies(container);
  result.scene_name = std::move(decoded.scene_name);
  result.material_ids = std::move(decoded.material_ids);
  result.project_asset_dependencies =
      std::move(decoded.project_asset_dependencies);
  result.dependency_data_available = true;
  result.stats.logical_geometry_count = decoded.stats.logical_geometry_count;
  result.stats.metadata_access_ms = decoded.stats.metadata_access_ms;
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    if (chunk.was_decompressed()) {
      result.stats.decompressed_bytes += chunk.raw_size;
      result.stats.decompression_ms += chunk.decompression_time_ms();
    }
  }
  if (!decoded.fully_validated) {
    result.message = std::move(decoded.validation_error);
    result.diagnostics.push_back(quader_io::qdr_make_error(
        "qdr.binary.partial_dependency_read", "", result.message));
    return result;
  }
  result.ok = true;
  return result;
}

[[nodiscard]] QdrProjectMaterialUsageReadResult
deserialize_owned_project_material_usage(std::vector<std::byte> bytes) {
  QdrProjectMaterialUsageReadResult result;
  result.source_byte_count = bytes.size();
  result.source_content_hash = quader_io::qdr_binary_content_hash(bytes);
  result.stats.file_bytes = bytes.size();
  const auto started = std::chrono::steady_clock::now();
  const auto container = quader_io::read_qdr_binary_container(std::move(bytes));
  result.stats.container_access_and_verification_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - started).count();
  if (!container.ok) {
    result.message = container.message;
    result.diagnostics = container.diagnostics;
    return result;
  }
  result.scene_uuid = container.scene_uuid;
  result.stats.stored_chunk_bytes = container.stored_chunk_bytes;
  result.stats.raw_chunk_bytes = container.raw_chunk_bytes;
  result.stats.compressed_chunk_count = container.compressed_chunk_count;
  auto decoded = decode_qdr_scene_binary_material_usage(container);
  result.scene_name = std::move(decoded.scene_name);
  result.material_ids = std::move(decoded.material_ids);
  result.embedded_material_ids = std::move(decoded.embedded_material_ids);
  result.stats.logical_geometry_count = decoded.stats.logical_geometry_count;
  result.stats.unique_geometry_decoded_count = decoded.stats.unique_geometry_decoded_count;
  result.stats.metadata_access_ms = decoded.stats.metadata_access_ms;
  result.stats.geometry_decode_ms = decoded.stats.geometry_decode_ms;
  for (const auto &chunk : container.chunks) {
    if (chunk.was_decompressed()) {
      result.stats.decompressed_bytes += chunk.raw_size;
      result.stats.decompression_ms += chunk.decompression_time_ms();
    }
  }
  result.ok = true;
  result.complete = true;
  return result;
}

} // namespace

QdrProjectReadResult QdrDocumentReader::deserialize_project_document(
    std::span<const std::byte> bytes,
    std::optional<ProductRelease> authoring_release) {
  QDR_PROFILE_FUNCTION();
  try {
    return deserialize_owned_project_document(copy_bytes(bytes), authoring_release);
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_error_result(exception.diagnostic());
  } catch (const std::exception &exception) {
    return project_error_result("qdr.binary.read_failed", exception.what());
  } catch (...) {
    return project_error_result("qdr.binary.unknown_error",
                                "Unknown error reading QDR binary scene.");
  }
}

QdrProjectReadResult QdrDocumentReader::read_project_document(
    const std::filesystem::path &path,
    std::optional<ProductRelease> authoring_release) {
  QDR_PROFILE_FUNCTION();
  const auto byte_read_started = std::chrono::steady_clock::now();
  try {
    // The opened source version is fully copied and closed before hydration;
    // a Godot import must not hold up a subsequent native atomic save.
    quader_io::SharedFileReadResult source =
        quader_io::read_file_bytes_shared(path);
    if (!source.ok) {
      std::string_view code = "qdr.file_read_failed";
      if (source.error == quader_io::SharedFileReadError::Open) {
        code = "qdr.file_open_failed";
      } else if (source.error == quader_io::SharedFileReadError::Size ||
                 source.error == quader_io::SharedFileReadError::Limit) {
        code = "qdr.binary.file_too_large";
      }
      return project_read_result_with_source_path(
          project_error_result(code, "Could not read QDR scene '" +
                                         path.string() + "': " + source.message),
          path);
    }
    const double byte_read_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    byte_read_started)
                                    .count();
    QdrProjectReadResult result =
        deserialize_owned_project_document(std::move(source.bytes),
                                            authoring_release);
    result.stats.byte_read_ms = byte_read_ms;
    return project_read_result_with_source_path(std::move(result), path);
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_read_result_with_source_path(
        project_error_result(exception.diagnostic()), path);
  } catch (const std::exception &exception) {
    return project_read_result_with_source_path(
        project_error_result("qdr.binary.read_failed", exception.what()),
        path);
  } catch (...) {
    return project_read_result_with_source_path(
        project_error_result("qdr.binary.unknown_error",
                             "Unknown error reading QDR binary scene."),
        path);
  }
}

QdrProjectReleaseReadResult QdrDocumentReader::read_project_release_requirement(
    const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  try {
    quader_io::SharedFileReadResult source =
        quader_io::read_file_bytes_shared(path);
    if (!source.ok) {
      return {.message = "Could not read QDR scene '" + path.string() +
                         "': " + source.message};
    }
    const quader_io::QdrBinaryContainerReadResult container =
        quader_io::read_qdr_binary_container(std::move(source.bytes));
    return {.ok = container.ok,
            .minimum_product_release = container.minimum_product_release,
            .message = container.message};
  } catch (const std::exception &exception) {
    return {.message = exception.what()};
  } catch (...) {
    return {.message = "Unknown error reading QDR binary scene requirement."};
  }
}

QdrProjectDependencyReadResult QdrDocumentReader::deserialize_project_dependencies(
    std::span<const std::byte> bytes) {
  QDR_PROFILE_FUNCTION();
  try {
    return deserialize_owned_project_dependencies(copy_bytes(bytes));
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_error_result<QdrProjectDependencyReadResult>(
        exception.diagnostic());
  } catch (const std::exception &exception) {
    return project_error_result<QdrProjectDependencyReadResult>(
        "qdr.binary.read_failed", exception.what());
  } catch (...) {
    return project_error_result<QdrProjectDependencyReadResult>(
        "qdr.binary.unknown_error", "Unknown error reading QDR scene dependencies.");
  }
}

QdrProjectDependencyReadResult QdrDocumentReader::read_project_dependencies(
    const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  const auto byte_read_started = std::chrono::steady_clock::now();
  try {
    quader_io::SharedFileReadResult source =
        quader_io::read_file_bytes_shared(path);
    if (!source.ok) {
      std::string_view code = "qdr.file_read_failed";
      if (source.error == quader_io::SharedFileReadError::Open) {
        code = "qdr.file_open_failed";
      } else if (source.error == quader_io::SharedFileReadError::Size ||
                 source.error == quader_io::SharedFileReadError::Limit) {
        code = "qdr.binary.file_too_large";
      }
      return project_read_result_with_source_path(
          project_error_result<QdrProjectDependencyReadResult>(
              code, "Could not read QDR scene '" + path.string() + "': " +
                        source.message),
          path);
    }
    const double byte_read_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    byte_read_started)
                                    .count();
    QdrProjectDependencyReadResult result =
        deserialize_owned_project_dependencies(std::move(source.bytes));
    result.stats.byte_read_ms = byte_read_ms;
    return project_read_result_with_source_path(std::move(result), path);
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_read_result_with_source_path(
        project_error_result<QdrProjectDependencyReadResult>(
            exception.diagnostic()),
        path);
  } catch (const std::exception &exception) {
    return project_read_result_with_source_path(
        project_error_result<QdrProjectDependencyReadResult>(
            "qdr.binary.read_failed", exception.what()),
        path);
  } catch (...) {
    return project_read_result_with_source_path(
        project_error_result<QdrProjectDependencyReadResult>(
            "qdr.binary.unknown_error",
            "Unknown error reading QDR scene dependencies."),
        path);
  }
}

QdrProjectMaterialUsageReadResult
QdrDocumentReader::deserialize_project_material_usage(
    std::span<const std::byte> bytes) {
  QDR_PROFILE_FUNCTION();
  try {
    return deserialize_owned_project_material_usage(copy_bytes(bytes));
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_error_result<QdrProjectMaterialUsageReadResult>(exception.diagnostic());
  } catch (const std::exception &exception) {
    return project_error_result<QdrProjectMaterialUsageReadResult>(
        "qdr.binary.material_usage_read_failed", exception.what());
  } catch (...) {
    return project_error_result<QdrProjectMaterialUsageReadResult>(
        "qdr.binary.unknown_error", "Unknown error reading QDR material usage.");
  }
}

QdrProjectMaterialUsageReadResult QdrDocumentReader::read_project_material_usage(
    const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  const auto started = std::chrono::steady_clock::now();
  try {
    auto source = quader_io::read_file_bytes_shared(path);
    if (!source.ok) {
      return project_read_result_with_source_path(
          project_error_result<QdrProjectMaterialUsageReadResult>(
              "qdr.file_read_failed", "Could not read QDR scene '" + path.string() +
              "': " + source.message), path);
    }
    const double read_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    auto result = deserialize_owned_project_material_usage(std::move(source.bytes));
    result.stats.byte_read_ms = read_ms;
    return project_read_result_with_source_path(std::move(result), path);
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_read_result_with_source_path(
        project_error_result<QdrProjectMaterialUsageReadResult>(exception.diagnostic()), path);
  } catch (const std::exception &exception) {
    return project_read_result_with_source_path(
        project_error_result<QdrProjectMaterialUsageReadResult>(
            "qdr.binary.material_usage_read_failed", exception.what()), path);
  } catch (...) {
    return project_read_result_with_source_path(
        project_error_result<QdrProjectMaterialUsageReadResult>(
            "qdr.binary.unknown_error", "Unknown error reading QDR material usage."), path);
  }
}

QdrMeshMaterialSlotsReadResult QdrDocumentReader::deserialize_mesh_material_slots(
    std::span<const std::byte> bytes, std::uint16_t scene_schema_version) {
  try {
    return {.ok = true, .material_slots =
        decode_qdr_geometry_material_slots(bytes, scene_schema_version)};
  } catch (const std::exception &exception) {
    return {.message = exception.what()};
  } catch (...) {
    return {.message = "Unknown error reading QDR mesh material slots."};
  }
}

} // namespace quader
