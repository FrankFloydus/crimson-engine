#include "App.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
AppOptions parse(std::initializer_list<const char*> arguments) {
    std::vector<std::string> strings{"crimson-renderer"};
    for(const auto* argument:arguments)strings.emplace_back(argument);
    std::vector<char*> argv;argv.reserve(strings.size());
    for(auto& argument:strings)argv.push_back(argument.data());
    return parseOptions(static_cast<int>(argv.size()),argv.data());
}
void run() {
    require(!parse({}).rendererBackend,"Missing renderer override must leave the persisted preference authoritative.");
    require(parse({"--renderer","dx11"}).rendererBackend==GraphicsBackend::Direct3D11,"Direct3D 11 override was not parsed.");
    require(parse({"--renderer","dx12"}).rendererBackend==GraphicsBackend::Direct3D12,"Direct3D 12 override was not parsed.");
    const auto combined=parse({"--hidden","--renderer","dx11","--frames","7","--size","1280x720"});
    require(combined.hidden&&combined.frameLimit==7&&combined.width==1280&&combined.height==720&&
        combined.rendererBackend==GraphicsBackend::Direct3D11,"Renderer override changed unrelated launch options.");
    const auto repeated=parse({"--renderer","dx11","--renderer","dx12"});
    require(repeated.rendererBackend==GraphicsBackend::Direct3D12,"Last explicit renderer override must win.");
    for(const auto* invalid:{"vulkan","DX12","d3d12","","12"}) {
        bool rejected=false;try {parse({"--renderer",invalid});}catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Malformed renderer override was accepted.");
    }
    bool missingRejected=false;try {parse({"--renderer"});}catch(const std::runtime_error&) {missingRejected=true;}
    require(missingRejected,"Renderer override without a value was accepted.");
    require(parse({"--profile","startup"}).profileDuration==15,"Startup capture default duration changed.");
    require(parse({"--profile","gameplay"}).profileDuration==10,"Gameplay capture default duration changed.");
    require(parse({"--profile","editor"}).profileMode=="editor" && parse({"--profile","editor"}).profileDuration==10,
        "Editor capture mode or default duration was not parsed.");
    require(parse({"--profile","editor","--profile-duration","25"}).profileDuration==25,"Editor capture ignored its duration.");
    const auto profile=parse({"--profile","startup","--profile-duration","37","--profile-output","capture directory"});
    require(profile.profileMode=="startup" && profile.profileDuration==37 && profile.profileOutput=="capture directory",
        "Tracy capture options were not retained.");
    for(const auto invalid: {std::initializer_list<const char*>{"--profile","cpu"},
                            {"--profile","startup","--profile-duration","0"},
                            {"--profile","startup","--profile-duration","3601"},
                            {"--profile-duration","2"}, {"--profile-output",""}, {"--profile"}}) {
        bool rejected=false;try {parse(invalid);}catch(const std::runtime_error&) {rejected=true;}
        require(rejected,"Invalid or incomplete Tracy capture options were accepted.");
    }
    require(!parseGraphicsBackend("vulkan")&&!validGraphicsBackend(static_cast<GraphicsBackend>(99)),"Unsupported backend was accepted.");
    for(const auto backend:{GraphicsBackend::Direct3D11,GraphicsBackend::Direct3D12})
        require(parseGraphicsBackend(graphicsBackendKey(backend))==backend,"Graphics backend key does not round-trip.");
}
}
int main() {
    try {run();std::cout<<"Application options checks passed.\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
