////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/mesh/obj_writer.hpp"

#include "diagnostics/profile.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>

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
  const ObjMeshWriteResult validation = validate_mesh(mesh);
  if (!validation.ok) {
    return validation;
  }
  return write_text_file(path, serialize_mesh(mesh, object_name));
}

} // namespace quader
