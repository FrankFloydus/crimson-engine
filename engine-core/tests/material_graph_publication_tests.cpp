#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Renderer.h"
#include "editor/MaterialPreview.h"
#include "editor/PreparedMaterialSignature.h"
#include "resources/ResourceManager.h"
#include <bgfx/platform.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <nlohmann/json.hpp>

namespace {
using namespace crimson;
using namespace std::chrono_literals;
namespace fs=std::filesystem;

void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }

struct TemporaryProject {
    fs::path root=fs::temp_directory_path()/("crimson-graph-publication-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryProject() {
        std::error_code error;
        const auto parent=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-graph-publication-"))fs::remove_all(target,error);
    }
};

std::string read(const fs::path& file) {
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Could not read shader fixture: "+file.string());
    return {std::istreambuf_iterator<char>(input),{}};
}

template<class Predicate> void waitFor(Predicate ready,const char* diagnostic,std::chrono::seconds timeout=90s) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(!ready()) {
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error(diagnostic);
        std::this_thread::sleep_for(2ms);
    }
}

template<class Predicate> void waitForCompilation(ResourceManager& resources,Predicate ready,const char* phase,std::chrono::seconds timeout=180s) {
    const auto started=std::chrono::steady_clock::now();
    const auto before=shaderCompilerMetrics();const auto initial=resources.stats();
    const auto timeoutMessage=std::string("Timed out waiting for ")+phase+".";
    try {waitFor(std::move(ready),timeoutMessage.c_str(),timeout);}
    catch(const std::exception& error) {
        const auto stats=resources.stats();const auto metrics=shaderCompilerMetrics();
        throw std::runtime_error(std::string(error.what())+" [phase="+phase+
            ", elapsedMs="+std::to_string(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count())+
            ", queued="+std::to_string(stats.queued)+", running="+std::to_string(stats.running)+
            ", completedLoads="+std::to_string(stats.loads-initial.loads)+", failedLoads="+std::to_string(stats.failures-initial.failures)+
            ", compilerInvocations="+std::to_string(metrics.compilerInvocations-before.compilerInvocations)+
            ", completedCompilerMs="+std::to_string((metrics.compilerNanoseconds-before.compilerNanoseconds)/1e6)+"]");
    }
}

