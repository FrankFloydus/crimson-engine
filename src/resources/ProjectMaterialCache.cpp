#include "ProjectMaterialCache.h"
#include "Paths.h"
#include "assets/MaterialAsset.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "vs_model.h"
#include "vs_pick.h"
#include "vs_point_shadow.h"
#include "vs_directional_shadow.h"
#include "vs_particle.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>

namespace crimson {
namespace {
std::span<const std::uint8_t> embeddedVertex(MaterialGraphPass pass) {
    switch(pass){
    case MaterialGraphPass::Model:case MaterialGraphPass::ModelTransmission:return cc_vs_model;
    case MaterialGraphPass::Picking:return cc_vs_pick;
    case MaterialGraphPass::PointShadow:return cc_vs_point_shadow;
    case MaterialGraphPass::DirectionalShadow:return cc_vs_directional_shadow;
    case MaterialGraphPass::Particle:case MaterialGraphPass::ParticleTransmission:return cc_vs_particle;
    }
    throw std::runtime_error("Unknown material pass.");
}
ShaderCompilerMetrics difference(ShaderCompilerMetrics before,ShaderCompilerMetrics after) {
    return {after.compilerInvocations-before.compilerInvocations,after.compilerNanoseconds-before.compilerNanoseconds,
        after.dependencyRequests-before.dependencyRequests,after.dependencyNanoseconds-before.dependencyNanoseconds,
        after.cacheOnlyRequests-before.cacheOnlyRequests,after.cacheOnlyHits-before.cacheOnlyHits,
        after.cacheOnlyNanoseconds-before.cacheOnlyNanoseconds,after.workerRequests-before.workerRequests,
        after.workerCacheHits-before.workerCacheHits};
}
}
ProjectMaterialCacheReport prepareProjectMaterialCache(const std::filesystem::path& project,const ShaderCompileConfig& config) {
    ProjectMaterialCacheReport report;const auto started=std::chrono::steady_clock::now();const auto before=shaderCompilerMetrics();
    std::vector<std::string> ids;
    if(contentStorageReady(project)) {
        for(const auto& entry:storedAssetPaths(project))
            if(!isEngineAssetId(entry.id)&&entry.file.extension()==".crimson-material"&&std::filesystem::is_regular_file(entry.file))ids.push_back(entry.id);
    } else {
        if(!std::filesystem::is_directory(project/"assets"))return report;
        const auto materialDirectory=project/"assets/materials";
        if(std::filesystem::is_directory(materialDirectory))for(const auto& entry:std::filesystem::directory_iterator(materialDirectory))
            if(entry.is_regular_file()&&entry.path().extension()==".crimson-material") {
                const auto id=entry.path().lexically_relative(project).generic_u8string();
                ids.emplace_back(reinterpret_cast<const char*>(id.data()),id.size());
            }
    }
    std::sort(ids.begin(),ids.end());ids.push_back(DefaultMaterialId);ids.push_back(DefaultParticleMaterialId);
    std::map<std::string,MaterialGraphShader> graphs;
    for(const auto& id:ids) {
        ++report.materials;
        try {auto material=loadMaterialAsset(project,id);auto shader=generateMaterialGraphShader(material);graphs.try_emplace(shader.topologyKey,std::move(shader));}
        catch(const std::exception& error){++report.failed;report.diagnostics.push_back(id+": "+error.what());}
    }
    struct Stage {ShaderBinary binary;bool cached{};};
    report.topologies=graphs.size();std::map<std::string,Stage> vertices,generatedStages;
    ShaderCacheReadSession cache(config);
    const auto loadVertex=[&](const std::string& name)->Stage& {
        if(const auto found=vertices.find(name);found!=vertices.end())return found->second;
        auto binary=cache.find(name);
        const bool cached=binary.has_value();
        if(cached)++report.cacheHits;else binary=compileShader(config,name);
        ++report.stages;return vertices.emplace(name,Stage{std::move(*binary),cached}).first->second;
    };
    const auto loadGenerated=[&](const ShaderCompileRecipe& recipe)->Stage& {
        // Generated recipe names describe individual compiler inputs. Coverage
        // passes and different materials can therefore share the same stage.
        if(const auto found=generatedStages.find(recipe.name);found!=generatedStages.end())return found->second;
        auto binary=cache.find(recipe);
        const bool cached=binary.has_value();
        if(cached)++report.cacheHits;else binary=compileShader(config,recipe);
        ++report.stages;return generatedStages.emplace(recipe.name,Stage{std::move(*binary),cached}).first->second;
    };
    for(const auto& [key,shader]:graphs) {
        try {
            for(const auto& variant:shader.variants) {
                auto& vertex=loadVertex(variant.vertexName);
                const auto recipe=materialGraphShaderRecipe(config,shader,variant);
                auto& fragment=loadGenerated(recipe);
                const auto domainRecipe=variant.tessellation?materialGraphDomainShaderRecipe(config,shader,variant):ShaderCompileRecipe{};
                Stage* domain=variant.tessellation?&loadGenerated(domainRecipe):nullptr;
                std::string diagnostic;
                const auto preflight=[&] {
                    return preflightGeneratedShaderPair(vertex.binary.bytes,fragment.binary.bytes,embeddedVertex(variant.pass),variant.manifest,diagnostic)&&
                        (!domain||preflightGeneratedTessellationDomain(domain->binary.bytes,embeddedVertex(variant.pass),variant.domainManifest,diagnostic));
                };
                if(!preflight()) {
                    // The disk envelope is only a quick cache check. Repair a
                    // structurally valid artifact rejected by full ABI/DXBC
                    // validation here, so startup does not rediscover it later.
                    if(!vertex.cached&&!fragment.cached&&(!domain||!domain->cached))throw std::runtime_error("Shader preflight: "+diagnostic);
                    if(!cache.unchanged())throw std::runtime_error("Shader inputs changed before cache repair.");
                    std::vector<ShaderBinary> rejected;
                    if(vertex.cached)rejected.push_back(vertex.binary);
                    if(fragment.cached)rejected.push_back(fragment.binary);
                    if(domain&&domain->cached)rejected.push_back(domain->binary);
                    cache.discard(rejected);
                    vertex.binary=compileShader(config,variant.vertexName);vertex.cached=false;
                    fragment.binary=compileShader(config,recipe);fragment.cached=false;
                    if(domain){domain->binary=compileShader(config,domainRecipe);domain->cached=false;}
                    diagnostic.clear();
                    if(!preflight())
                        throw std::runtime_error("Fresh shader preflight after cache repair: "+diagnostic);
                }
            }
            if(!cache.unchanged())throw std::runtime_error("Shader inputs changed during project cache preparation.");
            ++report.prepared;
        } catch(const std::exception& error){++report.failed;report.diagnostics.push_back(key+": "+error.what());}
    }
    report.elapsedMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    report.metrics=difference(before,shaderCompilerMetrics());return report;
}
std::string serializeProjectMaterialCacheReport(const ProjectMaterialCacheReport& report) {
    return nlohmann::json{{"materials",report.materials},{"uniqueTopologies",report.topologies},{"preparedTopologies",report.prepared},
        {"failures",report.failed},{"stageRequests",report.stages},{"cacheHits",report.cacheHits},{"elapsedMs",report.elapsedMilliseconds},
        {"compilerInvocations",report.metrics.compilerInvocations},{"compilerMs",report.metrics.compilerNanoseconds/1e6},
        {"diagnostics",report.diagnostics}}.dump(2)+"\n";
}
}
