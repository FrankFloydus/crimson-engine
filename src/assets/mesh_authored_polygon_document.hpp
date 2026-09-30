////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/polygon/document_read_snapshot.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

namespace quader {

/**
 * Pins an immutable authored publication without copying retained faces.
 * Pointer-compatible access materializes once for legacy document consumers;
 * read_view() exposes authoritative coordinates over the pinned SDK topology.
 */
class MeshAuthoredPolygonDocument final {
public:
  MeshAuthoredPolygonDocument() = default;
  MeshAuthoredPolygonDocument(std::shared_ptr<const quader_poly::Document> document)
      : document_(std::move(document)) {}

  MeshAuthoredPolygonDocument &operator=(
      std::shared_ptr<const quader_poly::Document> document) {
    snapshot_.reset();
    document_ = std::move(document);
    return *this;
  }

  void share_snapshot(modeling::PolygonDocumentReadSnapshot snapshot) {
    document_.reset();
    snapshot_ = std::make_shared<SnapshotStorage>(std::move(snapshot));
  }

  [[nodiscard]] const modeling::PolygonDocumentReadSnapshot *read_snapshot() const noexcept {
    return snapshot_ ? &snapshot_->snapshot : nullptr;
  }

  [[nodiscard]] quader_poly::DocumentReadView read_view() const noexcept {
    return snapshot_ ? snapshot_->snapshot.view()
        : document_ ? quader_poly::document_read_view(*document_)
                    : quader_poly::DocumentReadView{};
  }

  [[nodiscard]] std::uintptr_t identity_token() const noexcept {
    return reinterpret_cast<std::uintptr_t>(snapshot_
        ? static_cast<const void *>(snapshot_.get()) : document_.get());
  }

  [[nodiscard]] std::shared_ptr<const quader_poly::Document> shared_document() const {
    if (!snapshot_) return document_;
    std::call_once(snapshot_->materialization_once, [storage = snapshot_] {
      const auto view = storage->snapshot.view();
      auto document = std::make_shared<quader_poly::Document>();
      document->vertices.assign(view.vertices.begin(), view.vertices.end());
      document->faces.assign(view.faces.begin(), view.faces.end());
      document->hard_normal_edges.assign(view.hard_normal_edges.begin(), view.hard_normal_edges.end());
      document->soft_normal_edges.assign(view.soft_normal_edges.begin(), view.soft_normal_edges.end());
      document->next_vertex_id = view.next_vertex_id;
      document->next_face_id = view.next_face_id;
      storage->document = std::move(document);
    });
    return snapshot_->document;
  }

  [[nodiscard]] const quader_poly::Document *get() const { return shared_document().get(); }
  [[nodiscard]] const quader_poly::Document *operator->() const { return get(); }
  [[nodiscard]] const quader_poly::Document &operator*() const { return *get(); }
  [[nodiscard]] explicit operator bool() const noexcept { return document_ || snapshot_; }
  [[nodiscard]] operator std::shared_ptr<const quader_poly::Document>() const {
    return shared_document();
  }
  void reset() noexcept { document_.reset(); snapshot_.reset(); }

  friend bool operator==(const MeshAuthoredPolygonDocument &left,
                         const MeshAuthoredPolygonDocument &right) noexcept {
    return left.document_ == right.document_ && left.snapshot_ == right.snapshot_;
  }
  friend bool operator==(const MeshAuthoredPolygonDocument &left, std::nullptr_t) noexcept {
    return !static_cast<bool>(left);
  }
  friend bool operator==(const MeshAuthoredPolygonDocument &left,
                         const std::shared_ptr<const quader_poly::Document> &right) {
    return left.shared_document() == right;
  }

private:
  struct SnapshotStorage final {
    explicit SnapshotStorage(modeling::PolygonDocumentReadSnapshot value)
        : snapshot(std::move(value)) {}
    modeling::PolygonDocumentReadSnapshot snapshot;
    std::once_flag materialization_once;
    std::shared_ptr<const quader_poly::Document> document;
  };
  std::shared_ptr<const quader_poly::Document> document_;
  std::shared_ptr<SnapshotStorage> snapshot_;
};

} // namespace quader
