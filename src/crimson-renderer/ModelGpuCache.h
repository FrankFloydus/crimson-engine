#pragma once
#include "resources/StreamingModel.h"
#include <bgfx/bgfx.h>
#include <memory>
#include <string>

namespace crimson {
// GPU-only owner. CPU cache reading/simplification belongs to resource workers.
struct ResidentModelLod {
    bgfx::DynamicVertexBufferHandle vertices{BGFX_INVALID_HANDLE};
    bgfx::DynamicIndexBufferHandle indices{BGFX_INVALID_HANDLE};
    std::uint32_t vertexCount{},indexCount{},level{};
    std::size_t bytes{};
    ~ResidentModelLod();
    void bind() const;
};
class ModelGpuCache {
public:
    ModelGpuCache(); ~ModelGpuCache();
    void configure(const std::string&,std::uint64_t,const streaming::ModelStream&);
    bool queue(const std::string&,std::uint64_t,std::shared_ptr<const ModelGeometryLevel>);
    void advance(std::size_t,double milliseconds);
    bool ready(const std::string&,std::uint64_t) const;
    std::shared_ptr<const ResidentModelLod> resident(const std::string&,std::uint64_t,std::uint32_t primitive,std::uint32_t desired) const;
    const streaming::ModelStream* descriptor(const std::string&,std::uint64_t) const;
    void beginMainView();
    void markRendered(const std::string&,std::uint64_t);
    void evict(const std::string&);
    void trim(std::size_t,std::span<const std::string> liveIds);
    std::size_t bytes() const;
    std::uint64_t signature() const;
    const std::string& status() const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
