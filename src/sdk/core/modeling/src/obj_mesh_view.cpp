////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/io/obj_mesh_view.hpp>

#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace quader::modeling {
namespace {

[[nodiscard]] std::string sanitized_obj_name(std::string_view name) {
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

[[nodiscard]] Result<std::string> validate_mesh(const MeshPayload &mesh) {
  if (mesh.vertices.empty()) {
    return Result<std::string>::failure(
        make_error(ErrorCode::InvalidArgument, "OBJ mesh has no vertices."));
  }
  if (mesh.indices.empty()) {
    return Result<std::string>::failure(
        make_error(ErrorCode::InvalidArgument, "OBJ mesh has no indices."));
  }
  if ((mesh.indices.size() % 3U) != 0U) {
    return Result<std::string>::failure(make_error(
        ErrorCode::InvalidArgument, "OBJ mesh indices are not triangle-aligned."));
  }
  for (const std::uint32_t index : mesh.indices) {
    if (index >= mesh.vertices.size()) {
      return Result<std::string>::failure(make_error(
          ErrorCode::InvalidArgument, "OBJ mesh index is out of bounds."));
    }
  }
  return Result<std::string>::success({});
}

} // namespace

Result<std::string> serialize_obj_mesh(const MeshPayload &mesh,
                                       std::string_view name) {
  Result<std::string> validation = validate_mesh(mesh);
  if (!validation.ok()) {
    return validation;
  }

  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(9);
  stream << "# Quader Modeling SDK OBJ export\n";
  stream << "o " << sanitized_obj_name(name) << '\n';

  for (const MeshVertexPayload &vertex : mesh.vertices) {
    stream << "v " << vertex.position.x << ' ' << vertex.position.y << ' '
           << vertex.position.z << '\n';
  }
  for (const MeshVertexPayload &vertex : mesh.vertices) {
    stream << "vt " << vertex.uv0.x << ' ' << vertex.uv0.y << '\n';
  }
  for (const MeshVertexPayload &vertex : mesh.vertices) {
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

  return Result<std::string>::success(stream.str());
}

} // namespace quader::modeling
