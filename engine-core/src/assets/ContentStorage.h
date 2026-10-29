#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {

// Asset IDs are permanent, opaque references. These functions translate them to
// files anywhere beneath the chosen project folder, or private metadata.
struct StoredAssetPath { std::string id; std::filesystem::path file; };
struct ContentPathMove {
    std::filesystem::path from, to;
    // Retained import inputs stay inside a model package when an extracted image
    // is first placed beside that package. Its asset identity follows the copy.
    bool copy{};
};

// Recover interrupted current-format moves and prepare private builtin data.
// Call before constructing asset stores, document scanners, or file watchers.
void initializeContentStorage(const std::filesystem::path& root);
bool contentStorageReady(const std::filesystem::path& root);
// Explicit watcher/startup boundary. Warm lookups use the validated snapshot
// without probing the filesystem. Equal-content notifications keep its revision.
// Invalid external locations block lookups until a successful refresh.
bool refreshContentStorage(const std::filesystem::path& root);
std::filesystem::path resolveAssetPath(const std::filesystem::path& root, std::string_view id);
// Returns an indexed ID, or a stable discovery ID for a project-relative file.
std::string assetIdAtPath(const std::filesystem::path& root, const std::filesystem::path& file);
// Package/member source path for import reconstruction only; never publish
// this string as an AssetRef. References and index keys are UUIDs.
std::string legacyAssetId(const std::filesystem::path& root, std::string_view id);
std::vector<StoredAssetPath> storedAssetPaths(const std::filesystem::path& root);
// Validate a complete registration group before publishing one index update.
// An unchanged group does not write the index or advance its revision.
void registerAssetPaths(const std::filesystem::path& root, std::span<const StoredAssetPath> paths);
// Prepare index publication alongside a larger recoverable asset transaction.
// Does not write files or replace the current lookup snapshot.
std::string prepareAssetPathRegistration(const std::filesystem::path& root,std::span<const StoredAssetPath> paths,
                                         std::span<const std::string> removedIds = {});
void registerAssetPath(const std::filesystem::path& root, const std::string& id,
                       const std::filesystem::path& file);
std::uint64_t contentStorageRevision(const std::filesystem::path& root);
// Windows asset kinds do not change when a filename's suffix changes case.
std::string assetPathExtension(const std::filesystem::path& file);

// All moves and the location index are one recoverable transaction. Optional
// browser metadata is committed with them, so folder IDs survive rename/move.
void relocateContentPaths(const std::filesystem::path& root,
                          std::span<const ContentPathMove> moves,
                          const std::optional<std::string>& browserContents = {});

// Checked writable physical path beneath the selected project. Private metadata
// remains protected. Throws on traversal, symlink escapes and invalid filenames.
std::filesystem::path checkedContentPath(const std::filesystem::path& root,
                                       const std::filesystem::path& file);
std::string contentFilename(std::string name); // validates one Windows filename

} // namespace crimson
