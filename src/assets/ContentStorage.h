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
// files in the game's Content tree, protected Engine content, or private metadata.
struct StoredAssetPath { std::string id; std::filesystem::path file; };
struct ContentPathMove {
    std::filesystem::path from, to;
    // Retained import inputs stay inside a model package when an extracted image
    // is first placed beside that package. Its asset identity follows the copy.
    bool copy{};
};

// Recover interrupted moves and migrate the former assets/browser layout once.
// Call before constructing asset stores, document scanners, or file watchers.
void initializeContentStorage(const std::filesystem::path& root);
bool contentStorageReady(const std::filesystem::path& root);
// Explicit watcher/startup boundary. Warm lookups use the validated snapshot
// without probing the filesystem. Equal-content notifications keep its revision.
// Invalid external locations block lookups until a successful refresh.
bool refreshContentStorage(const std::filesystem::path& root);
std::filesystem::path resolveAssetPath(const std::filesystem::path& root, std::string_view id);
// Returns an indexed ID, or a project-relative identity during legacy bootstrap.
std::string assetIdAtPath(const std::filesystem::path& root, const std::filesystem::path& file);
// Legacy package/member identity for import reconstruction only; never publish
// this string as an AssetRef. New references and index keys are UUIDs.
std::string legacyAssetId(const std::filesystem::path& root, std::string_view id);
std::vector<StoredAssetPath> storedAssetPaths(const std::filesystem::path& root);
void registerAssetPath(const std::filesystem::path& root, const std::string& id,
                       const std::filesystem::path& file);
std::uint64_t contentStorageRevision(const std::filesystem::path& root);

// All moves and the location index are one recoverable transaction. Optional
// browser metadata is committed with them, so folder IDs survive rename/move.
void relocateContentPaths(const std::filesystem::path& root,
                          std::span<const ContentPathMove> moves,
                          const std::optional<std::string>& browserContents = {});

// Checked physical path beneath Content; Engine is deliberately read-only to
// editor mutations. Throws on traversal, symlink escapes, and invalid filenames.
std::filesystem::path checkedContentPath(const std::filesystem::path& root,
                                       const std::filesystem::path& file);
std::string contentFilename(std::string name); // validates one Windows filename

} // namespace crimson