MaterialAsset fixture() {
    MaterialAsset material;material.id="publication-fixture";material.name="Publication fixture";
    const auto output=addMaterialNode(material.graph,MaterialNodeKind::Output).id;
    findMaterialNode(material.graph,output)->output.blend=MaterialBlend::Mask;
    const auto scalar=addMaterialNode(material.graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(material.graph,scalar)->scalar=.7f;
    const auto texture=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,texture)->texture.image.id="assets/textures/publication.png";
    connectMaterialNodes(material.graph,scalar,MaterialPin::Value,output,MaterialPin::Roughness);
    connectMaterialNodes(material.graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(material.graph,texture,MaterialPin::A,output,MaterialPin::OpacityMask);
    return material;
}
void preparedSignatureChecks() {
    auto material=fixture();canonicalizeMaterialAsset(material);
    ModelAsset asset;asset.id="prepared-signature-model";
    std::vector<ModelMaterial> surfaces{material.surface};std::vector<ModelMaterialTextures> bindings(1);
    auto graph=std::make_shared<PreparedGraphMaterial>();graph->signature=71;graph->blend=MaterialBlend::Mask;bindings.front().graph=graph;
    const auto signature=[&]{return preparedModelSignature(asset,3,surfaces,bindings);};
    const auto original=signature();
    auto layout=material;layout.graph.nodes.front().position={123,456};canonicalizeMaterialAsset(layout);surfaces.front()=layout.surface;
    check(signature()==original,"Graph layout or immutable allocation identity changed a prepared render signature.");
    graph->signature=72;check(signature()!=original,"Changed graph program/uniform/texture packet did not replace prepared materials.");graph->signature=71;
    graph->blend=MaterialBlend::Opaque;check(signature()!=original,"Graph blend state was omitted from prepared identity.");graph->blend=MaterialBlend::Mask;
    graph->shading=MaterialShading::Unlit;check(signature()!=original,"Graph shading state was omitted from prepared identity.");graph->shading=MaterialShading::StandardPbr;
    graph->receiveFog=false;check(signature()!=original,"Graph fog reception was omitted from prepared identity.");graph->receiveFog=true;
    graph->refraction=true;check(signature()!=original,"Graph refraction composition was omitted from prepared identity.");graph->refraction=false;
    surfaces.front().doubleSided=!surfaces.front().doubleSided;check(signature()!=original,"Double-sided draw state did not replace prepared materials.");surfaces.front()=material.surface;
    surfaces.front().receiveFog=false;check(signature()!=original,"Fallback fog reception was omitted from prepared identity.");surfaces.front()=material.surface;
    surfaces.front().alphaCutoff+=.1f;check(signature()!=original,"Coverage cutoff did not replace prepared materials.");surfaces.front()=material.surface;
    surfaces.front().specular+=.1f;check(signature()!=original,"Fallback specular state did not replace prepared materials.");surfaces.front()=material.surface;
    bindings.front().sourceFingerprints[0]=99;check(signature()!=original,"A changed immutable texture source was omitted from prepared identity.");bindings.front().sourceFingerprints[0]=0;
    bindings.front().graphPending=true;check(signature()!=original,"Pending shader fallback was confused with its complete packet.");bindings.front().graphPending=false;
    check(signature()==original,"Unchanged prepared packets must retain their identity.");
    check(preparedModelSignature(asset,4,surfaces,bindings)!=original,"A replacement model generation retained the old packet identity.");
}
void preparedSourceCoherenceChecks() {
    MaterialAsset material;
    const auto output=addMaterialNode(material.graph,MaterialNodeKind::Output).id;
    const auto resize=addMaterialNode(material.graph,MaterialNodeKind::Resize).id;
    const auto blur=addMaterialNode(material.graph,MaterialNodeKind::Blur).id;
    connectMaterialNodes(material.graph,resize,MaterialPin::Value,output,MaterialPin::Roughness);
    connectMaterialNodes(material.graph,blur,MaterialPin::Value,output,MaterialPin::Metallic);
    const auto generated=generateMaterialGraphShader(material);
    PreparedGraphMaterial packet;packet.sources=generated.textures;
    check(packet.sources.size()==2&&packet.sources[0].resize&&packet.sources[1].resize&&
        packet.sources[0].image.empty()&&packet.sources[1].image.empty()&&
        packet.sources[0].resize->definitionKey!=packet.sources[1].resize->definitionKey,
        "Source coherence fixture needs distinct immutable cached recipes.");
    packet.sourceFingerprints[packet.sources[0].slot]=101;
    packet.sourceFingerprints[packet.sources[1].slot]=202;
    std::unordered_map<std::string,std::uint64_t> versions{{"earlier-managed-image",17}};
    check(preparedGraphSourcesCoherent(packet,versions)&&versions.size()==1&&!versions.contains(""),
        "Distinct cached image fingerprints collided under the empty managed-image identity.");
    MaterialGraphTextureBinding first;first.nodeId=91;first.slot=2;first.image.id="shared-managed-image";
    auto second=first;second.nodeId=92;second.slot=3;
    packet.sources.push_back(first);packet.sources.push_back(second);
    packet.sourceFingerprints[2]=303;packet.sourceFingerprints[3]=303;
    check(preparedGraphSourcesCoherent(packet,versions)&&versions.size()==2&&versions.at("shared-managed-image")==303,
        "Matching managed image revisions failed alongside immutable cached recipes.");
    packet.sourceFingerprints[3]=404;
    check(!preparedGraphSourcesCoherent(packet,versions),
        "Skipping immutable recipes also skipped a conflicting managed image revision.");
    packet.sourceFingerprints[3]=0;
    check(preparedGraphSourcesCoherent(packet,versions),
        "An unavailable managed fingerprint was treated as a conflicting revision.");
}

MaterialGraphProgramResult waitForResult(ResourceManager& resources,MaterialGraphPrograms& programs,const MaterialGraphShader& shader,bool optimized=true,
    const char* phase="graph shader publication",std::chrono::seconds timeout=180s) {
    MaterialGraphProgramResult result;
    try {waitForCompilation(resources,[&] {
        resources.pump();result=programs.request(shader);bgfx::frame();
        if(optimized&&result.current&&!result.programs->optimized&&!result.diagnostic.empty())
            throw std::runtime_error(result.diagnostic);
        return (result.current&&(!optimized||result.programs->optimized)) ||
            (!result.current&&result.diagnostic!="Compiling material graph...");
    },phase,timeout);}
    catch(const std::exception& error) {
        throw std::runtime_error(std::string(error.what())+" [variants="+std::to_string(shader.variants.size())+
            ", requestedOptimized="+std::to_string(optimized)+", current="+std::to_string(result.current)+
            ", compiling="+std::to_string(result.compiling)+", publishedOptimized="+
            std::to_string(result.programs&&result.programs->optimized)+", status="+result.diagnostic+"]");
    }
    return result;
}

void requireComplete(const MaterialGraphProgramResult& result) {
    if(!result.current)throw std::runtime_error("Expected complete graph shader publication: "+result.diagnostic);
    check(result.programs&&result.programs->bytes>0,"Published graph has no program allocation accounting.");
    check(bgfx::isValid(result.programs->parameters),"Published graph has no parameter uniform.");
    for(std::size_t pass=0;pass<5;++pass)check(bgfx::isValid(result.programs->programs[pass]),"A partial graph program bundle was published.");
    check(bgfx::isValid(result.programs->programs[5])==bgfx::isValid(result.programs->programs[6]),
        "A partial optional transmission pair was published.");
    check(bgfx::isValid(result.programs->programs[7])==bgfx::isValid(result.programs->programs[8]),
        "A partial optional reflection pair was published.");
    for(const auto handle:result.programs->samplers)check(bgfx::isValid(handle),"Published graph has an incomplete sampler set.");
}

// A single worker and this cancelable gate make publication order deterministic.
// No shader compiler or GPU call runs on this test's helper job.
struct WorkerGate {
    std::atomic_bool entered{},released{};
    ~WorkerGate(){released=true;}
};

using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point start) {return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
Json metricDifference(const ShaderCompilerMetrics& before) {
    const auto after=shaderCompilerMetrics();
    return {{"compilerInvocations",after.compilerInvocations-before.compilerInvocations},
        {"compilerMs",(after.compilerNanoseconds-before.compilerNanoseconds)/1e6},
        {"dependencyRequests",after.dependencyRequests-before.dependencyRequests},
        {"dependencyMs",(after.dependencyNanoseconds-before.dependencyNanoseconds)/1e6},
        {"cacheOnlyRequests",after.cacheOnlyRequests-before.cacheOnlyRequests},
        {"cacheOnlyHits",after.cacheOnlyHits-before.cacheOnlyHits},
        {"cacheOnlyMs",(after.cacheOnlyNanoseconds-before.cacheOnlyNanoseconds)/1e6},
        {"workerRequests",after.workerRequests-before.workerRequests},
        {"workerCacheHits",after.workerCacheHits-before.workerCacheHits}};
}

void writeProfile(const fs::path& root,const fs::path& output,const Json& report) {
    const auto destination=fs::weakly_canonical(fs::absolute(output));
    const auto relative=destination.lexically_relative(root/"build");
    check(!relative.empty()&&!relative.generic_string().starts_with(".."),"Project profile output must be under the project's build directory.");
    fs::create_directories(destination.parent_path());writeFileAtomically(destination,report.dump(2)+"\n");
    std::cout<<"Actual project shader profile written to "<<destination.string()<<'\n';
}

void profileEngine(const fs::path& project,const fs::path& rendererBin,const fs::path& output) {
    const auto root=fs::weakly_canonical(project),bin=fs::weakly_canonical(rendererBin);
    const ShaderCompileConfig config{bin/"shader-tools/shaderc.exe",bin/"shader-tools/shaders",bin/"shader-tools/include",resolveAssetPath(root,"assets/cache/shaders")};
    const auto names=engineShaderNames();
    Json report{{"project",root.generic_string()},{"rendererBin",bin.generic_string()},{"cache",config.cacheDirectory.generic_string()},
        {"stageCount",names.size()},{"notes","Mirrors startup all-engine-stage ResourceManager jobs with default two workers, compiler-owned content identity, resource file-state guards, Background priority and forceReload. Cache writes are disposable. Timings exclude renderer preflight/GPU publication."}};
    ShaderCacheReadSession cache(config);auto before=shaderCompilerMetrics();auto started=Clock::now();Json lookups=Json::array();
    for(const auto& name:names) {
        const auto start=Clock::now();const auto result=cache.find(name);
        lookups.push_back({{"name",name},{"hit",result.has_value()},{"elapsedMs",milliseconds(start)},
            {"bytes",result?result->bytes.size():0},{"fingerprint",result?result->fingerprint:0}});
    }
    report["authoredCacheLookup"]={{"elapsedMs",milliseconds(started)},{"metrics",metricDifference(before)},{"coherent",cache.unchanged()},{"stages",lookups}};
    TemporaryProject temporary;fs::create_directories(temporary.root);
    auto copied=config;copied.cacheDirectory=temporary.root/"engine-cache";
    fs::copy(config.cacheDirectory,copied.cacheDirectory,fs::copy_options::recursive);
    for(const auto* phase:{"firstEngineBatch","repeatedEngineBatch"}) {
        before=shaderCompilerMetrics();started=Clock::now();
        std::map<std::string,std::vector<fs::path>> dependencies;
        const auto dependencyStart=Clock::now();for(const auto& name:names)dependencies[name]=shaderDependencies(copied,name);
        const auto dependencyMs=milliseconds(dependencyStart);
        ResourceManager resources;
        std::vector<ResourceHandle<ShaderBinary>> jobs;std::vector<std::shared_ptr<std::atomic<double>>> times;
        const auto requestsStart=Clock::now();
        for(const auto& name:names) {
            auto time=std::make_shared<std::atomic<double>>();times.push_back(time);
            ResourceRequestOptions options;options.priority=ResourcePriority::Background;options.forceReload=true;options.dependencies=dependencies.at(name);
            options.dependencyValidation=DependencyValidation::FileState;
            jobs.push_back(resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"engine-shader:"+name},[copied,name,time](std::stop_token stop) {
                const auto start=Clock::now();auto binary=std::make_shared<ShaderBinary>(compileShader(copied,name,stop));time->store(milliseconds(start));
                return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};
            },std::move(options)));
        }
        const auto requestMs=milliseconds(requestsStart);
        waitForCompilation(resources,[&]{resources.pump();return std::all_of(jobs.begin(),jobs.end(),[](const auto& job) {
            return job.state()==ResourceState::Ready||job.state()==ResourceState::Failed;
        });},"actual engine shader startup batch");
        Json stages=Json::array();
        for(std::size_t index=0;index<jobs.size();++index)stages.push_back({{"name",names[index]},
            {"ready",jobs[index].state()==ResourceState::Ready},{"diagnostic",jobs[index].diagnostic()},{"loaderMs",times[index]->load()}});
        report[phase]={{"elapsedMs",milliseconds(started)},{"prepareDependenciesMs",dependencyMs},{"queueRequestsMs",requestMs},
            {"metrics",metricDifference(before)},{"stages",stages}};
        resources.stop();
    }
    writeProfile(root,output,report);
}

