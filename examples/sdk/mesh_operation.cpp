////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/modeling.hpp>

int main() {
  using namespace quader::modeling;

  Result<PolygonDocument> document_result =
      PolygonDocument::make_box({.min = {-1.0F, -1.0F, -1.0F},
                                 .max = {1.0F, 1.0F, 1.0F}});
  if (!document_result.ok()) {
    return 1;
  }

  PolygonDocument document = std::move(document_result).value();
  if (!document.select_all_faces().ok()) {
    return 2;
  }
  if (!document.translate_selection({1.0F, 0.0F, 0.0F}).ok()) {
    return 3;
  }

  Result<MeshPayload> mesh = document.compile_mesh();
  return mesh.ok() && !mesh.value().vertices.empty() ? 0 : 4;
}
