////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_standard_material.hpp"
#include "core/color_space.hpp"
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace quader {
namespace {
QImage pixels(const RenderMaterialTextureEvaluationSnapshot& source) {
  if(source.decoded_present())return QImage(reinterpret_cast<const uchar*>(source.decoded_rgba8->data()),
      static_cast<int>(source.width),static_cast<int>(source.height),QImage::Format_RGBA8888).copy();
  if(source.encoded_present())return QImage::fromData(reinterpret_cast<const uchar*>(source.encoded_file_bytes->data()),
      static_cast<int>(source.encoded_file_bytes->size())).convertToFormat(QImage::Format_RGBA8888);
  return {};
}

struct Textures {
  std::array<bgfx::TextureHandle,4> owned{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  ~Textures(){for(auto handle:owned)if(bgfx::isValid(handle))bgfx::destroy(handle);}
};

bgfx::TextureHandle texture(QImage image,bool srgb) {
  if(image.isNull()||image.width()>65535||image.height()>65535)return BGFX_INVALID_HANDLE;
  const auto w=image.width(),h=image.height();
  image=image.convertToFormat(QImage::Format_RGBA8888);
  // All levels are renderer-owned immutable bytes; no QImage storage escapes.
  std::vector<std::uint8_t> data;
  for(;;) {
    const auto offset=data.size();data.resize(offset+std::size_t(image.width())*image.height()*4);
    for(int y=0;y<image.height();++y)std::memcpy(data.data()+offset+std::size_t(y)*image.width()*4,image.constScanLine(y),std::size_t(image.width())*4);
    if(image.width()==1&&image.height()==1)break;
    const int nw=std::max(1,image.width()/2),nh=std::max(1,image.height()/2);
    QImage next(nw,nh,QImage::Format_RGBA8888);
    for(int y=0;y<nh;++y)for(int x=0;x<nw;++x)for(int c=0;c<4;++c) {
      float sum=0;
      for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx) {
        const int sx=std::min(image.width()-1,x*2+dx),sy=std::min(image.height()-1,y*2+dy);
        float value=image.constScanLine(sy)[sx*4+c]/255.f;
        sum+=srgb&&c<3?srgb_to_linear(value):value;
      }
      float value=sum*.25f;if(srgb&&c<3)value=linear_to_srgb(value);
      next.scanLine(y)[x*4+c]=normalized_to_byte(value);
    }
    image=std::move(next);
  }
  return bgfx::createTexture2D(static_cast<std::uint16_t>(w),static_cast<std::uint16_t>(h),w>1||h>1,1,bgfx::TextureFormat::RGBA8,
      (srgb?BGFX_TEXTURE_SRGB:0)|BGFX_SAMPLER_MIN_ANISOTROPIC|BGFX_SAMPLER_MAG_ANISOTROPIC,
      bgfx::copy(data.data(),static_cast<std::uint32_t>(data.size())));
}

}
std::shared_ptr<const CrimsonStandardMaterial> prepare_crimson_standard_material(const RenderStandardMaterialEvaluationSnapshot& material,const std::shared_ptr<const CrimsonStandardMaterial>& previous) {
  auto result=std::make_shared<CrimsonStandardMaterial>();
  result->source=material;
  if(previous&&previous->source.textures==material.textures&&previous->source.directx_normal==material.directx_normal) {
    result->lease=previous->lease;result->bindings=previous->bindings;
    result->surface.alphaBlend=previous->surface.alphaBlend;
  } else {
  auto resources=std::make_shared<Textures>();result->lease=resources;
  constexpr std::array<unsigned,4> kSourceSlots{0,1,3,2};
  for(unsigned slot=0;slot<4;++slot) {
    const auto& source=material.textures[kSourceSlots[slot]];
    auto image=pixels(source);
    if(source.present()&&image.isNull())throw std::runtime_error("Standard material texture decoding failed");
    if(image.isNull())continue;
    if(slot==0)for(int y=0;y<image.height()&&!result->surface.alphaBlend;++y) {
      const auto* pixels=image.constScanLine(y);
      for(int x=0;x<image.width();++x)if(pixels[x*4+3]<255) {
        result->surface.alphaBlend=true;
        break;
      }
    }
    if(slot==2&&material.directx_normal)for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
      image.scanLine(y)[x*4+1]=255-image.constScanLine(y)[x*4+1];
    resources->owned[slot]=texture(std::move(image),slot==0&&source.color_space==TextureColorSpace::Srgb);
    if(!bgfx::isValid(resources->owned[slot]))throw std::runtime_error("Standard material texture allocation failed");
    result->bindings.handles[slot]=resources->owned[slot];result->bindings.leases[slot]=resources;
    result->bindings.sourceFingerprints[slot]=source.identity_revision;
    result->bindings.signatures[slot]=source.content_revision^(slot==2&&material.directx_normal?0xa7f89d1e48267b3full:0ull);
  }
  }
  const auto finite=[](float value,float fallback){return std::isfinite(value)?value:fallback;};
  auto& surface=result->surface;
  surface.baseColor={std::clamp(finite(material.base_color.x,1),0.f,1.f),std::clamp(finite(material.base_color.y,1),0.f,1.f),std::clamp(finite(material.base_color.z,1),0.f,1.f),1};
  surface.roughness=std::clamp(finite(material.roughness,1),0.f,1.f);surface.metallic=std::clamp(finite(material.metallic,0),0.f,1.f);
  surface.normalScale=std::clamp(finite(material.normal_strength,1),-16.f,16.f);surface.doubleSided=true;surface.receiveFog=false;
  surface.separateMetallicRoughnessMaps=true;surface.burleyDiffuse=material.diffuse_model==MaterialDiffuseModel::Burley;
  const auto& uv=material.texture_transform;
  const float rotation=finite(uv.rotation_degrees,0),a=rotation*std::numbers::pi_v<float>/180.f,c=std::cos(a),sn=std::sin(a);
  const float cx=finite(uv.center.x,.5f),cy=finite(uv.center.y,.5f),sx=finite(uv.scale.x,1),sy=finite(uv.scale.y,1);
  surface.uvScale={sx,sy,1};surface.uvRotation={0,0,rotation};
  surface.uvOffset={cx+finite(uv.offset.x,0)-c*cx*sx+sn*cy*sy,cy+finite(uv.offset.y,0)-sn*cx*sx-c*cy*sy,0};
  return result;
}
}
