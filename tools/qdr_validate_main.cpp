////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_reader.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_diagnostics(const quader::QdrProjectReadResult &result) {
  for (const quader_io::QdrDiagnostic &diagnostic : result.diagnostics) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
  if (result.diagnostics.empty() && !result.message.empty()) {
    std::cerr << result.message << '\n';
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: qdr_validate <scene.qdr> [scene.qdr ...]\n";
    return 2;
  }

  bool all_valid = true;
  for (int index = 1; index < argc; ++index) {
    const std::filesystem::path path(argv[index]);
    quader::QdrProjectReadResult result =
        quader::QdrDocumentReader::read_project_document(path);
    if (!result.ok) {
      all_valid = false;
      std::cerr << path.string() << ": invalid\n";
      print_diagnostics(result);
      continue;
    }
    std::cout << path.string() << ": valid QDRB scene \""
              << result.document.name() << "\" ("
              << result.document.scene().entities().size()
              << " entities)\n";
  }
  return all_valid ? 0 : 1;
}
