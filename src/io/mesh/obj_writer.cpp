////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/mesh/obj_writer.hpp"

#include "diagnostics/profile.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

std::string sanitized_obj_name(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  bool previous_separator = false;
  for (const char character : name) {
    const bool safe =
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_' ||
        character == '-' || character == '.';
    if (safe) {
      result.push_back(character);
      previous_separator = false;
    } else if (!previous_separator) {
      result.push_back('_');
      previous_separator = true;
    }
  }
  while (!result.empty() && result.back() == '_') {
    result.pop_back();
  }
  while (!result.empty() && result.front() == '_') {
    result.erase(result.begin());
  }
  return result.empty() ? "Mesh" : result;
}

std::filesystem::path target_directory(const std::filesystem::path &path) {
  const std::filesystem::path directory = path.parent_path();
  return directory.empty() ? std::filesystem::path(".") : directory;
}

ObjMeshWriteResult write_text_file(const std::filesystem::path &path,
                                   const std::string &text) {
  if (path.empty() || path.filename().empty()) {
    return {false, "OBJ export failed: file path is empty."};
  }

  std::error_code error;
  const std::filesystem::path directory = target_directory(path);
  if (!std::filesystem::is_directory(directory, error)) {
    return {false,
            "OBJ export failed: output directory does not exist: " +
                directory.string()};
  }

  std::ofstream stream(path, std::ios::out | std::ios::binary |
                                 std::ios::trunc);
  if (!stream.is_open()) {
    return {false, "OBJ export failed: could not create file: " +
                       path.string()};
  }

  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream.good()) {
    return {false, "OBJ export failed: could not write file: " +
                       path.string()};
  }

  stream.flush();
  if (!stream.good()) {
    return {false, "OBJ export failed: could not flush file: " +
                       path.string()};
  }

  stream.close();
  if (!stream.good()) {
    return {false, "OBJ export failed: could not close file: " +
                       path.string()};
  }

  return {true, "OBJ mesh exported."};
}

std::string path_collision_key(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (error) {
    absolute = path;
  }
  std::string key = absolute.lexically_normal().generic_string();
#if defined(_WIN32)
  std::transform(key.begin(), key.end(), key.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
#endif
  return key;
}

std::filesystem::path unique_transaction_path(
    const std::filesystem::path &target, std::string_view role) {
  static std::atomic<std::uint64_t> next_id{1U};
  const std::filesystem::path directory = target_directory(target);
  const std::string filename = target.filename().string();
  for (std::uint32_t attempt = 0; attempt < 1024U; ++attempt) {
    const std::uint64_t id = next_id.fetch_add(1U);
    const std::filesystem::path candidate =
        directory /
        ("." + filename + ".quader-obj-" + std::string(role) + "-" +
         std::to_string(id));
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) && !error) {
      return candidate;
    }
  }
  return {};
}

void remove_if_present(const std::filesystem::path &path) {
  if (path.empty()) {
    return;
  }
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

/**
 * Tracks one staged destination throughout an atomic OBJ batch write.
 */
struct StagedObjWrite {
  std::filesystem::path target;
  std::filesystem::path temporary;
  std::filesystem::path backup;
  bool target_existed = false;
  bool backup_created = false;
  bool installed = false;
};

void clean_transaction_files(std::span<StagedObjWrite> writes) {
  for (StagedObjWrite &write : writes) {
    remove_if_present(write.temporary);
    remove_if_present(write.backup);
  }
}

bool rollback_installed_writes(std::span<StagedObjWrite> writes) {
  bool restored = true;
  for (auto iterator = writes.rbegin(); iterator != writes.rend();
       ++iterator) {
    StagedObjWrite &write = *iterator;
    if (write.installed) {
      std::error_code remove_error;
      std::filesystem::remove(write.target, remove_error);
      restored = restored && !remove_error;
      write.installed = false;
    }
    if (write.backup_created) {
      std::error_code restore_error;
      std::filesystem::rename(write.backup, write.target, restore_error);
      restored = restored && !restore_error;
      if (!restore_error) {
        write.backup_created = false;
      }
    }
    remove_if_present(write.temporary);
  }
  return restored;
}

} // namespace

ObjMeshWriteResult ObjMeshWriter::validate_mesh(const MeshAsset &mesh) {
  if (mesh.vertices.empty()) {
    return {false, "OBJ export failed: mesh has no vertices."};
  }
  if (mesh.indices.empty()) {
    return {false, "OBJ export failed: mesh has no indices."};
  }
  if ((mesh.indices.size() % 3U) != 0U) {
    return {false,
            "OBJ export failed: mesh indices are not triangle-aligned."};
  }
  for (const MeshVertex &vertex : mesh.vertices) {
    if (!std::isfinite(vertex.position.x) ||
        !std::isfinite(vertex.position.y) ||
        !std::isfinite(vertex.position.z) ||
        !std::isfinite(vertex.normal.x) ||
        !std::isfinite(vertex.normal.y) ||
        !std::isfinite(vertex.normal.z) || !std::isfinite(vertex.uv.x) ||
        !std::isfinite(vertex.uv.y)) {
      return {false, "OBJ export failed: mesh vertex data is not finite."};
    }
  }
  for (const std::uint32_t index : mesh.indices) {
    if (index >= mesh.vertices.size()) {
      return {false, "OBJ export failed: mesh index is out of bounds."};
    }
  }
  return {true, {}};
}

