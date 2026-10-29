#include "resources/ShaderReloadSession.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-reload-native-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ShaderCompileConfig config{root/"compiler.fixture",root/"sources",root/"includes",root/"cache"};
    Fixture(){std::filesystem::create_directories(config.sourceDirectory);std::filesystem::create_directories(config.includeDirectory);write(config.compiler,"test compiler identity; never executed");
        for(const auto& name:engineShaderNames())write(config.sourceDirectory/(name+".sc"),"// shader fixture\n");
        for(const auto* file:{"varying.def.sc","model.varying.def.sc","model_paint.varying.def.sc","model_instanced.varying.def.sc","particle.varying.def.sc","smaa.varying.def.sc","decal.varying.def.sc","collision_overlay.varying.def.sc"})write(config.sourceDirectory/file,"// varying fixture\n");
    }
    ~Fixture(){std::error_code error;std::filesystem::remove_all(root,error);}
    static void write(const std::filesystem::path& file,const std::string& value){std::ofstream output(file,std::ios::binary);output<<value;}
};
template<class Predicate> void pump(ResourceManager& resources,ShaderReloadSession& reload,Predicate done){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    while(!done()){resources.pump();reload.pump();(void)resources.takePublications();if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Shader reload fixture exceeded its bounded CPU deadline.");std::this_thread::sleep_for(std::chrono::milliseconds{1});}
}
void atomicNativePublication(){
    Fixture fixture;ResourceManager resources(ResourceConfig{.workers=2});
    std::atomic_bool fail{},block{},started{};std::atomic_int compilations{};unsigned publications{};bool accept=true;
    ShaderReloadHooks hooks;
    hooks.compile=[&](const ShaderCompileConfig&,const std::string& name,std::stop_token token){
        ++compilations;started=true;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{2};
        while(block&&!token.stop_requested()){if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Blocked fixture expired.");std::this_thread::yield();}
        if(token.stop_requested())throw std::runtime_error("Canceled old shader family.");if(fail&&name=="fs_model")throw std::runtime_error("Rejected shader fixture.");
        ShaderBinary binary;binary.name=name;binary.vertex=name.starts_with("vs_");binary.bytes={7,8,9};binary.fingerprint=1;return binary;
    };
    hooks.publish=[&](const ShaderReloadPrograms& candidate,std::string& diagnostic){
        ++publications;require(candidate.graphics.size()==32,"Atomic reload omitted a named fixed graphics/compute program.");
        require(!candidate.procedural.empty()&&!candidate.cluster.empty()&&!candidate.deformation.empty()&&!candidate.ssgiTrace.empty()&&!candidate.ssgiTemporal.empty()&&!candidate.ssgiFilter.empty()&&!candidate.ssgiPublish.empty()&&!candidate.ssaoEvaluate.empty()&&!candidate.ssaoPublish.empty()&&!candidate.refractionDownsample.empty(),"Atomic reload omitted a dedicated compute stage.");
        for(const auto& program:candidate.graphics)require(!program.fragment.empty(),"An incomplete stage reached the canonical renderer publication boundary.");
        if(!accept)diagnostic="Native renderer retained its last accepted group.";return accept;
    };
    ShaderReloadSession reload(resources,fixture.config,hooks);
    pump(resources,reload,[&]{return !reload.pending()&&resources.idle();});
    require(reload.revision()==1&&publications==1,"Complete shader family did not publish exactly once.");
    const auto source=fixture.config.sourceDirectory/"fs_model.sc";
    fail=true;reload.changed({&source,1});pump(resources,reload,[&]{return !reload.pending()&&resources.idle();});
    require(reload.revision()==1&&publications==1&&reload.status().find("Rejected shader fixture")!=std::string::npos,"A failed source replaced the last accepted native shader family.");
    fail=false;accept=false;reload.changed({&source,1});pump(resources,reload,[&]{return !reload.pending()&&resources.idle();});
    require(reload.revision()==1&&publications==2&&reload.status().find("last accepted")!=std::string::npos,"A rejected GPU publication advanced the accepted source epoch.");
    accept=true;block=true;started=false;reload.changed({&source,1});
    pump(resources,reload,[&]{return started.load();});
    reload.changed({&source,1});block=false;
    pump(resources,reload,[&]{return !reload.pending()&&resources.idle();});
    require(reload.revision()==2&&publications==3,"Superseding a pending reload published stale or partial work.");
    const auto names=engineShaderNames();require(std::find(names.begin(),names.end(),"vs_imgui")==names.end()&&std::find(names.begin(),names.end(),"fs_imgui")==names.end(),"The native Qt application still demanded retired UI shader stages.");
    const auto requests=compilations.load();std::this_thread::sleep_for(std::chrono::milliseconds{2});resources.pump();reload.pump();require(compilations==requests&&publications==3,"A stable frame repeated native shader compilation/publication.");
}
void unavailableCompilerRetainsEmbedded(){Fixture fixture;std::filesystem::remove(fixture.config.compiler);ResourceManager resources;bool published=false;ShaderReloadHooks hooks;hooks.publish=[&](const auto&,auto&){published=true;return true;};ShaderReloadSession reload(resources,fixture.config,std::move(hooks));require(!reload.available()&&!reload.pending()&&!published&&resources.stats().queued==0,"Unavailable compiler did not retain embedded native programs without work.");}
}
int main(){try{atomicNativePublication();unavailableCompilerRetainsEmbedded();std::cout<<"Native renderer-only coherent shader reload, failure retention, cancellation and stable frames passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
