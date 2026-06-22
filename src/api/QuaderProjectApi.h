////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class ProjectSession;

/**
 * Exposes Quader Project API operations through the public Quader API surface.
 */
class QuaderProjectApi final {
public:
    explicit QuaderProjectApi(ProjectSession& session);

    QuaderApiVoidResult newProject();
    QuaderApiVoidResult open(const std::filesystem::path& path);
    QuaderApiVoidResult save();
    QuaderApiVoidResult saveAs(const std::filesystem::path& path);
    QuaderApiVoidResult close();

    [[nodiscard]] bool dirty() const;
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] QuaderProjectMetadata metadata() const;

private:
    [[nodiscard]] QuaderApiVoidResult unsupported(std::string message) const;

    ProjectSession& session_;
    std::filesystem::path path_;
    std::uint64_t savedRevision_ = 0;
};

} // namespace quader
