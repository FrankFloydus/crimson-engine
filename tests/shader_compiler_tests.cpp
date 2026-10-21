#include "resources/ShaderCompiler.h"
#include "resources/ShaderSourceBaseline.h"
#include "resources/Paths.h"
#include "crimson-renderer/ShaderPreflight.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "assets/MaterialAsset.h"
#include "editor/ShaderRuntimePaths.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <array>
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
void runtimePathChecks(const std::filesystem::path& root) {
    const ShaderRuntimePaths live{root/"checkout"/"shaders",root/"checkout"/"include"};
    const ShaderRuntimePaths staged{root/"application"/"shader-tools"/"shaders",root/"application"/"shader-tools"/"include"};
    for(const auto& tree:{live,staged}) {
        std::filesystem::create_directories(tree.sources);std::filesystem::create_directories(tree.includes);
    }
    const bool liveComplete=std::filesystem::is_directory(live.sources)&&std::filesystem::is_directory(live.includes);
    const auto release=selectShaderRuntimePaths(false,live,staged,liveComplete);
    const auto debug=selectShaderRuntimePaths(true,live,staged,liveComplete);
    require(release==staged,"Release shader compilation selected live checkout shaders beside a staged executable payload.");
    require(debug==live,"Debug shader compilation lost live checkout hot reload.");
    const auto releaseRoots=shaderRuntimeWatchedRoots(root,release),debugRoots=shaderRuntimeWatchedRoots(root,debug);
    require(releaseRoots==std::vector<std::filesystem::path>{root/"assets",staged.sources,staged.includes},
        "Release watcher roots differ from its selected shader compilation family.");
    require(debugRoots==std::vector<std::filesystem::path>{root/"assets",live.sources,live.includes},
        "Debug watcher roots differ from its selected shader compilation family.");
    require(selectShaderRuntimePaths(true,live,staged,false)==staged&&selectShaderRuntimePaths(false,live,staged,false)==staged,
        "Installed shader compilation failed to use its complete staged family.");
    const ShaderRuntimePaths missingPayload{root/"missing-payload"/"shaders",root/"missing-payload"/"include"};
    require(selectShaderRuntimePaths(false,live,missingPayload,true)==missingPayload,
        "Missing Release shader payload silently fell back to an incompatible checkout.");
    initializeContentStorage(root);
    require(shaderRuntimeWatchedRoots(root,release)==std::vector<std::filesystem::path>{root/"Content",root/"Engine",root/".crimson",staged.sources,staged.includes},
        "Content watcher omitted a physical asset/metadata root or changed the selected Release shader family.");
}
void embeddedBaselineChecks(const std::filesystem::path& root) {
    const auto first=root/"one",second=root/"relocated";
    constexpr std::string_view hash="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::array entries{
        ShaderSourceBaselineEntry{ShaderSourceRoot::Sources,"surface.sc",hash},
        ShaderSourceBaselineEntry{ShaderSourceRoot::Includes,"nested/common.sh",hash},
        ShaderSourceBaselineEntry{ShaderSourceRoot::Compiler,"",hash},
        ShaderSourceBaselineEntry{ShaderSourceRoot::Contract,"","crimson-shader-v2;bgfx-bin11;dx11;windows;s_5_0;O3"}};
    for(const auto& folder:{first,second}){
        write(folder/"sources/surface.sc","abc");write(folder/"includes/nested/common.sh","abc");write(folder/"compiler.exe","abc");
    }
    const ShaderCompileConfig a{first/"compiler.exe",first/"sources",first/"includes",first/"cache"},
        b{second/"compiler.exe",second/"sources",second/"includes",second/"cache"};
    require(shaderSourcesMatchEmbedded(a,entries)&&shaderSourcesMatchEmbedded(b,entries),"Identical embedded shaders reloaded after path relocation.");
    std::filesystem::last_write_time(b.sourceDirectory/"surface.sc",std::filesystem::file_time_type::clock::now()+std::chrono::seconds(10));
    require(shaderSourcesMatchEmbedded(b,entries),"Timestamp-only shader staging invalidated the embedded source baseline.");
    write(b.includeDirectory/"nested/common.sh","abd");
    require(!shaderSourcesMatchEmbedded(b,entries),"Changed shader include incorrectly reused embedded programs.");
    write(b.includeDirectory/"nested/common.sh","abc");write(b.compiler,"abd");
    require(!shaderSourcesMatchEmbedded(b,entries),"Changed shader compiler incorrectly reused embedded programs.");
    write(b.compiler,"abc");auto contract=b;contract.buildContract+=";different";
    require(!shaderSourcesMatchEmbedded(contract,entries),"Changed shader contract incorrectly reused embedded programs.");
    require(!shaderSourcesMatchEmbedded(b,{}),"Missing shader source baseline suppressed a necessary initial reload.");
}
void optimizationPolicyChecks(const ShaderCompileConfig& config) {
    auto material=defaultMaterialAsset();canonicalizeMaterialAsset(material);
    auto& graph=material.graph;const auto output=materialOutputNode(graph).id;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto color=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    const auto normal=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;
    connectMaterialNodes(graph,color,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,color,MaterialPin::A,output,MaterialPin::OpacityMask);
    connectMaterialNodes(graph,normal,MaterialPin::Normal,output,MaterialPin::Normal);
    const auto generated=generateMaterialGraphShader(material);
    for(const auto& variant:generated.variants) {
        const auto optimizedRecipe=materialGraphShaderRecipe(config,generated,variant);
        require(optimizedRecipe.optimization==ShaderCompileOptimization::Full,"Build shader recipes stopped selecting full optimization by default.");
        auto interactiveRecipe=optimizedRecipe;interactiveRecipe.optimization=ShaderCompileOptimization::Interactive;
        const auto vertex=compileShader(config,variant.vertexName);
        const auto optimized=compileShader(config,optimizedRecipe);
        require(!ShaderCacheReadSession(config).find(interactiveRecipe),"An optimized artifact was mistaken for an interactive artifact.");
        const auto interactive=compileShader(config,interactiveRecipe);
        require(optimized.fingerprint!=interactive.fingerprint,"Different shader optimization policies shared a persistent cache key.");
        std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,optimized.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Optimized generated material preflight: "+diagnostic);
        if(!preflightGeneratedShaderPair(vertex.bytes,interactive.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Interactive generated material preflight: "+diagnostic);
        const auto before=shaderCompilerMetrics();
        const auto repeatedOptimized=compileShader(config,optimizedRecipe),repeatedInteractive=compileShader(config,interactiveRecipe);
        require(shaderCompilerMetrics().compilerInvocations==before.compilerInvocations&&
            repeatedOptimized.bytes==optimized.bytes&&repeatedInteractive.bytes==interactive.bytes,
            "Repeated optimized/interactive shader requests did not reuse their separate binaries.");
        ShaderCacheReadSession cache(config);
        const auto cachedOptimized=cache.find(optimizedRecipe),cachedInteractive=cache.find(interactiveRecipe);
        require(cachedOptimized&&cachedInteractive&&cachedOptimized->fingerprint==optimized.fingerprint&&
            cachedInteractive->fingerprint==interactive.fingerprint&&cache.unchanged(),
            "Cache-only startup did not distinguish optimized and interactive generated stages.");
        cache.discard(std::span(&*cachedInteractive,1));
        require(ShaderCacheReadSession(config).find(optimizedRecipe).has_value()&&!ShaderCacheReadSession(config).find(interactiveRecipe),
            "Discarding an interactive stage removed its separate optimized artifact.");
    }
}
void nativeDomainChecks(const ShaderCompileConfig& config) {
    ShaderCompileRecipe recipe;
    recipe.name="native-domain-cache";recipe.stage=ShaderStage::Domain;recipe.raw=true;
    recipe.varyingDefinition=config.sourceDirectory/"model.varying.def.sc";recipe.contract="native-domain-test-v1";
    recipe.source=R"(
struct Control { float3 position : TEXCOORD0; };
struct Constants { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
[domain("tri")]
float4 main(Constants constants,float3 barycentric : SV_DomainLocation,const OutputPatch<Control,3> patch) : SV_Position {
    return float4(patch[0].position*barycentric.x+patch[1].position*barycentric.y+patch[2].position*barycentric.z,1.0);
}
)";
    const auto before=shaderCompilerMetrics();
    auto invalid=recipe;invalid.raw=false;
    bool rejected=false;try{(void)compileShader(config,invalid);}catch(const std::exception&){rejected=true;}
    require(rejected&&shaderCompilerMetrics().compilerInvocations==before.compilerInvocations,
        "A non-native domain recipe reached shaderc.");
    invalid=recipe;invalid.source="#include \"untracked.hlsl\"\n"+recipe.source;
    rejected=false;try{(void)compileShader(config,invalid);}catch(const std::exception&){rejected=true;}
    require(rejected&&shaderCompilerMetrics().compilerInvocations==before.compilerInvocations,
        "Raw generated HLSL accepted an uncertifiable include dependency.");
    std::stop_source canceled;canceled.request_stop();
    rejected=false;try{(void)compileShader(config,recipe,canceled.get_token());}catch(const std::exception&){rejected=true;}
    require(rejected&&shaderCompilerMetrics().compilerInvocations==before.compilerInvocations,
        "A canceled native domain request started a compiler process.");
    const auto binary=compileShader(config,recipe);
    require(binary.bytes.size()>16&&binary.bytes[0]=='D'&&binary.bytes[1]=='S'&&binary.bytes[2]=='H'&&binary.bytes[3]==11&&!binary.vertex,
        "Native SM5 domain recipe did not publish a domain binary.");
    ShaderCacheReadSession cache(config);const auto cached=cache.find(recipe);
    require(cached&&cached->bytes==binary.bytes&&cached->fingerprint==binary.fingerprint&&cache.unchanged(),
        "A raw domain artifact did not hydrate from its coherent persistent cache.");
    auto interactive=recipe;interactive.optimization=ShaderCompileOptimization::Interactive;
    require(!cache.find(interactive),"An optimized domain artifact was reused as interactive bytecode.");
    const auto quick=compileShader(config,interactive);
    require(quick.fingerprint!=binary.fingerprint&&ShaderCacheReadSession(config).find(interactive).has_value(),
        "Native domain optimization policies shared an identity or failed to persist.");
    auto changed=recipe;changed.source+="\n// A different generated height evaluator.\n";
    require(!ShaderCacheReadSession(config).find(changed),"Changed native domain source reused stale code.");
    const auto modified=compileShader(config,changed);
    require(modified.fingerprint!=binary.fingerprint,"Native domain source was absent from its cache identity.");
    const auto warmBefore=shaderCompilerMetrics();
    require(compileShader(config,recipe).bytes==binary.bytes&&compileShader(config,interactive).bytes==quick.bytes&&
        shaderCompilerMetrics().compilerInvocations==warmBefore.compilerInvocations,
        "Unchanged raw domain requests started compiler processes.");
    cache.discard(std::span{&*cached,1});
    require(!ShaderCacheReadSession(config).find(recipe)&&ShaderCacheReadSession(config).find(interactive).has_value(),
        "Discarding a rejected optimized domain artifact removed another optimization policy.");
    (void)compileShader(config,recipe);
    for(const auto& file:std::filesystem::directory_iterator(config.cacheDirectory))
        require(file.path().extension()!=".tmp"&&file.path().extension()!=".hlsl"&&file.path().extension()!=".d",
            "Native domain compilation left staging files behind.");
}
void run(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    Temp temp;
    runtimePathChecks(temp.root/"runtime selection");
    embeddedBaselineChecks(temp.root/"embedded baseline");
    nativeDomainChecks({compiler,source,include,temp.root/"native domain policies"});
    optimizationPolicyChecks({compiler,source,include,temp.root/"optimization policies"});
    const auto directory=temp.root/"shader source with spaces", cache=temp.root/"compiled cache";
    std::filesystem::create_directories(directory);
    const auto original=read(source/"fs_encode.sc");
    write(directory/"fs_encode.sc","#define CRIMSON_RELOAD_HEADER \"reload-extra.sc\"\n#include CRIMSON_RELOAD_HEADER\n"+original);
    write(directory/"reload-extra.sc","#define CRIMSON_TEST_VALUE 1\n");
    std::filesystem::copy_file(source/"varying.def.sc",directory/"varying.def.sc");
    std::filesystem::copy_file(source/"debanding.sh",directory/"debanding.sh");
    const ShaderCompileConfig config{compiler,directory,include,cache};
    const auto first=compileShader(config,"fs_encode");
    require(first.bytes.size()>16 && first.bytes[0]=='F' && first.bytes[1]=='S' && first.bytes[2]=='H' && first.bytes[3]==11,"Pinned shader compilation produced the wrong envelope.");
    require(std::any_of(first.dependencies.begin(),first.dependencies.end(),[](const auto& file){return file.filename()=="reload-extra.sc";}),"Compiler-reported macro include was lost.");
    require(std::any_of(first.dependencies.begin(),first.dependencies.end(),[](const auto& file){return file.filename()=="debanding.sh";}),"Debanding was absent from runtime shader dependencies.");
    const auto discovered=shaderDependencies(config,"fs_encode");
    require(std::any_of(discovered.begin(),discovered.end(),[](const auto& file){return file.filename()=="reload-extra.sc";}),"Actual dependency index was not reused.");
    const auto warm=compileShader(config,"fs_encode");
    require(warm.bytes==first.bytes && warm.fingerprint==first.fingerprint,"Warm shader cache changed code or fingerprint.");
    {
        ShaderCacheReadSession reopened(config);
        const auto hit=reopened.find("fs_encode");
        require(hit&&hit->bytes==first.bytes&&hit->fingerprint==first.fingerprint&&reopened.unchanged(),
            "Cache-only startup lookup did not hydrate an unchanged engine shader.");
        auto changedContract=config;changedContract.buildContract+=";startup-contract-change";
        require(!ShaderCacheReadSession(changedContract).find("fs_encode"),"Cache-only lookup ignored the build contract.");
        require(!reopened.find("fs_grid"),"Cache-only miss compiled an unavailable shader.");
        write(directory/"reload-extra.sc","#define CRIMSON_TEST_VALUE 3\n");
        require(!reopened.unchanged(),"A dependency edit between cached passes did not reject the startup bundle.");
        write(directory/"reload-extra.sc","#define CRIMSON_TEST_VALUE 1\n");
    }
    {
        // The frame thread can read this dependency index while a worker
        // recreates a missing binary. Identical disposable metadata is already
        // published and must not require delete-sharing from its reader.
        const auto index=cache/"fs_encode.dependencies.json",binary=cache/("fs_encode-"+std::to_string(first.fingerprint)+".bin");
        const auto timestamp=std::filesystem::last_write_time(index);std::ifstream reader(index,std::ios::binary);
        require(static_cast<bool>(reader)&&std::filesystem::remove(binary),"Cannot prepare held shader index regression.");
        const auto rebuilt=compileShader(config,"fs_encode");
        require(rebuilt.bytes==first.bytes&&std::filesystem::last_write_time(index)==timestamp,"A matching active shader dependency index was replaced.");
    }
    {
        // Unused conditional inputs are a lexical superset, not a reason to
        // run shaderc twice. Macro-only newly discovered inputs still require
        // the second before/after-validated compile.
        write(directory/"inactive.sh","#define CRIMSON_INACTIVE_VALUE 1\n");
        const ShaderCompileRecipe conditional{"conditional_recipe","#if 0\n#include \"inactive.sh\"\n#endif\n"+original,
            ShaderStage::Fragment,directory/"varying.def.sc",{},"conditional-v1"};
        const auto before=shaderCompilerMetrics();const auto initial=compileShader(config,conditional);
        require(shaderCompilerMetrics().compilerInvocations-before.compilerInvocations==1,
            "A generated shader with only inactive lexical dependencies was compiled more than once.");
        require(std::none_of(initial.dependencies.begin(),initial.dependencies.end(),[](const auto& file){return file.filename()=="inactive.sh";}),
            "Inactive lexical input leaked into the actual persistent dependency graph.");
        write(directory/"inactive.sh","#define CRIMSON_INACTIVE_VALUE 2\n");
        const auto warmBefore=shaderCompilerMetrics();const auto warmConditional=compileShader(config,conditional);
        require(shaderCompilerMetrics().compilerInvocations==warmBefore.compilerInvocations&&warmConditional.fingerprint==initial.fingerprint,
            "Editing an inactive include invalidated the actual generated shader cache.");
        write(directory/"generated-macro.sh","#define CRIMSON_GENERATED_MACRO 1\n");
        const ShaderCompileRecipe macro{"macro_recipe","#define CRIMSON_GENERATED_INCLUDE \"generated-macro.sh\"\n#include CRIMSON_GENERATED_INCLUDE\n"+original,
            ShaderStage::Fragment,directory/"varying.def.sc",{},"macro-v1"};
        const auto macroBefore=shaderCompilerMetrics();const auto generated=compileShader(config,macro);
        require(shaderCompilerMetrics().compilerInvocations-macroBefore.compilerInvocations==2&&
            std::any_of(generated.dependencies.begin(),generated.dependencies.end(),[](const auto& file){return file.filename()=="generated-macro.sh";}),
            "A newly discovered generated macro include lost its validation compile or dependency.");
    }
    {
        const ShaderCompileRecipe recipe{"held_recipe",original,ShaderStage::Fragment,directory/"varying.def.sc",{},"held-index-v1"};
        const auto initial=compileShader(config,recipe);std::filesystem::path index;
        for(const auto& entry:std::filesystem::directory_iterator(cache)) {
            const auto name=entry.path().filename().string();if(name.starts_with("held_recipe-")&&name.ends_with(".recipe.json"))index=entry.path();
        }
        require(!index.empty(),"Generated shader did not publish its recipe index.");
        const auto warmLookup=ShaderCacheReadSession(config).find(recipe);
        require(warmLookup&&warmLookup->bytes==initial.bytes&&warmLookup->fingerprint==initial.fingerprint,
            "Cache-only startup lookup did not hydrate an unchanged generated shader.");
        auto different=recipe;different.contract+=";different-graph-contract";
        require(!ShaderCacheReadSession(config).find(different),"Cache-only lookup ignored the generated recipe contract.");
        const auto timestamp=std::filesystem::last_write_time(index);std::ifstream reader(index,std::ios::binary);
        auto identity=index.filename().string();identity.resize(identity.size()-std::string(".recipe.json").size());
        require(static_cast<bool>(reader)&&std::filesystem::remove(cache/(identity+"-"+std::to_string(initial.fingerprint)+".bin")),"Cannot prepare held recipe index regression.");
        const auto rebuilt=compileShader(config,recipe);
        require(rebuilt.bytes==initial.bytes&&std::filesystem::last_write_time(index)==timestamp,"A matching active generated recipe index was replaced.");
        reader.close();
        const auto binary=cache/(identity+"-"+std::to_string(initial.fingerprint)+".bin");
        write(binary,"damaged cache");
        require(!ShaderCacheReadSession(config).find(recipe),"Cache-only hydration accepted a damaged shader envelope.");
        require(compileShader(config,recipe).bytes==initial.bytes,"A damaged derived shader cache did not rebuild coherently.");
        ShaderCacheReadSession invalidation(config);const auto artifact=invalidation.find(recipe);
        require(artifact&&invalidation.unchanged(),"Could not load the cached artifact for explicit preflight recovery.");
        invalidation.discard(std::span(&*artifact,1));
        require(!ShaderCacheReadSession(config).find(recipe)&&std::filesystem::is_regular_file(directory/"fs_encode.sc"),
            "Preflight recovery kept its rejected cache artifact or deleted a source file.");
        require(compileShader(config,recipe).bytes==initial.bytes,"A discarded preflight candidate did not rebuild coherently.");
    }
    write(directory/"reload-extra.sc","#define CRIMSON_TEST_VALUE 2\n");
    require(!ShaderCacheReadSession(config).find("fs_encode"),"Cache-only lookup ignored a changed macro include.");
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
    for(const auto name:{"cs_ssr_color","cs_ssr_depth","fs_model_reflection","fs_pbr_reflection","fs_particle_reflection"})
        require(std::find(names.begin(),names.end(),name)!=names.end(),"SSR programs are absent from atomic reload discovery.");
    require(std::find(names.begin(),names.end(),"vs_point_shadow")!=names.end() &&
        std::find(names.begin(),names.end(),"fs_point_shadow")!=names.end(),"Shadow stages are absent from atomic reload discovery.");
    const auto vertex=compileShader(shadows,"vs_point_shadow"),fragment=compileShader(shadows,"fs_point_shadow");
    std::string diagnostic;
    require(preflightShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,fragment.bytes,diagnostic),"Valid caster contract failed preflight.");
    require(std::find(names.begin(),names.end(),"vs_directional_shadow")!=names.end()&&std::find(names.begin(),names.end(),"fs_directional_shadow")!=names.end(),"Directional stages are absent from reload discovery.");
    const auto directionalVertex=compileShader(shadows,"vs_directional_shadow"),directionalFragment=compileShader(shadows,"fs_directional_shadow");
    require(preflightShaderPair(directionalVertex.bytes,directionalFragment.bytes,directionalVertex.bytes,directionalFragment.bytes,diagnostic),"Valid directional caster contract failed preflight.");
    require(!preflightShaderPair(vertex.bytes,fragment.bytes,directionalVertex.bytes,directionalFragment.bytes,diagnostic),"Point caster replaced grouped directional projection uniforms.");
    auto caster=read(shadowSource/"fs_point_shadow.sc");const auto sampler=caster.find("SAMPLER2D(s_albedo, 0)");
    require(sampler!=std::string::npos,"Caster fixture cannot find alpha sampler zero.");
    caster.replace(sampler,std::string("SAMPLER2D(s_albedo, 0)").size(),"SAMPLER2D(s_albedo, 1)");
    write(shadowSource/"fs_point_shadow.sc",caster);const auto moved=compileShader(shadows,"fs_point_shadow");
    require(!preflightShaderPair(vertex.bytes,moved.bytes,vertex.bytes,fragment.bytes,diagnostic),"Moving caster alpha sampler to slot one was accepted.");
    const auto disguised=samplerEnvelopeSlot(moved.bytes,"s_albedo",0);
    require(!preflightShaderPair(vertex.bytes,disguised,vertex.bytes,fragment.bytes,diagnostic),"DXBC sampler relocation escaped validation when its bgfx metadata retained slot zero.");
    const auto modelVertex=compileShader(shadows,"vs_model"),modelFragment=compileShader(shadows,"fs_model");
    const auto dependency=[](const auto& compiled,const char* name){return std::any_of(compiled.dependencies.begin(),compiled.dependencies.end(),[&](const auto& path){return path.filename()==name;});};
    require(dependency(modelFragment,"material_mapping.sh") && dependency(fragment,"material_mapping.sh"),"Shared material mapping was absent from color/caster reload dependencies.");
    require(dependency(modelFragment,"model_material_normal.sh"),"Shared model normal evaluation was absent from color reload dependencies.");
    require(dependency(modelFragment,"directional_shadow.sh"),"Directional receiver changes do not invalidate shader reload.");
    require(dependency(modelFragment,"point_light_record.sh"),"Point record layout changes do not invalidate fragment shader reload.");
    {
        // V6 gives the former reserved record component GI-energy semantics.
        // Reject V5 programs in either publication direction even though the
        // six-texel stride itself is unchanged.
        const auto clusteredSource=read(shadowSource/"clustered_lighting.sh");
        const auto assignmentSource=read(shadowSource/"cs_cluster_lights.sc");
        const auto assignment=compileShader(shadows,"cs_cluster_lights");
        require(dependency(assignment,"point_light_record.sh"),"Point record layout changes do not invalidate compute shader reload.");
        require(preflightComputeShader(assignment.bytes,assignment.bytes,diagnostic),"Current clustered record layout failed its compute contract.");
        const auto priorLayout=[](std::string text) {
            const std::string declaration="uniform vec4 u_clusterRecordLayoutV6;";
            const auto at=text.find(declaration);
            require(at!=std::string::npos,"Clustered ABI fixture has no reflected layout guard.");
            const std::string stride="int(u_clusterRecordLayoutV6.x)";
            require(text.find(stride)!=std::string::npos,"Clustered layout guard is declared but does not affect record indexing.");
            const std::string current="u_clusterRecordLayoutV6",previous="u_clusterRecordLayoutV5";
            for(std::size_t offset=0;(offset=text.find(current,offset))!=std::string::npos;offset+=previous.size()) {
                text.replace(offset,current.size(),previous);
            }
            return text;
        };
        write(shadowSource/"clustered_lighting.sh",priorLayout(clusteredSource));
        write(shadowSource/"cs_cluster_lights.sc",priorLayout(assignmentSource));
        const auto priorFragment=compileShader(shadows,"fs_model");
        const auto priorAssignment=compileShader(shadows,"cs_cluster_lights");
        require(!preflightShaderPair(modelVertex.bytes,modelFragment.bytes,modelVertex.bytes,priorFragment.bytes,diagnostic),
            "A previous engine contract accepted a live shader with the new point record layout.");
        require(!preflightComputeShader(assignment.bytes,priorAssignment.bytes,diagnostic),
            "A previous engine compute contract accepted the new point record layout.");
        require(!preflightShaderPair(modelVertex.bytes,priorFragment.bytes,modelVertex.bytes,modelFragment.bytes,diagnostic)&&
            !preflightComputeShader(priorAssignment.bytes,assignment.bytes,diagnostic),
            "A rebuilt engine accepted shaders without its required record layout binding.");
        write(shadowSource/"clustered_lighting.sh",clusteredSource);
        write(shadowSource/"cs_cluster_lights.sc",assignmentSource);
    }
    const auto pickFragment=compileShader(shadows,"fs_pick"),particleFragment=compileShader(shadows,"fs_particle"),presentFragment=compileShader(shadows,"fs_present");
    require(dependency(pickFragment,"material_mapping.sh") && dependency(particleFragment,"material_mapping.sh"),"Shared material mapping was absent from picking/sprite reload dependencies.");
    require(dependency(presentFragment,"tone_mapping.sh"),"Tone mapping include was absent from runtime reload dependencies.");
    {
        constexpr std::array ssaoNames{"cs_ssao_evaluate","cs_ssao_publish"};
        std::array<ShaderBinary,ssaoNames.size()> ssao;
        for(std::size_t i=0;i<ssaoNames.size();++i) {
            require(std::find(names.begin(),names.end(),ssaoNames[i])!=names.end(),
                "A SSAO compute stage is absent from atomic shader discovery.");
            ssao[i]=compileShader(shadows,ssaoNames[i]);
            require(ssao[i].bytes.size()>16&&ssao[i].bytes[0]=='C'&&!ssao[i].vertex,
                "Runtime compiler did not compile a SSAO stage as compute.");
            require(preflightComputeShader(ssao[i].bytes,ssao[i].bytes,diagnostic),
                "Valid SSAO compute stage failed runtime preflight.");
            require(dependency(ssao[i],"ssao_compute.sh"),
                "Shared SSAO traversal was absent from runtime reload dependencies.");
        }
        const auto warmBefore=shaderCompilerMetrics();
        ShaderCacheReadSession ssaoCache(shadows);
        for(std::size_t i=0;i<ssaoNames.size();++i) {
            const auto warm=compileShader(shadows,ssaoNames[i]);
            const auto cached=ssaoCache.find(ssaoNames[i]);
            require(warm.bytes==ssao[i].bytes&&warm.fingerprint==ssao[i].fingerprint&&
                cached&&cached->bytes==ssao[i].bytes&&cached->fingerprint==ssao[i].fingerprint,
                "Warm SSAO cache changed code or failed cache-only hydration.");
        }
        require(shaderCompilerMetrics().compilerInvocations==warmBefore.compilerInvocations&&ssaoCache.unchanged(),
            "Unchanged SSAO shaders invoked the runtime compiler or lost cache coherence.");
        const auto ssaoShared=read(shadowSource/"ssao_compute.sh");
        write(shadowSource/"ssao_compute.sh",ssaoShared+"\n// Disposable shared SSAO dependency change.\n");
        require(!ssaoCache.unchanged(),"Shared SSAO edits left an earlier cache snapshot coherent.");
        ShaderCacheReadSession changedSsaoCache(shadows);
        for(std::size_t i=0;i<ssaoNames.size();++i) {
            require(!changedSsaoCache.find(ssaoNames[i]),"Shared SSAO edit reused a stale cache-only artifact.");
            const auto changedSsao=compileShader(shadows,ssaoNames[i]);
            require(changedSsao.fingerprint!=ssao[i].fingerprint&&
                preflightComputeShader(changedSsao.bytes,ssao[i].bytes,diagnostic),
                "Shared SSAO edit did not invalidate every stage while preserving its compute contract.");
        }
        write(shadowSource/"ssao_compute.sh",ssaoShared);
        const auto originalPublish=read(shadowSource/"cs_ssao_publish.sc");
        auto publishSource=originalPublish;
        const std::string ssaoOutput="IMAGE2D_WO(s_ssaoAtlasOutput,rgba32f,3)";
        const auto ssaoSlot=publishSource.find(ssaoOutput);
        require(ssaoSlot!=std::string::npos,"SSAO publication fixture has no expected image slot.");
        publishSource.replace(ssaoSlot,ssaoOutput.size(),"IMAGE2D_WO(s_ssaoAtlasOutput,rgba32f,4)");
        write(shadowSource/"cs_ssao_publish.sc",publishSource);
        const auto wrongSsaoSlot=compileShader(shadows,"cs_ssao_publish");
        require(!preflightComputeShader(wrongSsaoSlot.bytes,ssao.back().bytes,diagnostic),
            "Changed SSAO publication UAV slot escaped atomic reload validation.");
        write(shadowSource/"cs_ssao_publish.sc",originalPublish);
    }
    const auto glowFragment=compileShader(shadows,"cs_glow_downsample");
    require(dependency(glowFragment,"glow_compute.sh"),"Glow kernel was absent from runtime reload dependencies.");
    const auto warmGlow=compileShader(shadows,"cs_glow_downsample");
    require(warmGlow.bytes==glowFragment.bytes && warmGlow.fingerprint==glowFragment.fingerprint,
        "Warm Glow shader cache changed code or fingerprint.");
    const std::string originalName="u_materialUvOffset",replacementName="u_rejectedMaterialOffset";
    // Rename declaration and consumers so compilation succeeds and this tests
    // renderer uniform-contract rejection, rather than undefined shader syntax.
    // Follow the compiled dependency closure: normal evaluation is shared with
    // the reflection pass, and further helper extraction must not leave a
    // consumer referring to the intentionally renamed fixture declaration.
    std::vector<std::pair<std::filesystem::path,std::string>> originalMappingSources;
    for(const auto& path:modelFragment.dependencies) {
        const auto relative=std::filesystem::relative(path,shadowSource);
        if(relative.empty()||relative.is_absolute()||*relative.begin()==".."||(path.extension()!=".sc"&&path.extension()!=".sh"))continue;
        const auto original=read(path);auto renamed=original;
        if(renamed.find(originalName)==std::string::npos)continue;
        for(std::size_t pos=0;(pos=renamed.find(originalName,pos))!=std::string::npos;pos+=replacementName.size())renamed.replace(pos,originalName.size(),replacementName);
        originalMappingSources.emplace_back(path,original);write(path,renamed);
    }
    require(std::any_of(originalMappingSources.begin(),originalMappingSources.end(),[](const auto& item){return item.first.filename()=="material_mapping.sh";}),
        "Mapping uniform rejection fixture did not rename its declaration.");
    const auto wrongMapping=compileShader(shadows,"fs_model");
    require(!preflightShaderPair(modelVertex.bytes,wrongMapping.bytes,modelVertex.bytes,modelFragment.bytes,diagnostic),"Changing material mapping uniforms escaped renderer contract validation.");
    for(const auto& [path,original]:originalMappingSources)write(path,original);
    auto receiver=read(shadowSource/"point_shadow.sh");const auto receiverSampler=receiver.find("SAMPLER2DSHADOW(s_pointShadow, 8)");
    require(receiverSampler!=std::string::npos,"Receiver fixture cannot find shadow sampler eight.");
    receiver.replace(receiverSampler,std::string("SAMPLER2DSHADOW(s_pointShadow, 8)").size(),"SAMPLER2DSHADOW(s_pointShadow, 9)");
    write(shadowSource/"point_shadow.sh",receiver);const auto wrongReceiver=compileShader(shadows,"fs_model");
    require(!preflightShaderPair(modelVertex.bytes,wrongReceiver.bytes,modelVertex.bytes,modelFragment.bytes,diagnostic),"Moving shadow comparison sampler to slot nine was accepted.");
    require(std::any_of(modelFragment.dependencies.begin(),modelFragment.dependencies.end(),[](const auto& path){return path.filename()=="point_shadow.sh";}),"Shadow receiver include was not tracked for reload.");

    require(std::find(names.begin(),names.end(),"cs_model_deform")!=names.end(),"Model deformation compute is absent from atomic shader discovery.");
    const auto deformation=compileShader(shadows,"cs_model_deform");
    require(deformation.bytes.size()>16&&deformation.bytes[0]=='C'&&!deformation.vertex,"Runtime compiler did not compile model deformation as compute.");
    require(preflightComputeShader(deformation.bytes,deformation.bytes,diagnostic),"Valid model deformation compute failed preflight.");
    auto deformationSource=read(shadowSource/"cs_model_deform.sc");const auto originalDeformation=deformationSource;
    const auto deformationBinding=deformationSource.find("u_modelDeformation");require(deformationBinding!=std::string::npos,"Deformation fixture has no binding contract.");
    for(std::size_t offset=0;(offset=deformationSource.find("u_modelDeformation",offset))!=std::string::npos;offset+=20)
        deformationSource.replace(offset,std::string("u_modelDeformation").size(),"u_changedDeformation");
    write(shadowSource/"cs_model_deform.sc",deformationSource);const auto wrongDeformation=compileShader(shadows,"cs_model_deform");
    require(!preflightComputeShader(wrongDeformation.bytes,deformation.bytes,diagnostic),"Changed deformation binding escaped compute contract validation.");
    write(shadowSource/"cs_model_deform.sc",originalDeformation);
    require(std::find(names.begin(),names.end(),"cs_model_smooth_normals")!=names.end(),"Smooth normal compute is absent from atomic shader discovery.");
    const auto smoothNormals=compileShader(shadows,"cs_model_smooth_normals");
    require(smoothNormals.bytes.size()>16&&smoothNormals.bytes[0]=='C'&&!smoothNormals.vertex,"Runtime compiler did not compile smooth normals as compute.");
    require(preflightComputeShader(smoothNormals.bytes,smoothNormals.bytes,diagnostic),"Valid smooth normal compute failed preflight.");
    require(!preflightComputeShader(smoothNormals.bytes,deformation.bytes,diagnostic),"Smooth normals replaced the independent deformation compute contract.");
    require(std::find(names.begin(),names.end(),"cs_procedural_filter")!=names.end(),"Procedural compute is absent from atomic shader discovery.");
    const auto compute=compileShader(shadows,"cs_procedural_filter");
    require(compute.bytes.size()>16 && compute.bytes[0]=='C' && !compute.vertex,"Runtime shader compiler did not compile the compute stage.");
    require(preflightComputeShader(compute.bytes,compute.bytes,diagnostic),"Valid procedural compute contract failed preflight.");
    require(dependency(compute,"procedural_sky.sh"),"Procedural sky evaluator was absent from compute dependencies.");
    const auto skyFragment=compileShader(shadows,"fs_sky");
    require(dependency(skyFragment,"procedural_sky.sh"),"Visible sky did not share the compute evaluator dependency.");
    auto computeSource=read(shadowSource/"cs_procedural_filter.sc");const auto originalComputeSource=computeSource;
    const std::string group="NUM_THREADS(8, 8, 1)";
    const auto groupPosition=computeSource.find(group);require(groupPosition!=std::string::npos,"Compute fixture has no expected thread group.");
    computeSource.replace(groupPosition,group.size(),"NUM_THREADS(4, 8, 1)");
    write(shadowSource/"cs_procedural_filter.sc",computeSource);
    const auto wrongGroup=compileShader(shadows,"cs_procedural_filter");
    require(!preflightComputeShader(wrongGroup.bytes,compute.bytes,diagnostic),"Changed dispatch thread-group dimensions were accepted.");
    computeSource=originalComputeSource;
    const std::string image="IMAGE2D_ARRAY_WO(s_proceduralOutput, rgba16f, 0)";
    const auto imagePosition=computeSource.find(image);require(imagePosition!=std::string::npos,"Compute fixture has no expected output image.");
    computeSource.replace(imagePosition,image.size(),"IMAGE2D_ARRAY_WO(s_proceduralOutput, rgba16f, 1)");
    write(shadowSource/"cs_procedural_filter.sc",computeSource);
    const auto wrongImage=compileShader(shadows,"cs_procedural_filter");
    // Pinned shaderc removes RDEF and does not include image slots in its bgfx
    // uniform envelope. The executable dcl_uav declaration must still be checked.
    require(wrongImage.bytes!=compute.bytes,"Moving the compute UAV did not change the executable fixture.");
    require(!preflightComputeShader(wrongImage.bytes,compute.bytes,diagnostic),"Changed compute output image slot was accepted.");
    write(shadowSource/"cs_procedural_filter.sc",originalComputeSource);
    auto skyShared=read(shadowSource/"procedural_sky.sh");
    const std::string skyUniform="u_proceduralSky",wrongSkyUniform="u_wrongProceduralSky";
    for (std::size_t pos=0;(pos=skyShared.find(skyUniform,pos))!=std::string::npos;pos+=wrongSkyUniform.size())
        skyShared.replace(pos,skyUniform.size(),wrongSkyUniform);
    write(shadowSource/"procedural_sky.sh",skyShared);
    const auto wrongSky=compileShader(shadows,"cs_procedural_filter");
    require(wrongSky.fingerprint!=compute.fingerprint && !preflightComputeShader(wrongSky.bytes,compute.bytes,diagnostic),
        "Changed shared sky uniforms did not invalidate the cache and fail compute preflight.");
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=4) throw std::runtime_error("Pass compiler, engine source directory, and pinned include directory.");
        run(pathFromUtf8(argv[1]),pathFromUtf8(argv[2]),pathFromUtf8(argv[3]));
        std::cout<<"Shader compiler checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
