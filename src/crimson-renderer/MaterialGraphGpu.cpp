#include "MaterialGraphGpu.h"
#include "resources/ResourceManager.h"
#include "assets/MaterialAsset.h"
#include "resources/CacheIO.h"
#include "vs_model.h"
#include "vs_pick.h"
#include "vs_point_shadow.h"
#include "vs_directional_shadow.h"
#include "vs_particle.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <unordered_map>

namespace crimson {
namespace {
std::span<const std::uint8_t> vertex(MaterialGraphPass pass) {
    switch(pass) {
    case MaterialGraphPass::Model:return cc_vs_model;
    case MaterialGraphPass::Picking:return cc_vs_pick;
    case MaterialGraphPass::PointShadow:return cc_vs_point_shadow;
    case MaterialGraphPass::Particle:return cc_vs_particle;
    case MaterialGraphPass::DirectionalShadow:return cc_vs_directional_shadow;
    }
    throw std::runtime_error("Invalid material graph pass.");
}
bgfx::ProgramHandle createProgram(std::span<const std::uint8_t> vs,std::span<const std::uint8_t> fs) {
    const auto v=bgfx::createShader(bgfx::copy(vs.data(),static_cast<std::uint32_t>(vs.size())));
    if(!bgfx::isValid(v))throw std::runtime_error("Material graph vertex program allocation failed.");
    const auto f=bgfx::createShader(bgfx::copy(fs.data(),static_cast<std::uint32_t>(fs.size())));
    if(!bgfx::isValid(f)){bgfx::destroy(v);throw std::runtime_error("Material graph fragment program allocation failed.");}
    const auto program=bgfx::createProgram(v,f,true);
    if(!bgfx::isValid(program))throw std::runtime_error("Material graph program allocation failed.");
    return program;
}
void preflightBundle(
    std::span<const MaterialGraphShaderVariant> variants,std::span<const ShaderBinary* const> binaries) {
    if(binaries.size()!=variants.size()*2)throw std::runtime_error("Incomplete material graph shader bundle.");
    for(std::size_t i=0;i<variants.size();++i) {
        std::string error;
        if(!preflightGeneratedShaderPair(binaries[2*i]->bytes,binaries[2*i+1]->bytes,vertex(variants[i].pass),variants[i].manifest,error))
            throw std::runtime_error("Material shader validation: "+error);
    }
}
std::shared_ptr<MaterialGraphProgramsVersion> createBundle(
    std::span<const MaterialGraphShaderVariant> variants,std::span<const ShaderBinary* const> binaries) {
    auto candidate=std::make_shared<MaterialGraphProgramsVersion>();
    candidate->signature=streaming::detail::HashSeed;
    candidate->parameters=bgfx::createUniform(MaterialGraphParameterUniform,bgfx::UniformType::Vec4,MaterialGraphMaxParameterRows);
    if(!bgfx::isValid(candidate->parameters))throw std::runtime_error("Material graph uniform allocation failed.");
    for(std::uint16_t i=0;i<MaterialGraphMaxTextureBindings;++i) {
        candidate->samplers[i]=bgfx::createUniform(materialGraphSamplerName(i).c_str(),bgfx::UniformType::Sampler);
        if(!bgfx::isValid(candidate->samplers[i]))throw std::runtime_error("Material graph sampler allocation failed.");
    }
    for(std::size_t i=0;i<variants.size();++i) {
        candidate->programs[static_cast<std::size_t>(variants[i].pass)]=createProgram(binaries[2*i]->bytes,binaries[2*i+1]->bytes);
        candidate->bytes+=binaries[2*i]->bytes.size()+binaries[2*i+1]->bytes.size();
        streaming::detail::hash(candidate->signature,binaries[2*i]->fingerprint);
        streaming::detail::hash(candidate->signature,binaries[2*i+1]->fingerprint);
    }
    return candidate;
}
}
MaterialGraphProgramsVersion::MaterialGraphProgramsVersion(){programs.fill(BGFX_INVALID_HANDLE);samplers.fill(BGFX_INVALID_HANDLE);}
MaterialGraphProgramsVersion::~MaterialGraphProgramsVersion(){
    for(auto handle:programs)if(bgfx::isValid(handle))bgfx::destroy(handle);
    for(auto handle:samplers)if(bgfx::isValid(handle))bgfx::destroy(handle);
    if(bgfx::isValid(parameters))bgfx::destroy(parameters);
}
PreparedGraphMaterial::PreparedGraphMaterial(){textures.fill(BGFX_INVALID_HANDLE);flags.fill(UINT32_MAX);}
void PreparedGraphMaterial::bind(MaterialEvaluationContext context)const {
    if(!programs)return;
    if(!uniforms.empty()) {
        if(runtimeGraph&&!runtimeParameters.empty()) {
            std::array<Vec4,MaterialGraphMaxParameterRows> runtime;
            std::copy(uniforms.begin(),uniforms.end(),runtime.begin());
            try {
                updateMaterialGraphRuntimeUniforms(*runtimeGraph,runtimeParameters,context,std::span{runtime}.first(uniforms.size()));
                bgfx::setUniform(programs->parameters,runtime.data(),static_cast<std::uint16_t>(uniforms.size()));
            } catch(const std::exception&) {
                // Invalid runtime arithmetic retains the complete deterministic packet.
                bgfx::setUniform(programs->parameters,uniforms.data(),static_cast<std::uint16_t>(uniforms.size()));
            }
        } else bgfx::setUniform(programs->parameters,uniforms.data(),static_cast<std::uint16_t>(uniforms.size()));
    }
    for(const auto& source:sources)if(bgfx::isValid(textures[source.slot]))
        bgfx::setTexture(static_cast<std::uint8_t>(source.slot),programs->samplers[source.slot],textures[source.slot],flags[source.slot]);
}
std::uint64_t PreparedGraphMaterial::coverageSignature(MaterialEvaluationContext context)const {
    auto result=signature;
    if(!dynamicCoverage||!runtimeGraph)return result;
    std::array<Vec4,MaterialGraphMaxParameterRows> runtime;
    std::copy(uniforms.begin(),uniforms.end(),runtime.begin());
    try {
        updateMaterialGraphRuntimeUniforms(*runtimeGraph,runtimeParameters,context,std::span{runtime}.first(uniforms.size()));
        for(const auto& parameter:runtimeParameters)if(parameter.coverage) {
            streaming::detail::hash(result,runtime[parameter.row]);
            if(parameter.mapping){streaming::detail::hash(result,runtime[parameter.row+1]);streaming::detail::hash(result,runtime[parameter.row+2]);}
        }
    } catch(const std::exception&) {return signature;}
    return result;
}
bgfx::ProgramHandle PreparedGraphMaterial::program(MaterialGraphPass pass)const{
    if(programs)return programs->programs[static_cast<std::size_t>(pass)];
    return BGFX_INVALID_HANDLE;
}

struct MaterialGraphPrograms::Impl {
    struct Entry {
        std::vector<MaterialGraphShaderVariant> variants;
        std::vector<ResourceHandle<ShaderBinary>> jobs; // Vertex/fragment pairs in pass order.
        std::vector<std::uint64_t> published,failed;
        std::shared_ptr<const MaterialGraphProgramsVersion> current;
        std::chrono::steady_clock::time_point used;
        std::string error;
        std::uint64_t epoch{};
        ResourcePriority priority{ResourcePriority::Background};
    };
    ResourceManager& resources;
    ShaderCompileConfig config;
    std::unordered_map<std::string,Entry> entries;
    std::shared_ptr<std::vector<std::weak_ptr<const MaterialGraphProgramsVersion>>> versions{std::make_shared<std::vector<std::weak_ptr<const MaterialGraphProgramsVersion>>>()};
    std::shared_ptr<std::uint64_t> sourceEpoch{std::make_shared<std::uint64_t>(1)};
    mutable std::uint64_t dependencyEpoch{};
    mutable std::vector<std::filesystem::path> dependencyFiles;
    std::unique_ptr<ShaderCacheReadSession> cacheRead;
    std::uint64_t cacheReadEpoch{};
    Impl(ResourceManager& manager,ShaderCompileConfig settings):resources(manager),config(std::move(settings)){}
};
MaterialGraphPrograms::MaterialGraphPrograms(ResourceManager& manager,ShaderCompileConfig config):impl_(std::make_unique<Impl>(manager,std::move(config))){}
MaterialGraphPrograms::~MaterialGraphPrograms()=default;
std::unique_ptr<MaterialGraphPrograms> MaterialGraphPrograms::createSibling() const {
    auto sibling=std::make_unique<MaterialGraphPrograms>(impl_->resources,impl_->config);
    sibling->impl_->sourceEpoch=impl_->sourceEpoch;
    sibling->impl_->versions=impl_->versions;return sibling;
}
MaterialGraphProgramResult MaterialGraphPrograms::request(const MaterialGraphShader& shader,ResourcePriority priority) {
    auto& owner=*impl_;
    auto& entry=owner.entries[shader.topologyKey];
    entry.used=std::chrono::steady_clock::now();
    try {
        const auto* caps=bgfx::getCaps();
        if(!caps || caps->limits.maxTextureSamplers<16)throw std::runtime_error("Material graphs require sixteen texture sampler stages.");
        const bool promote=priority<entry.priority&&std::any_of(entry.jobs.begin(),entry.jobs.end(),[](const auto& job) {
            const auto state=job.state();return state==ResourceState::Queued||state==ResourceState::Loading||state==ResourceState::Preparing;
        });
        if(entry.epoch!=(*owner.sourceEpoch)||promote) {
            if(shader.nodePreview ? (shader.variants.size()!=1 || shader.variants.front().pass!=MaterialGraphPass::Model) : shader.variants.size()!=5)
                throw std::runtime_error("Material graph pass bundle does not match its material/preview contract.");
            // Reopening unchanged content must not queue a compiler job before
            // discovering that every exact binary is already on disk. Publish
            // only a complete preflighted bundle; a miss keeps the async path.
            if(entry.epoch==0) {
                if(!owner.cacheRead||owner.cacheReadEpoch!=(*owner.sourceEpoch)) {
                    owner.cacheRead=std::make_unique<ShaderCacheReadSession>(owner.config);owner.cacheReadEpoch=(*owner.sourceEpoch);
                }
                auto& cache=*owner.cacheRead;
                std::vector<ShaderBinary> cached;cached.reserve(shader.variants.size()*2);
                for(const auto& variant:shader.variants) {
                    auto vs=cache.find(variant.vertexName);
                    if(!vs){cached.clear();break;}
                    auto fs=cache.find(materialGraphShaderRecipe(owner.config,shader,variant));
                    if(!fs){cached.clear();break;}
                    cached.push_back(std::move(*vs));cached.push_back(std::move(*fs));
                }
                if(!cached.empty()&&cache.unchanged()) {
                    std::vector<const ShaderBinary*> binaries;for(const auto& binary:cached)binaries.push_back(&binary);
                    bool valid=true;
                    try {preflightBundle(shader.variants,binaries);}
                    catch(const std::exception&) {cache.discard(cached);valid=false;}
                    if(valid&&cache.unchanged()) {
                        auto candidate=createBundle(shader.variants,binaries);
                        entry.current=candidate;entry.variants=shader.variants;entry.epoch=(*owner.sourceEpoch);entry.priority=priority;
                        owner.versions->push_back(candidate);return {std::move(candidate),{},true,false};
                    }
                }
            }
            std::vector<ResourceHandle<ShaderBinary>> jobs;
            for(const auto& variant:shader.variants) {
                const auto config=owner.config;
                const auto vertexName=variant.vertexName;
                ResourceRequestOptions vertexOptions;vertexOptions.priority=priority;
                // compileShader validates content and cache identity itself;
                // the resource wrapper only needs to detect concurrent writes.
                vertexOptions.dependencyValidation=DependencyValidation::FileState;
                vertexOptions.variant="material-vertex-"+std::to_string((*owner.sourceEpoch));
                vertexOptions.dependencies=shaderDependencies(config,vertexName);
                jobs.push_back(owner.resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"material-vertex:"+vertexName},
                    [config,vertexName](std::stop_token stop){auto binary=std::make_shared<ShaderBinary>(compileShader(config,vertexName,stop));
                        return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};},std::move(vertexOptions)));
                const auto recipe=materialGraphShaderRecipe(config,shader,variant);
                ResourceRequestOptions options;options.priority=priority;
                options.dependencyValidation=DependencyValidation::FileState;
                options.variant="material-graph-"+std::to_string((*owner.sourceEpoch));
                options.dependencies=shaderDependencies(config,recipe);
                jobs.push_back(owner.resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"material-shader:"+recipe.name},
                    [config,recipe](std::stop_token stop){auto binary=std::make_shared<ShaderBinary>(compileShader(config,recipe,stop));
                        return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};},std::move(options)));
            }
            entry.jobs=std::move(jobs);entry.variants=shader.variants;entry.published.clear();entry.failed.clear();entry.error.clear();entry.epoch=(*owner.sourceEpoch);entry.priority=priority;
        }
        std::vector<ResourceSnapshot<ShaderBinary>> binaries;std::vector<std::uint64_t> revisions;
        bool ready=true;
        for(const auto& job:entry.jobs) {
            if(job.state()==ResourceState::Failed)throw std::runtime_error(job.diagnostic());
            auto snapshot=job.snapshot();if(!snapshot){ready=false;continue;}
            revisions.push_back(snapshot.revision());binaries.push_back(std::move(snapshot));
        }
        if(!ready) {
            const bool compiling=std::any_of(entry.jobs.begin(),entry.jobs.end(),[](const auto& job) {
                const auto state=job.state();return state==ResourceState::Queued||state==ResourceState::Loading||state==ResourceState::Preparing;
            });
            return {entry.current,"Compiling material graph...",false,compiling};
        }
        if(entry.current && revisions==entry.published)return {entry.current,{},true};
        if(revisions==entry.failed)return {entry.current,entry.error,false};
        // A rejected complete result remains rejected until its source epoch changes.
        entry.failed=revisions;
        std::vector<const ShaderBinary*> pointers;for(const auto& binary:binaries)pointers.push_back(&*binary);
        preflightBundle(entry.variants,pointers);
        auto candidate=createBundle(entry.variants,pointers);
        entry.current=candidate;owner.versions->push_back(candidate);entry.published=std::move(revisions);entry.failed.clear();entry.error.clear();
        return {std::move(candidate),{},true};
    } catch(const std::exception& error) {entry.error=error.what();return {entry.current,entry.error,false};}
}
void MaterialGraphPrograms::changed(std::span<const std::filesystem::path> files) {
    if(std::any_of(files.begin(),files.end(),[](const auto& path){return path.extension()==".sc"||path.extension()==".sh";})) {
        ++(*impl_->sourceEpoch);impl_->cacheRead.reset();
    }
}
void MaterialGraphPrograms::trim() {
    const auto now=std::chrono::steady_clock::now();
    std::erase_if(impl_->entries,[&](const auto& item){return now-item.second.used>std::chrono::seconds(5);});
    if(impl_->entries.empty())impl_->cacheRead.reset();
    std::erase_if(*impl_->versions,[](const auto& version){return version.expired();});
}
std::uint64_t MaterialGraphPrograms::revision()const {return (*impl_->sourceEpoch);}
std::vector<std::filesystem::path> MaterialGraphPrograms::sourceDependencies()const {
    if(impl_->dependencyEpoch==(*impl_->sourceEpoch))return impl_->dependencyFiles;
    // The static include family is shared across all graph topologies. Include
    // compiler/vertex/varying inputs in persistent thumbnail cache identity too.
    const auto shader=generateMaterialGraphShader(defaultMaterialAsset());
    std::vector<std::filesystem::path> result;
    for(const auto& variant:shader.variants) {
        const auto fragment=shaderDependencies(impl_->config,materialGraphShaderRecipe(impl_->config,shader,variant));
        const auto vs=shaderDependencies(impl_->config,variant.vertexName);
        result.insert(result.end(),fragment.begin(),fragment.end());result.insert(result.end(),vs.begin(),vs.end());
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());
    impl_->dependencyEpoch=(*impl_->sourceEpoch);impl_->dependencyFiles=result;return result;
}
std::size_t MaterialGraphPrograms::residentBytes()const {
    std::size_t result{};for(const auto& weak:*impl_->versions)if(const auto version=weak.lock())result+=version->bytes;return result;
}
}
