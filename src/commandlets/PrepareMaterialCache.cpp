#include "resources/ProjectMaterialCache.h"
#include "resources/Paths.h"
#include "assets/AtomicFile.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        std::filesystem::path project,bin,output;bool strict=false;
        for(int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--strict"){strict=true;continue;}
            if(i+1>=argc)throw std::runtime_error("Expected a value after "+option);
            const auto value=crimson::pathFromUtf8(argv[++i]);
            if(option=="--project-root")project=value;
            else if(option=="--renderer-bin")bin=value;
            else if(option=="--report")output=value;
            else throw std::runtime_error("Unknown option: "+option);
        }
        if(project.empty()) {std::cout<<"No project selected; material cache preparation skipped.\n";return 0;}
        project=std::filesystem::weakly_canonical(project);
        if(bin.empty())bin=std::filesystem::absolute(crimson::pathFromUtf8(argv[0])).parent_path();
        bin=std::filesystem::weakly_canonical(bin);
        if(!std::filesystem::is_directory(project/"assets")){std::cout<<"No project assets; material cache preparation skipped.\n";return 0;}
        const crimson::ShaderCompileConfig config{bin/"shader-tools/shaderc.exe",bin/"shader-tools/shaders",bin/"shader-tools/include",project/"assets/cache/shaders"};
        if(!std::filesystem::is_regular_file(config.compiler)||!std::filesystem::is_directory(config.sourceDirectory)||!std::filesystem::is_directory(config.includeDirectory))
            throw std::runtime_error("The paired renderer shader payload is unavailable.");
        const auto result=crimson::prepareProjectMaterialCache(project,config);
        if(!output.empty()){std::filesystem::create_directories(std::filesystem::absolute(output).parent_path());crimson::writeFileAtomically(output,crimson::serializeProjectMaterialCacheReport(result));}
        for(const auto& diagnostic:result.diagnostics)std::cerr<<"Material cache warning: "<<diagnostic<<'\n';
        std::cout<<"Material cache: "<<result.prepared<<'/'<<result.topologies<<" topologies prepared, "<<result.cacheHits<<" cached stages, "
            <<result.metrics.compilerInvocations<<" compiler processes, "<<result.failed<<" failures, "<<result.elapsedMilliseconds<<" ms.\n";
        return strict&&result.failed?1:0;
    }catch(const std::exception& error){std::cerr<<"Material cache preparation: "<<error.what()<<'\n';return 1;}
}
