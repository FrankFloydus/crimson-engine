#include "assets/MaterialAsset.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "assets/AtomicFile.h"
#include <crimson/renderer/forward/MaterialBlendMath.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct HeightBlendFixture {
    MaterialAsset material,source;
    std::array<MaterialNodeId,4> references{};
    HeightBlendFixture() {
        source.id="height-blend-source";source.name="Height source";
        const auto output=addMaterialNode(source.graph,MaterialNodeKind::Output).id;
        // Ordinary Lit authoring exposes Height while tessellation is enabled.
        // Retain that authored link after disabling displacement so Blend can
        // inherit it without changing the existing output activation contract.
        materialOutputNode(source.graph).output.tessellation.enabled=true;
        for(unsigned channel=0;channel<3;++channel) {
            const auto image=addMaterialNode(source.graph,MaterialNodeKind::Texture2D).id;
            auto& node=*findMaterialNode(source.graph,image);
            node.texture.image.id=channel==0?"assets/textures/blend-color.png":channel==1?"assets/textures/blend-normal.png":"assets/textures/blend-height.png";
            node.texture.role=channel==0?MaterialTextureRole::Color:channel==1?MaterialTextureRole::Normal:MaterialTextureRole::Data;
            connectMaterialNodes(source.graph,image,channel==0?MaterialPin::RGB:channel==1?MaterialPin::Normal:MaterialPin::R,
                output,channel==0?MaterialPin::BaseColor:channel==1?MaterialPin::Normal:MaterialPin::Height);
        }
        materialOutputNode(source.graph).output.tessellation.enabled=false;
        material.id="height-blend-parent";material.name="Height Blend";
        const auto root=addMaterialNode(material.graph,MaterialNodeKind::Output).id;
        materialOutputNode(material.graph).output.shading=MaterialShading::BlendPbr;
        const std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
        for(unsigned i=0;i<4;++i) {
            references[i]=addMaterialNode(material.graph,MaterialNodeKind::MaterialReference).id;
            findMaterialNode(material.graph,references[i])->material.id=source.id;
            connectMaterialNodes(material.graph,references[i],MaterialPin::Surface,root,pins[i]);
        }
    }
    MaterialGraphSourceSet snapshots()const{return {std::make_shared<const MaterialAsset>(source)};}
};
void heightBlendMathChecks() {
    const auto close=[](float a,float b){return std::abs(a-b)<2e-6f;};
    check(material_blend_height_alpha(0,-16,16,16,0)==0&&material_blend_height_alpha(1,16,-16,16,0)==1,
        "Height blending leaked into zero paint or prevented full painted coverage.");
    for(float alpha:{.001f,.1f,.25f,.5f,.9f,.999f}) {
        check(material_blend_height_alpha(alpha,4,-3,0,.001f)==alpha&&close(material_blend_height_alpha(alpha,.3f,.3f,1,.2f),alpha),
            "Zero height influence or equal heights changed the ordinary paint alpha.");
        check(material_blend_height_alpha(alpha,-.3f,0,1,.2f)>alpha&&material_blend_height_alpha(alpha,.3f,0,1,.2f)<alpha,
            "Height did not reveal a coating sooner in recesses and later on raised areas.");
    }
    for(float underlying:{-16.f,-.5f,0.f,.5f,16.f})for(float layer:{-16.f,0.f,16.f})
        for(float influence:{0.f,1.f,16.f})for(float softness:{0.f,.001f,.2f,1.f}) {
            float previous=-1;
            for(unsigned step=0;step<=100;++step) {
                const auto alpha=material_blend_height_alpha(step*.01f,underlying,layer,influence,softness);
                check(std::isfinite(alpha)&&alpha>=0&&alpha<=1&&alpha>=previous,"Height alpha became nonfinite, escaped its range, or reversed paint coverage.");
                previous=alpha;
            }
        }
    // The ordered composition must retain the former normalized weighted sum
    // when height and per-property overrides are neutral, including RGB sums>1.
    constexpr std::array<float,4> values{.17f,.63f,-.4f,1.7f};
    for(unsigned r=0;r<=4;++r)for(unsigned g=0;g<=4;++g)for(unsigned b=0;b<=4;++b) {
        std::array<float,4> weights{std::max(0.f,1.f-(r+g+b)*.25f),r*.25f,g*.25f,b*.25f};
        float total{};for(auto weight:weights)total+=weight;for(auto& weight:weights)weight/=total;
        float flat{},accumulated=weights[0],composed=values[0];
        for(unsigned i=0;i<4;++i)flat+=values[i]*weights[i];
        for(unsigned i=1;i<4;++i){const auto alpha=weights[i]/std::max(accumulated+weights[i],1e-8f);composed+=(values[i]-composed)*alpha;accumulated+=weights[i];}
        check(close(flat,composed),"Neutral layer controls changed the existing normalized painting result.");
    }
}
void heightBlendShaderChecks() {
    HeightBlendFixture fixture;
    const auto flat=generateMaterialGraphShader(fixture.material,fixture.snapshots());
    check(flat.textures.size()==2&&flat.meshPaint&&!flat.tessellation.enabled&&!flat.dynamicHeight,
        "Flat Blend demanded an inactive Height texture or changed paint/geometry behavior.");
    check(flat.variants.front().source.find("ccGraphBlendHeightAlpha")==std::string::npos,"Disabled height blending retained active shader work.");
    auto& layer=findMaterialNode(fixture.material.graph,fixture.references[1])->blendLayer;layer.heightBlend=true;
    const auto height=generateMaterialGraphShader(fixture.material,fixture.snapshots());
    check(height.textures.size()==3&&!height.tessellation.enabled&&!height.dynamicHeight&&!height.dynamicCoverage,
        "Height-aware appearance changed displacement/coverage or duplicated a shared Height sample.");
    check(height.variants.front().source.find(MaterialBlendHeightShaderFunction)!=std::string::npos&&
        height.variants.front().source.find("underlyingHeight = mix(")!=std::string::npos,
        "Height Blend omitted its bounded transition or accumulated underlying height.");
    check(std::count_if(height.textures.begin(),height.textures.end(),[](const auto& binding){return binding.image.id=="assets/textures/blend-height.png"&&binding.role==MaterialGraphTextureRole::Data;})==1,
        "Inherited height lost its linear data role or repeated-reference sharing.");
    for(const auto& variant:height.variants)if(variant.pass==MaterialGraphPass::Picking||variant.pass==MaterialGraphPass::PointShadow||variant.pass==MaterialGraphPass::DirectionalShadow)
        check(variant.source.find("ccGraphBlendHeightAlpha")==std::string::npos&&variant.source.find("underlyingHeight")==std::string::npos,
            "Surface height blending leaked into opaque depth/picking/shadow passes.");
    layer.heightInfluence=.3f;layer.blendSoftness=.07f;layer.heightScale=2;layer.heightMidpoint=.4f;
    layer.normalInfluence=0;layer.roughnessInfluence=.6f;layer.colorInfluence=.8f;layer.metallicInfluence=.2f;layer.occlusionInfluence=.4f;
    layer.tint={.7f,.8f,.9f};layer.roughnessScale=.75f;
    const auto values=generateMaterialGraphShader(fixture.material,fixture.snapshots());
    check(height.topologyKey==values.topologyKey&&height.uniforms!=values.uniforms,"A numeric layer edit compiled a new shader instead of updating uniforms.");
    layer.mappingOverride=true;layer.mapping.mode=MaterialUvMode::LocalTriplanar;layer.mapping.scale={2,3,4};
    const auto mapped=generateMaterialGraphShader(fixture.material,fixture.snapshots());
    layer.mapping.scale={5,6,7};layer.mapping.offset={.1f,.2f,.3f};layer.mapping.rotation={0,45,0};
    const auto moved=generateMaterialGraphShader(fixture.material,fixture.snapshots());
    check(mapped.topologyKey==moved.topologyKey&&mapped.uniforms!=moved.uniforms,"Layer placement values changed shader topology.");
    check(std::any_of(moved.textures.begin(),moved.textures.end(),[&](const auto& binding){return moved.uniforms[binding.mappingRow][0]==5&&moved.uniforms[binding.mappingRow][1]==6;}),
        "Layer placement override did not reach generated sample mappings.");
    const auto preview=generateMaterialGraphPreviewShader(fixture.material,{fixture.references[1],MaterialPin::Surface},fixture.snapshots());
    check(preview.variants.size()==1&&!preview.meshPaint,"Layer appearance preview acquired scene paint ownership.");
}
void heightBlendCompileChecks(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    const auto root=std::filesystem::temp_directory_path()/("crimson-height-blend-shaders-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;const auto resolved=std::filesystem::weakly_canonical(path,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&resolved.parent_path()==temp&&resolved.filename().string().starts_with("crimson-height-blend-shaders-"))std::filesystem::remove_all(resolved,error);}} cleanup{root};
    const ShaderCompileConfig config{compiler,source,include,root};
    const auto compile=[&](const MaterialGraphShader& graph,bool onlySurface=false){
        for(const auto& variant:graph.variants) {
            if(onlySurface&&variant.pass!=MaterialGraphPass::Model)continue;
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,graph,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))throw std::runtime_error("Height Blend: "+diagnostic);
        }
    };
    HeightBlendFixture fixture;compile(generateMaterialGraphShader(fixture.material,fixture.snapshots()),true);
    for(unsigned i=1;i<4;++i){auto& layer=findMaterialNode(fixture.material.graph,fixture.references[i])->blendLayer;layer.heightBlend=true;layer.normalInfluence=.4f;layer.roughnessInfluence=.7f;}
    auto& mapping=findMaterialNode(fixture.material.graph,fixture.references[1])->blendLayer;
    mapping.mappingOverride=true;mapping.mapping.mode=MaterialUvMode::LocalTriplanar;mapping.mapping.scale={2,3,4};
    compile(generateMaterialGraphShader(fixture.material,fixture.snapshots()));
    compile(generateMaterialGraphPreviewShader(fixture.material,{fixture.references[1],MaterialPin::Surface},fixture.snapshots()));
    std::cout<<"Height Blend compiled flat/height surfaces, particle, reflection, depth/shadow/picking and a layer preview with DXBC interface preflight.\n";
}
void constantRoughnessEligibilityChecks() {
    MaterialAsset material;auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id;
    const auto generated=[&]{return generateMaterialGraphShader(material);};
    auto first=generated();check(first.constantRoughness==1.f,"Unlinked roughness did not retain its exact parameter proof.");
    materialOutputNode(graph).output.roughness=.2f;auto edited=generated();
    check(first.topologyKey==edited.topologyKey&&edited.constantRoughness==.2f&&first.constantRoughness==1.f,
        "A value-only roughness edit retained stale eligibility or mutated the previous shader packet.");
    const auto scalar=addMaterialNode(graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(graph,scalar)->scalar=.8f;connectMaterialNodes(graph,scalar,MaterialPin::Value,output,MaterialPin::Roughness);
    first=generated();check(first.constantRoughness==.8f,"Direct scalar roughness lost its exact parameter proof.");
    findMaterialNode(graph,scalar)->scalar=.1f;edited=generated();
    check(first.topologyKey==edited.topologyKey&&edited.constantRoughness==.1f,
        "Cached scalar topology retained the old SSR eligibility after a value edit.");
    const auto color=addMaterialNode(graph,MaterialNodeKind::Color).id;findMaterialNode(graph,color)->color={.25f,.75f,.5f,.9f};
    connectMaterialNodes(graph,color,MaterialPin::RGB,output,MaterialPin::Roughness);
    check(generated().constantRoughness==.25f,"RGB-to-scalar roughness did not use the first color component.");
    connectMaterialNodes(graph,color,MaterialPin::G,output,MaterialPin::Roughness);
    check(generated().constantRoughness==.75f,"Direct color-channel roughness used the wrong component.");
    const auto time=addMaterialNode(graph,MaterialNodeKind::Time).id;
    connectMaterialNodes(graph,time,MaterialPin::Value,output,MaterialPin::Roughness);
    check(!generated().constantRoughness,"Runtime roughness was classified using only its initial value.");
    const auto multiply=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(graph,scalar,MaterialPin::Value,multiply,MaterialPin::Left);
    connectMaterialNodes(graph,multiply,MaterialPin::Value,output,MaterialPin::Roughness);
    check(!generated().constantRoughness,"CPU arithmetic was treated as an exact bound on shader roughness.");
    const auto texture=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,texture)->texture.image.id="assets/textures/roughness.png";
    findMaterialNode(graph,texture)->texture.role=MaterialTextureRole::Data;
    connectMaterialNodes(graph,texture,MaterialPin::G,multiply,MaterialPin::Right);
    check(!generated().constantRoughness,"Textured roughness retained an unsafe scalar-factor proof.");
    connectMaterialNodes(graph,texture,MaterialPin::G,output,MaterialPin::Roughness);
    check(!generated().constantRoughness,"A fragment-varying roughness texture was classified as constant.");
}
MaterialAsset fixture() {
    MaterialAsset material;material.id="fixture";material.name="Shader graph";
    auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id;
    findMaterialNode(graph,output)->output.blend=MaterialBlend::Mask;
    const auto uv=addMaterialNode(graph,MaterialNodeKind::UV).id;
    findMaterialNode(graph,uv)->uv.mode=MaterialUvMode::WorldTriplanar;
    const auto color=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,color)->texture.image=AssetRef<DecodedImage>{"assets/textures/color.png"};
    const auto data=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,data)->texture.role=MaterialTextureRole::Data;
    findMaterialNode(graph,data)->texture.image=AssetRef<DecodedImage>{"assets/textures/packed.png"};
    const auto normal=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;
    findMaterialNode(graph,normal)->texture.image=AssetRef<DecodedImage>{"assets/textures/normal.png"};
    const auto scalar=addMaterialNode(graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(graph,scalar)->scalar=.7f;
    const auto first=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
    const auto second=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
    for(auto id:{color,data,normal})connectMaterialNodes(graph,uv,MaterialPin::UV,id,MaterialPin::UV);
    connectMaterialNodes(graph,color,MaterialPin::RGB,first,MaterialPin::Left);
    connectMaterialNodes(graph,scalar,MaterialPin::Value,first,MaterialPin::Right);
    connectMaterialNodes(graph,first,MaterialPin::Value,second,MaterialPin::Left);
    connectMaterialNodes(graph,data,MaterialPin::RGB,second,MaterialPin::Right);
    connectMaterialNodes(graph,second,MaterialPin::Value,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,data,MaterialPin::G,output,MaterialPin::Roughness);
    connectMaterialNodes(graph,data,MaterialPin::B,output,MaterialPin::Metallic);
    connectMaterialNodes(graph,color,MaterialPin::A,output,MaterialPin::OpacityMask);
    connectMaterialNodes(graph,normal,MaterialPin::Normal,output,MaterialPin::Normal);
    return material;
}
MaterialAsset rgbScalarFixture() {
    MaterialAsset material;auto& graph=material.graph;
    const auto out=addMaterialNode(graph,MaterialNodeKind::Output).id;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto rough=addMaterialNode(graph,MaterialNodeKind::Texture2D).id,metal=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,rough)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,rough)->texture.image.id="assets/textures/roughness.png";
    findMaterialNode(graph,metal)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,metal)->texture.image.id="assets/textures/metallic.png";
    const auto color=addMaterialNode(graph,MaterialNodeKind::Color).id,product=addMaterialNode(graph,MaterialNodeKind::Multiply).id,saturate=addMaterialNode(graph,MaterialNodeKind::Clamp).id;
    findMaterialNode(graph,color)->color={.2f,.9f,.7f,.6f};
    connectMaterialNodes(graph,rough,MaterialPin::RGB,out,MaterialPin::Roughness);
    connectMaterialNodes(graph,metal,MaterialPin::RGB,out,MaterialPin::Metallic);
    connectMaterialNodes(graph,rough,MaterialPin::RGB,product,MaterialPin::Left);
    connectMaterialNodes(graph,color,MaterialPin::RGB,product,MaterialPin::Right);
    connectMaterialNodes(graph,product,MaterialPin::Value,out,MaterialPin::Specular);
    connectMaterialNodes(graph,product,MaterialPin::Value,saturate,MaterialPin::Left);
    connectMaterialNodes(graph,saturate,MaterialPin::Value,out,MaterialPin::Occlusion);
    connectMaterialNodes(graph,metal,MaterialPin::RGB,out,MaterialPin::OpacityMask);
    return material;
}
MaterialAsset animatedFixture() {
    auto material=fixture();auto& graph=material.graph;
    const auto output=materialOutputNode(graph).id;
    const auto texture=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Texture2D;})->id;
    const auto uv=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::UV;})->id;
    const auto time=addMaterialNode(graph,MaterialNodeKind::Time).id,panner=addMaterialNode(graph,MaterialNodeKind::Panner).id;
    const auto add=addMaterialNode(graph,MaterialNodeKind::Add).id,lerp=addMaterialNode(graph,MaterialNodeKind::Lerp).id;
    const auto power=addMaterialNode(graph,MaterialNodeKind::Power).id,color=addMaterialNode(graph,MaterialNodeKind::Color).id;
    findMaterialNode(graph,panner)->panner.speed={.25f,-.5f};
    findMaterialNode(graph,time)->time={false,true,4};
    connectMaterialNodes(graph,uv,MaterialPin::UV,panner,MaterialPin::UV);
    connectMaterialNodes(graph,time,MaterialPin::Value,panner,MaterialPin::Time);
    connectMaterialNodes(graph,panner,MaterialPin::UV,texture,MaterialPin::UV);
    connectMaterialNodes(graph,texture,MaterialPin::RGB,add,MaterialPin::Left);
    connectMaterialNodes(graph,add,MaterialPin::Value,lerp,MaterialPin::Left);
    connectMaterialNodes(graph,color,MaterialPin::RGB,lerp,MaterialPin::Right);
    connectMaterialNodes(graph,time,MaterialPin::Value,lerp,MaterialPin::Alpha);
    connectMaterialNodes(graph,lerp,MaterialPin::Value,power,MaterialPin::Left);
    connectMaterialNodes(graph,power,MaterialPin::Value,output,MaterialPin::BaseColor);
    return material;
}
MaterialAsset grungeAdjustmentFixture() {
    MaterialAsset material;auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto uv=addMaterialNode(graph,MaterialNodeKind::UV).id;
    const auto panner=addMaterialNode(graph,MaterialNodeKind::Panner).id;
    findMaterialNode(graph,panner)->panner.speed={.1f,-.2f};
    const auto grunge=addMaterialNode(graph,MaterialNodeKind::Grunge).id;
    const auto desaturation=addMaterialNode(graph,MaterialNodeKind::Desaturation).id;
    const auto contrastRgb=addMaterialNode(graph,MaterialNodeKind::ContrastRGB).id;
    const auto hue=addMaterialNode(graph,MaterialNodeKind::HueShift).id;
    const auto contrast=addMaterialNode(graph,MaterialNodeKind::Contrast).id;
    connectMaterialNodes(graph,uv,MaterialPin::UV,panner,MaterialPin::UV);
    connectMaterialNodes(graph,panner,MaterialPin::UV,grunge,MaterialPin::UV);
    connectMaterialNodes(graph,grunge,MaterialPin::RGB,desaturation,MaterialPin::Left);
    connectMaterialNodes(graph,desaturation,MaterialPin::RGB,contrastRgb,MaterialPin::Left);
    connectMaterialNodes(graph,contrastRgb,MaterialPin::RGB,hue,MaterialPin::Left);
    connectMaterialNodes(graph,hue,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,grunge,MaterialPin::RGB,contrast,MaterialPin::Left);
    connectMaterialNodes(graph,contrast,MaterialPin::Value,output,MaterialPin::OpacityMask);
    return material;
}
void adjustmentChecks() {
    auto material=grungeAdjustmentFixture();const auto original=generateMaterialGraphShader(material);
    check(original.textures.size()==1&&original.textures.front().grunge&&original.textures.front().image.empty()&&original.dynamicCoverage,
        "Animated Grunge did not lower to a derived Texture 2D binding with dynamic coverage.");
    check(original.textures.front().role==MaterialGraphTextureRole::Data,"Grunge image lost its linear-data texture role.");
    check(!materialGraphFixedSurface(material.graph),"A spatial Grunge was certified for the fixed-surface path.");
    auto& graph=material.graph;
    MaterialNodeId grunge{},contrast{},hue{};
    for(auto& node:graph.nodes) {
        if(node.kind==MaterialNodeKind::Grunge) {
            grunge=node.id;node.grunge.seed=64;node.grunge.tiling=17;node.grunge.coverage=.2f;
            node.grunge.disorder=.8f;node.grunge.detail=.9f;node.grunge.stainsOpacity=.7f;node.grunge.leaksOpacity=.6f;
        }
        if(materialNodeImageAdjustment(node.kind))node.right.value={2,2,2,2};
        if(node.kind==MaterialNodeKind::HueShift)hue=node.id;
        if(node.kind==MaterialNodeKind::Contrast)contrast=node.id;
    }
    const auto edited=generateMaterialGraphShader(material);
    check(original.topologyKey==edited.topologyKey&&original.uniforms!=edited.uniforms,"Grunge or adjustment edits changed shader topology instead of uniforms.");
    for(const auto& node:graph.nodes)if(materialNodeImageAdjustment(node.kind)||node.kind==MaterialNodeKind::Grunge)
        for(const auto& pin:materialNodeOutputs(graph,node)) {
            const auto preview=generateMaterialGraphPreviewShader(material,{node.id,pin.pin});
            check(preview.nodePreview&&preview.textures.size()==1&&preview.textures.front().grunge,
                "Adjustment/Grunge preview lost the shared generated image binding.");
        }
    for(const auto id:{grunge,contrast}) {
        const auto rgb=generateMaterialGraphPreviewShader(material,{id,MaterialPin::RGB});
        const auto value=generateMaterialGraphPreviewShader(material,{id,MaterialPin::Value});
        check(rgb.variants.front().source.find("result.baseColor = vec3_splat(g")!=std::string::npos&&
            value.variants.front().source.find("result.baseColor = vec3_splat(g")!=std::string::npos,
            "Scalar generator RGB/Value previews did not splat their scalar result.");
    }
    const auto scalar=addMaterialNode(graph,MaterialNodeKind::Scalar).id;
    connectMaterialNodes(graph,scalar,MaterialPin::Value,hue,MaterialPin::Left);
    check(generateMaterialGraphShader(material).variants.front().source.find("ccGraphHueShift(vec3_splat(")!=std::string::npos,
        "Adjustment scalar input did not broadcast locally to RGB.");
    materialOutputNode(graph).output.blend=MaterialBlend::Opaque;
    check(!generateMaterialGraphShader(material).dynamicCoverage,"Opaque Grunge color invalidated static coverage.");
}
void runtimeUniformChecks() {
    auto material=animatedFixture();const auto serialized=serializeMaterialGraph(material.graph);
    const auto shader=generateMaterialGraphShader(material);
    check(shader.runtimeGraph&&!shader.runtimeParameters.empty()&&shader.dynamicCoverage,"Animated mask did not record runtime uniform dependencies.");
    check(shader.runtimeDefinitionSignature!=0,"Animated graph omitted its immutable runtime definition identity.");
    auto changedPanner=material;
    for(auto& node:changedPanner.graph.nodes)if(node.kind==MaterialNodeKind::Panner)node.panner.speed[0]+=.25f;
    const auto pannerShader=generateMaterialGraphShader(changedPanner);
    check(pannerShader.topologyKey==shader.topologyKey&&pannerShader.uniforms==shader.uniforms&&pannerShader.runtimeDefinitionSignature!=shader.runtimeDefinitionSignature,
        "Panner speed edits at t=0 must replace runtime packets without recompiling topology.");
    auto changedTime=material;
    for(auto& node:changedTime.graph.nodes)if(node.kind==MaterialNodeKind::Time)node.time={true,true,2};
    const auto timeShader=generateMaterialGraphShader(changedTime);
    check(timeShader.topologyKey==shader.topologyKey&&timeShader.uniforms==shader.uniforms&&timeShader.runtimeDefinitionSignature!=shader.runtimeDefinitionSignature,
        "Time period/pause edits at t=0 must replace runtime packets without recompiling topology.");
    auto layout=material;layout.graph.nodes.front().position={123,456};
    check(generateMaterialGraphShader(layout).runtimeDefinitionSignature==shader.runtimeDefinitionSignature,"Graph layout changed runtime definition identity.");
    auto disconnected=defaultMaterialAsset();canonicalizeMaterialAsset(disconnected);
    const auto disconnectedTime=addMaterialNode(disconnected.graph,MaterialNodeKind::Time).id;
    auto preview=generateMaterialGraphPreviewShader(disconnected,{disconnectedTime,MaterialPin::Value});
    findMaterialNode(disconnected.graph,disconnectedTime)->time={true,true,2};
    const auto editedPreview=generateMaterialGraphPreviewShader(disconnected,{disconnectedTime,MaterialPin::Value});
    check(preview.topologyKey==editedPreview.topologyKey&&preview.uniforms==editedPreview.uniforms&&preview.runtimeDefinitionSignature!=editedPreview.runtimeDefinitionSignature,
        "A disconnected Time preview lost its runtime definition edit.");
    disconnected.graph.nodes.front().position={777,888};
    check(generateMaterialGraphPreviewShader(disconnected,{disconnectedTime,MaterialPin::Value}).runtimeDefinitionSignature==editedPreview.runtimeDefinitionSignature,
        "A disconnected preview's layout changed runtime definition identity.");
    const auto disconnectedPanner=addMaterialNode(disconnected.graph,MaterialNodeKind::Panner).id;
    connectMaterialNodes(disconnected.graph,disconnectedTime,MaterialPin::Value,disconnectedPanner,MaterialPin::Time);
    preview=generateMaterialGraphPreviewShader(disconnected,{disconnectedPanner,MaterialPin::UV});
    findMaterialNode(disconnected.graph,disconnectedPanner)->panner.speed={.5f,1};
    const auto editedPannerPreview=generateMaterialGraphPreviewShader(disconnected,{disconnectedPanner,MaterialPin::UV});
    check(preview.topologyKey==editedPannerPreview.topologyKey&&preview.uniforms==editedPannerPreview.uniforms&&preview.runtimeDefinitionSignature!=editedPannerPreview.runtimeDefinitionSignature,
        "A disconnected Panner preview lost its runtime definition edit.");
    auto first=shader.uniforms,second=shader.uniforms;
    updateMaterialGraphRuntimeUniforms(*shader.runtimeGraph,shader.runtimeParameters,{1,8},first);
    updateMaterialGraphRuntimeUniforms(*shader.runtimeGraph,shader.runtimeParameters,{3,8},second);
    check(first!=second&&shader.uniforms==generateMaterialGraphShader(material).uniforms,"Clock updates froze or mutated the authored uniform packet.");
    check(serializeMaterialGraph(material.graph)==serialized,"Runtime uniform publication changed authored material contents.");
    for(auto& node:material.graph.nodes) {
        if(node.kind==MaterialNodeKind::Panner){node.panner.fractionalPart=true;node.panner.speed={-.25f,3};}
        if(node.kind==MaterialNodeKind::Time)node.time={true,false,0};
        if(node.kind==MaterialNodeKind::Lerp)node.alpha.value={2,2,2,2};
        if(node.kind==MaterialNodeKind::Power)node.right.value={3,3,3,3};
    }
    check(generateMaterialGraphShader(material).topologyKey==shader.topologyKey,"Time/Panner/arithmetic parameter edits compiled new shader topology.");
    materialOutputNode(material.graph).output.blend=MaterialBlend::Opaque;
    check(!generateMaterialGraphShader(material).dynamicCoverage,"Animated opaque color invalidated static shadows.");
}
void tessellatedCoverageNormalChecks() {
    auto material=fixture();materialOutputNode(material.graph).output.tessellation.enabled=true;
    const auto generated=generateMaterialGraphShader(material);
    for(const auto& variant:generated.variants) {
        if(variant.pass!=MaterialGraphPass::Picking&&variant.pass!=MaterialGraphPass::PointShadow&&variant.pass!=MaterialGraphPass::DirectionalShadow)continue;
        check(variant.tessellation&&!variant.domainSource.empty()&&variant.source.find("#define CC_GRAPH_TESSELLATION 1")!=std::string::npos,
            "Tessellated coverage lost its domain stage or deformation-normal guard.");
        check(variant.source.find("uniform mat4 u_modelNormalMatrix;")!=std::string::npos&&variant.source.find("uniform vec4 u_modelOptions;")!=std::string::npos&&
            variant.source.find("#include \"material_graph_depth.sh\"")!=std::string::npos,
            "Tessellated world-triplanar coverage lost the shared deformed-normal shader inputs.");
        for(const auto& expected:{std::pair{"u_modelNormalMatrix",ShaderUniformKind::Mat4},std::pair{"u_modelOptions",ShaderUniformKind::Vec4}})
            check(std::any_of(variant.manifest.begin(),variant.manifest.end(),[&](const auto& binding){return binding.name==expected.first&&binding.kind==expected.second&&binding.count==1;}),
                "Tessellated depth/picking manifest omits its deformation-normal matrix/options contract.");
    }
}
MaterialAsset refractionFixture() {
    auto material=fixture();auto& graph=material.graph;
    auto& output=materialOutputNode(graph);const auto id=output.id;
    output.output.blend=MaterialBlend::Alpha;output.output.refraction=true;
    output.output.thickness=.02f;output.output.roughness=.2f;
    const auto* coverage=materialInputLink(graph,id,MaterialPin::OpacityMask);
    connectMaterialNodes(graph,coverage->fromNode,coverage->fromPin,id,MaterialPin::Opacity);
    const auto ior=addMaterialNode(graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(graph,ior)->scalar=1.5f;
    connectMaterialNodes(graph,ior,MaterialPin::Value,id,MaterialPin::Ior);
    return material;
}
void refractionShaderChecks() {
    auto material=refractionFixture();const auto generated=generateMaterialGraphShader(material);
    check(generated.refraction&&generated.variants.size()==9,"Enabled refraction did not publish both optional transmission variants.");
    auto edited=material;materialOutputNode(edited.graph).output.thickness=.1f;
    materialOutputNode(edited.graph).output.attenuationColor={.3f,.5f,.8f};
    const auto changed=generateMaterialGraphShader(edited);
    check(generated.topologyKey==changed.topologyKey&&generated.uniforms!=changed.uniforms,"Optical parameter edits compiled another topology or retained stale data.");
    for(std::size_t index:{1u,2u,4u})check(generated.variants[index].source==changed.variants[index].source,"Optical parameter edits changed independent picking/shadow coverage.");
    for(const auto& variant:generated.variants) {
        const bool transmission=variant.pass==MaterialGraphPass::ModelTransmission||variant.pass==MaterialGraphPass::ParticleTransmission;
        const auto binding=[&](const char* name)->const ShaderUniformContract* {const auto found=std::find_if(variant.manifest.begin(),variant.manifest.end(),[&](const auto& value){return value.name==name;});return found==variant.manifest.end()?nullptr:&*found;};
        check(bool(binding("s_refractionSceneColor"))==transmission,"Scene-color refraction sampler leaked into another pass.");
        if(transmission) {
            check(binding("s_refractionSceneColor")->samplerSlot==12&&binding("s_refractionSceneDepth")->samplerSlot==14&&
                binding("s_prefiltered")->samplerSlot==13&&binding("s_brdfLut")&&binding("s_brdfLut")->samplerSlot==15&&
                binding("u_pbrSettings")&&binding("u_clusterAtlas")&&binding("u_clusterOptions")&&!binding("s_irradiance")&&!binding("s_pointShadow"),
                "Transmission binding layout aliases the twelve material textures or lighting inputs.");
            check(variant.source.find("#include \"material_graph_transmission.sh\"")!=std::string::npos,"Transmission pass selected the ordinary surface fragment.");
        }
    }
    auto specular=material;materialOutputNode(specular.graph).output.specular=.9f;
    check(generateMaterialGraphShader(specular).uniforms==generated.uniforms,"Dormant Specular changed the refraction packet.");
    const auto scalar=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[](const auto& node){return node.kind==MaterialNodeKind::Scalar;})->id;
    const auto preview=generateMaterialGraphPreviewShader(material,{scalar,MaterialPin::Value});
    check(!preview.refraction&&preview.variants.size()==1,"Node visualization acquired refractive composition.");
    materialOutputNode(material.graph).output.tessellation.enabled=true;
    const auto displaced=generateMaterialGraphShader(material);
    const auto& transmission=displaced.variants[5];
    check(transmission.tessellation&&transmission.vertexName=="vs_model"&&transmission.domainSource.find("#define CC_TESS_PASS 0")!=std::string::npos,
        "Mesh transmission no longer shares Model's displaced geometry/varyings.");
    check(!displaced.variants[6].tessellation&&displaced.variants[6].vertexName=="vs_particle","Sprite transmission incorrectly requested tessellated mesh geometry.");
    check(displaced.variants[7].pass==MaterialGraphPass::ModelReflection&&displaced.variants[7].tessellation&&
        displaced.variants[7].domainSource==displaced.variants[5].domainSource&&
        displaced.variants[8].pass==MaterialGraphPass::ParticleReflection&&!displaced.variants[8].tessellation,
        "Refractive reflection receiver disagrees with its own displaced surface or sprite geometry.");
}
void cpuChecks() {
    auto material=fixture();const auto original=generateMaterialGraphShader(material);
    check(original.variants.size()==7 && original.textures.size()==3 && original.usesNormal,"Reachable graph bindings/passes were not lowered.");
    check(original.reflection&&original.variants[5].pass==MaterialGraphPass::ModelReflection&&original.variants[6].pass==MaterialGraphPass::ParticleReflection,
        "Lit material did not append both complete reflection receiver variants.");
    for(const auto& variant:original.variants) {
        const bool reflection=variant.pass==MaterialGraphPass::ModelReflection||variant.pass==MaterialGraphPass::ParticleReflection;
        const auto binding=[&](const char* name)->const ShaderUniformContract* {
            const auto found=std::find_if(variant.manifest.begin(),variant.manifest.end(),[&](const auto& value){return value.name==name;});
            return found==variant.manifest.end()?nullptr:&*found;
        };
        check(bool(binding("u_ssrView"))==reflection&&bool(binding("s_ssrSceneColor"))==reflection,
            "Reflection source/settings leaked into an ordinary color, transmission or coverage pass.");
        if(reflection) {
            check(binding("s_ssrSceneColor")->samplerSlot==12&&binding("s_prefiltered")->samplerSlot==13&&
                binding("s_ssrDepth")->samplerSlot==14&&binding("s_brdfLut")->samplerSlot==15&&
                !binding("s_irradiance")&&!binding("s_pointShadow")&&!binding("u_ssrCapture"),
                "Reflection source/HiZ/BRDF bindings alias graph textures, shadow slots or capture controls.");
            check(variant.source.find("#include \"material_graph_reflection.sh\"")!=std::string::npos,
                "Reflection receiver selected a full-lighting or transmission fragment.");
        }
    }
    for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive})
        for(const auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit}) {
            auto candidate=material;auto& output=materialOutputNode(candidate.graph).output;output.blend=blend;output.shading=shading;
            const auto generated=generateMaterialGraphShader(candidate);
            const bool expected=shading==MaterialShading::StandardPbr&&blend!=MaterialBlend::Additive;
            check(generated.reflection==expected&&generated.variants.size()==(expected?7u:5u),
                "Reflection variants were omitted from Lit coverage or admitted for Unlit/Additive materials.");
        }
    for(const auto& variant:original.variants) {
        check(variant.source.find("crimson-material-graph-generator-v41")!=std::string::npos,
            "Generated material identity does not invalidate earlier PBR/optical bundles.");
        const bool circularInput=variant.source.find("v_uvInterpolationCoordinates, v_uvInterpolationCorrection, v_uvInterpolationMode")!=std::string::npos;
        check(circularInput==(variant.pass!=MaterialGraphPass::Particle&&variant.pass!=MaterialGraphPass::ParticleReflection),
            "Generated mesh/particle inputs disagree with the circular UV vertex interface.");
        const auto has=[&](const char* name,std::uint16_t count=1) {
            return std::any_of(variant.manifest.begin(),variant.manifest.end(),[&](const auto& binding) {
                return binding.name==name && binding.kind==ShaderUniformKind::Vec4 && binding.count==count;
            });
        };
        const bool surface=variant.pass==MaterialGraphPass::Model || variant.pass==MaterialGraphPass::Particle;
        const bool reflection=variant.pass==MaterialGraphPass::ModelReflection || variant.pass==MaterialGraphPass::ParticleReflection;
        check(has("u_pbrSettings")==bool(surface||reflection),"PBR quality settings are missing from shading or leaked into a coverage pass.");
        const bool readableDepth=variant.pass==MaterialGraphPass::PointShadow || variant.pass==MaterialGraphPass::DirectionalShadow;
        check(!has("u_glowSource"),"Removed emission publication remained in the generated material manifest.");
        const bool motionDepth=variant.pass==MaterialGraphPass::PointShadow;
        const auto inputLine=variant.source.substr(0,variant.source.find('\n'));
        check((inputLine.find(", v_tangent")!=std::string::npos)==bool(surface||reflection||motionDepth),
            "Generated readable-depth input does not carry previous rigid position through the shared vertex interface.");
        check((variant.source.find("#define CC_GRAPH_POINT_SHADOW 1")!=std::string::npos)==motionDepth,
            "Generated motion-depth output is absent or leaks into another material pass.");
        check(has("u_directionalShadowSoftness",4)==surface && has("u_directionalShadowDepthRange",16)==surface,
            "Directional PCSS arrays are missing or incompatible with the generated surface contract.");
        for(const auto name:{"u_occluderDepthAtlas","u_occluderDepthOptions","u_occluderPhase"})
            check(!has(name),"Removed player-fade partition uniforms remain in generated shaders.");
        check(has("u_ssgiAtlas")==surface && has("u_ssgiSettings")==surface && has("u_ssgiDraw")==bool(surface||readableDepth),
            "SSGI source, receiver or reactive-depth uniforms are absent from their engine pass contract.");
        check(has("u_fogSky")==bool(surface||reflection),"Fog sky controls leaked into depth passes or are missing from surface passes.");
        for(const auto name:{"u_fogSunDirection","u_fogSunColor"}) {
            const bool hasSun=std::any_of(variant.manifest.begin(),variant.manifest.end(),[&](const auto& binding) {
                return binding.name==name&&binding.kind==ShaderUniformKind::Vec4&&binding.count==4;
            });
            check(hasSun==bool(surface||reflection),"Fog directional-light arrays have an incompatible engine pass contract.");
        }
        check(std::all_of(variant.manifest.begin(),variant.manifest.end(),[](const auto& binding) {
            return binding.kind!=ShaderUniformKind::Sampler || binding.samplerSlot<16;
        }),"SSGI expanded the generated material texture-stage budget.");
    }
    check(original.textures[0].mappingRow==original.textures[1].mappingRow && original.textures[1].mappingRow==original.textures[2].mappingRow,"Shared UV node allocated different transforms.");
    check(original.uniforms.size()<=MaterialGraphMaxParameterRows,"Parameter budget exceeded.");
    for(const auto& variant:original.variants)
        check(variant.source.find("result.opacity = clamp(")!=std::string::npos,"A generated pass bypassed shared opacity saturation.");
    auto cutoff=material;materialOutputNode(cutoff.graph).output.alphaCutoff=2;
    check(generateMaterialGraphShader(cutoff).uniforms[2][3]==2,"Mask cutoff above one was incorrectly clamped.");
    auto reordered=material;std::reverse(reordered.graph.nodes.begin(),reordered.graph.nodes.end());
    std::reverse(reordered.graph.links.begin(),reordered.graph.links.end());
    check(generateMaterialGraphShader(reordered).topologyKey==original.topologyKey,"Storage order changed deterministic lowering.");
    for(auto& node:material.graph.nodes) {
        node.name="Renamed";node.position={40,50};
        if(node.kind==MaterialNodeKind::Scalar)node.scalar=1.8f;
        if(node.kind==MaterialNodeKind::UV){node.uv.mode=MaterialUvMode::UV1;node.uv.scale={-.4f,2,1};node.uv.rotation={0,0,90};}
        if(node.kind==MaterialNodeKind::Texture2D){node.texture.image={};node.texture.normalStrength=-.8f;node.texture.sampler.wrapU=ModelWrap::Mirror;}
    }
    auto edited=generateMaterialGraphShader(material);
    check(original.topologyKey==edited.topologyKey && original.variants[0].source==edited.variants[0].source,"Packet/layout edits recompiled material topology.");
    check(original.uniforms!=edited.uniforms,"Authored values were not published into parameters.");
    check(std::all_of(edited.textures.begin(),edited.textures.end(),[&](const auto& b){return edited.uniforms[b.settingsRow][1]==0;}),"Cleared texture presence is not packet data.");
    const auto dormant=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(material.graph,dormant)->texture.image=AssetRef<DecodedImage>{"assets/textures/dormant.png"};
    check(generateMaterialGraphShader(material).topologyKey==original.topologyKey,"Disconnected node changed shader identity.");
    materialOutputNode(material.graph).output.shading=MaterialShading::Unlit;
    const auto unlit=generateMaterialGraphShader(material);
    check(unlit.topologyKey!=original.topologyKey && !unlit.usesNormal && unlit.textures.size()==2,"Unlit did not prune dormant normal/PBR branches.");
    check(original.variants[0].source.find("material_graph_model.sh")!=std::string::npos,"Model shader does not share its renderer template.");
    auto transparent=fixture();
    auto& transparentOutput=materialOutputNode(transparent.graph);
    transparentOutput.output.blend=MaterialBlend::Alpha;
    const auto transparentOutputId=transparentOutput.id;
    const auto alphaNode=addMaterialNode(transparent.graph,MaterialNodeKind::Scalar).id;
    findMaterialNode(transparent.graph,alphaNode)->scalar=.35f;
    connectMaterialNodes(transparent.graph,alphaNode,MaterialPin::Value,transparentOutputId,MaterialPin::Opacity);
    const auto alphaGraph=generateMaterialGraphShader(transparent);
    check(alphaGraph.uniforms[3][3]==2 && alphaGraph.variants[0].source.find("#define CC_GRAPH_LIT 1")!=std::string::npos,"Lit transparency was not generated.");
    auto specular=fixture();materialOutputNode(specular.graph).output.specular=.8f;
    const auto specularGraph=generateMaterialGraphShader(specular);
    check(specularGraph.topologyKey==original.topologyKey && specularGraph.uniforms[4][0]==.8f && original.uniforms[4][0]==.5f,"Specular default or parameter-only updates changed.");
    const auto vector=addMaterialNode(transparent.graph,MaterialNodeKind::Vector2).id;
    findMaterialNode(transparent.graph,vector)->vector2={-2,3};
    const auto uvNode=std::find_if(transparent.graph.nodes.begin(),transparent.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::UV;})->id;
    connectMaterialNodes(transparent.graph,vector,MaterialPin::XY,uvNode,MaterialPin::ScaleXY);
    const auto mapped=generateMaterialGraphShader(transparent);
    const auto row=mapped.textures.front().mappingRow;
    check(mapped.uniforms[row][0]==-2 && mapped.uniforms[row][1]==3 && mapped.uniforms[row][2]==1,"Vector2 mapping did not use the shared resolved mapping packet.");
    for(const auto& node:transparent.graph.nodes) {
        if(node.kind==MaterialNodeKind::Output)continue;
        for(const auto& pin:materialNodeOutputs(transparent.graph,node)) {
            const auto preview=generateMaterialGraphPreviewShader(transparent,{node.id,pin.pin});
            check(preview.nodePreview && preview.variants.size()==1 && preview.variants.front().pass==MaterialGraphPass::Model,"Node preview did not isolate its program contract.");
            check(preview.uniforms[3][3]==0 && preview.variants.front().source.find("result.opacity = 1.0;")!=std::string::npos,"Node preview unexpectedly retained material blending.");
        }
    }
    auto overloaded=fixture();auto& graph=overloaded.graph;
    auto tail=materialInputLink(graph,materialOutputNode(graph).id,MaterialPin::BaseColor)->fromNode;
    for(int i=0;i<11;++i) {
        const auto tex=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
        const auto mul=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(graph,tail,MaterialPin::Value,mul,MaterialPin::Left);
        connectMaterialNodes(graph,tex,MaterialPin::RGB,mul,MaterialPin::Right);tail=mul;
    }
    bool rejected=false;try{
        connectMaterialNodes(graph,tail,MaterialPin::Value,materialOutputNode(graph).id,MaterialPin::BaseColor);
        generateMaterialGraphShader(overloaded);
    }catch(const std::exception&){rejected=true;}
    check(rejected,"More than 12 active texture bindings were accepted.");
    MaterialAsset parameters;auto& parameterGraph=parameters.graph;
    const auto parameterOutput=addMaterialNode(parameterGraph,MaterialNodeKind::Output).id;
    auto parameterTail=addMaterialNode(parameterGraph,MaterialNodeKind::Scalar).id;
    for(int i=0;i<60;++i) {
        const auto value=addMaterialNode(parameterGraph,MaterialNodeKind::Scalar).id;
        const auto product=addMaterialNode(parameterGraph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(parameterGraph,parameterTail,MaterialPin::Value,product,MaterialPin::Left);
        connectMaterialNodes(parameterGraph,value,MaterialPin::Value,product,MaterialPin::Right);parameterTail=product;
    }
    connectMaterialNodes(parameterGraph,parameterTail,MaterialPin::Value,parameterOutput,MaterialPin::Roughness);
    const auto chained=generateMaterialGraphShader(parameters);
    check(chained.uniforms.size()==69 && chained.uniforms.size()<=MaterialGraphMaxParameterRows,"Long scalar Multiply chain lost parameter packing.");
    auto rgbScalar=rgbScalarFixture();const auto rgbShader=generateMaterialGraphShader(rgbScalar);
    for(const auto& variant:rgbShader.variants) {
        if(variant.pass==MaterialGraphPass::Model||variant.pass==MaterialGraphPass::Particle)for(const auto name:{"roughness","metallic","specular"}) {
            const auto start=variant.source.find(std::string("result.")+name+" = (");
            check(start!=std::string::npos&&variant.source.substr(start,variant.source.find('\n',start)-start).ends_with(").x;"),
                "RGB scalar material input lacks explicit first-component conversion.");
        }
        check(variant.source.find(" = clamp((g")!=std::string::npos&&variant.source.find(").x, 0.0, 1.0)")!=std::string::npos,
            "RGB Saturate/coverage did not convert to Scalar before clamping.");
    }
    for(auto& node:rgbScalar.graph.nodes)if(node.kind==MaterialNodeKind::Color)node.color={.8f,.1f,4,.2f};
    const auto rgbEdited=generateMaterialGraphShader(rgbScalar);
    check(rgbEdited.topologyKey==rgbShader.topologyKey&&rgbEdited.uniforms!=rgbShader.uniforms,"Color parameter edits recompiled scalar conversion topology.");
}
void passIdentityChecks() {
    const ShaderCompileConfig config{};
    for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        auto material=fixture();
        auto& output=materialOutputNode(material.graph);output.output.blend=blend;
        const auto outputId=output.id;
        if(blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive) {
            const auto link=*materialInputLink(material.graph,outputId,MaterialPin::OpacityMask);
            connectMaterialNodes(material.graph,link.fromNode,link.fromPin,outputId,MaterialPin::Opacity);
        }
        const auto original=generateMaterialGraphShader(material);
        // A new color-only branch changes topology, allocations and expression
        // numbering without changing any coverage operation or its bindings.
        const auto color=addMaterialNode(material.graph,MaterialNodeKind::Texture2D).id;
        const auto mix=addMaterialNode(material.graph,MaterialNodeKind::Lerp).id;
        const auto previous=*materialInputLink(material.graph,outputId,MaterialPin::BaseColor);
        connectMaterialNodes(material.graph,previous.fromNode,previous.fromPin,mix,MaterialPin::Left);
        connectMaterialNodes(material.graph,color,MaterialPin::RGB,mix,MaterialPin::Right);
        connectMaterialNodes(material.graph,mix,MaterialPin::Value,outputId,MaterialPin::BaseColor);
        const auto edited=generateMaterialGraphShader(material);
        check(edited.topologyKey!=original.topologyKey,"Color topology edit did not change its material bundle.");
        for(std::size_t i=0;i<original.variants.size();++i) {
            const auto& before=original.variants[i];const auto& after=edited.variants[i];
            const bool surface=before.pass==MaterialGraphPass::Model||before.pass==MaterialGraphPass::Particle||before.pass==MaterialGraphPass::ModelReflection||before.pass==MaterialGraphPass::ParticleReflection;
            check((before.source!=after.source)==surface,"Color-only topology invalidated an unrelated depth shader.");
            check((materialGraphShaderRecipe(config,original,before).name!=materialGraphShaderRecipe(config,edited,after).name)==surface,
                "Shader job identity included another pass's topology.");
        }
        for(const auto& texture:original.textures) {
            // The fixture's coverage uses its first texture, including UV data.
            if(texture.nodeId!=materialInputLink(material.graph,outputId,MaterialPin::OpacityMask)->fromNode)continue;
            const auto after=std::find_if(edited.textures.begin(),edited.textures.end(),[&](const auto& item){return item.nodeId==texture.nodeId;});
            check(after!=edited.textures.end()&&after->slot==texture.slot&&after->mappingRow==texture.mappingRow&&after->settingsRow==texture.settingsRow,
                "Color edit moved shared coverage bindings.");
        }
        if(blend!=MaterialBlend::Opaque) {
            const auto opacity=addMaterialNode(material.graph,MaterialNodeKind::Multiply).id;
            const auto pin=blend==MaterialBlend::Mask?MaterialPin::OpacityMask:MaterialPin::Opacity;
            const auto previousOpacity=*materialInputLink(material.graph,outputId,pin);
            connectMaterialNodes(material.graph,previousOpacity.fromNode,previousOpacity.fromPin,opacity,MaterialPin::Left);
            connectMaterialNodes(material.graph,opacity,MaterialPin::Value,outputId,pin);
            const auto changed=generateMaterialGraphShader(material);
            check(changed.variants[1].source!=edited.variants[1].source,"Opacity topology failed to invalidate picking coverage.");
            check((changed.variants[2].source!=edited.variants[2].source)==(blend==MaterialBlend::Mask),
                "Shadow shader identity ignored Mask coverage or compiled unused transparent opacity.");
        }
    }
    // Shared branches must be ordered from coverage itself, independently of
    // which texture is first encountered through the BaseColor traversal.
    MaterialAsset shared;
    const auto output=addMaterialNode(shared.graph,MaterialNodeKind::Output).id;
    materialOutputNode(shared.graph).output.blend=MaterialBlend::Mask;
    const auto a=addMaterialNode(shared.graph,MaterialNodeKind::Texture2D).id;
    const auto b=addMaterialNode(shared.graph,MaterialNodeKind::Texture2D).id;
    const auto product=addMaterialNode(shared.graph,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(shared.graph,a,MaterialPin::R,product,MaterialPin::Left);
    connectMaterialNodes(shared.graph,b,MaterialPin::R,product,MaterialPin::Right);
    connectMaterialNodes(shared.graph,product,MaterialPin::Value,output,MaterialPin::OpacityMask);
    connectMaterialNodes(shared.graph,b,MaterialPin::RGB,output,MaterialPin::BaseColor);
    const auto before=generateMaterialGraphShader(shared);
    connectMaterialNodes(shared.graph,a,MaterialPin::RGB,output,MaterialPin::BaseColor);
    const auto after=generateMaterialGraphShader(shared);
    for(const auto i:{1,2,4})check(before.variants[i].source==after.variants[i].source,
        "Surface traversal order moved shared coverage expressions or bindings.");
}
std::map<std::filesystem::path,std::filesystem::file_time_type> cacheTimes(const std::filesystem::path& root) {
    std::map<std::filesystem::path,std::filesystem::file_time_type> result;
    for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path(),entry.last_write_time());
    return result;
}
MaterialAsset imageFoundationFixture() {
    MaterialAsset material;auto& g=material.graph;const auto add=[&](MaterialNodeKind k){return addMaterialNode(g,k).id;};
    const auto out=add(MaterialNodeKind::Output);materialOutputNode(g).output.blend=MaterialBlend::Mask;
    const auto uv=add(MaterialNodeKind::UV),texture=add(MaterialNodeKind::Texture2D),noise=add(MaterialNodeKind::Noise);
    findMaterialNode(g,uv)->uv.mode=MaterialUvMode::UV1;findMaterialNode(g,uv)->uv.scale={2,3,1};findMaterialNode(g,texture)->texture.image.id="assets/textures/packed.png";findMaterialNode(g,texture)->texture.role=MaterialTextureRole::Data;
    connectMaterialNodes(g,uv,MaterialPin::UV,texture,MaterialPin::UV);
    const auto levels=add(MaterialNodeKind::Levels),invert=add(MaterialNodeKind::Invert),bc=add(MaterialNodeKind::BrightnessContrast),sharpen=add(MaterialNodeKind::Sharpen),blend=add(MaterialNodeKind::Blend);
    connectMaterialNodes(g,texture,MaterialPin::RGBA,levels,MaterialPin::Left);connectMaterialNodes(g,levels,MaterialPin::Value,invert,MaterialPin::Left);connectMaterialNodes(g,invert,MaterialPin::Value,bc,MaterialPin::Left);connectMaterialNodes(g,bc,MaterialPin::Value,sharpen,MaterialPin::Left);connectMaterialNodes(g,sharpen,MaterialPin::Value,blend,MaterialPin::Left);connectMaterialNodes(g,noise,MaterialPin::RGB,blend,MaterialPin::Right);
    const auto split=add(MaterialNodeKind::RGBASplit),merge=add(MaterialNodeKind::RGBAMerge),alphaSplit=add(MaterialNodeKind::AlphaSplit),alphaMerge=add(MaterialNodeKind::AlphaMerge),gray=add(MaterialNodeKind::Grayscale),toColor=add(MaterialNodeKind::ToColor),normal=add(MaterialNodeKind::HeightToNormal);
    connectMaterialNodes(g,blend,MaterialPin::Value,split,MaterialPin::Left);
    for(const auto pin:{MaterialPin::R,MaterialPin::G,MaterialPin::B,MaterialPin::A})connectMaterialNodes(g,split,pin,merge,pin);
    connectMaterialNodes(g,merge,MaterialPin::RGBA,alphaSplit,MaterialPin::Left);connectMaterialNodes(g,alphaSplit,MaterialPin::RGB,alphaMerge,MaterialPin::Left);connectMaterialNodes(g,alphaSplit,MaterialPin::A,alphaMerge,MaterialPin::Alpha);
    connectMaterialNodes(g,alphaMerge,MaterialPin::RGBA,out,MaterialPin::BaseColor);connectMaterialNodes(g,alphaMerge,MaterialPin::A,out,MaterialPin::OpacityMask);
    connectMaterialNodes(g,alphaMerge,MaterialPin::RGBA,gray,MaterialPin::Left);connectMaterialNodes(g,gray,MaterialPin::Value,toColor,MaterialPin::Left);connectMaterialNodes(g,toColor,MaterialPin::RGB,out,MaterialPin::Roughness);connectMaterialNodes(g,noise,MaterialPin::Value,normal,MaterialPin::Left);connectMaterialNodes(g,normal,MaterialPin::Normal,out,MaterialPin::Normal);
    return material;
}
MaterialAsset resizeFixture() {
    MaterialAsset material;auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id;
    materialOutputNode(graph).output.tessellation.enabled=true;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto texture=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,texture)->texture.image.id="assets/textures/resize-source.png";
    const auto levels=addMaterialNode(graph,MaterialNodeKind::Levels).id;
    const auto gray=addMaterialNode(graph,MaterialNodeKind::Grayscale).id;
    const auto resize=addMaterialNode(graph,MaterialNodeKind::Resize).id;
    connectMaterialNodes(graph,texture,MaterialPin::RGBA,levels,MaterialPin::Left);
    connectMaterialNodes(graph,levels,MaterialPin::Value,gray,MaterialPin::Left);
    connectMaterialNodes(graph,gray,MaterialPin::Value,resize,MaterialPin::Left);
    connectMaterialNodes(graph,resize,MaterialPin::Value,output,MaterialPin::Height);
    connectMaterialNodes(graph,resize,MaterialPin::Value,output,MaterialPin::OpacityMask);
    return material;
}
MaterialAsset mixedResizeFixture() {
    MaterialAsset material;auto& graph=material.graph;
    const auto add=[&](MaterialNodeKind kind){return addMaterialNode(graph,kind).id;};
    const auto output=add(MaterialNodeKind::Output);
    materialOutputNode(graph).output.tessellation.enabled=true;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto texture=add(MaterialNodeKind::Texture2D),levels=add(MaterialNodeKind::Levels);
    findMaterialNode(graph,texture)->texture.image.id="assets/textures/mixed-resize-source.png";
    const auto rgb=add(MaterialNodeKind::Resize),rgba=add(MaterialNodeKind::Resize);
    const auto sharpen=add(MaterialNodeKind::Sharpen),gray=add(MaterialNodeKind::Grayscale),normal=add(MaterialNodeKind::HeightToNormal);
    connectMaterialNodes(graph,texture,MaterialPin::RGB,rgb,MaterialPin::Left);
    connectMaterialNodes(graph,texture,MaterialPin::RGBA,levels,MaterialPin::Left);
    connectMaterialNodes(graph,levels,MaterialPin::Value,rgba,MaterialPin::Left);
    connectMaterialNodes(graph,rgba,MaterialPin::Value,sharpen,MaterialPin::Left);
    connectMaterialNodes(graph,sharpen,MaterialPin::Value,gray,MaterialPin::Left);
    connectMaterialNodes(graph,gray,MaterialPin::Value,normal,MaterialPin::Left);
    // The original source remains independently live beside both baked images.
    connectMaterialNodes(graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,rgb,MaterialPin::Value,output,MaterialPin::Emission);
    connectMaterialNodes(graph,sharpen,MaterialPin::Value,output,MaterialPin::OpacityMask);
    connectMaterialNodes(graph,gray,MaterialPin::Value,output,MaterialPin::Height);
    connectMaterialNodes(graph,normal,MaterialPin::Normal,output,MaterialPin::Normal);
    return material;
}
void resizeShaderChecks() {
    auto material=resizeFixture();const auto generated=generateMaterialGraphShader(material);
    check(generated.textures.size()==1&&generated.textures[0].resize.has_value(),
        "Resize failed to replace its upstream image branch with one materialized sampler.");
    check(generated.textures[0].role==MaterialGraphTextureRole::Data&&generated.textures[0].image.empty(),
        "Resize output must be linear floating data with no authored image identity.");
    const auto& recipe=*generated.textures[0].resize;
    check(recipe.graph&&recipe.definitionKey&&generated.runtimeDefinitionSignature,
        "Resize packet lost its immutable source definition or publication identity.");
    for(const auto& variant:generated.variants)if(variant.tessellation) {
        check(variant.domainSource.find("ccImageLevels(")==std::string::npos,
            "The domain shader re-evaluates a CPU-baked Levels branch.");
        check(!variant.domainSource.empty(),"Resize Height omitted a native domain variant.");
    }
    auto edited=material;
    for(auto& node:edited.graph.nodes)if(node.kind==MaterialNodeKind::Levels)node.image.gamma=1.8f;
    auto updated=generateMaterialGraphShader(edited);
    check(updated.topologyKey==generated.topologyKey&&updated.uniforms==generated.uniforms&&
        updated.runtimeDefinitionSignature!=generated.runtimeDefinitionSignature&&
        updated.textures[0].resize->definitionKey!=recipe.definitionKey,
        "A baked upstream value edit must replace image content without recompiling its sampler shader.");
    for(auto& node:edited.graph.nodes)if(node.kind==MaterialNodeKind::Resize){node.resize.width=2048;node.resize.height=512;}
    const auto resized=generateMaterialGraphShader(edited);
    check(resized.topologyKey==updated.topologyKey&&resized.runtimeDefinitionSignature!=updated.runtimeDefinitionSignature,
        "Resize dimensions failed content invalidation or changed per-sample shader topology.");
    for(auto& node:edited.graph.nodes)node.position={456,789};
    const auto moved=generateMaterialGraphShader(edited);
    check(moved.runtimeDefinitionSignature==resized.runtimeDefinitionSignature,
        "Moving material nodes invalidated a Resize image.");
    const auto preview=generateMaterialGraphPreviewShader(material,{recipe.node,MaterialPin::Value});
    check(preview.textures.size()==1&&preview.textures[0].resize,
        "Resize preview escaped its materialized sampler boundary.");
    const auto mixed=mixedResizeFixture();const auto mixedShader=generateMaterialGraphShader(mixed);
    check(mixedShader.textures.size()==3&&std::count_if(mixedShader.textures.begin(),mixedShader.textures.end(),
        [](const auto& binding){return binding.resize.has_value();})==2,
        "Multiple Resize branches lost or duplicated an independently live source texture.");
    const auto texture=std::find_if(mixed.graph.nodes.begin(),mixed.graph.nodes.end(),
        [](const auto& node){return node.kind==MaterialNodeKind::Texture2D;})->id;
    check(std::count_if(mixedShader.textures.begin(),mixedShader.textures.end(),
        [&](const auto& binding){return binding.nodeId==texture&&!binding.resize;})==1,
        "A texture used directly and through Resize must keep exactly one direct sampler.");
    for(const auto& node:mixed.graph.nodes)if(node.kind==MaterialNodeKind::Resize||
        node.kind==MaterialNodeKind::Sharpen||node.kind==MaterialNodeKind::HeightToNormal) {
        const auto pin=node.kind==MaterialNodeKind::HeightToNormal?MaterialPin::Normal:MaterialPin::Value;
        const auto branch=generateMaterialGraphPreviewShader(mixed,{node.id,pin});
        check(branch.textures.size()==1&&branch.textures[0].resize,
            "A Resize or downstream spatial preview escaped its baked image boundary.");
        if(node.kind==MaterialNodeKind::Resize) {
            const auto* input=materialInputLink(mixed.graph,node.id,MaterialPin::Left);
            check(materialOutputType(mixed.graph,node.id,pin)==
                (input->fromPin==MaterialPin::RGB?MaterialValueType::Color:MaterialValueType::Color4),
                "Resize failed to retain its RGB or RGBA input family.");
        }
    }
    for(const auto& variant:mixedShader.variants)if(variant.tessellation)
        check(std::count_if(variant.domainManifest.begin(),variant.domainManifest.end(),
            [](const auto& binding){return binding.kind==ShaderUniformKind::Sampler;})==1,
            "Height domain bindings include a direct or Resize branch unrelated to Height.");
}
MaterialAsset stylizationFixture(MaterialNodeKind kind) {
    MaterialAsset material;auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id,texture=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    const auto effect=addMaterialNode(graph,kind).id;
    findMaterialNode(graph,texture)->texture.image.id="assets/textures/stylization.png";
    materialOutputNode(graph).output.tessellation.enabled=true;materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    connectMaterialNodes(graph,texture,MaterialPin::RGBA,effect,MaterialPin::Left);
    connectMaterialNodes(graph,effect,MaterialPin::Value,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,effect,MaterialPin::Value,output,MaterialPin::OpacityMask);
    connectMaterialNodes(graph,effect,MaterialPin::Value,output,MaterialPin::Height);
    return material;
}
void stylizationShaderChecks() {
    for(const auto kind:{MaterialNodeKind::Pixelization,MaterialNodeKind::Posterization,MaterialNodeKind::Dither}) {
        auto material=stylizationFixture(kind);const auto generated=generateMaterialGraphShader(material);
        const auto effect=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[&](const auto& n){return n.kind==kind;})->id;
        check(generated.textures.size()==1,"Stylization duplicated the source image's shader binding.");
        auto* node=findMaterialNode(material.graph,effect);
        if(kind==MaterialNodeKind::Pixelization)node->stylization.pixelSize=9;
        else if(kind==MaterialNodeKind::Posterization)node->stylization.levels=6;
        else {node->stylization.strength=.4f;node->stylization.pattern=DitherPattern::Bayer8x8;node->stylization.size=3;node->stylization.levels=7;}
        const auto edited=generateMaterialGraphShader(material);
        check(edited.topologyKey==generated.topologyKey,"A numeric stylization edit changed the generated shader topology.");
        if(kind==MaterialNodeKind::Posterization) {
            check(!generated.textures[0].resize&&edited.uniforms!=generated.uniforms,
                "Posterization must stay a direct image adjustment with value-only parameter updates.");
        } else {
            check(generated.textures[0].resize&&edited.textures[0].resize&&generated.textures[0].role==MaterialGraphTextureRole::Data,
                "Pixelization or Dither escaped its cached linear image boundary.");
            check(edited.uniforms==generated.uniforms&&edited.textures[0].resize->definitionKey!=generated.textures[0].resize->definitionKey&&
                edited.runtimeDefinitionSignature!=generated.runtimeDefinitionSignature,
                "Cached stylization controls did not invalidate image content independently of the shader packet.");
            if(kind==MaterialNodeKind::Pixelization)check(generated.textures[0].sampler.magFilter==ModelFilter::Nearest,
                "Pixelization's materialized sampler would interpolate across neighboring pixel blocks.");
            if(kind==MaterialNodeKind::Pixelization)node->stylization.pixelSize=1;else node->stylization.strength=0;
            const auto neutral=generateMaterialGraphShader(material);
            check(neutral.topologyKey==edited.topologyKey&&neutral.textures[0].sampler.magFilter==ModelFilter::Linear&&
                neutral.textures[0].sampler.minFilter==ModelFilter::LinearMipmapLinear,
                "Neutral Pixelization or Dither retained crisp filtering or recompiled its shader topology.");
            if(kind==MaterialNodeKind::Pixelization)node->stylization.pixelSize=9;else node->stylization.strength=.4f;
        }
        const auto preview=generateMaterialGraphPreviewShader(material,{effect,MaterialPin::Value});
        check(preview.textures.size()==1&&preview.textures[0].resize.has_value()==(kind!=MaterialNodeKind::Posterization),
            "Stylization preview changed the material's direct or cached sampling contract.");
        const auto before=edited.runtimeDefinitionSignature;
        for(auto& n:material.graph.nodes)n.position={123,456};
        check(generateMaterialGraphShader(material).runtimeDefinitionSignature==before,"Moving stylization nodes invalidated generated image content.");
    }
}
void compileStylization(const ShaderCompileConfig& config) {
    unsigned fragments{},domains{};
    const auto compile=[&](const MaterialGraphShader& generated) {
        for(const auto& variant:generated.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,generated,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Material stylization: "+diagnostic);
            ++fragments;
            if(variant.tessellation) {
                const auto domain=compileShader(config,materialGraphDomainShaderRecipe(config,generated,variant));
                if(!preflightGeneratedTessellationDomain(domain.bytes,vertex.bytes,variant.domainManifest,diagnostic))
                    throw std::runtime_error("Material stylization domain: "+diagnostic);
                ++domains;
            }
        }
    };
    for(const auto kind:{MaterialNodeKind::Pixelization,MaterialNodeKind::Posterization,MaterialNodeKind::Dither}) {
        const auto material=stylizationFixture(kind);compile(generateMaterialGraphShader(material));
        const auto effect=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[&](const auto& n){return n.kind==kind;})->id;
        compile(generateMaterialGraphPreviewShader(material,{effect,MaterialPin::Value}));
    }
    check(fragments>0&&domains>0,"Stylization compile coverage omitted fragments or native domain stages.");
    std::cout<<"Material stylization compiled "<<fragments<<" fragments and "<<domains<<" native domain stages.\n";
}
void stylizationCompileChecks(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    const auto root=std::filesystem::temp_directory_path()/("crimson-stylization-shaders-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;const auto resolved=std::filesystem::weakly_canonical(path,ec);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ec);if(!ec&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-stylization-shaders-"))std::filesystem::remove_all(resolved,ec);}} cleanup{root};
    compileStylization({compiler,source,include,root});
}
MaterialAsset imageStyleFixture(MaterialNodeKind kind) {
    MaterialAsset material;auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output).id,texture=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    const auto effect=addMaterialNode(graph,kind).id;
    findMaterialNode(graph,texture)->texture.image.id="assets/textures/image-style.png";
    materialOutputNode(graph).output.tessellation.enabled=true;materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    connectMaterialNodes(graph,texture,MaterialPin::RGBA,effect,MaterialPin::Left);
    if(kind==MaterialNodeKind::ColorRangeMask)connectMaterialNodes(graph,texture,MaterialPin::RGB,output,MaterialPin::BaseColor);
    else connectMaterialNodes(graph,effect,kind==MaterialNodeKind::GradientMap?MaterialPin::RGB:MaterialPin::Value,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,effect,kind==MaterialNodeKind::GradientMap?MaterialPin::A:MaterialPin::Value,output,MaterialPin::OpacityMask);
    connectMaterialNodes(graph,effect,kind==MaterialNodeKind::GradientMap?MaterialPin::R:MaterialPin::Value,output,MaterialPin::Height);
    return material;
}
void imageStyleShaderChecks() {
    for(const auto kind:{MaterialNodeKind::Kuwahara,MaterialNodeKind::GradientMap,MaterialNodeKind::ColorRangeMask,MaterialNodeKind::DetailBalance}) {
        auto material=imageStyleFixture(kind);const auto generated=generateMaterialGraphShader(material);
        const auto effect=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[&](const auto& n){return n.kind==kind;})->id;
        check(generated.textures.size()==1,"Image-style lowering duplicated a shared source sampler.");
        auto* node=findMaterialNode(material.graph,effect);
        if(kind==MaterialNodeKind::Kuwahara){node->kuwahara.radius=7;node->kuwahara.mix=.4f;}
        else if(kind==MaterialNodeKind::DetailBalance){node->detailBalance.radius=7;node->detailBalance.detail=1.6f;}
        else if(kind==MaterialNodeKind::GradientMap){node->gradientMap.stops={{1,0,{2,0,.5f}},{3,.4f,{0,3,.25f}},{2,1,{.5f,.25f,4}}};node->gradientMap.nextStopId=4;node->gradientMap.interpolation=MaterialGradientInterpolation::Smooth;}
        else {node->colorRangeMask.color={.3f,.6f,.2f};node->colorRangeMask.tolerance=.25f;node->colorRangeMask.softness=.35f;node->colorRangeMask.invert=true;}
        const auto edited=generateMaterialGraphShader(material);
        check(edited.topologyKey==generated.topologyKey,"Image-style numeric values, ramp stop count or interpolation changed shader topology.");
        if(kind==MaterialNodeKind::Kuwahara||kind==MaterialNodeKind::DetailBalance) {
            check(generated.textures[0].resize&&edited.textures[0].resize&&edited.uniforms==generated.uniforms&&
                edited.textures[0].resize->definitionKey!=generated.textures[0].resize->definitionKey,
                "A spatial image-style edit failed independent cached-image content invalidation.");
        } else check(!generated.textures[0].resize&&edited.uniforms!=generated.uniforms,
            "Gradient Map or Color Range Mask failed direct point-shader uniform publication.");
        if(kind==MaterialNodeKind::GradientMap) {
            const auto before=serializeMaterialGraph(material.graph);const auto layout=materialGraphLayoutHash(material.graph);
            const auto values=materialGraphValueHash(material.graph,MaterialShading::StandardPbr,MaterialBlend::Mask);
            for(auto& stop:node->gradientMap.stops)stop.id+=100;node->gradientMap.nextStopId+=100;
            const auto identities=generateMaterialGraphShader(material);
            check(identities.topologyKey==edited.topologyKey&&identities.uniforms==edited.uniforms&&identities.runtimeDefinitionSignature==edited.runtimeDefinitionSignature&&
                materialGraphValueHash(material.graph,MaterialShading::StandardPbr,MaterialBlend::Mask)==values&&
                materialGraphLayoutHash(material.graph)!=layout&&serializeMaterialGraph(material.graph)!=before,
                "Transient ramp stop identities affected rendered values or failed authored-history persistence.");
            node->gradientMap.stops.clear();for(unsigned i=0;i<16;++i)node->gradientMap.stops.push_back({i+1,i/15.f,{i/15.f,1-i/15.f,2}});node->gradientMap.nextStopId=17;
            const auto maximum=generateMaterialGraphShader(material);
            check(maximum.topologyKey==generated.topologyKey&&maximum.uniforms.size()==generated.uniforms.size(),
                "The sixteen-stop ramp exceeded its fixed parameter packet or compiled another topology.");
        }
        for(const auto& pin:materialNodeOutputs(material.graph,*node)) {
            const auto preview=generateMaterialGraphPreviewShader(material,{effect,pin.pin});
            check(preview.textures.size()==1&&preview.textures[0].resize.has_value()==(kind==MaterialNodeKind::Kuwahara||kind==MaterialNodeKind::DetailBalance),
                "An image-style output preview changed the direct/cached sampling contract.");
        }
        if(kind==MaterialNodeKind::DetailBalance) {
            const auto content=edited.textures[0].resize->definitionKey;
            node->detailBalance={0,2};const auto neutralRadius=generateMaterialGraphShader(material);
            node->detailBalance={32,1};const auto neutralDetail=generateMaterialGraphShader(material);
            check(neutralRadius.topologyKey==edited.topologyKey&&neutralDetail.topologyKey==edited.topologyKey&&
                neutralRadius.textures[0].resize->definitionKey!=content&&neutralDetail.textures[0].resize->definitionKey!=content,
                "Detail Balance bypass controls changed shader topology or retained stale generated content.");
            for(auto& n:material.graph.nodes)n.position={321,654};
            check(generateMaterialGraphShader(material).runtimeDefinitionSignature==neutralDetail.runtimeDefinitionSignature,
                "Moving Detail Balance nodes invalidated its image recipe.");
        }
    }
}
void compileImageStyle(const ShaderCompileConfig& config,bool detailOnly=false) {
    unsigned fragments{},domains{};
    const auto compile=[&](const MaterialGraphShader& generated) {
        for(const auto& variant:generated.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,generated,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))throw std::runtime_error("Image-style material: "+diagnostic);
            ++fragments;
            if(variant.tessellation) {
                const auto domain=compileShader(config,materialGraphDomainShaderRecipe(config,generated,variant));
                if(!preflightGeneratedTessellationDomain(domain.bytes,vertex.bytes,variant.domainManifest,diagnostic))throw std::runtime_error("Image-style domain: "+diagnostic);
                ++domains;
            }
        }
    };
    for(const auto kind:{MaterialNodeKind::Kuwahara,MaterialNodeKind::GradientMap,MaterialNodeKind::ColorRangeMask,MaterialNodeKind::DetailBalance}) {
        if(detailOnly&&kind!=MaterialNodeKind::DetailBalance)continue;
        const auto material=imageStyleFixture(kind);compile(generateMaterialGraphShader(material));
        const auto* node=&*std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[&](const auto& n){return n.kind==kind;});
        for(const auto& pin:materialNodeOutputs(material.graph,*node))compile(generateMaterialGraphPreviewShader(material,{node->id,pin.pin}));
    }
    check(fragments>0&&domains>0,"Image-style native checks omitted fragment or domain coverage.");
    std::cout<<"Image-style materials compiled "<<fragments<<" fragments and "<<domains<<" native domain stages.\n";
}
void imageStyleCompileChecks(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include,bool detailOnly=false) {
    const auto root=std::filesystem::temp_directory_path()/("crimson-image-style-shaders-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;const auto resolved=std::filesystem::weakly_canonical(path,ec);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ec);if(!ec&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-image-style-shaders-"))std::filesystem::remove_all(resolved,ec);}} cleanup{root};
    compileImageStyle({compiler,source,include,root},detailOnly);
}
void imageFoundationShaderChecks() {
    auto material=imageFoundationFixture();const auto generated=generateMaterialGraphShader(material);check(generated.textures.size()==1,"Procedural Noise allocated a sampler or split/merge duplicated image resources.");
    check(generated.variants.front().source.find("ccImageOffsetContext")!=std::string::npos&&generated.variants.front().source.find("dFdx(context.uv.zw)")!=std::string::npos,"Spatial image operations omitted the UV0-to-UV1 Jacobian.");
    {MaterialAsset four;const auto out=addMaterialNode(four.graph,MaterialNodeKind::Output).id,color=addMaterialNode(four.graph,MaterialNodeKind::Color).id,power=addMaterialNode(four.graph,MaterialNodeKind::Power).id;connectMaterialNodes(four.graph,color,MaterialPin::RGBA,power,MaterialPin::Left);connectMaterialNodes(four.graph,power,MaterialPin::Value,out,MaterialPin::BaseColor);const auto rgbaPower=generateMaterialGraphShader(four);check(rgbaPower.variants.front().source.find("vec4 ccGraphPositivePow")!=std::string::npos,"RGBA on an existing Power node omitted its fourth-lane overload.");}
    const auto old=generateMaterialGraphShader(fixture());check(old.variants.front().source.find("ccImageNoiseField")==std::string::npos,"New image helpers changed an existing material's generated source.");
    for(auto& n:material.graph.nodes)if(n.kind>=MaterialNodeKind::Levels){const auto value=serializeMaterialGraph(material.graph);const auto restored=parseMaterialGraph(value);check(serializeMaterialGraph(restored)==value,"New image node settings failed shader-fixture persistence.");break;}
    auto changed=material;for(auto& n:changed.graph.nodes)if(n.kind==MaterialNodeKind::Levels)n.image.gamma=2;
    const auto updated=generateMaterialGraphShader(changed);check(updated.topologyKey==generated.topologyKey&&updated.uniforms!=generated.uniforms,"A numeric Levels edit rebuilt the shader or failed to update parameters.");
    auto& root=materialOutputNode(material.graph);root.output.tessellation.enabled=true;
    const auto scalar=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Grayscale;})->id;
    connectMaterialNodes(material.graph,scalar,MaterialPin::Value,root.id,MaterialPin::Height);
    const auto domain=generateMaterialGraphShader(material);check(domain.variants.front().domainSource.find("imageUv1Du")!=std::string::npos&&domain.variants.front().domainSource.find("ccImagePatchUv0")!=std::string::npos,"Image Height branches omitted patch coordinate metrics.");
    // Independently solve a rotated/scaled UV1 chart and an affine world frame.
    // A UV0 displacement must preserve the same physical point in every source.
    const Vec2 u{2,1},v{-1,3},delta{.02f,-.03f};const Vec2 uv1Delta{u[0]*delta[0]+v[0]*delta[1],u[1]*delta[0]+v[1]*delta[1]};
    const float actualGradient=4*uv1Delta[0]-2*uv1Delta[1],expectedGradient=6*delta[0]-10*delta[1];check(std::abs(actualGradient-expectedGradient)<1e-6f,"Mixed UV1 image gradient lost scale or rotation.");
}
MaterialAsset atomicImageFixture() {
    MaterialAsset material;auto& g=material.graph;const auto add=[&](MaterialNodeKind k){return addMaterialNode(g,k).id;};
    const auto out=add(MaterialNodeKind::Output);materialOutputNode(g).output.blend=MaterialBlend::Mask;materialOutputNode(g).output.tessellation.enabled=true;
    const auto input=add(MaterialNodeKind::ImageInput),blur=add(MaterialNodeKind::Blur),warp=add(MaterialNodeKind::DirectionalWarp),transform=add(MaterialNodeKind::ImageTransform),panner=add(MaterialNodeKind::Panner);
    findMaterialNode(g,input)->texture.image.id="assets/textures/atomic.png";findMaterialNode(g,input)->texture.role=MaterialTextureRole::Data;
    findMaterialNode(g,panner)->panner.speed={.1f,.2f};
    connectMaterialNodes(g,input,MaterialPin::RGBA,blur,MaterialPin::Left);connectMaterialNodes(g,blur,MaterialPin::Value,warp,MaterialPin::Left);connectMaterialNodes(g,warp,MaterialPin::Value,transform,MaterialPin::Left);connectMaterialNodes(g,panner,MaterialPin::UV,transform,MaterialPin::UV);
    const auto scan=add(MaterialNodeKind::HistogramScan),mask=add(MaterialNodeKind::MaskAdjust),select=add(MaterialNodeKind::DirectionSelect),normal=add(MaterialNodeKind::DecodeNormal);
    connectMaterialNodes(g,transform,MaterialPin::Value,scan,MaterialPin::Left);connectMaterialNodes(g,scan,MaterialPin::Value,mask,MaterialPin::Left);connectMaterialNodes(g,scan,MaterialPin::Value,select,MaterialPin::Left);connectMaterialNodes(g,scan,MaterialPin::Value,normal,MaterialPin::Left);
    connectMaterialNodes(g,scan,MaterialPin::Value,out,MaterialPin::BaseColor);connectMaterialNodes(g,mask,MaterialPin::Value,out,MaterialPin::Height);connectMaterialNodes(g,mask,MaterialPin::Value,out,MaterialPin::OpacityMask);connectMaterialNodes(g,select,MaterialPin::Value,out,MaterialPin::Roughness);connectMaterialNodes(g,normal,MaterialPin::Normal,out,MaterialPin::Normal);
    return material;
}
void atomicShaderChecks() {
    auto material=atomicImageFixture();const auto generated=generateMaterialGraphShader(material);
    check(generated.textures.size()==1&&generated.textures[0].resize&&generated.textures[0].image.empty(),"Nested cached atoms leaked private source samplers.");
    check(generated.uniforms.size()<MaterialGraph::MaximumParameterRows&&generated.dynamicHeight&&generated.dynamicCoverage,"Mapped cached Height lost bounded uniforms or runtime motion.");
    const auto& binding=generated.textures[0];const auto transform=binding.nodeId;const auto oldDefinition=binding.resize->definitionKey;
    const auto& content=*binding.resize->graph;check(!materialInputLink(content,transform,MaterialPin::UV),"Cached Image Transform recipe retained runtime Panner.");
    const auto panner=materialInputLink(material.graph,transform,MaterialPin::UV)->fromNode;
    findMaterialNode(material.graph,panner)->panner.speed={.7f,-.4f};
    const auto mappingEdit=generateMaterialGraphShader(material);check(mappingEdit.textures[0].resize->definitionKey==oldDefinition&&mappingEdit.topologyKey==generated.topologyKey,"Runtime mapping edit rebaked completed image or recompiled shader.");
    auto packet=generated.uniforms;updateMaterialGraphRuntimeUniforms(material.graph,generated.runtimeParameters,{2,2},packet);check(packet!=generated.uniforms,"Image Transform Panner failed runtime parameter update.");
    const auto scan=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::HistogramScan;})->id;
    findMaterialNode(material.graph,scan)->atomic.coverage=.23f;findMaterialNode(material.graph,scan)->atomic.contrast=.7f;
    const auto numericEdit=generateMaterialGraphShader(material);check(numericEdit.topologyKey==generated.topologyKey&&numericEdit.uniforms!=generated.uniforms,"Numeric atomic adjustment recompiled source or failed packet update.");
    for(const auto& variant:generated.variants)if(variant.tessellation)check(variant.domainSource.find("ccImageMaskAdjust(")!=std::string::npos&&variant.domainSource.find("ccGraphImageSource")==std::string::npos,"Native Height lost point op or inlined a spatial neighborhood.");
    MaterialAsset source;const auto out=addMaterialNode(source.graph,MaterialNodeKind::Output).id,tex=addMaterialNode(source.graph,MaterialNodeKind::Texture2D).id,input=addMaterialNode(source.graph,MaterialNodeKind::ImageInput).id;
    findMaterialNode(source.graph,tex)->texture.image.id="assets/textures/base.png";findMaterialNode(source.graph,input)->texture.image.id="assets/textures/override.png";
    connectMaterialNodes(source.graph,tex,MaterialPin::RGBA,input,MaterialPin::Left);connectMaterialNodes(source.graph,input,MaterialPin::RGB,out,MaterialPin::BaseColor);
    auto shader=generateMaterialGraphShader(source);check(shader.textures.size()==1&&shader.textures[0].nodeId==input,"Assigned Image Input evaluated dormant Base sampler.");
    findMaterialNode(source.graph,input)->atomic.enabled=false;shader=generateMaterialGraphShader(source);check(shader.textures.empty(),"Disabled Image Input scheduled source texture work.");
    findMaterialNode(source.graph,input)->atomic.enabled=true;findMaterialNode(source.graph,input)->texture.image.id.clear();shader=generateMaterialGraphShader(source);check(shader.textures.size()==1&&shader.textures[0].nodeId==tex,"Empty override did not use connected Base.");
}
MaterialAsset decodedNormalFixture(MaterialUvMode mode,bool cached) {
    MaterialAsset material;auto& g=material.graph;const auto add=[&](MaterialNodeKind k){return addMaterialNode(g,k).id;};
    const auto out=add(MaterialNodeKind::Output),uv=add(MaterialNodeKind::UV),texture=add(MaterialNodeKind::Texture2D),levels=add(MaterialNodeKind::Levels),normal=add(MaterialNodeKind::DecodeNormal);
    findMaterialNode(g,uv)->uv.mode=mode;findMaterialNode(g,uv)->uv.scale={-2,3,1};findMaterialNode(g,uv)->uv.rotation={15,-25,37};
    findMaterialNode(g,texture)->texture.role=MaterialTextureRole::Data;findMaterialNode(g,texture)->texture.image.id="assets/textures/encoded-normal.png";
    connectMaterialNodes(g,texture,MaterialPin::RGBA,levels,MaterialPin::Left);
    if(cached){const auto transform=add(MaterialNodeKind::ImageTransform);connectMaterialNodes(g,levels,MaterialPin::Value,transform,MaterialPin::Left);connectMaterialNodes(g,uv,MaterialPin::UV,transform,MaterialPin::UV);connectMaterialNodes(g,transform,MaterialPin::Value,normal,MaterialPin::Left);}
    else {connectMaterialNodes(g,uv,MaterialPin::UV,texture,MaterialPin::UV);connectMaterialNodes(g,levels,MaterialPin::Value,normal,MaterialPin::Left);}
    connectMaterialNodes(g,normal,MaterialPin::Normal,out,MaterialPin::Normal);return material;
}
void decodedNormalMappingChecks() {
    for(const auto mode:{MaterialUvMode::UV0,MaterialUvMode::UV1,MaterialUvMode::LocalTriplanar,MaterialUvMode::WorldTriplanar})for(bool cached:{false,true}){
        auto material=decodedNormalFixture(mode,cached);const auto shader=generateMaterialGraphShader(material);check(shader.textures.size()==1,"Mapped decoder allocated extra samplers.");
        const auto& source=shader.variants.front().source;
        check(source.find("ccImageMappedTangentNormal(context,tangent,scale,rotation)")!=std::string::npos&&source.find("x=vec3((n.x<0.0?-1.0:1.0)*x.z,-x.y,-x.x)")!=std::string::npos&&source.find("ccMappingInverseRotate(x*weights.x+y*weights.y+z*weights.z,rotation)")!=std::string::npos,"Mapped decoder lost UV frame, per-projection reorientation or inverse rotation.");
        check(source.find("context.uv = vec4(uv, uv)")!=std::string::npos,"Mapped decoder failed to evaluate the complete processed image per projection.");
        const auto normal=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::DecodeNormal;})->id;
        findMaterialNode(material.graph,normal)->image.normalFlipY=true;findMaterialNode(material.graph,normal)->image.normalStrength=.4f;
        const auto edited=generateMaterialGraphShader(material);check(edited.topologyKey==shader.topologyKey&&edited.uniforms!=shader.uniforms,"Normal Strength/FlipY compiled new source.");
    }
    auto material=decodedNormalFixture(MaterialUvMode::UV0,false);auto& g=material.graph;const auto second=addMaterialNode(g,MaterialNodeKind::Texture2D).id,uv=addMaterialNode(g,MaterialNodeKind::UV).id,blend=addMaterialNode(g,MaterialNodeKind::Blend).id;
    findMaterialNode(g,second)->texture.role=MaterialTextureRole::Data;findMaterialNode(g,second)->texture.image.id="assets/textures/second-normal.png";findMaterialNode(g,uv)->uv.mode=MaterialUvMode::UV1;connectMaterialNodes(g,uv,MaterialPin::UV,second,MaterialPin::UV);
    const auto normal=std::find_if(g.nodes.begin(),g.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::DecodeNormal;})->id;const auto previous=*materialInputLink(g,normal,MaterialPin::Left);
    connectMaterialNodes(g,previous.fromNode,previous.fromPin,blend,MaterialPin::Left);connectMaterialNodes(g,second,MaterialPin::RGBA,blend,MaterialPin::Right);connectMaterialNodes(g,blend,MaterialPin::Value,normal,MaterialPin::Left);
    bool rejected=false;try{(void)generateMaterialGraphShader(material);}catch(const std::exception& error){rejected=std::string(error.what()).find("different mappings")!=std::string::npos;}check(rejected,"Decoder silently accepted encoded images in different mapping bases.");
}
void ssrCompileChecks(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    const auto root=std::filesystem::temp_directory_path()/("crimson-graph-ssr-shaders-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;const auto resolved=std::filesystem::weakly_canonical(path,ec);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ec);if(!ec&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-graph-ssr-shaders-"))std::filesystem::remove_all(resolved,ec);}} cleanup{root};
    const ShaderCompileConfig config{compiler,source,include,root};
    unsigned reflectionFragments{},domainStages{};
    const auto compileReceivers=[&](const char* label,const MaterialAsset& material,bool compileCapture) {
        const auto generated=generateMaterialGraphShader(material);
        check(generated.reflection,"Focused SSR fixture omitted reflection variants.");
        unsigned receivers{};
        for(const auto& variant:generated.variants) {
            const bool reflection=variant.pass==MaterialGraphPass::ModelReflection||variant.pass==MaterialGraphPass::ParticleReflection;
            const bool capture=variant.pass==MaterialGraphPass::Model||variant.pass==MaterialGraphPass::Particle;
            if(!reflection&&!(compileCapture&&capture))continue;
            const char* pass=variant.pass==MaterialGraphPass::ModelReflection?"ModelReflection":
                variant.pass==MaterialGraphPass::ParticleReflection?"ParticleReflection":
                variant.pass==MaterialGraphPass::Model?"ModelCapture":"ParticleCapture";
            std::cout<<"Focused SSR "<<label<<": compiling "<<pass<<".\n"<<std::flush;
            const auto recipe=materialGraphShaderRecipe(config,generated,variant);
            const auto fragment=compileShader(config,recipe),vertex=compileShader(config,variant.vertexName);
            std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Focused SSR receiver/capture: "+diagnostic);
            auto missingPbr=variant.manifest;
            std::erase_if(missingPbr,[](const auto& binding){return binding.name=="u_pbrSettings";});
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,missingPbr,diagnostic),
                "Lit receiver/capture admitted the obsolete PBR quality contract.");
            if(!reflection)continue;
            ++receivers;++reflectionFragments;
            auto wrong=variant.manifest;
            std::erase_if(wrong,[](const auto& binding){return binding.name=="u_ssrSettings";});
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                "SSR receiver admitted missing tracing settings.");
            wrong=variant.manifest;
            const auto depth=std::find_if(wrong.begin(),wrong.end(),[](const auto& binding){return binding.name=="s_ssrDepth";});
            check(depth!=wrong.end(),"SSR receiver omitted its depth sampler manifest.");
            depth->samplerSlot=13;
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                "SSR depth sampler aliased the environment slot.");
            const auto timestamps=cacheTimes(root);const auto warm=compileShader(config,recipe);
            check(warm.fingerprint==fragment.fingerprint&&warm.bytes==fragment.bytes&&cacheTimes(root)==timestamps,
                "Focused SSR warm compilation changed or rewrote its cached artifact.");
            if(variant.tessellation) {
                std::cout<<"Focused SSR "<<label<<": compiling native "<<pass<<" domain.\n"<<std::flush;
                const auto domain=compileShader(config,materialGraphDomainShaderRecipe(config,generated,variant));
                if(!preflightGeneratedTessellationDomain(domain.bytes,vertex.bytes,variant.domainManifest,diagnostic))
                    throw std::runtime_error("Focused SSR displaced domain: "+diagnostic);
                auto missingHeight=variant.domainManifest;
                std::erase_if(missingHeight,[](const auto& binding){return binding.kind==ShaderUniformKind::Sampler;});
                check(!preflightGeneratedTessellationDomain(domain.bytes,vertex.bytes,missingHeight,diagnostic),
                    "SSR domain admitted an undeclared Height texture.");
                ++domainStages;
            }
        }
        check(receivers==2,"Focused SSR did not compile both mesh and particle receiver variants.");
    };
    for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha}) {
        auto material=fixture();const auto output=materialOutputNode(material.graph).id;
        findMaterialNode(material.graph,output)->output.blend=blend;
        if(blend==MaterialBlend::Alpha) {
            const auto mask=*materialInputLink(material.graph,output,MaterialPin::OpacityMask);
            connectMaterialNodes(material.graph,mask.fromNode,mask.fromPin,output,MaterialPin::Opacity);
        }
        compileReceivers(blend==MaterialBlend::Opaque?"Lit Opaque":blend==MaterialBlend::Mask?"Lit Mask":"Lit Alpha",material,blend==MaterialBlend::Opaque);
    }
    compileReceivers("Lit Glass",refractionFixture(),false);
    // Glass attenuation consumes the same DFG budget as its surface/reflection
    // programs. Validate its dedicated free atlas slot without changing the
    // twelve graph textures or the immutable color/depth snapshot bindings.
    const auto glass=generateMaterialGraphShader(refractionFixture());
    for(const auto& variant:glass.variants)if(variant.pass==MaterialGraphPass::ModelTransmission||variant.pass==MaterialGraphPass::ParticleTransmission) {
        std::cout<<"Focused SSR Lit Glass: compiling "<<(variant.pass==MaterialGraphPass::ModelTransmission?"ModelTransmission":"ParticleTransmission")<<".\n"<<std::flush;
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,glass,variant)),vertex=compileShader(config,variant.vertexName);
        std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Glass PBR quality contract: "+diagnostic);
        for(const char* name:{"u_pbrSettings","u_clusterAtlas","u_clusterOptions","s_brdfLut"}) {
            auto missing=variant.manifest;std::erase_if(missing,[&](const auto& binding){return binding.name==name;});
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,missing,diagnostic),"Glass transmission admitted a missing DFG/PBR binding.");
        }
        auto aliased=variant.manifest;
        const auto atlas=std::find_if(aliased.begin(),aliased.end(),[](const auto& binding){return binding.name=="s_brdfLut";});
        check(atlas!=aliased.end(),"Glass transmission omitted the BRDF atlas.");atlas->samplerSlot=14;
        check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,aliased,diagnostic),"Glass BRDF atlas aliased the readable depth sampler.");
    }
    auto maximum=fixture();auto& graph=maximum.graph;
    const auto output=materialOutputNode(graph).id;
    auto tail=materialInputLink(graph,output,MaterialPin::BaseColor)->fromNode;
    for(unsigned slot=0;slot<9;++slot) {
        const auto image=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(graph,image)->texture.image.id="assets/textures/ssr-slot-"+std::to_string(slot)+".png";
        const auto product=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(graph,tail,MaterialPin::Value,product,MaterialPin::Left);
        connectMaterialNodes(graph,image,MaterialPin::RGB,product,MaterialPin::Right);tail=product;
    }
    connectMaterialNodes(graph,tail,MaterialPin::Value,output,MaterialPin::BaseColor);
    const auto height=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& node){return node.kind==MaterialNodeKind::Texture2D&&node.texture.role==MaterialTextureRole::Data;});
    check(height!=graph.nodes.end(),"Focused SSR displaced fixture omitted its linear Height source.");
    findMaterialNode(graph,output)->output.tessellation.enabled=true;
    connectMaterialNodes(graph,height->id,MaterialPin::R,output,MaterialPin::Height);
    const auto displaced=generateMaterialGraphShader(maximum);
    check(displaced.textures.size()==12&&displaced.usesNormal&&displaced.tessellation.enabled,
        "Focused SSR fixture lost twelve textures, normal mapping or displaced geometry.");
    compileReceivers("Twelve-texture normals and Height",maximum,false);
    check(reflectionFragments==10&&domainStages==1,"Focused SSR compile matrix changed its intended coverage.");
    std::cout<<"Focused SSR compiled "<<reflectionFragments<<" receiver fragments, two capture fragments, two glass transmission fragments and "<<domainStages<<" native domain stage.\n";
}
void compileChecks(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    {
        std::ifstream input(source/"material_graph_depth.sh");check(bool(input),"Tessellated coverage template is unavailable.");
        const std::string depth{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        check(depth.find("#if CC_GRAPH_TESSELLATION")!=std::string::npos&&
            depth.find("context.normal = ccGraphTessellatedNormal(v_worldPosition, v_localPosition, context.normal, u_modelOptions.w, context.displacementFrame)")!=std::string::npos,
            "Depth/picking coverage does not consume the shared displaced smooth-normal frame.");
    }
    const auto root=std::filesystem::temp_directory_path()/("crimson-graph-shaders-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;const auto resolved=std::filesystem::weakly_canonical(path,ec);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ec);if(!ec&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-graph-shaders-"))std::filesystem::remove_all(resolved,ec);}} cleanup{root};
    const ShaderCompileConfig config{compiler,source,include,root};
    const auto grunge=grungeAdjustmentFixture();
    const auto grungeShader=generateMaterialGraphShader(grunge);
    const auto compileGenerated=[&](const MaterialGraphShader& generated) {
        for(const auto& variant:generated.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,generated,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Grunge/image adjustments: "+diagnostic);
        }
    };
    {
        HeightBlendFixture fixture;findMaterialNode(fixture.material.graph,fixture.references[1])->blendLayer.heightBlend=true;
        findMaterialNode(fixture.material.graph,fixture.references[2])->blendLayer.heightBlend=true;
        findMaterialNode(fixture.material.graph,fixture.references[3])->blendLayer.heightBlend=true;
        compileGenerated(generateMaterialGraphShader(fixture.material,fixture.snapshots()));
        compileGenerated(generateMaterialGraphPreviewShader(fixture.material,{fixture.references[1],MaterialPin::Surface},fixture.snapshots()));
    }
    compileStylization(config);compileImageStyle(config);
    {
        auto material=refractionFixture();const auto ordinary=generateMaterialGraphShader(material);compileGenerated(ordinary);
        // The matte transport target is a required part of the transmission
        // contract, even when shaderc strips DXBC resource reflection.
        const auto& variant=ordinary.variants[5];const auto vertex=compileShader(config,variant.vertexName);
        std::ifstream input(source/"material_graph_transmission.sh");check(bool(input),"Transmission target test template is unavailable.");
        std::string fragmentTemplate{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        const auto body=fragmentTemplate.find('{',fragmentTemplate.find("void main()"));
        check(body!=std::string::npos,"Transmission target test lost its entry point.");
        fragmentTemplate.insert(body+1,"\n    vec4 discardedMatte;\n");
        const std::string target="gl_FragData[1]";std::size_t cursor{};
        while((cursor=fragmentTemplate.find(target,cursor))!=std::string::npos){fragmentTemplate.replace(cursor,target.size(),"discardedMatte");cursor+=14;}
        auto corrupt=materialGraphShaderRecipe(config,ordinary,variant);
        const std::string includeTemplate="#include \"material_graph_transmission.sh\"";
        const auto includeAt=corrupt.source.find(includeTemplate);check(includeAt!=std::string::npos,"Transmission target test lost its fragment include.");
        corrupt.source.replace(includeAt,includeTemplate.size(),fragmentTemplate);
        const auto fragment=compileShader(config,corrupt);std::string diagnostic;
        check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic)&&diagnostic.find("matte")!=std::string::npos,
            "Generated transmission admitted a missing matte render target.");
        materialOutputNode(material.graph).output.tessellation.enabled=true;
        const auto generated=generateMaterialGraphShader(material);
        compileGenerated(generated);
        for(const auto& variant:generated.variants)if(variant.tessellation)
            check(!compileShader(config,materialGraphDomainShaderRecipe(config,generated,variant)).bytes.empty(),"Refractive mesh domain stage failed native compilation.");
    }
    for(const auto mode:{MaterialUvMode::UV0,MaterialUvMode::UV1,MaterialUvMode::LocalTriplanar,MaterialUvMode::WorldTriplanar})for(bool cached:{false,true}){
        const auto material=decodedNormalFixture(mode,cached);compileGenerated(generateMaterialGraphShader(material));
        const auto normal=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::DecodeNormal;})->id;compileGenerated(generateMaterialGraphPreviewShader(material,{normal,MaterialPin::Normal}));
    }
    {
        const auto material=atomicImageFixture();const auto generated=generateMaterialGraphShader(material);compileGenerated(generated);
        for(const auto& variant:generated.variants)if(variant.tessellation)check(!compileShader(config,materialGraphDomainShaderRecipe(config,generated,variant)).bytes.empty(),"Atomic cached/point Height failed native domain compilation.");
        for(const auto& node:material.graph.nodes)if(node.kind==MaterialNodeKind::ImageInput||node.kind==MaterialNodeKind::DecodeNormal||node.kind==MaterialNodeKind::HistogramScan||node.kind==MaterialNodeKind::MaskAdjust||node.kind==MaterialNodeKind::DirectionSelect)for(const auto& pin:materialNodeOutputs(material.graph,node))compileGenerated(generateMaterialGraphPreviewShader(material,{node.id,pin.pin}));
    }
    {
        const auto material=resizeFixture();const auto generated=generateMaterialGraphShader(material);
        compileGenerated(generated);
        for(const auto& variant:generated.variants)if(variant.tessellation)
            check(!compileShader(config,materialGraphDomainShaderRecipe(config,generated,variant)).bytes.empty(),
                "A materialized Resize texture failed native Height/coverage compilation.");
        const auto resize=std::find_if(material.graph.nodes.begin(),material.graph.nodes.end(),
            [](const auto& node){return node.kind==MaterialNodeKind::Resize;})->id;
        compileGenerated(generateMaterialGraphPreviewShader(material,{resize,MaterialPin::Value}));
        const auto mixed=mixedResizeFixture();const auto mixedShader=generateMaterialGraphShader(mixed);
        compileGenerated(mixedShader);
        for(const auto& variant:mixedShader.variants)if(variant.tessellation)
            check(!compileShader(config,materialGraphDomainShaderRecipe(config,mixedShader,variant)).bytes.empty(),
                "Mixed RGB/RGBA Resize branches failed native Height compilation.");
        for(const auto& node:mixed.graph.nodes)if(node.kind==MaterialNodeKind::Resize||
            node.kind==MaterialNodeKind::Sharpen||node.kind==MaterialNodeKind::HeightToNormal)
            compileGenerated(generateMaterialGraphPreviewShader(mixed,
                {node.id,node.kind==MaterialNodeKind::HeightToNormal?MaterialPin::Normal:MaterialPin::Value}));
    }
    {
        auto imageMaterial=imageFoundationFixture();compileGenerated(generateMaterialGraphShader(imageMaterial));
        for(const auto& n:imageMaterial.graph.nodes)if(n.kind>=MaterialNodeKind::Levels){const auto pins=materialNodeOutputs(imageMaterial.graph,n);for(const auto& pin:pins) {const auto preview=generateMaterialGraphPreviewShader(imageMaterial,{n.id,pin.pin});check(!compileShader(config,materialGraphShaderRecipe(config,preview,preview.variants.front())).bytes.empty(),"An image node/output preview failed to compile.");}}
        auto& output=materialOutputNode(imageMaterial.graph);output.output.tessellation.enabled=true;const auto gray=std::find_if(imageMaterial.graph.nodes.begin(),imageMaterial.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Grayscale;})->id;connectMaterialNodes(imageMaterial.graph,gray,MaterialPin::Value,output.id,MaterialPin::Height);
        const auto generated=generateMaterialGraphShader(imageMaterial);for(const auto& variant:generated.variants)if(variant.tessellation){check(!compileShader(config,materialGraphDomainShaderRecipe(config,generated,variant)).bytes.empty(),"Image nodes in a native Height/domain branch failed to compile.");}
    }
    compileGenerated(grungeShader);
    for(const auto& node:grunge.graph.nodes)if(materialNodeImageAdjustment(node.kind)||node.kind==MaterialNodeKind::Grunge)
        compileGenerated(generateMaterialGraphPreviewShader(grunge,{node.id,node.kind==MaterialNodeKind::Grunge||node.kind==MaterialNodeKind::Contrast?MaterialPin::Value:MaterialPin::RGB}));
    for(const auto& node:grunge.graph.nodes)if(node.kind==MaterialNodeKind::Grunge||node.kind==MaterialNodeKind::Contrast)
        compileGenerated(generateMaterialGraphPreviewShader(grunge,{node.id,MaterialPin::RGB}));
    MaterialAsset importedBlend;importedBlend.id="imported-blend-fixture";importedBlend.name="Imported BLEND";
    importedBlend.surface.alphaBlend=true;importedBlend.surface.baseColor={.3f,.4f,.5f,.35f};
    importedBlend.surface.baseColorTexture.image.id="assets/models/imported/images/alpha.png";
    canonicalizeMaterialAsset(importedBlend);
    const auto importedGraph=generateMaterialGraphShader(importedBlend);
    check(importedBlend.blend==MaterialBlend::Alpha&&importedGraph.uniforms[3][3]==2,
        "Imported glTF BLEND did not reach the transparent material shader contract.");
    for(const auto& variant:importedGraph.variants) {
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,importedGraph,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Imported BLEND material: "+diagnostic);
    }
    auto animated=animatedFixture();
    for(const auto& variant:generateMaterialGraphShader(animated).variants) {
        const auto generated=generateMaterialGraphShader(animated);
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,generated,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))throw std::runtime_error("Animated graph: "+diagnostic);
    }
    for(const auto& node:animated.graph.nodes)if(node.kind==MaterialNodeKind::Add||node.kind==MaterialNodeKind::Lerp||node.kind==MaterialNodeKind::Power||
        node.kind==MaterialNodeKind::Panner||node.kind==MaterialNodeKind::Time) {
        const auto generated=generateMaterialGraphPreviewShader(animated,{node.id,node.kind==MaterialNodeKind::Panner?MaterialPin::UV:MaterialPin::Value});
        check(!compileShader(config,materialGraphShaderRecipe(config,generated,generated.variants.front())).bytes.empty(),"New node preview did not compile.");
    }
    const auto vectorPower=addMaterialNode(animated.graph,MaterialNodeKind::Power).id;
    findMaterialNode(animated.graph,vectorPower)->left={MaterialValueType::Vector2,{2,3,0,0}};
    const auto vectorPreview=generateMaterialGraphPreviewShader(animated,{vectorPower,MaterialPin::Value});
    check(!compileShader(config,materialGraphShaderRecipe(config,vectorPreview,vectorPreview.variants.front())).bytes.empty(),"Vector2 Power preview did not compile.");
    auto material=fixture();
    for(bool unlit:{false,true}) for(auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        auto& output=materialOutputNode(material.graph).output;
        output.shading=unlit?MaterialShading::Unlit:MaterialShading::StandardPbr;
        output.blend=blend;
        const auto graph=generateMaterialGraphShader(material);
        for(const auto& variant:graph.variants) {
            const auto recipe=materialGraphShaderRecipe(config,graph,variant);
            const auto fragment=compileShader(config,recipe);
            const auto vertex=compileShader(config,variant.vertexName);
            std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error(variant.vertexName+": "+diagnostic);
            const auto timestamps=cacheTimes(root);const auto warm=compileShader(config,recipe);
            check(warm.fingerprint==fragment.fingerprint && warm.bytes==fragment.bytes,"Generated shader warm cache changed.");
            check(cacheTimes(root)==timestamps,"Generated shader warm hit rewrote the cache instead of reusing it.");
            if(variant.pass==MaterialGraphPass::Model) {
                auto wrong=variant.manifest;
                const auto binding=std::find_if(wrong.begin(),wrong.end(),[](const auto& b){return b.name==materialGraphSamplerName(0);});
                binding->samplerSlot=11;
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),"Generated manifest accepted a wrong sampler slot.");
                wrong=variant.manifest;
                const auto dimension=std::find_if(wrong.begin(),wrong.end(),[](const auto& b){return b.name==materialGraphSamplerName(0);});
                dimension->textureDimension=ShaderTextureDimension::Cube;
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),"Generated manifest accepted a wrong texture dimension.");
                wrong=variant.manifest;
                std::erase_if(wrong,[](const auto& b){return b.name==MaterialGraphParameterUniform;});
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),"Generated manifest accepted undeclared parameters.");
                wrong=variant.manifest;
                std::erase_if(wrong,[](const auto& b){return b.name=="u_ssgiDraw";});
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                    "Generated manifest accepted an obsolete source-capture/reactive-depth contract.");
                wrong=variant.manifest;
                std::erase_if(wrong,[](const auto& b){return b.name=="u_fogSky"||b.name=="u_fogSunDirection"||b.name=="u_fogSunColor";});
                check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                    "Generated manifest accepted an obsolete fog sky/scattering contract.");
                if(!unlit) {
                    wrong=variant.manifest;
                    std::erase_if(wrong,[](const auto& b){return b.name=="u_ssgiAtlas";});
                    check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrong,diagnostic),
                        "Generated manifest accepted an obsolete GI atlas receiver contract.");
                }
            }
        }
    }
    // A material map edit recompiles against the live engine include tree.
    // Every current Character uniform must be allowed by the compiled engine
    // manifest; a pre-separation manifest must reject that same binary instead
    // of silently admitting an interface that the engine cannot bind.
    MaterialAsset editedMaps;editedMaps.id="character-lighting-map-edits";
    const auto editedOutput=addMaterialNode(editedMaps.graph,MaterialNodeKind::Output).id;
    MaterialNodeId editedColor{},editedNormal{};std::string texturedTopology;
    for(unsigned revision=0;revision<4;++revision) {
        if(revision==1) {
            editedColor=addMaterialNode(editedMaps.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(editedMaps.graph,editedColor)->texture.image.id="assets/textures/first-color.png";
            connectMaterialNodes(editedMaps.graph,editedColor,MaterialPin::RGB,editedOutput,MaterialPin::BaseColor);
        } else if(revision==2) {
            editedNormal=addMaterialNode(editedMaps.graph,MaterialNodeKind::Texture2D).id;
            findMaterialNode(editedMaps.graph,editedNormal)->texture.role=MaterialTextureRole::Normal;
            findMaterialNode(editedMaps.graph,editedNormal)->texture.image.id="assets/textures/first-normal.png";
            connectMaterialNodes(editedMaps.graph,editedNormal,MaterialPin::Normal,editedOutput,MaterialPin::Normal);
        } else if(revision==3) {
            findMaterialNode(editedMaps.graph,editedColor)->texture.image.id="assets/textures/reimported-color.png";
            findMaterialNode(editedMaps.graph,editedNormal)->texture.image.id.clear();
        }
        const auto edited=generateMaterialGraphShader(editedMaps);
        if(revision==2)texturedTopology=edited.topologyKey;
        if(revision==3)check(edited.topologyKey==texturedTopology,"Replacing/clearing map assignments changed material topology.");
        for(const auto& variant:edited.variants) {
            if(variant.pass!=MaterialGraphPass::Model&&variant.pass!=MaterialGraphPass::Particle)continue;
            const auto binding=[&](const char* name,ShaderUniformKind kind,unsigned count) {
                const auto found=std::find_if(variant.manifest.begin(),variant.manifest.end(),[&](const auto& item){return item.name==name;});
                check(found!=variant.manifest.end()&&found->kind==kind&&found->count==count,"Scene lighting uniform manifest changed shape.");
            };
            for(const char* name:{"u_surfaceCategory","u_clusterRecordLayoutV6","u_ambientLight"})binding(name,ShaderUniformKind::Vec4,1);
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,edited,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Material map edit revision "+std::to_string(revision)+": "+diagnostic);
            auto oldManifest=variant.manifest;
            std::erase_if(oldManifest,[](const auto& item){return item.name=="u_directionColor";});
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,oldManifest,diagnostic)&&diagnostic.find("u_directionColor")!=std::string::npos,
                "A material map edit accepted the missing directional color uniform.");
            oldManifest=variant.manifest;
            std::find_if(oldManifest.begin(),oldManifest.end(),[](const auto& item){return item.name=="u_clusterRecordLayoutV6";})->name="u_clusterRecordLayoutV5";
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,oldManifest,diagnostic)&&diagnostic.find("u_clusterRecordLayoutV6")!=std::string::npos,
                "A material map edit accepted a light record ABI that the previous engine cannot bind.");
            auto wrongArray=variant.manifest;
            std::find_if(wrongArray.begin(),wrongArray.end(),[](const auto& item){return item.name=="u_directionColor";})->count=1;
            check(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,wrongArray,diagnostic),
                "A material map edit accepted an undersized directional uniform array.");
        }
    }
    auto rgbScalar=rgbScalarFixture();
    const auto rgbOutput=materialOutputNode(rgbScalar.graph).id;
    const auto maskSource=*materialInputLink(rgbScalar.graph,rgbOutput,MaterialPin::OpacityMask);
    for(const auto blend:{MaterialBlend::Mask,MaterialBlend::Alpha}) {
        materialOutputNode(rgbScalar.graph).output.blend=blend;
        if(blend==MaterialBlend::Alpha)connectMaterialNodes(rgbScalar.graph,maskSource.fromNode,MaterialPin::RGB,rgbOutput,MaterialPin::Opacity);
        const auto graph=generateMaterialGraphShader(rgbScalar);
        for(const auto& variant:graph.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,graph,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("RGB scalar material inputs: "+variant.vertexName+": "+diagnostic);
        }
    }
    material=fixture();
    for(const auto& node:material.graph.nodes) {
        if(node.kind==MaterialNodeKind::Output)continue;
        const auto pin=materialNodeOutputs(material.graph,node).front().pin;
        const auto preview=generateMaterialGraphPreviewShader(material,{node.id,pin});
        const auto& variant=preview.variants.front();
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,preview,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Node preview: "+diagnostic);
    }
    {
        const auto vector=addMaterialNode(material.graph,MaterialNodeKind::Vector2).id;
        const auto preview=generateMaterialGraphPreviewShader(material,{vector,MaterialPin::XY});
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,preview,preview.variants.front()));
        check(!fragment.bytes.empty(),"Vector2 preview did not compile.");
    }
    {
        MaterialAsset explicitSprite;const auto output=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Output).id;
        materialOutputNode(explicitSprite.graph).output.blend=MaterialBlend::Alpha;
        const auto color=addMaterialNode(explicitSprite.graph,MaterialNodeKind::ParticleColor).id;
        const auto radial=addMaterialNode(explicitSprite.graph,MaterialNodeKind::RadialGradient).id;
        const auto multiply=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Multiply).id;
        const auto saturate=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Clamp).id;
        findMaterialNode(explicitSprite.graph,saturate)->left.value[0]=2;
        const auto alphaMultiply=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(explicitSprite.graph,saturate,MaterialPin::Value,alphaMultiply,MaterialPin::Left);
        connectMaterialNodes(explicitSprite.graph,color,MaterialPin::A,alphaMultiply,MaterialPin::Right);
        connectMaterialNodes(explicitSprite.graph,color,MaterialPin::RGB,output,MaterialPin::BaseColor);
        connectMaterialNodes(explicitSprite.graph,alphaMultiply,MaterialPin::Value,multiply,MaterialPin::Left);
        connectMaterialNodes(explicitSprite.graph,radial,MaterialPin::Value,multiply,MaterialPin::Right);
        connectMaterialNodes(explicitSprite.graph,multiply,MaterialPin::Value,output,MaterialPin::Opacity);
        const auto explicitGraph=generateMaterialGraphShader(explicitSprite);
        check(explicitGraph.variants.front().source.find("context.particleColor")!=std::string::npos &&
            explicitGraph.variants.front().source.find("ccGraphRadialGradient")!=std::string::npos,"Explicit particle inputs were not lowered.");
        for(const auto& variant:explicitGraph.variants) {
            const auto fragment=compileShader(config,materialGraphShaderRecipe(config,explicitGraph,variant));
            const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
            if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
                throw std::runtime_error("Explicit particle graph: "+diagnostic);
        }
        for(const auto target:{MaterialGraphPreviewTarget{color,MaterialPin::RGB},MaterialGraphPreviewTarget{radial,MaterialPin::Value},MaterialGraphPreviewTarget{saturate,MaterialPin::Value}}) {
            const auto preview=generateMaterialGraphPreviewShader(explicitSprite,target);
            check(!compileShader(config,materialGraphShaderRecipe(config,preview,preview.variants.front())).bytes.empty(),"Particle input node preview failed to compile.");
        }
    }
    auto maximum=fixture();auto& maxGraph=maximum.graph;
    auto maxTail=materialInputLink(maxGraph,materialOutputNode(maxGraph).id,MaterialPin::BaseColor)->fromNode;
    for(int i=0;i<9;++i) {
        const auto image=addMaterialNode(maxGraph,MaterialNodeKind::Texture2D).id;
        const auto product=addMaterialNode(maxGraph,MaterialNodeKind::Multiply).id;
        connectMaterialNodes(maxGraph,maxTail,MaterialPin::Value,product,MaterialPin::Left);
        connectMaterialNodes(maxGraph,image,MaterialPin::RGB,product,MaterialPin::Right);maxTail=product;
    }
    connectMaterialNodes(maxGraph,maxTail,MaterialPin::Value,materialOutputNode(maxGraph).id,MaterialPin::BaseColor);
    const auto full=generateMaterialGraphShader(maximum);check(full.textures.size()==12,"Maximum-budget fixture lost active textures.");
    const auto& fullVariant=full.variants[0];const auto fullFragment=compileShader(config,materialGraphShaderRecipe(config,full,fullVariant));
    const auto fullVertex=compileShader(config,fullVariant.vertexName);std::string fullDiagnostic;
    if(!preflightGeneratedShaderPair(fullVertex.bytes,fullFragment.bytes,fullVertex.bytes,fullVariant.manifest,fullDiagnostic))
        throw std::runtime_error("Maximum texture budget: "+fullDiagnostic);
    materialOutputNode(maxGraph).output.blend=MaterialBlend::Alpha;materialOutputNode(maxGraph).output.refraction=true;
    connectMaterialNodes(maxGraph,maxTail,MaterialPin::Value,materialOutputNode(maxGraph).id,MaterialPin::Ior);
    const auto glassBudget=generateMaterialGraphShader(maximum);check(glassBudget.textures.size()==12,"Refraction reduced the material texture budget.");
    const auto& glassVariant=glassBudget.variants[5];const auto glassFragment=compileShader(config,materialGraphShaderRecipe(config,glassBudget,glassVariant));
    if(!preflightGeneratedShaderPair(fullVertex.bytes,glassFragment.bytes,fullVertex.bytes,glassVariant.manifest,fullDiagnostic))
        throw std::runtime_error("Maximum transmission texture budget: "+fullDiagnostic);
    MaterialAsset constant;constant.graph=materialGraphFromLegacy(constant.surface,MaterialShading::Unlit,MaterialBlend::Alpha);
    const auto constantGraph=generateMaterialGraphShader(constant);
    check(constantGraph.textures.empty(),"Constant material unexpectedly allocated texture bindings.");
    for(const auto& variant:constantGraph.variants) {
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,constantGraph,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Constant material: "+diagnostic);
    }
    // Uniform/sampler manifests alone cannot distinguish a single-target
    // fragment from one that publishes coherent diffuse source attachments.
    const auto mrtGraph=generateMaterialGraphShader(defaultMaterialAsset());
    const auto& mrtVariant=mrtGraph.variants.front();
    const auto mrtVertex=compileShader(config,mrtVariant.vertexName);
    const auto mrtFragment=compileShader(config,materialGraphShaderRecipe(config,mrtGraph,mrtVariant));
    std::ifstream mrtTemplateInput(source/"material_graph_model.sh");
    check(bool(mrtTemplateInput),"SSGI render target test template is unavailable.");
    const std::string mrtTemplate{std::istreambuf_iterator<char>(mrtTemplateInput),std::istreambuf_iterator<char>()};
    for(unsigned corruption:{0u,1u}) {
        const bool missingTargets=corruption==1;
        auto changed=mrtTemplate;
        if(missingTargets) {
            const auto body=changed.find('{',changed.find("void main()"));
            check(body!=std::string::npos,"SSGI render target test lost its entry point.");
            changed.insert(body+1,"\n    vec4 discardedWorld, discardedCharacter, discardedGeometry;\n");
        }
        for(unsigned target=missingTargets?1u:3u;target<=3u;++target) {
            const auto before="gl_FragData["+std::to_string(target)+"]";
            const std::string after=missingTargets?(target==1?"discardedWorld":target==2?"discardedCharacter":"discardedGeometry"):
                "gl_FragData[4]";
            std::size_t at{};
            while((at=changed.find(before,at))!=std::string::npos){changed.replace(at,before.size(),after);at+=after.size();}
        }
        auto recipe=materialGraphShaderRecipe(config,mrtGraph,mrtVariant);
        const std::string includeTemplate="#include \"material_graph_model.sh\"";
        const auto at=recipe.source.find(includeTemplate);
        check(at!=std::string::npos,"SSGI render target test lost its model template include.");
        recipe.source.replace(at,includeTemplate.size(),changed);
        const auto fragment=compileShader(config,recipe);std::string diagnostic;
        check(!preflightGeneratedShaderPair(mrtVertex.bytes,fragment.bytes,mrtVertex.bytes,mrtVariant.manifest,diagnostic)&&
            diagnostic.find("render target")!=std::string::npos,
            "Generated material admitted missing or reassigned lighting render targets.");
        check(!preflightShaderPair(mrtVertex.bytes,fragment.bytes,mrtVertex.bytes,mrtFragment.bytes,diagnostic),
            "Shader reload admitted a changed fragment render target signature.");
    }
    std::stop_source canceled;canceled.request_stop();bool rejected=false;
    const auto graph=generateMaterialGraphShader(material);
    try{compileShader(config,materialGraphShaderRecipe(config,graph,graph.variants[0]),canceled.get_token());}catch(const std::exception&){rejected=true;}
    check(rejected,"Canceled generated compilation returned a candidate.");
    auto recipe=materialGraphShaderRecipe(config,graph,graph.variants[0]);
    const auto dependency=root/"recipe_dependency.sh";
    recipe.source+="\n#include \"recipe_dependency.sh\"\n";
    recipe.includeDirectories.insert(recipe.includeDirectories.begin(),root);
    writeFileAtomically(dependency,"// first dependency revision\n");
    const auto first=compileShader(config,recipe);
    check(std::any_of(first.dependencies.begin(),first.dependencies.end(),[&](const auto& path){return path==dependency;}),"Generated recipe lost an explicit include dependency.");
    writeFileAtomically(dependency,"// second dependency revision\n");
    const auto second=compileShader(config,recipe);
    check(first.fingerprint!=second.fingerprint,"Generated recipe ignored an included-source edit.");
    writeFileAtomically(dependency,"invalid shader source !\n");
    rejected=false;try{compileShader(config,recipe);}catch(const std::exception&){rejected=true;}
    check(rejected,"Invalid shader source published a successful compilation.");
    writeFileAtomically(dependency,"// second dependency revision\n");
    const auto recovered=compileShader(config,recipe);
    check(recovered.bytes==second.bytes,"A failed compilation damaged the last valid cached shader.");
}
}
int main(int argc,char** argv) {
    try {
        heightBlendMathChecks();heightBlendShaderChecks();imageStyleShaderChecks();stylizationShaderChecks();constantRoughnessEligibilityChecks();decodedNormalMappingChecks();atomicShaderChecks();resizeShaderChecks();imageFoundationShaderChecks();refractionShaderChecks();cpuChecks();tessellatedCoverageNormalChecks();runtimeUniformChecks();adjustmentChecks();passIdentityChecks();
        if(argc==4)compileChecks(argv[1],argv[2],argv[3]);
        else if(argc==5&&std::string_view(argv[1])=="--ssr")ssrCompileChecks(argv[2],argv[3],argv[4]);
        else if(argc==5&&std::string_view(argv[1])=="--stylization")stylizationCompileChecks(argv[2],argv[3],argv[4]);
        else if(argc==5&&std::string_view(argv[1])=="--image-style")imageStyleCompileChecks(argv[2],argv[3],argv[4]);
        else if(argc==5&&std::string_view(argv[1])=="--detail-balance")imageStyleCompileChecks(argv[2],argv[3],argv[4],true);
        else if(argc==5&&std::string_view(argv[1])=="--blend-height")heightBlendCompileChecks(argv[2],argv[3],argv[4]);
        else if(argc!=1)throw std::runtime_error("Usage: material_graph_shader_tests [--ssr|--stylization|--image-style|--detail-balance|--blend-height] <shaderc> <source-dir> <include-dir>");
        std::cout<<"Material graph lowering, packets, limits and generated shader checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
