////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session.hpp"
#include "api/quader_editor_api_types.hpp"

namespace quader {

/**
 * Exposes Quader Project API operations through the public Quader API surface.
 */
class QuaderProjectApi final {
public:
    explicit QuaderProjectApi(QuaderApiSession session);

    QuaderApiVoidResult newProject();
    QuaderApiVoidResult open(const std::filesystem::path& path);
    QuaderApiVoidResult save();
    QuaderApiVoidResult saveAs(const std::filesystem::path& path);
    QuaderApiVoidResult close();
    void setRuntimeHooks(QuaderProjectRuntimeHooks hooks);

    [[nodiscard]] bool dirty() const;
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] QuaderProjectMetadata metadata() const;

private:
    [[nodiscard]] QuaderApiVoidResult unsupported(std::string message) const;
    [[nodiscard]] QuaderApiVoidResult saveToPath(const std::filesystem::path& path, bool updatePath);
    [[nodiscard]] std::optional<std::filesystem::path> chooseOpenPath() const;
    [[nodiscard]] std::optional<std::filesystem::path> chooseSaveAsPath() const;

    QuaderApiSession session_;
    std::filesystem::path path_;
    QuaderProjectRuntimeHooks runtimeHooks_;
};

} // namespace quader
