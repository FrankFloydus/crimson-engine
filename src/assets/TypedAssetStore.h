////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRevision.h"
#include "core/Types.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>

namespace quader {

template <typename Document, typename Revision>
/**
 * Owns Typed Asset Store behavior within the asset document and registry layer.
 */
class TypedAssetStore {
public:
    AssetId add(AssetId id, Document document) {
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

    bool update(AssetId id, const std::function<void(Document&)>& edit) {
        auto existing = documents_.find(id);
        if (existing == documents_.end()) {
            return false;
        }
        edit(existing->second);
        stamp(id, existing->second);
        return true;
    }

    Document* get(AssetId id) {
        auto existing = documents_.find(id);
        return existing == documents_.end() ? nullptr : &existing->second;
    }

    const Document* get(AssetId id) const {
        auto existing = documents_.find(id);
        return existing == documents_.end() ? nullptr : &existing->second;
    }

    Revision revision(AssetId id) const {
        const Document* document = get(id);
        return document ? document->revision : Revision{};
    }

    std::size_t size() const { return documents_.size(); }

private:
    void stamp(AssetId id, Document& document) {
        nextRevision_ = nextRevision(nextRevision_);
        document.id = id;
        document.revision = nextRevision_;
    }

    Revision nextRevision_;
    std::unordered_map<AssetId, Document> documents_;
};

} // namespace quader