// Uses the installed renderer's compiler/source/include paths and the authored
// cache. Only disposable copies are handed to the program owner, whose normal
// recovery may discard invalid derived artifacts. The sole worker is blocked
// while probing first/repeated publication, so misses never launch shaderc.
void profileProject(const fs::path& project,const fs::path& rendererBin,const fs::path& output,bool cold,bool edit=false) {
    const auto root=fs::weakly_canonical(project);
    const auto bin=fs::weakly_canonical(rendererBin);
    const ShaderCompileConfig config{bin/"shader-tools/shaderc.exe",bin/"shader-tools/shaders",bin/"shader-tools/include",resolveAssetPath(root,"assets/cache/shaders")};
    check(fs::is_regular_file(config.compiler),"Runtime shader compiler is unavailable.");
    const auto sessionFile=resolveAssetPath(root,"assets/editor-session.json");
    const auto session=Json::parse(read(sessionFile));
    const auto sceneFile=resolveAssetPath(root,session.at("scene").get<std::string>());
    const auto scene=Json::parse(read(sceneFile));
    std::set<std::string> usedMaterials;
    Json sceneModels=Json::array();
    for(const auto& entity:scene.at("entities")) {
        if(!entity.contains("model"))continue;
        const auto id=entity.at("model").get<std::string>();
        sceneModels.push_back({{"id",id},{"entity",entity.at("id")}});
        std::vector<std::string> overrides;
        if(entity.contains("modelMaterials"))overrides=entity.at("modelMaterials").get<std::vector<std::string>>();
        else if(entity.contains("modelMaterial"))overrides.push_back(entity.at("modelMaterial").get<std::string>());
        for(const auto& material:overrides)if(!material.empty())usedMaterials.insert(material);
        if(isEngineAssetId(id)){if(overrides.empty()||overrides.front().empty())usedMaterials.insert(DefaultMaterialId);continue;}
        const auto manifest=Json::parse(read(resolveAssetPath(root,id)));
        if(manifest.contains("materialAssets")) {
            std::size_t slot{};
            for(const auto& value:manifest.at("materialAssets")) {
                if(slot>=overrides.size()||overrides[slot].empty())usedMaterials.insert(value.get<std::string>());
                ++slot;
            }
        } else {
            const auto document=Json::parse(read(resolveAssetPath(root,id).parent_path()/manifest.at("document").get<std::string>()));
            for(std::size_t index=0;index<document.value("materials",Json::array()).size();++index)
                if(index>=overrides.size()||overrides[index].empty())usedMaterials.insert(importedMaterialId(id,index));
        }
    }
    std::vector<fs::path> protectedFiles{sessionFile,sceneFile};
    const auto layout=root/"crimson_ui.ini";if(fs::is_regular_file(layout))protectedFiles.push_back(layout);
    std::vector<std::string> ids;
    if(contentStorageReady(root)) {
        for(const auto& file:storedAssetPaths(root))if(!isEngineAssetId(file.id)&&file.file.extension()==".crimson-material"&&fs::is_regular_file(file.file)) {protectedFiles.push_back(file.file);ids.push_back(file.id);}
    } else for(const auto& file:fs::directory_iterator(root/"assets/materials"))if(file.path().extension()==".crimson-material") {
        protectedFiles.push_back(file.path());ids.push_back(file.path().lexically_relative(root).generic_string());
    }
    std::sort(ids.begin(),ids.end());
    const auto protectedBefore=fingerprintFiles(protectedFiles);
    std::map<std::string,MaterialGraphShader> unique;
    std::map<std::string,MaterialAsset> representatives;
    std::set<std::string> sceneTopologies;
    Json materials=Json::array();
    const auto generate=[&](MaterialAsset material,bool active) {
        const auto start=Clock::now();auto shader=generateMaterialGraphShader(material);const auto ms=milliseconds(start);
        materials.push_back({{"id",material.id},{"name",material.name},{"activeScene",active},{"topologyKey",shader.topologyKey},{"generateMs",ms},
            {"variants",shader.variants.size()},{"textureBindings",shader.textures.size()}});
        if(active)sceneTopologies.insert(shader.topologyKey);
        representatives.try_emplace(shader.topologyKey,std::move(material));
        unique.try_emplace(shader.topologyKey,std::move(shader));
    };
    for(const auto& id:ids)generate(loadMaterialAsset(root,id),usedMaterials.contains(id));
    generate(defaultMaterialAsset(),usedMaterials.contains(DefaultMaterialId));
    generate(defaultParticleMaterialAsset(),false);
    Json report{{"project",root.generic_string()},{"rendererBin",bin.generic_string()},{"cache",config.cacheDirectory.generic_string()},
        {"scene",sceneFile.generic_string()},{"sceneModels",sceneModels},{"materials",materials},
        {"uniqueTopologyCount",unique.size()},{"activeSceneTopologyCount",sceneTopologies.size()},
        {"notes","Noop GPU timings measure CPU publication/preflight only; cold compiler inputs are actual project topologies in a disposable cache."}};
    Json lookups=Json::array();
    ShaderCacheReadSession cache(config);
    auto before=shaderCompilerMetrics();auto stageStart=Clock::now();
    for(const auto& [key,shader]:unique) {
        Json passes=Json::array();
        for(const auto& variant:shader.variants) {
            const auto started=Clock::now();const auto vertex=cache.find(variant.vertexName);const auto vertexMs=milliseconds(started);
            const auto fragmentStart=Clock::now();const auto fragment=cache.find(materialGraphShaderRecipe(config,shader,variant));
            passes.push_back({{"pass",static_cast<int>(variant.pass)},{"vertexName",variant.vertexName},
                {"vertexHit",vertex.has_value()},{"vertexLookupMs",vertexMs},{"fragmentHit",fragment.has_value()},
                {"fragmentLookupMs",milliseconds(fragmentStart)},{"fragmentFingerprint",fragment?fragment->fingerprint:0},
                {"vertexBytes",vertex?vertex->bytes.size():0},{"fragmentBytes",fragment?fragment->bytes.size():0}});
        }
        lookups.push_back({{"topologyKey",key},{"activeScene",sceneTopologies.contains(key)},{"passes",passes}});
    }
    report["authoredCacheLookup"]={{"elapsedMs",milliseconds(stageStart)},{"metrics",metricDifference(before)},{"coherent",cache.unchanged()},{"topologies",lookups}};
    TemporaryProject temporary;fs::create_directories(temporary.root);
    auto warmConfig=config;warmConfig.cacheDirectory=temporary.root/"warm-cache";
    fs::copy(config.cacheDirectory,warmConfig.cacheDirectory,fs::copy_options::recursive);
    {
        ResourceConfig resourceConfig;resourceConfig.workers=1;resourceConfig.maximumQueued=1024;
        ResourceManager resources(resourceConfig);
        auto gate=std::make_shared<WorkerGate>();ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;
        auto blocker=resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"project-profile-worker-gate"},[gate](std::stop_token stop) {
            gate->entered=true;while(!gate->released&&!stop.stop_requested())std::this_thread::sleep_for(1ms);
            return ResourceLoadResult<ShaderBinary>{std::make_shared<ShaderBinary>(),0,1,{}};
        },options);
        waitFor([&]{return gate->entered.load();},"Project profiler worker gate failed.");
        MaterialGraphPrograms programs(resources,warmConfig);
        for(const auto* phase:{"firstProgramRequests","repeatedProgramRequests"}) {
            before=shaderCompilerMetrics();stageStart=Clock::now();Json requests=Json::array();
            for(const auto& [key,shader]:unique) {
                const auto started=Clock::now();const auto result=programs.request(shader,ResourcePriority::Visible);
                requests.push_back({{"topologyKey",key},{"activeScene",sceneTopologies.contains(key)},
                    {"elapsedMs",milliseconds(started)},{"current",result.current},{"compiling",result.compiling},{"diagnostic",result.diagnostic}});
            }
            const auto stats=resources.stats();
            report[phase]={{"elapsedMs",milliseconds(stageStart)},{"metrics",metricDifference(before)},{"queued",stats.queued},{"requests",requests}};
        }
        // stop cancels the held worker before joining; queued shader tasks never
        // run, including cache misses observed above.
        resources.stop();
    }
    for(int frame=0;frame<3;++frame)bgfx::frame();
    if(cold) {
        auto coldConfig=config;coldConfig.cacheDirectory=temporary.root/"cold-cache";
        for(const auto* phase:{"coldWorkerCompiles","repeatedWorkerCacheReads"}) {
            before=shaderCompilerMetrics();stageStart=Clock::now();Json stages=Json::array();
            for(const auto& [key,shader]:unique) {
                // The active scene is the representative startup workload; the
                // broader catalog remains included in the read-only hit audit.
                if(!sceneTopologies.contains(key))continue;
                for(const auto& variant:shader.variants) {
                    auto itemBefore=shaderCompilerMetrics();auto started=Clock::now();auto vertex=compileShader(coldConfig,variant.vertexName);
                    stages.push_back({{"topologyKey",key},{"name",variant.vertexName},{"elapsedMs",milliseconds(started)},
                        {"metrics",metricDifference(itemBefore)},{"bytes",vertex.bytes.size()}});
                    itemBefore=shaderCompilerMetrics();started=Clock::now();
                    auto fragment=compileShader(coldConfig,materialGraphShaderRecipe(coldConfig,shader,variant));
                    stages.push_back({{"topologyKey",key},{"name",fragment.name},{"elapsedMs",milliseconds(started)},
                        {"metrics",metricDifference(itemBefore)},{"bytes",fragment.bytes.size()}});
                }
            }
            report[phase]={{"elapsedMs",milliseconds(stageStart)},{"metrics",metricDifference(before)},{"stages",stages}};
        }
    }
    if(edit) {
        const MaterialGraphShader* representative=nullptr;std::string representativeKey;
        for(const auto& [key,shader]:unique)if(sceneTopologies.contains(key)) {
            const auto& material=representatives.at(key);
            if(materialOutputNode(material.graph).output.shading!=MaterialShading::StandardPbr)continue;
            if(!representative||shader.textures.size()>representative->textures.size()||
                (shader.textures.size()==representative->textures.size()&&shader.variants.front().source.size()>representative->variants.front().source.size())) {
                representative=&shader;representativeKey=key;
            }
        }
        check(representative!=nullptr,"The project has no active lit graph for the edit profile.");
        auto material=representatives.at(representativeKey);
        const auto outputId=materialOutputNode(material.graph).id;
        const auto* link=materialInputLink(material.graph,outputId,MaterialPin::BaseColor);
        auto baseNode=link?link->fromNode:std::uint64_t{};const auto basePin=link?link->fromPin:MaterialPin::RGB;
        if(!baseNode)baseNode=addMaterialNode(material.graph,MaterialNodeKind::Color).id;
        const auto particle=addMaterialNode(material.graph,MaterialNodeKind::ParticleColor).id;
        const auto factor=addMaterialNode(material.graph,MaterialNodeKind::Scalar).id;
        const auto tint=addMaterialNode(material.graph,MaterialNodeKind::Multiply).id;
        const auto multiply=addMaterialNode(material.graph,MaterialNodeKind::Multiply).id;
        findMaterialNode(material.graph,factor)->scalar=.75f;
        connectMaterialNodes(material.graph,particle,MaterialPin::RGB,tint,MaterialPin::Left);
        connectMaterialNodes(material.graph,factor,MaterialPin::Value,tint,MaterialPin::Right);
        connectMaterialNodes(material.graph,baseNode,basePin,multiply,MaterialPin::Left);
        connectMaterialNodes(material.graph,tint,MaterialPin::Value,multiply,MaterialPin::Right);
        connectMaterialNodes(material.graph,multiply,MaterialPin::Value,outputId,MaterialPin::BaseColor);
        const auto shader=generateMaterialGraphShader(material);
        check(shader.topologyKey!=representativeKey,"The project edit profile did not change shader topology.");
        auto editConfig=config;editConfig.cacheDirectory=temporary.root/"edit-cache";
        auto editBefore=shaderCompilerMetrics();auto editStart=Clock::now();
        for(const auto& variant:representative->variants) {
            (void)compileShader(editConfig,variant.vertexName);
            if(variant.pass!=MaterialGraphPass::Model&&variant.pass!=MaterialGraphPass::Particle)
                (void)compileShader(editConfig,materialGraphShaderRecipe(editConfig,*representative,variant));
        }
        Json editReport{{"materialId",material.id},{"materialName",material.name},{"textureCount",shader.textures.size()},
            {"baseTopology",representativeKey},{"editedTopology",shader.topologyKey},
            {"notes","Real active-scene lit material with a new BaseColor multiply expression. A disposable cache is primed only with shared vertices and unchanged coverage; surface stages start cold. Noop timings include CPU compilation and preflight, not native GPU rendering."},
            {"sharedStagePreparation",{{"elapsedMs",milliseconds(editStart)},{"metrics",metricDifference(editBefore)}}}};
        {
            auto fullConfig=editConfig;fullConfig.cacheDirectory=temporary.root/"optimized-only-edit-cache";
            fs::copy(editConfig.cacheDirectory,fullConfig.cacheDirectory,fs::copy_options::recursive);
            ResourceManager fullResources;std::vector<ResourceHandle<ShaderBinary>> fullJobs;
            const auto fullBefore=shaderCompilerMetrics();const auto fullStart=Clock::now();
            for(const auto& variant:shader.variants) {
                ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.variant="optimized-only-profile";
                const auto vertexName=variant.vertexName;
                fullJobs.push_back(fullResources.request<ShaderBinary>(AssetRef<ShaderBinary>{"profile-vertex:"+vertexName},[fullConfig,vertexName](std::stop_token stop) {
                    auto binary=std::make_shared<ShaderBinary>(compileShader(fullConfig,vertexName,stop));
                    return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};
                },options));
                const auto recipe=materialGraphShaderRecipe(fullConfig,shader,variant);
                fullJobs.push_back(fullResources.request<ShaderBinary>(AssetRef<ShaderBinary>{"profile-fragment:"+recipe.name},[fullConfig,recipe](std::stop_token stop) {
                    auto binary=std::make_shared<ShaderBinary>(compileShader(fullConfig,recipe,stop));
                    return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};
                },options));
            }
            waitForCompilation(fullResources,[&]{fullResources.pump();return std::all_of(fullJobs.begin(),fullJobs.end(),[](const auto& job) {
                if(job.state()==ResourceState::Failed)throw std::runtime_error(job.diagnostic());
                return job.state()==ResourceState::Ready;
            });},"optimized-only edit profile");
            MaterialGraphPrograms fullPrograms(fullResources,fullConfig);const auto full=fullPrograms.request(shader);requireComplete(full);
            check(full.programs->optimized,"Optimized-only edit profile did not publish its full-quality bundle.");
            editReport["optimizedOnlyFirstCurrent"]={{"elapsedMs",milliseconds(fullStart)},{"metrics",metricDifference(fullBefore)}};
            fullResources.stop();
        }
        ResourceManager resources;MaterialGraphPrograms programs(resources,editConfig);
        editBefore=shaderCompilerMetrics();editStart=Clock::now();
        auto first=waitForResult(resources,programs,shader,false,"profile first interactive generation");requireComplete(first);
        editReport["firstCurrent"]={{"elapsedMs",milliseconds(editStart)},{"optimized",first.programs->optimized},{"metrics",metricDifference(editBefore)}};
        findMaterialNode(material.graph,factor)->scalar=.9f;const auto parameterShader=generateMaterialGraphShader(material);
        const auto parameterBefore=shaderCompilerMetrics();const auto parameterStart=Clock::now();
        const auto parameterResult=programs.request(parameterShader,ResourcePriority::Selected);
        check(parameterResult.current&&parameterResult.programs==first.programs,"Parameter edit did not reuse the current interactive packet.");
        editReport["parameterEdit"]={{"elapsedMs",milliseconds(parameterStart)},{"samePrograms",parameterResult.programs==first.programs},{"metrics",metricDifference(parameterBefore)}};
        auto optimized=waitForResult(resources,programs,shader,true,"profile optimized generation");requireComplete(optimized);
        check(optimized.programs->optimized,"Project edit profiling did not finish background optimization.");
        editReport["optimizedCurrent"]={{"elapsedMs",milliseconds(editStart)},{"metrics",metricDifference(editBefore)}};
        const auto warmBefore=shaderCompilerMetrics();const auto warmStart=Clock::now();const auto warmStats=resources.stats();
        MaterialGraphPrograms warm(resources,editConfig);const auto warmed=warm.request(shader);
        check(warmed.current&&warmed.programs->optimized&&resources.stats().queued==warmStats.queued,"Optimized project edit did not reopen immediately from its complete cache.");
        editReport["warmOptimized"]={{"elapsedMs",milliseconds(warmStart)},{"queuedJobs",resources.stats().queued-warmStats.queued},{"metrics",metricDifference(warmBefore)}};
        resources.stop();report["interactiveEdit"]=std::move(editReport);
    }
    report["authoredFilesUnchanged"]=fingerprintFiles(protectedFiles)==protectedBefore;
    check(report["authoredFilesUnchanged"].get<bool>(),"Authored files changed during project profiling.");
    // Diagnostics belong in build/, never next to authored material or scene data.
    writeProfile(root,output,report);
}

