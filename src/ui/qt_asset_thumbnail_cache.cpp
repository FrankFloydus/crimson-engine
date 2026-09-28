////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_asset_thumbnail_cache.hpp"

#include "api/quader_project_assets_api.hpp"
#include "render/render_system.hpp"
#include "ui/standard_material_preview_render_snapshot_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <list>
#include <utility>

namespace quader {

RenderMaterialEvaluationSnapshot asset_material_thumbnail_render_snapshot(
    const QuaderProjectMaterialThumbnailInfo &source) {
  const QuaderStandardMaterialDocumentInfo values{
      .id = source.material_id,
      .material = source.material,
      .draft_revision = source.content_revision,
  };
  RenderMaterialEvaluationSnapshot result =
      standard_material_preview_render_snapshot(values);
  RenderStandardMaterialEvaluationSnapshot *standard = result.standard_material();
  for (std::size_t index = 0u; index < source.textures.size(); ++index) {
    if (!source.textures[index].has_value()) {
      continue;
    }
    const QuaderStandardMaterialPreviewTexture &texture = *source.textures[index];
    standard->textures[index] = {
        .project_texture_id = texture.id.value_or(ProjectAssetId{}),
        .identity_revision = texture.id.has_value()
                                 ? render_project_asset_identity_revision(*texture.id)
                                 : 0u,
        .content_revision = texture.content_revision,
        .color_space = texture.color_space == QuaderProjectTextureColorSpace::Srgb
                           ? TextureColorSpace::Srgb
                           : TextureColorSpace::Linear,
        .decoded_rgba8 = texture.rgba8,
        .width = texture.width,
        .height = texture.height,
    };
  }
  return result;
}

/// Retains presentation keys and owned Qt pixels, never mutable asset state.
struct QtAssetThumbnailCache::Impl final {
  /// Cheap paint key; the catalog revision fences dependency changes before load.
  struct RequestKey final {
    ProjectAssetId id;
    std::uint64_t generation = 0u;
    std::uint64_t catalog_revision = 0u;
    std::uint64_t content_revision = 0u;
    std::uint32_t pixels = 0u;
    qreal device_pixel_ratio = 1.0;
    std::uint32_t preset_revision = MaterialThumbnailRenderer::kPresetRevision;
    friend bool operator==(const RequestKey &, const RequestKey &) = default;
  };

  struct Entry final {
    RequestKey key;
    std::optional<RenderStandardMaterialEvaluationSnapshot> evaluation;
    QImage image;
  };

  struct Pending final {
    RequestKey key;
    std::optional<RenderMaterialEvaluationSnapshot> snapshot;
    std::optional<RenderStandardMaterialEvaluationSnapshot> evaluation;
    std::shared_ptr<const MaterialThumbnailReadback> readback;
  };

  static bool same_presentation(const RequestKey &left, const RequestKey &right) {
    return left.id == right.id && left.generation == right.generation &&
           left.pixels == right.pixels &&
           left.device_pixel_ratio == right.device_pixel_ratio &&
           left.preset_revision == right.preset_revision;
  }

  static RenderStandardMaterialEvaluationSnapshot evaluation_key(
      const RenderStandardMaterialEvaluationSnapshot &source) {
    auto result = source;
    // The LRU owns only thumbnail pixels; texture identities, dimensions and
    // content revisions describe the rendering without pinning decoded images.
    for (auto &texture : result.textures) {
      texture.encoded_file_bytes.reset();
      texture.decoded_rgba8.reset();
    }
    return result;
  }

  explicit Impl(std::size_t budget) : byte_budget(budget) {}

  void clear() {
    entries.clear();
    queued.clear();
    active.reset();
    bytes = 0u;
  }

