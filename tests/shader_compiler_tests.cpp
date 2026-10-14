#include "resources/ShaderCompiler.h"
#include "resources/Paths.h"
#include "crimson-renderer/ShaderPreflight.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstring>

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
std::vector<std::uint8_t> samplerEnvelopeSlot(std::vector<std::uint8_t> bytes,const std::string& name,std::uint16_t slot) {
    require(bytes.size()>=14,"Invalid compiled envelope fixture.");
    std::uint16_t count{};std::memcpy(&count,bytes.data()+12,sizeof(count));std::size_t cursor=14;
    for (unsigned i=0;i<count;++i) {
        require(cursor<bytes.size(),"Truncated compiled uniform fixture.");const auto length=bytes[cursor++];
        require(cursor+length+10<=bytes.size(),"Truncated compiled binding fixture.");
        const std::string candidate(reinterpret_cast<const char*>(bytes.data()+cursor),length);cursor+=length;
        if (candidate==name) {std::memcpy(bytes.data()+cursor+2,&slot,sizeof(slot));return bytes;}
        cursor+=10;
    }
    throw std::runtime_error("Compiled fixture has no requested sampler.");
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

    // Compile real shadow contracts, then prove that an otherwise compatible
    // sampler moved to the wrong renderer slot cannot enter the live group.
    const auto shadowSource=temp.root/"shadow sources";
    std::filesystem::copy(source,shadowSource,std::filesystem::copy_options::recursive);
    const ShaderCompileConfig shadows{compiler,shadowSource,include,temp.root/"shadow cache"};
    const auto names=engineShaderNames();
    require(std::find(names.begin(),names.end(),"vs_point_shadow")!=names.end() &&
        std::find(names.begin(),names.end(),"fs_point_shadow")!=names.end(),"Shadow stages are absent from atomic reload discovery.");
    const auto vertex=compileShader(shadows,"vs_point_shadow"),fragment=compileShader(shadows,"fs_point_shadow");
    std::string diagnostic;
    require(preflightShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,fragment.bytes,diagnostic),"Valid caster contract failed preflight.");
    auto caster=read(shadowSource/"fs_point_shadow.sc");const auto sampler=caster.find("SAMPLER2D(s_albedo, 0)");
    require(sampler!=std::string::npos,"Caster fixture cannot find alpha sampler zero.");
    caster.replace(sampler,std::string("SAMPLER2D(s_albedo, 0)").size(),"SAMPLER2D(s_albedo, 1)");
    write(shadowSource/"fs_point_shadow.sc",caster);const auto moved=compileShader(shadows,"fs_point_shadow");
    require(!preflightShaderPair(vertex.bytes,moved.bytes,vertex.bytes,fragment.bytes,diagnostic),"Moving caster alpha sampler to slot one was accepted.");
    const auto disguised=samplerEnvelopeSlot(moved.bytes,"s_albedo",0);
    require(!preflightShaderPair(vertex.bytes,disguised,vertex.bytes,fragment.bytes,diagnostic),"DXBC sampler relocation escaped validation when its bgfx metadata retained slot zero.");
    const auto modelVertex=compileShader(shadows,"vs_model"),modelFragment=compileShader(shadows,"fs_model");
    auto receiver=read(shadowSource/"point_shadow.sh");const auto receiverSampler=receiver.find("SAMPLER2DSHADOW(s_pointShadow, 8)");
    require(receiverSampler!=std::string::npos,"Receiver fixture cannot find shadow sampler eight.");
    receiver.replace(receiverSampler,std::string("SAMPLER2DSHADOW(s_pointShadow, 8)").size(),"SAMPLER2DSHADOW(s_pointShadow, 9)");
    write(shadowSource/"point_shadow.sh",receiver);const auto wrongReceiver=compileShader(shadows,"fs_model");
    require(!preflightShaderPair(modelVertex.bytes,wrongReceiver.bytes,modelVertex.bytes,modelFragment.bytes,diagnostic),"Moving shadow comparison sampler to slot nine was accepted.");
    require(std::any_of(modelFragment.dependencies.begin(),modelFragment.dependencies.end(),[](const auto& path){return path.filename()=="point_shadow.sh";}),"Shadow receiver include was not tracked for reload.");
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=4) throw std::runtime_error("Pass compiler, engine source directory, and pinned include directory.");
        run(pathFromUtf8(argv[1]),pathFromUtf8(argv[2]),pathFromUtf8(argv[3]));
        std::cout<<"Shader compiler checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