void unsupportedHeightChecks(const fs::path& compiler,const fs::path& sources,const fs::path& include) {
    TemporaryProject temporary;
    const ShaderCompileConfig config{compiler,sources,include,temporary.root/"cache"};
    auto material=fixture();materialOutputNode(material.graph).output.tessellation.enabled=true;
    const auto shader=generateMaterialGraphShader(material);
    check(std::any_of(shader.variants.begin(),shader.variants.end(),[](const auto& pass){return pass.tessellation;}),
        "Unsupported-backend fixture did not enable native material stages.");
    ResourceManager resources;
    {
        MaterialGraphPrograms programs(resources,config);MaterialGraphProgramResult result;
        waitForCompilation(resources,[&] {
            resources.pump();result=programs.request(shader);bgfx::frame();
            if(!result.current&&!result.compiling)throw std::runtime_error(result.diagnostic);
            return result.current;
        },"unsupported-backend height fallback publication");
        requireComplete(result);
        check(result.diagnostic.find("requires Direct3D 12")!=std::string::npos,
            "Unsupported hardware tessellation silently rendered an undisplaced material.");
        MaterialGraphPrograms reopened(resources,config);
        const auto warm=reopened.request(shader);requireComplete(warm);
        check(warm.diagnostic==result.diagnostic,"Warm unsupported-backend hydration lost its capability diagnostic.");
        for(const auto& file:fs::directory_iterator(config.cacheDirectory))
            check(!file.path().filename().string().starts_with("graph-domain-stage-"),
                "Noop fallback compiled or requested unsupported domain stages.");
    }
    resources.stop();
}

