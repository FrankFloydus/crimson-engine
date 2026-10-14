#pragma once

#include "ProceduralSky.h"
#include <bgfx/bgfx.h>
#include <array>
#include <cstdint>
#include <string>

namespace crimson {

// Renderer-private owner; construct lazily and destroy before bgfx shutdown.
// One instance serves the live world, another the isolated thumbnail world.
class ProceduralSkyGpu {
public:
    ProceduralSkyGpu()=default;
    ~ProceduralSkyGpu();
    ProceduralSkyGpu(const ProceduralSkyGpu&)=delete;
    ProceduralSkyGpu& operator=(const ProceduralSkyGpu&)=delete;
    // A dedicated compute view must precede every consumer. All levels are
    // enqueued together; a failed preparation retains the previous generation.
    bool prepare(const ProceduralSkySettings&,const ProceduralSun&,bgfx::ViewId);
    bgfx::ProgramHandle prepareProgramReload(std::span<const std::uint8_t>,std::string& diagnostic) const;
    void publishProgramReload(bgfx::ProgramHandle) noexcept;
    void bindBackground() const;
    bool ready() const {return valid_;}
    bgfx::TextureHandle diffuse() const {return cubes_[ready_].diffuse;}
    bgfx::TextureHandle specular() const {return cubes_[ready_].specular;}
    float maxSpecularLod() const {return procedural::SpecularMipCount-1;}
    std::uint64_t key() const {return key_;}
    const std::string& status() const {return status_;}
    std::size_t residentBytes() const;

private:
    struct Cubes {bgfx::TextureHandle diffuse{BGFX_INVALID_HANDLE},specular{BGFX_INVALID_HANDLE};};
    std::array<Cubes,2> cubes_{};
    unsigned ready_{};
    bool valid_{};
    bool dirty_{};
    std::uint64_t key_{};
    procedural::Parameters parameters_{};
    bgfx::ProgramHandle program_{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle skyUniform_{BGFX_INVALID_HANDLE},filterUniform_{BGFX_INVALID_HANDLE};
    std::array<bgfx::UniformHandle,4> samplesUniform_{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    std::string status_;
    bool initialize();
    bool allocate(Cubes&);
};
} // namespace crimson
