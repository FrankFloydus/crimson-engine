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

  ModelingApi quader = ModelingApi::create();
  MeshHandle box = quader.create_box({.name = "Roundtrip Box"});
  if (!box.valid()) {
    return 1;
  }

  std::string serialized = quader.io().serialize_qdr(box.id());
  if (serialized.empty()) {
    return 2;
  }

  MeshHandle imported =
      quader.io().deserialize_qdr_object(serialized, "Imported Box");
  if (!imported.valid()) {
    return 3;
  }

  return imported.summary().name == "Imported Box" ? 0 : 4;
}
