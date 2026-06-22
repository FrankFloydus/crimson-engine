////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/document/ProjectDocument.h"

#include <filesystem>
#include <string>

namespace quader {

/**
 * Stores the QDR Project Write Result data contract used by the QDR document persistence layer.
 */
struct QdrProjectWriteResult {
    bool ok = false;
    std::string message;
};

/**
 * Owns QDR Document Writer behavior within the QDR document persistence layer.
 */
class QdrDocumentWriter final {
public:
    [[nodiscard]] static std::string serializeProjectDocument(const ProjectDocument& document);
    [[nodiscard]] static QdrProjectWriteResult writeProjectDocumentAtomic(
            const std::filesystem::path& path,
            const ProjectDocument& document);
};

} // namespace quader
