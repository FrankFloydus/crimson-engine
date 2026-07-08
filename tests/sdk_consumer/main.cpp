////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/modeling.hpp>

#include <string>

int main() {
  using namespace quader::modeling;

  Result<PolygonDocument> document = PolygonDocument::make_box({});
  if (!document.ok()) {
    return 1;
  }
  PolygonDocument box = std::move(document).value();
  if (!box.select_all_faces().ok()) {
    return 2;
  }
  if (!box.translate_selection({1.0F, 0.0F, 0.0F}).ok()) {
    return 3;
  }
  Result<MeshPayload> mesh = box.compile_mesh();
  if (!mesh.ok() || mesh.value().indices.empty()) {
    return 4;
  }
  Result<std::string> qdr = serialize_single_document_as_qdr(box);
  if (!qdr.ok()) {
    return 5;
  }
  Result<QdrDocumentDto> round_trip = deserialize_qdr_document(qdr.value());
  if (!round_trip.ok() || round_trip.value().objects.empty()) {
    return 6;
  }
  ModelingSession session;
  Result<ObjectId> object_id = session.add_document(std::move(box), "Box");
  if (!object_id.ok()) {
    return 7;
  }
  const PolygonDocument *session_document = session.document(object_id.value());
  if (session_document == nullptr || session_document->face_ids().empty()) {
    return 8;
  }
  if (!session.select_face(object_id.value(),
                           session_document->face_ids().front())
           .ok()) {
    return 9;
  }
  if (!session.begin_translate_preview(object_id.value(),
                                       {0.0F, 1.0F, 0.0F})
           .ok()) {
    return 10;
  }
  Result<ToolPreviewPayload> preview = session.preview_payload();
  if (!preview.ok() || !preview.value().valid) {
    return 11;
  }
  if (!session.commit_preview().ok()) {
    return 12;
  }
  return 0;
}
