#include "ShaderCompiler.h"
#include "ResourceManager.h"
#include "Paths.h"
#include "CacheIO.h"
#include "assets/AtomicFile.h"
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace crimson {
namespace {
struct CompilerCounters {
    std::atomic_uint64_t compilerInvocations{},compilerNanoseconds{},dependencyRequests{},dependencyNanoseconds{};
    std::atomic_uint64_t cacheOnlyRequests{},cacheOnlyHits{},cacheOnlyNanoseconds{},workerRequests{},workerCacheHits{};
} metrics;
struct MetricTimer {
    std::atomic_uint64_t& total;
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    ~MetricTimer(){total.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count(),std::memory_order_relaxed);}
};
const std::map<std::string,std::string> varying{
    {"cs_procedural_filter","varying.def.sc"},
    {"cs_cluster_lights","varying.def.sc"},
    {"cs_model_deform","varying.def.sc"},
    {"cs_ambient_visibility","varying.def.sc"},
    {"cs_ambient_publish","varying.def.sc"},
    {"vs_particle","particle.varying.def.sc"},{"fs_particle","particle.varying.def.sc"},
    {"vs_pbr","varying.def.sc"},{"fs_pbr","varying.def.sc"},{"vs_model","model.varying.def.sc"},{"fs_model","model.varying.def.sc"},
    {"vs_pick","model.varying.def.sc"},{"fs_pick","model.varying.def.sc"},{"vs_present","varying.def.sc"},{"fs_present","varying.def.sc"},
    {"vs_point_shadow","model.varying.def.sc"},{"fs_point_shadow","model.varying.def.sc"},
    {"vs_directional_shadow","model.varying.def.sc"},{"fs_directional_shadow","model.varying.def.sc"},
    {"fs_glow_extract","varying.def.sc"},{"fs_glow_blur","varying.def.sc"},
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
uint64_t compileFingerprint(const ShaderCompileConfig& config,const std::string& name,uint64_t result) {
    const auto contract=config.buildContract+";"+name+";"+varying.at(name)+";"+(name.starts_with("cs_") ? "compute" : name.starts_with("vs_") ? "vertex" : "fragment");
    for (const unsigned char byte:contract) { result^=byte; result*=1099511628211ull; }
    return result;
}
uint64_t compileFingerprint(const ShaderCompileConfig& config,const std::string& name,const std::vector<std::filesystem::path>& dependencies) {
    return compileFingerprint(config,name,fingerprintFiles(dependencies));
}
void saveDependencyIndex(const ShaderCompileConfig& config,const std::string& name,const std::vector<std::filesystem::path>& dependencies) {
    nlohmann::json paths=nlohmann::json::array();
    for (const auto& file:dependencies) paths.push_back(utf8(file));
    const nlohmann::json index{{"version",2},{"contract",config.buildContract},{"source",resourcePathKey(config.sourceDirectory)},
        {"include",resourcePathKey(config.includeDirectory)},{"compiler",resourcePathKey(config.compiler)},{"dependencies",std::move(paths)}};
    streaming::detail::writeCacheFileAtomically(indexPath(config,name),index.dump(2)+"\n");
}
std::vector<std::uint8_t> readBinary(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) throw std::runtime_error("Shader file is unavailable.");
    const auto length=input.tellg(); if(length<=0 || length>64*1024*1024) throw std::runtime_error("Shader file has an invalid size.");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length)); input.seekg(0); input.read(reinterpret_cast<char*>(data.data()),length);
    if(!input) throw std::runtime_error("Shader file could not be read."); return data;
}
std::optional<std::vector<std::uint8_t>> cachedBinary(const std::filesystem::path& path,char stage) {
    try {
        auto bytes=readBinary(path);
        if(bytes.size()>16&&bytes[0]==stage&&bytes[1]=='S'&&bytes[2]=='H'&&bytes[3]==11)return bytes;
    } catch(const std::exception&) { /* Missing/damaged derived data is a miss. */ }
    return {};
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
    MetricTimer timing{metrics.compilerNanoseconds};
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
    metrics.compilerInvocations.fetch_add(1,std::memory_order_relaxed);
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
ShaderCompilerMetrics shaderCompilerMetrics() {
    const auto get=[](const auto& value){return value.load(std::memory_order_relaxed);};
    return {get(metrics.compilerInvocations),get(metrics.compilerNanoseconds),get(metrics.dependencyRequests),get(metrics.dependencyNanoseconds),
        get(metrics.cacheOnlyRequests),get(metrics.cacheOnlyHits),get(metrics.cacheOnlyNanoseconds),get(metrics.workerRequests),get(metrics.workerCacheHits)};
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
    metrics.dependencyRequests.fetch_add(1,std::memory_order_relaxed);MetricTimer timing{metrics.dependencyNanoseconds};
    const auto spec=varying.find(name); if(spec==varying.end()) throw std::runtime_error("Unknown engine shader.");
    std::vector<std::filesystem::path> pending{config.sourceDirectory/(name+".sc"),config.sourceDirectory/spec->second};
    try {
        if (std::filesystem::is_regular_file(indexPath(config,name))) {
            std::ifstream input(indexPath(config,name)); const auto index=nlohmann::json::parse(input);input.close();
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
    metrics.workerRequests.fetch_add(1,std::memory_order_relaxed);
    if(!std::filesystem::is_regular_file(config.compiler)) throw std::runtime_error("Pinned shader compiler is unavailable; embedded shaders remain active.");
    auto dependencies=shaderDependencies(config,name);const auto dependencyState=fingerprintFileStates(dependencies);
    const auto initialFingerprint=fingerprintFiles(dependencies);
    auto fingerprint=compileFingerprint(config,name,initialFingerprint);
    std::filesystem::create_directories(config.cacheDirectory);
    auto target=config.cacheDirectory/(name+"-"+std::to_string(fingerprint)+".bin");
    if(auto bytes=cachedBinary(target,name.starts_with("cs_")?'C':name.starts_with("vs_")?'V':'F')) {
        if(token.stop_requested()||fingerprintFileStates(dependencies)!=dependencyState)
            throw std::runtime_error("Shader source changed while reading its cache.");
        metrics.workerCacheHits.fetch_add(1,std::memory_order_relaxed);
        return {name,name.starts_with("vs_"),std::move(*bytes),std::move(dependencies),fingerprint};
    }
    {std::error_code ignored;std::filesystem::remove(target,ignored);}
    static std::atomic_uint64_t sequence{};
    const auto stage=config.cacheDirectory/(name+"-"+std::to_string(sequence.fetch_add(1))+".tmp");
    try {
        const auto command=L"-f "+quoted(config.sourceDirectory/(name+".sc"))+L" -o "+quoted(stage)+L" --platform windows --type "+
            (name.starts_with("cs_") ? std::wstring(L"compute") : name.starts_with("vs_") ? std::wstring(L"vertex") : std::wstring(L"fragment"))+L" --profile s_5_0 -O 3 --depends --varyingdef "+
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

namespace {
std::string recipeIdentity(const ShaderCompileRecipe& recipe) {
    if(recipe.name.empty() || recipe.name.size()>128 || recipe.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
        throw std::runtime_error("Generated shader recipe has an invalid name.");
    if(recipe.source.empty() || recipe.source.size()>2*1024*1024) throw std::runtime_error("Generated shader source is empty or exceeds its limit.");
    std::uint64_t hash=14695981039346656037ull;
    const auto add=[&](const std::string& value){for(unsigned char ch:value){hash^=ch;hash*=1099511628211ull;}hash^=0xff;hash*=1099511628211ull;};
    add(recipe.name);add(recipe.source);add(recipe.contract);add(std::to_string(static_cast<unsigned>(recipe.stage)));add(resourcePathKey(recipe.varyingDefinition));
    for(const auto& directory:recipe.includeDirectories)add(resourcePathKey(directory));
    // Preserve existing optimized cache identities. An interactive binary is a
    // different compiler result, never a substitute for the optimized artifact.
    switch(recipe.optimization) {
    case ShaderCompileOptimization::Full:break;
    case ShaderCompileOptimization::Interactive:add("fxc-skip-optimization-v1");break;
    default:throw std::runtime_error("Generated shader optimization policy is invalid.");
    }
    return recipe.name+"-"+std::to_string(hash);
}
std::filesystem::path recipeInclude(const ShaderCompileConfig& config,const ShaderCompileRecipe& recipe,
    const std::filesystem::path& parent,const std::string& name) {
    if(!parent.empty() && std::filesystem::is_regular_file(parent/name))return parent/name;
    for(const auto& directory:recipe.includeDirectories)if(std::filesystem::is_regular_file(directory/name))return directory/name;
    if(std::filesystem::is_regular_file(config.sourceDirectory/name))return config.sourceDirectory/name;
    if(std::filesystem::is_regular_file(config.includeDirectory/name))return config.includeDirectory/name;
    return {};
}
std::uint64_t recipeFingerprint(const ShaderCompileConfig& config,const std::string& identity,std::uint64_t hash) {
    for(unsigned char ch:config.buildContract+";recipe-v1;"+identity+";"+resourcePathKey(config.sourceDirectory)+";"+resourcePathKey(config.includeDirectory))
        {hash^=ch;hash*=1099511628211ull;}
    return hash;
}
std::uint64_t recipeFingerprint(const ShaderCompileConfig& config,const std::string& identity,const std::vector<std::filesystem::path>& dependencies) {
    return recipeFingerprint(config,identity,fingerprintFiles(dependencies));
}
}
std::vector<std::filesystem::path> shaderDependencies(const ShaderCompileConfig& config,const ShaderCompileRecipe& recipe) {
    metrics.dependencyRequests.fetch_add(1,std::memory_order_relaxed);MetricTimer timing{metrics.dependencyNanoseconds};
    const auto identity=recipeIdentity(recipe);
    std::vector<std::filesystem::path> pending{recipe.varyingDefinition};
    std::vector<std::filesystem::path> result;
    std::set<std::string> known;
    const std::regex include(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
    const auto scan=[&](std::istream& input,const std::filesystem::path& parent) {
        std::string line;
        while(std::getline(input,line)) {
            std::smatch match;if(!std::regex_search(line,match,include))continue;
            const auto child=recipeInclude(config,recipe,parent,match[1].str());
            if(child.empty())throw std::runtime_error("Generated shader include is missing: "+match[1].str());
            pending.push_back(child);
        }
    };
    try {
        std::ifstream input(config.cacheDirectory/(identity+".recipe.json"));
        if(input) {
            const auto metadata=nlohmann::json::parse(input);input.close();
            if(metadata.at("version")==1 && metadata.at("contract")==config.buildContract && metadata.at("compiler")==resourcePathKey(config.compiler) &&
                metadata.at("source")==resourcePathKey(config.sourceDirectory) && metadata.at("include")==resourcePathKey(config.includeDirectory)) {
                const auto& dependencies=metadata.at("dependencies");
                if(!dependencies.is_array() || dependencies.size()>256)throw std::runtime_error("Generated shader dependency index is invalid.");
                for(const auto& value:dependencies) {
                    const auto file=pathFromUtf8(value.get<std::string>());
                    if(std::filesystem::is_regular_file(file))pending.push_back(file);
                }
                pending.push_back(config.compiler);return sortedDependencies(std::move(pending));
            }
        }
    } catch(...) { /* A disposable dependency index can be reconstructed. */ }
    std::istringstream source(recipe.source);scan(source,{});
    for(std::size_t i=0;i<pending.size();++i) {
        if(pending.size()>256)throw std::runtime_error("Generated shader include graph exceeds its limit.");
        const auto file=pending[i];if(!known.insert(resourcePathKey(file)).second)continue;
        if(!std::filesystem::is_regular_file(file))throw std::runtime_error("Generated shader varying/include is unavailable.");
        result.push_back(file);std::ifstream input(file);scan(input,file.parent_path());
    }
    result.push_back(config.compiler);return sortedDependencies(std::move(result));
}
ShaderBinary compileShader(const ShaderCompileConfig& config,const ShaderCompileRecipe& recipe,std::stop_token token) {
    metrics.workerRequests.fetch_add(1,std::memory_order_relaxed);
    if(token.stop_requested())throw std::runtime_error("Shader compilation canceled.");
    if(!std::filesystem::is_regular_file(config.compiler))throw std::runtime_error("Pinned shader compiler is unavailable.");
    const auto identity=recipeIdentity(recipe);
    auto dependencies=shaderDependencies(config,recipe);
    const auto dependencyState=fingerprintFileStates(dependencies);
    auto fingerprint=recipeFingerprint(config,identity,dependencies);
    std::filesystem::create_directories(config.cacheDirectory);
    auto target=config.cacheDirectory/(identity+"-"+std::to_string(fingerprint)+".bin");
    if(auto bytes=cachedBinary(target,recipe.stage==ShaderStage::Compute?'C':recipe.stage==ShaderStage::Vertex?'V':'F')) {
        if(token.stop_requested()||fingerprintFileStates(dependencies)!=dependencyState)
            throw std::runtime_error("Generated shader dependencies changed while reading their cache.");
        metrics.workerCacheHits.fetch_add(1,std::memory_order_relaxed);
        return {recipe.name,recipe.stage==ShaderStage::Vertex,std::move(*bytes),std::move(dependencies),fingerprint};
    }
    {std::error_code ignored;std::filesystem::remove(target,ignored);}
    static std::atomic_uint64_t sequence{};
    const auto stage=config.cacheDirectory/(identity+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(sequence.fetch_add(1))+".tmp");
    const auto source=std::filesystem::path(stage.wstring()+L".sc");
    const auto dependencyFile=std::filesystem::path(stage.wstring()+L".d");
    const auto clean=[&]{std::error_code ignored;std::filesystem::remove(stage,ignored);std::filesystem::remove(source,ignored);std::filesystem::remove(dependencyFile,ignored);};
    try {
        writeFileAtomically(source,recipe.source);
        const auto type=recipe.stage==ShaderStage::Vertex?L"vertex":recipe.stage==ShaderStage::Compute?L"compute":L"fragment";
        std::wstring includeArguments;
        const auto addDirectory=[&](const std::filesystem::path& directory){if(!directory.empty())includeArguments+=L" -i "+quoted(directory);};
        for(const auto& directory:recipe.includeDirectories)addDirectory(directory);
        addDirectory(config.sourceDirectory);addDirectory(config.includeDirectory);
        // shaderc -O 0 still invokes the FXC optimizer. Omitting -O explicitly
        // selects D3DCOMPILE_SKIP_OPTIMIZATION in the pinned compiler.
        const auto optimization=recipe.optimization==ShaderCompileOptimization::Interactive?L"":L" -O 3";
        const auto command=L"-f "+quoted(source)+L" -o "+quoted(stage)+L" --platform windows --type "+type+
            L" --profile s_5_0"+optimization+L" --depends --varyingdef "+quoted(recipe.varyingDefinition)+includeArguments;
        const auto initial=fingerprintFiles(dependencies);
        runCompiler(config.compiler,command,token);
        if(token.stop_requested() || fingerprintFiles(dependencies)!=initial)throw std::runtime_error("Generated shader dependencies changed during compilation.");
        const auto discover=[&] {
            auto actual=parseShaderDependencyFile(dependencyFile);
            std::erase_if(actual,[&](const auto& file){return resourcePathKey(file)==resourcePathKey(source);});
            for(auto& file:actual)if(file.is_relative()) {
                const auto resolved=recipeInclude(config,recipe,{},utf8(file));if(!resolved.empty())file=resolved;
            }
            actual.push_back(recipe.varyingDefinition);actual.push_back(config.compiler);
            return sortedDependencies(std::move(actual));
        };
        auto actual=discover();const auto before=fingerprintFiles(actual);
        const bool newlyDiscovered=std::any_of(actual.begin(),actual.end(),[&](const auto& file) {
            return std::none_of(dependencies.begin(),dependencies.end(),[&](const auto& known){return resourcePathKey(file)==resourcePathKey(known);});
        });
        if(newlyDiscovered) {
            // Inactive conditional includes can make shaderc's graph a strict
            // subset of the lexical scan. Every actual input was already
            // checked; only newly discovered inputs require another compile.
            runCompiler(config.compiler,command,token);
            if(discover()!=actual)throw std::runtime_error("Generated shader include graph changed during compilation.");
        }
        if(token.stop_requested() || fingerprintFiles(actual)!=before)throw std::runtime_error("Generated shader dependencies changed during compilation.");
        dependencies=std::move(actual);fingerprint=recipeFingerprint(config,identity,dependencies);
        target=config.cacheDirectory/(identity+"-"+std::to_string(fingerprint)+".bin");
        auto data=readBinary(stage);std::error_code error;std::filesystem::rename(stage,target,error);
        if(error && !std::filesystem::is_regular_file(target))throw std::runtime_error("Could not publish generated shader cache.");
        nlohmann::json paths=nlohmann::json::array();for(const auto& file:dependencies)paths.push_back(utf8(file));
        streaming::detail::writeCacheFileAtomically(config.cacheDirectory/(identity+".recipe.json"),nlohmann::json{{"version",1},{"contract",config.buildContract},
            {"compiler",resourcePathKey(config.compiler)},{"source",resourcePathKey(config.sourceDirectory)},
            {"include",resourcePathKey(config.includeDirectory)},{"dependencies",paths}}.dump());
        clean();return {recipe.name,recipe.stage==ShaderStage::Vertex,std::move(data),std::move(dependencies),fingerprint};
    } catch(...) {clean();throw;}
}

ShaderCacheReadSession::ShaderCacheReadSession(ShaderCompileConfig config):config_(std::move(config)) {}
std::uint64_t ShaderCacheReadSession::fingerprint(const std::vector<std::filesystem::path>& dependencies) {
    std::uint64_t hash=14695981039346656037ull;
    std::string prefix;
    std::array<char,64*1024> buffer{};
    const auto append=[&](const char* bytes,std::size_t count) {
        for(std::size_t i=0;i<count;++i){hash^=static_cast<unsigned char>(bytes[i]);hash*=1099511628211ull;}
    };
    for(const auto& file:dependencies) {
        const auto name=resourcePathKey(file);
        // Stamps only key this transient memo. The persistent fingerprint still
        // includes every canonical path and content byte, exactly as workers do.
        const auto before=fingerprintFileStates({file});
        const auto [observed,inserted]=observed_.try_emplace(name,file,before);
        if(!inserted&&observed->second.second!=before)coherent_=false;
        prefix+=name;prefix.push_back('\0');prefix+=std::to_string(before);prefix.push_back('\0');
        if(const auto found=prefixes_.find(prefix);found!=prefixes_.end()){hash=found->second;continue;}
        append(name.data(),name.size());
        std::ifstream input(file,std::ios::binary);
        if(!input)throw std::runtime_error("Shader cache dependency is unavailable.");
        while(input){input.read(buffer.data(),buffer.size());append(buffer.data(),static_cast<std::size_t>(input.gcount()));}
        if(!input.eof()||fingerprintFileStates({file})!=before)throw std::runtime_error("Shader cache dependency changed while reading.");
        prefixes_.emplace(prefix,hash);
    }
    return hash;
}
std::optional<ShaderBinary> ShaderCacheReadSession::find(const std::string& name) {
    metrics.cacheOnlyRequests.fetch_add(1,std::memory_order_relaxed);MetricTimer timing{metrics.cacheOnlyNanoseconds};
    try {
        auto dependencies=shaderDependencies(config_,name);
        const auto before=fingerprintFileStates(dependencies);
        const auto key=compileFingerprint(config_,name,fingerprint(dependencies));
        const auto target=config_.cacheDirectory/(name+"-"+std::to_string(key)+".bin");
        if(!std::filesystem::is_regular_file(target))return {};
        auto bytes=cachedBinary(target,name.starts_with("cs_")?'C':name.starts_with("vs_")?'V':'F');if(!bytes)return {};
        if(fingerprintFileStates(dependencies)!=before)return {};
        artifacts_[name+"-"+std::to_string(key)]=target;
        metrics.cacheOnlyHits.fetch_add(1,std::memory_order_relaxed);
        return ShaderBinary{name,name.starts_with("vs_"),std::move(*bytes),std::move(dependencies),key};
    } catch(const std::exception&) {return {};}
}
std::optional<ShaderBinary> ShaderCacheReadSession::find(const ShaderCompileRecipe& recipe) {
    metrics.cacheOnlyRequests.fetch_add(1,std::memory_order_relaxed);MetricTimer timing{metrics.cacheOnlyNanoseconds};
    try {
        const auto identity=recipeIdentity(recipe);
        auto dependencies=shaderDependencies(config_,recipe);
        const auto before=fingerprintFileStates(dependencies);
        const auto key=recipeFingerprint(config_,identity,fingerprint(dependencies));
        const auto target=config_.cacheDirectory/(identity+"-"+std::to_string(key)+".bin");
        if(!std::filesystem::is_regular_file(target))return {};
        auto bytes=cachedBinary(target,recipe.stage==ShaderStage::Compute?'C':recipe.stage==ShaderStage::Vertex?'V':'F');if(!bytes)return {};
        if(fingerprintFileStates(dependencies)!=before)return {};
        artifacts_[recipe.name+"-"+std::to_string(key)]=target;
        metrics.cacheOnlyHits.fetch_add(1,std::memory_order_relaxed);
        return ShaderBinary{recipe.name,recipe.stage==ShaderStage::Vertex,std::move(*bytes),std::move(dependencies),key};
    } catch(const std::exception&) {return {};}
}
bool ShaderCacheReadSession::unchanged() const {
    if(!coherent_)return false;
    try {
        for(const auto& [key,observed]:observed_) {
            (void)key;
            if(fingerprintFileStates({observed.first})!=observed.second)return false;
        }
        return true;
    } catch(const std::exception&) {return false;}
}
void ShaderCacheReadSession::discard(std::span<const ShaderBinary> binaries) {
    for(const auto& binary:binaries) {
        const auto found=artifacts_.find(binary.name+"-"+std::to_string(binary.fingerprint));
        if(found==artifacts_.end())continue;
        std::error_code ignored;std::filesystem::remove(found->second,ignored);artifacts_.erase(found);
    }
}
}
