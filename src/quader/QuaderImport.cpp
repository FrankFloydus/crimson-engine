#include "QuaderImport.h"
#include "assets/ContentStorage.h"
#include "scene/Scene.h"
#include "assets/AtomicFile.h"
#include "assets/ModelImportIdentity.h"
#include "resources/Paths.h"
#include "resources/AssetId.h"
#include "resources/StreamingModel.h"
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
void require(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
void canceled(std::stop_token token){if(token.stop_requested())throw std::runtime_error("Quader import canceled.");}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);const auto count=input.tellg();require(input&&count>=0&&count<=512ll*1024*1024,"Cannot read managed Quader asset.");
    std::string out(static_cast<std::size_t>(count),'\0');input.seekg(0);if(!out.empty())input.read(out.data(),static_cast<std::streamsize>(out.size()));require(static_cast<bool>(input),"Managed Quader asset changed while reading.");return out;
}
std::string digest(const std::string& text){return modelImportDataDigest({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});}
void omitNames(Json& value){
    if(value.is_object()){value.erase("name");for(auto& child:value)omitNames(child);}
    else if(value.is_array())for(auto& child:value)omitNames(child);
}
std::string visualIdentity(Json document,Json materials){
    omitNames(document);for(auto& id:materials)id=canonicalAssetId(id.get<std::string>());
    return document.dump()+"\n"+materials.dump();
}
std::string identity(const QuaderSourceScene& scene,const QuaderSourceTexture& texture,std::size_t slot,bool directX){
    return scene.project.id+"|"+texture.id+"|"+(texture.id.empty()?scene.id+"|"+texture.path:"")+"|"+(slot==3?"normal":slot==0?"color":"data")+"|"+std::to_string(texture.srgb)+"|"+std::to_string(slot==3&&directX);
}
float scaled(float value,float factor){const double result=double(value)*factor;require(std::isfinite(result)&&std::abs(result)<=std::numeric_limits<float>::max(),"Import Scale exceeds Crimson's numeric range.");return static_cast<float>(result);}
Vec3 scaled(const Vec3& value,float factor){return {scaled(value[0],factor),scaled(value[1],factor),scaled(value[2],factor)};}
Mat4 placement(const Mat4& source,float factor){auto value=source;for(std::size_t i=12;i<15;++i)value[i]=scaled(source[i],factor);require(math::finite(value)&&math::isAffine(value),"Quader placement is invalid.");return value;}
void triangle(const Vec3& a,const Vec3& b,const Vec3& c,float factor,const Mat4& transform){
    const auto x=scaled(a,factor),y=scaled(b,factor),z=scaled(c,factor);const auto before=math::cross(b-a,c-a),after=math::cross(y-x,z-x);
    require(math::finite(after)&&(math::length(before)==0||math::length(after)>0)&&math::finite(math::transformPoint(transform,x))&&
        math::finite(math::transformPoint(transform,y))&&math::finite(math::transformPoint(transform,z)),"Import Scale collapses or overflows Quader geometry.");
}
std::string png(const QuaderSourceTexture& texture,std::size_t slot,bool directX){
    require(!texture.bytes.empty()&&texture.bytes.size()<=UINT32_MAX,"Invalid Quader texture bytes.");bx::DefaultAllocator allocator;bx::Error error;
    std::unique_ptr<bimg::ImageContainer,decltype(&bimg::imageFree)> image(bimg::imageParse(&allocator,texture.bytes.data(),static_cast<std::uint32_t>(texture.bytes.size()),bimg::TextureFormat::Count,&error),bimg::imageFree);
    require(error.isOk()&&image&&image->m_width&&image->m_height&&image->m_width<=16384&&image->m_height<=16384&&image->m_depth<=1&&image->m_numLayers==1&&!image->m_cubeMap,"Cannot decode a supported 2D Quader texture.");
    const auto sourceFormat=image->m_format;const bool gray=sourceFormat==bimg::TextureFormat::R8||sourceFormat==bimg::TextureFormat::R16||sourceFormat==bimg::TextureFormat::RG8||sourceFormat==bimg::TextureFormat::RG16;
    const bool alpha=sourceFormat==bimg::TextureFormat::RG8||sourceFormat==bimg::TextureFormat::RG16;
    if(image->m_format!=bimg::TextureFormat::RGBA8){auto* converted=bimg::imageConvert(&allocator,bimg::TextureFormat::RGBA8,*image,false);require(converted!=nullptr,"Cannot convert Quader texture pixels.");image.reset(converted);}
    bimg::ImageMip mip{};require(bimg::imageGetRawData(*image,0,0,image->m_data,image->m_size,mip)&&mip.m_format==bimg::TextureFormat::RGBA8,"Incomplete Quader texture pixels.");
    const auto count=std::size_t(mip.m_width)*mip.m_height*4;require(count<=mip.m_size&&count<=512ull*1024*1024,"Quader texture exceeds the decoded memory limit.");std::vector<std::uint8_t> pixels(mip.m_data,mip.m_data+count);
    const bool targetSrgb=slot==0;
    for(std::size_t i=0;i<count;i+=4){if(gray){pixels[i+3]=alpha?pixels[i+1]:255;pixels[i+1]=pixels[i+2]=pixels[i];}
        if(targetSrgb!=texture.srgb)for(std::size_t channel=0;channel<3;++channel){const float value=pixels[i+channel]/255.f;
            const float converted=targetSrgb?(value<=.0031308f?value*12.92f:1.055f*std::pow(value,1/2.4f)-.055f):(value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f));
            pixels[i+channel]=static_cast<std::uint8_t>(std::clamp(std::round(converted*255.f),0.f,255.f));}
        if(slot==3&&directX)pixels[i+1]=255-pixels[i+1];}
    std::vector<unsigned char> encoded;const auto result=lodepng::encode(encoded,pixels,mip.m_width,mip.m_height);require(result==0,"Cannot encode normalized Quader texture.");return {reinterpret_cast<const char*>(encoded.data()),encoded.size()};
}
MaterialAsset materialAsset(const QuaderSourceMaterial& source,const std::string& id,const std::array<std::string,4>& images){
    MaterialAsset material;material.id=id;material.name=source.name;material.graph.nodes.reserve(24);material.graph.links.reserve(32);auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output,{650,50}).id;auto& settings=materialOutputNode(graph).output;
    settings.baseColor=source.color;settings.roughness=source.roughness;settings.metallic=source.metallic;
    const float angle=source.uvRotation*std::numbers::pi_v<float>/180.f,c=std::cos(angle),s=std::sin(angle);
    MaterialUvMapping mapping;mapping.scale={source.uvScale[0],source.uvScale[1],1};mapping.rotation={0,0,source.uvRotation};mapping.scaleLinked=source.uvScale[0]==source.uvScale[1];
    mapping.offset={source.uvCenter[0]+source.uvOffset[0]-(c*source.uvScale[0]*source.uvCenter[0]-s*source.uvScale[1]*source.uvCenter[1]),
        source.uvCenter[1]+source.uvOffset[1]-(s*source.uvScale[0]*source.uvCenter[0]+c*source.uvScale[1]*source.uvCenter[1]),0};
    const auto uv=addMaterialNode(graph,MaterialNodeKind::UV,{-650,80}).id;findMaterialNode(graph,uv)->uv=mapping;
    const auto link=[&](MaterialNodeId from,MaterialPin pin,MaterialNodeId to,MaterialPin input){graph.links.push_back({graph.nextLinkId++,from,to,pin,input});};
    for(std::size_t slot=0;slot<4;++slot){if(images[slot].empty())continue;const auto image=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-400,float(slot)*240}).id;
        auto* texture=findMaterialNode(graph,image);texture->texture.image.id=images[slot];texture->texture.role=slot==0?MaterialTextureRole::Color:slot==3?MaterialTextureRole::Normal:MaterialTextureRole::Data;
        texture->texture.normalStrength=source.normalStrength;link(uv,MaterialPin::UV,image,MaterialPin::UV);
        if(slot==3){link(image,MaterialPin::Normal,output,MaterialPin::Normal);continue;}
        const auto target=slot==0?MaterialPin::BaseColor:slot==1?MaterialPin::Roughness:MaterialPin::Metallic;
        const auto factor=addMaterialNode(graph,slot==0?MaterialNodeKind::Color:MaterialNodeKind::Scalar,{-380,float(slot)*240+105}).id;
        auto* constant=findMaterialNode(graph,factor);constant->name=std::string(materialPinName(target))+" factor";
        constant->color={source.color[0],source.color[1],source.color[2],1};constant->scalar=slot==1?source.roughness:source.metallic;
        const auto multiply=addMaterialNode(graph,MaterialNodeKind::Multiply,{0,float(slot)*240}).id;
        link(image,slot==0?MaterialPin::RGB:MaterialPin::R,multiply,MaterialPin::Left);
        link(factor,slot==0?MaterialPin::RGB:MaterialPin::Value,multiply,MaterialPin::Right);link(multiply,MaterialPin::Value,output,target);}
    canonicalizeMaterialAsset(material);validateMaterialAsset(material);return material;
}
struct Preparation {
    struct Package {std::string id,gltf,buffer,manifest,identity;std::array<std::string,4> inputs;};
    const QuaderSourceScene& scene;const QuaderPrepareOptions& options;std::stop_token stop;QuaderImportResult out;
    std::unordered_map<std::string,std::size_t> fileIndexes,inputIndexes;std::unordered_map<std::string,MaterialAsset> materials;std::filesystem::path stage;
    std::unordered_map<std::string,std::vector<Package>> packages;
    std::unordered_set<std::string> preparedPackages;bool indexedPackages{};
    explicit Preparation(const QuaderSourceScene& source,const QuaderPrepareOptions& settings,std::stop_token token):scene(source),options(settings),stop(token){
        static std::atomic<std::uint64_t> sequence{};const auto id=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
        stage=resolveAssetPath(options.projectRoot,"assets/.staging-quader-"+id);std::filesystem::create_directories(stage);
        for(const auto& [id,material]:options.existingMaterials)materials[canonicalAssetId(id)]=material;
        auto fallback=defaultMaterialAsset();canonicalizeMaterialAsset(fallback);materials[DefaultMaterialId]=std::move(fallback);
    }
    std::string publishedId(const std::string& id)const{return canonicalAssetId(id);}
    ~Preparation(){try{const auto target=std::filesystem::weakly_canonical(stage),parent=std::filesystem::weakly_canonical(resolveAssetPath(options.projectRoot,"assets/.staging-quader").parent_path());
        if(target.parent_path()!=parent||!target.filename().string().starts_with(".staging-quader-"))return;
        std::error_code ignored;std::filesystem::remove_all(target,ignored);}catch(...){/* Never widen cleanup after a path-resolution failure. */}}
    void input(const std::string& id,const std::optional<std::string>& prior){
        const auto fingerprint=prior?std::optional{digest(*prior)}:std::nullopt;
        if(const auto found=inputIndexes.find(id);found!=inputIndexes.end()){
            require(out.materialInputs[found->second].digest==fingerprint,"Crimson material assets changed during Quader preparation. Import again.");return;}
        inputIndexes.emplace(id,out.materialInputs.size());out.materialInputs.push_back({id,fingerprint});
    }
    void reuseMaterial(const std::string& id){
        const auto path=checkedManagedAssetPath(options.projectRoot,id);const auto prior=bytes(path);input(id,prior);
        auto material=loadMaterialAsset(options.projectRoot,id);
        require(digest(bytes(path))==digest(prior),"Crimson material changed during Quader preparation. Import again.");
        materials[publishedId(id)]=std::move(material);
    }
    std::string availableFileId(const std::string& original)const{
        return availableQuaderAssetId(original,options.reservedIds);
    }
    void file(const std::string& id,std::string data,bool preserve=false,bool immutable=false){
        canceled(stop);const auto path=checkedManagedAssetPath(options.projectRoot,id);
        if(options.reservedIds.contains(id)||options.reservedIds.contains(canonicalAssetId(id)))throw std::runtime_error("Quader import would recreate an explicitly deleted asset: "+id);
        if(const auto found=fileIndexes.find(id);found!=fileIndexes.end()){require(out.files[found->second].contents==data,"Conflicting Quader asset identities.");return;}
        const auto prior=std::filesystem::exists(path)?std::optional{bytes(path)}:std::nullopt;
        if(!immutable)input(id,prior);
        if(prior){if(*prior==data||preserve)return;require(!immutable,"Managed Quader geometry was modified; delete the conflicting package before importing again.");}
        fileIndexes.emplace(id,out.files.size());out.files.push_back({id,std::move(data)});
    }
    std::vector<std::string> importMaterials(){
        std::vector<std::string> result;result.reserve(scene.materials.size());
        for(std::uint32_t index=0;index<scene.materials.size();++index){canceled(stop);const auto& identity=scene.materials[index];const auto originalId=quaderMaterialAssetId(scene.project.id,scene.id,identity);
            const auto replacement=options.materialOverrides.find(originalId);
            // Explicit authored replacements remain valid. A deleted source's
            // built-in fallback must not turn a fresh import into gray geometry.
            if(replacement!=options.materialOverrides.end()&&canonicalAssetId(replacement->second)!=canonicalAssetId(originalId)&&
                ((!options.reservedIds.contains(originalId)&&!options.reservedIds.contains(canonicalAssetId(originalId)))||(!replacement->second.empty()&&!isEngineAssetId(replacement->second)))){
                const auto target=replacement->second.empty()?std::string(DefaultMaterialId):replacement->second;result.push_back(target);
                if(!isEngineAssetId(target))reuseMaterial(target);continue;}
            const auto id=availableFileId(originalId);
            result.push_back(publishedId(id));const bool exists=std::filesystem::is_regular_file(checkedManagedAssetPath(options.projectRoot,id));
            const bool protectedMaterial=options.protectedMaterials.contains(id)||options.protectedMaterials.contains(publishedId(id));
            if(exists&&(!options.refreshMaterials||protectedMaterial)){
                reuseMaterial(id);
                if(options.refreshMaterials&&protectedMaterial)out.deferredMaterials.push_back(publishedId(id));continue;}
            require(static_cast<bool>(scene.readMaterial),"No Quader material reader is available.");const auto source=scene.readMaterial(index);
            require(source.identity.id==identity.id&&source.identity.embedded==identity.embedded,"Quader material identity changed during import.");
            if(options.completed){if(identity.embedded)require(source.fingerprint==scene.fingerprint,"Embedded Quader material does not match its scene save.");
                else{const auto record=options.completed->materials.find(identity.id);require(record!=options.completed->materials.end(),"Save the linked Quader material before automatic synchronization.");
                    require(source.fingerprint==record->second.fingerprint,"Quader material changed after its completed-save record. Waiting for another save.");}}
            std::array<std::string,4> images;
            for(std::size_t slot=0;slot<4;++slot){const auto& texture=source.textures[slot];if(!texture.present)continue;canceled(stop);
                const auto originalImageId="assets/textures/quader-"+digest(identityKey(texture,slot,source.normalDirectX))+".png";
                auto replaced=options.imageOverrides.find(originalImageId);if(replaced==options.imageOverrides.end())replaced=options.imageOverrides.find(publishedId(originalImageId));
                if(replaced!=options.imageOverrides.end()) {
                    const auto& target=replaced->second[slot==3?1:0];
                    if((!options.reservedIds.contains(originalImageId)&&!options.reservedIds.contains(publishedId(originalImageId)))||(!target.empty()&&!isEngineAssetId(target))){images[slot]=publishedId(target);continue;}
                }
                const auto imageId=availableFileId(originalImageId);
                images[slot]=publishedId(imageId);file(imageId,png(texture,slot,source.normalDirectX),!options.refreshMaterials);out.assetIds.push_back(publishedId(imageId));}
            auto candidate=materialAsset(source,publishedId(id),images);file(id,serializeMaterialAsset(candidate));materials[publishedId(id)]=std::move(candidate);out.assetIds.push_back(publishedId(id));}
        return result;
    }
    std::string identityKey(const QuaderSourceTexture& texture,std::size_t slot,bool directX)const{return identity(scene,texture,slot,directX);}
    bool reservedPackage(const std::string& id)const{
        const auto legacy=legacyAssetId(options.projectRoot,id);const auto base=pathFromUtf8(legacy).parent_path();
        for(const auto* member:{"manifest.json","model.gltf","source.gltf","buffers/0.bin"}){
            const auto value=(base/member).generic_string();
            if(options.reservedIds.contains(value)||options.reservedIds.contains(canonicalAssetId(value)))return true;
        }
        return options.reservedIds.contains(id)||options.reservedIds.contains(canonicalAssetId(id));
    }
    void indexPackages(){
        if(indexedPackages)return;indexedPackages=true;
        // Imports run on a worker. Inspect the immutable package catalog once,
        // not once per occurrence; physical placement and display names are not
        // geometry identity. Old UUID-migrated packages keep their original bytes.
        for(const auto& entry:storedAssetPaths(options.projectRoot)){
            canceled(stop);if(entry.file.filename()!="manifest.json"||!std::filesystem::is_regular_file(entry.file)||reservedPackage(entry.id))continue;
            try{
                Package package;package.id=entry.id;package.manifest=bytes(entry.file);const auto manifest=Json::parse(package.manifest);
                if(manifest.value("format",std::string{})!="Quader"||!manifest.contains("quader"))continue;
                const auto& source=manifest.at("quader");if(source.value("project",std::string{})!=scene.project.id||source.value("scene",std::string{})!=scene.id||source.value("collision",false))continue;
                if(manifest.value("document",std::string{})!="model.gltf"||manifest.value("original",std::string{})!="source.gltf")continue;
                const auto directory=entry.file.parent_path();package.gltf=bytes(directory/"model.gltf");
                if(bytes(directory/"source.gltf")!=package.gltf)continue;
                package.buffer=bytes(directory/"buffers/0.bin");
                package.identity=visualIdentity(Json::parse(package.gltf),manifest.at("materialAssets"));
                const std::array<std::filesystem::path,4> files{entry.file,directory/"model.gltf",directory/"source.gltf",directory/"buffers/0.bin"};
                for(std::size_t i=0;i<files.size();++i)package.inputs[i]=assetIdAtPath(options.projectRoot,files[i]);
                const auto key=digest(package.identity+"\n"+package.buffer);packages[key].push_back(std::move(package));
            }catch(const std::exception&){/* Unrelated invalid packages cannot be reused. Direct identity conflicts still fail in file(). */}
        }
        for(auto& [key,candidates]:packages){(void)key;std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.id<b.id;});}
    }
    std::string package(const std::string& name,const std::vector<QuaderSourceVertex>& vertices,const std::vector<std::uint32_t>& indices,
        const std::vector<QuaderSourceSurface>& surfaces,const std::vector<std::string>& materialIds){
        if(indices.empty())return {};require(!vertices.empty()&&indices.size()%3==0,"Quader mesh does not contain valid triangles.");
        std::string buffer;Json views=Json::array(),accessors=Json::array();const auto append=[&](const void* data,std::size_t count){const auto offset=buffer.size();buffer.append(static_cast<const char*>(data),count);return offset;};
        Vec3 minimum{std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},maximum{-minimum[0],-minimum[1],-minimum[2]};
        std::vector<Vec3> positions,normals;std::vector<Vec2> uvs;positions.reserve(vertices.size());normals.reserve(vertices.size());uvs.reserve(vertices.size());
        for(const auto& vertex:vertices){const auto p=scaled(vertex.position,options.scale);positions.push_back(p);normals.push_back(vertex.normal);uvs.push_back(vertex.uv);for(std::size_t axis=0;axis<3;++axis){minimum[axis]=std::min(minimum[axis],p[axis]);maximum[axis]=std::max(maximum[axis],p[axis]);}}
        const auto accessor=[&](const void* data,std::size_t byteCount,std::size_t count,const char* type,int component,int target){const auto offset=append(data,byteCount);const auto view=views.size();views.push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",byteCount},{"target",target}});
            const auto value=accessors.size();accessors.push_back({{"bufferView",view},{"componentType",component},{"count",count},{"type",type}});return value;};
        const auto position=accessor(positions.data(),positions.size()*sizeof(Vec3),positions.size(),"VEC3",5126,34962);accessors[position]["min"]={minimum[0],minimum[1],minimum[2]};accessors[position]["max"]={maximum[0],maximum[1],maximum[2]};
        const auto normal=accessor(normals.data(),normals.size()*sizeof(Vec3),normals.size(),"VEC3",5126,34962);
        const auto uv=accessor(uvs.data(),uvs.size()*sizeof(Vec2),uvs.size(),"VEC2",5126,34962);
        Json primitives=Json::array(),gltfMaterials=Json::array(),nativeMaterials=Json::array();
        for(const auto& surface:surfaces){if(!surface.count)continue;require(surface.count%3==0&&std::uint64_t(surface.offset)+surface.count<=indices.size(),"Invalid Quader primitive range.");
            const auto slot=gltfMaterials.size();std::string id=surface.material==UINT32_MAX?DefaultMaterialId:materialIds.at(surface.material);nativeMaterials.push_back(id);
            gltfMaterials.push_back({{"name",id},{"pbrMetallicRoughness",{{"baseColorFactor",{1,1,1,1}},{"metallicFactor",0},{"roughnessFactor",1}}}});
            const auto index=accessor(indices.data()+surface.offset,surface.count*sizeof(std::uint32_t),surface.count,"SCALAR",5125,34963);
            Json attributes{{"POSITION",position},{"NORMAL",normal},{"TEXCOORD_0",uv}};
            primitives.push_back({{"attributes",std::move(attributes)},{"indices",index},{"material",slot},{"mode",4}});}
        if(primitives.empty())return {};nativeMaterials.push_back(DefaultMaterialId);
        Json document{{"asset",{{"version","2.0"},{"generator","Crimson Quader bridge ABI 6"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0}}}})},
            {"nodes",Json::array({{{"name",name},{"mesh",0}}})},{"meshes",Json::array({{{"name",name},{"primitives",std::move(primitives)}}})},
            {"materials",std::move(gltfMaterials)},{"buffers",Json::array({{{"uri","buffers/0.bin"},{"byteLength",buffer.size()}}})},{"bufferViews",std::move(views)},{"accessors",std::move(accessors)}};
        auto gltf=document.dump();const auto exactContent=digest(scene.project.id+"|"+scene.id+"|"+gltf+"\n"+nativeMaterials.dump()+"\n"+buffer);
        const auto normalized=visualIdentity(document,nativeMaterials),key=digest(normalized+"\n"+buffer);
        auto content=digest("Quader visual geometry v2|"+scene.project.id+"|"+scene.id+"|"+normalized+"\n"+buffer);
        // Prefer the exact legacy package when present, so corruption of its
        // immutable members is still reported rather than hidden by a new ID.
        const auto oldBase="assets/models/quader-"+exactContent;
        const auto generationBase=[&](const std::string& original){
            for(std::size_t generation=0;;++generation){const auto value=original+(generation?"-"+std::to_string(generation):std::string{});
                if(!reservedPackage(value+"/manifest.json"))return value;}
        };
        const auto legacyBase=generationBase(oldBase);
        const bool exactLegacy=std::filesystem::is_regular_file(checkedManagedAssetPath(options.projectRoot,legacyBase+"/manifest.json"));
        if(exactLegacy)content=exactContent;
        const auto originalBase="assets/models/quader-"+content;
        std::string base;
        // Deletion reserves old references, not the right to import the source
        // again. Reuse the first live generation and never revive a deleted ID.
        base=generationBase(originalBase);auto id=base+"/manifest.json";
        Json manifest{{"version",1},{"name",name},{"format","Quader"},{"document","model.gltf"},{"original","source.gltf"},
            {"dependencies",Json::array({{{"kind","buffer"},{"index",0},{"path","buffers/0.bin"},{"source","Quader reader ABI 6"}}})},{"materialAssets",nativeMaterials},
            {"quader",{{"project",scene.project.id},{"scene",scene.id},{"collision",false},{"content",content}}}};
        auto manifestText=manifest.dump(2)+"\n";indexPackages();
        const auto candidates=packages.find(key);const Package* reused{};
        if(!exactLegacy&&candidates!=packages.end())for(const auto& candidate:candidates->second)if(candidate.identity==normalized&&candidate.buffer==buffer){reused=&candidate;break;}
        if(reused){
            id=reused->id;gltf=reused->gltf;buffer=reused->buffer;manifestText=reused->manifest;
            const std::array<const std::string*,4> values{&manifestText,&gltf,&gltf,&buffer};
            for(std::size_t i=0;i<values.size();++i)if(!reused->inputs[i].empty())input(reused->inputs[i],*values[i]);
        }else{
            file(base+"/model.gltf",gltf,false,true);file(base+"/source.gltf",gltf,false,true);file(base+"/buffers/0.bin",buffer,false,true);file(id,manifestText,false,true);
            packages[key].push_back({publishedId(id),gltf,buffer,manifestText,normalized,{}});
        }
        const auto published=publishedId(id);out.assetIds.push_back(published);
        if(!preparedPackages.insert(published).second)return published;
        const auto local=stage/content;std::filesystem::create_directories(local/"buffers");writeFileAtomically(local/"model.gltf",gltf);writeFileAtomically(local/"source.gltf",gltf);writeFileAtomically(local/"buffers/0.bin",buffer);writeFileAtomically(local/"manifest.json",manifestText);
        ModelResolvers resolvers;resolvers.material=[&](const std::string& material)->std::optional<ModelMaterial>{const auto found=materials.find(canonicalAssetId(material));if(found!=materials.end())return found->second.surface;return std::nullopt;};
        auto asset=loadModelPackage(local/"manifest.json",options.projectRoot,resolvers);asset->id=publishedId(id);canceled(stop);
        auto stream=std::make_shared<streaming::ModelStream>(streaming::buildModelCache(*asset,options.projectRoot,false));asset->stream=std::move(stream);
        out.preparedModels.push_back(std::move(asset));return published;
    }
};
}
std::string availableQuaderAssetId(const std::string& original,const std::set<std::string>& reservedIds){
    const auto reserved=[&](const auto& id){return reservedIds.contains(id)||reservedIds.contains(canonicalAssetId(id));};
    if(!reserved(original))return original;
    if(isAssetUuid(original))for(std::size_t generation=1;;++generation){const auto id=derivedAssetId(original,"generation:"+std::to_string(generation));if(!reserved(id))return id;}
    const auto extension=original.rfind('.');
    require(extension!=std::string::npos,"Quader asset identity has no file extension.");
    for(std::size_t generation=1;;++generation){
        const auto id=original.substr(0,extension)+"-"+std::to_string(generation)+original.substr(extension);
        if(!reserved(id))return id;
    }
}
QuaderImportResult prepareQuaderImport(const QuaderSourceScene& scene,const QuaderPrepareOptions& options,std::stop_token stop){
    require(std::isfinite(options.scale)&&options.scale>0,"Import Scale must be positive and finite.");canceled(stop);
    if(options.completed){require(options.completed->scene.has_value(),"Save the linked scene in Quader to initialize automatic synchronization.");const auto& record=*options.completed->scene;
        require(record.projectId==scene.project.id&&record.documentId==scene.id&&record.relativePath==scene.relativePath&&record.fingerprint==scene.fingerprint,"Quader scene changed after its completed-save record. Waiting for another save.");}
    Preparation context(scene,options,stop);auto& out=context.out;out.projectId=scene.project.id;out.sceneId=scene.id;
    if(options.completed)out.sourceKey=quaderSaveKey(*options.completed,options.scale,options.refreshMaterials,options.syncLights);
    else out.sourceKey=scene.project.id+"|"+scene.id+"|manual|"+std::to_string(scene.fingerprint.bytes)+"|"+std::to_string(scene.fingerprint.hash);
    const auto materialIds=context.importMaterials();
    for(const auto& source:scene.meshes){canceled(stop);QuaderImportedMesh mesh;mesh.sourceKey="mesh:"+std::to_string(source.displayEntity)+":"+std::to_string(source.geometryEntity)+":"+std::to_string(source.definitionEntity);mesh.name=source.name;mesh.local=placement(source.transform,options.scale);mesh.visible=source.visible;
        std::vector<QuaderSourceSurface> surfaces=source.surfaces;if(surfaces.empty()&&!source.indices.empty())surfaces.push_back({UINT32_MAX,0,static_cast<std::uint32_t>(source.indices.size())});
        for(const auto& surface:surfaces){require(surface.count%3==0&&std::uint64_t(surface.offset)+surface.count<=source.indices.size(),"Invalid Quader surface indices.");
            for(std::uint32_t i=0;i<surface.count;i+=3){const auto a=source.indices[surface.offset+i],b=source.indices[surface.offset+i+1],c=source.indices[surface.offset+i+2];require(a<source.vertices.size()&&b<source.vertices.size()&&c<source.vertices.size(),"Invalid Quader vertex index.");triangle(source.vertices[a].position,source.vertices[b].position,source.vertices[c].position,options.scale,mesh.local);}}
        mesh.modelId=context.package(source.name,source.vertices,source.indices,surfaces,materialIds);
        if(source.collision==QuaderCollision::Unsupported)++out.skippedCollisions;
        if(source.collision==QuaderCollision::StaticTriangles&&!source.collisionPoints.empty()){
            require(source.collisionPoints.size()%3==0,"Invalid Quader static collision triangles.");std::vector<Vec3> positions;std::vector<std::uint32_t> indices;positions.reserve(source.collisionPoints.size());indices.reserve(source.collisionPoints.size());
            for(std::size_t i=0;i<source.collisionPoints.size();i+=3){triangle(source.collisionPoints[i],source.collisionPoints[i+1],source.collisionPoints[i+2],options.scale,mesh.local);
                for(std::size_t corner=0;corner<3;++corner){indices.push_back(static_cast<std::uint32_t>(positions.size()));positions.push_back(scaled(source.collisionPoints[i+corner],options.scale));}}
            mesh.collision=makeCollisionMesh(std::move(positions),std::move(indices));}
        out.meshes.push_back(std::move(mesh));}
    std::size_t directional{},points{};std::set<std::uint64_t> lightIds;
    if(options.syncLights)for(const auto& source:scene.lights){canceled(stop);if(source.type==2){++out.skippedSpots;continue;}require(source.type<=1,"Unsupported Quader light type.");require(lightIds.insert(source.entity).second,"Duplicate Quader light identity.");
        require(!((source.type==0&&directional>=MaximumDirectionalLights)||(source.type==1&&points>=MaximumPointLights)),"Quader scene exceeds Crimson's supported directional or point light count.");if(source.type==0)++directional;else ++points;
        QuaderImportedLight light;light.sourceKey="light:"+std::to_string(source.entity);light.name=source.name;light.point=source.type==1;light.color=source.color;light.intensity=source.energy;
        light.radius=std::max(.01f,scaled(source.range,options.scale));require(light.radius<=100000.f,"Quader light range exceeds Crimson's supported radius.");light.castShadows=source.castShadows;light.visible=source.visible;
        const auto direction=math::normalized(math::transformDirection(source.transform,{0,-1,0}),{0,-1,0});auto up=math::normalized(math::transformDirection(source.transform,{0,0,1}),{0,0,1});
        if(std::abs(math::dot(direction,up))>.999)up=std::abs(direction[1])<.999?Vec3{0,1,0}:Vec3{0,0,1};
        const auto right=math::normalized(math::cross(direction,up),{1,0,0});up=math::normalized(math::cross(right,direction),{0,1,0});
        light.local={right[0],right[1],right[2],0,up[0],up[1],up[2],0,-direction[0],-direction[1],-direction[2],0,
            scaled(source.transform[12],options.scale),scaled(source.transform[13],options.scale),scaled(source.transform[14],options.scale),1};out.lights.push_back(std::move(light));}
    std::sort(out.assetIds.begin(),out.assetIds.end());out.assetIds.erase(std::unique(out.assetIds.begin(),out.assetIds.end()),out.assetIds.end());return std::move(out);
}
} // namespace crimson
