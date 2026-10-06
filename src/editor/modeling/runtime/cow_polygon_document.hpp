////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/polygon/document_read_snapshot.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace quader_editor_core {

class CowPolygonDocument;

/** Read-only ranges preserve snapshot storage until a vector is requested. */
template <typename Element> class CowPolygonDocumentVectorView final {
public:
  using ValueType = Element;
  using SizeType = typename std::vector<Element>::size_type;
  using ConstIterator = typename std::span<const Element>::iterator;
  using ConstReverseIterator = std::reverse_iterator<ConstIterator>;

  [[nodiscard]] bool empty() const noexcept { return view().empty(); }
  [[nodiscard]] SizeType size() const noexcept { return view().size(); }
  [[nodiscard]] const Element *data() const noexcept { return view().data(); }
  [[nodiscard]] const Element &front() const { return view().front(); }
  [[nodiscard]] const Element &back() const { return view().back(); }
  [[nodiscard]] const Element &at(SizeType index) const {
    const auto elements = view();
    if (index >= elements.size()) {
      throw std::out_of_range("Polygon document element index");
    }
    return elements[index];
  }
  [[nodiscard]] const Element &operator[](SizeType index) const {
    return view()[index];
  }
  [[nodiscard]] ConstIterator begin() const noexcept { return view().begin(); }
  [[nodiscard]] ConstIterator end() const noexcept { return view().end(); }
  [[nodiscard]] ConstIterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] ConstIterator cend() const noexcept { return end(); }
  [[nodiscard]] ConstReverseIterator rbegin() const noexcept {
    return ConstReverseIterator(end());
  }
  [[nodiscard]] ConstReverseIterator rend() const noexcept {
    return ConstReverseIterator(begin());
  }
  [[nodiscard]] operator const std::vector<Element> &() const;
  [[nodiscard]] bool operator==(const std::vector<Element> &other) const {
    return std::ranges::equal(view(), other);
  }

private:
  using Member = std::vector<Element> quader_poly::Document::*;
  using ReadMember = std::span<const Element> quader_poly::DocumentReadView::*;

  CowPolygonDocumentVectorView(const CowPolygonDocument *document, Member member,
                               ReadMember read_member) noexcept
      : document_(document), member_(member), read_member_(read_member) {}
  [[nodiscard]] std::span<const Element> view() const noexcept;

  const CowPolygonDocument *document_ = nullptr;
  Member member_ = nullptr;
  ReadMember read_member_ = nullptr;
  friend class CowPolygonDocument;
};

/**
 * Pins immutable SDK coordinates and topology across runtime/history owners.
 * Legacy document access materializes once; the first edit detaches its storage.
 */
class CowPolygonDocument final {
private:
  /// Shares a pinned snapshot and its lazily materialized immutable document.
  struct SnapshotStorage final {
    explicit SnapshotStorage(quader::modeling::PolygonDocumentReadSnapshot value)
        : snapshot(std::move(value)) {}

    quader::modeling::PolygonDocumentReadSnapshot snapshot;
    std::once_flag materialization_once;
    std::shared_ptr<const quader_poly::Document> document;
  };

  std::shared_ptr<const quader_poly::Document> document_;
  std::shared_ptr<SnapshotStorage> snapshot_;

  [[nodiscard]] std::shared_ptr<const quader_poly::Document>
  shared_document() const {
    if (!snapshot_) return document_;
    std::call_once(snapshot_->materialization_once, [storage = snapshot_] {
      const auto view = storage->snapshot.view();
      auto document = std::make_shared<quader_poly::Document>();
      document->vertices.assign(view.vertices.begin(), view.vertices.end());
      document->faces.assign(view.faces.begin(), view.faces.end());
      document->hard_normal_edges.assign(view.hard_normal_edges.begin(),
                                        view.hard_normal_edges.end());
      document->soft_normal_edges.assign(view.soft_normal_edges.begin(),
                                        view.soft_normal_edges.end());
      document->next_vertex_id = view.next_vertex_id;
      document->next_face_id = view.next_face_id;
      storage->document = std::move(document);
    });
    return snapshot_->document;
  }

public:
  CowPolygonDocument()
      : document_(std::make_shared<quader_poly::Document>()) {}

  CowPolygonDocument(quader_poly::Document document)
      : document_(std::make_shared<quader_poly::Document>(std::move(document))) {}

