////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/crimson/crimson_environment.hpp"
#include "core/scene_environment_catalog.hpp"
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>
#include <stdexcept>

namespace quader {
using crimson::Vec3;
// Environment preparation stays on the CPU worker. The packaged sky cube is
// the source of truth, including HDR radiance; no Filament runtime is needed.
crimson::PreparedEnvironment prepare_crimson_packaged_environment(const std::filesystem::path& root,std::string id) {
  const auto* descriptor=scene_environment_descriptor(id);
  if(!descriptor)throw std::runtime_error("Unknown material-preview environment");
  const auto file=root/"ibl"/descriptor->directory/(std::string(descriptor->file_stem)+"_skybox.ktx");
  std::ifstream stream(file,std::ios::binary);
  const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),{}};
  if(bytes.empty())throw std::runtime_error("Cannot read packaged preview environment: "+file.string());
  bx::DefaultAllocator allocator;
  auto* cube=bimg::imageParse(&allocator,bytes.data(),static_cast<std::uint32_t>(bytes.size()),bimg::TextureFormat::RGBA32F);
  if(!cube||!cube->m_cubeMap){if(cube)bimg::imageFree(cube);throw std::runtime_error("Invalid HDR preview cubemap");}
  struct Free {bimg::ImageContainer* image;~Free(){bimg::imageFree(image);}} free{cube};
  std::array<bimg::ImageMip,6> faces{};
  for(std::uint16_t face=0;face<6;++face)
    if(!bimg::imageGetRawData(*cube,face,0,cube->m_data,cube->m_size,faces[face]))
      throw std::runtime_error("Incomplete preview cubemap");
  const auto sample=[&](Vec3 direction) {
    const float ax=std::abs(direction[0]),ay=std::abs(direction[1]),az=std::abs(direction[2]);
    unsigned face;float u,v,major;
    if(ax>=ay&&ax>=az){major=ax;face=direction[0]>=0?0:1;u=direction[0]>=0?-direction[2]:direction[2];v=-direction[1];}
    else if(ay>=az){major=ay;face=direction[1]>=0?2:3;u=direction[0];v=direction[1]>=0?direction[2]:-direction[2];}
    else {major=az;face=direction[2]>=0?4:5;u=direction[2]>=0?direction[0]:-direction[0];v=-direction[1];}
    const auto& mip=faces[face];
    const auto x=std::min(mip.m_width-1,static_cast<std::uint32_t>(std::max(0.f,(u/major*.5f+.5f)*mip.m_width)));
    const auto y=std::min(mip.m_height-1,static_cast<std::uint32_t>(std::max(0.f,(v/major*.5f+.5f)*mip.m_height)));
    std::array<float,4> rgba{};std::memcpy(rgba.data(),mip.m_data+(std::size_t(y)*mip.m_width+x)*sizeof(rgba),sizeof(rgba));
    return Vec3{std::max(0.f,rgba[0]),std::max(0.f,rgba[1]),std::max(0.f,rgba[2])};
  };
  crimson::ibl::Image source;source.width=512;source.height=256;source.pixels.resize(512*256);
  for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x) {
    const float theta=(y+.5f)/source.height*std::numbers::pi_v<float>;
    const float phi=((x+.5f)/source.width-.5f)*2*std::numbers::pi_v<float>;
    source.pixels[std::size_t(y)*source.width+x]=sample({std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)});
  }
  crimson::PreparedEnvironment result;
  result.settings.diffuseSize=16;result.settings.specularSize=128;result.settings.lutSize=64;
  result.settings.diffuseSamples=128;result.settings.specularSamples=128;result.settings.lutSamples=256;
  result.key=crimson::ibl::cacheKey(bytes,result.settings);
  result.data=crimson::ibl::bake(source,result.settings);result.status="Packaged preview environment: "+id;
  return result;
}

}
