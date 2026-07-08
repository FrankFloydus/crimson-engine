////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/modeling.hpp>

int main() {
  using namespace quader::modeling;

  ModelingApi quader = ModelingApi::create();
  MeshHandle box = quader.create_box({.name = "Preview Box"});
  if (!box.valid()) {
    return 1;
  }

  OperationReceipt selected = box.faces().all().replace();
  if (!selected.success) {
    return 2;
  }

  TransformTool move = quader.tools().activate_move_tool();
  OperationReceipt begin = move.begin({0.0F, 1.0F, 0.0F});
  if (!begin.success) {
    return 3;
  }

  if (!quader.preview().active()) {
    return 4;
  }

  OperationReceipt update = move.update({0.0F, 2.0F, 0.0F});
  if (!update.success) {
    return 5;
  }

  OperationReceipt committed = move.commit("Move Preview Box");
  if (!committed.success) {
    return 6;
  }

  MeshPayload mesh = box.payloads().compile_mesh();
  return mesh.vertices.empty() ? 7 : 0;
}
