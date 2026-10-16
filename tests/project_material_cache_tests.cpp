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
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct TemporaryProject {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-cache-prepare-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryProject(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&path.parent_path()==parent&&path.filename().string().starts_with("crimson-cache-prepare-"))std::filesystem::remove_all(path,error);}
};
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
        check(uniqueStages<15,"Coverage stages are still keyed by the whole material family.");
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
        std::cout<<"Project shader preparation, duplicate topology, persistent cache and authored-file preservation checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