  void insert(Entry entry) {
    for (auto current = entries.begin(); current != entries.end();) {
      if (same_presentation(current->key, entry.key)) {
        bytes -= static_cast<std::size_t>(current->image.sizeInBytes());
        current = entries.erase(current);
      } else {
        ++current;
      }
    }
    const std::size_t cost = static_cast<std::size_t>(entry.image.sizeInBytes());
    if (cost > byte_budget) {
      // Cache the failed size as an empty entry instead of resubmitting forever.
      entry.image = {};
    }
    bytes += static_cast<std::size_t>(entry.image.sizeInBytes());
    entries.push_front(std::move(entry));
    while (bytes > byte_budget || entries.size() > 256u) {
      bytes -= static_cast<std::size_t>(entries.back().image.sizeInBytes());
      entries.pop_back();
    }
  }

  SnapshotLoader loader;
  Submit submit;
  Pump pump;
  std::list<Entry> entries;
  std::deque<Pending> queued;
  std::optional<Pending> active;
  std::size_t byte_budget = 0u;
  std::size_t bytes = 0u;
  std::uint64_t generation = 0u;
  std::uint64_t catalog_revision = 0u;
  bool visible = false;
};

QtAssetThumbnailCache::QtAssetThumbnailCache(std::size_t byte_budget)
    : impl_(std::make_unique<Impl>(byte_budget)) {}

QtAssetThumbnailCache::~QtAssetThumbnailCache() = default;

void QtAssetThumbnailCache::initialize(RenderSystem &renderer,
                                       SnapshotLoader loader) {
  initialize(std::move(loader),
             [&renderer](RenderMaterialEvaluationSnapshot snapshot,
                         std::uint32_t size) {
               return renderer.submit_material_thumbnail(std::move(snapshot), size);
             },
             [&renderer] { renderer.poll_material_thumbnails(); });
}

void QtAssetThumbnailCache::initialize(SnapshotLoader loader, Submit submit,
                                       Pump pump) {
  shutdown();
  impl_->loader = std::move(loader);
  impl_->submit = std::move(submit);
  impl_->pump = std::move(pump);
}

void QtAssetThumbnailCache::shutdown() {
  impl_->clear();
  impl_->loader = {};
  impl_->submit = {};
  impl_->pump = {};
  impl_->visible = false;
}

void QtAssetThumbnailCache::set_visible(bool visible) {
  impl_->visible = visible;
  if (!visible) {
    impl_->queued.clear();
    impl_->active.reset();
  }
}

void QtAssetThumbnailCache::set_project_generation(std::uint64_t generation) {
  if (impl_->generation != generation) {
    impl_->clear();
    impl_->generation = generation;
  }
}

void QtAssetThumbnailCache::set_catalog_revision(std::uint64_t revision) {
  impl_->catalog_revision = revision;
}

QImage QtAssetThumbnailCache::request(ProjectAssetId id,
                                      std::uint64_t content_revision,
                                      QSize logical_size,
                                      qreal device_pixel_ratio) {
  if (!id.valid() || !logical_size.isValid() || logical_size.isEmpty() ||
      !std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0.0) {
    return {};
  }
  const auto pixels = static_cast<std::uint32_t>(std::clamp(
      std::round(std::min(logical_size.width(), logical_size.height()) *
                 device_pixel_ratio),
      1.0, static_cast<double>(MaterialThumbnailRenderer::kMaximumSize)));
  const Impl::RequestKey key{id, impl_->generation, impl_->catalog_revision,
                             content_revision, pixels, device_pixel_ratio};
  if (impl_->active.has_value() && impl_->active->key.id == id &&
      (!Impl::same_presentation(impl_->active->key, key) ||
       impl_->active->key.content_revision != key.content_revision)) {
    impl_->active.reset();
  }
  const auto found = std::find_if(impl_->entries.begin(), impl_->entries.end(),
                                 [&key](const Impl::Entry &entry) {
                                   return Impl::same_presentation(entry.key, key);
                                 });
  QImage retained;
  if (found != impl_->entries.end()) {
    impl_->entries.splice(impl_->entries.begin(), impl_->entries, found);
    retained = impl_->entries.front().image;
    if (impl_->entries.front().key == key) {
      return retained;
    }
  }
  if (!impl_->visible || !impl_->loader || !impl_->submit ||
      (impl_->active.has_value() &&
       Impl::same_presentation(impl_->active->key, key) &&
       impl_->active->key.content_revision == key.content_revision) ||
      std::ranges::any_of(impl_->queued, [&key](const Impl::Pending &pending) {
        return pending.key == key;
      })) {
    return retained;
  }
  // A newer revision or size for an asset retires its superseded queued request.
  std::erase_if(impl_->queued, [id](const Impl::Pending &pending) {
    return pending.key.id == id;
  });
  if (impl_->queued.size() == kMaximumPending) {
    impl_->queued.pop_front();
  }
  impl_->queued.push_back(Impl::Pending{.key = key});
  return retained;
}

void QtAssetThumbnailCache::retain_visible_requests(
    std::span<const ProjectAssetId> visible_ids) {
  const auto hidden = [visible_ids](const Impl::Pending &pending) {
    return std::ranges::find(visible_ids, pending.key.id) == visible_ids.end();
  };
  std::erase_if(impl_->queued, hidden);
  if (impl_->active.has_value() && hidden(*impl_->active)) {
    impl_->active.reset();
  }
}

std::vector<ProjectAssetId> QtAssetThumbnailCache::poll() {
  std::vector<ProjectAssetId> completed;
  if (impl_->pump) {
    impl_->pump();
  }
  if (!impl_->visible) {
    return completed;
  }
  if (impl_->active.has_value()) {
    const auto &readback = impl_->active->readback;
    if (!readback->ready()) {
      return completed;
    }
    QImage image;
    const std::uint32_t size = impl_->active->key.pixels;
    if (readback->size == size &&
        readback->rgba8.size() == static_cast<std::size_t>(size) * size * 4u) {
      const QImage borrowed(reinterpret_cast<const uchar *>(readback->rgba8.data()),
                            static_cast<int>(size), static_cast<int>(size),
                            static_cast<int>(size * 4u),
                            QImage::Format_RGBA8888_Premultiplied);
      // Filament normalizes client readback rows to top-down on every backend.
      // Copy the bytes before releasing the completed asynchronous readback.
      image = borrowed.copy();
      image.setDevicePixelRatio(impl_->active->key.device_pixel_ratio);
    }
    completed.push_back(impl_->active->key.id);
    impl_->insert({impl_->active->key, impl_->active->evaluation, std::move(image)});
    impl_->active.reset();
  }
  if (impl_->queued.empty() || !impl_->loader || !impl_->submit) {
    return completed;
  }
  Impl::Pending &pending = impl_->queued.front();
  if (pending.key.catalog_revision != impl_->catalog_revision) {
    pending.key.catalog_revision = impl_->catalog_revision;
    pending.snapshot.reset();
  }
  if (!pending.snapshot.has_value()) {
    pending.snapshot = impl_->loader(pending.key.id);
    if (!pending.snapshot.has_value() ||
        pending.snapshot->project_material_id != pending.key.id ||
        pending.snapshot->content_revision != pending.key.content_revision ||
        pending.snapshot->standard_material() == nullptr) {
      completed.push_back(pending.key.id);
      impl_->insert({pending.key, {}, {}});
      impl_->queued.pop_front();
      return completed;
    }
    pending.evaluation = Impl::evaluation_key(*pending.snapshot->standard_material());
    const auto reusable = std::find_if(
        impl_->entries.begin(), impl_->entries.end(),
        [&pending](const Impl::Entry &entry) {
          return Impl::same_presentation(entry.key, pending.key) &&
                 entry.evaluation == pending.evaluation;
        });
    if (reusable != impl_->entries.end()) {
      reusable->key = pending.key;
      completed.push_back(pending.key.id);
      impl_->queued.pop_front();
      return completed;
    }
  }
  pending.readback = impl_->submit(*pending.snapshot, pending.key.pixels);
  if (pending.readback != nullptr) {
    pending.snapshot.reset();
    impl_->active = std::move(pending);
    impl_->queued.pop_front();
  }
  return completed;
}

std::size_t QtAssetThumbnailCache::cached_bytes() const noexcept {
  return impl_->bytes;
}

std::size_t QtAssetThumbnailCache::pending_count() const noexcept {
  return impl_->queued.size() + (impl_->active.has_value() ? 1u : 0u);
}

} // namespace quader
