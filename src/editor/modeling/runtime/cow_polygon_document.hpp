////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace quader_editor_core {

/** Read-only vector facade that keeps legacy PolygonObject reads concise. */
template <typename Element> class CowPolygonDocumentVectorView final {
public:
  using ValueType = Element;
  using SizeType = typename std::vector<Element>::size_type;
  using ConstIterator = typename std::vector<Element>::const_iterator;
  using ConstReverseIterator =
      typename std::vector<Element>::const_reverse_iterator;

  [[nodiscard]] bool empty() const noexcept { return value().empty(); }
  [[nodiscard]] SizeType size() const noexcept { return value().size(); }
  [[nodiscard]] const Element *data() const noexcept { return value().data(); }
  [[nodiscard]] const Element &front() const { return value().front(); }
  [[nodiscard]] const Element &back() const { return value().back(); }
  [[nodiscard]] const Element &at(SizeType index) const {
    return value().at(index);
  }
  [[nodiscard]] const Element &operator[](SizeType index) const {
    return value()[index];
  }
  [[nodiscard]] ConstIterator begin() const noexcept { return value().begin(); }
  [[nodiscard]] ConstIterator end() const noexcept { return value().end(); }
  [[nodiscard]] ConstIterator cbegin() const noexcept {
    return value().cbegin();
  }
  [[nodiscard]] ConstIterator cend() const noexcept { return value().cend(); }
  [[nodiscard]] ConstReverseIterator rbegin() const noexcept {
    return value().rbegin();
  }
  [[nodiscard]] ConstReverseIterator rend() const noexcept {
    return value().rend();
  }

  [[nodiscard]] operator const std::vector<Element> &() const noexcept {
    return value();
  }

  [[nodiscard]] bool operator==(const std::vector<Element> &other) const {
    return value() == other;
  }

private:
  using Member = std::vector<Element> quader_poly::Document::*;

  CowPolygonDocumentVectorView(
      const std::shared_ptr<const quader_poly::Document> *document,
      Member member) noexcept
      : document_(document), member_(member) {}

  [[nodiscard]] const std::vector<Element> &value() const noexcept {
    return (**document_).*member_;
  }

  const std::shared_ptr<const quader_poly::Document> *document_ = nullptr;
  Member member_ = nullptr;

  friend class CowPolygonDocument;
};

/**
 * Shares immutable polygon topology across runtime objects and history states.
 * The first explicit edit detaches when another owner still references it.
 */
class CowPolygonDocument final {
private:
  std::shared_ptr<const quader_poly::Document> document_;

public:
  CowPolygonDocument()
      : document_(std::make_shared<quader_poly::Document>()),
        vertices(&document_, &quader_poly::Document::vertices),
        faces(&document_, &quader_poly::Document::faces),
        hard_normal_edges(&document_,
                          &quader_poly::Document::hard_normal_edges) {}

  CowPolygonDocument(quader_poly::Document document)
      : document_(std::make_shared<quader_poly::Document>(std::move(document))),
        vertices(&document_, &quader_poly::Document::vertices),
        faces(&document_, &quader_poly::Document::faces),
        hard_normal_edges(&document_,
                          &quader_poly::Document::hard_normal_edges) {}

  explicit CowPolygonDocument(
      std::shared_ptr<const quader_poly::Document> document)
      : document_(document ? std::move(document)
                           : std::make_shared<quader_poly::Document>()),
        vertices(&document_, &quader_poly::Document::vertices),
        faces(&document_, &quader_poly::Document::faces),
        hard_normal_edges(&document_,
                          &quader_poly::Document::hard_normal_edges) {}

  CowPolygonDocument(const CowPolygonDocument &other) noexcept
      : document_(other.document_),
        vertices(&document_, &quader_poly::Document::vertices),
        faces(&document_, &quader_poly::Document::faces),
        hard_normal_edges(&document_,
                          &quader_poly::Document::hard_normal_edges) {}

  CowPolygonDocument(CowPolygonDocument &&other) noexcept
      : document_(std::move(other.document_)),
        vertices(&document_, &quader_poly::Document::vertices),
        faces(&document_, &quader_poly::Document::faces),
        hard_normal_edges(&document_,
                          &quader_poly::Document::hard_normal_edges) {
    if (!document_) {
      document_ = std::make_shared<quader_poly::Document>();
    }
  }

  CowPolygonDocument &operator=(const CowPolygonDocument &other) noexcept {
    if (this != &other) {
      document_ = other.document_;
    }
    return *this;
  }

  CowPolygonDocument &operator=(CowPolygonDocument &&other) noexcept {
    if (this != &other) {
      document_ = std::move(other.document_);
      if (!document_) {
        document_ = std::make_shared<quader_poly::Document>();
      }
    }
    return *this;
  }

  CowPolygonDocument &operator=(quader_poly::Document document) {
    document_ =
        std::make_shared<quader_poly::Document>(std::move(document));
    return *this;
  }

  void share(std::shared_ptr<const quader_poly::Document> document) {
    document_ = document ? std::move(document)
                         : std::make_shared<quader_poly::Document>();
  }

  [[nodiscard]] const quader_poly::Document &read() const noexcept {
    return *document_;
  }

  /** Pins the immutable authored topology for asset/history/cache owners. */
  [[nodiscard]] std::shared_ptr<const quader_poly::Document>
  topology_payload() const noexcept {
    return document_;
  }

  [[nodiscard]] quader_poly::Document &edit() {
    if (document_.use_count() != 1) {
      document_ = std::make_shared<quader_poly::Document>(*document_);
    }
    return *std::const_pointer_cast<quader_poly::Document>(document_);
  }

  [[nodiscard]] operator const quader_poly::Document &() const noexcept {
    return read();
  }

  [[nodiscard]] operator quader_poly::Document &() { return edit(); }

  [[nodiscard]] std::uintptr_t storage_token() const noexcept {
    return reinterpret_cast<std::uintptr_t>(document_.get());
  }

  [[nodiscard]] bool
  shares_storage_with(const CowPolygonDocument &other) const noexcept {
    return document_.get() == other.document_.get();
  }

  CowPolygonDocumentVectorView<quader_poly::Vertex> vertices;
  CowPolygonDocumentVectorView<quader_poly::Face> faces;
  CowPolygonDocumentVectorView<quader_poly::Edge> hard_normal_edges;
};

} // namespace quader_editor_core
