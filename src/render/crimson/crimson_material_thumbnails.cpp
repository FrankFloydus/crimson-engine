////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_material_thumbnails.hpp"
#include "render/crimson/crimson_material_preview.hpp"
#include "core/logger.hpp"
#include <cstring>
#include <string>

namespace quader {
struct CrimsonMaterialThumbnails::Impl {
  explicit Impl(std::filesystem::path root):preview(std::move(root),true){}
  CrimsonMaterialPreview preview;
  std::shared_ptr<MaterialThumbnailReadback> active;
  std::string id;
  std::uint64_t sequence=0;
  bool recorded=false,submitted=false;
};
CrimsonMaterialThumbnails::CrimsonMaterialThumbnails(std::filesystem::path root):impl_(std::make_unique<Impl>(std::move(root))){}
CrimsonMaterialThumbnails::~CrimsonMaterialThumbnails()=default;
std::shared_ptr<const MaterialThumbnailReadback> CrimsonMaterialThumbnails::submit(RenderMaterialEvaluationSnapshot snapshot,std::uint32_t size) {
  auto& p=*impl_;
  if((p.active&&!p.active->ready())||!size||size>kMaterialThumbnailMaximumSize||!snapshot.standard_material())return {};
  if(!p.preview.set_material(std::move(snapshot)))return {};
  p.preview.resize(size,size);p.id="quader-standard-material-"+std::to_string(++p.sequence);
  p.preview.set_thumbnail_identity(p.id,p.sequence);
  p.active=std::make_shared<MaterialThumbnailReadback>();p.active->size=size;
  p.recorded=false;p.submitted=false;return p.active;
}
bool CrimsonMaterialThumbnails::needs_recording()const {
  return impl_->active&&!impl_->active->ready()&&!impl_->recorded;
}
bool CrimsonMaterialThumbnails::record(crimson::shared::Renderer& renderer) {
  auto& p=*impl_;if(!needs_recording()||renderer.thumbnailBusy())return false;
  p.recorded=p.preview.record(renderer);
  if(!p.recorded&&!p.preview.pending())p.active->complete.store(true,std::memory_order_release);
  return p.recorded;
}
void CrimsonMaterialThumbnails::submitted(){if(impl_->recorded)impl_->submitted=true;}
void CrimsonMaterialThumbnails::poll(crimson::shared::Renderer& renderer,std::uint32_t frame) {
  auto& p=*impl_;if(!p.active||p.active->ready()||!p.submitted)return;
  auto result=renderer.pollThumbnail(frame,"quader-standard-material");
  if(!result) {
    if(renderer.thumbnailBusy())return;
    LOG_ERROR("Standard material thumbnail readback failed: {}",renderer.thumbnailStatus());
    p.active->complete.store(true,std::memory_order_release);return;
  }
  if(result->id!=p.id||result->signature!=p.sequence||result->image.width!=p.active->size||result->image.height!=p.active->size) {
    LOG_ERROR("Standard material thumbnail rejected a stale readback owner or extent");
    p.active->complete.store(true,std::memory_order_release);return;
  }
  p.active->rgba8.resize(result->image.rgba.size());
  std::memcpy(p.active->rgba8.data(),result->image.rgba.data(),result->image.rgba.size());
  p.active->complete.store(true,std::memory_order_release);
}
void CrimsonMaterialThumbnails::shutdown(){impl_->active.reset();impl_->preview.shutdown();impl_->recorded=false;impl_->submitted=false;}
}
