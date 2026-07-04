////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_diagnostic.hpp"
#include "scene/document/project_document.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Stores the QDR Project Read Result data contract used by the QDR document persistence layer.
 */
struct QdrProjectReadResult {
    bool ok = false;
    ProjectDocument document;
    std::string message;
    bool migrated_legacy_session = false;
    std::vector<quader_io::QdrDiagnostic> diagnostics;
};

/**
 * Owns QDR Document Reader behavior within the QDR document persistence layer.
 */
class QdrDocumentReader final {
public:
  [[nodiscard]] static QdrProjectReadResult
  deserialize_project_document(std::string_view text);
  [[nodiscard]] static QdrProjectReadResult
  read_project_document(const std::filesystem::path &path);
};

} // namespace quader
