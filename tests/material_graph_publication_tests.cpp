#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Renderer.h"
#include "resources/ResourceManager.h"
#include <bgfx/platform.h>
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
using namespace std::chrono_literals;
namespace fs=std::filesystem;

void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }

struct TemporaryProject {
    fs::path root=fs::temp_directory_path()/("crimson-graph-publication-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryProject() {
        std::error_code error;
        const auto parent=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-graph-publication-"))fs::remove_all(target,error);
    }
};

std::string read(const fs::path& file) {
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Could not read shader fixture: "+file.string());
    return {std::istreambuf_iterator<char>(input),{}};
}

template<class Predicate> void waitFor(Predicate ready,const char* diagnostic) {
    const auto deadline=std::chrono::steady_clock::now()+90s;
    while(!ready()) {
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error(diagnostic);
        std::this_thread::sleep_for(2ms);
    }
}

MaterialAsset fixture() {
    MaterialAsset material;material.id="publication-fixture";material.name="Publication fixture";
    const auto output=addMaterialNode(material.graph,MaterialNodeKind::Output).id;
    findMaterialNode(material.graph,output)->output.blend=MaterialBlend::Mask;
    const auto scalar=addMaterialNode(material.graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(material.graph,scalar)->scalar=.7f;
    const auto texture=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,texture)->texture.image.id="assets/textures/publication.png";
    connectMaterialNodes(material.graph,scalar,MaterialPin::Value,output,MaterialPin::Roughness);
    connectMaterialNodes(material.graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(material.graph,texture,MaterialPin::A,output,MaterialPin::OpacityMask);
    return material;
}

MaterialGraphProgramResult waitForResult(ResourceManager& resources,MaterialGraphPrograms& programs,const MaterialGraphShader& shader) {
    MaterialGraphProgramResult result;
    waitFor([&] {
        resources.pump();result=programs.request(shader);bgfx::frame();
        return result.current || result.diagnostic!="Compiling material graph...";
    },"Timed out waiting for graph shader publication.");
    return result;
}

void requireComplete(const MaterialGraphProgramResult& result) {
    if(!result.current)throw std::runtime_error("Expected complete graph shader publication: "+result.diagnostic);
    check(result.programs&&result.programs->bytes>0,"Published graph has no program allocation accounting.");
    check(bgfx::isValid(result.programs->parameters),"Published graph has no parameter uniform.");
    for(const auto handle:result.programs->programs)check(bgfx::isValid(handle),"A partial graph program bundle was published.");
    for(const auto handle:result.programs->samplers)check(bgfx::isValid(handle),"Published graph has an incomplete sampler set.");
}

// A single worker and this cancelable gate make publication order deterministic.
// No shader compiler or GPU call runs on this test's helper job.
struct WorkerGate {
    std::atomic_bool entered{},released{};
    ~WorkerGate(){released=true;}
};

void publicationChecks(const fs::path& compiler,const fs::path& sources,const fs::path& include) {
    TemporaryProject temporary;
    const auto copiedSources=temporary.root/"shaders";
    fs::create_directories(temporary.root);
    fs::copy(sources,copiedSources,fs::copy_options::recursive);
    const ShaderCompileConfig config{compiler,copiedSources,include,temporary.root/"cache"};
    const auto editedSource=copiedSources/"material_graph_model.sh";
    const auto originalSource=read(editedSource);
    const auto commonSource=copiedSources/"material_graph_common.sh";
    const auto originalCommon=read(commonSource);
    const std::array changedFiles{editedSource};
    auto material=fixture();const auto shader=generateMaterialGraphShader(material);
    {
        ModelMaterialTextures fallback;
        check(usesParticleMaterialPreview(MaterialPreviewMesh::Sprite,fallback),"Explicit Sprite preview did not select billboard geometry.");
        auto packet=std::make_shared<PreparedGraphMaterial>();fallback.graph=packet;
        check(usesParticleMaterialPreview(MaterialPreviewMesh::Sprite,fallback),"Full graph Sprite preview lost its particle program path.");
        packet->nodePreview=true;
        check(!usesParticleMaterialPreview(MaterialPreviewMesh::Sprite,fallback),"Node preview incorrectly selected its unavailable Sprite program.");
        check(!usesParticleMaterialPreview(MaterialPreviewMesh::Sphere,{}),"Sphere preview unexpectedly selected billboard geometry.");
    }
    PreparedGraphMaterial retainedPacket;
    std::weak_ptr<const MaterialGraphProgramsVersion> retiredVersion,retainedVersion;
    {
        ResourceConfig resourceConfig;resourceConfig.workers=1;
        ResourceManager resources{resourceConfig};
        {
            MaterialGraphPrograms programs{resources,config};
            auto pending=programs.request(shader);
            check(!pending.current&&!pending.programs&&pending.diagnostic=="Compiling material graph...","First graph request did not remain pending until worker publication.");

            // Finish old CPU jobs but deliberately do not pump their completions.
            waitFor([&]{const auto stats=resources.stats();return stats.queued==0&&stats.running==0;},"Initial shader compilation did not finish.");
            auto gate=std::make_shared<WorkerGate>();
            ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;
            auto blocker=resources.request<ShaderBinary>(AssetRef<ShaderBinary>{"publication-worker-gate"},[gate](std::stop_token stop) {
                gate->entered=true;
                while(!gate->released&&!stop.stop_requested())std::this_thread::sleep_for(1ms);
                auto binary=std::make_shared<ShaderBinary>();
                return ResourceLoadResult<ShaderBinary>{binary,0,1,{}};
            },options);
            waitFor([&]{return gate->entered.load();},"Could not establish shader worker gate.");
            programs.changed(changedFiles);
            pending=programs.request(shader);
            resources.pump();
            pending=programs.request(shader);
            check(!pending.current&&!pending.programs,"A completed job from the previous source epoch published while its replacement was pending.");
            check(programs.residentBytes()==0,"Rejected stale jobs allocated a GPU program bundle.");
            gate->released=true;
            auto first=waitForResult(resources,programs,shader);requireComplete(first);
            check(programs.residentBytes()==first.programs->bytes,"Initial graph program bytes were not counted once.");
            {
                auto previewOwner=programs.createSibling();
                const auto texture=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Texture2D;});
                const auto previewShader=generateMaterialGraphPreviewShader(material,{texture->id,MaterialPin::RGB});
                auto preview=waitForResult(resources,*previewOwner,previewShader);
                check(preview.current && preview.programs && bgfx::isValid(preview.programs->programs[0]),"Independent node preview did not publish a Model program.");
                check(!bgfx::isValid(preview.programs->programs[1]),"Node preview allocated ordinary material passes.");
                const auto revision=previewOwner->revision();programs.changed(changedFiles);
                check(previewOwner->revision()>revision,"Node preview did not share shader-source invalidation.");
                auto previewPending=previewOwner->request(previewShader);
                check(!previewPending.current && previewPending.programs==preview.programs,"Node preview discarded its previous complete bundle during source invalidation.");
                waitForResult(resources,*previewOwner,previewShader);
                first=waitForResult(resources,programs,shader);requireComplete(first);
            }
            std::array<std::vector<std::uint8_t>,4> originalFragments;
            for(const auto& variant:shader.variants)
                originalFragments[static_cast<std::size_t>(variant.pass)]=compileShader(config,materialGraphShaderRecipe(config,shader,variant)).bytes;
            PreparedGraphMaterial retiredPacket;retiredPacket.programs=first.programs;
            retiredVersion=first.programs;
            for(auto pass:{MaterialGraphPass::Model,MaterialGraphPass::Picking,MaterialGraphPass::PointShadow,MaterialGraphPass::Particle})
                check(bgfx::isValid(retiredPacket.program(pass)),"Packet did not pin all four program variants.");

            for(auto& node:material.graph.nodes)if(node.kind==MaterialNodeKind::Scalar)node.scalar=.2f;
            const auto parameterShader=generateMaterialGraphShader(material);
            check(parameterShader.topologyKey==shader.topologyKey&&parameterShader.uniforms!=shader.uniforms,"Parameter fixture did not preserve shader topology.");
            const auto loadsBefore=resources.stats().loads;
            auto parameterOnly=programs.request(parameterShader);requireComplete(parameterOnly);
            check(parameterOnly.programs==first.programs&&resources.stats().loads==loadsBefore,"A parameter edit compiled or replaced the shared program bundle.");

            writeFileAtomically(editedSource,"#error Deliberate_material_graph_publication_failure\n"+originalSource);
            programs.changed(changedFiles);
            auto failingPending=programs.request(parameterShader);
            check(!failingPending.current&&failingPending.programs==first.programs,"Pending hot reload did not retain the complete last-good bundle.");
            auto failed=waitForResult(resources,programs,parameterShader);
            check(!failed.current&&!failed.diagnostic.empty()&&failed.programs==first.programs,"Failed shader compilation replaced or discarded the last-good bundle.");
            check(programs.residentBytes()==first.programs->bytes,"Failed compilation leaked a partially published program bundle.");
            // Complete every sibling job before changing its include files again.
            waitFor([&]{resources.pump();return resources.idle();},"Failed shader batch did not finish.");

            // bgfx shares shaders by binary hash and programs by shader pair.
            // A comment-only recovery can correctly reuse all four handles.
            // Change a runtime branch used by every pass so this fixture can
            // meaningfully detect mixing old and new program generations.
            auto recoveredCommon=originalCommon;
            const std::string fallback="if (settings.y < 0.5) return vec4_splat(1.0);";
            const auto fallbackPosition=recoveredCommon.find(fallback);
            check(fallbackPosition!=std::string::npos,"Graph common helper no longer contains the recovery fixture branch.");
            recoveredCommon.replace(fallbackPosition,fallback.size(),"if (settings.y < 0.5) return vec4_splat(0.75);");
            writeFileAtomically(editedSource,originalSource);
            writeFileAtomically(commonSource,recoveredCommon);
            const std::array recoveryFiles{editedSource,commonSource};
            programs.changed(recoveryFiles);
            auto recoveringPending=programs.request(parameterShader);
            check(!recoveringPending.current&&recoveringPending.programs==first.programs,"Recovery stopped retaining the previous complete shader generation.");
            auto recovered=waitForResult(resources,programs,parameterShader);requireComplete(recovered);
            check(recovered.programs!=first.programs&&recovered.programs->signature!=first.programs->signature,"Valid source recovery did not publish a new program generation.");
            for(const auto& variant:parameterShader.variants) {
                const auto binary=compileShader(config,materialGraphShaderRecipe(config,parameterShader,variant));
                check(binary.bytes!=originalFragments[static_cast<std::size_t>(variant.pass)],"Recovery fixture did not change a pass's compiled instructions.");
            }
            for(std::size_t pass=0;pass<4;++pass)
                check(recovered.programs->programs[pass].idx!=first.programs->programs[pass].idx,"Recovery mixed a previous program into the new four-pass bundle.");
            check(programs.residentBytes()==first.programs->bytes+recovered.programs->bytes,"Pinned retired program bytes disappeared from residency accounting.");

            first.programs.reset();parameterOnly.programs.reset();failingPending.programs.reset();failed.programs.reset();recoveringPending.programs.reset();
            check(!retiredVersion.expired(),"A packet lost its retired generation while still holding a lease.");
            retiredPacket.programs.reset();
            check(retiredVersion.expired(),"Retired programs remained owned after the last packet released them.");
            programs.trim();
            check(programs.residentBytes()==recovered.programs->bytes,"Released retired GPU bytes remained counted.");
            retainedPacket.programs=recovered.programs;retainedVersion=recovered.programs;
        }
        check(!retainedVersion.expired(),"Destroying the program owner invalidated an outstanding packet lease.");
        resources.stop();
    }
    for(const auto handle:retainedPacket.programs->programs)check(bgfx::isValid(handle),"A complete generation did not survive its owner through the packet lease.");
    retainedPacket.programs.reset();
    check(retainedVersion.expired(),"Program generation was not destroyed before graphics shutdown.");
    for(int frame=0;frame<3;++frame)bgfx::frame();
}
}

int main(int argc,char** argv) {
    bool initialized=false;
    try {
        check(argc==4,"Expected shaderc, shader source directory and bgfx shader include arguments.");
        bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
        check(bgfx::init(init),"Could not initialize headless Noop graphics checks.");initialized=true;
        publicationChecks(argv[1],argv[2],argv[3]);
        bgfx::shutdown();initialized=false;
        std::cout<<"Headless material shader publication, epoch, recovery and lease checks passed.\n";return 0;
    } catch(const std::exception& error) {
        if(initialized)bgfx::shutdown();
        std::cerr<<error.what()<<'\n';return 1;
    }
}
