// Explicit actual-project diagnostic. Never initialize asset storage against
// the authored source: all resource preparation runs in a private short clone.
#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Picking.h"
#include "editor/EditorRenderFrame.h"
#include "editor/EditorResources.h"
#include "GpuTestBackend.h"
#include "math/Json.h"
#include "resources/ShaderCompiler.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
struct Options {
    fs::path source,artifacts,shaderDirectory;std::string scene{"Content/Main.crimson-scene"},camera{"ShadowCamera"},settings{"both"},channel{"both"};
    EntityId emitter{0x5bb7882f31e19ae7};unsigned width{960},height{540},warmup{512},frames{48},staticSamples{};
    float amplitude{.35f};bool keepProject{},stationaryOnly{};std::map<std::string,std::string> overrides;
};
SsgiSettings overrideSettings(SsgiSettings value,const Options& options) {
    for(const auto& [option,text]:options.overrides) {
        if(option=="--ssgi-quality") {
            if(text=="low")value.quality=SsgiQuality::Low;else if(text=="medium")value.quality=SsgiQuality::Medium;
            else if(text=="high")value.quality=SsgiQuality::High;else if(text=="custom")value.quality=SsgiQuality::Custom;
            else throw std::runtime_error("Unknown SSGI quality.");
        } else if(option=="--ssgi-rays") {const auto budget=ssgiQualityBudget(value);value.rays=std::stoi(text);value.steps=int(budget.steps);value.quality=SsgiQuality::Custom;}
        else if(option=="--ssgi-steps") {const auto budget=ssgiQualityBudget(value);value.steps=std::stoi(text);value.rays=int(budget.rays);value.quality=SsgiQuality::Custom;}
        else if(option=="--ssgi-bounces")value.bounces=std::stoi(text);
        else if(option=="--ssgi-radius")value.denoiseRadius=std::stoi(text);
        else if(option=="--ssgi-strength")value.strength=std::stof(text);
        else if(option=="--ssgi-reach")value.maxDistance=std::stof(text);
        else if(option=="--ssgi-thickness")value.thickness=std::stof(text);
        else if(option=="--ssgi-stability")value.temporalStability=std::stof(text);
        else if(option=="--ssgi-bias")value.rayBias=std::stof(text);
        else if(option=="--ssgi-smoothing")value.denoiseStrength=std::stof(text);
        else if(option=="--ssgi-clamp")value.radianceClamp=std::stof(text);
        else throw std::runtime_error("Unknown SSGI tuning option: "+option);
    }
    check(validSsgiSettings(value),"SSGI override settings are invalid.");return value;
}
std::string fileHash(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);check(bool(input),"Cannot hash "+path.string());
    std::array<char,65536> buffer{};std::uint64_t hash=14695981039346656037ull;
    while(input){input.read(buffer.data(),buffer.size());for(std::streamsize i=0;i<input.gcount();++i){hash^=static_cast<unsigned char>(buffer[std::size_t(i)]);hash*=1099511628211ull;}}
    check(input.eof(),"Cannot finish hashing "+path.string());std::ostringstream output;output<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return output.str();
}
std::string entityText(EntityId id){std::ostringstream text;text<<std::hex<<std::setw(16)<<std::setfill('0')<<id;return text.str();}
struct Snapshot {
    fs::path root=fs::temp_directory_path()/("cssgi-motion-"+std::to_string(Clock::now().time_since_epoch().count()));
    Json files=Json::array();bool keep{};
    Snapshot(const fs::path& source,bool retain):keep(retain) {
        fs::create_directories(root);
        const auto copy=[&](const fs::path& file) {
            const auto relative=file.lexically_relative(source);check(!relative.empty()&&!relative.is_absolute(),"Invalid project snapshot member.");
            for(const auto& part:relative)check(part!="..","Project snapshot member escaped the source.");
            const auto before=fileHash(file);const auto target=root/relative;fs::create_directories(target.parent_path());fs::copy_file(file,target);
            check(fileHash(target)==before&&fileHash(file)==before,"Project changed during snapshot: "+relative.string());
            files.push_back({{"path",relative.generic_string()},{"fnv1a64",before},{"bytes",fs::file_size(file)}});
        };
        for(const char* name:{"Content","Engine","assets"})if(fs::is_directory(source/name))
            for(const auto& entry:fs::recursive_directory_iterator(source/name)) {
                check(!entry.is_symlink(),"Project snapshot does not follow symbolic links.");
                if(entry.is_regular_file())copy(entry.path());
            }
        // Cache reconstructions and asset-store startup are allowed only in
        // this clone. Retain durable UUID/reference metadata, omit live staging.
        if(fs::is_directory(source/".crimson"))for(const auto& entry:fs::directory_iterator(source/".crimson"))
            if(entry.is_regular_file())copy(entry.path());
    }
    ~Snapshot(){if(keep||std::uncaught_exceptions()){std::cerr<<"Retained disposable project: "<<root<<'\n';return;}
        std::error_code error;const auto target=fs::weakly_canonical(root,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("cssgi-motion-"))fs::remove_all(target,error);}
};
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {bgfx::TextureHandle value{BGFX_INVALID_HANDLE};~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);}};
Json giJson(const SsgiSettings& value) {
    const auto budget=ssgiQualityBudget(value);
    return {{"enabled",value.enabled},{"strength",value.strength},{"maxDistance",value.maxDistance},{"thickness",value.thickness},
        {"quality",static_cast<int>(value.quality)},{"rays",budget.rays},{"steps",budget.steps},{"bounces",value.bounces},
        {"temporalStability",value.temporalStability},{"rayBias",value.rayBias},{"denoiseRadius",value.denoiseRadius},
        {"denoiseStrength",value.denoiseStrength},{"radianceClamp",value.radianceClamp}};
}
void overridePrograms(Renderer& renderer,const ShaderCompileConfig& config,const fs::path& directory) {
    if(directory.empty())return;std::array<ShaderBinary,4> binaries;
    constexpr std::array names{"cs_ssgi_trace","cs_ssgi_temporal","cs_ssgi_filter","cs_ssgi_publish"};
    for(std::size_t index=0;index<binaries.size();++index) {
        const auto path=directory/(std::string(names[index])+".sc");std::ifstream input(path,std::ios::binary);check(bool(input),"Missing override: "+path.string());
        ShaderCompileRecipe recipe;recipe.name=names[index];recipe.source={std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
        recipe.stage=ShaderStage::Compute;recipe.varyingDefinition=config.sourceDirectory/"varying.def.sc";
        recipe.includeDirectories={directory,config.sourceDirectory,config.includeDirectory};recipe.contract="ssgi-motion-override-v1";
        binaries[index]=compileShader(config,recipe);
    }
    check(renderer.reloadPrograms({}, {}, {}, {},binaries[0].bytes,binaries[1].bytes,binaries[2].bytes,binaries[3].bytes),renderer.shaderStatus());
}
double linearLuminance(const DecodedImage& image,std::size_t pixel) {
    constexpr std::array weights{.2126,.7152,.0722};double result{};
    for(unsigned c=0;c<3;++c){const double encoded=image.rgba[pixel*4+c]/255.;result+=weights[c]*(encoded<=.04045?encoded/12.92:std::pow((encoded+.055)/1.055,2.4));}return result;
}
void saveImage(const fs::path& path,const DecodedImage& image) {
    fs::create_directories(path.parent_path());std::vector<unsigned char> encoded;
    check(lodepng::encode(encoded,image.rgba,image.width,image.height)==0,"Cannot encode diagnostic PNG.");
    std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(encoded.data()),std::streamsize(encoded.size()));
    check(bool(output),"Cannot save "+path.string());
}
Json imageMetrics(const DecodedImage& image,const DecodedImage* previous) {
    double mean{},delta{};std::size_t count{};
    // Central receiver region excludes most frame-edge visibility changes.
    // Motion physically changes lighting, so these are comparative trajectory
    // statistics, not errors against a stationary physical reference.
    for(unsigned y=image.height/10;y<image.height*9/10;++y)for(unsigned x=image.width/10;x<image.width*9/10;++x) {
        const auto pixel=std::size_t(y)*image.width+x;const double value=linearLuminance(image,pixel);mean+=value;++count;
        if(previous){const double difference=value-linearLuminance(*previous,pixel);delta+=difference*difference;}
    }
    return {{"central_mean_linear",mean/count},{"central_frame_delta_rms",previous?Json(std::sqrt(delta/count)):Json(nullptr)}};
}
struct Moments {
    unsigned count{};double mean{},m2{};
    void add(double value){++count;const auto delta=value-mean;mean+=delta/count;m2+=delta*(value-mean);}
    double variance()const{return count>1?m2/(count-1):0;}
};
class StationaryStatistics {
public:
    StationaryStatistics(unsigned width,unsigned height) {
        x0_=width*380/960;y0_=height*100/540;x1_=width*570/960;y1_=height*300/540;
        const auto pixels=std::size_t(x1_-x0_)*(y1_-y0_);pixel_.resize(pixels);laplacian_.resize(pixels);
        for(unsigned index=0;index<sizes_.size();++index)blocks_[index].resize(
            std::size_t((x1_-x0_+sizes_[index]-1)/sizes_[index])*((y1_-y0_+sizes_[index]-1)/sizes_[index]));
    }
    Json add(const DecodedImage& image) {
        const unsigned width=x1_-x0_,height=y1_-y0_;std::vector<double> values(pixel_.size());double total{},square{};
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const auto index=std::size_t(y)*width+x;const auto value=linearLuminance(image,std::size_t(y+y0_)*image.width+x+x0_);
            values[index]=value;pixel_[index].add(value);total+=value;square+=value*value;
        }
        frameMean_.add(total/values.size());
        for(unsigned y=1;y+1<height;++y)for(unsigned x=1;x+1<width;++x) {
            const auto index=std::size_t(y)*width+x;
            laplacian_[index].add(4*values[index]-values[index-1]-values[index+1]-values[index-width]-values[index+width]);
        }
        for(unsigned index=0;index<sizes_.size();++index) {
            const unsigned size=sizes_[index],columns=(width+size-1)/size;
            std::vector<double> sums(blocks_[index].size());std::vector<unsigned> counts(sums.size());
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                const auto block=std::size_t(y/size)*columns+x/size;sums[block]+=values[std::size_t(y)*width+x];++counts[block];
            }
            for(std::size_t block=0;block<sums.size();++block)blocks_[index][block].add(sums[block]/counts[block]);
        }
        const double mean=total/values.size();return {{"mean_linear",mean},{"spatial_std_linear",std::sqrt(std::max(0.,square/values.size()-mean*mean))}};
    }
    Json summary()const {
        const auto rms=[](const auto& moments){double variance{};std::size_t count{};for(const auto& item:moments)if(item.count>1){variance+=item.variance();++count;}return count?std::sqrt(variance/count):0.;};
        const double normalizer=std::max(frameMean_.mean,.000001),pixelNoise=rms(pixel_),laplacianNoise=rms(laplacian_);
        Json blocks=Json::array();for(unsigned index=0;index<sizes_.size();++index) {
            const double noise=rms(blocks_[index]);blocks.push_back({{"size_px",sizes_[index]},{"temporal_rms",noise},{"relative_temporal_rms",noise/normalizer}});
        }
        return {{"roi",{x0_,y0_,x1_,y1_}},{"captures",frameMean_.count},{"mean_linear",frameMean_.mean},
            {"pixel_temporal_rms",pixelNoise},{"relative_pixel_temporal_rms",pixelNoise/normalizer},
            {"frame_mean_temporal_rms",std::sqrt(frameMean_.variance())},{"relative_frame_mean_temporal_rms",std::sqrt(frameMean_.variance())/normalizer},
            {"temporal_laplacian_rms",laplacianNoise},{"relative_temporal_laplacian_rms",laplacianNoise/normalizer},{"blocks",std::move(blocks)}};
    }
