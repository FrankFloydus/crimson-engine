#include "editor/AssetPreviews.h"
#include "editor/AssetPreviewCache.h"
#include "editor/EditorResources.h"
#include "editor/EditorSession.h"
#include "editor/ResourceRuntime.h"
#include "assets/AssetLibrary.h"
#include "assets/BuiltinGeometry.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "decals/DecalResolve.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Renderer.h"
#include <crimson/renderer/core/Profiler.hpp>
#include "resources/AssetId.h"
#include "resources/CacheIO.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_set>

// This executable runs the actual resource coordinator with a null editor and
// the actual preview/resource owners directly. No EditorSession is constructed;
// its document/UI integration is outside this regression's boundary. These two
// link endpoints must remain unreachable, rather than replacing any resource,
// watcher, graphics, thumbnail or deletion implementation with a test double.
namespace crimson {
void EditorSession::filesChanged(std::span<const std::filesystem::path>) {
    throw std::logic_error("The deletion resource regression unexpectedly entered editor UI integration.");
}
void EditorSession::prepareFrame(AssetLibrary&,Renderer&,std::uint32_t) {
    throw std::logic_error("The deletion resource regression unexpectedly constructed an editor session.");
}
}

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
void require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("ad-"+std::to_string(Clock::now().time_since_epoch().count()));
    Project() {fs::create_directories(root);}
    ~Project() {
        std::error_code error;
        const auto temporary=fs::weakly_canonical(fs::temp_directory_path(),error);
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temporary&&target.filename().string().starts_with("ad-"))fs::remove_all(target,error);
    }
};
struct SdlOwner {
    SdlOwner() {require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}
    ~SdlOwner() {SDL_Quit();}
};
struct ImGuiOwner {
    ImGuiOwner() {ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;}
    ~ImGuiOwner() {ImGui::DestroyContext();}
};
void image(const fs::path& file,const std::array<std::uint8_t,4>& pixel) {
    std::vector<std::uint8_t> pixels;
    for(unsigned index=0;index<16*16;++index)pixels.insert(pixels.end(),pixel.begin(),pixel.end());
    std::vector<unsigned char> encoded;
    require(lodepng::encode(encoded,pixels,16,16)==0,"Could not encode deletion fixture image.");
    writeFileAtomically(file,{reinterpret_cast<const char*>(encoded.data()),encoded.size()});
}
MaterialAsset graph(const fs::path& root,const std::string& name,const std::string& color,const std::string& normal) {
    auto material=createMaterialAsset(root,name);
    const auto output=materialOutputNode(material.graph).id;
    // Mask coverage deliberately uses the generated graph packet path, with
    // independent color/normal roles and the same texture shared by both models.
    materialOutputNode(material.graph).output.blend=MaterialBlend::Mask;
    const auto base=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,base)->texture.image.id=color;
    connectMaterialNodes(material.graph,base,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(material.graph,base,MaterialPin::A,output,MaterialPin::OpacityMask);
    const auto mappedNormal=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,mappedNormal)->texture.image.id=normal;
    findMaterialNode(material.graph,mappedNormal)->texture.role=MaterialTextureRole::Normal;
    connectMaterialNodes(material.graph,mappedNormal,MaterialPin::Normal,output,MaterialPin::Normal);
    canonicalizeMaterialAsset(material);saveMaterialAsset(root,material);return material;
}
bool catalogContains(const AssetLibrary& assets,const std::string& id) {
    return std::any_of(assets.catalog().begin(),assets.catalog().end(),[&](const auto& entry){return entry.id==id;});
}
void materialThumbnails(SDL_Window* window,const fs::path& sourceRoot={}) {
    Project project;initializeContentStorage(project.root);
    const auto colorId=newAssetId(),normalId=newAssetId();
    const auto colorFile=project.root/"Content"/"Color.png",normalFile=project.root/"Content"/"Normal.png";
    fs::create_directories(colorFile.parent_path());
    image(colorFile,{180,80,40,255});image(normalFile,{128,128,255,255});
    const std::array<StoredAssetPath,2> images{{{colorId,colorFile},{normalId,normalFile}}};registerAssetPaths(project.root,images);
    auto fixed=graph(project.root,"Fixed Quader texture",colorId,normalId);
    materialOutputNode(fixed.graph).output.blend=MaterialBlend::Opaque;canonicalizeMaterialAsset(fixed);saveMaterialAsset(project.root,fixed);
    require(materialGraphFixedSurface(fixed.graph).has_value(),"The Quader-like thumbnail fixture no longer exercises fixed material lowering.");
    const auto generated=graph(project.root,"Generated masked texture",colorId,normalId);
    auto blend=createMaterialAsset(project.root,"Height blend");initializeBlendMaterialGraph(blend.graph);
    std::array<MaterialNodeId,4> layers{};const std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
    for(std::size_t i=0;i<pins.size();++i){const auto* link=materialInputLink(blend.graph,materialOutputNode(blend.graph).id,pins[i]);require(link,"Blend fixture omitted a source layer.");layers[i]=link->fromNode;findMaterialNode(blend.graph,layers[i])->material.id=fixed.id;}
    const auto height=addMaterialNode(blend.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(blend.graph,height)->texture.image.id=colorId;findMaterialNode(blend.graph,height)->texture.role=MaterialTextureRole::Data;
    connectMaterialNodes(blend.graph,height,MaterialPin::R,layers[0],MaterialPin::Height);findMaterialNode(blend.graph,layers[1])->blendLayer.heightBlend=true;
    canonicalizeMaterialAsset(blend);saveMaterialAsset(project.root,blend);
    auto decalMaterial=createMaterialAsset(project.root,"Decal material");materialOutputNode(decalMaterial.graph).output.shading=MaterialShading::Decal;
    const auto decalTexture=addMaterialNode(decalMaterial.graph,MaterialNodeKind::Texture2D).id;findMaterialNode(decalMaterial.graph,decalTexture)->texture.image.id=colorId;
    connectMaterialNodes(decalMaterial.graph,decalTexture,MaterialPin::RGB,materialOutputNode(decalMaterial.graph).id,MaterialPin::BaseColor);
    canonicalizeMaterialAsset(decalMaterial);saveMaterialAsset(project.root,decalMaterial);
    auto decalAsset=createDecalAsset(project.root,"Decal asset");decalAsset.material.id=decalMaterial.id;saveDecalAsset(project.root,decalAsset);
    std::vector<std::string> visible{fixed.id,generated.id,blend.id,decalMaterial.id,decalAsset.id};
    std::vector<fs::path> originalFiles;
    if(!sourceRoot.empty()) {
        // Read actual project material graphs and images into an isolated cache
        // root. Keep every UUID and byte intact; the running editor's project,
        // source files and shader/preview caches are never mutation targets.
        std::unordered_set<std::string> copied;
        const auto copySource=[&](const std::string& id) {
            const auto source=resolveAssetPath(sourceRoot,id);
            require(fs::is_regular_file(source),"The actual-project thumbnail source is missing: "+id);
            const auto target=project.root/"Content"/"ActualInputs"/(id+source.extension().string());
            fs::create_directories(target.parent_path());copyFileAtomically(source,target);
            registerAssetPath(project.root,id,target);originalFiles.push_back(source);
        };
        std::function<void(const std::string&)> copyMaterial=[&](const std::string& id) {
            if(isEngineAssetId(id)||!copied.insert(id).second)return;
            const auto material=loadMaterialAsset(sourceRoot,id);
            for(const auto& texture:materialTextureReferences(material))
                if(!isEngineAssetId(texture)&&copied.insert(texture).second)copySource(texture);
            for(const auto& child:materialMaterialReferences(material))copyMaterial(child);
            copySource(id);
        };
        for(const auto* id:{"a98c9d63-efed-5f84-b13a-9273b13b1734", // imported Quader stone
                            "c3744381-b311-4b32-a277-507ee54a5269", // saved height blend example
                            "61e7eb9b-f476-5edb-8a3d-00a102cc6d3a"}) { // user's current decal material
            copyMaterial(id);visible.emplace_back(id);
        }
    }
    const auto originalFingerprint=fingerprintFiles(originalFiles);

    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    ImGuiOwner context;ImGuiBgfx tools(renderer);AssetLibrary assets(project.root,&resources);
    ResourceRuntime runtime(project.root,resources,assets,renderer,tools);EditorResources prepared(assets,renderer,resources,project.root);
    TestGpuContext gpu;renderer.resizeViewport(64,64);renderer.setGridVisible(false);bgfx::reset(64,64,BGFX_RESET_NONE);
    auto environment=std::make_shared<PreparedEnvironment>();environment->key=0x54484d424d4154;
    environment->settings={2,8,8,16,16,16};environment->data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})},environment->settings);
    ResourceRequestOptions environmentOptions;environmentOptions.variant="ibl";
    resources.seed<PreparedEnvironment>(AssetRef<PreparedEnvironment>{StudioEnvironmentId},{environment,sizeof(PreparedEnvironment),environment->key,{}},environmentOptions);
    std::uint32_t frame{};
    const auto tick=[&]{runtime.preFrame(nullptr,frame);prepared.beginFrame();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();};
    const auto wait=[&](AssetPreviews& previews,const char* phase,bool diskOnly){
        const auto begin=Clock::now(),deadline=begin+std::chrono::seconds{120};auto nextLog=begin;
        for(;;){
            tick();previews.pump(frame,visible,32);prepared.endFrame();
            const bool ready=std::all_of(visible.begin(),visible.end(),[&](const auto& id){return bgfx::isValid(renderer.thumbnailTexture(id));});
            if(ready){const auto stats=previews.stats();std::cout<<phase<<" material thumbnails ready in "<<std::chrono::duration<double,std::milli>(Clock::now()-begin).count()<<"ms; renders="<<stats.renderRequests<<" disk="<<stats.diskHits<<'\n'<<std::flush;
                if(diskOnly)require(stats.renderRequests==0&&stats.diskHits==visible.size(),"Warm material thumbnails did not restore through their persistent source descriptors.");return;}
            if(Clock::now()>=nextLog){nextLog=Clock::now()+std::chrono::seconds{2};for(const auto& id:visible)if(!bgfx::isValid(renderer.thumbnailTexture(id)))std::cout<<phase<<' '<<assets.displayName(id)<<": "<<previews.diagnostic(id)<<'\n'<<std::flush;}
            require(Clock::now()<deadline,std::string(phase)+" material thumbnails failed to converge. Assets: "+assets.status()+"\nPreviews: "+previews.status()+"\nModels: "+prepared.status());
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    };
    {
        AssetPreviews previews(project.root,assets,resources,renderer,prepared);wait(previews,"cold",false);
        // Finish the ordinary async PNG/index writes before discarding the owner.
        const auto deadline=Clock::now()+std::chrono::seconds{15};bool persisted{};
        while(Clock::now()<deadline){tick();previews.pump(frame,visible,32);prepared.endFrame();
            const auto folder=project.root/".crimson"/"cache"/"previews";
            persisted=std::all_of(visible.begin(),visible.end(),[&](const auto& id){return fs::is_regular_file(assetPreviewIndexPath(folder,id,32));});
            if(persisted)break;std::this_thread::sleep_for(std::chrono::milliseconds{1});}
        require(persisted,"Material thumbnail pixels became resident but their persistent source indexes never completed.");
        auto policy=streaming::detail::HashSeed;const auto serialized=assets.references().serialize();streaming::detail::hash(policy,serialized.data(),serialized.size());
        for(const auto& id:visible){const auto cached=readAssetPreviewCache(project.root,project.root/".crimson"/"cache"/"previews",id,32,renderer.shaderSignature(),policy);std::size_t colored{};
            for(std::size_t offset=0;offset<cached.image->rgba.size();offset+=4)colored+=cached.image->rgba[offset]>cached.image->rgba[offset+1]+10&&cached.image->rgba[offset]>cached.image->rgba[offset+2]+10;
            if(std::find(visible.begin(),visible.begin()+5,id)!=visible.begin()+5)
                require(colored>20,"Material thumbnail published a blank/neutral fallback instead of its authored red surface: "+assets.displayName(id));}
    }
    for(const auto& id:visible)renderer.evictThumbnail(id);
    {AssetPreviews reopened(project.root,assets,resources,renderer,prepared);wait(reopened,"warm",true);}
    require(fingerprintFiles(originalFiles)==originalFingerprint,"The actual-project thumbnail probe changed an authored source.");
    runtime.stop();frame=renderer.drainPending(frame);tools.shutdown();
}
// Explicit saved-project diagnostic; every resource owner and write target uses
// this disposable copy. This mode is deliberately outside the routine CTest set.
void thumbnailProject(SDL_Window* window,const fs::path& source,const std::string& sceneId,const fs::path& output) {
    Project project;
    const auto original=fs::weakly_canonical(source);
    std::vector<fs::path> originals;
    std::cout<<"Staging thumbnail diagnostic from "<<original<<" into "<<project.root<<std::endl;
    for(const char* name:{"Content","Engine","assets",".crimson"}) {
        const auto from=original/name;if(!fs::is_directory(from))continue;
        fs::create_directories(project.root/name);
        for(const auto& item:fs::directory_iterator(from)) {
            const auto filename=item.path().filename();
            if(filename=="branding"||filename==".resource-transactions"||filename.string().starts_with(".staging"))continue;
            if(filename=="cache") {
                fs::create_directories(project.root/name/filename);
                for(const char* cache:{"mesh-lods","texture-mips","shaders"})
                    if(fs::is_directory(item.path()/cache))fs::copy(item.path()/cache,project.root/name/filename/cache,fs::copy_options::recursive);
            }else {
                fs::copy(item.path(),project.root/name/filename,fs::copy_options::recursive);
                if(item.is_regular_file())originals.push_back(item.path());
                else for(const auto& file:fs::recursive_directory_iterator(item.path()))if(file.is_regular_file())originals.push_back(file.path());
            }
        }
    }
    initializeContentStorage(project.root);
    const auto sceneFile=resolveAssetPath(project.root,sceneId);
    const auto relativeScene=sceneFile.lexically_relative(project.root);
    require(!relativeScene.empty()&&*relativeScene.begin()!="..","Copied thumbnail scene escaped its disposable project.");
    std::sort(originals.begin(),originals.end());
    const auto originalFingerprint=fingerprintFiles(originals);
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    ImGuiOwner context;ImGuiBgfx tools(renderer);AssetLibrary assets(project.root,&resources);
    ResourceRuntime runtime(project.root,resources,assets,renderer,tools);EditorResources prepared(assets,renderer,resources,project.root);
    AssetPreviews previews(project.root,assets,resources,renderer,prepared);TestGpuContext gpu;
    renderer.resizeViewport(64,64);renderer.setGridVisible(false);bgfx::reset(64,64,BGFX_RESET_NONE);
    const auto snapshot=readSceneSnapshot(project.root,sceneId);Scene scene;scene.apply(snapshot.records,snapshot.properties);
    const auto extracted=extractScene(scene);const auto& settings=extracted.environment.environment;
    const auto sourceEnvironment=settings.source==EnvironmentSource::Hdri?assets.references().resolveEnvironment(settings.map):std::string{};
    const auto environmentId=sourceEnvironment.empty()?std::string(StudioEnvironmentId):sourceEnvironment;
    auto environment=std::make_shared<PreparedEnvironment>();environment->key=0x54484d50524f4a;
    environment->settings={2,8,8,16,16,16};environment->data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})},environment->settings);
    ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Preview;
    if(!sourceEnvironment.empty())options.dependencies={resolveAssetPath(project.root,sourceEnvironment)};
    const auto releaseEnvironment=std::make_shared<std::atomic_bool>(false);
    const auto gate=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{environmentId},[environment,releaseEnvironment](std::stop_token stop){
        while(!releaseEnvironment->load()&&!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds{1});
        if(stop.stop_requested())throw std::runtime_error("Saved-project thumbnail environment gate canceled.");
        return ResourceLoadResult<PreparedEnvironment>{environment,sizeof(PreparedEnvironment),environment->key,{}};
    },options);
    const std::array visible{sceneId};std::uint32_t frame{};const auto begin=Clock::now(),deadline=begin+std::chrono::seconds{90};
    std::optional<Clock::time_point> gated,released,settled;auto samples=nlohmann::json::array();
    std::vector<double> allTimes,planningTimes,gatedTimes,releasedTimes,settledTimes;bool gateVerified{};
    while(Clock::now()<deadline) {
        runtime.preFrame(nullptr,frame);prepared.beginFrame();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();
        const std::string phase=settled?"settled":released?"released":gated?"gated":"planning";
        const auto pumpBegin=Clock::now();previews.pump(frame,visible,128);
        const auto pumpMs=std::chrono::duration<double,std::milli>(Clock::now()-pumpBegin).count();prepared.endFrame();
        const auto now=Clock::now();const auto stats=previews.stats();const auto diagnostic=previews.diagnostic(sceneId);
        const bool published=bgfx::isValid(renderer.thumbnailTexture(sceneId));
        if(!gated&&stats.sourcePlanBuilds&&diagnostic=="Preparing thumbnail environment")gated=now;
        // Hold longer than the production 5 s resource-interest idle grace.
        if(gated&&!releaseEnvironment->load()&&now-*gated>=std::chrono::seconds{6}) {
            gateVerified=gate.state()!=ResourceState::Ready;releaseEnvironment->store(true);released=now;
            std::cout<<"Released thumbnail environment gate after retained-plan idle interval."<<std::endl;
        }
        if(releaseEnvironment->load()&&!settled&&(published||stats.decalPreparationFailures))settled=now;
        samples.push_back({{"frame",frame},{"elapsed_ms",std::chrono::duration<double,std::milli>(now-begin).count()},
            {"phase",phase},{"pump_ms",pumpMs},{"diagnostic",diagnostic},{"published",published},{"runtime_status",runtime.status()},
            {"asset_status",assets.status()},{"prepared_status",prepared.status()},
            {"source_plan_builds",stats.sourcePlanBuilds},{"source_plan_hits",stats.sourcePlanHits},{"decal_preparations",stats.decalPreparations},
            {"decal_failures",stats.decalPreparationFailures},{"render_requests",stats.renderRequests},{"resident_hits",stats.residentHits},{"disk_hits",stats.diskHits}});
        allTimes.push_back(pumpMs);if(phase=="planning")planningTimes.push_back(pumpMs);if(phase=="gated")gatedTimes.push_back(pumpMs);
        if(phase=="released"||phase=="settled")releasedTimes.push_back(pumpMs);if(phase=="settled")settledTimes.push_back(pumpMs);
        shared::profiling::frame_mark();
        if(released&&now-*released>=std::chrono::seconds{15})break;
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }
    releaseEnvironment->store(true);
    const auto summarize=[](std::vector<double> values) {
        if(values.empty())return nlohmann::json{{"frames",0},{"max_ms",0},{"p50_ms",0},{"p95_ms",0}};
        std::sort(values.begin(),values.end());return nlohmann::json{{"frames",values.size()},{"max_ms",values.back()},
            {"p50_ms",values[(values.size()-1)/2]},
            {"p95_ms",values[static_cast<std::size_t>(std::ceil(values.size()*.95))-1]}};
    };
    const auto stats=previews.stats();
    const auto resourceStats=resources.stats();
    const bool originalsUnchanged=fingerprintFiles(originals)==originalFingerprint;
    nlohmann::json report{{"source_project",original.generic_string()},{"scene_id",sceneId},{"scene_name",snapshot.name},{"pixel_edge",128},
        {"source_file_count",originals.size()},{"source_fingerprint",originalFingerprint},{"source_fingerprint_algorithm","fingerprintFiles/FNV-1a64"},
        {"source_files_unchanged",originalsUnchanged},
        {"source_environment_id",environmentId},{"environment_override","No scene edit; matching ibl resource uses a gated tiny neutral CPU bake"},
        {"tracy_enabled",shared::profiling::enabled()},{"tracy_connected",shared::profiling::connected()},
        {"gate_verified_pending",gateVerified},{"settled",settled.has_value()},{"published",bgfx::isValid(renderer.thumbnailTexture(sceneId))},
        {"diagnostic",previews.diagnostic(sceneId)},{"status",previews.status()},{"all",summarize(allTimes)},{"planning",summarize(planningTimes)},{"gated",summarize(gatedTimes)},
        {"after_release",summarize(releasedTimes)},{"after_settled",summarize(settledTimes)},
        {"source_plan_builds",stats.sourcePlanBuilds},{"decal_preparations",stats.decalPreparations},{"decal_failures",stats.decalPreparationFailures},{"frames",samples}};
    report["resources"]={{"records",resourceStats.records},{"live_interests",resourceStats.liveInterests},
        {"loads",resourceStats.loads},{"failures",resourceStats.failures},{"queued",resourceStats.queued},{"running",resourceStats.running}};
    const auto reportFile=fs::absolute(output);fs::create_directories(reportFile.parent_path());
    writeFileAtomically(reportFile,report.dump(2));
    runtime.stop();frame=renderer.drainPending(frame);tools.shutdown();
    require(originalsUnchanged,"Saved-project thumbnail diagnostic changed an authored source.");
    std::cout<<"Saved-project thumbnail diagnostic: "<<reportFile<<"; "<<report.at("all").dump()<<"; after release "<<report.at("after_release").dump()<<std::endl;
    require(gateVerified&&settled.has_value(),"Saved-project thumbnail diagnostic did not complete its retained-plan gate and readiness/failure interval.");
}
// Exercise failure/backoff through the actual thumbnail owner. The material
// dependency changes, while the scene asset and its authored decal stay intact.
void decalThumbnailRetry(SDL_Window* window) {
    Project project;initializeContentStorage(project.root);
    auto receiverMaterial=createMaterialAsset(project.root,"Retry receiver");
    materialOutputNode(receiverMaterial.graph).output.shading=MaterialShading::Unlit;
    canonicalizeMaterialAsset(receiverMaterial);saveMaterialAsset(project.root,receiverMaterial);
    auto decalMaterial=createMaterialAsset(project.root,"Retry decal material");
    materialOutputNode(decalMaterial.graph).output.shading=MaterialShading::Decal;
    materialOutputNode(decalMaterial.graph).output.baseColor={1,.1f,.1f};
    canonicalizeMaterialAsset(decalMaterial);saveMaterialAsset(project.root,decalMaterial);
    auto decalAsset=createDecalAsset(project.root,"Retry decal");decalAsset.material.id=decalMaterial.id;saveDecalAsset(project.root,decalAsset);
    SceneDocument document(project.root);const auto box=document.createBox("Retry receiver");
    auto boxRecord=*document.scene().find(box);boxRecord.modelMaterials={AssetRef<MaterialAsset>{receiverMaterial.id}};
    auto primitive=boxRecord.primitive.value_or(PrimitiveMeshSettings{});primitive.size={1.25f,1,1.5f};boxRecord.primitive=primitive;
    const auto geometryId=builtinModelGeometryId(boxRecord.model->id,primitive);
    require(document.updateEntity(box,boxRecord),document.status());
    SceneDecal decal;decal.asset.id=decalAsset.id;decal.seed.receiver=box;decal.seed.modelId=boxRecord.model->id;decal.seed.localPoint={.1f,.5f,.1f};
    const auto placement=decals::surfacePlacementFrame(decal.seed.localPoint,{0,1,0})*math::scaling({.4f,.4f,1});
    document.createDecal(decal,"Retry surface decal",placement);
    require(document.save("Decal thumbnail retry"),document.status());const auto sceneId=document.assetId();
    const auto sceneFile=resolveAssetPath(project.root,sceneId),materialFile=resolveAssetPath(project.root,receiverMaterial.id);
    const auto unchangedScene=fingerprintFiles({sceneFile});

    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    ImGuiOwner context;ImGuiBgfx tools(renderer);AssetLibrary assets(project.root,&resources);
    ResourceRuntime runtime(project.root,resources,assets,renderer,tools);EditorResources prepared(assets,renderer,resources,project.root);
    AssetPreviews previews(project.root,assets,resources,renderer,prepared);TestGpuContext gpu;
    renderer.resizeViewport(64,64);renderer.setGridVisible(false);bgfx::reset(64,64,BGFX_RESET_NONE);
    auto environment=std::make_shared<PreparedEnvironment>();environment->key=0x444543414c525452;
    environment->settings={2,8,8,16,16,16};environment->data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})},environment->settings);
    ResourceRequestOptions environmentOptions;environmentOptions.variant="ibl";
    const auto releaseEnvironment=std::make_shared<std::atomic_bool>(false);
    const auto environmentPending=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{StudioEnvironmentId},[environment,releaseEnvironment](std::stop_token stop){
        while(!releaseEnvironment->load()&&!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds{1});
        if(stop.stop_requested())throw std::runtime_error("Thumbnail retention environment gate canceled.");
        return ResourceLoadResult<PreparedEnvironment>{environment,sizeof(PreparedEnvironment),environment->key,{}};
    },environmentOptions);
    const std::array visible{sceneId};std::uint32_t frame{};
    const auto tick=[&]{runtime.preFrame(nullptr,frame);prepared.beginFrame();renderer.advanceStreaming();frame=renderer.advance_frame();gpu.flush();
        previews.pump(frame,visible,32);prepared.endFrame();require(runtime.status().empty(),"Decal thumbnail coordinator: "+runtime.status());};
    const auto until=[&](const auto& condition,const char* reason,std::chrono::seconds timeout){const auto deadline=Clock::now()+timeout;
        do{tick();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(Clock::now()<deadline);
        throw std::runtime_error(std::string(reason)+"\nThumbnail: "+previews.diagnostic(sceneId)+"\nAssets: "+assets.status());};
    until([&]{return previews.stats().sourcePlanBuilds>0&&previews.diagnostic(sceneId)=="Preparing thumbnail environment";},
        "Saved-scene metadata did not reach the gated thumbnail environment.",std::chrono::seconds{15});
    const auto retainedPlan=previews.stats();
    for(unsigned retry=0;retry<16;++retry){
        tick();require(assets.primitiveModelAlive(geometryId),"A retained thumbnail scene plan released its weakly cached receiver metadata during asynchronous readiness.");
        const auto stats=previews.stats();
        require(stats.sourcePlanBuilds==retainedPlan.sourcePlanBuilds&&stats.decalPreparations==0&&stats.renderRequests==0,
            "Metadata retention used scene replanning, decal geometry or GPU rendering to pin its receiver.");
    }
    require(environmentPending.state()!=ResourceState::Ready,"Thumbnail metadata retention bypassed its pending resource control.");
    releaseEnvironment->store(true);
    until([&]{return previews.stats().decalPreparationFailures>0;},"Unlit receiver did not reach actual terminal thumbnail failure.",std::chrono::seconds{15});
    require(previews.diagnostic(sceneId).find("Unlit")!=std::string::npos,"Terminal thumbnail fixture failed for an unrelated reason.");
    const auto failed=previews.stats();const auto retryBegin=Clock::now();
    constexpr unsigned Requests=256;
    for(unsigned index=0;index<Requests;++index)tick();
    const auto after=previews.stats();
    // The retry policy is time based. This bound tolerates a slow test machine
    // without assuming a particular CPU/GPU frame duration or sleeping a second.
    const auto maximumRetries=static_cast<std::uint64_t>(std::ceil(std::chrono::duration<double>(Clock::now()-retryBegin).count()))+1;
    require(after.requests>=failed.requests+Requests,"The regression did not issue repeated actual thumbnail-owner requests.");
    require(after.decalPreparations-failed.decalPreparations<=maximumRetries&&after.decalPreparationFailures-failed.decalPreparationFailures<=maximumRetries,
        "A terminal decal thumbnail repeated full preparation on ordinary visible-card requests.");
    require(after.renderRequests==0&&!bgfx::isValid(renderer.thumbnailTexture(sceneId)),"An invalid decal scene was published as a finished thumbnail.");

    // This is a dependent material file, not the scene's own identity. Pending
    // failure descriptors must make this event reset the owner's real backoff.
    materialOutputNode(receiverMaterial.graph).output.shading=MaterialShading::StandardPbr;
    canonicalizeMaterialAsset(receiverMaterial);saveMaterialAsset(project.root,receiverMaterial);
    assets.publishMaterial(std::make_shared<const MaterialAsset>(receiverMaterial));
    const auto beforeInvalidate=previews.stats();previews.invalidateFile(materialFile);
    require(previews.stats().invalidations>beforeInvalidate.invalidations,"A terminal thumbnail lost its material dependency invalidation route.");
    const auto failuresBeforeRepair=previews.stats().decalPreparationFailures;
    bool sawWorkerPending=false,sawMaterialPending=false;
    until([&]{const auto stats=previews.stats();
        require(stats.decalPreparationFailures==failuresBeforeRepair,"An active decal worker/material preparation acquired terminal thumbnail backoff.");
        const auto diagnostic=previews.diagnostic(sceneId);
        sawWorkerPending|=diagnostic.find("Preparing decal surface")!=std::string::npos||diagnostic.find("Preparing surface geometry")!=std::string::npos;
        sawMaterialPending|=diagnostic.find("Compiling")!=std::string::npos||diagnostic.find("Preparing decal material")!=std::string::npos;
        return bgfx::isValid(renderer.thumbnailTexture(sceneId));
    },"A repaired material dependency did not resume the actual decal thumbnail.",std::chrono::seconds{90});
    const auto repaired=previews.stats();
    require(repaired.decalPreparations>after.decalPreparations+1&&repaired.renderRequests>0,
        "Dependency invalidation did not resume repeated nonterminal decal preparation and publication.");
    require(sawWorkerPending||sawMaterialPending,"The repaired fixture stopped exercising asynchronous decal readiness.");
    require(fingerprintFiles({sceneFile})==unchangedScene,"Dependent-material thumbnail repair changed the authored scene or decal.");
    std::cout<<"Actual decal thumbnail retry: requests="<<after.requests-failed.requests
        <<" terminal_preparations="<<after.decalPreparations-failed.decalPreparations
        <<" repaired_preparations="<<repaired.decalPreparations-after.decalPreparations
        <<" worker_pending="<<sawWorkerPending<<" material_pending="<<sawMaterialPending<<" published=true\n";
    runtime.stop();frame=renderer.drainPending(frame);tools.shutdown();
}


}
int main(int argc,char** argv) {
    try {
        const bool tracy=argc>1&&std::string_view(argv[argc-1])=="--tracy";
        if(tracy){--argc;shared::profiling::set_enabled(true);shared::profiling::thread_name("Saved-project thumbnail diagnostic");}
        SdlOwner sdl;
        const std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson deletion resource regression",64,64,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        require(bool(window),SDL_GetError());
        if(argc>=2&&std::string_view(argv[1])=="--thumbnail-project") {
            require(argc==5,"Usage: asset_deletion_runtime_tests --thumbnail-project PROJECT SCENE_ID OUTPUT_JSON [--tracy]");
            thumbnailProject(window.get(),argv[2],argv[3],argv[4]);
        }
        else if(argc>=2&&std::string_view(argv[1])=="--decal-thumbnail-retry")decalThumbnailRetry(window.get());
        else if(argc>=2&&std::string_view(argv[1])=="--material-thumbnails")materialThumbnails(window.get(),argc>=3?fs::path(argv[2]):fs::path{});
        else
        return 0;
    }catch(const std::exception& error){std::cerr<<"Asset deletion runtime test failed: "<<error.what()<<'\n';return 1;}
}
