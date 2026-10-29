////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_view.hpp"
#include <crimson/renderer/Renderer.hpp>
#include <filesystem>
#include <functional>
#include <memory>

namespace quader {
using CrimsonPreviewRecorder = std::function<bool(const MaterialPreviewState&,std::uint32_t,std::uint32_t)>;

// A preview records into the host's ordinary shared frame. Qt owns its display
// image and queue; the view never creates a window, submits, or advances bgfx.
class CrimsonMaterialPreview final : public RenderMaterialPreviewView {
public:
  explicit CrimsonMaterialPreview(std::filesystem::path resources,bool thumbnail=false);
  ~CrimsonMaterialPreview() override;
  void shutdown() override;
  void resize(std::uint32_t,std::uint32_t) override;
  void set_preview_state(const MaterialPreviewState&) override;
  bool set_material(StandardMaterialPreviewSnapshot) override;
  bool initialized() const noexcept override;
  std::uint32_t width() const noexcept override;
  std::uint32_t height() const noexcept override;
  bool pending() const noexcept override;
  void invalidate() override { mark_dirty(); }
  std::weak_ptr<void> lifetime() const;
  std::uint64_t source_owner()const noexcept;
  void set_recorder(CrimsonPreviewRecorder);
  void set_thumbnail_identity(std::string id,std::uint64_t signature);
  void mark_dirty();
  bool dirty() const;
  bool record(crimson::shared::Renderer&);
  void submitted();
  void release_gpu();
  crimson::shared::Submission last_copy;
  void* last_destination=nullptr;
  std::uint32_t last_destination_width=0,last_destination_height=0;
  std::uint64_t last_destination_generation=0;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