std::string ObjMeshWriter::serialize_mesh(const MeshAsset &mesh,
                                          std::string_view object_name) {
  QDR_PROFILE_FUNCTION();
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(9);
  stream << "# Quader OBJ export\n";
  stream << "o " << sanitized_obj_name(object_name) << '\n';

  for (const MeshVertex &vertex : mesh.vertices) {
    stream << "v " << vertex.position.x << ' ' << vertex.position.y << ' '
           << vertex.position.z << '\n';
  }
  for (const MeshVertex &vertex : mesh.vertices) {
    stream << "vt " << vertex.uv.x << ' ' << vertex.uv.y << '\n';
  }
  for (const MeshVertex &vertex : mesh.vertices) {
    stream << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' '
           << vertex.normal.z << '\n';
  }

  for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
    const std::uint32_t a = mesh.indices[index] + 1U;
    const std::uint32_t b = mesh.indices[index + 1U] + 1U;
    const std::uint32_t c = mesh.indices[index + 2U] + 1U;
    stream << "f " << a << '/' << a << '/' << a << ' ' << b << '/' << b
           << '/' << b << ' ' << c << '/' << c << '/' << c << '\n';
  }

  return stream.str();
}

ObjMeshWriteResult
ObjMeshWriter::write_mesh(const std::filesystem::path &path,
                          const MeshAsset &mesh,
                          std::string_view object_name) {
  QDR_PROFILE_FUNCTION();
  const ObjMeshWriteRequest request{
      .path = path,
      .mesh = &mesh,
      .object_name = std::string(object_name),
  };
  return write_meshes_atomic(std::span<const ObjMeshWriteRequest>(&request,
                                                                  1U));
}

ObjMeshWriteResult ObjMeshWriter::write_meshes_atomic(
    std::span<const ObjMeshWriteRequest> requests) {
  QDR_PROFILE_FUNCTION();
  if (requests.empty()) {
    return {false, "OBJ export failed: no meshes were provided."};
  }

  std::unordered_set<std::string> targets;
  std::vector<StagedObjWrite> writes;
  writes.reserve(requests.size());
  for (const ObjMeshWriteRequest &request : requests) {
    if (request.mesh == nullptr) {
      return {false, "OBJ export failed: mesh is missing."};
    }
    const ObjMeshWriteResult validation = validate_mesh(*request.mesh);
    if (!validation.ok) {
      return validation;
    }
    if (request.path.empty() || request.path.filename().empty()) {
      return {false, "OBJ export failed: file path is empty."};
    }

    std::error_code error;
    const std::filesystem::path directory = target_directory(request.path);
    if (!std::filesystem::is_directory(directory, error) || error) {
      return {false,
              "OBJ export failed: output directory does not exist: " +
                  directory.string()};
    }
    error.clear();
    const bool target_exists = std::filesystem::exists(request.path, error);
    if (error) {
      return {false, "OBJ export failed: could not inspect file: " +
                         request.path.string()};
    }
    if (target_exists) {
      error.clear();
      if (!std::filesystem::is_regular_file(request.path, error) || error) {
        return {false, "OBJ export failed: target is not a regular file: " +
                           request.path.string()};
      }
    }
    if (!targets.insert(path_collision_key(request.path)).second) {
      return {false, "OBJ export failed: duplicate output path: " +
                         request.path.string()};
    }
    writes.push_back(StagedObjWrite{
        .target = request.path,
        .target_existed = target_exists,
    });
  }

  for (std::size_t index = 0; index < requests.size(); ++index) {
    StagedObjWrite &write = writes[index];
    write.temporary = unique_transaction_path(write.target, "tmp");
    if (write.temporary.empty()) {
      clean_transaction_files(writes);
      return {false, "OBJ export failed: could not reserve temporary file."};
    }
    const ObjMeshWriteResult staged = write_text_file(
        write.temporary,
        serialize_mesh(*requests[index].mesh, requests[index].object_name));
    if (!staged.ok) {
      clean_transaction_files(writes);
      return staged;
    }
  }

  for (StagedObjWrite &write : writes) {
    if (write.target_existed) {
      write.backup = unique_transaction_path(write.target, "backup");
      if (write.backup.empty()) {
        const bool restored = rollback_installed_writes(writes);
        return {false,
                restored
                    ? "OBJ export failed: could not reserve backup file."
                    : "OBJ export failed and existing files could not be fully restored."};
      }
      std::error_code backup_error;
      std::filesystem::rename(write.target, write.backup, backup_error);
      if (backup_error) {
        const bool restored = rollback_installed_writes(writes);
        return {false,
                restored
                    ? "OBJ export failed: could not preserve existing file: " +
                          write.target.string()
                    : "OBJ export failed and existing files could not be fully restored."};
      }
      write.backup_created = true;
    }

    std::error_code install_error;
    std::filesystem::rename(write.temporary, write.target, install_error);
    if (install_error) {
      const bool restored = rollback_installed_writes(writes);
      return {false,
              restored
                  ? "OBJ export failed: could not replace file: " +
                        write.target.string()
                  : "OBJ export failed and existing files could not be fully restored."};
    }
    write.temporary.clear();
    write.installed = true;
  }

  clean_transaction_files(writes);
  return {true, requests.size() == 1U ? "OBJ mesh exported."
                                      : "OBJ meshes exported."};
}

} // namespace quader
