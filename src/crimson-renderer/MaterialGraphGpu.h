#pragma once
#include "MaterialGraphShader.h"
#include "assets/MaterialGraph.h"
#include <bgfx/bgfx.h>
#include <array>
#include <memory>
#include <span>

namespace crimson {
class ResourceManager;

// Main-thread GPU owner. Submitted packets pin this exact complete generation.
struct MaterialGraphProgramsVersion {
    std::array<bgfx::ProgramHandle,4> programs{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle parameters{BGFX_INVALID_HANDLE};
    std::array<bgfx::UniformHandle,MaterialGraphMaxTextureBindings> samplers;
    std::uint64_t signature{};
    std::size_t bytes{};
    MaterialGraphProgramsVersion();
    ~MaterialGraphProgramsVersion();
    MaterialGraphProgramsVersion(const MaterialGraphProgramsVersion&)=delete;
    MaterialGraphProgramsVersion& operator=(const MaterialGraphProgramsVersion&)=delete;
};

struct PreparedGraphMaterial {
    std::shared_ptr<const MaterialGraphProgramsVersion> programs;
    std::vector<Vec4> uniforms;
    std::vector<MaterialGraphTextureBinding> sources;
    std::array<bgfx::TextureHandle,MaterialGraphMaxTextureBindings> textures;
    std::array<std::uint32_t,MaterialGraphMaxTextureBindings> flags;
    std::array<std::shared_ptr<const void>,MaterialGraphMaxTextureBindings> leases;
    std::array<std::uint64_t,MaterialGraphMaxTextureBindings> sourceFingerprints{},textureSignatures{};
    MaterialShading shading{MaterialShading::StandardPbr};
    MaterialBlend blend{MaterialBlend::Opaque};
    bool nodePreview{}; // Explicit one-pass editor visualization, independent of preview geometry.
    std::uint64_t signature{};
    PreparedGraphMaterial();
    void bind() const;
    bgfx::ProgramHandle program(MaterialGraphPass pass) const;
};

struct MaterialGraphProgramResult {
    std::shared_ptr<const MaterialGraphProgramsVersion> programs;
    std::string diagnostic;
    bool current{};
};

// Render-boundary owner, explicitly composed by ResourceRuntime. Workers only
// compile CPU binaries; every bgfx call and final destructor is on the main thread.
class MaterialGraphPrograms final {
public:
    MaterialGraphPrograms(ResourceManager&,ShaderCompileConfig);
    ~MaterialGraphPrograms();
    std::unique_ptr<MaterialGraphPrograms> createSibling() const;
    MaterialGraphProgramResult request(const MaterialGraphShader&);
    void changed(std::span<const std::filesystem::path>);
    void trim();
    std::size_t residentBytes() const;
    std::vector<std::filesystem::path> sourceDependencies() const;
    std::uint64_t revision() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
