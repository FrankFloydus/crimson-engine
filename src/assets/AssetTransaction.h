#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace crimson {
struct AssetFileChange {
    std::string id;
    std::optional<std::string> contents;
};
std::filesystem::path checkedManagedAssetPath(const std::filesystem::path&,const std::string& id);
// Recoverable file publication, including additions and index updates.
void commitAssetTransaction(const std::filesystem::path&,std::span<const AssetFileChange>);
void recoverAssetTransactions(const std::filesystem::path&);
}