void lazyReflectionChecks(ResourceManager& resources,const ShaderCompileConfig& config,const MaterialGraphShader& shader) {
    // Only optional recipes change. Required stage identities and disk binaries
    // stay warm, while a unique comment keeps repeat focused runs meaningful.
    auto lazyShader=shader;lazyShader.topologyKey+="-lazy-reflection";
    const auto marker="\n// Independent lazy reflection publication fixture "+
        std::to_string(Clock::now().time_since_epoch().count())+".\n";
    for(auto& variant:lazyShader.variants)
        if(variant.pass==MaterialGraphPass::ModelReflection||variant.pass==MaterialGraphPass::ParticleReflection)
            variant.source+=marker;
    MaterialGraphPrograms lazy(resources,config);
    auto lazyGate=std::make_shared<WorkerGate>();
    ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;
    auto lazyBlocker=resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"lazy-reflection-worker-gate"},[lazyGate](std::stop_token stop) {
        lazyGate->entered=true;while(!lazyGate->released&&!stop.stop_requested())std::this_thread::sleep_for(1ms);
        return ResourceLoadResult<ShaderBinary>{std::make_shared<ShaderBinary>(),0,1,{}};
    },options);
    waitFor([&]{return lazyGate->entered.load();},"Could not establish optional reflection publication gate.");
    const auto before=shaderCompilerMetrics();const auto queued=resources.stats().queued;
    const auto base=lazy.request(lazyShader,ResourcePriority::Selected,MaterialGraphUsage::Surface);requireComplete(base);
    check(base.programs->optimized&&!base.compiling&&!bgfx::isValid(base.programs->programs[7])&&!bgfx::isValid(base.programs->programs[8])&&
        resources.stats().queued==queued&&shaderCompilerMetrics().compilerInvocations==before.compilerInvocations,
        "SSR-off publication waited for or queued unused reflection programs.");
    const auto pinnedPrograms=base.programs->programs;
    const auto upgrading=lazy.request(lazyShader,ResourcePriority::Selected,MaterialGraphUsage::SurfaceReflection);
    const auto upgradeQueued=resources.stats().queued-queued;
    const auto upgradeState=Json{{"current",upgrading.current},{"compiling",upgrading.compiling},
        {"sameBasePacket",upgrading.programs==base.programs},{"queuedJobs",upgradeQueued},
        {"status",upgrading.diagnostic},{"metrics",metricDifference(before)}};
    if(!upgrading.current||!upgrading.compiling||upgrading.programs!=base.programs)
        throw std::runtime_error("Requesting SSR hid the ready base packet. "+upgradeState.dump());
    // An optional fragment can also queue a cache-only vertex read if no ready
    // ResourceManager vertex record exists. A warm disk cache does not imply
    // that such a record survived an earlier epoch/publication test.
    if(upgradeQueued<2||upgradeQueued>4||shaderCompilerMetrics().compilerInvocations!=before.compilerInvocations)
        throw std::runtime_error("SSR upgrade queued work beyond its optional fragment/vertex pair. "+upgradeState.dump());
    lazyGate->released=true;MaterialGraphProgramResult extended;
    waitForCompilation(resources,[&] {
        resources.pump();extended=lazy.request(lazyShader,ResourcePriority::Selected,MaterialGraphUsage::SurfaceReflection);bgfx::frame();
        check(extended.current,"Optional reflection preparation made an existing surface unavailable.");
        return bgfx::isValid(extended.programs->programs[7])&&bgfx::isValid(extended.programs->programs[8]);
    },"optional reflection pair upgrade");
    requireComplete(extended);
    check(extended.programs!=base.programs&&bgfx::isValid(base.programs->programs[0]),
        "Optional upgrade mutated or destroyed its pinned prior surface generation.");
    for(std::size_t pass=0;pass<5;++pass)
        check(base.programs->programs[pass].idx==pinnedPrograms[pass].idx&&bgfx::isValid(base.programs->programs[pass]),
            "Optional SSR upgrade mutated or retired a pinned required program.");
    check(lazy.request(lazyShader,ResourcePriority::Selected,MaterialGraphUsage::Surface).programs==base.programs,
        "Turning SSR off replaced the unchanged required surface family.");
    waitForCompilation(resources,[&]{resources.pump();return resources.idle();},"lazy reflection worker drain");
    const auto metrics=metricDifference(before);
    std::cout<<"Lazy reflection publication: "<<Json{{"upgrade",upgradeState},{"completedMetrics",metrics}}.dump()<<'\n';
    check(shaderCompilerMetrics().compilerInvocations==before.compilerInvocations+2,
        "Optional SSR upgrade did not compile exactly its two missing reflection stages.");
}

void focusedLazyReflectionChecks(const fs::path& compiler,const fs::path& sources,const fs::path& include,const fs::path& warmCache) {
    TemporaryProject temporary;
    const ShaderCompileConfig config{compiler,sources,include,warmCache.empty()?temporary.root/"cache":warmCache};
    const auto shader=generateMaterialGraphShader(fixture());
    // Prepare only the five required passes. A supplied matching warm cache
    // makes this a cache-only setup; cold optional SSR and unrelated reload,
    // recovery, preview and optimization fixtures are deliberately omitted.
    const auto before=shaderCompilerMetrics();
    for(const auto& variant:shader.variants) {
        if(variant.pass==MaterialGraphPass::ModelReflection||variant.pass==MaterialGraphPass::ParticleReflection)continue;
        (void)compileShader(config,variant.vertexName);
        (void)compileShader(config,materialGraphShaderRecipe(config,shader,variant));
    }
    std::cout<<"Lazy reflection required-stage setup: "<<metricDifference(before).dump()<<'\n';
    ResourceConfig resourceConfig;resourceConfig.workers=1;ResourceManager resources{resourceConfig};
    lazyReflectionChecks(resources,config,shader);resources.stop();
    for(int frame=0;frame<3;++frame)bgfx::frame();
}

