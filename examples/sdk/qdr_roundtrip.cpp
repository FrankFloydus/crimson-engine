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

  Result<PolygonDocument> document_result = PolygonDocument::make_box({});
  if (!document_result.ok()) {
    return 1;
  }

  PolygonDocument document = std::move(document_result).value();
  Result<std::string> serialized = serialize_single_document_as_qdr(document);
  if (!serialized.ok()) {
    return 2;
  }

  Result<QdrDocumentDto> round_trip =
      deserialize_qdr_document(serialized.value());
  if (!round_trip.ok() || round_trip.value().objects.size() != 1U) {
    return 3;
  }

  return round_trip.value().objects.front().document.face_count() == 6U ? 0 : 4;
}
