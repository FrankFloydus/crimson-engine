////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/io/qdr_document.hpp>

#include "polygon_document_native.hpp"

#include <io/qdr/qdr_document.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace quader::modeling {
namespace {

[[nodiscard]] quader_io::QdrSelectionMode
to_native_selection_mode(QdrSelectionMode mode) {
  switch (mode) {
  case QdrSelectionMode::Object:
    return quader_io::QdrSelectionMode::Object;
  case QdrSelectionMode::Vertex:
    return quader_io::QdrSelectionMode::Vertex;
  case QdrSelectionMode::Edge:
    return quader_io::QdrSelectionMode::Edge;
  case QdrSelectionMode::Face:
    return quader_io::QdrSelectionMode::Face;
  }
  return quader_io::QdrSelectionMode::Object;
}

[[nodiscard]] QdrSelectionMode
from_native_selection_mode(quader_io::QdrSelectionMode mode) {
  switch (mode) {
  case quader_io::QdrSelectionMode::Object:
    return QdrSelectionMode::Object;
  case quader_io::QdrSelectionMode::Vertex:
    return QdrSelectionMode::Vertex;
  case quader_io::QdrSelectionMode::Edge:
    return QdrSelectionMode::Edge;
  case quader_io::QdrSelectionMode::Face:
    return QdrSelectionMode::Face;
  }
  return QdrSelectionMode::Object;
}

[[nodiscard]] int native_id(ObjectId id, int fallback) {
  return id.valid() ? static_cast<int>(id.index) : fallback;
}

[[nodiscard]] int native_id(MaterialId id) {
  return id.valid() ? static_cast<int>(id.index) : 0;
}

[[nodiscard]] ObjectId object_id(int id) {
  return id > 0 ? make_id<ObjectTag>(static_cast<std::uint32_t>(id))
                : ObjectId{};
}

[[nodiscard]] MaterialId material_id(int id) {
  return id > 0 ? make_id<MaterialTag>(static_cast<std::uint32_t>(id))
                : MaterialId{};
}

[[nodiscard]] quader_io::QdrDocument to_native(const QdrDocumentDto &dto) {
  quader_io::QdrDocument document;
  document.selection_mode = to_native_selection_mode(dto.selection_mode);
  document.content_revision = dto.content_revision;
  document.selection_revision = dto.selection_revision;
  document.active_object_id = native_id(dto.active_object, -1);

  int next_id = 1;
  for (const QdrObjectDto &object : dto.objects) {
    const int id = native_id(object.id, next_id);
    next_id = std::max(next_id, id + 1);
    document.objects.push_back({
        .id = id,
        .material_id = native_id(object.material),
        .selected = object.selected,
        .document = PolygonDocumentNativeAccess::document(object.document),
        .selection = PolygonDocumentNativeAccess::selection(object.document),
    });
  }
  document.next_object_id = std::max(next_id, 1);
  if (document.active_object_id < 0 && !document.objects.empty()) {
    document.active_object_id = document.objects.front().id;
  }
  return document;
}

[[nodiscard]] QdrDocumentDto from_native(quader_io::QdrDocument native) {
  QdrDocumentDto dto;
  dto.selection_mode = from_native_selection_mode(native.selection_mode);
  dto.content_revision = native.content_revision;
  dto.selection_revision = native.selection_revision;
  dto.active_object = object_id(native.active_object_id);
  dto.objects.reserve(native.objects.size());
  for (quader_io::QdrObject &object : native.objects) {
    dto.objects.push_back({
        .id = object_id(object.id),
        .material = material_id(object.material_id),
        .selected = object.selected,
        .name = "Object " + std::to_string(object.id),
        .document = PolygonDocumentNativeAccess::from_native(
            std::move(object.document), std::move(object.selection)),
    });
  }
  return dto;
}

[[nodiscard]] Error read_error(const quader_io::QdrReadResult &result) {
  Error error = make_error(ErrorCode::IoError, result.message);
  error.diagnostics.reserve(result.diagnostics.size());
  for (const quader_io::QdrDiagnostic &diagnostic : result.diagnostics) {
    error.diagnostics.push_back({
        .code = diagnostic.code,
        .severity = diagnostic.severity == quader_io::QdrDiagnosticSeverity::Error
                        ? DiagnosticSeverity::Error
                        : diagnostic.severity == quader_io::QdrDiagnosticSeverity::Warning
                              ? DiagnosticSeverity::Warning
                              : DiagnosticSeverity::Info,
        .path = diagnostic.path,
        .message = diagnostic.message,
    });
  }
  return error;
}

} // namespace

Result<std::string> serialize_qdr_document(const QdrDocumentDto &document) {
  if (document.objects.empty()) {
    return Result<std::string>::failure(
        make_error(ErrorCode::InvalidArgument,
                   "QDR document must contain at least one object."));
  }
  return Result<std::string>::success(
      quader_io::serialize_document(to_native(document)));
}

Result<QdrDocumentDto> deserialize_qdr_document(std::string_view text) {
  quader_io::QdrReadResult result = quader_io::deserialize_document(text);
  if (!result.ok) {
    return Result<QdrDocumentDto>::failure(read_error(result));
  }
  return Result<QdrDocumentDto>::success(from_native(std::move(result.document)));
}

Result<std::string>
serialize_single_document_as_qdr(const PolygonDocument &document) {
  QdrDocumentDto dto;
  dto.objects.push_back({
      .id = make_id<ObjectTag>(1),
      .selected = true,
      .name = "Mesh",
      .document = document,
  });
  dto.active_object = make_id<ObjectTag>(1);
  dto.selection_mode = QdrSelectionMode::Object;
  dto.content_revision = document.content_revision();
  dto.selection_revision = document.selection_revision();
  return serialize_qdr_document(dto);
}

} // namespace quader::modeling