void publicationChecks(const fs::path& compiler,const fs::path& sources,const fs::path& include) {
    TemporaryProject temporary;
    const auto copiedSources=temporary.root/"shaders";
    fs::create_directories(temporary.root);
    fs::copy(sources,copiedSources,fs::copy_options::recursive);
    const ShaderCompileConfig config{compiler,copiedSources,include,temporary.root/"cache"};
    const auto editedSource=copiedSources/"material_graph_model.sh";
    const auto originalSource=read(editedSource);
    const auto commonSource=copiedSources/"material_graph_common.sh";
    const auto originalCommon=read(commonSource);
    const std::array changedFiles{editedSource};
    auto material=fixture();const auto shader=generateMaterialGraphShader(material);
    {
        auto animated=material;const auto time=addMaterialNode(animated.graph,MaterialNodeKind::Time).id;
        connectMaterialNodes(animated.graph,time,MaterialPin::Value,materialOutputNode(animated.graph).id,MaterialPin::OpacityMask);
        const auto generated=generateMaterialGraphShader(animated);
        PreparedGraphMaterial packet;packet.signature=123;packet.uniforms=generated.uniforms;packet.runtimeGraph=generated.runtimeGraph;
        packet.runtimeParameters=generated.runtimeParameters;packet.dynamicCoverage=generated.dynamicCoverage;
        const auto before=packet.uniforms;
        check(packet.dynamicCoverage&&packet.coverageSignature({1,9})!=packet.coverageSignature({2,9}),"Animated masked coverage did not invalidate the shadow cache.");
        check(packet.coverageSignature({1,9})==packet.coverageSignature({1,13})&&packet.uniforms==before,
            "Game-time coverage consumed the real clock or mutated its retained packet.");
        packet.dynamicCoverage=false;check(packet.coverageSignature({1,9})==packet.coverageSignature({2,13}),"Static coverage depended on the frame clock.");
    }
    {
        ModelMaterialTextures fallback;
        check(usesParticleMaterialPreview(MaterialPreviewMesh::Sprite,fallback),"Explicit Sprite preview did not select billboard geometry.");
        auto packet=std::make_shared<PreparedGraphMaterial>();fallback.graph=packet;
        check(usesParticleMaterialPreview(MaterialPreviewMesh::Sprite,fallback),"Full graph Sprite preview lost its particle program path.");
        packet->nodePreview=true;
        check(!usesParticleMaterialPreview(MaterialPreviewMesh::Sprite,fallback),"Node preview incorrectly selected its unavailable Sprite program.");
        check(!usesParticleMaterialPreview(MaterialPreviewMesh::Sphere,{}),"Sphere preview unexpectedly selected billboard geometry.");
        packet->sources.push_back({});fallback.complete=false;
        check(!materialPreviewPacketReady(fallback),"Preview accepted a graph with an unavailable texture handle.");
        packet->textures[0]={7};
        check(materialPreviewPacketReady(fallback),"A coherent coarse texture packet left the material preview blank while refinement was pending.");
        fallback.sourceReady=false;fallback.complete=true;
        check(!materialPreviewPacketReady(fallback),"A nominally refined packet mixed an obsolete image source into the preview.");
    }
    PreparedGraphMaterial retainedPacket;
    std::weak_ptr<const MaterialGraphProgramsVersion> retiredVersion,retainedVersion;
    {
        ResourceConfig resourceConfig;resourceConfig.workers=1;
        ResourceManager resources{resourceConfig};
        {
            MaterialGraphPrograms programs{resources,config};
            auto pending=programs.request(shader);
            check(!pending.current&&!pending.programs&&pending.compiling&&pending.diagnostic=="Compiling material graph...","Cold graph request did not remain pending until worker publication.");

            // Finish old CPU jobs but deliberately do not pump their completions.
            waitForCompilation(resources,[&]{const auto stats=resources.stats();return stats.queued==0&&stats.running==0;},"initial cold shader CPU batch");
            auto gate=std::make_shared<WorkerGate>();
            ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;
            auto blocker=resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"publication-worker-gate"},[gate](std::stop_token stop) {
                gate->entered=true;
                while(!gate->released&&!stop.stop_requested())std::this_thread::sleep_for(1ms);
                auto binary=std::make_shared<ShaderBinary>();
                return ResourceLoadResult<ShaderBinary>{binary,0,1,{}};
            },options);
            waitFor([&]{return gate->entered.load();},"Could not establish shader worker gate.");
            programs.changed(changedFiles);
            pending=programs.request(shader);
            const auto queuedBeforePromotion=resources.stats().queued;
            const auto foreground=programs.request(shader,ResourcePriority::Selected);
            check(!foreground.current&&foreground.compiling&&resources.stats().queued==queuedBeforePromotion,
                "Foreground material priority duplicated pending shader jobs or published an incomplete bundle.");
            resources.pump();
            pending=programs.request(shader);
            check(!pending.current&&!pending.programs,"A completed job from the previous source epoch published while its replacement was pending.");
            check(programs.residentBytes()==0,"Rejected stale jobs allocated a GPU program bundle.");
            gate->released=true;
            auto first=waitForResult(resources,programs,shader,true,"initial complete optimized generation");requireComplete(first);
            for(const auto pass:{MaterialGraphPass::ModelReflection,MaterialGraphPass::ParticleReflection})
                check(bgfx::isValid(first.programs->programs[static_cast<std::size_t>(pass)]),
                    "A Lit material published before its complete reflection pair was available.");
            {
                auto incomplete=shader;incomplete.topologyKey+="-missing-reflection";incomplete.variants.pop_back();
                const auto rejected=programs.request(incomplete);
                check(!rejected.current&&!rejected.compiling&&!rejected.programs&&!rejected.diagnostic.empty(),
                    "Publication accepted a material missing one reflection receiver variant.");
            }
            check(programs.residentBytes()==first.programs->bytes,"Initial graph program bytes were not counted once.");
            {
                // A new owner models editor reopening. Hold the only worker so
                // a warm first result cannot accidentally depend on queued work.
                auto warmGate=std::make_shared<WorkerGate>();
                auto warmBlocker=resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"warm-start-worker-gate"},[warmGate](std::stop_token stop) {
                    warmGate->entered=true;
                    while(!warmGate->released&&!stop.stop_requested())std::this_thread::sleep_for(1ms);
                    return ResourceLoadResult<ShaderBinary>{std::make_shared<ShaderBinary>(),0,1,{}};
                },options);
                waitFor([&]{return warmGate->entered.load();},"Could not block the worker for warm-start hydration.");
                const auto before=resources.stats();
                MaterialGraphPrograms reopened(resources,config);
                const auto hydrated=reopened.request(shader);requireComplete(hydrated);
                check(!hydrated.compiling&&hydrated.programs->optimized&&hydrated.programs->signature==first.programs->signature,
                    "Reopened material changed its exact compiled program bundle.");
                check(resources.stats().queued==before.queued&&resources.stats().loads==before.loads,
                    "Warm material startup queued resource work instead of using its persistent bundle.");
                auto structural=material;
                const auto texture=std::find_if(structural.graph.nodes.begin(),structural.graph.nodes.end(),[](const auto& node){return node.kind==MaterialNodeKind::Texture2D;})->id;
                const auto factor=addMaterialNode(structural.graph,MaterialNodeKind::Scalar).id;
                const auto multiply=addMaterialNode(structural.graph,MaterialNodeKind::Multiply).id;
                findMaterialNode(structural.graph,factor)->scalar=.4f;
                connectMaterialNodes(structural.graph,texture,MaterialPin::RGB,multiply,MaterialPin::Left);
                connectMaterialNodes(structural.graph,factor,MaterialPin::Value,multiply,MaterialPin::Right);
                connectMaterialNodes(structural.graph,multiply,MaterialPin::Value,materialOutputNode(structural.graph).id,MaterialPin::BaseColor);
                const auto structuralShader=generateMaterialGraphShader(structural);
                check(structuralShader.topologyKey!=shader.topologyKey,"Structural shader fixture did not change its surface topology.");
                auto structuralOwner=programs.createSibling();
                const auto dependencyBefore=shaderCompilerMetrics().dependencyRequests;
                check(structuralOwner->sourceDependencies()==programs.sourceDependencies(),"Sibling programs disagree about the shared shader source family.");
                check(shaderCompilerMetrics().dependencyRequests==dependencyBefore,"Sibling programs rescanned an unchanged source dependency family.");
                const auto structuralBefore=shaderCompilerMetrics();const auto queuedBefore=resources.stats().queued;
                const auto structuralPending=structuralOwner->request(structuralShader,ResourcePriority::Selected);
                check(!structuralPending.current&&structuralPending.compiling&&!structuralPending.programs,
                    "A partially cached structural edit published an incomplete program generation.");
                check(resources.stats().queued==queuedBefore+4,"Surface edit queued unchanged vertex, picking or shadow shaders.");
                const auto structuralAfter=shaderCompilerMetrics();
                check(structuralAfter.cacheOnlyHits==structuralBefore.cacheOnlyHits+10,
                    "Surface edit failed to reuse cached vertices and its three unchanged coverage passes.");
                check(structuralAfter.dependencyRequests==structuralBefore.dependencyRequests+18,
                    "Surface edit repeated dependency walks after its cache lookup.");
                warmGate->released=true;
                const auto interactive=waitForResult(resources,*structuralOwner,structuralShader,false,"structural edit interactive generation");requireComplete(interactive);
                check(!interactive.programs->optimized,"A new surface skipped its complete interactive generation.");
                auto parameters=structural;findMaterialNode(parameters.graph,factor)->scalar=.6f;
                const auto parameterShader=generateMaterialGraphShader(parameters);
                const auto parameterMetrics=shaderCompilerMetrics();const auto parameterStats=resources.stats();
                const auto parameterResult=structuralOwner->request(parameterShader,ResourcePriority::Selected);
                check(parameterResult.current&&parameterResult.programs==interactive.programs&&
                    resources.stats().queued==parameterStats.queued&&shaderCompilerMetrics().compilerInvocations==parameterMetrics.compilerInvocations,
                    "A parameter edit replaced or recompiled an interactive complete generation.");
                {
                    waitForCompilation(resources,[&]{resources.pump();return resources.idle();},"interactive shader drain before optimization gate");
                    auto optimizeGate=std::make_shared<WorkerGate>();
                    auto optimizeBlocker=resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"optimization-worker-gate"},[optimizeGate](std::stop_token stop) {
                        optimizeGate->entered=true;while(!optimizeGate->released&&!stop.stop_requested())std::this_thread::sleep_for(1ms);
                        return ResourceLoadResult<ShaderBinary>{std::make_shared<ShaderBinary>(),0,1,{}};
                    },options);
                    waitFor([&]{return optimizeGate->entered.load();},"Could not establish the background optimization gate.");
                    auto secondOwner=structuralOwner->createSibling();const auto secondInteractive=secondOwner->request(structuralShader);
                    check(secondInteractive.current&&!secondInteractive.programs->optimized,"Sibling did not reuse the complete interactive disk cache.");
                    waitFor([&]{const auto result=structuralOwner->request(structuralShader);return result.current&&result.compiling;},
                        "A settled interactive generation did not queue background optimization.");
                    const auto optimizeQueued=resources.stats().queued;
                    const auto secondOptimizing=secondOwner->request(structuralShader);
                    check(secondOptimizing.current&&secondOptimizing.compiling&&optimizeQueued==1&&resources.stats().queued==optimizeQueued,
                        "Background optimization occupied more than one worker slot or siblings duplicated its admitted stage.");
                    secondOwner.reset();
                    auto superseding=structural;
                    const auto extraFactor=addMaterialNode(superseding.graph,MaterialNodeKind::Scalar).id;
                    const auto extraMultiply=addMaterialNode(superseding.graph,MaterialNodeKind::Multiply).id;
                    findMaterialNode(superseding.graph,extraFactor)->scalar=.8f;
                    connectMaterialNodes(superseding.graph,multiply,MaterialPin::Value,extraMultiply,MaterialPin::Left);
                    connectMaterialNodes(superseding.graph,extraFactor,MaterialPin::Value,extraMultiply,MaterialPin::Right);
                    connectMaterialNodes(superseding.graph,extraMultiply,MaterialPin::Value,materialOutputNode(superseding.graph).id,MaterialPin::BaseColor);
                    const auto supersedingShader=generateMaterialGraphShader(superseding);auto foregroundOwner=programs.createSibling();
                    const auto cancellationMetrics=shaderCompilerMetrics();
                    const auto foregroundPending=foregroundOwner->request(supersedingShader,ResourcePriority::Selected);
                    check(!foregroundPending.current&&foregroundPending.compiling,"A new foreground topology did not start its interactive batch.");
                    const auto canceled=structuralOwner->request(structuralShader);
                    check(canceled.current&&!canceled.compiling&&canceled.programs==interactive.programs,
                        "Interrupting background optimization discarded or blocked the last complete interactive generation.");
                    optimizeGate->released=true;
                    const auto foregroundReady=waitForResult(resources,*foregroundOwner,supersedingShader,false,"superseding foreground generation");requireComplete(foregroundReady);
                    waitForCompilation(resources,[&]{resources.pump();return resources.idle();},"canceled background shader drain");
                    check(shaderCompilerMetrics().compilerInvocations==cancellationMetrics.compilerInvocations+4,
                        "Superseded background optimization launched shaderc ahead of the four foreground color/reflection jobs.");
                }
                const auto updated=waitForResult(resources,*structuralOwner,structuralShader,true,"structural edit optimized generation");requireComplete(updated);
                check(updated.programs->optimized&&updated.programs!=interactive.programs&&updated.programs->signature!=interactive.programs->signature,
                    "Background optimization did not atomically replace the complete interactive generation.");
                for(const auto pass:{MaterialGraphPass::Picking,MaterialGraphPass::PointShadow,MaterialGraphPass::DirectionalShadow})
                    check(updated.programs->programs[static_cast<std::size_t>(pass)].idx==first.programs->programs[static_cast<std::size_t>(pass)].idx,
                        "Surface edit replaced an unchanged complete coverage pass.");
                const auto optimizedBefore=resources.stats();const auto optimizedMetrics=shaderCompilerMetrics();
                auto optimizedOwner=programs.createSibling();const auto optimizedWarm=optimizedOwner->request(structuralShader);
                check(optimizedWarm.current&&optimizedWarm.programs->optimized&&!optimizedWarm.compiling&&
                    resources.stats().queued==optimizedBefore.queued&&shaderCompilerMetrics().compilerInvocations==optimizedMetrics.compilerInvocations,
                    "Warm material reopening preferred an interactive tier or queued optimization despite its complete optimized cache.");
                waitForCompilation(resources,[&]{resources.pump();return resources.idle();},"warm-start shader worker drain");
            }
            lazyReflectionChecks(resources,config,shader);
            {
                auto previewOwner=programs.createSibling();
                const auto texture=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Texture2D;});
                const auto previewShader=generateMaterialGraphPreviewShader(material,{texture->id,MaterialPin::RGB});
                auto preview=waitForResult(resources,*previewOwner,previewShader,true,"independent node preview publication");
                check(preview.current && preview.programs && bgfx::isValid(preview.programs->programs[0]),"Independent node preview did not publish a Model program.");
                check(!bgfx::isValid(preview.programs->programs[1]),"Node preview allocated ordinary material passes.");
                const auto revision=previewOwner->revision();programs.changed(changedFiles);
                check(previewOwner->revision()>revision,"Node preview did not share shader-source invalidation.");
                auto previewPending=previewOwner->request(previewShader);
                check(!previewPending.current && previewPending.programs==preview.programs,"Node preview discarded its previous complete bundle during source invalidation.");
                waitForResult(resources,*previewOwner,previewShader,true,"invalidated node preview publication");
                first=waitForResult(resources,programs,shader,true,"material source reload publication");requireComplete(first);
            }
            std::array<std::vector<std::uint8_t>,9> originalFragments;
            for(const auto& variant:shader.variants)
                originalFragments[static_cast<std::size_t>(variant.pass)]=compileShader(config,materialGraphShaderRecipe(config,shader,variant)).bytes;
            PreparedGraphMaterial retiredPacket;retiredPacket.programs=first.programs;
            retiredVersion=first.programs;
            for(auto pass:{MaterialGraphPass::Model,MaterialGraphPass::Picking,MaterialGraphPass::PointShadow,MaterialGraphPass::Particle,MaterialGraphPass::DirectionalShadow,MaterialGraphPass::ModelReflection,MaterialGraphPass::ParticleReflection})
                check(bgfx::isValid(retiredPacket.program(pass)),"Packet did not pin all color/coverage/reflection variants.");

            for(auto& node:material.graph.nodes)if(node.kind==MaterialNodeKind::Scalar)node.scalar=.2f;
            const auto parameterShader=generateMaterialGraphShader(material);
            check(parameterShader.topologyKey==shader.topologyKey&&parameterShader.uniforms!=shader.uniforms,"Parameter fixture did not preserve shader topology.");
            const auto loadsBefore=resources.stats().loads;
            auto parameterOnly=programs.request(parameterShader);requireComplete(parameterOnly);
            check(parameterOnly.programs==first.programs&&resources.stats().loads==loadsBefore,"A parameter edit compiled or replaced the shared program bundle.");

            writeFileAtomically(editedSource,"#error Deliberate_material_graph_publication_failure\n"+originalSource);
            programs.changed(changedFiles);
            auto failingPending=programs.request(parameterShader);
            check(!failingPending.current&&failingPending.programs==first.programs,"Pending hot reload did not retain the complete last-good bundle.");
            auto failed=waitForResult(resources,programs,parameterShader,true,"deliberately invalid graph rejection",90s);
            check(!failed.current&&!failed.compiling&&!failed.diagnostic.empty()&&failed.programs==first.programs,"Failed shader compilation replaced the last-good bundle or remained actively compiling.");
            check(programs.residentBytes()==first.programs->bytes,"Failed compilation leaked a partially published program bundle.");
            // Complete every sibling job before changing its include files again.
            waitForCompilation(resources,[&]{resources.pump();return resources.idle();},"failed shader batch drain",90s);

            // bgfx shares shaders by binary hash and programs by shader pair.
            // A comment-only recovery can correctly reuse all five handles.
            // Change a runtime branch used by every pass so this fixture can
            // meaningfully detect mixing old and new program generations.
            auto recoveredCommon=originalCommon;
            const std::string fallback="if (settings.y < 0.5) return vec4_splat(1.0);";
            const auto fallbackPosition=recoveredCommon.find(fallback);
            check(fallbackPosition!=std::string::npos,"Graph common helper no longer contains the recovery fixture branch.");
            recoveredCommon.replace(fallbackPosition,fallback.size(),"if (settings.y < 0.5) return vec4_splat(0.75);");
            writeFileAtomically(editedSource,originalSource);
            writeFileAtomically(commonSource,recoveredCommon);
            const std::array recoveryFiles{editedSource,commonSource};
            programs.changed(recoveryFiles);
            auto recoveringPending=programs.request(parameterShader);
            check(!recoveringPending.current&&recoveringPending.programs==first.programs,"Recovery stopped retaining the previous complete shader generation.");
            auto recovered=waitForResult(resources,programs,parameterShader,true,"recovered complete optimized generation");requireComplete(recovered);
            check(recovered.programs!=first.programs&&recovered.programs->signature!=first.programs->signature,"Valid source recovery did not publish a new program generation.");
            for(const auto& variant:parameterShader.variants) {
                const auto binary=compileShader(config,materialGraphShaderRecipe(config,parameterShader,variant));
                check(binary.bytes!=originalFragments[static_cast<std::size_t>(variant.pass)],"Recovery fixture did not change a pass's compiled instructions.");
            }
            for(const auto& variant:parameterShader.variants) {
                const auto pass=static_cast<std::size_t>(variant.pass);
                check(recovered.programs->programs[pass].idx!=first.programs->programs[pass].idx,"Recovery mixed a previous program into the complete color/coverage/reflection bundle.");
            }
            check(programs.residentBytes()==first.programs->bytes+recovered.programs->bytes,"Pinned retired program bytes disappeared from residency accounting.");

            first.programs.reset();parameterOnly.programs.reset();failingPending.programs.reset();failed.programs.reset();recoveringPending.programs.reset();
            check(!retiredVersion.expired(),"A packet lost its retired generation while still holding a lease.");
            retiredPacket.programs.reset();
            check(retiredVersion.expired(),"Retired programs remained owned after the last packet released them.");
            programs.trim();
            check(programs.residentBytes()==recovered.programs->bytes,"Released retired GPU bytes remained counted.");
            retainedPacket.programs=recovered.programs;retainedVersion=recovered.programs;
        }
        check(!retainedVersion.expired(),"Destroying the program owner invalidated an outstanding packet lease.");
        resources.stop();
    }
    for(std::size_t pass=0;pass<5;++pass)check(bgfx::isValid(retainedPacket.programs->programs[pass]),"A complete generation did not survive its owner through the packet lease.");
    retainedPacket.programs.reset();
    check(retainedVersion.expired(),"Program generation was not destroyed before graphics shutdown.");
    for(int frame=0;frame<3;++frame)bgfx::frame();
}
}

