#include "assets/MaterialAsset.h"
#include "crimson-renderer/MaterialGraphShader.h"
#include "assets/AtomicFile.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
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
    const auto color=addMaterialNode(graph,MaterialNodeKind::Color).id,product=addMaterialNode(graph,MaterialNodeKind::Multiply).id,saturate=addMaterialNode(graph,MaterialNodeKind::Saturate).id;
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
void cpuChecks() {
    auto material=fixture();const auto original=generateMaterialGraphShader(material);
    check(original.variants.size()==4 && original.textures.size()==3 && original.usesNormal,"Reachable graph bindings/passes were not lowered.");
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
    connectMaterialNodes(graph,tail,MaterialPin::Value,materialOutputNode(graph).id,MaterialPin::BaseColor);
    bool rejected=false;try{generateMaterialGraphShader(overloaded);}catch(const std::exception&){rejected=true;}
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
        for(const auto name:{"roughness","metallic","specular"}) {
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
std::map<std::filesystem::path,std::filesystem::file_time_type> cacheTimes(const std::filesystem::path& root) {
    std::map<std::filesystem::path,std::filesystem::file_time_type> result;
    for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_regular_file())result.emplace(entry.path(),entry.last_write_time());
    return result;
}
void compileChecks(const std::filesystem::path& compiler,const std::filesystem::path& source,const std::filesystem::path& include) {
    const auto root=std::filesystem::temp_directory_path()/("crimson-graph-shaders-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;const auto resolved=std::filesystem::weakly_canonical(path,ec);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),ec);if(!ec&&resolved.parent_path()==parent&&resolved.filename().string().starts_with("crimson-graph-shaders-"))std::filesystem::remove_all(resolved,ec);}} cleanup{root};
    const ShaderCompileConfig config{compiler,source,include,root};
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
            }
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
        const auto saturate=addMaterialNode(explicitSprite.graph,MaterialNodeKind::Saturate).id;
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
    MaterialAsset constant;constant.graph=materialGraphFromLegacy(constant.surface,MaterialShading::Unlit,MaterialBlend::Alpha);
    const auto constantGraph=generateMaterialGraphShader(constant);
    check(constantGraph.textures.empty(),"Constant material unexpectedly allocated texture bindings.");
    for(const auto& variant:constantGraph.variants) {
        const auto fragment=compileShader(config,materialGraphShaderRecipe(config,constantGraph,variant));
        const auto vertex=compileShader(config,variant.vertexName);std::string diagnostic;
        if(!preflightGeneratedShaderPair(vertex.bytes,fragment.bytes,vertex.bytes,variant.manifest,diagnostic))
            throw std::runtime_error("Constant material: "+diagnostic);
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
        cpuChecks();if(argc==4)compileChecks(argv[1],argv[2],argv[3]);
        std::cout<<"Material graph lowering, packets, limits and generated shader checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
