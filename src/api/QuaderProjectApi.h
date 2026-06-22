#pragma once

#include "api/QuaderEditorApiTypes.h"

namespace quader {

class ProjectSession;

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