int main(int argc,char** argv) {
    bool initialized=false;
    try {
        const bool profile=argc>=2&&std::string(argv[1])=="--profile-project";
        const bool engine=argc>=2&&std::string(argv[1])=="--profile-engine";
        const bool edit=argc>=2&&std::string(argv[1])=="--profile-edit";
        const bool lazy=argc>=2&&std::string(argv[1])=="--lazy-reflection-only";
        check((profile||lazy)?(argc==5||argc==6):(engine||edit)?argc==5:argc==4,"Expected shaderc/source/include, --lazy-reflection-only shaderc/source/include [warm-cache-directory], --profile-project projectRoot rendererBin output.json [--cold], --profile-edit projectRoot rendererBin output.json, or --profile-engine projectRoot rendererBin output.json.");
        bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
        check(bgfx::init(init),"Could not initialize headless Noop graphics checks.");initialized=true;
        if(profile)profileProject(argv[2],argv[3],argv[4],argc==6&&std::string(argv[5])=="--cold");
        else if(engine)profileEngine(argv[2],argv[3],argv[4]);
        else if(edit)profileProject(argv[2],argv[3],argv[4],false,true);
        else if(lazy)focusedLazyReflectionChecks(argv[2],argv[3],argv[4],argc==6?fs::path{argv[5]}:fs::path{});
        else {preparedSignatureChecks();preparedSourceCoherenceChecks();publicationChecks(argv[1],argv[2],argv[3]);unsupportedHeightChecks(argv[1],argv[2],argv[3]);}
        bgfx::shutdown();initialized=false;
        std::cout<<"Headless material shader publication, epoch, recovery and lease checks passed.\n";return 0;
    } catch(const std::exception& error) {
        if(initialized)bgfx::shutdown();
        std::cerr<<error.what()<<'\n';return 1;
    }
}
