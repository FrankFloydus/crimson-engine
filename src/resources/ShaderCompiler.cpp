#include "ShaderCompiler.h"
#include "ResourceManager.h"
#include "Paths.h"
#include "assets/AtomicFile.h"
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace crimson {
namespace {
const std::map<std::string,std::string> varying{
    {"vs_particle","particle.varying.def.sc"},{"fs_particle","particle.varying.def.sc"},
    {"vs_pbr","varying.def.sc"},{"fs_pbr","varying.def.sc"},{"vs_model","model.varying.def.sc"},{"fs_model","model.varying.def.sc"},
    {"vs_pick","model.varying.def.sc"},{"fs_pick","model.varying.def.sc"},{"vs_present","varying.def.sc"},{"fs_present","varying.def.sc"},
    {"vs_grid","varying.def.sc"},{"fs_grid","varying.def.sc"},{"fs_encode","varying.def.sc"},{"fs_sky","varying.def.sc"},{"vs_imgui","imgui.varying.def.sc"},{"fs_imgui","imgui.varying.def.sc"}};
struct NativeHandle { HANDLE value{INVALID_HANDLE_VALUE}; ~NativeHandle(){if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value);} };
std::string utf8(const std::filesystem::path& path) {
    const auto text=path.generic_u8string(); return {reinterpret_cast<const char*>(text.data()),text.size()};
}
std::vector<std::filesystem::path> sortedDependencies(std::vector<std::filesystem::path> files) {
    for (auto& file:files) file=std::filesystem::weakly_canonical(std::filesystem::absolute(file));
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return resourcePathKey(a)<resourcePathKey(b);});
    files.erase(std::unique(files.begin(),files.end(),[](const auto& a,const auto& b){return resourcePathKey(a)==resourcePathKey(b);}),files.end());
    return files;
}
std::filesystem::path indexPath(const ShaderCompileConfig& config,const std::string& name) { return config.cacheDirectory/(name+".dependencies.json"); }
uint64_t compileFingerprint(const ShaderCompileConfig& config,const std::string& name,const std::vector<std::filesystem::path>& dependencies) {
    auto result=fingerprintFiles(dependencies);
    const auto contract=config.buildContract+";"+name+";"+varying.at(name)+";"+(name.starts_with("vs_") ? "vertex" : "fragment");
    for (const unsigned char byte:contract) { result^=byte; result*=1099511628211ull; }
    return result;
}
void saveDependencyIndex(const ShaderCompileConfig& config,const std::string& name,const std::vector<std::filesystem::path>& dependencies) {
    nlohmann::json paths=nlohmann::json::array();
    for (const auto& file:dependencies) paths.push_back(utf8(file));
    const nlohmann::json index{{"version",2},{"contract",config.buildContract},{"source",resourcePathKey(config.sourceDirectory)},
        {"include",resourcePathKey(config.includeDirectory)},{"compiler",resourcePathKey(config.compiler)},{"dependencies",std::move(paths)}};
    writeFileAtomically(indexPath(config,name),index.dump(2)+"\n");
}
std::vector<std::uint8_t> readBinary(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) throw std::runtime_error("Shader file is unavailable.");
    const auto length=input.tellg(); if(length<=0 || length>64*1024*1024) throw std::runtime_error("Shader file has an invalid size.");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length)); input.seekg(0); input.read(reinterpret_cast<char*>(data.data()),length);
    if(!input) throw std::runtime_error("Shader file could not be read."); return data;
}
std::wstring quoted(const std::filesystem::path& path) {
    const auto input=path.wstring(); std::wstring output=L"\""; std::size_t slashes=0;
    for(const auto character:input) {
        if(character==L'\\') { ++slashes; continue; }
        output.append(character==L'"' ? slashes*2+1 : slashes,L'\\'); slashes=0; output+=character;
    }
    output.append(slashes*2,L'\\'); output+=L'"'; return output;
}
std::string runCompiler(const std::filesystem::path& executable,std::wstring arguments,std::stop_token token) {
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE}; NativeHandle readPipe,writePipe;
    if(!CreatePipe(&readPipe.value,&writePipe.value,&attributes,0) || !SetHandleInformation(readPipe.value,HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error("Could not capture shader compiler output.");
    NativeHandle nullInput{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if (nullInput.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Could not initialize shader compiler input.");
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdOutput=writePipe.value; startup.hStdError=writePipe.value; startup.hStdInput=nullInput.value;
    PROCESS_INFORMATION process{}; auto command=quoted(executable)+L" "+arguments;
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("Could not start the pinned shader compiler.");
    NativeHandle child{process.hProcess},thread{process.hThread}; CloseHandle(writePipe.value); writePipe.value=INVALID_HANDLE_VALUE;
    std::string output; std::array<char,4096> bytes{};
    const auto drain=[&] {
        DWORD available{};
        while(PeekNamedPipe(readPipe.value,nullptr,0,nullptr,&available,nullptr) && available) {
            DWORD received{}; const auto count=std::min<DWORD>(available,static_cast<DWORD>(bytes.size()));
            if(!ReadFile(readPipe.value,bytes.data(),count,&received,nullptr)) break;
            if(output.size()<64*1024) output.append(bytes.data(),std::min<std::size_t>(received,64*1024-output.size()));
        }
    };
    while(WaitForSingleObject(child.value,10)==WAIT_TIMEOUT) {
        drain(); if(token.stop_requested()) { TerminateProcess(child.value,ERROR_CANCELLED); WaitForSingleObject(child.value,INFINITE); throw std::runtime_error("Shader compilation canceled."); }
    }
    drain(); DWORD code{}; if(!GetExitCodeProcess(child.value,&code) || code!=0)
        throw std::runtime_error(output.empty() ? "Shader compilation failed." : output);
    return output;
}
}
std::vector<std::string> engineShaderNames() { std::vector<std::string> result; for(const auto& [name,definition]:varying) { (void)definition; result.push_back(name); } return result; }
std::vector<std::filesystem::path> parseShaderDependencyFile(const std::filesystem::path& file) {
    std::ifstream input(file); if (!input) throw std::runtime_error("Shader compiler did not publish its dependency file.");
    std::vector<std::filesystem::path> result; std::string line; bool first=true;
    // Pinned shaderc writes one full dependency path per continuation line. It
    // does not shell-tokenize paths: preserve embedded spaces and drive colons.
    while (std::getline(input,line)) {
        if (first) {
            const auto separator=line.find(" : ");
            if (separator==std::string::npos) throw std::runtime_error("Invalid shader dependency header.");
            line.erase(0,separator+3); first=false;
        }
        auto last=line.find_last_not_of(" \t\r");
        if (last!=std::string::npos && line[last]=='\\') line.erase(last);
        const auto begin=line.find_first_not_of(" \t\r"),end=line.find_last_not_of(" \t\r");
        if (begin==std::string::npos) continue;
        result.push_back(pathFromUtf8(line.substr(begin,end-begin+1)));
        if (result.size()>256) throw std::runtime_error("Shader dependency graph is too large.");
    }
    if (first) throw std::runtime_error("Shader dependency file is empty.");
    return result;
}
std::vector<std::filesystem::path> shaderDependencies(const ShaderCompileConfig& config,const std::string& name) {
    const auto spec=varying.find(name); if(spec==varying.end()) throw std::runtime_error("Unknown engine shader.");
    std::vector<std::filesystem::path> pending{config.sourceDirectory/(name+".sc"),config.sourceDirectory/spec->second};
    try {
        if (std::filesystem::is_regular_file(indexPath(config,name))) {
            std::ifstream input(indexPath(config,name)); const auto index=nlohmann::json::parse(input);
            if (index.at("version")==2 && index.at("contract")==config.buildContract && index.at("source")==resourcePathKey(config.sourceDirectory) &&
                index.at("include")==resourcePathKey(config.includeDirectory) && index.at("compiler")==resourcePathKey(config.compiler)) {
                const auto& dependencies=index.at("dependencies");
                if (!dependencies.is_array() || dependencies.size()>256) throw std::runtime_error("Invalid cached shader dependency graph.");
                for (const auto& value:dependencies) {
                    const auto file=pathFromUtf8(value.get<std::string>());
                    if (std::filesystem::is_regular_file(file)) pending.push_back(file);
                }
                pending.push_back(config.compiler); return sortedDependencies(std::move(pending));
            }
        }
    } catch (...) { /* Disposable metadata can be reconstructed by shaderc. */ }
    std::vector<std::filesystem::path> result; std::set<std::string> known;
    const std::regex include(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
    for(std::size_t i=0;i<pending.size();++i) {
        if(pending.size()>128) throw std::runtime_error("Shader include graph is too large.");
        const auto file=pending[i]; if(!known.insert(resourcePathKey(file)).second) continue;
        if(!std::filesystem::is_regular_file(file)) throw std::runtime_error("Shader source/include is missing.");
        result.push_back(file); std::ifstream input(file); std::string line;
        while(std::getline(input,line)) {
            std::smatch match; if(!std::regex_search(line,match,include)) continue;
            auto child=file.parent_path()/match[1].str();
            if(!std::filesystem::is_regular_file(child)) child=config.includeDirectory/match[1].str();
            if(std::filesystem::is_regular_file(child)) pending.push_back(std::move(child));
        }
    }
    result.push_back(config.compiler); return sortedDependencies(std::move(result));
}
ShaderBinary compileShader(const ShaderCompileConfig& config,const std::string& name,std::stop_token token) {
    if(!std::filesystem::is_regular_file(config.compiler)) throw std::runtime_error("Pinned shader compiler is unavailable; embedded shaders remain active.");
    auto dependencies=shaderDependencies(config,name); const auto initialFingerprint=fingerprintFiles(dependencies);
    auto fingerprint=compileFingerprint(config,name,dependencies);
    std::filesystem::create_directories(config.cacheDirectory);
    auto target=config.cacheDirectory/(name+"-"+std::to_string(fingerprint)+".bin");
    if(std::filesystem::is_regular_file(target)) return {name,name.starts_with("vs_"),readBinary(target),std::move(dependencies),fingerprint};
    static std::atomic_uint64_t sequence{};
    const auto stage=config.cacheDirectory/(name+"-"+std::to_string(sequence.fetch_add(1))+".tmp");
    try {
        const auto command=L"-f "+quoted(config.sourceDirectory/(name+".sc"))+L" -o "+quoted(stage)+L" --platform windows --type "+
            (name.starts_with("vs_") ? std::wstring(L"vertex") : std::wstring(L"fragment"))+L" --profile s_5_0 -O 3 --depends --varyingdef "+
            quoted(config.sourceDirectory/varying.at(name))+L" -i "+quoted(config.includeDirectory);
        runCompiler(config.compiler,command,token);
        if(token.stop_requested()) throw std::runtime_error("Shader compilation canceled.");
        if(fingerprintFiles(dependencies)!=initialFingerprint) throw std::runtime_error("Shader source changed during compilation.");
        const auto discover=[&] {
            auto actual=parseShaderDependencyFile(std::filesystem::path(stage.wstring()+L".d"));
            for (auto& file:actual) if (file.is_relative()) {
                if (std::filesystem::is_regular_file(config.sourceDirectory/file)) file=config.sourceDirectory/file;
                else if (std::filesystem::is_regular_file(config.includeDirectory/file)) file=config.includeDirectory/file;
            }
            actual.push_back(config.sourceDirectory/(name+".sc")); actual.push_back(config.sourceDirectory/varying.at(name)); actual.push_back(config.compiler);
            return sortedDependencies(std::move(actual));
        };
        auto actual=discover();
        const bool newlyDiscovered=std::any_of(actual.begin(),actual.end(),[&](const auto& file) {
            return std::none_of(dependencies.begin(),dependencies.end(),[&](const auto& known){return resourcePathKey(file)==resourcePathKey(known);});
        });
        const auto actualBefore=fingerprintFiles(actual);
        if (newlyDiscovered) {
            // Macro includes cannot be known lexically. Compile once more with
            // the discovered full graph so every dependency has a before/after
            // fingerprint and never cache a mixed source revision.
            runCompiler(config.compiler,command,token);
            const auto second=discover();
            if (second!=actual) throw std::runtime_error("Shader include graph changed during compilation.");
        }
        if (token.stop_requested() || fingerprintFiles(actual)!=actualBefore) throw std::runtime_error("Shader source changed during compilation.");
        dependencies=std::move(actual); fingerprint=compileFingerprint(config,name,dependencies);
        target=config.cacheDirectory/(name+"-"+std::to_string(fingerprint)+".bin");
        auto data=readBinary(stage); std::error_code error; std::filesystem::rename(stage,target,error);
        if(error && !std::filesystem::is_regular_file(target)) throw std::runtime_error("Could not publish compiled shader cache.");
        std::filesystem::remove(stage,error); std::filesystem::remove(std::filesystem::path(stage.wstring()+L".d"),error);
        saveDependencyIndex(config,name,dependencies);
        return {name,name.starts_with("vs_"),std::move(data),std::move(dependencies),fingerprint};
    } catch(...) {
        std::error_code ignored; std::filesystem::remove(stage,ignored); std::filesystem::remove(std::filesystem::path(stage.wstring()+L".d"),ignored); throw;
    }
}
}