private:
    unsigned x0_{},y0_{},x1_{},y1_{};Moments frameMean_;
    std::vector<Moments> pixel_,laplacian_;std::array<std::vector<Moments>,3> blocks_;constexpr static std::array<unsigned,3> sizes_{8,16,32};
};

void exercise(SDL_Window* window,Snapshot& snapshot,const ShaderCompileConfig& config,const Options& options) {
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));TestGpuContext context;
    MaterialGraphPrograms programs(resources,config);AssetLibrary assets(snapshot.root,&resources);assets.setMaterialGraphPrograms(&programs);
    EditorResources prepared(assets,renderer,resources,snapshot.root);SceneDocument document(snapshot.root);
    const auto sceneAssetId=assetIdAtPath(snapshot.root,snapshot.root/options.scene);
    check(document.load(sceneAssetId),document.status());
    const auto records=document.scene().records();EntityId cameraId{};
    for(const auto& entity:records)if(entity.camera&&entity.name==options.camera){check(cameraId==NoEntity,"Camera name is ambiguous.");cameraId=entity.id;}
    check(cameraId!=NoEntity,"Requested authored camera was not found.");const auto camera=resolveSceneCamera(document.scene(),cameraId);
    check(camera.has_value(),"Requested authored camera could not be resolved.");
    const auto baseline=extractScene(document.scene());const auto emitter=std::find_if(baseline.models.begin(),baseline.models.end(),[&](const auto& model){return model.id==options.emitter;});
    check(emitter!=baseline.models.end(),"Requested emissive mesh is not present.");
    const auto emissionWorld=emitter->world;const Vec3 originalPosition{emissionWorld[12],emissionWorld[13],emissionWorld[14]};
    const auto environmentMap=baseline.environment.environment.source==EnvironmentSource::Hdri?
        assets.references().resolveEnvironment(baseline.environment.environment.map):std::string{};
    check(renderer.publishEnvironment(*prepareEnvironmentCpu(snapshot.root,environmentMap)),renderer.environmentStatus());
    overridePrograms(renderer,config,options.shaderDirectory);renderer.setGridVisible(false);
    renderer.resizeViewport(1,1);renderer.resizeGameViewport(options.width,options.height);bgfx::reset(320,240,BGFX_RESET_NONE);bgfx::setDebug(BGFX_DEBUG_PROFILER);
    const auto projection=camera->projectionMatrix(float(options.width)/options.height,ClipDepth::ZeroToOne)*camera->viewMatrix();
    const auto projectedEmitter=math::project(originalPosition,projection,{0,0,float(options.width),float(options.height)},ClipDepth::ZeroToOne);
    const bool sourceInFront=math::dot(originalPosition-camera->eye(),camera->forward())>0;
    Json report{{"source_project",options.source.string()},{"disposable_project",snapshot.root.string()},{"source_files",snapshot.files},
        {"backend",renderer.backendName()},{"shader_directory",options.shaderDirectory.empty()?"embedded":options.shaderDirectory.string()},
        {"scene",options.scene},{"camera_name",options.camera},{"camera_id",entityText(cameraId)},
        {"camera",{{"position",camera->position},{"target",camera->target},{"up",camera->up},{"vertical_fov",camera->verticalFov(float(options.width)/options.height)},
            {"near",camera->nearPlane},{"far",camera->farPlane}}},{"emitter_id",entityText(options.emitter)},
        {"emitter_world",emissionWorld},{"width",options.width},{"height",options.height},
        {"warmup_frames",options.warmup},{"static_capture_count",options.staticSamples},{"stationary_only",options.stationaryOnly},
        {"tuning_overrides",options.overrides},
        {"motion",{{"amplitude_m",options.amplitude},{"direction","camera right"},{"period_rendered_frames",120},{"simulation_hz",60}}},
        {"metrics_note","Frame differences include the physical lighting change; compare identical trajectories and settings across shader variants."},
        {"cases",Json::array()}};
    report["emitter_screen_pixel"]=projectedEmitter?Json(*projectedEmitter):Json(nullptr);
    report["emitter_center_in_view"]=projectedEmitter&&sourceInFront&&(*projectedEmitter)[0]>=0&&(*projectedEmitter)[0]<options.width&&(*projectedEmitter)[1]>=0&&(*projectedEmitter)[1]<options.height;
    if(!report["emitter_center_in_view"].get<bool>())std::cout<<"NOTE: saved camera does not contain the emitter center; authored camera is preserved.\n";
    std::uint32_t frame{};std::uint64_t worldIdentity{5000};unsigned phaseFrame{};bool moving{},removed{};Vec3 heldOffset{};
    SsgiSettings settings=baseline.environment.environment.ssgi;EditorRenderFramePreparationStats preparation;Json lastDraw;
    const auto tick=[&]{frame=renderer.advance_frame();context.flush();};
    const auto draw=[&] {
        resources.pump();assets.updateResources(frame);renderer.advanceStreaming();auto extracted=baseline;
        const Vec3 offset=moving?camera->right()*(options.amplitude*std::sin(float(phaseFrame)*6.28318530718f/120.f)):heldOffset;
        for(auto& model:extracted.models)if(model.id==options.emitter){model.world=math::translation(offset)*emissionWorld;if(removed)model.visible=false;}
        extracted.environment.environment.ssgi=settings;prepared.beginFrame();prepared.setMaterialContext({});
        EditorRenderFrameInput input;input.extracted=std::move(extracted);input.settings.environment=input.extracted.environment.environment;
        input.settings.environment.map=environmentMap;input.settings.exposure=baseline.environment.exposure;input.settings.proceduralSun=baseline.proceduralSun;
        input.settings.antiAliasing=AntiAliasing::Smaa;input.sceneCamera=*camera;input.gameCamera=*camera;
        input.sceneWidth=input.sceneHeight=1;input.sceneAspect=float(options.width)/options.height;
        input.gameWidth=options.width;input.gameHeight=options.height;input.gameVisible=true;
        input.worldId=1;input.giWorldId=worldIdentity;input.gameCameraId=cameraId;input.environmentId=document.scene().properties().activeEnvironment;
        const auto preparedFrame=prepareEditorRenderFrame(std::move(input),assets,prepared,&preparation);renderer.renderFrame(preparedFrame);prepared.endFrame();
        check(renderer.ssgiStatus().empty(),renderer.ssgiStatus());check(renderer.shadowStatus().empty(),renderer.shadowStatus());
        lastDraw={{"frame_before_draw",frame},{"ssgi_sequence",frame%4096},{"phase_frame",phaseFrame},
            {"emitter_offset",offset},{"emitter_position",originalPosition+offset},{"removed",removed},{"resources_ready",preparation.ready},{"resources_refined",preparation.refined}};
        if(moving)heldOffset=offset;++phaseFrame;
    };
    const auto warm=[&](unsigned count){for(unsigned i=0;i<count;++i){draw();tick();}};
    const auto read=[&] {
        Texture staging{bgfx::createTexture2D(std::uint16_t(options.width),std::uint16_t(options.height),false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.value),"Actual-scene readback allocation failed.");
        DecodedImage image{options.width,options.height,std::vector<std::uint8_t>(std::size_t(options.width)*options.height*4)};
        bgfx::blit(DiagnosticReadbackView,staging.value,0,0,renderer.gameViewportTexture(),0,0,std::uint16_t(options.width),std::uint16_t(options.height));
        const auto ready=bgfx::readTexture(staging.value,image.rgba.data());check(ready!=UINT32_MAX,"Actual-scene readback is unsupported.");
        for(unsigned attempt=0;attempt<120;++attempt){if(attempt)draw();tick();if(picking::frameReady(frame,ready))return image;}
        throw std::runtime_error("Actual-scene readback timed out.");
    };
    for(const std::string preset:{"saved","defaults"}) {
        if(options.settings!="both"&&options.settings!=preset)continue;
        settings=preset=="saved"?baseline.environment.environment.ssgi:SsgiSettings{};settings.enabled=baseline.environment.environment.ssgi.enabled;
        settings=overrideSettings(settings,options);
        for(const auto debug:{RenderDebugView::Lit,RenderDebugView::IndirectLighting}) {
            const std::string channel=debug==RenderDebugView::Lit?"lit":"indirect";if(options.channel!="both"&&options.channel!=channel)continue;
            renderer.setDebugView(RenderViewPurpose::Game,debug);
            ++worldIdentity;moving=removed=false;heldOffset={};phaseFrame=0;
            const auto deadline=Clock::now()+std::chrono::minutes{3};unsigned readyFrames{};
            do{draw();tick();readyFrames=preparation.ready&&preparation.refined?readyFrames+1:0;if(readyFrames<8)std::this_thread::sleep_for(std::chrono::milliseconds{1});}
            while(readyFrames<8&&Clock::now()<deadline);
            check(readyFrames>=8,"Actual scene resources did not become fully ready: "+prepared.status());
            // Align the shader's 4096-frame sample period across old/new runs,
            // independently of variable asset/material preparation latency.
            while(frame%4096)tick();warm(options.warmup);
            Json result{{"settings",preset},{"channel",channel},{"gi",giJson(settings)},{"frames",Json::array()},
                {"stationary_windows",Json::array()}};DecodedImage previous;
            StationaryStatistics stationary(options.width,options.height),windowStatistics(options.width,options.height);unsigned staticCount{};
            std::vector<std::string> phases{"static"};if(!options.stationaryOnly)phases.insert(phases.end(),{"moving","stopped","removed"});
            for(const std::string phase:phases) {
                moving=phase=="moving";removed=phase=="removed";phaseFrame=0;
                const unsigned duration=phase=="moving"?options.frames:phase=="static"?24:32;
                while(phase=="static"&&options.staticSamples?staticCount<options.staticSamples:phaseFrame<duration) {
                    draw();auto metadata=lastDraw;const auto image=read();metadata["frame_after_readback"]=frame;metadata["phase"]=phase;
                    const auto relative=fs::path(preset+"-"+channel)/(phase+"-"+std::to_string(metadata["phase_frame"].get<unsigned>())+".png");
                    saveImage(options.artifacts/relative,image);metadata["image"]=relative.generic_string();metadata["image_fnv1a64"]=fileHash(options.artifacts/relative);
                    metadata["metrics"]=imageMetrics(image,previous.rgba.empty()?nullptr:&previous);previous=image;
                    if(phase=="static") {
                        metadata["stationary_receiver"]=stationary.add(image);windowStatistics.add(image);++staticCount;
                        if(staticCount%64==0) {auto summary=windowStatistics.summary();summary["first_capture"]=staticCount-64;summary["last_capture"]=staticCount-1;
                            result["stationary_windows"].push_back(std::move(summary));windowStatistics=StationaryStatistics(options.width,options.height);}
                    }
                    const auto* stats=bgfx::getStats();metadata["latest_gpu_frame_after_readback"]=stats->gpuFrameNum;
                    metadata["latest_gpu_elapsed_ms"]=stats->gpuTimerFreq>0&&stats->gpuTimeEnd>=stats->gpuTimeBegin?Json(double(stats->gpuTimeEnd-stats->gpuTimeBegin)*1000./stats->gpuTimerFreq):Json(nullptr);
                    result["frames"].push_back(std::move(metadata));
                }
                std::cout<<preset<<' '<<channel<<' '<<phase<<" complete, bgfx_frame="<<frame<<std::endl;
            }
            result["stationary_summary"]=stationary.summary();
            if(staticCount%64){auto summary=windowStatistics.summary();summary["first_capture"]=staticCount-staticCount%64;summary["last_capture"]=staticCount-1;result["stationary_windows"].push_back(std::move(summary));}
            report["cases"].push_back(std::move(result));
        }
    }
    report["source_files_changed_during_run"]=Json::array();
    for(const auto& item:snapshot.files){const auto source=options.source/item["path"].get<std::string>();if(!fs::is_regular_file(source)||fileHash(source)!=item["fnv1a64"].get<std::string>())report["source_files_changed_during_run"].push_back(item["path"]);}
    fs::create_directories(options.artifacts);std::ofstream output(options.artifacts/"scene-motion.json");output<<report.dump(2)<<'\n';check(bool(output),"Cannot write actual-scene diagnostic report.");
    resources.stop();renderer.drainPending(frame);std::cout<<"Actual-scene captures: "<<options.artifacts<<'\n';
}
} // namespace

