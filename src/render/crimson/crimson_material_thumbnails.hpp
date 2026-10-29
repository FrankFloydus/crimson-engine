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
#include <memory>

namespace quader {
// One queued Standard sphere, one canonical asynchronous ticket. Rendering is
// recorded by the same host frame as every visible pane; poll never submits.
class CrimsonMaterialThumbnails final {
public:
  explicit CrimsonMaterialThumbnails(std::filesystem::path resources);
  ~CrimsonMaterialThumbnails();
  std::shared_ptr<const MaterialThumbnailReadback> submit(RenderMaterialEvaluationSnapshot,std::uint32_t size);
  bool needs_recording() const;
  bool record(crimson::shared::Renderer&);
  void submitted();
  void poll(crimson::shared::Renderer&,std::uint32_t completed_frame);
  void shutdown();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
