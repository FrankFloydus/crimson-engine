#include "resources/ShaderCompiler.h"
#include "resources/Paths.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace {
using namespace crimson;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void write(const std::filesystem::path& path,const std::string& text) {
    std::filesystem::create_directories(path.parent_path()); std::ofstream output(path,std::ios::binary); output<<text;
    if (!output) throw std::runtime_error("Cannot write disposable shader fixture.");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary); return {std::istreambuf_iterator<char>(input),{}};
}
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("crimson-shader-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { std::filesystem::create_directories(root); }
    ~Temp() {
        std::error_code error; const auto target=std::filesystem::weakly_canonical(root,error);
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if (!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-shader-test-")) std::filesystem::remove_all(target,error);
    }
};
void run(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    Temp temp;
    const auto directory=temp.root/"shader source with spaces", cache=temp.root/"compiled cache";
    std::filesystem::create_directories(directory);
    const auto original=read(source/"fs_encode.sc");
    write(directory/"fs_encode.sc","#define CRIMSON_RELOAD_HEADER \"reload-extra.sc\"\n#include CRIMSON_RELOAD_HEADER\n"+original);
    write(directory/"reload-extra.sc","#define CRIMSON_TEST_VALUE 1\n");
    std::filesystem::copy_file(source/"varying.def.sc",directory/"varying.def.sc");
    const ShaderCompileConfig config{compiler,directory,include,cache};
    const auto first=compileShader(config,"fs_encode");
    require(first.bytes.size()>16 && first.bytes[0]=='F' && first.bytes[1]=='S' && first.bytes[2]=='H' && first.bytes[3]==11,"Pinned shader compilation produced the wrong envelope.");
    require(std::any_of(first.dependencies.begin(),first.dependencies.end(),[](const auto& file){return file.filename()=="reload-extra.sc";}),"Compiler-reported macro include was lost.");
    const auto discovered=shaderDependencies(config,"fs_encode");
    require(std::any_of(discovered.begin(),discovered.end(),[](const auto& file){return file.filename()=="reload-extra.sc";}),"Actual dependency index was not reused.");
    const auto warm=compileShader(config,"fs_encode");
    require(warm.bytes==first.bytes && warm.fingerprint==first.fingerprint,"Warm shader cache changed code or fingerprint.");
    write(directory/"reload-extra.sc","#define CRIMSON_TEST_VALUE 2\n");
    const auto changed=compileShader(config,"fs_encode");
    require(changed.fingerprint!=first.fingerprint,"Editing a macro include did not invalidate compiled cache.");
    auto other=config; other.buildContract+=";another-interface";
    const auto contract=compileShader(other,"fs_encode");
    require(contract.fingerprint!=changed.fingerprint,"Different compiler/build contract reused shader cache.");
    write(directory/"fs_encode.sc","this is invalid shader syntax");
    bool failed=false; try { compileShader(config,"fs_encode"); } catch (const std::exception&) { failed=true; }
    require(failed && !changed.bytes.empty(),"Bad source did not fail while retaining previous candidate data.");
    const auto dependencyFile=temp.root/"paths.d";
    write(dependencyFile,"C:\\output with spaces\\shader.bin :  \\\n C:\\source with spaces\\first.sc \\\n C:\\include\\second.sh\n");
    const auto parsed=parseShaderDependencyFile(dependencyFile);
    require(parsed.size()==2 && parsed[0].wstring()==L"C:\\source with spaces\\first.sc" && parsed[1].wstring()==L"C:\\include\\second.sh","Dependency parser split spaces or drive paths.");
    other=config; other.compiler=temp.root/"missing shaderc.exe"; failed=false;
    try { compileShader(other,"fs_encode"); } catch (const std::exception&) { failed=true; }
    require(failed,"Missing runtime compiler did not retain the embedded fallback path.");
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=4) throw std::runtime_error("Pass compiler, engine source directory, and pinned include directory.");
        run(pathFromUtf8(argv[1]),pathFromUtf8(argv[2]),pathFromUtf8(argv[3]));
        std::cout<<"Shader compiler checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
