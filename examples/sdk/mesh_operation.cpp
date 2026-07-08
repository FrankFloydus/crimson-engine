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
  MeshHandle box = quader.create_box({
      .name = "Example Box",
      .min = {-1.0F, -1.0F, -1.0F},
      .max = {1.0F, 1.0F, 1.0F},
  });
  if (!box.valid()) {
    return 1;
  }

  OperationReceipt moved = box.transform().translate({1.0F, 0.0F, 0.0F});
  if (!moved.success) {
    return 2;
  }

  MeshPayload mesh = box.payloads().compile_mesh();
  return !mesh.vertices.empty() ? 0 : 3;
}
