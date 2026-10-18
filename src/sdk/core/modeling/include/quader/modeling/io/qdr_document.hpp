////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace quader::modeling {

/**
 * Enumerates portable QDR selection modes.
 */
enum class QdrSelectionMode {
  Object,
  Vertex,
  Edge,
  Face,
};

/**
 * Stores one portable QDR object with SDK-owned polygon document state.
 */
struct QdrObjectDto {
  ObjectId id{};
  MaterialId material{};
  bool selected = false;
  std::string name;
  PolygonDocument document;
};

/**
 * Stores one portable QDR material reference.
 */
struct QdrMaterialDto {
  MaterialId id{};
  std::string name;
};

/**
 * Stores the portable QDR document DTO exposed by the SDK.
 */
struct QdrDocumentDto {
  std::vector<QdrObjectDto> objects;
  std::vector<QdrMaterialDto> materials;
  ObjectId active_object{};
  QdrSelectionMode selection_mode = QdrSelectionMode::Object;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
};

/**
 * Encodes only the durable authored topology of a polygon document.
 *
 * Selection and runtime revision state are intentionally excluded so the
 * resulting payload can be used as a stable, deduplicated geometry blob.
 * Authored records, including malformed loops and attributes, are retained
 * without cleanup. IO checks binary bounds and representability; modeling
 * validation and safe derived rendering remain separate owner concerns.
 */
[[nodiscard]] Result<std::vector<std::byte>>
serialize_qdr_authored_topology(const PolygonDocument &document);

/**
 * Decodes one authored-topology payload into a polygon document with cleared
 * selection and baseline runtime revisions.
 */
[[nodiscard]] Result<PolygonDocument>
deserialize_qdr_authored_topology(std::span<const std::byte> bytes);

[[nodiscard]] Result<std::vector<std::byte>>
serialize_qdr_document(const QdrDocumentDto &document);
[[nodiscard]] Result<QdrDocumentDto>
deserialize_qdr_document(std::span<const std::byte> bytes);
[[nodiscard]] Result<std::vector<std::byte>>
serialize_single_document_as_qdr(const PolygonDocument &document);

} // namespace quader::modeling
