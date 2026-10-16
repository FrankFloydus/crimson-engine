#pragma once
#include "MaterialGraphShader.h"
#include "assets/MaterialGraph.h"
#include "resources/ResourceManager.h"
#include <bgfx/bgfx.h>
#include <array>
#include <memory>
#include <span>

namespace crimson {
// Main-thread GPU owner. Submitted packets pin this exact complete generation.
struct MaterialGraphProgramsVersion {
    std::array<bgfx::ProgramHandle,5> programs;
    bgfx::UniformHandle parameters{BGFX_INVALID_HANDLE};
    std::array<bgfx::UniformHandle,MaterialGraphMaxTextureBindings> samplers;
    std::uint64_t signature{};
    std::size_t bytes{};
    bool optimized{}; // Interactive programs remain complete while optimized replacements compile.
    MaterialGraphProgramsVersion();
    ~MaterialGraphProgramsVersion();
    MaterialGraphProgramsVersion(const MaterialGraphProgramsVersion&)=delete;
    MaterialGraphProgramsVersion& operator=(const MaterialGraphProgramsVersion&)=delete;
};

struct PreparedGraphMaterial {
    std::shared_ptr<const MaterialGraphProgramsVersion> programs;
    std::vector<Vec4> uniforms;
    std::vector<MaterialGraphTextureBinding> sources;
    std::shared_ptr<const MaterialGraph> runtimeGraph;
    std::vector<MaterialGraphRuntimeParameter> runtimeParameters;
    std::array<bgfx::TextureHandle,MaterialGraphMaxTextureBindings> textures;
    std::array<std::uint32_t,MaterialGraphMaxTextureBindings> flags;
    std::array<std::shared_ptr<const void>,MaterialGraphMaxTextureBindings> leases;
    std::array<std::uint64_t,MaterialGraphMaxTextureBindings> sourceFingerprints{},textureSignatures{};
    MaterialShading shading{MaterialShading::StandardPbr};
    MaterialBlend blend{MaterialBlend::Opaque};
    bool receiveFog{true};
    bool nodePreview{}; // Explicit one-pass editor visualization, independent of preview geometry.
    bool dynamicCoverage{};
    std::uint64_t signature{};
    PreparedGraphMaterial();
    void bind(MaterialEvaluationContext = {}) const;
    std::uint64_t coverageSignature(MaterialEvaluationContext = {}) const;
    bgfx::ProgramHandle program(MaterialGraphPass pass) const;
};

struct MaterialGraphProgramResult {
    std::shared_ptr<const MaterialGraphProgramsVersion> programs;
    std::string diagnostic;
    bool current{};
    bool compiling{}; // Active CPU work, separate from terminal compile/preflight failure.
};

// Render-boundary owner, explicitly composed by ResourceRuntime. Workers only
// compile CPU binaries; every bgfx call and final destructor is on the main thread.
class MaterialGraphPrograms final {
public:
    MaterialGraphPrograms(ResourceManager&,ShaderCompileConfig);
    ~MaterialGraphPrograms();
    std::unique_ptr<MaterialGraphPrograms> createSibling() const;
    MaterialGraphProgramResult request(const MaterialGraphShader&,ResourcePriority priority=ResourcePriority::Preview);
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
