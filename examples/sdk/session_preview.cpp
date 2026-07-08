////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/modeling.hpp>

#include <vector>

int main() {
  using namespace quader::modeling;

  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  if (!document_result.ok()) {
    return 1;
  }

  ModelingSession session;
  Result<ObjectId> object_id =
      session.add_document(std::move(document_result).value(), "Box");
  if (!object_id.ok()) {
    return 2;
  }

  const PolygonDocument *document = session.document(object_id.value());
  if (document == nullptr) {
    return 3;
  }
  const std::vector<FaceId> faces = document->face_ids();
  if (faces.empty() || !session.select_face(object_id.value(), faces.front()).ok()) {
    return 4;
  }

  if (!session.begin_translate_preview(object_id.value(),
                                       {0.0F, 1.0F, 0.0F})
           .ok()) {
    return 5;
  }

  Result<ToolPreviewPayload> payload = session.preview_payload();
  if (!payload.ok() || !payload.value().valid) {
    return 6;
  }

  return session.commit_preview().ok() ? 0 : 7;
}