  explicit CowPolygonDocument(
      std::shared_ptr<const quader_poly::Document> document)
      : document_(document ? std::move(document)
                           : std::make_shared<quader_poly::Document>()) {}

  CowPolygonDocument(const CowPolygonDocument &other) noexcept
      : document_(other.document_), snapshot_(other.snapshot_) {}

  CowPolygonDocument(CowPolygonDocument &&other) noexcept
      : document_(std::move(other.document_)),
        snapshot_(std::move(other.snapshot_)) {
    if (!document_ && !snapshot_) {
      document_ = std::make_shared<quader_poly::Document>();
    }
  }

  CowPolygonDocument &operator=(const CowPolygonDocument &other) noexcept {
    if (this != &other) {
      document_ = other.document_;
      snapshot_ = other.snapshot_;
    }
    return *this;
  }

  CowPolygonDocument &operator=(CowPolygonDocument &&other) noexcept {
    if (this != &other) {
      document_ = std::move(other.document_);
      snapshot_ = std::move(other.snapshot_);
      if (!document_ && !snapshot_) {
        document_ = std::make_shared<quader_poly::Document>();
      }
    }
    return *this;
  }

  CowPolygonDocument &operator=(quader_poly::Document document) {
    document_ = std::make_shared<quader_poly::Document>(std::move(document));
    snapshot_.reset();
    return *this;
  }

  void share(std::shared_ptr<const quader_poly::Document> document) {
    document_ = document ? std::move(document)
                         : std::make_shared<quader_poly::Document>();
    snapshot_.reset();
  }

  void share(quader::modeling::PolygonDocumentReadSnapshot snapshot) {
    snapshot_ = std::make_shared<SnapshotStorage>(std::move(snapshot));
    document_.reset();
  }

  [[nodiscard]] quader_poly::DocumentReadView read_view() const noexcept {
    return snapshot_ ? snapshot_->snapshot.view()
                     : quader_poly::document_read_view(*document_);
  }

  [[nodiscard]] const quader_poly::Document &read() const {
    return *shared_document();
  }

  /** Legacy owners requesting a full document materialize only on demand. */
  [[nodiscard]] std::shared_ptr<const quader_poly::Document>
  topology_payload() const {
    return shared_document();
  }

  [[nodiscard]] quader_poly::Document &edit() {
    if (snapshot_) {
      document_ = std::make_shared<quader_poly::Document>(read());
      snapshot_.reset();
    } else if (document_.use_count() != 1) {
      document_ = std::make_shared<quader_poly::Document>(*document_);
    }
    return *std::const_pointer_cast<quader_poly::Document>(document_);
  }

  [[nodiscard]] operator const quader_poly::Document &() const { return read(); }
  [[nodiscard]] operator quader_poly::Document &() { return edit(); }

  [[nodiscard]] std::uintptr_t storage_token() const noexcept {
    return snapshot_
               ? static_cast<std::uintptr_t>(
                     snapshot_->snapshot.content_identity().value)
               : reinterpret_cast<std::uintptr_t>(document_.get());
  }

  [[nodiscard]] bool
  shares_storage_with(const CowPolygonDocument &other) const noexcept {
    if (snapshot_ && other.snapshot_) {
      return snapshot_->snapshot.content_identity() ==
             other.snapshot_->snapshot.content_identity();
    }
    return !snapshot_ && !other.snapshot_ && document_ == other.document_;
  }

  CowPolygonDocumentVectorView<quader_poly::Vertex> vertices{
      this, &quader_poly::Document::vertices,
      &quader_poly::DocumentReadView::vertices};
  CowPolygonDocumentVectorView<quader_poly::Face> faces{
      this, &quader_poly::Document::faces, &quader_poly::DocumentReadView::faces};
  CowPolygonDocumentVectorView<quader_poly::Edge> hard_normal_edges{
      this, &quader_poly::Document::hard_normal_edges,
      &quader_poly::DocumentReadView::hard_normal_edges};
  CowPolygonDocumentVectorView<quader_poly::Edge> soft_normal_edges{
      this, &quader_poly::Document::soft_normal_edges,
      &quader_poly::DocumentReadView::soft_normal_edges};
};

template <typename Element>
std::span<const Element>
CowPolygonDocumentVectorView<Element>::view() const noexcept {
  return document_->read_view().*read_member_;
}

template <typename Element>
CowPolygonDocumentVectorView<Element>::operator const std::vector<Element> &()
    const {
  return document_->read().*member_;
}

} // namespace quader_editor_core
