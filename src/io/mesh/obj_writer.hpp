////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace quader {

/**
 * Stores the OBJ mesh write result used by the mesh I/O layer.
 */
struct ObjMeshWriteResult {
  bool ok = false;
  std::string message;
};

/**
 * Describes one destination in an atomic OBJ mesh write batch.
 */
struct ObjMeshWriteRequest {
  std::filesystem::path path;
  const MeshAsset *mesh = nullptr;
  std::string object_name;
};

/**
 * Owns Wavefront OBJ writing for exported mesh geometry.
 */
class ObjMeshWriter final {
public:
  [[nodiscard]] static ObjMeshWriteResult validate_mesh(const MeshAsset &mesh);
  [[nodiscard]] static std::string serialize_mesh(const MeshAsset &mesh,
                                                  std::string_view object_name);
  [[nodiscard]] static ObjMeshWriteResult
  write_mesh(const std::filesystem::path &path, const MeshAsset &mesh,
             std::string_view object_name);
  [[nodiscard]] static ObjMeshWriteResult
  write_meshes_atomic(std::span<const ObjMeshWriteRequest> requests);
};

} // namespace quader
