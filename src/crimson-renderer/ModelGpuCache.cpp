#include "ModelGpuCache.h"
#include "ModelDeformationGpu.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
std::string resourceKey(const std::string& id,std::uint64_t revision) { return id+'\x1f'+std::to_string(revision); }
bgfx::VertexLayout modelLayout() {
    static_assert(sizeof(ModelVertex)==27*sizeof(float));
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::Normal,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord1,2,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent,4,bgfx::AttribType::Float).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2,4,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord3,4,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord4,1,bgfx::AttribType::Float).end();
    return layout;
}
struct Bundle {
    std::uint64_t revision{};
    streaming::ModelStream descriptor;
    std::vector<std::vector<std::shared_ptr<ResidentModelLod>>> levels;
};
struct Upload {
    std::string id; std::uint64_t revision{};
    std::shared_ptr<const ModelGeometryLevel> source;
    std::shared_ptr<ResidentModelLod> target;
    std::vector<std::uint16_t> narrow;
    std::size_t vertexOffset{},indexOffset{};
};
struct SharedAdmission {
    std::string id;
    std::uint64_t revision{};
    std::uint32_t primitive{},level{};
    std::shared_ptr<ResidentModelLod> resident;
};
}
ResidentModelLod::~ResidentModelLod() { if (bgfx::isValid(vertices)) bgfx::destroy(vertices); if (bgfx::isValid(indices)) bgfx::destroy(indices); }
void ResidentModelLod::bind() const { bgfx::setVertexBuffer(0,vertices); bgfx::setIndexBuffer(indices); }
struct ModelGpuCache::Impl {
    std::unordered_map<std::string,Bundle> models;
    std::deque<Upload> uploads;
    std::deque<SharedAdmission> sharedAdmissions;
    std::vector<std::weak_ptr<ResidentModelLod>> allocations;
    std::unordered_map<std::uint64_t,std::weak_ptr<ResidentModelLod>> sharedLevels;
    std::unordered_set<std::string> mainView;
    std::unordered_map<std::string,std::uint64_t> renderedRevisions;
    std::uint64_t signature{1}; std::string status;
};
ModelGpuCache::ModelGpuCache():impl_(std::make_unique<Impl>()) {}
ModelGpuCache::~ModelGpuCache()=default;
void ModelGpuCache::configure(const std::string& id,std::uint64_t revision,const streaming::ModelStream& descriptor) {
    const auto key=resourceKey(id,revision);
    if (impl_->models.contains(key)) return;
    if (id.empty() || descriptor.primitives.empty()) throw std::invalid_argument("Model streaming descriptor is empty.");
    Bundle next; next.revision=revision; next.descriptor=descriptor;
    for (const auto& primitive:descriptor.primitives) {
        if (primitive.levels.empty()) throw std::invalid_argument("Model primitive has no streamable LOD.");
        next.levels.emplace_back(primitive.levels.size());
    }
    impl_->models.emplace(key,std::move(next));
}
bool ModelGpuCache::queue(const std::string& id,std::uint64_t revision,std::shared_ptr<const ModelGeometryLevel> source) {
    try {
        const auto found=impl_->models.find(resourceKey(id,revision));
        if (found==impl_->models.end() || found->second.revision!=revision || !source || source->primitive>=found->second.levels.size()) return false;
        auto& levels=found->second.levels[source->primitive];
        if (source->lodIndex>=levels.size()) return false;
        if (levels[source->lodIndex] ||
            std::any_of(impl_->uploads.begin(),impl_->uploads.end(),[&](const auto& upload){return upload.id==id && upload.revision==revision && upload.source->primitive==source->primitive && upload.source->lodIndex==source->lodIndex;}) ||
            std::any_of(impl_->sharedAdmissions.begin(),impl_->sharedAdmissions.end(),[&](const auto& admission){return admission.id==id && admission.revision==revision && admission.primitive==source->primitive && admission.level==source->lodIndex;})) return true;
        const auto& geometry=source->geometry;
        const auto& expected=found->second.descriptor.primitives[source->primitive].levels[source->lodIndex];
        if (geometry.vertices.size()!=expected.vertexCount || geometry.indices.size()!=expected.indexCount || geometry.indices.empty() ||
            geometry.vertices.size()>UINT32_MAX/sizeof(ModelVertex) || geometry.indices.size()>UINT32_MAX/sizeof(std::uint32_t)) throw std::runtime_error("Streamed LOD geometry does not match its descriptor.");
        const auto sharedKey=expected.key^(static_cast<std::uint64_t>(source->lodIndex)<<32);
        if(const auto cached=impl_->sharedLevels.find(sharedKey);cached!=impl_->sharedLevels.end())
            if(auto shared=cached->second.lock();shared && shared->vertexCount==expected.vertexCount && shared->indexCount==expected.indexCount && shared->level==source->lodIndex) {
                // Receiver preparation for another camera must not change a
                // frame's resident set. Reused levels publish at the same next
                // advance boundary as completed uploads, with no byte transfer.
                impl_->sharedAdmissions.push_back({id,revision,source->primitive,source->lodIndex,std::move(shared)});
                impl_->status.clear();return true;
            }
        const auto maximum=source->validated ? 0u:*std::max_element(geometry.indices.begin(),geometry.indices.end());
        if (!source->validated && maximum>=geometry.vertices.size()) throw std::runtime_error("Streamed LOD has an invalid vertex reference.");
        const bool wide=source->validated ? source->wideIndices:maximum>UINT16_MAX;
        if (wide && !(bgfx::getCaps()->supported&BGFX_CAPS_INDEX32)) throw std::runtime_error("GPU does not support streamed 32-bit model indices.");
        auto target=std::make_shared<ResidentModelLod>(); target->level=source->lodIndex;
        target->vertexCount=static_cast<std::uint32_t>(geometry.vertices.size()); target->indexCount=static_cast<std::uint32_t>(geometry.indices.size());
        target->bytes=geometry.vertices.size()*sizeof(ModelVertex)+geometry.indices.size()*(wide ? 4:2);
        target->deformation=createModelDeformationSource(geometry);
        if(target->deformation)target->bytes+=target->deformation->bytes;
        const auto flags=target->deformation ? BGFX_BUFFER_COMPUTE_READ|BGFX_BUFFER_COMPUTE_FORMAT_32X1|BGFX_BUFFER_COMPUTE_TYPE_FLOAT:BGFX_BUFFER_NONE;
        target->vertices=bgfx::createDynamicVertexBuffer(target->vertexCount,modelLayout(),flags);
        target->indices=bgfx::createDynamicIndexBuffer(target->indexCount,wide ? BGFX_BUFFER_INDEX32:BGFX_BUFFER_NONE);
        if (!bgfx::isValid(target->vertices) || !bgfx::isValid(target->indices)) throw std::runtime_error("Streamed model GPU allocation failed.");
        impl_->allocations.push_back(target);
        Upload upload{id,revision,std::move(source),std::move(target),{},0,0};
        if (!wide && upload.source->indices16.empty()) upload.narrow.assign(geometry.indices.begin(),geometry.indices.end());
        impl_->uploads.push_back(std::move(upload)); impl_->status.clear(); return true;
    } catch (const std::exception& error) { impl_->status=error.what(); return false; }
}
void ModelGpuCache::advance(std::size_t remaining,double milliseconds) {
    const auto started=std::chrono::steady_clock::now();
    while(!impl_->sharedAdmissions.empty() &&
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<milliseconds) {
        auto admission=std::move(impl_->sharedAdmissions.front());impl_->sharedAdmissions.pop_front();
        const auto found=impl_->models.find(resourceKey(admission.id,admission.revision));
        if(found==impl_->models.end() || found->second.revision!=admission.revision || admission.primitive>=found->second.levels.size())continue;
        auto& levels=found->second.levels[admission.primitive];
        if(admission.level>=levels.size() || levels[admission.level])continue;
        levels[admission.level]=std::move(admission.resident);
        if(impl_->mainView.contains(admission.id))++impl_->signature;
    }
    while (!impl_->uploads.empty() && remaining>=sizeof(ModelVertex) &&
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<milliseconds) {
        auto& upload=impl_->uploads.front(); const auto found=impl_->models.find(resourceKey(upload.id,upload.revision));
        if (found==impl_->models.end() || found->second.revision!=upload.revision) { impl_->uploads.pop_front(); continue; }
        const auto& geometry=upload.source->geometry;
        if (upload.vertexOffset<geometry.vertices.size()) {
            const auto count=std::min({geometry.vertices.size()-upload.vertexOffset,remaining/sizeof(ModelVertex),std::size_t{65536}});
            const auto bytes=count*sizeof(ModelVertex);
            bgfx::update(upload.target->vertices,static_cast<std::uint32_t>(upload.vertexOffset),bgfx::copy(geometry.vertices.data()+upload.vertexOffset,static_cast<std::uint32_t>(bytes)));
            upload.vertexOffset+=count; remaining-=bytes;
        } else if (upload.target->deformation && !upload.target->deformation->ready()) {
            remaining-=uploadModelDeformationSource(*upload.target->deformation,geometry,remaining);
        } else if (upload.indexOffset<geometry.indices.size()) {
            const auto& narrow=upload.source->indices16.empty() ? upload.narrow:upload.source->indices16;
            const auto stride=narrow.empty() ? 4u:2u;
            const auto count=std::min({geometry.indices.size()-upload.indexOffset,remaining/stride,std::size_t{262144}});
            if (!count) break;
            const void* data=narrow.empty() ? static_cast<const void*>(geometry.indices.data()+upload.indexOffset):static_cast<const void*>(narrow.data()+upload.indexOffset);
            bgfx::update(upload.target->indices,static_cast<std::uint32_t>(upload.indexOffset),bgfx::copy(data,static_cast<std::uint32_t>(count*stride)));
            upload.indexOffset+=count; remaining-=count*stride;
        }
        if (upload.vertexOffset==geometry.vertices.size() && upload.indexOffset==geometry.indices.size() &&
            (!upload.target->deformation || upload.target->deformation->ready())) {
            const auto& info=found->second.descriptor.primitives[upload.source->primitive].levels[upload.source->lodIndex];
            impl_->sharedLevels[info.key^(static_cast<std::uint64_t>(upload.source->lodIndex)<<32)]=upload.target;
            found->second.levels[upload.source->primitive][upload.source->lodIndex]=std::move(upload.target);
            const bool visible=impl_->mainView.contains(upload.id);
            impl_->uploads.pop_front(); if (visible) ++impl_->signature;
        }
    }
}
bool ModelGpuCache::ready(const std::string& id,std::uint64_t revision) const {
    const auto found=impl_->models.find(resourceKey(id,revision));
    return found!=impl_->models.end() && found->second.revision==revision &&
        std::all_of(found->second.levels.begin(),found->second.levels.end(),[](const auto& levels){return std::any_of(levels.begin(),levels.end(),[](const auto& level){return !!level;});});
}
std::shared_ptr<const ResidentModelLod> ModelGpuCache::resident(const std::string& id,std::uint64_t revision,std::uint32_t primitive,std::uint32_t desired) const {
    const auto found=impl_->models.find(resourceKey(id,revision));
    if (found==impl_->models.end() || found->second.revision!=revision || primitive>=found->second.levels.size()) return {};
    const auto& levels=found->second.levels[primitive]; desired=std::min(desired,static_cast<std::uint32_t>(levels.size()-1));
    for (std::size_t i=desired;i<levels.size();++i) if (levels[i]) return levels[i];
    for (std::size_t i=desired;i-->0;) if (levels[i]) return levels[i];
    return {};
}
const streaming::ModelStream* ModelGpuCache::descriptor(const std::string& id,std::uint64_t revision) const {
    const auto found=impl_->models.find(resourceKey(id,revision)); return found!=impl_->models.end() ? &found->second.descriptor:nullptr;
}
void ModelGpuCache::beginMainView() { impl_->mainView.clear(); }
void ModelGpuCache::markRendered(const std::string& id,std::uint64_t revision,bool affectsPicking) {
    if(affectsPicking)impl_->mainView.insert(id);
    // Resource revisions increase across reloads. Another view can still draw
    // its last-good packet, but must not make an older revision current again.
    auto& rendered=impl_->renderedRevisions[id];rendered=std::max(rendered,revision);
}
void ModelGpuCache::evict(const std::string& id) {
    std::erase_if(impl_->uploads,[&](const auto& upload){return upload.id==id;});
    std::erase_if(impl_->sharedAdmissions,[&](const auto& admission){return admission.id==id;});
    if(std::erase_if(impl_->models,[&](const auto& entry){return entry.second.descriptor.id==id;}) && impl_->mainView.contains(id)) ++impl_->signature;
}
void ModelGpuCache::trim(std::size_t target,std::span<const std::string> live) {
    std::erase_if(impl_->allocations,[](const auto& allocation){return allocation.expired();});
    std::erase_if(impl_->sharedLevels,[](const auto& entry){return entry.second.expired();});
    const auto stale=[&](const Bundle& bundle) {
        const auto rendered=impl_->renderedRevisions.find(bundle.descriptor.id);
        // A newer candidate has not drawn yet: retain its uploads/admissions
        // until advance can publish them. Only superseded versions are stale.
        return rendered!=impl_->renderedRevisions.end() && bundle.revision<rendered->second;
    };
    std::erase_if(impl_->uploads,[&](const auto& upload) {const auto found=impl_->models.find(resourceKey(upload.id,upload.revision));return found!=impl_->models.end() && stale(found->second);});
    std::erase_if(impl_->sharedAdmissions,[&](const auto& admission) {
        const auto found=impl_->models.find(resourceKey(admission.id,admission.revision));
        return found==impl_->models.end() || stale(found->second);
    });
    std::unordered_map<const ResidentModelLod*,std::size_t> owned;
    for(const auto& [key,bundle]:impl_->models) { (void)key;for(const auto& levels:bundle.levels)for(const auto& level:levels)if(level)++owned[level.get()]; }
    for(const auto& admission:impl_->sharedAdmissions)++owned[admission.resident.get()];
    std::erase_if(impl_->models,[&](const auto& item) {
        return stale(item.second) && std::all_of(item.second.levels.begin(),item.second.levels.end(),[&](const auto& levels){return std::all_of(levels.begin(),levels.end(),[&](const auto& level){return !level || static_cast<std::size_t>(level.use_count())<=owned[level.get()];});});
    });
    if (bytes()<=target) return;
    const auto used=[&](const std::string& id){return std::find(live.begin(),live.end(),id)!=live.end();};
    std::erase_if(impl_->uploads,[&](const auto& upload){return !used(upload.id);});
    std::erase_if(impl_->sharedAdmissions,[&](const auto& admission){return !used(admission.id);});
    std::erase_if(impl_->models,[&](const auto& pair){return !used(pair.second.descriptor.id);});
    auto total=bytes();
    for (auto& [key,bundle]:impl_->models) {
        (void)key;
        for (auto& levels:bundle.levels) for (std::size_t i=0;i+1<levels.size() && total>target;++i)
            if (levels[i] && levels[i].use_count()==1 && std::any_of(levels.begin()+i+1,levels.end(),[](const auto& level){return !!level;})) {
                total-=levels[i]->bytes; levels[i].reset(); if(impl_->mainView.contains(bundle.descriptor.id)) ++impl_->signature;
            }
    }
}
std::size_t ModelGpuCache::bytes() const {
    std::size_t result=0;
    for (const auto& allocation:impl_->allocations) if (const auto level=allocation.lock()) result+=level->bytes;
    return result;
}
std::uint64_t ModelGpuCache::signature() const { return impl_->signature; }
const std::string& ModelGpuCache::status() const { return impl_->status; }
}
