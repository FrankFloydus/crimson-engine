////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_types.hpp"

#include "api/quader_polygon_document_internal_access.hpp"

#include <utility>

namespace quader {

/**
 * Stores backend polygon data behind the public opaque QuaderPolygonDocument wrapper.
 */
struct QuaderPolygonDocumentStorage {
    quader_poly::Document document;
};

QuaderPolygonDocument::QuaderPolygonDocument()
    : storage_(std::make_shared<QuaderPolygonDocumentStorage>()) {}

QuaderPolygonDocument::~QuaderPolygonDocument() = default;

QuaderPolygonDocument::QuaderPolygonDocument(const QuaderPolygonDocument&) = default;

QuaderPolygonDocument& QuaderPolygonDocument::operator=(const QuaderPolygonDocument&) = default;

QuaderPolygonDocument::QuaderPolygonDocument(QuaderPolygonDocument&&) noexcept = default;

QuaderPolygonDocument& QuaderPolygonDocument::operator=(QuaderPolygonDocument&&) noexcept = default;

QuaderPolygonDocument::QuaderPolygonDocument(std::shared_ptr<QuaderPolygonDocumentStorage> storage)
    : storage_(std::move(storage)) {
    if (!storage_) {
        storage_ = std::make_shared<QuaderPolygonDocumentStorage>();
    }
}

QuaderPolygonDocument QuaderPolygonDocumentInternalAccess::fromBackend(quader_poly::Document document) {
    auto storage = std::make_shared<QuaderPolygonDocumentStorage>();
    storage->document = std::move(document);
    return QuaderPolygonDocument(std::move(storage));
}

quader_poly::Document QuaderPolygonDocumentInternalAccess::toBackend(const QuaderPolygonDocument& document) {
    if (!document.storage_) {
        return {};
    }
    return document.storage_->document;
}

} // namespace quader
