#include "resources/ProjectMaterialCache.h"
#include "resources/ResourceManager.h"
#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include "assets/ContentStorage.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <array>
#include <algorithm>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct TemporaryProject {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-cache-prepare-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryProject(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&path.parent_path()==parent&&path.filename().string().starts_with("crimson-cache-prepare-"))std::filesystem::remove_all(path,error);}
};
void coverageStageIdentityChecks(const crimson::ShaderCompileConfig& config) {
    using namespace crimson;
    const auto variant=[](const MaterialGraphShader& shader,MaterialGraphPass pass) -> const MaterialGraphShaderVariant& {
        const auto found=std::find_if(shader.variants.begin(),shader.variants.end(),[&](const auto& value){return value.pass==pass;});
        check(found!=shader.variants.end(),"Material cache identity fixture omitted a required shader pass.");return *found;
    };
    for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha}) {
        auto material=defaultMaterialAsset();canonicalizeMaterialAsset(material);
        const auto outputId=materialOutputNode(material.graph).id;
        materialOutputNode(material.graph).output.blend=blend;
        const auto coverage=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
        const auto coveragePin=blend==MaterialBlend::Mask?MaterialPin::OpacityMask:MaterialPin::Opacity;
        if(blend!=MaterialBlend::Opaque)connectMaterialNodes(material.graph,coverage,MaterialPin::A,outputId,coveragePin);
        const auto original=generateMaterialGraphShader(material);
        // A new sampled color expression changes the surface topology, sampler
        // allocation and bundle key. None of those changes consumes coverage.
        const auto color=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
        const auto multiply=addMaterialNode(material.graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(material.graph,color,MaterialPin::RGB,multiply,MaterialPin::Left);
        connectMaterialNodes(material.graph,multiply,MaterialPin::Value,outputId,MaterialPin::BaseColor);
        const auto edited=generateMaterialGraphShader(material);
        check(edited.topologyKey!=original.topologyKey&&
            materialGraphShaderRecipe(config,original,variant(original,MaterialGraphPass::Model)).name!=
                materialGraphShaderRecipe(config,edited,variant(edited,MaterialGraphPass::Model)).name,
            "Material cache identity fixture did not change its surface compiler input.");
        for(const auto pass:{MaterialGraphPass::Picking,MaterialGraphPass::PointShadow,MaterialGraphPass::DirectionalShadow}) {
            const auto& before=variant(original,pass);const auto& after=variant(edited,pass);
            check(before.source==after.source&&before.vertexName==after.vertexName&&before.varyingName==after.varyingName&&
                materialGraphShaderRecipe(config,original,before).name==materialGraphShaderRecipe(config,edited,after).name,
                "A surface-only topology edit invalidated an independent coverage stage or cache identity.");
        }
        if(blend==MaterialBlend::Opaque)continue;
        // Prove the coverage identities are selective rather than constant:
        // editing their actual opacity expression must invalidate consumers.
        const auto opacity=addMaterialNode(material.graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(material.graph,coverage,MaterialPin::A,opacity,MaterialPin::Left);
        connectMaterialNodes(material.graph,opacity,MaterialPin::Value,outputId,coveragePin);
        const auto changed=generateMaterialGraphShader(material);
        for(const auto pass:{MaterialGraphPass::Picking,MaterialGraphPass::PointShadow,MaterialGraphPass::DirectionalShadow}) {
            const auto& before=variant(edited,pass);const auto& after=variant(changed,pass);
            const bool consumesCoverage=pass==MaterialGraphPass::Picking||blend==MaterialBlend::Mask;
            check((before.source!=after.source)==consumesCoverage&&
                (materialGraphShaderRecipe(config,edited,before).name!=materialGraphShaderRecipe(config,changed,after).name)==consumesCoverage,
                "A coverage edit did not invalidate exactly the depth stages that consume it.");
        }
    }
}
void heightCacheChecks(const std::filesystem::path& root,const crimson::ShaderCompileConfig& base) {
    using namespace crimson;
    std::filesystem::create_directories(root/"assets/materials");
    auto material=defaultMaterialAsset();canonicalizeMaterialAsset(material);
    auto& output=materialOutputNode(material.graph);const auto outputId=output.id;
    output.output.tessellation.enabled=true;
    const auto texture=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,texture)->texture.role=MaterialTextureRole::Data;
    connectMaterialNodes(material.graph,texture,MaterialPin::R,outputId,MaterialPin::Height);
    material.id="assets/materials/height-a.crimson-material";
    writeFileAtomically(root/material.id,serializeMaterialAsset(material));
    material.id="assets/materials/height-b.crimson-material";
    materialOutputNode(material.graph).output.tessellation.amount=.3f;
    writeFileAtomically(root/material.id,serializeMaterialAsset(material));
    const std::vector files{root/"assets/materials/height-a.crimson-material",root/material.id};
    const auto authored=fingerprintFiles(files);
    auto config=base;config.cacheDirectory=root/"assets/cache/shaders";
    std::set<std::string> vertices,generated;
    std::size_t domains{};
    for(const auto& asset:{material,defaultMaterialAsset(),defaultParticleMaterialAsset()}) {
        const auto shader=generateMaterialGraphShader(asset);
        for(const auto& pass:shader.variants) {
            vertices.insert(pass.vertexName);generated.insert(materialGraphShaderRecipe(config,shader,pass).name);
            if(pass.tessellation) {
                ++domains;const auto recipe=materialGraphDomainShaderRecipe(config,shader,pass);
                check(recipe.raw&&recipe.stage==ShaderStage::Domain,"Height did not request a native domain recipe.");
                generated.insert(recipe.name);
            }
        }
    }
    check(domains>0,"Enabled material height did not produce tessellation stages.");
    const auto expected=vertices.size()+generated.size();
    const auto first=prepareProjectMaterialCache(root,config);
    if(first.failed||first.prepared!=3||first.topologies!=3||first.stages!=expected||first.metrics.compilerInvocations!=expected)
        throw std::runtime_error("Height cache preparation did not publish coherent shared stages: "+serializeProjectMaterialCacheReport(first));
    const auto warm=prepareProjectMaterialCache(root,config);
    check(warm.failed==0&&warm.cacheHits==expected&&warm.metrics.compilerInvocations==0,
        "Warm height preparation recompiled a domain or lost stage coalescing.");
    const auto shader=generateMaterialGraphShader(material);
    const auto pass=std::find_if(shader.variants.begin(),shader.variants.end(),[](const auto& value){return value.tessellation;});
    check(pass!=shader.variants.end(),"Height material has no domain recovery fixture.");
    const auto recipe=materialGraphDomainShaderRecipe(config,shader,*pass);
    ShaderCacheReadSession cache(config);const auto valid=cache.find(recipe);
    check(valid.has_value(),"Prepared domain is absent from the persistent cache.");
    auto wrongRecipe=recipe;wrongRecipe.name="wrong-domain-interface";
    wrongRecipe.source=R"(
struct Control { float3 position : TEXCOORD0; };
struct Constants { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
[domain("tri")]
float4 main(Constants constants,float3 barycentric : SV_DomainLocation,const OutputPatch<Control,3> patch) : SV_Position {
    return float4(patch[0].position*barycentric.x+patch[1].position*barycentric.y+patch[2].position*barycentric.z,1.0);
}
)";
    const auto wrong=compileShader(config,wrongRecipe);
    std::filesystem::path artifact;const auto suffix="-"+std::to_string(valid->fingerprint)+".bin";
    for(const auto& file:std::filesystem::directory_iterator(config.cacheDirectory))
        if(const auto name=file.path().filename().string();name.starts_with(recipe.name+"-")&&name.ends_with(suffix))artifact=file.path();
    check(!artifact.empty(),"Cannot locate the disposable domain cache recovery artifact.");
    {std::ofstream out(artifact,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(wrong.bytes.data()),wrong.bytes.size());check(bool(out),"Cannot write the domain cache fixture.");}
    const auto repaired=prepareProjectMaterialCache(root,config);
    if(repaired.failed||repaired.prepared!=3||repaired.metrics.compilerInvocations==0)
        throw std::runtime_error("A valid-envelope domain with a wrong interface was not repaired: "+serializeProjectMaterialCacheReport(repaired));
    const auto repairedWarm=prepareProjectMaterialCache(root,config);
    check(repairedWarm.failed==0&&repairedWarm.cacheHits==expected&&repairedWarm.metrics.compilerInvocations==0,
        "The repaired domain did not remain coherent on a warm cache preparation.");
    check(fingerprintFiles(files)==authored,"Height preparation rewrote authored materials.");
}

}
int main(int argc,char** argv) {
    try {
        check(argc==4,"Expected compiler, shader source and include directories.");TemporaryProject temporary;
        std::filesystem::create_directories(temporary.root/"assets/materials");
        auto material=crimson::defaultMaterialAsset();material.id="assets/materials/a.crimson-material";
        crimson::writeFileAtomically(temporary.root/material.id,crimson::serializeMaterialAsset(material));
        material.id="assets/materials/b.crimson-material";material.name="Same topology, another identity";
        crimson::writeFileAtomically(temporary.root/material.id,crimson::serializeMaterialAsset(material));
        const std::vector files{temporary.root/"assets/materials/a.crimson-material",temporary.root/material.id};
        const auto authored=crimson::fingerprintFiles(files);
        const crimson::ShaderCompileConfig config{argv[1],argv[2],argv[3],temporary.root/"assets/cache/shaders"};
        std::set<std::string> vertexStages,fragmentStages;
        for(const auto& asset:{crimson::defaultMaterialAsset(),crimson::defaultParticleMaterialAsset()}) {
            const auto generated=crimson::generateMaterialGraphShader(asset);
            for(const auto& pass:generated.variants) {
                vertexStages.insert(pass.vertexName);
                fragmentStages.insert(crimson::materialGraphShaderRecipe(config,generated,pass).name);
            }
        }
        const auto uniqueStages=vertexStages.size()+fragmentStages.size();
        coverageStageIdentityChecks(config);
        const auto first=crimson::prepareProjectMaterialCache(temporary.root,config);
        if(first.failed!=0||first.materials!=4||first.topologies!=2||first.prepared!=2)
            throw std::runtime_error("Build preparation did not coalesce equal authored material graphs: "+crimson::serializeProjectMaterialCacheReport(first));
        check(first.stages==uniqueStages&&first.metrics.compilerInvocations==uniqueStages,"Build preparation requested or compiled a shared stage more than once.");
        const auto second=crimson::prepareProjectMaterialCache(temporary.root,config);
        check(second.failed==0&&second.prepared==2&&second.cacheHits==uniqueStages&&second.metrics.cacheOnlyRequests==uniqueStages&&second.metrics.compilerInvocations==0,
            "Repeated build preparation missed its content cache or reread a shared stage.");
        const auto shader=crimson::generateMaterialGraphShader(crimson::defaultMaterialAsset());
        const auto& variant=shader.variants.front();const auto recipe=crimson::materialGraphShaderRecipe(config,shader,variant);
        crimson::ShaderCacheReadSession cache(config);const auto valid=cache.find(recipe);
        check(valid.has_value(),"The prepared material fragment is unavailable for the recovery fixture.");
        const auto unrelated=crimson::compileShader(config,"fs_encode");
        std::filesystem::path artifact;
        const auto suffix="-"+std::to_string(valid->fingerprint)+".bin";
        for(const auto& file:std::filesystem::directory_iterator(config.cacheDirectory)) {
            const auto name=file.path().filename().string();
            if(name.starts_with(recipe.name+"-")&&name.ends_with(suffix))artifact=file.path();
        }
        check(!artifact.empty(),"Cannot find the disposable fragment artifact to corrupt.");
        {std::ofstream out(artifact,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(unrelated.bytes.data()),unrelated.bytes.size());check(bool(out),"Could not publish the damaged cache fixture.");}
        const auto repaired=crimson::prepareProjectMaterialCache(temporary.root,config);
        check(repaired.failed==0&&repaired.prepared==2&&repaired.metrics.compilerInvocations>0&&repaired.metrics.compilerInvocations<=2,
            "A valid-envelope fragment with the wrong shader ABI was not repaired once during build preparation.");
        const auto repairedWarm=crimson::prepareProjectMaterialCache(temporary.root,config);
        check(repairedWarm.failed==0&&repairedWarm.cacheHits==uniqueStages&&repairedWarm.metrics.compilerInvocations==0,
            "The repaired shader artifact was not published coherently to the persistent cache.");
        crimson::writeFileAtomically(temporary.root/"assets/materials/broken.crimson-material","invalid fixture");
        const auto broken=crimson::prepareProjectMaterialCache(temporary.root,config);
        check(broken.failed==1&&!broken.diagnostics.empty()&&broken.prepared==2,"An invalid unused material prevented valid cache preparation or hid its diagnostic.");
        check(crimson::fingerprintFiles(files)==authored,"Shader preparation rewrote authored material files.");
        crimson::initializeContentStorage(temporary.root);
        const auto source=crimson::resolveAssetPath(temporary.root,"assets/materials/b.crimson-material");
        const auto destination=temporary.root/"Content/Levels/Act One/Surface.crimson-material";
        const std::array<crimson::ContentPathMove,1> moves{{{source,destination,false}}};crimson::relocateContentPaths(temporary.root,moves);
        auto movedConfig=config;movedConfig.cacheDirectory=crimson::resolveAssetPath(temporary.root,"assets/cache/shaders");
        const auto moved=crimson::prepareProjectMaterialCache(temporary.root,movedConfig);
        check(moved.materials==5&&moved.failed==1&&moved.prepared==2&&moved.metrics.compilerInvocations==0,
            "Content cache preparation missed a relocated descendant material, duplicated Engine defaults or lost the existing shader cache.");
        const auto missing=crimson::prepareProjectMaterialCache(temporary.root/"no project",config);
        check(missing.materials==0&&missing.stages==0&&missing.metrics.compilerInvocations==0,"Missing project queued material compilation.");
        heightCacheChecks(temporary.root/"height project",config);
        std::cout<<"Project shader preparation, duplicate topology, persistent cache and authored-file preservation checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
