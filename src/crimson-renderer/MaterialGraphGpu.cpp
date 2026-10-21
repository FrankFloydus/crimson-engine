#include "MaterialGraphGpu.h"
#include "resources/ResourceManager.h"
#include "assets/MaterialAsset.h"
#include "resources/CacheIO.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace crimson {
namespace {
using StageSet=std::array<ShaderBinary,3>; // Conventional vertex, fragment, optional domain.
std::uint64_t binaryFingerprint(std::span<const std::uint8_t> bytes) {
    std::uint64_t result=14695981039346656037ull;
    for(const auto byte:bytes){result^=byte;result*=1099511628211ull;}
    return result;
}
std::vector<GraphProgramBinary> graph_binaries(std::span<const MaterialGraphShaderVariant> variants,std::span<const ShaderBinary* const> binaries) {
    if(binaries.size()!=variants.size()*3)throw std::runtime_error("Incomplete material graph shader bundle.");
    std::vector<GraphProgramBinary> result;result.reserve(variants.size());
    for(std::size_t i=0;i<variants.size();++i) {
        const auto* vs=binaries[3*i];const auto* fs=binaries[3*i+1];
        if(!vs||!fs)throw std::runtime_error("Incomplete material graph shader pair.");
        GraphProgramBinary program{vs->bytes,fs->bytes,vs->fingerprint,fs->fingerprint};
        if(variants[i].tessellation) {
            const auto* ds=binaries[3*i+2];
            if(!ds)throw std::runtime_error("Incomplete material tessellation domain stage.");
            program.hull=material_tessellation_hull_shader();program.domain=ds->bytes;
            program.hull_fingerprint=binaryFingerprint(program.hull);program.domain_fingerprint=ds->fingerprint;
        }
        result.push_back(program);
    }
    return result;
}
void preflightBundle(std::span<const MaterialGraphShaderVariant> variants,std::span<const ShaderBinary* const> binaries) {preflight_graph_programs(variants,graph_binaries(variants,binaries));}
std::shared_ptr<MaterialGraphProgramsVersion> createBundle(std::span<const MaterialGraphShaderVariant> variants,std::span<const ShaderBinary* const> binaries) {return create_graph_programs(variants,graph_binaries(variants,binaries));}
}
struct MaterialGraphPrograms::Impl {
    struct ForegroundInterest {};
    struct OptimizationInterest {
        std::shared_ptr<std::stop_source> cancellation{std::make_shared<std::stop_source>()};
        std::uint64_t attempt{};
        ~OptimizationInterest(){cancellation->request_stop();}
    };
    struct Batch {
        std::vector<ResourceHandle<ShaderBinary>> jobs; // Three slots per pass; domain is optional.
        std::vector<std::optional<StageSet>> cached;
        std::vector<std::filesystem::path> sourceFiles;
        std::uint64_t sourceState{};
        std::shared_ptr<ForegroundInterest> foreground;
        std::shared_ptr<OptimizationInterest> optimization;
        std::shared_ptr<std::stop_source> cancellation;
        std::uint64_t attempt{};
        bool background{},interactive{};
    };
    struct Entry {
        std::vector<MaterialGraphShaderVariant> variants;
        std::unique_ptr<Batch> batch;
        std::shared_ptr<const MaterialGraphProgramsVersion> current;
        std::chrono::steady_clock::time_point used,optimizeAfter;
        std::string error;
        std::uint64_t epoch{},currentEpoch{};
        ResourcePriority priority{ResourcePriority::Background};
        bool failed{};
    };
    struct SourceInputs {
        std::uint64_t dependencyEpoch{},cacheReadEpoch{};
        std::vector<std::filesystem::path> dependencyFiles;
        std::unique_ptr<ShaderCacheReadSession> cacheRead;
        std::vector<std::weak_ptr<ForegroundInterest>> foreground;
        std::weak_ptr<OptimizationInterest> optimization;
        std::chrono::steady_clock::time_point lastEdit;
        bool foregroundBusy() {
            std::erase_if(foreground,[](const auto& interest){return interest.expired();});
            return !foreground.empty();
        }
        void interruptOptimizations() {
            if(const auto active=optimization.lock())active->cancellation->request_stop();
            optimization.reset();
        }
    };
    ResourceManager& resources;
    ShaderCompileConfig config;
    std::unordered_map<std::string,Entry> entries;
    std::shared_ptr<std::vector<std::weak_ptr<const MaterialGraphProgramsVersion>>> versions{std::make_shared<std::vector<std::weak_ptr<const MaterialGraphProgramsVersion>>>()};
    std::shared_ptr<std::uint64_t> sourceEpoch{std::make_shared<std::uint64_t>(1)};
    std::shared_ptr<SourceInputs> inputs{std::make_shared<SourceInputs>()};
    Impl(ResourceManager& manager,ShaderCompileConfig settings):resources(manager),config(std::move(settings)){}
};
MaterialGraphPrograms::MaterialGraphPrograms(ResourceManager& manager,ShaderCompileConfig config):impl_(std::make_unique<Impl>(manager,std::move(config))){}
MaterialGraphPrograms::~MaterialGraphPrograms()=default;
std::unique_ptr<MaterialGraphPrograms> MaterialGraphPrograms::createSibling() const {
    auto sibling=std::make_unique<MaterialGraphPrograms>(impl_->resources,impl_->config);
    sibling->impl_->sourceEpoch=impl_->sourceEpoch;
    sibling->impl_->inputs=impl_->inputs;
    sibling->impl_->versions=impl_->versions;return sibling;
}
MaterialGraphProgramResult MaterialGraphPrograms::request(const MaterialGraphShader& shader,ResourcePriority priority) {
    constexpr auto quietInterval=std::chrono::milliseconds(750);
    auto& owner=*impl_;auto& inputs=*owner.inputs;
    auto& entry=owner.entries[shader.topologyKey];
    const auto now=std::chrono::steady_clock::now();entry.used=now;
    const std::string capabilityDiagnostic=shader.tessellation.enabled&&!bgfx::isTessellationSupported()?
        "Hardware tessellation requires Direct3D 12; this backend renders the base mesh.":"";
    const auto current=[&]{return entry.current&&entry.currentEpoch==(*owner.sourceEpoch);};
    const auto pending=[&](bool active) {return MaterialGraphProgramResult{entry.current,current()?capabilityDiagnostic:"Compiling material graph...",current(),active};};
    const auto interactivePass=[](const auto& variant){return variant.pass==MaterialGraphPass::Model||variant.pass==MaterialGraphPass::Particle||variant.pass==MaterialGraphPass::ModelTransmission||variant.pass==MaterialGraphPass::ParticleTransmission||variant.pass==MaterialGraphPass::ModelReflection||variant.pass==MaterialGraphPass::ParticleReflection;};
    try {
        const auto* caps=bgfx::getCaps();
        if(!caps || caps->limits.maxTextureSamplers<16)throw std::runtime_error("Material graphs require sixteen texture sampler stages.");
        // A shared stop source reaches running shaderc processes, not merely
        // queued requests, so background optimization promptly yields both CPU
        // workers when another graph needs its first usable generation.
        const auto queue=[&](Impl::Batch& batch,ResourcePriority jobPriority) {
            std::vector<std::filesystem::path> dependencies;
            if(std::any_of(batch.cached.begin(),batch.cached.end(),[](const auto& pair){return !pair;}))dependencies=sourceDependencies();
            if(batch.sourceFiles.empty()&&!dependencies.empty()) {
                batch.sourceFiles=dependencies;batch.sourceState=fingerprintFileStates(batch.sourceFiles);
            }
            for(std::size_t i=0;i<entry.variants.size();++i) {
                if(batch.cached[i])continue;
                const auto& variant=entry.variants[i];const auto config=owner.config;
                const auto vertexName=variant.vertexName;
                ResourceRequestOptions vertexOptions;vertexOptions.priority=jobPriority;
                vertexOptions.dependencyValidation=DependencyValidation::FileState;
                vertexOptions.variant="material-vertex-"+std::to_string((*owner.sourceEpoch));
                vertexOptions.dependencies=dependencies;
                batch.jobs[3*i]=owner.resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"material-vertex:"+vertexName},
                    [config,vertexName](std::stop_token stop){auto binary=std::make_shared<ShaderBinary>(compileShader(config,vertexName,stop));
                        return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};},std::move(vertexOptions));
                const auto requestGenerated=[&](ShaderCompileRecipe recipe,std::size_t slot) {
                    if(!batch.background&&interactivePass(variant))recipe.optimization=ShaderCompileOptimization::Interactive;
                    ResourceRequestOptions options;options.priority=jobPriority;options.dependencyValidation=DependencyValidation::FileState;
                    options.variant="material-graph-"+std::to_string((*owner.sourceEpoch))+(batch.background?"-optimized-"+std::to_string(batch.attempt):
                        recipe.optimization==ShaderCompileOptimization::Interactive?"-interactive":"-optimized");
                    options.dependencies=dependencies;options.retainLoader=!batch.background;
                    const auto cancellation=batch.cancellation;
                    batch.jobs[3*i+slot]=owner.resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"material-shader:"+recipe.name},
                        [config,recipe,cancellation](std::stop_token stop) {
                            std::shared_ptr<ShaderBinary> binary;
                            if(cancellation) {
                                std::stop_source combined;
                                std::stop_callback resourceStop{stop,[&]{combined.request_stop();}};
                                std::stop_callback backgroundStop{cancellation->get_token(),[&]{combined.request_stop();}};
                                binary=std::make_shared<ShaderBinary>(compileShader(config,recipe,combined.get_token()));
                            } else binary=std::make_shared<ShaderBinary>(compileShader(config,recipe,stop));
                            return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};},std::move(options));
                };
                requestGenerated(materialGraphShaderRecipe(config,shader,variant),1);
                if(variant.tessellation)requestGenerated(materialGraphDomainShaderRecipe(config,shader,variant),2);
            }
        };
        const auto begin=[&](bool background) {
            auto batch=std::make_unique<Impl::Batch>();batch->background=background;
            batch->cached.resize(entry.variants.size());batch->jobs.resize(entry.variants.size()*3);
            if(!inputs.cacheRead||inputs.cacheReadEpoch!=(*owner.sourceEpoch)) {
                inputs.cacheRead=std::make_unique<ShaderCacheReadSession>(owner.config);inputs.cacheReadEpoch=(*owner.sourceEpoch);
            }
            auto& cache=*inputs.cacheRead;
            for(std::size_t i=0;i<entry.variants.size();++i) {
                const auto& variant=entry.variants[i];
                if(!background&&entry.epoch!=0) {batch->interactive|=interactivePass(variant);continue;}
                auto vs=cache.find(variant.vertexName);
                // Optimized disk binaries always win, including on cold app
                // startup. Only genuinely missing pairs use the quick compiler.
                const auto readPair=[&](ShaderCompileOptimization optimization) {
                    if(!vs)return false;
                    auto recipe=materialGraphShaderRecipe(owner.config,shader,variant);recipe.optimization=optimization;
                    auto fs=cache.find(recipe);if(!fs)return false;
                    StageSet stages{*vs,std::move(*fs),{}};
                    if(variant.tessellation) {
                        auto domainRecipe=materialGraphDomainShaderRecipe(owner.config,shader,variant);domainRecipe.optimization=optimization;
                        auto ds=cache.find(domainRecipe);if(!ds)return false;
                        stages[2]=std::move(*ds);
                    }
                    const std::array<const ShaderBinary*,3> binaries{&stages[0],&stages[1],variant.tessellation?&stages[2]:nullptr};
                    try {preflightBundle(std::span{&variant,1},binaries);batch->cached[i]=std::move(stages);return true;}
                    catch(const std::exception&) {cache.discard(stages);return false;}
                };
                if(readPair(ShaderCompileOptimization::Full))continue;
                if(!background&&interactivePass(variant)) {batch->interactive=true;(void)readPair(ShaderCompileOptimization::Interactive);}
            }
            if(!cache.unchanged()) {
                ++(*owner.sourceEpoch);inputs.cacheRead.reset();inputs.interruptOptimizations();return false;
            }
            if(background) {
                auto active=inputs.optimization.lock();
                if(!active||active->cancellation->stop_requested()) {
                    static std::atomic_uint64_t nextAttempt{};
                    active=std::make_shared<Impl::OptimizationInterest>();active->attempt=nextAttempt.fetch_add(1,std::memory_order_relaxed)+1;inputs.optimization=active;
                }
                batch->optimization=active;batch->attempt=active->attempt;batch->cancellation=active->cancellation;
            } else if(batch->interactive||std::any_of(batch->cached.begin(),batch->cached.end(),[](const auto& pair){return !pair;})) {
                inputs.lastEdit=now;inputs.interruptOptimizations();
                batch->foreground=std::make_shared<Impl::ForegroundInterest>();inputs.foreground.push_back(batch->foreground);
            }
            queue(*batch,background?ResourcePriority::Background:priority);entry.batch=std::move(batch);return true;
        };
        if(entry.epoch!=(*owner.sourceEpoch)) {
            if(shader.nodePreview ? (shader.variants.size()!=1 || shader.variants.front().pass!=MaterialGraphPass::Model) : shader.variants.size()!=5u+(shader.refraction?2u:0u)+(shader.reflection?2u:0u))
                throw std::runtime_error("Material graph pass bundle does not match its material/preview contract.");
            if(!shader.nodePreview) {
                std::vector expected{MaterialGraphPass::Model,MaterialGraphPass::Picking,MaterialGraphPass::PointShadow,MaterialGraphPass::Particle,
                    MaterialGraphPass::DirectionalShadow};
                if(shader.refraction){expected.push_back(MaterialGraphPass::ModelTransmission);expected.push_back(MaterialGraphPass::ParticleTransmission);}
                if(shader.reflection){expected.push_back(MaterialGraphPass::ModelReflection);expected.push_back(MaterialGraphPass::ParticleReflection);}
                for(std::size_t index=0;index<shader.variants.size();++index)
                    if(shader.variants[index].pass!=expected[index])throw std::runtime_error("Material graph contains missing, repeated or reordered program variants.");
            }
            entry.batch.reset();entry.variants=shader.variants;
            // Native hull/domain execution is a DX12 capability. Other current
            // backends retain their ordinary graph programs without compiling
            // unsupported stages or replacing displacement with parallax.
            if(bgfx::getRendererType()!=bgfx::RendererType::Direct3D12)
                for(auto& variant:entry.variants)variant.tessellation=false;
            entry.failed=false;entry.error.clear();entry.priority=priority;
            if(!begin(false))return pending(true);
            entry.epoch=(*owner.sourceEpoch);
        } else if(entry.batch&&!entry.batch->background&&priority<entry.priority) {
            entry.priority=priority;queue(*entry.batch,priority);
        }
        if(entry.batch&&entry.batch->background&&entry.batch->cancellation->stop_requested()) {
            entry.batch.reset();entry.optimizeAfter=now+quietInterval;
        }
        if(!entry.batch&&current()&&!entry.current->optimized&&!entry.failed &&
            now>=entry.optimizeAfter&&now-inputs.lastEdit>=quietInterval&&!inputs.foregroundBusy()) {
            if(!begin(true))return pending(true);
        }
        if(!entry.batch)return {entry.current,entry.error.empty()?capabilityDiagnostic:entry.error,current(),false};
        auto& batch=*entry.batch;
        std::vector<ResourceSnapshot<ShaderBinary>> snapshots;
        std::vector<const ShaderBinary*> pointers;
        bool ready=true,active=false;
        for(std::size_t i=0;i<batch.jobs.size();++i) {
            if(i%3==2&&!entry.variants[i/3].tessellation) {pointers.push_back(nullptr);continue;}
            if(batch.cached[i/3]) {pointers.push_back(&(*batch.cached[i/3])[i%3]);continue;}
            const auto& job=batch.jobs[i];const auto state=job.state();
            if(state==ResourceState::Failed)throw std::runtime_error(job.diagnostic());
            active|=state==ResourceState::Queued||state==ResourceState::Loading||state==ResourceState::Preparing;
            auto snapshot=job.snapshot();if(state!=ResourceState::Ready||!snapshot){ready=false;continue;}
            pointers.push_back(&*snapshot);snapshots.push_back(std::move(snapshot));
        }
        if(!ready)return pending(active);
        if((inputs.cacheRead&&!inputs.cacheRead->unchanged()) ||
            (!batch.sourceFiles.empty()&&fingerprintFileStates(batch.sourceFiles)!=batch.sourceState)) {
            ++(*owner.sourceEpoch);inputs.cacheRead.reset();inputs.interruptOptimizations();return pending(true);
        }
        for(std::size_t i=0;i<entry.variants.size();++i)if(!batch.cached[i])
            preflightBundle(std::span{&entry.variants[i],1},std::span{pointers}.subspan(3*i,3));
        auto candidate=createBundle(entry.variants,pointers);candidate->optimized=!batch.interactive;
        entry.current=candidate;entry.currentEpoch=(*owner.sourceEpoch);owner.versions->push_back(candidate);
        entry.optimizeAfter=now+quietInterval;entry.batch.reset();entry.error.clear();entry.failed=false;
        return {std::move(candidate),capabilityDiagnostic,true,false};
    } catch(const std::exception& error) {
        entry.error=(entry.batch&&entry.batch->background?"Material optimization: ":"")+std::string(error.what());entry.failed=true;entry.batch.reset();
        return {entry.current,entry.error,current(),false};
    }
}
void MaterialGraphPrograms::changed(std::span<const std::filesystem::path> files) {
    if(std::any_of(files.begin(),files.end(),[](const auto& path){return path.extension()==".sc"||path.extension()==".sh";})) {
        ++(*impl_->sourceEpoch);impl_->inputs->cacheRead.reset();impl_->inputs->interruptOptimizations();
    }
}
void MaterialGraphPrograms::trim() {
    const auto now=std::chrono::steady_clock::now();
    std::erase_if(impl_->entries,[&](const auto& item){return now-item.second.used>std::chrono::seconds(5);});
    if(impl_->entries.empty()&&impl_->inputs.use_count()==1)impl_->inputs->cacheRead.reset();
    std::erase_if(*impl_->versions,[](const auto& version){return version.expired();});
}
std::uint64_t MaterialGraphPrograms::revision()const {return (*impl_->sourceEpoch);}
std::vector<std::filesystem::path> MaterialGraphPrograms::sourceDependencies()const {
    auto& inputs=*impl_->inputs;
    if(inputs.dependencyEpoch==(*impl_->sourceEpoch))return inputs.dependencyFiles;
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
    inputs.dependencyEpoch=(*impl_->sourceEpoch);inputs.dependencyFiles=result;return result;
}
std::size_t MaterialGraphPrograms::residentBytes()const {
    std::size_t result{};for(const auto& weak:*impl_->versions)if(const auto version=weak.lock())result+=version->bytes;return result;
}
}