int main(int argc,char** argv) {
    try {
        check(argc>=4,"Pass shaderc, shader source and shader include directories plus --project-root SOURCE --artifacts OUTPUT.");Options options;
        for(int index=4;index<argc;++index) {
            const std::string_view option=argv[index];if(option=="--keep-project"){options.keepProject=true;continue;}
            if(option=="--stationary-only"){options.stationaryOnly=true;continue;}
            check(index+1<argc,"Missing diagnostic option value.");const std::string value=argv[++index];
            if(option=="--project-root")options.source=fs::weakly_canonical(fs::absolute(value));
            else if(option=="--artifacts")options.artifacts=fs::absolute(value).lexically_normal();
            else if(option=="--scene")options.scene=value;else if(option=="--camera-name")options.camera=value;
            else if(option=="--emitter-id")options.emitter=std::stoull(value,nullptr,16);
            else if(option=="--settings")options.settings=value;
            else if(option=="--channel")options.channel=value;
            else if(option=="--ssgi-shader-dir"||option=="--shader-dir")options.shaderDirectory=fs::absolute(value).lexically_normal();
            else if(option.starts_with("--ssgi-"))options.overrides[std::string(option)]=value;
            else if(option=="--width")options.width=unsigned(std::stoul(value));else if(option=="--height")options.height=unsigned(std::stoul(value));
            else if(option=="--warmup")options.warmup=unsigned(std::stoul(value));else if(option=="--frames")options.frames=unsigned(std::stoul(value));
            else if(option=="--static-samples")options.staticSamples=unsigned(std::stoul(value));
            else if(option=="--amplitude")options.amplitude=std::stof(value);else throw std::runtime_error("Unknown option: "+std::string(option));
        }
        check(!options.source.empty()&&!options.artifacts.empty(),"Source project and output directory are required.");
        check(options.width>=32&&options.height>=32&&options.width<=8192&&options.height<=8192&&options.frames>=8&&options.frames<=600&&options.warmup<=4096&&std::isfinite(options.amplitude)&&options.amplitude>=0&&options.amplitude<=2,"Diagnostic dimensions or trajectory is invalid.");
        check(options.settings=="saved"||options.settings=="defaults"||options.settings=="both","Settings must be saved, defaults or both.");
        check(options.channel=="lit"||options.channel=="indirect"||options.channel=="both","Channel must be lit, indirect or both.");
        if(options.stationaryOnly&&!options.staticSamples)options.staticSamples=128;
        check(options.staticSamples<=2048,"Static capture count exceeds the diagnostic bound.");
        Snapshot snapshot(options.source,options.keepProject);fs::create_directories(options.artifacts);
        ShaderCompileConfig config{argv[1],argv[2],argv[3],options.artifacts/"shader-cache"};Sdl sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson actual-scene GI motion capture",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get(),snapshot,config,options);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
