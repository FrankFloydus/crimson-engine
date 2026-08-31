////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_revision.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <utility>

namespace quader {

template <typename Document, typename Revision>
/**
 * Owns Typed Asset Store behavior within the asset document and registry layer.
 */
class TypedAssetStore {
public:
    /** Stores participant documents and allocator state for an exact targeted rollback. */
    struct TargetedCheckpoint {
      bool valid = false;
      Revision next_revision;
      std::unordered_map<AssetId, std::optional<Document>> documents;
    };

    AssetId add(AssetId id, Document document) {
        if (id == kInvalidAssetId || documents_.contains(id)) {
          return kInvalidAssetId;
        }
        stamp(id, document);
        documents_[id] = std::move(document);
        return id;
    }

    bool replace(AssetId id, Document document) {
        auto existing = documents_.find(id);
        if (existing == documents_.end()) {
            return false;
        }
        stamp(id, document);
        existing->second = std::move(document);
        return true;
    }

    bool remove(AssetId id) {
        return documents_.erase(id) != 0u;
    }

    bool update(AssetId id, const std::function<void(Document&)>& edit) {
        auto existing = documents_.find(id);
        if (existing == documents_.end()) {
            return false;
        }
        edit(existing->second);
        stamp(id, existing->second);
        return true;
    }

    const Document* get(AssetId id) const {
        auto existing = documents_.find(id);
        return existing == documents_.end() ? nullptr : &existing->second;
    }

    bool contains(AssetId id) const {
        return documents_.find(id) != documents_.end();
    }

    std::vector<AssetId> ids() const {
        std::vector<AssetId> result;
        result.reserve(documents_.size());
        for (const auto& [id, document] : documents_) {
            (void)document;
            result.push_back(id);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    Revision revision(AssetId id) const {
        const Document* document = get(id);
        return document ? document->revision : Revision{};
    }

    std::size_t size() const { return documents_.size(); }

    [[nodiscard]] TargetedCheckpoint
    capture_targeted(std::span<const AssetId> ids) const {
      TargetedCheckpoint checkpoint;
      checkpoint.valid = true;
      checkpoint.next_revision = next_revision_;
      checkpoint.documents.reserve(ids.size());
      for (const AssetId id : ids) {
        if (id == kInvalidAssetId || checkpoint.documents.contains(id)) {
          continue;
        }
        const auto existing = documents_.find(id);
        checkpoint.documents.emplace(
            id, existing != documents_.end()
                    ? std::optional<Document>{existing->second}
                    : std::nullopt);
      }
      return checkpoint;
    }

    bool restore_targeted(const TargetedCheckpoint &checkpoint,
                          std::span<const AssetId> remove_ids = {}) {
      if (!checkpoint.valid) {
        return false;
      }
      for (const AssetId id : remove_ids) {
        if (!checkpoint.documents.contains(id)) {
          documents_.erase(id);
        }
      }
      for (const auto &[id, document] : checkpoint.documents) {
        if (document.has_value()) {
          documents_[id] = *document;
        } else {
          documents_.erase(id);
        }
      }
      next_revision_ = checkpoint.next_revision;
      return true;
    }

    /**
     * Restores only named documents while retaining the store's monotonic
     * revision allocator. This is used when a long-lived modeling preview
     * overlaps unrelated asset publications that must remain authored.
     */
    bool restore_targeted_preserving_revision(
        const TargetedCheckpoint &checkpoint,
        std::span<const AssetId> restore_ids,
        std::span<const AssetId> remove_ids = {}) {
      if (!checkpoint.valid) {
        return false;
      }
      for (const AssetId id : remove_ids) {
        if (!checkpoint.documents.contains(id)) {
          documents_.erase(id);
        }
      }
      for (const AssetId id : restore_ids) {
        const auto captured = checkpoint.documents.find(id);
        if (captured == checkpoint.documents.end()) {
          continue;
        }
        if (!captured->second.has_value()) {
          documents_.erase(id);
          continue;
        }
        Document restored = *captured->second;
        stamp(id, restored);
        documents_[id] = std::move(restored);
      }
      return true;
    }

    void restamp_after(Revision revision_floor) {
      if (next_revision_ < revision_floor) {
        next_revision_ = revision_floor;
      }
      for (const AssetId id : ids()) {
        auto document = documents_.find(id);
        if (document != documents_.end()) {
          stamp(id, document->second);
        }
      }
    }

private:
    void stamp(AssetId id, Document& document) {
      next_revision_ = next_revision(next_revision_);
      document.id = id;
      document.revision = next_revision_;
    }

    Revision next_revision_;
    std::unordered_map<AssetId, Document> documents_;
};

} // namespace quader
