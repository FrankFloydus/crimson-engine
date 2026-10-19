#pragma once
#include <crimson/renderer/forward/PreparedMaterial.h>
#include "MaterialGraphShader.h"
#include "resources/ResourceManager.h"
namespace crimson {
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
