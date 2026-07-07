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

    QuaderApiVoidResult new_project();
    QuaderApiVoidResult open(const std::filesystem::path& path);
    QuaderApiVoidResult save();
    QuaderApiVoidResult save_as(const std::filesystem::path &path);
    QuaderApiResult<QuaderObjExportResult>
    export_selected_meshes_as_obj(const std::filesystem::path &path);
    QuaderApiVoidResult close();
    void set_runtime_hooks(QuaderProjectRuntimeHooks hooks);

    [[nodiscard]] bool can_export_selected_meshes_as_obj() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] QuaderProjectMetadata metadata() const;

private:
    [[nodiscard]] QuaderApiVoidResult unsupported(std::string message) const;
    [[nodiscard]] QuaderApiVoidResult
    save_to_path(const std::filesystem::path &path, bool update_path);
    [[nodiscard]] std::optional<std::filesystem::path> choose_open_path() const;
    [[nodiscard]] std::optional<std::filesystem::path>
    choose_save_as_path() const;
    [[nodiscard]] std::optional<std::filesystem::path>
    choose_export_obj_path() const;

    QuaderApiSession session_;
    std::filesystem::path path_;
    QuaderProjectRuntimeHooks runtime_hooks_;
};

} // namespace quader
