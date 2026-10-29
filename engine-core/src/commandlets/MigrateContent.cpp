#include "assets/ContentStorage.h"
#include "assets/QuaderSceneBundle.h"
#include "assets/AtomicFile.h"
#include "resources/Paths.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        std::filesystem::path root,report;
        for(int index=1;index<argc;++index) {
            const std::string option=argv[index];
            if(index+1>=argc)throw std::runtime_error("Expected a value after "+option);
            const auto value=crimson::pathFromUtf8(argv[++index]);
            if(option=="--project-root")root=value;
            else if(option=="--report")report=value;
            else throw std::runtime_error("Unknown option: "+option);
        }
        if(root.empty())throw std::runtime_error("Specify --project-root for the game to migrate.");
        root=std::filesystem::weakly_canonical(root);
        if(!std::filesystem::is_directory(root))throw std::runtime_error("The project directory does not exist.");
        crimson::initializeContentStorage(root);
        nlohmann::json result{{"version",1},{"bundles",nlohmann::json::array()}};
        std::size_t models{};
        for(const auto& entry:crimson::storedAssetPaths(root)) {
            if(entry.file.extension()!=".crimson-quader"||!std::filesystem::is_regular_file(entry.file))continue;
            std::ifstream input(entry.file,std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
            const auto bundle=crimson::decodeQuaderSceneBundle(bytes);models+=bundle.models.size();
            result["bundles"].push_back({{"id",bundle.id},{"name",bundle.name},{"models",bundle.models.size()},
                {"file",entry.file.lexically_relative(root).generic_string()}});
        }
        result["modelCount"]=models;
        if(!report.empty())crimson::writeFileAtomically(report,result.dump(2)+"\n");
        std::cout<<result.dump(2)<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Content migration failed: "<<error.what()<<'\n';return 1;}
}
