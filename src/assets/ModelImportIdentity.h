#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <span>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace crimson {
struct ModelImportFileStamp {
    std::filesystem::path file;
    std::uintmax_t bytes{};
    std::filesystem::file_time_type modified;
};
struct ModelImportIdentity {
    std::string digest;
    std::vector<ModelImportFileStamp> files;
};
struct MatchingModelImport {
    std::filesystem::path directory;
    std::string name;
    std::vector<ModelImportFileStamp> files;
};
// Worker-only SHA-256 over original document, normalized document, buffers and
// images. Every path is resolved inside this managed package; source URIs are
// provenance only and are never followed while inspecting an existing import.
ModelImportIdentity identifyModelImport(const std::filesystem::path& directory);
std::string modelImportDataDigest(std::span<const std::uint8_t> bytes);
std::optional<MatchingModelImport> findMatchingModelImport(const std::filesystem::path& projectRoot,
    const std::string& digest,const std::unordered_set<std::string>& reservedIds);
std::unordered_set<std::string> reservedModelImportIds(const std::filesystem::path& projectRoot);
bool modelImportFilesUnchanged(const std::vector<ModelImportFileStamp>& files);
// Lightweight publication check. Packages written before identity metadata can
// instead use the worker-verified file stamps stored by findMatchingModelImport.
bool modelImportManifestMatches(const std::filesystem::path& directory,const std::string& digest);
bool modelImportIdentityReserved(const std::filesystem::path& projectRoot,const std::filesystem::path& directory,
    const std::function<bool(const std::string&)>& reservedId);
}
