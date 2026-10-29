#pragma once
#include "AssetReferenceManager.h"
#include "ModelAsset.h"
#include <atomic>
#include <memory>
#include <stop_token>

namespace crimson {
struct DeletionProgress {std::atomic_size_t completed{};std::size_t total{};};
struct DeletionValidation {
    std::vector<std::shared_ptr<const ModelAsset>> models; // Compact metadata and independently loadable LODs.
    std::vector<std::filesystem::path> dependencies;
    uint64_t fileStateFingerprint{};
    std::size_t bytes{};
};
// Worker-only complete validation of every surviving package under the proposed
// replacement policy. No scene, catalog, GPU or managed-source publication.
std::shared_ptr<DeletionValidation> validateAssetDeletion(const std::filesystem::path& root,std::span<const AssetEntry> survivingModels,
    const AssetReferenceManager& proposed,std::stop_token stop={},const std::shared_ptr<DeletionProgress>& progress={},
    std::span<const AssetEntry> survivingParticles={},std::span<const AssetEntry> survivingDecals={});
bool deletionValidationCurrent(const DeletionValidation& validation);
}
