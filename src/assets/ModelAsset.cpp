#include "ModelAsset.h"
#include "ModelAnimation.h"
#include "MaterialAsset.h"
#include "ContentStorage.h"
#include "QuaderSceneBundle.h"
#include "resources/AssetId.h"
#include "resources/StreamingModel.h"

#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>
#include <mikktspace.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <mutex>
#include <deque>
#include <set>
#include <span>
#include <stdexcept>
#include <unordered_map>

namespace crimson {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
constexpr size_t MaxBytes = 512ull * 1024 * 1024;
constexpr size_t MaxVertices = 8'000'000;
constexpr size_t MaxNodes = 100'000;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Model: " + message); }
void check(bool condition, const std::string& message) { if (!condition) fail(message); }
std::string utf8(const fs::path& p) {
    const auto s = p.generic_u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()};
}
fs::path path(const std::string& s) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s;
}
Bytes read(const fs::path& p) {
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    check(file && size > 0 && static_cast<uint64_t>(size) <= MaxBytes, "cannot read file or resource exceeds 512 MiB: " + utf8(p.filename()));
    Bytes result(static_cast<size_t>(size)); file.seekg(0);
    file.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    check(static_cast<bool>(file), "incomplete file: " + utf8(p.filename())); return result;
}
void write(const fs::path& p, std::span<const uint8_t> bytes) {
    fs::create_directories(p.parent_path()); std::ofstream file(p, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close(); check(static_cast<bool>(file), "cannot save managed resource: " + utf8(p.filename()));
}
void writeJson(const fs::path& p, const Json& json) {
    const auto text = json.dump(2); write(p, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}
uint32_t u32(const uint8_t* bytes) {
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
bool inside(const fs::path& candidate, const fs::path& parent) {
    auto c = candidate.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
        if (c == candidate.end() || lower(utf8(*c)) != lower(utf8(*p))) return false;
    return true;
}
fs::path managedPath(const fs::path& directory, const std::string& relative) {
    const auto p = path(relative);
    check(!p.empty() && !p.is_absolute() && !p.has_root_name(), "managed dependency must use a relative path");
    const auto root = fs::weakly_canonical(directory), result = fs::weakly_canonical(root / p);
    check(inside(result, root) && result != root, "managed dependency escapes its package"); return result;
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c-'0';
    if (c >= 'a' && c <= 'f') return c-'a'+10;
    if (c >= 'A' && c <= 'F') return c-'A'+10;
    return -1;
}
std::string percentDecode(const std::string& source) {
    std::string result;
    for (size_t i=0; i<source.size(); ++i) {
        if (source[i] != '%') { result += source[i]; continue; }
        check(i+2 < source.size() && hex(source[i+1]) >= 0 && hex(source[i+2]) >= 0, "invalid URI percent escape");
        const char c = static_cast<char>(hex(source[i+1])*16+hex(source[i+2]));
        check(c != '\0', "URI contains a null character"); result += c; i += 2;
    }
    return result;
}
Bytes dataUri(const std::string& uri) {
    const auto comma = uri.find(','); check(comma != std::string::npos, "malformed data URI");
    const auto header = uri.substr(0, comma);
    if (!header.ends_with(";base64")) {
        const auto text = percentDecode(uri.substr(comma+1)); return {text.begin(), text.end()};
    }
    const auto text = uri.substr(comma+1); check(text.size()%4 == 0 && text.size()/4*3 <= MaxBytes, "invalid base64 data length");
    Bytes result; result.reserve(text.size()/4*3);
    auto digit = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c-'A'; if (c >= 'a' && c <= 'z') return c-'a'+26;
        if (c >= '0' && c <= '9') return c-'0'+52; if (c == '+') return 62; if (c == '/') return 63; return -1;
    };
    for (size_t i=0; i<text.size(); i+=4) {
        const int a=digit(text[i]), b=digit(text[i+1]), c=digit(text[i+2]), d=digit(text[i+3]);
        check(a>=0 && b>=0 && (c>=0 || text[i+2]=='=') && (d>=0 || text[i+3]=='='), "invalid base64 data");
        check((c>=0 || text[i+3]=='=') && ((c>=0 && d>=0) || i+4==text.size()), "invalid base64 padding");
        result.push_back(static_cast<uint8_t>((a<<2)|(b>>4)));
        if (c>=0) result.push_back(static_cast<uint8_t>((b<<4)|(c>>2)));
        if (d>=0) result.push_back(static_cast<uint8_t>((c<<6)|d));
    }
    return result;
}
Bytes readUri(const fs::path& directory, const std::string& uri) {
    if (uri.starts_with("data:")) return dataUri(uri);
    const auto decoded = percentDecode(uri);
    check(decoded.find(':') == std::string::npos && decoded.find('?') == std::string::npos && decoded.find('#') == std::string::npos,
          "only local relative dependency URIs are supported: " + uri);
    const auto relative = path(decoded);
    check(!relative.empty() && !relative.is_absolute() && !relative.has_root_name(), "dependency URI is not relative: " + uri);
    return read(directory / relative);
}
struct Document { Json json; Bytes bin; };
Document document(const Bytes& bytes, bool binary) {
    if (!binary) return {Json::parse(bytes), {}};
    check(bytes.size()>=20 && u32(bytes.data())==0x46546c67 && u32(bytes.data()+4)==2 && u32(bytes.data()+8)==bytes.size(), "invalid GLB 2.0 header");
    Document result; bool jsonSeen=false, binSeen=false;
    for (size_t offset=12; offset<bytes.size();) {
        check(bytes.size()-offset>=8, "truncated GLB chunk header");
        const auto length=u32(bytes.data()+offset), type=u32(bytes.data()+offset+4); offset+=8;
        check(length%4==0 && length<=bytes.size()-offset, "truncated or unaligned GLB chunk");
        if (!jsonSeen) {
            check(type==0x4e4f534a, "the first GLB chunk must be JSON");
            result.json=Json::parse(bytes.begin()+offset, bytes.begin()+offset+length); jsonSeen=true;
        } else if (type==0x004e4942) {
            check(!binSeen, "duplicate GLB binary chunk"); result.bin.assign(bytes.begin()+offset,bytes.begin()+offset+length); binSeen=true;
        }
        offset+=length;
    }
    check(jsonSeen, "GLB has no JSON document"); return result;
}
size_t number(const Json& object, const char* key, size_t fallback=0) {
    if (!object.contains(key)) return fallback;
    check(object[key].is_number_unsigned() || (object[key].is_number_integer() && object[key].get<int64_t>()>=0), std::string("invalid nonnegative integer: ")+key);
    return object[key].get<size_t>();
}
void range(size_t offset, size_t length, size_t limit, const std::string& description) {
    check(offset<=limit && length<=limit-offset, description + " is outside its buffer");
}
std::string imageExtension(const Bytes& encoded) {
    static constexpr std::array<uint8_t,8> png{137,80,78,71,13,10,26,10};
    if (encoded.size()>=png.size() && std::equal(png.begin(),png.end(),encoded.begin())) return ".png";
    if (encoded.size()>=3 && encoded[0]==255 && encoded[1]==216 && encoded[2]==255) return ".jpg";
    return {};
}
void validateImage(const Bytes& bytes) {
    static bx::DefaultAllocator allocator; bx::Error error;
    std::unique_ptr<bimg::ImageContainer,decltype(&bimg::imageFree)> image(
        bimg::imageParse(&allocator,bytes.data(),static_cast<uint32_t>(bytes.size()),bimg::TextureFormat::RGBA8,&error),bimg::imageFree);
    const bool ok=error.isOk();
    check(ok && image && image->m_width>0 && image->m_height>0 && image->m_depth<=1 && !image->m_cubeMap && image->m_numLayers==1,
          "invalid or unsupported material image");
    check(image->m_width<=16384 && image->m_height<=16384, "material image exceeds the Direct3D 11 texture size limit");
}

// The manifest maps the immutable original source to a normalized, self-contained
// glTF document. No source URI is followed when a managed package is reopened.
Json stageDocument(const fs::path& source, const fs::path& stage, const Bytes& original, bool binary) {
    auto parsed=document(original,binary); auto& json=parsed.json;
    check(json.is_object() && json.contains("asset") && json["asset"].value("version",std::string{})=="2.0", "expected a glTF 2.0 asset");
    if (json.contains("extensionsRequired") && !json["extensionsRequired"].empty())
        fail("unsupported required extension: " + json["extensionsRequired"][0].get<std::string>());
    Json dependencies=Json::array(),sourceBufferExtras=Json::array(); std::vector<Bytes> buffers;
    for (auto& buffer : json["buffers"]) {
        const size_t index=buffers.size(), size=number(buffer,"byteLength");
        check(size>0 && size<=MaxBytes,"invalid buffer byteLength");
        const std::string uri=buffer.value("uri",std::string{});
        auto bytes=uri.empty() ? parsed.bin : readUri(source.parent_path(),uri);
        check(!uri.empty() || (binary && index==0),"buffer is missing its URI");
        check(bytes.size()>=size,"buffer is shorter than its declared byteLength");
        if(bytes.size()!=size && !uri.empty() && !uri.starts_with("data:"))
            sourceBufferExtras.push_back({{"index",index},{"bytes",bytes.size()},{"sha256",modelImportDataDigest(bytes)}});
        bytes.resize(size);
        const auto file="buffers/"+std::to_string(index)+".bin"; write(stage/path(file),bytes); buffer["uri"]=file;
        dependencies.push_back({{"kind","buffer"},{"index",index},{"path",file},{"source",uri.empty()?"GLB binary chunk":uri.starts_with("data:")?"data URI":uri}});
        buffers.push_back(std::move(bytes));
    }
    if (json["buffers"].is_null()) json.erase("buffers");
    std::set<size_t> coreImages;
    if (json.contains("textures")) for (const auto& texture:json["textures"])
        if (texture.contains("source")) coreImages.insert(number(texture,"source"));
    size_t imageIndex=0;
    for (auto& image : json["images"]) {
        Bytes bytes; const std::string uri=image.value("uri",std::string{});
        check(!image.contains("bufferView") || uri.empty(),"image cannot have both URI and bufferView");
        if (!uri.empty()) bytes=readUri(source.parent_path(),uri);
        else {
            check(image.contains("bufferView"),"image has no URI or bufferView");
            const size_t viewIndex=number(image,"bufferView");
            check(json.contains("bufferViews") && viewIndex<json["bufferViews"].size(),"image references missing buffer view");
            const auto& view=json["bufferViews"][viewIndex]; const auto bufferIndex=number(view,"buffer");
            check(bufferIndex<buffers.size(),"image references missing buffer");
            const auto offset=number(view,"byteOffset"), length=number(view,"byteLength");
            range(offset,length,buffers[bufferIndex].size(),"image buffer view");
            bytes.assign(buffers[bufferIndex].begin()+offset,buffers[bufferIndex].begin()+offset+length);
        }
        const auto ext=imageExtension(bytes);
        check(!ext.empty() || !coreImages.contains(imageIndex),"core glTF images must contain PNG or JPEG data");
        if (!ext.empty()) validateImage(bytes);
        const auto file="images/"+std::to_string(imageIndex)+(ext.empty()?".bin":ext); write(stage/path(file),bytes);
        image.erase("bufferView"); image["uri"]=file;
        if (!ext.empty()) image["mimeType"]=ext==".png"?"image/png":"image/jpeg";
        dependencies.push_back({{"kind","image"},{"index",imageIndex++},{"path",file},{"source",uri.empty()?"buffer view":uri.starts_with("data:")?"data URI":uri}});
    }
    if (json["images"].is_null()) json.erase("images");
    writeJson(stage/"model.gltf",json);
    const auto originalName=binary?"source.glb":"source.gltf"; write(stage/originalName,original);
    Json manifest={{"version",1},{"name",utf8(source.stem())},{"format",binary?"GLB":"glTF"},
            {"document","model.gltf"},{"original",originalName},{"dependencies",std::move(dependencies)}};
    if(!sourceBufferExtras.empty())manifest["sourceBufferExtras"]=std::move(sourceBufferExtras);
    return manifest;
}

// aiScene deliberately does not retain glTF source table identities, original
// index streams or interpolation tangents. This small semantic view validates
// package bounds and retains those values while Assimp decodes the mesh streams.
struct Accessor {
    const Json* source{}; const Json* view{};
    std::size_t count{},offset{},stride{},components{},componentSize{},elementSize{};
    int componentType{}; std::string type; bool normalized{};
};
struct GltfSource {
    Json json; std::vector<Bytes> buffers;
    const Json& table(const char* name) const {
        static const Json empty=Json::array(); const auto found=json.find(name);
        if(found==json.end())return empty;
        check(found->is_array(),std::string("invalid asset table: ")+name);return *found;
    }
    const Json& item(const char* name,std::size_t index) const {
        const auto& values=table(name);check(index<values.size(),std::string("missing ")+name+" reference");return values[index];
    }
    static std::size_t componentBytes(int type) {
        switch(type){case 5120:case 5121:return 1;case 5122:case 5123:return 2;case 5125:case 5126:return 4;default:fail("invalid accessor component type");}
    }
    Accessor accessor(std::size_t index) const {
        const auto& value=item("accessors",index);Accessor a;a.source=&value;
        a.count=number(value,"count");a.offset=number(value,"byteOffset");a.componentType=value.value("componentType",0);
        a.componentSize=componentBytes(a.componentType);a.type=value.value("type",std::string{});a.normalized=value.value("normalized",false);
        if(a.type=="SCALAR")a.components=1;else if(a.type=="VEC2")a.components=2;else if(a.type=="VEC3")a.components=3;
        else if(a.type=="VEC4"||a.type=="MAT2")a.components=4;else if(a.type=="MAT3")a.components=9;else if(a.type=="MAT4")a.components=16;
        else fail("invalid accessor type or count");
        const auto width=a.type=="MAT2"?2u:a.type=="MAT3"?3u:a.type=="MAT4"?4u:0u;
        a.elementSize=width?width*((width*a.componentSize+3)/4*4):a.components*a.componentSize;
        check(a.count>0&&a.count<=MaxVertices*3,"invalid accessor type or count");
        if(value.contains("bufferView"))a.view=&item("bufferViews",number(value,"bufferView"));
        a.stride=a.view?number(*a.view,"byteStride",a.elementSize):a.elementSize;
        check(a.offset%a.componentSize==0&&a.stride>=a.elementSize&&a.stride%a.componentSize==0,"invalid accessor stride or alignment");
        check(!a.normalized||a.componentType!=5126,"float attributes cannot be normalized");
        if(a.view){const auto viewOffset=number(*a.view,"byteOffset"),size=number(*a.view,"byteLength");
            check((viewOffset+a.offset)%a.componentSize==0,"unaligned accessor");
            check(a.offset<=size&&a.elementSize<=size-a.offset&&a.count-1<=(size-a.offset-a.elementSize)/a.stride,"accessor exceeds its buffer view");
        }else check(a.offset==0,"accessor without a buffer view has a byte offset");
        return a;
    }
    const uint8_t* viewData(const Json& view) const {
        const auto buffer=number(view,"buffer");check(buffer<buffers.size(),"buffer view has no buffer");return buffers[buffer].data()+number(view,"byteOffset");
    }
};
uint32_t unsignedValue(const uint8_t* p,int type) {
    if(type==5121)return *p;if(type==5123)return uint32_t(p[0])|(uint32_t(p[1])<<8);if(type==5125)return u32(p);
    fail("indices must be unsigned 8, 16, or 32 bit values");
}
void validateAccessors(const GltfSource& data) {
    for(const auto& view:data.table("bufferViews")) {
        const auto buffer=number(view,"buffer");check(buffer<data.buffers.size(),"buffer view has no buffer");
        range(number(view,"byteOffset"),number(view,"byteLength"),data.buffers[buffer].size(),"buffer view");
        const auto stride=number(view,"byteStride");check(!stride||(stride>=4&&stride<=252&&stride%4==0),"invalid buffer view stride");
    }
    for(std::size_t index=0;index<data.table("accessors").size();++index) {
        const auto a=data.accessor(index);const auto found=a.source->find("sparse");if(found==a.source->end())continue;
        const auto& sparse=*found;const auto count=number(sparse,"count");
        check(count>0&&count<=a.count&&sparse.contains("indices")&&sparse.contains("values"),"invalid sparse accessor");
        const auto& indices=sparse.at("indices");const auto& values=sparse.at("values");const int type=indices.value("componentType",0);
        check(type==5121||type==5123||type==5125,"invalid sparse index type");const auto size=GltfSource::componentBytes(type);
        const auto& indexView=data.item("bufferViews",number(indices,"bufferView"));const auto& valueView=data.item("bufferViews",number(values,"bufferView"));
        check(!number(indexView,"byteStride")&&!number(valueView,"byteStride"),"sparse values and indices must be tightly packed");
        range(number(indices,"byteOffset"),count*size,number(indexView,"byteLength"),"sparse indices");
        range(number(values,"byteOffset"),count*a.elementSize,number(valueView,"byteLength"),"sparse values");
        check((number(indexView,"byteOffset")+number(indices,"byteOffset"))%size==0&&
            (number(valueView,"byteOffset")+number(values,"byteOffset"))%a.componentSize==0,"unaligned sparse accessor");
        const auto* encoded=data.viewData(indexView)+number(indices,"byteOffset");uint32_t previous{};
        for(std::size_t i=0;i<count;++i){const auto value=unsignedValue(encoded+i*size,type);
            check(value<a.count&&(!i||value>previous),"sparse indices must be increasing and within accessor bounds");previous=value;}
    }
}
float componentValue(const uint8_t* encoded,int type,bool normalized) {
    switch(type) {
    case 5120:{int8_t value{};std::memcpy(&value,encoded,1);return normalized?std::max(float(value)/127.f,-1.f):float(value);}
    case 5121:return normalized?float(*encoded)/255.f:float(*encoded);
    case 5122:{int16_t value{};std::memcpy(&value,encoded,2);return normalized?std::max(float(value)/32767.f,-1.f):float(value);}
    case 5123:{uint16_t value{};std::memcpy(&value,encoded,2);return normalized?float(value)/65535.f:float(value);}
    case 5125:return float(u32(encoded));
    case 5126:{float value{};std::memcpy(&value,encoded,4);return value;}
    default:fail("invalid accessor component type");}
}
std::vector<float> floats(const GltfSource& data,const Accessor& a) {
    std::vector<float> result(a.count*a.components);
    const auto width=a.type=="MAT2"?2u:a.type=="MAT3"?3u:a.type=="MAT4"?4u:0u;
    const auto unpack=[&](const uint8_t* encoded,float* output){for(std::size_t c=0;c<a.components;++c){
        const auto offset=width?(c/width)*((width*a.componentSize+3)/4*4)+(c%width)*a.componentSize:c*a.componentSize;
        output[c]=componentValue(encoded+offset,a.componentType,a.normalized);}};
    if(a.view){const auto* encoded=data.viewData(*a.view)+a.offset;for(std::size_t i=0;i<a.count;++i)unpack(encoded+i*a.stride,result.data()+i*a.components);}
    if(const auto found=a.source->find("sparse");found!=a.source->end()){
        const auto& sparse=*found;const auto& index=sparse.at("indices");const auto& values=sparse.at("values");
        const auto type=index.at("componentType").get<int>();const auto size=GltfSource::componentBytes(type);
        const auto* mapping=data.viewData(data.item("bufferViews",number(index,"bufferView")))+number(index,"byteOffset");
        const auto* encoded=data.viewData(data.item("bufferViews",number(values,"bufferView")))+number(values,"byteOffset");
        for(std::size_t i=0;i<number(sparse,"count");++i)unpack(encoded+i*a.elementSize,result.data()+unsignedValue(mapping+i*size,type)*a.components);
    }
    check(std::all_of(result.begin(),result.end(),[](float value){return std::isfinite(value);}),"nonfinite vertex attribute");return result;
}
std::vector<uint32_t> indices(const GltfSource& data,const Accessor& a) {
    check(a.type=="SCALAR"&&!a.normalized,"invalid triangle index accessor");
    uint8_t zero[4]{};unsignedValue(zero,a.componentType);std::vector<uint32_t> result(a.count);
    if(a.view){const auto* encoded=data.viewData(*a.view)+a.offset;for(std::size_t i=0;i<a.count;++i)result[i]=unsignedValue(encoded+i*a.stride,a.componentType);}
    if(const auto found=a.source->find("sparse");found!=a.source->end()){
        const auto& sparse=*found;const auto& index=sparse.at("indices");const auto& values=sparse.at("values");const int type=index.at("componentType").get<int>();
        const auto* mapping=data.viewData(data.item("bufferViews",number(index,"bufferView")))+number(index,"byteOffset");
        const auto* encoded=data.viewData(data.item("bufferViews",number(values,"bufferView")))+number(values,"byteOffset");
        for(std::size_t i=0;i<number(sparse,"count");++i)result[unsignedValue(mapping+i*GltfSource::componentBytes(type),type)]=unsignedValue(encoded+i*a.componentSize,a.componentType);
    }return result;
}

// The importer can only open captured managed dependencies. Its transient input
// keeps vertices in source order: Assimp otherwise compacts indexed glTF streams,
// which would invalidate skin/morph streams and discard authored unused vertices.
class AssimpMemoryStream final:public Assimp::IOStream {
    const Bytes& bytes_;std::size_t position_{};
public:
    explicit AssimpMemoryStream(const Bytes& bytes):bytes_(bytes){}
    std::size_t Read(void* output,std::size_t size,std::size_t count)override{
        if(!size)return 0;count=std::min(count,(bytes_.size()-position_)/size);const auto length=size*count;
        std::memcpy(output,bytes_.data()+position_,length);position_+=length;return count;
    }
    std::size_t Write(const void*,std::size_t,std::size_t)override{return 0;}
    aiReturn Seek(std::size_t offset,aiOrigin origin)override{
        std::size_t next{};
        if(origin==aiOrigin_SET)next=offset;
        else if(origin==aiOrigin_CUR){if(offset>bytes_.size()-position_)return aiReturn_FAILURE;next=position_+offset;}
        else {if(offset>bytes_.size())return aiReturn_FAILURE;next=bytes_.size()-offset;}
        if(next>bytes_.size())return aiReturn_FAILURE;position_=next;return aiReturn_SUCCESS;
    }
    std::size_t Tell()const override{return position_;}
    std::size_t FileSize()const override{return bytes_.size();}
    void Flush()override{}
};
class AssimpPackageIo final:public Assimp::IOSystem {
    std::unordered_map<std::string,Bytes> files_;
    static std::string key(const char* value){return utf8(path(value).lexically_normal());}
public:
    explicit AssimpPackageIo(std::unordered_map<std::string,Bytes> files):files_(std::move(files)){}
    bool Exists(const char* value)const override{return files_.contains(key(value));}
    char getOsSeparator()const override{return '/';}
    Assimp::IOStream* Open(const char* value,const char* mode="rb")override{
        if(std::strchr(mode,'w')||std::strchr(mode,'a'))return nullptr;const auto found=files_.find(key(value));
        return found==files_.end()?nullptr:new AssimpMemoryStream(found->second);
    }
    void Close(Assimp::IOStream* stream)override{delete stream;}
};
std::unique_ptr<Assimp::Importer> importAssimp(const GltfSource& data) {
    auto json=data.json;
    // Unsupported extensions retain their documented core fallback. The exact
    // animation/skin/morph representation is carried by the semantic adapter.
    const auto stripExtensions=[&](auto&& self,Json& value)->void{
        if(value.is_object()){value.erase("extensions");value.erase("extensionsUsed");for(auto& child:value.items())self(self,child.value());}
        else if(value.is_array())for(auto& child:value)self(self,child);
    };stripExtensions(stripExtensions,json);
    json.erase("animations");json.erase("skins");json.erase("cameras");
    json["nodes"]=Json::array();json["scenes"]=Json::array({{{"nodes",Json::array()}}});json["scene"]=0;
    Bytes canonical;
    const auto materialCount=data.table("materials").size();
    if(materialCount){
        // Assimp lazily loads material slots in first-use order. A tiny private
        // carrier references every authored slot first, including unused slots.
        canonical.resize(3*sizeof(float));const auto view=json["bufferViews"].size();
        json["bufferViews"].push_back({{"buffer",data.buffers.size()},{"byteOffset",0},{"byteLength",canonical.size()}});
        const auto accessor=json["accessors"].size();json["accessors"].push_back({{"bufferView",view},{"componentType",5126},{"count",1},{"type","VEC3"}});
        Json primitives=Json::array();for(std::size_t material=0;material<materialCount;++material)primitives.push_back({{"mode",0},{"material",material},{"attributes",{{"POSITION",accessor}}}});
        json["meshes"].push_back({{"primitives",std::move(primitives)}});
        json["nodes"].push_back({{"mesh",data.table("meshes").size()}});json["scenes"][0]["nodes"].push_back(0);
    }
    // Source hierarchy is validated iteratively below; never let Assimp recurse
    // into an untrusted cycle, and include every mesh even outside the active scene.
    for(std::size_t index=0;index<data.table("meshes").size();++index){const auto node=json["nodes"].size();json["nodes"].push_back({{"mesh",index}});json["scenes"][0]["nodes"].push_back(node);}
    for(std::size_t meshIndex=0;meshIndex<data.table("meshes").size();++meshIndex){auto& mesh=json["meshes"][meshIndex];mesh.erase("weights");for(auto& primitive:mesh["primitives"]){
        primitive.erase("indices");primitive.erase("targets");primitive["mode"]=0;
        auto& attributes=primitive.at("attributes");check(attributes.is_object()&&attributes.contains("POSITION"),"primitive needs FLOAT VEC3 positions");
        const auto positions=data.accessor(attributes["POSITION"].get<std::size_t>());
        check(positions.type=="VEC3"&&positions.componentType==5126&&positions.count<=MaxVertices,"primitive needs FLOAT VEC3 positions");
        for(auto it=attributes.begin();it!=attributes.end();) {
            const auto& semantic=it.key();
            if((semantic!="POSITION"&&semantic!="NORMAL"&&semantic!="TANGENT"&&semantic!="TEXCOORD_0"&&semantic!="TEXCOORD_1"&&semantic!="COLOR_0")||
                (semantic=="TANGENT"&&!attributes.contains("NORMAL"))){it=attributes.erase(it);continue;}
            const auto accessor=data.accessor(it.value().get<std::size_t>());
            const auto components=semantic=="POSITION"||semantic=="NORMAL"?3u:semantic=="TANGENT"?4u:semantic=="COLOR_0"?accessor.components:2u;
            check(accessor.count==positions.count&&accessor.components==components,"vertex attribute count or shape mismatch");
            check(semantic=="COLOR_0"?(accessor.type=="VEC3"||accessor.type=="VEC4"):accessor.type==(components==2?"VEC2":components==3?"VEC3":"VEC4"),"vertex attribute count or shape mismatch");
            if(semantic=="COLOR_0")check(components==3||components==4,"COLOR_0 must have three or four components");
            if(semantic=="NORMAL"||semantic=="TANGENT")check(accessor.componentType==5126,"NORMAL/TANGENT must use floating point components");
            if(semantic.starts_with("TEXCOORD_")||semantic=="COLOR_0")check(accessor.componentType==5126||
                (accessor.normalized&&(accessor.componentType==5121||accessor.componentType==5123)),"UV/color attributes must be floats or normalized unsigned bytes/shorts");
            const auto validated=floats(data,accessor);
            if(semantic=="TANGENT")for(std::size_t vertex=0;vertex<accessor.count;++vertex)check(validated[vertex*4+3]==1||validated[vertex*4+3]==-1,"tangent handedness must be +1 or -1");
            // Assimp 6.0.4 glTF2Asset.inl applies sparse data with the wrong
            // interleaved stride or a short final stride; glTF2Importer.cpp also
            // memcpy-decodes integer UVs.
            // Canonicalize only those affected streams in this transient view.
            // https://github.com/assimp/assimp/blob/v6.0.4/code/AssetLib/glTF2/glTF2Asset.inl
            const bool convert=!accessor.view||accessor.stride!=accessor.elementSize||accessor.source->contains("sparse")||(it.key().starts_with("TEXCOORD_")&&accessor.componentType!=5126);
            if(convert){const auto& values=validated;const auto offset=canonical.size();const auto size=values.size()*sizeof(float);
                canonical.resize(offset+size);std::memcpy(canonical.data()+offset,values.data(),size);
                const auto view=json["bufferViews"].size();json["bufferViews"].push_back({{"buffer",data.buffers.size()},{"byteOffset",offset},{"byteLength",size}});
                const auto replacement=json["accessors"].size();json["accessors"].push_back({{"bufferView",view},{"componentType",5126},{"count",accessor.count},{"type",accessor.type}});it.value()=replacement;
            }++it;
        }
        // Assimp requires contiguous TEXCOORD sets. Keep a private UV0 alias
        // for valid UV1-only sources; source availability and validation below
        // still use the authored document, so missing UV0 remains missing.
        if(!attributes.contains("TEXCOORD_0")&&attributes.contains("TEXCOORD_1"))attributes["TEXCOORD_0"]=attributes["TEXCOORD_1"];
    }}
    if(!canonical.empty())json["buffers"].push_back({{"uri","__crimson_assimp_streams.bin"},{"byteLength",canonical.size()}});
    std::unordered_map<std::string,Bytes> files;const auto text=json.dump();files.emplace("model.gltf",Bytes(text.begin(),text.end()));
    for(std::size_t index=0;index<data.buffers.size();++index)files.emplace(data.item("buffers",index).at("uri").get<std::string>(),data.buffers[index]);
    if(!canonical.empty())files.emplace("__crimson_assimp_streams.bin",std::move(canonical));
    auto importer=std::make_unique<Assimp::Importer>();importer->SetIOHandler(new AssimpPackageIo(std::move(files)));
    const auto* scene=importer->ReadFile("model.gltf",0);
    check(scene!=nullptr,"Assimp import failed: "+std::string(importer->GetErrorString()));return importer;
}
void warn(ModelAsset& asset, const std::string& message) {
    if (std::find(asset.warnings.begin(),asset.warnings.end(),message)==asset.warnings.end()) asset.warnings.push_back(message);
}
using math::subtract;
using math::cross;
Vec3 normalized(Vec3 a,Vec3 fallback={0,1,0}) {
    return math::normalized(a,fallback,1e-20);
}
ModelMatrix multiply(const ModelMatrix& a,const ModelMatrix& b) {
    const auto result=math::multiply(a,b);
    check(math::finite(result),"node transform overflow"); return result;
}
void include(ModelBounds& bounds,const Vec3& position,const ModelMatrix& m) {
    const auto value=math::transformPoint(m,position);
    check(math::finite(value),"transformed vertex is nonfinite");
    math::include(bounds,value);
}

ModelWrap wrap(int value) {
    if(value==10497)return ModelWrap::Repeat;if(value==33071)return ModelWrap::Clamp;if(value==33648)return ModelWrap::Mirror;
    fail("unsupported sampler wrapping mode");
}
ModelFilter filter(int value,bool minification) {
    switch(value){case 0:case 9729:return ModelFilter::Linear;case 9728:return ModelFilter::Nearest;
    case 9984:check(minification,"invalid texture filter");return ModelFilter::NearestMipmapNearest;
    case 9985:check(minification,"invalid texture filter");return ModelFilter::LinearMipmapNearest;
    case 9986:check(minification,"invalid texture filter");return ModelFilter::NearestMipmapLinear;
    case 9987:check(minification,"invalid texture filter");return ModelFilter::LinearMipmapLinear;default:fail("invalid texture filter");}
}
ModelTexture texture(const Json& view,const GltfSource& data,const std::vector<std::string>& images,ModelAsset& asset) {
    ModelTexture result;if(view.is_null()||!view.contains("index"))return result;
    const auto& source=data.item("textures",number(view,"index"));check(source.contains("source"),"texture has no supported core image fallback");
    const auto uv=number(view,"texCoord");check(uv<=1,"material references a UV set beyond UV1");
    result.image=AssetRef<DecodedImage>{images.at(number(source,"source"))};result.texCoord=uint32_t(uv);
    check(!result.image.empty(),"core material texture has no supported PNG/JPEG image");
    if(view.contains("extensions")&&view["extensions"].contains("KHR_texture_transform"))warn(asset,"Optional KHR_texture_transform ignored; core texture coordinates are used.");
    if(source.contains("sampler")){const auto& sampler=data.item("samplers",number(source,"sampler"));
        result.wrapU=wrap(sampler.value("wrapS",10497));result.wrapV=wrap(sampler.value("wrapT",10497));
        result.minFilter=filter(sampler.value("minFilter",0),true);result.magFilter=filter(sampler.value("magFilter",0),false);}
    return result;
}
const Json& field(const Json& value,const char* name) {static const Json missing;const auto found=value.find(name);return found==value.end()?missing:*found;}
template<class Value> void vectorValue(const Json& source,Value& target) {
    if(source.is_null())return;check(source.is_array()&&source.size()==target.size(),"invalid vector shape");
    for(std::size_t i=0;i<target.size();++i){check(source[i].is_number(),"invalid vector component");target[i]=source[i].get<float>();}
}
void materials(const GltfSource& data,const std::vector<std::string>& images,ModelAsset& asset,const aiScene* imported=nullptr) {
    bool needsFallback=false;for(const auto& mesh:data.table("meshes"))for(const auto& primitive:mesh.at("primitives"))needsFallback|=!primitive.contains("material");
    const auto& sources=data.table("materials");asset.materials.reserve(sources.size()+(needsFallback?1:0));
    check(!imported||imported->mNumMaterials>=sources.size(),"Assimp lost source material slots");
    for(std::size_t i=0;i<sources.size();++i) {
        const auto& source=sources[i];ModelMaterial m;m.name=source.value("name",std::string{});if(m.name.empty())m.name="Material "+std::to_string(i+1);
        const auto& pbr=field(source,"pbrMetallicRoughness");
        if(!pbr.is_null()){
            vectorValue(field(pbr,"baseColorFactor"),m.baseColor);m.metallic=pbr.value("metallicFactor",1.f);m.roughness=pbr.value("roughnessFactor",1.f);
            m.baseColorTexture=texture(field(pbr,"baseColorTexture"),data,images,asset);
            m.metallicRoughnessTexture=texture(field(pbr,"metallicRoughnessTexture"),data,images,asset);
        }
        m.normalTexture=texture(field(source,"normalTexture"),data,images,asset);if(!m.normalTexture.image.empty())m.normalScale=source["normalTexture"].value("scale",1.f);
        m.occlusionTexture=texture(field(source,"occlusionTexture"),data,images,asset);if(!m.occlusionTexture.image.empty())m.occlusionStrength=source["occlusionTexture"].value("strength",1.f);
        m.emissiveTexture=texture(field(source,"emissiveTexture"),data,images,asset);vectorValue(field(source,"emissiveFactor"),m.emissive);
        const auto alpha=source.value("alphaMode",std::string{"OPAQUE"});check(alpha=="OPAQUE"||alpha=="MASK"||alpha=="BLEND","invalid material alpha mode");
        m.alphaMask=alpha=="MASK";m.alphaBlend=alpha=="BLEND";m.alphaCutoff=source.value("alphaCutoff",.5f);m.doubleSided=source.value("doubleSided",false);
        auto unit=[](float value){return std::isfinite(value)&&value>=0&&value<=1;};
        check(std::all_of(m.baseColor.begin(),m.baseColor.end(),unit)&&unit(m.metallic)&&unit(m.roughness)&&unit(m.occlusionStrength),"material factors must be in [0, 1]: "+m.name);
        check(std::isfinite(m.normalScale)&&std::isfinite(m.alphaCutoff)&&m.alphaCutoff>=0&&std::all_of(m.emissive.begin(),m.emissive.end(),unit),"invalid normal, alpha, or emissive factor: "+m.name);
        if(imported) {
            const auto& material=*imported->mMaterials[i];aiColor4D base;aiColor3D emissive;
            if(material.Get(AI_MATKEY_BASE_COLOR,base)==AI_SUCCESS)m.baseColor={base.r,base.g,base.b,base.a};
            material.Get(AI_MATKEY_METALLIC_FACTOR,m.metallic);material.Get(AI_MATKEY_ROUGHNESS_FACTOR,m.roughness);
            if(material.Get(AI_MATKEY_COLOR_EMISSIVE,emissive)==AI_SUCCESS)m.emissive={emissive.r,emissive.g,emissive.b};
        }
        asset.materials.push_back(std::move(m));
    }
    if(needsFallback){ModelMaterial fallback;fallback.name="Default material";asset.materials.push_back(std::move(fallback));}
}

struct TangentContext { std::vector<ModelVertex>* corners; uint32_t uv; };
ModelVertex& corner(const SMikkTSpaceContext* context,int face,int vertex) {
    return (*static_cast<TangentContext*>(context->m_pUserData)->corners)[static_cast<size_t>(face)*3+vertex];
}
void generateTangents(std::vector<ModelVertex>& corners,uint32_t uv) {
    TangentContext user{&corners,uv}; SMikkTSpaceInterface api{};
    api.m_getNumFaces=[](const SMikkTSpaceContext* c){return static_cast<int>(static_cast<TangentContext*>(c->m_pUserData)->corners->size()/3);};
    api.m_getNumVerticesOfFace=[](const SMikkTSpaceContext*,int){return 3;};
    api.m_getPosition=[](const SMikkTSpaceContext* c,float out[],int f,int v){std::copy_n(corner(c,f,v).position.data(),3,out);};
    api.m_getNormal=[](const SMikkTSpaceContext* c,float out[],int f,int v){std::copy_n(corner(c,f,v).normal.data(),3,out);};
    api.m_getTexCoord=[](const SMikkTSpaceContext* c,float out[],int f,int v){
        const auto& vertex=corner(c,f,v); const auto& coordinates=static_cast<TangentContext*>(c->m_pUserData)->uv?vertex.uv1:vertex.uv0; std::copy_n(coordinates.data(),2,out);
    };
    api.m_setTSpaceBasic=[](const SMikkTSpaceContext* c,const float tangent[],float sign,int f,int v){
        auto& target=corner(c,f,v).tangent; std::copy_n(tangent,3,target.begin()); target[3]=sign;
    };
    SMikkTSpaceContext context{&api,&user}; check(genTangSpaceDefault(&context)!=0,"MikkTSpace tangent generation failed");
}
std::optional<Accessor> attribute(const GltfSource& data,const Json& primitive,const std::string& name) {
    const auto& attrs=primitive.at("attributes");const auto found=attrs.find(name);if(found==attrs.end())return {};
    check(found->is_number_unsigned()||(found->is_number_integer()&&found->get<int64_t>()>=0),"invalid vertex attribute reference");return data.accessor(found->get<std::size_t>());
}
void deformation(const GltfSource& data,const Json& source,ModelPrimitive& primitive,bool hasNormals,bool hasTangents) {
    for(const auto& attr:source.at("attributes").items())if(attr.key().starts_with("JOINTS_")||attr.key().starts_with("WEIGHTS_"))
        check(attr.key()=="JOINTS_0"||attr.key()=="JOINTS_1"||attr.key()=="WEIGHTS_0"||attr.key()=="WEIGHTS_1","more than eight joint influences are unsupported (JOINTS/WEIGHTS sets beyond 1)");
    const bool skinned=attribute(data,source,"JOINTS_0").has_value()||attribute(data,source,"WEIGHTS_0").has_value();
    if(skinned){primitive.skinWeights.resize(primitive.vertices.size());primitive.hasSkinning=true;}
    for(int set=0;set<2;++set) {
        const auto joints=attribute(data,source,"JOINTS_"+std::to_string(set)),weights=attribute(data,source,"WEIGHTS_"+std::to_string(set));
        check(bool(joints)==bool(weights),"JOINTS and WEIGHTS attributes must be paired");if(!joints)continue;check(skinned,"JOINTS_1 requires JOINTS_0");
        check(joints->type=="VEC4"&&joints->count==primitive.vertices.size()&&!joints->normalized&&(joints->componentType==5121||joints->componentType==5123),"JOINTS must be unnormalized unsigned byte/short VEC4 attributes");
        check(weights->type=="VEC4"&&weights->count==primitive.vertices.size()&&(weights->componentType==5126||(weights->normalized&&(weights->componentType==5121||weights->componentType==5123))),"WEIGHTS must be float or normalized unsigned byte/short VEC4 attributes");
        const auto jointValues=floats(data,*joints),weightValues=floats(data,*weights);
        for(std::size_t vertex=0;vertex<primitive.vertices.size();++vertex)for(std::size_t component=0;component<4;++component){
            const auto offset=vertex*4+component;auto& target=primitive.skinWeights[vertex];check(weightValues[offset]>=0,"skin weights must be nonnegative");
            target.joints[std::size_t(set)*4+component]=std::uint16_t(jointValues[offset]);target.weights[std::size_t(set)*4+component]=weightValues[offset];}
    }
    for(auto& vertex:primitive.skinWeights){double sum{};for(float value:vertex.weights)sum+=value;check(sum>1e-12&&std::isfinite(sum),"skin vertex has zero total weight");for(float& weight:vertex.weights)weight=float(weight/sum);}
    const auto& targets=field(source,"targets");check(targets.is_null()||targets.is_array(),"invalid morph targets");
    check(targets.size()<=256,"more than 256 morph targets per primitive are unsupported");primitive.morphTargets.resize(targets.size());
    for(std::size_t targetIndex=0;targetIndex<targets.size();++targetIndex){
        const auto& sourceTarget=targets[targetIndex];auto& target=primitive.morphTargets[targetIndex];check(sourceTarget.is_object()&&!sourceTarget.empty(),"morph targets cannot be empty");
        for(const auto& attr:sourceTarget.items()){
            check(attr.key()=="POSITION"||attr.key()=="NORMAL"||attr.key()=="TANGENT","unsupported morph target attribute");
            const auto accessor=data.accessor(attr.value().get<std::size_t>());
            check(accessor.type=="VEC3"&&accessor.count==primitive.vertices.size()&&accessor.componentType==5126,"morph targets require matching FLOAT VEC3 POSITION/NORMAL/TANGENT deltas");
            std::vector<Vec3>* output=nullptr;
            if(attr.key()=="POSITION")output=&target.positions;
            else if(attr.key()=="NORMAL"){check(hasNormals,"morph NORMAL target requires a base NORMAL attribute");output=&target.normals;}
            else {if(!hasNormals)continue;check(hasTangents,"morph TANGENT target requires a base TANGENT attribute");output=&target.tangents;}
            const auto values=floats(data,accessor);output->resize(primitive.vertices.size());
            for(std::size_t vertex=0;vertex<output->size();++vertex){std::copy_n(values.data()+vertex*3,3,(*output)[vertex].begin());if(output==&target.positions)math::include(target.positionBounds,(*output)[vertex]);}
        }
    }
}
void geometry(const GltfSource& data,const aiScene& imported,ModelAsset& asset) {
    std::size_t importedMesh=data.table("materials").size(); // Skip the private material carrier.
    const auto& meshes=data.table("meshes");
    for(std::size_t meshIndex=0;meshIndex<meshes.size();++meshIndex){
        const auto& mesh=meshes[meshIndex];ModelMesh modelMesh;modelMesh.name=mesh.value("name",std::string{});if(modelMesh.name.empty())modelMesh.name="Mesh "+std::to_string(meshIndex+1);
        if(mesh.contains("weights"))modelMesh.weights=mesh["weights"].get<std::vector<float>>();
        check(std::all_of(modelMesh.weights.begin(),modelMesh.weights.end(),[](float value){return std::isfinite(value);}),"nonfinite mesh morph weight");
        const auto& primitives=mesh.at("primitives");
        for(std::size_t primitiveIndex=0;primitiveIndex<primitives.size();++primitiveIndex){
            const auto& source=primitives[primitiveIndex];const auto targetCount=field(source,"targets").size();
            check(!primitiveIndex||targetCount==field(primitives[0],"targets").size(),"mesh primitives must have matching morph target counts");
            check(modelMesh.weights.empty()||modelMesh.weights.size()==targetCount,"mesh morph weights must match its targets");
            const int mode=source.value("mode",4);check(mode==4||mode==5||mode==6,"point and line primitives are unsupported");
            const auto positions=attribute(data,source,"POSITION");check(positions&&positions->type=="VEC3"&&positions->componentType==5126&&positions->count<=MaxVertices,"primitive needs FLOAT VEC3 positions");
            const auto normals=attribute(data,source,"NORMAL");auto tangents=attribute(data,source,"TANGENT");const bool authoredTangents=tangents.has_value();if(!normals)tangents.reset();
            const auto uv0=attribute(data,source,"TEXCOORD_0"),uv1=attribute(data,source,"TEXCOORD_1"),colors=attribute(data,source,"COLOR_0");
            auto uvInterpolationCoordinates=attribute(data,source,"_CRIMSON_UV_INTERPOLATION_COORD"),uvInterpolationCorrection=attribute(data,source,"_CRIMSON_UV_INTERPOLATION_COEFF");
            const auto uvInterpolationMode=attribute(data,source,"_CRIMSON_UV_INTERPOLATION_MODE");
            const auto legacyCoordinates=attribute(data,source,"_CRIMSON_CIRCULAR_COORD"),legacyCorrection=attribute(data,source,"_CRIMSON_CIRCULAR_COEFF");
            check(bool(legacyCoordinates)==bool(legacyCorrection),"legacy circular UV attributes must be paired");
            const bool legacy=legacyCoordinates.has_value();
            check(!legacy||(!uvInterpolationCoordinates&&!uvInterpolationCorrection&&!uvInterpolationMode),"ambiguous legacy and current UV interpolation attributes");
            if(legacy){uvInterpolationCoordinates=legacyCoordinates;uvInterpolationCorrection=legacyCorrection;}
            check(bool(uvInterpolationCoordinates)==bool(uvInterpolationCorrection)&&
                (legacy||bool(uvInterpolationCoordinates)==bool(uvInterpolationMode)),"UV interpolation attributes must be paired with their mode");
            std::vector<float> interpolationCoordinateValues,interpolationCorrectionValues,interpolationModeValues;
            if(uvInterpolationCoordinates){
                check(uv0.has_value(),"UV interpolation attributes require UV0");
                for(const auto* value:{&*uvInterpolationCoordinates,&*uvInterpolationCorrection})
                    check(value->count==positions->count&&value->type=="VEC4"&&value->componentType==5126&&!value->normalized,
                        "UV interpolation attributes require matching FLOAT VEC4 values");
                interpolationCoordinateValues=floats(data,*uvInterpolationCoordinates);interpolationCorrectionValues=floats(data,*uvInterpolationCorrection);
                if(uvInterpolationMode){
                    check(uvInterpolationMode->count==positions->count&&uvInterpolationMode->type=="SCALAR"&&uvInterpolationMode->componentType==5126&&!uvInterpolationMode->normalized,
                        "UV interpolation mode requires matching FLOAT SCALAR values");
                    interpolationModeValues=floats(data,*uvInterpolationMode);
                }
            }
            ModelPrimitive p;const auto materialIndex=number(source,"material",data.table("materials").size());check(materialIndex<asset.materials.size(),"primitive references missing material");p.material=uint32_t(materialIndex);
            const auto& material=asset.materials[p.material];p.uvMask=uint8_t((uv0?1:0)|(uv1?2:0));
            uint32_t authoredTangentUv=0;
            if(source.contains("material")){const auto& normalTexture=field(data.item("materials",number(source,"material")),"normalTexture");if(!normalTexture.is_null())authoredTangentUv=uint32_t(number(normalTexture,"texCoord"));}
            if(!material.triplanar&&!material.normalTexture.image.empty()&&material.normalTexture.texCoord!=authoredTangentUv){if(tangents)warn(asset,"Normal-map UV selection changed; authored tangents were regenerated.");tangents.reset();}
            p.tangentUv=tangents?uint8_t(authoredTangentUv):material.normalTexture.image.empty()||material.triplanar?0xff:uint8_t(material.normalTexture.texCoord);
            for(const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}){
                check(slot->texCoord<=1,"material references a UV set beyond UV1");check(material.triplanar||slot->image.empty()||(slot->texCoord?uv1:uv0).has_value(),"material references a missing UV set");}
            const auto validateAttribute=[&](const std::optional<Accessor>& value,std::size_t components){if(value){check(value->count==positions->count&&value->components==components,"vertex attribute count or shape mismatch");(void)floats(data,*value);}};
            validateAttribute(positions,3);validateAttribute(normals,3);validateAttribute(tangents,4);validateAttribute(uv0,2);validateAttribute(uv1,2);
            if(normals)check(normals->componentType==5126,"NORMAL must use floating point components");if(tangents)check(tangents->componentType==5126,"TANGENT must use floating point components");
            for(const auto* value:{&uv0,&uv1,&colors})if(*value)check((*value)->componentType==5126||((*value)->normalized&&((*value)->componentType==5121||(*value)->componentType==5123)),"UV/color attributes must be floats or normalized unsigned bytes/shorts");
            if(colors){check(colors->components==3||colors->components==4,"COLOR_0 must have three or four components");validateAttribute(colors,colors->components);}
            check(importedMesh<imported.mNumMeshes,"Assimp lost a source primitive");const auto& decoded=*imported.mMeshes[importedMesh++];
            check(decoded.mNumVertices==positions->count&&decoded.mVertices,"Assimp changed source vertex correspondence");
            check(!normals||decoded.HasNormals(),"Assimp lost vertex normals");check(!tangents||decoded.HasTangentsAndBitangents(),"Assimp lost vertex tangents");
            check(!uv0||decoded.HasTextureCoords(0),"Assimp lost UV0");check(!uv1||decoded.HasTextureCoords(1),"Assimp lost UV1");check(!colors||decoded.HasVertexColors(0),"Assimp lost vertex colors");
            std::vector<float> tangentValues;if(tangents){tangentValues=floats(data,*tangents);for(std::size_t v=0;v<tangents->count;++v)check(tangentValues[v*4+3]==1||tangentValues[v*4+3]==-1,"tangent handedness must be +1 or -1");}
            p.vertices.resize(decoded.mNumVertices);
            for(std::size_t v=0;v<p.vertices.size();++v){auto& vertex=p.vertices[v];const auto& position=decoded.mVertices[v];vertex.position={position.x,position.y,position.z};
                if(normals){const auto& n=decoded.mNormals[v];vertex.normal=normalized({n.x,n.y,n.z});}
                if(tangents){const auto& t=decoded.mTangents[v];const auto tangent=normalized({t.x,t.y,t.z},{1,0,0});
                    vertex.tangent={tangent[0],tangent[1],tangent[2],tangentValues[v*4+3]};}
                for(std::size_t uv=0;uv<2;++uv)if(uv?bool(uv1):bool(uv0)){const auto& value=decoded.mTextureCoords[uv][v];auto& targetUv=uv?vertex.uv1:vertex.uv0;targetUv={value.x,1.f-value.y};}
                if(uvInterpolationCoordinates){
                    std::copy_n(interpolationCoordinateValues.data()+v*4,4,vertex.uvInterpolationCoordinates.begin());
                    std::copy_n(interpolationCorrectionValues.data()+v*4,4,vertex.uvInterpolationCorrection.begin());
                    vertex.uvInterpolationMode=legacy?(vertex.uvInterpolationCoordinates[2]>0?1.f:0.f):interpolationModeValues[v];
                    check((vertex.uvInterpolationMode==0||vertex.uvInterpolationMode==1||vertex.uvInterpolationMode==2)&&
                        (vertex.uvInterpolationMode!=1||vertex.uvInterpolationCoordinates[2]>0)&&
                        (vertex.uvInterpolationMode!=0||(vertex.uvInterpolationCoordinates==Vec4{}&&vertex.uvInterpolationCorrection==Vec4{})),
                        "invalid UV interpolation corner attributes");
                }
                if(colors){const auto& color=decoded.mColors[0][v];vertex.color={color.r,color.g,color.b,colors->components==3?1.f:color.a};check(std::all_of(vertex.color.begin(),vertex.color.end(),[](float value){return value>=0&&value<=1;}),"vertex color must be in [0, 1]");}
            }
            deformation(data,source,p,normals.has_value(),authoredTangents);
            std::vector<uint32_t> input;if(source.contains("indices"))input=indices(data,data.accessor(number(source,"indices")));else{input.resize(p.vertices.size());std::iota(input.begin(),input.end(),0u);}
            check(input.size()>=3&&std::all_of(input.begin(),input.end(),[&](uint32_t vertex){return vertex<p.vertices.size();}),"triangle indices are out of bounds or incomplete");
            if(mode==4){check(input.size()%3==0,"triangle index count must be divisible by three");p.indices=std::move(input);}
            else{check(input.size()<=MaxVertices,"triangle strip/fan is too large");p.indices.reserve((input.size()-2)*3);
                for(std::size_t i=2;i<input.size();++i){uint32_t a=mode==6?input[0]:input[i-2],b=input[i-1],c=input[i];if(mode==5&&(i&1))std::swap(a,b);if(a!=b&&b!=c&&a!=c)p.indices.insert(p.indices.end(),{a,b,c});}
                check(!p.indices.empty(),"primitive has no nondegenerate triangles");}
            const bool needTangents=!material.triplanar && !material.normalTexture.image.empty() && !tangents;
            if(uvInterpolationCoordinates)for(std::size_t triangle=0;triangle<p.indices.size();triangle+=3){
                const auto& a=p.vertices[p.indices[triangle]];const auto& b=p.vertices[p.indices[triangle+1]];const auto& c=p.vertices[p.indices[triangle+2]];
                check(a.uvInterpolationMode==b.uvInterpolationMode&&a.uvInterpolationMode==c.uvInterpolationMode&&
                    a.uvInterpolationCorrection==b.uvInterpolationCorrection&&a.uvInterpolationCorrection==c.uvInterpolationCorrection,
                    "UV interpolation triangle must retain one interpolation field");
            }
            if (!normals || needTangents) {
                std::vector<ModelVertex> corners; corners.reserve(p.indices.size());
                for (auto index:p.indices) corners.push_back(p.vertices[index]);
                if (!normals) {
                    warn(asset,"Missing vertex normals were generated as flat face normals.");
                    for (size_t i=0;i<corners.size();i+=3) {
                        const auto n=normalized(cross(subtract(corners[i+1].position,corners[i].position),subtract(corners[i+2].position,corners[i].position)));
                        corners[i].normal=corners[i+1].normal=corners[i+2].normal=n;
                    }
                }
                if (needTangents) { generateTangents(corners,material.normalTexture.texCoord); warn(asset,"Missing normal-map tangents were generated with MikkTSpace."); }
                // Morph normals and tangents must follow the same generated
                // triangle corners as the base mesh. Keep corner data until
                // reindexing, since one source vertex may split across faces.
                std::vector<std::vector<Vec3>> generatedNormals(p.morphTargets.size()),generatedTangents(p.morphTargets.size());
                for(std::size_t targetIndex=0;targetIndex<p.morphTargets.size();++targetIndex) {
                    auto& target=p.morphTargets[targetIndex];
                    if(normals&&!needTangents)continue;
                    auto targetCorners=corners;
                    for(std::size_t cornerIndex=0;cornerIndex<targetCorners.size();++cornerIndex) {
                        const auto index=p.indices[cornerIndex];auto& vertex=targetCorners[cornerIndex];
                        if(!target.positions.empty())vertex.position=vertex.position+target.positions[index];
                        if(normals&&!target.normals.empty())vertex.normal=normalized(vertex.normal+target.normals[index]);
                    }
                    if(!normals) {
                        generatedNormals[targetIndex].resize(corners.size());
                        for(std::size_t face=0;face<targetCorners.size();face+=3) {
                            const auto normal=normalized(cross(subtract(targetCorners[face+1].position,targetCorners[face].position),subtract(targetCorners[face+2].position,targetCorners[face].position)));
                            for(std::size_t corner=0;corner<3;++corner) {
                                targetCorners[face+corner].normal=normal;generatedNormals[targetIndex][face+corner]=normal-corners[face+corner].normal;
                            }
                        }
                        target.tangents.clear();
                    }
                    if(needTangents) {
                        generateTangents(targetCorners,material.normalTexture.texCoord);generatedTangents[targetIndex].resize(corners.size());
                        for(std::size_t corner=0;corner<corners.size();++corner)for(std::size_t axis=0;axis<3;++axis)
                            generatedTangents[targetIndex][corner][axis]=targetCorners[corner].tangent[axis]-corners[corner].tangent[axis];
                    }
                }
                // Reindex complete corner attributes, so discontinuous face normals
                // and tangent orientations split while identical corners share.
                const auto sourceIndices=p.indices;auto skinWeights=std::move(p.skinWeights);auto morphTargets=std::move(p.morphTargets);
                p.morphTargets.resize(morphTargets.size());
                for(std::size_t targetIndex=0;targetIndex<morphTargets.size();++targetIndex)p.morphTargets[targetIndex].positionBounds=morphTargets[targetIndex].positionBounds;
                std::unordered_map<std::string,uint32_t> unique; p.vertices.clear(); p.indices.clear(); p.indices.reserve(corners.size());
                static_assert(sizeof(ModelVertex)==27*sizeof(float));
                for (std::size_t cornerIndex=0;cornerIndex<corners.size();++cornerIndex) {
                    auto& v=corners[cornerIndex];const auto sourceIndex=sourceIndices[cornerIndex];
                    check(std::all_of(v.tangent.begin(),v.tangent.end(),[](float x){return std::isfinite(x);}),"nonfinite generated tangent");
                    std::string key(reinterpret_cast<const char*>(&v),sizeof(v));
                    if(!skinWeights.empty())key.append(reinterpret_cast<const char*>(&skinWeights[sourceIndex]),sizeof(ModelSkinWeights));
                    for(std::size_t targetIndex=0;targetIndex<morphTargets.size();++targetIndex) {
                        const auto& target=morphTargets[targetIndex];
                        if(!target.positions.empty())key.append(reinterpret_cast<const char*>(&target.positions[sourceIndex]),sizeof(Vec3));
                        const auto& normalDelta=generatedNormals[targetIndex].empty()?target.normals:generatedNormals[targetIndex];
                        const auto& tangentDelta=generatedTangents[targetIndex].empty()?target.tangents:generatedTangents[targetIndex];
                        if(!normalDelta.empty())key.append(reinterpret_cast<const char*>(&normalDelta[generatedNormals[targetIndex].empty()?sourceIndex:cornerIndex]),sizeof(Vec3));
                        if(!tangentDelta.empty())key.append(reinterpret_cast<const char*>(&tangentDelta[generatedTangents[targetIndex].empty()?sourceIndex:cornerIndex]),sizeof(Vec3));
                    }
                    const auto [it,inserted]=unique.emplace(std::move(key),static_cast<uint32_t>(p.vertices.size()));
                    if (inserted) {
                        p.vertices.push_back(v);if(!skinWeights.empty())p.skinWeights.push_back(skinWeights[sourceIndex]);
                        for(std::size_t targetIndex=0;targetIndex<morphTargets.size();++targetIndex) {
                            const auto& original=morphTargets[targetIndex];auto& target=p.morphTargets[targetIndex];
                            if(!original.positions.empty())target.positions.push_back(original.positions[sourceIndex]);
                            if(!generatedNormals[targetIndex].empty())target.normals.push_back(generatedNormals[targetIndex][cornerIndex]);
                            else if(!original.normals.empty())target.normals.push_back(original.normals[sourceIndex]);
                            if(!generatedTangents[targetIndex].empty())target.tangents.push_back(generatedTangents[targetIndex][cornerIndex]);
                            else if(!original.tangents.empty())target.tangents.push_back(original.tangents[sourceIndex]);
                        }
                    } p.indices.push_back(it->second);
                }
            }
            for(const auto& vertex:p.vertices)math::include(p.bounds,vertex.position);
            for(std::size_t vertex=0;vertex<p.skinWeights.size();++vertex)for(std::size_t influence=0;influence<MaximumModelJointInfluences;++influence) {
                const auto& weights=p.skinWeights[vertex];if(weights.weights[influence]<=0)continue;const auto joint=std::size_t(weights.joints[influence]);
                if(p.skinJointBounds.size()<=joint)p.skinJointBounds.resize(joint+1);math::include(p.skinJointBounds[joint],p.vertices[vertex].position);
            }
            p.sourceVertexCount=p.vertices.size();p.sourceIndexCount=p.indices.size();
            modelMesh.primitives.push_back(static_cast<uint32_t>(asset.primitives.size())); asset.primitives.push_back(std::move(p));
        }
        asset.meshes.push_back(std::move(modelMesh));
    }
}

void hierarchy(const GltfSource& data,ModelAsset& asset) {
    const auto& nodes=data.table("nodes");check(nodes.size()<=MaxNodes,"too many hierarchy nodes");asset.nodes.resize(nodes.size());std::vector<uint32_t> parents(nodes.size());
    for(std::size_t i=0;i<nodes.size();++i){const auto& source=nodes[i];auto& node=asset.nodes[i];node.name=source.value("name",std::string{});if(node.name.empty())node.name="Node "+std::to_string(i+1);
        node.matrixAuthored=source.contains("matrix");if(source.contains("skin")){const auto skin=number(source,"skin");check(skin<data.table("skins").size(),"invalid mesh skin reference");node.skin=int32_t(skin);}
        vectorValue(field(source,"translation"),node.translation);vectorValue(field(source,"scale"),node.scale);vectorValue(field(source,"rotation"),node.rotation);
        check(!node.matrixAuthored||(!source.contains("translation")&&!source.contains("rotation")&&!source.contains("scale")),"node cannot combine matrix and TRS transforms");
        if(node.matrixAuthored)vectorValue(source["matrix"],node.local);else node.local=math::multiply(math::multiply(math::translation(node.translation),math::rotation(node.rotation)),math::scaling(node.scale));
        check(std::all_of(node.local.begin(),node.local.end(),[](float value){return std::isfinite(value);}),"nonfinite node transform");check(math::isAffine(node.local,1e-6),"node transform must be affine");
        if(source.contains("mesh")){const auto index=number(source,"mesh");check(index<asset.meshes.size(),"node references missing mesh");node.mesh=int32_t(index);const auto& mesh=asset.meshes[index];
            const auto morphCount=mesh.primitives.empty()?0:asset.primitives[mesh.primitives.front()].morphTargets.size();
            if(source.contains("weights")){node.weights=source["weights"].get<std::vector<float>>();check(node.weights.size()==morphCount,"node morph weights must match its targets");}
            else if(!mesh.weights.empty())node.weights=mesh.weights;else node.weights.resize(morphCount);
        }else check(!source.contains("weights")&&!source.contains("skin"),"skin and morph weights require a mesh node");
        if(source.contains("children"))for(const auto& value:source["children"]){const auto child=number(Json{{"index",value}},"index");check(child<nodes.size()&&++parents[child]==1,"node hierarchy has multiple parents or duplicate children");node.children.push_back(uint32_t(child));asset.nodes[child].parent=int32_t(i);}
    }
    // Validate all nodes, including nodes outside the selected scene.
    std::vector<uint32_t> pending;auto remaining=parents;for(uint32_t i=0;i<remaining.size();++i)if(!remaining[i])pending.push_back(i);
    for(std::size_t i=0;i<pending.size();++i)for(auto child:asset.nodes[pending[i]].children)if(--remaining[child]==0)pending.push_back(child);check(pending.size()==asset.nodes.size(),"node hierarchy contains a cycle");
    const auto& scenes=data.table("scenes");
    if(!scenes.empty()){const auto index=number(data.json,"scene");check(index<scenes.size(),"missing default scene");std::set<uint32_t> roots;
        if(scenes[index].contains("nodes"))for(const auto& value:scenes[index]["nodes"]){const auto root=number(Json{{"index",value}},"index");check(root<parents.size()&&!parents[root]&&roots.insert(uint32_t(root)).second,"scene root has a parent or is duplicated");asset.roots.push_back(uint32_t(root));}}
    else for(uint32_t i=0;i<parents.size();++i)if(!parents[i])asset.roots.push_back(i);
    struct Entry{uint32_t node;ModelMatrix parent;};std::vector<Entry> stack;for(auto it=asset.roots.rbegin();it!=asset.roots.rend();++it)stack.push_back({*it,ModelIdentity});
    while(!stack.empty()){const auto entry=stack.back();stack.pop_back();const auto& node=asset.nodes[entry.node];const auto world=multiply(entry.parent,node.local);
        if(node.mesh>=0){const bool singular=math::handedness(world)==0;
            if(singular)warn(asset,(node.skin>=0?"Skin joint transforms replace singular mesh placement on node: ":!data.table("animations").empty()?"Retained animated draw with singular bind transform on node: ":"Skipped singular transform on node: ")+node.name);
            if(!singular||!data.table("animations").empty()||node.skin>=0)for(auto primitive:asset.meshes[std::size_t(node.mesh)].primitives){asset.draws.push_back({primitive,world,int32_t(entry.node)});const auto& geometry=asset.primitives[primitive];for(auto index:geometry.indices)include(asset.bounds,geometry.vertices[index].position,world);}}
        for(auto it=node.children.rbegin();it!=node.children.rend();++it)stack.push_back({*it,world});
    }
    check(!asset.primitives.empty(),"asset contains no supported triangle geometry");if(asset.draws.empty())warn(asset,"The selected scene has no drawable geometry.");
}
void skins(const GltfSource& data,ModelAsset& asset) {
    const auto& sources=data.table("skins");check(sources.size()<=MaxNodes,"too many skins");asset.skins.reserve(sources.size());
    for(std::size_t skinIndex=0;skinIndex<sources.size();++skinIndex){const auto& source=sources[skinIndex];ModelSkin skin;skin.name=source.value("name",std::string{});if(skin.name.empty())skin.name="Skin "+std::to_string(skinIndex+1);
        check(source.contains("joints")&&source["joints"].is_array()&&!source["joints"].empty()&&source["joints"].size()<=65536,"skin must contain between 1 and 65536 joints");
        for(const auto& joint:source["joints"]){const auto index=number(Json{{"index",joint}},"index");check(index<asset.nodes.size(),"skin has a missing joint");skin.joints.push_back(uint32_t(index));}
        if(source.contains("skeleton")){const auto index=number(source,"skeleton");check(index<asset.nodes.size(),"skin has a missing skeleton");skin.skeleton=int32_t(index);}
        skin.inverseBindMatrices.resize(skin.joints.size());
        if(source.contains("inverseBindMatrices")){const auto accessor=data.accessor(number(source,"inverseBindMatrices"));check(accessor.type=="MAT4"&&accessor.componentType==5126&&accessor.count>=skin.joints.size(),"inverse bind matrices must be FLOAT MAT4 and cover every joint");
            const auto values=floats(data,accessor);for(std::size_t joint=0;joint<skin.joints.size();++joint)std::copy_n(values.data()+joint*16,16,skin.inverseBindMatrices[joint].begin());}
        asset.skins.push_back(std::move(skin));
    }
    for(const auto& node:asset.nodes)if(node.skin>=0){check(std::size_t(node.skin)<asset.skins.size()&&node.mesh>=0,"invalid mesh skin reference");const auto& skin=asset.skins[std::size_t(node.skin)];
        for(auto primitive:asset.meshes[std::size_t(node.mesh)].primitives){const auto& geometry=asset.primitives[primitive];check(!geometry.skinWeights.empty(),"skinned mesh requires JOINTS_0 and WEIGHTS_0 attributes");
            for(const auto& vertex:geometry.skinWeights)for(std::size_t influence=0;influence<MaximumModelJointInfluences;++influence)check(vertex.joints[influence]<skin.joints.size(),"vertex joint index exceeds its skin joint count");}}
}
void recoverTripoBindPose(const GltfSource& data,ModelAsset& asset) {
    // Some static Tripo exports retain inverse bind matrices but omit every
    // node transform. Recover that exporter-specific bind pose in derived data;
    // an authored identity transform or any animation remains authoritative.
    const auto& header=field(data.json,"asset");
    if(!header.is_object()||header.value("generator",std::string{})!="Tripo"||!data.table("animations").empty())return;
    for(const auto& node:data.table("nodes"))
        if(node.contains("matrix")||node.contains("translation")||node.contains("rotation")||node.contains("scale"))return;
    std::vector<bool> usedSkins(asset.skins.size());
    for(const auto& node:asset.nodes)if(node.skin>=0)usedSkins[std::size_t(node.skin)]=true;
    std::vector<std::optional<ModelMatrix>> jointWorlds(asset.nodes.size());
    const auto sameMatrix=[](const ModelMatrix& left,const ModelMatrix& right) {
        for(std::size_t component=0;component<16;++component)
            if(std::abs(double(left[component])-right[component])>1e-5*std::max({1.0,std::abs(double(left[component])),std::abs(double(right[component]))}))return false;
        return true;
    };
    bool changed=false;
    for(std::size_t skinIndex=0;skinIndex<asset.skins.size();++skinIndex)if(usedSkins[skinIndex]) {
        const auto& skin=asset.skins[skinIndex];
        if(!data.item("skins",skinIndex).contains("inverseBindMatrices"))return;
        for(std::size_t joint=0;joint<skin.joints.size();++joint) {
            const auto& inverseBind=skin.inverseBindMatrices[joint];
            if(!math::isAffine(inverseBind))return;
            const auto world=math::inverse(inverseBind);
            if(!world||!math::isAffine(*world))return;
            auto& existing=jointWorlds[skin.joints[joint]];
            if(existing&&!sameMatrix(*existing,*world))return;
            existing=*world;changed|=!sameMatrix(*world,ModelIdentity);
        }
    }
    if(!changed)return;
    // Resolve the complete hierarchy before mutating anything, including shared
    // joints and non-joint intermediates. Conflicting or singular data is left
    // on the normal validation path rather than partially repairing a skin.
    std::vector<ModelMatrix> locals(asset.nodes.size()),worlds(asset.nodes.size());
    std::vector<std::size_t> order;order.reserve(asset.nodes.size());
    for(std::size_t index=0;index<asset.nodes.size();++index)if(asset.nodes[index].parent<0)order.push_back(index);
    for(std::size_t current=0;current<order.size();++current) {
        const auto index=order[current];const auto& node=asset.nodes[index];
        const auto parent=node.parent<0?ModelIdentity:worlds[std::size_t(node.parent)];
        locals[index]=node.local;
        if(jointWorlds[index]) {
            const auto inverseParent=math::inverse(parent);if(!inverseParent)return;
            locals[index]=math::multiply(*inverseParent,*jointWorlds[index]);worlds[index]=*jointWorlds[index];
        }else worlds[index]=math::multiply(parent,locals[index]);
        if(!math::isAffine(locals[index])||!math::isAffine(worlds[index]))return;
        for(const auto child:node.children)order.push_back(child);
    }
    if(order.size()!=asset.nodes.size())return;
    for(std::size_t index=0;index<asset.nodes.size();++index)if(jointWorlds[index]) {
        asset.nodes[index].local=locals[index];asset.nodes[index].matrixAuthored=true;
    }
    for(auto& draw:asset.draws)if(draw.node>=0)draw.transform=worlds[std::size_t(draw.node)];
    warn(asset,"Recovered missing Tripo skeleton bind transforms from inverse bind matrices.");
}
void animations(const GltfSource& data,ModelAsset& asset) {
    const auto& sources=data.table("animations");check(sources.size()<=MaxNodes,"too many animation clips");asset.animations.reserve(sources.size());
    for(std::size_t clipIndex=0;clipIndex<sources.size();++clipIndex){const auto& source=sources[clipIndex];ModelAnimationClip clip;clip.name=source.value("name",std::string{});if(clip.name.empty())clip.name="Animation "+std::to_string(clipIndex+1);
        const auto& channels=source.at("channels");check(channels.is_array()&&!channels.empty()&&channels.size()<=MaxNodes,"animation must contain channels");
        clip.start=std::numeric_limits<float>::max();clip.end=0;std::set<std::pair<uint32_t,ModelAnimationPath>> targets;
        for(const auto& sourceChannel:channels){ModelAnimationChannel channel;check(sourceChannel.contains("target")&&sourceChannel["target"].contains("node")&&sourceChannel.contains("sampler"),"animation channel requires a target node and sampler");
            const auto& target=sourceChannel["target"];const auto targetNode=number(target,"node");check(targetNode<asset.nodes.size(),"animation channel targets missing node");channel.node=uint32_t(targetNode);const auto& node=asset.nodes[channel.node];const auto property=target.value("path",std::string{});
            if(property=="translation"){channel.path=ModelAnimationPath::Translation;channel.components=3;}
            else if(property=="rotation"){channel.path=ModelAnimationPath::Rotation;channel.components=4;}
            else if(property=="scale"){channel.path=ModelAnimationPath::Scale;channel.components=3;}
            else if(property=="weights"){channel.path=ModelAnimationPath::Weights;channel.components=uint32_t(node.weights.size());check(channel.components>0,"animated morph weights require mesh targets");}
            else fail("unsupported animation channel path");
            check(targets.emplace(channel.node,channel.path).second,"animation contains duplicate channels for one node property");check(channel.path==ModelAnimationPath::Weights||!node.matrixAuthored,"TRS animation cannot target a matrix-authored node");
            const auto& samplers=source.at("samplers");const auto samplerIndex=number(sourceChannel,"sampler");check(samplerIndex<samplers.size(),"animation channel references missing sampler");const auto& sampler=samplers[samplerIndex];
            check(sampler.contains("input")&&sampler.contains("output"),"animation sampler requires input and output accessors");const auto input=data.accessor(number(sampler,"input")),output=data.accessor(number(sampler,"output"));
            check(input.type=="SCALAR"&&input.componentType==5126&&!input.normalized,"animation times must be FLOAT SCALAR");channel.times=floats(data,input);
            for(std::size_t key=0;key<channel.times.size();++key)check(channel.times[key]>=0&&(!key||channel.times[key]>channel.times[key-1]),"animation times must be nonnegative and strictly increasing");
            const auto interpolation=sampler.value("interpolation",std::string{"LINEAR"});
            if(interpolation=="STEP")channel.interpolation=ModelAnimationInterpolation::Step;else if(interpolation=="LINEAR")channel.interpolation=ModelAnimationInterpolation::Linear;
            else if(interpolation=="CUBICSPLINE"){channel.interpolation=ModelAnimationInterpolation::CubicSpline;check(channel.times.size()>=2,"CUBICSPLINE animation requires at least two keys");}else fail("unsupported animation interpolation");
            const bool weights=channel.path==ModelAnimationPath::Weights;
            const bool normalizedOutput=(weights||channel.path==ModelAnimationPath::Rotation)&&output.normalized&&(output.componentType==5120||output.componentType==5121||output.componentType==5122||output.componentType==5123);
            check(((output.componentType==5126&&!output.normalized)||normalizedOutput)&&(weights?output.type=="SCALAR":output.type==(channel.components==4?"VEC4":"VEC3")),"animation output accessor shape or component type is invalid");
            channel.values=floats(data,output);const auto multiplier=channel.interpolation==ModelAnimationInterpolation::CubicSpline?3u:1u;
            check(channel.values.size()==channel.times.size()*channel.components*multiplier,"animation sampler output count differs from its input keys");
            clip.start=std::min(clip.start,channel.times.front());clip.end=std::max(clip.end,channel.times.back());clip.channels.push_back(std::move(channel));
        }asset.animations.push_back(std::move(clip));
    }
}

std::string modelIdentity(const fs::path& root,const fs::path& directory) {
    if(const auto id=assetIdAtPath(root,directory/"manifest.json");!id.empty())return canonicalAssetId(id);
    return canonicalAssetId("assets/models/"+utf8(directory.filename())+"/manifest.json");
}
fs::path modelStagingRoot(const fs::path& root) {
    return contentStorageReady(root)?resolveAssetPath(root,"assets/.staging-models"):root/"assets/models";
}
bool modelIdentityAvailable(const fs::path& root,const fs::path& directory) {
    const auto id=modelIdentity(root,directory);const auto file=fs::weakly_canonical(directory/"manifest.json");
    for(const auto& stored:storedAssetPaths(root))if(stored.id==id&&stored.file!=file)return false;
    return true;
}
bool modelManifestLocation(const fs::path& root,const fs::path& file) {
    if(file.filename()!="manifest.json")return false;
    if(inside(file,fs::weakly_canonical(contentStorageReady(root)?root/"Content":root/"assets/models")))return true;
    if(contentStorageReady(root)&&inside(file,fs::weakly_canonical(root/".crimson/cache/quader-imports"))) {
        // Generated packages are addressable only through a registered member
        // of a tracked bundle. An arbitrary cache manifest is not an asset.
        const auto id=assetIdAtPath(root,file);const auto source=quaderBundleSource(root,id);
        if(!isAssetUuid(id)||!source||!fs::is_regular_file(*source)||source->extension()!=".crimson-quader")return false;
        const auto owner=assetIdAtPath(root,*source);
        return isAssetUuid(owner)&&file==fs::weakly_canonical(quaderBundleCacheDirectory(root,owner,id)/"manifest.json")&&
            inside(fs::weakly_canonical(*source),fs::weakly_canonical(root/"Content"));
    }
    const auto privateRoot=fs::weakly_canonical(contentStorageReady(root)?root/".crimson":root/"assets");
    const auto relative=file.lexically_relative(privateRoot);
    if(relative.empty()||relative.is_absolute()||*relative.begin()=="..")return false;
    return utf8(*relative.begin()).starts_with(".staging-quader-");
}
std::shared_ptr<ModelAsset> load(const fs::path& manifestFile,const fs::path& root,const fs::path& identityDirectory,const ModelResolvers& resolvers={},bool metadataOnly=false) {
    const auto bytes=read(manifestFile); const auto manifest=Json::parse(bytes);
    check(manifest.value("version",0)==1,"unsupported model package version");
    const auto directory=manifestFile.parent_path(), documentPath=managedPath(directory,manifest.at("document").get<std::string>());
    GltfSource data;data.json=Json::parse(read(documentPath));
    check(data.json.is_object()&&data.json.contains("asset")&&data.json["asset"].value("version",std::string{})=="2.0","unsupported glTF version");
    check(!data.json.contains("extensionsRequired")||data.json["extensionsRequired"].empty(),"unsupported required extension");
    check(data.table("nodes").size()<=MaxNodes&&data.table("meshes").size()<=MaxNodes&&data.table("materials").size()<=MaxNodes,"asset table exceeds supported size");
    std::set<std::string> dependencies;
    for (const auto& dependency:manifest.at("dependencies")) {
        const auto file=dependency.at("path").get<std::string>(); managedPath(directory,file); dependencies.insert(file);
    }
    if(!metadataOnly)data.buffers.reserve(data.table("buffers").size());
    for(const auto& buffer:data.table("buffers")){
        const auto uri=buffer.value("uri",std::string{});check(!uri.empty()&&dependencies.contains(uri),"buffer is not a declared managed dependency");
        const auto source=managedPath(directory,uri);const auto size=number(buffer,"byteLength");
        check(fs::is_regular_file(source)&&size>0&&size<=MaxBytes&&fs::file_size(source)==size,"managed buffer size mismatch");
        if(!metadataOnly)data.buffers.push_back(read(source));
    }
    if(!metadataOnly)validateAccessors(data);
    auto asset=std::make_shared<ModelAsset>(); asset->name=manifest.at("name").get<std::string>(); asset->format=manifest.at("format").get<std::string>();
    asset->id=modelIdentity(root,identityDirectory);
    if(data.json.contains("extensionsUsed"))for(const auto& extension:data.json["extensionsUsed"])warn(*asset,"Optional extension ignored: "+extension.get<std::string>());
    if(!data.table("cameras").empty()||(data.json.contains("extensions")&&data.json["extensions"].contains("KHR_lights_punctual")))warn(*asset,"Imported cameras and lights are ignored; preview lighting and camera remain active.");
    std::vector<std::string> images;images.reserve(data.table("images").size());
    for(const auto& image:data.table("images")){
        const auto uri=image.value("uri",std::string{});check(!uri.empty()&&dependencies.contains(uri),"image is not a declared managed dependency");
        const auto id=derivedAssetId(asset->id,"image:"+uri);
        const auto replacement=resolvers.image?resolvers.image(id):id;
        if(replacement.empty() || isEngineAssetId(replacement)) { images.push_back(id);asset->images.push_back(id);continue; }
        auto source=managedPath(directory,uri);
        if(directory==identityDirectory&&contentStorageReady(root))source=resolveAssetPath(root,id);
        if(replacement!=id)source=resolveAssetPath(root,replacement);
        if(metadataOnly){check(fs::is_regular_file(source),"managed material image is missing");const auto ext=lower(utf8(source.extension()));
            if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".tga"||ext==".bmp"){images.push_back(id);asset->images.push_back(id);}
            else{images.emplace_back();warn(*asset,"An image used only by an unsupported optional extension is retained but not displayed.");}continue;}
        const auto imageBytes=read(source);
        if (replacement==id && imageExtension(imageBytes).empty()) {
            images.emplace_back(); warn(*asset,"An image used only by an unsupported optional extension is retained but not displayed.");
        } else {
            validateImage(imageBytes);
            images.push_back(id); asset->images.push_back(id);
        }
    }
    std::unique_ptr<Assimp::Importer> importer;
    if(!metadataOnly)importer=importAssimp(data);
    materials(data,images,*asset,importer?importer->GetScene():nullptr);
    const auto explicitMaterials=manifest.find("materialAssets");
    const bool needsFallback=asset->materials.size()>data.table("materials").size();
    if(explicitMaterials!=manifest.end()) {
        check(explicitMaterials->is_array() && std::all_of(explicitMaterials->begin(),explicitMaterials->end(),[](const Json& value){return value.is_string();}),
            "materialAssets must be an array of material identities");
        if(!needsFallback && explicitMaterials->size()==asset->materials.size()+1) {
            // Old immutable packages included the loader's unconditional fallback.
            // Discard only that known unused entry, preserving all authored indices.
            const auto trailing=canonicalAssetId(explicitMaterials->back().get<std::string>());
            check(trailing==DefaultMaterialId || trailing==importedMaterialId(asset->id,asset->materials.size()),
                "materialAssets has an unexpected extra material slot");
            asset->legacyUnusedMaterialSlot=asset->materials.size();
        } else check(explicitMaterials->size()==asset->materials.size(),"materialAssets must match the material slots");
    } else if(!needsFallback) {
        // Generic glTF packages have implicit imported material identities. Their
        // old unused fallback can still occur in saved scene override vectors.
        asset->legacyUnusedMaterialSlot=asset->materials.size();
    }
    for(size_t i=0;i<asset->materials.size();++i) {
        const auto id=explicitMaterials!=manifest.end()?canonicalAssetId(explicitMaterials->at(i).get<std::string>()):importedMaterialId(asset->id,i);
        if(explicitMaterials!=manifest.end() && id!=DefaultMaterialId)(void)materialAssetPath(root,id);
        asset->materialIds.push_back(id);
        if(resolvers.material)if(auto replacement=resolvers.material(id))asset->materials[i]=std::move(*replacement);
    }
    if(!metadataOnly) {
        geometry(data,*importer->GetScene(),*asset);hierarchy(data,*asset);skins(data,*asset);recoverTripoBindPose(data,*asset);animations(data,*asset);validateModelAnimationData(*asset);
        const bool deformed=!asset->skins.empty()||std::any_of(asset->primitives.begin(),asset->primitives.end(),[](const auto& primitive){return !primitive.morphTargets.empty();});
        if(deformed)asset->bounds=evaluateModelPose(*asset).bounds;
    }return asset;
}

} // namespace

ModelImportStage::~ModelImportStage() {
    if((published&&!reused)||staging.empty())return;
    try {const auto resolved=fs::weakly_canonical(staging),parent=fs::weakly_canonical(modelStagingRoot(root));
        if(resolved.parent_path()!=parent||!utf8(resolved.filename()).starts_with(".staging-model-"))return;
        std::error_code ignored;fs::remove_all(resolved,ignored);
    }catch(...) { /* A staged package has no published identity. Never widen cleanup. */ }
}
std::shared_ptr<ModelImportStage> stageModelImport(const fs::path& source,const fs::path& projectRoot) {
    const auto ext=lower(utf8(source.extension())); check(ext==".gltf" || ext==".glb","unsupported model extension");
    const auto root=fs::weakly_canonical(fs::absolute(projectRoot));if(!contentStorageReady(root))initializeContentStorage(root);const auto models=resolveAssetPath(root,"assets/models"),stages=modelStagingRoot(root);
    fs::create_directories(models);fs::create_directories(stages); static std::atomic<uint64_t> sequence{};
    std::string id; fs::path destination,stage;
    do {
        id="model-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
        destination=models/path(id); stage=stages/path(".staging-"+id);
    } while (fs::exists(destination) || fs::exists(stage));
    check(fs::create_directory(stage),"could not create a unique model staging directory");
    auto result=std::make_shared<ModelImportStage>();result->root=root;result->staging=stage;result->destination=destination;result->sourceName=utf8(source.stem());
    const auto original=read(source);auto manifest=stageDocument(source,stage,original,ext==".glb");
    writeJson(stage/"manifest.json",manifest);
    auto identity=identifyModelImport(stage);result->contentIdentity=identity.digest;result->inputFiles=std::move(identity.files);
    manifest["importIdentity"]={{"version",1},{"sha256",result->contentIdentity}};writeJson(stage/"manifest.json",manifest);
    result->inputFiles.back()={stage/"manifest.json",fs::file_size(stage/"manifest.json"),fs::last_write_time(stage/"manifest.json")};
    const auto reserved=reservedModelImportIds(root);
    const auto matching=findMatchingModelImport(root,result->contentIdentity,reserved);
    if(matching) {destination=matching->directory;result->reuseFiles=matching->files;}
    else {
        const auto base="model-"+result->contentIdentity;
        for(std::uint32_t suffix=0;;++suffix) {
            destination=models/path(base+(suffix?"-"+std::to_string(suffix):std::string{}));
            if(!fs::exists(destination) && !reserved.contains(modelIdentity(root,destination))&&modelIdentityAvailable(root,destination))break;
        }
    }
    result->destination=destination;result->asset=load(stage/"manifest.json",root,destination);
    if(matching)result->asset->name=matching->name;
    return result;
}
namespace {
void retargetImport(ModelAsset& asset,const fs::path& root,const fs::path& directory,const fs::path& stagedManifest) {
    const auto previousModel=asset.id,next=modelIdentity(root,directory);
    std::ifstream input(stagedManifest);const auto manifest=Json::parse(input);
    std::unordered_map<std::string,std::string> images;
    for(const auto& member:manifest.at("dependencies"))if(member.at("kind")=="image") {
        const auto role="image:"+member.at("path").get<std::string>();images.emplace(derivedAssetId(previousModel,role),derivedAssetId(next,role));
    }
    const auto remap=[&](std::string& id){if(const auto found=images.find(id);found!=images.end())id=found->second;};
    asset.id=next;for(auto& id:asset.images)remap(id);
    for(auto& material:asset.materials)for(auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture})remap(texture->image.id);
    for(std::size_t i=0;i<asset.materialIds.size();++i)if(asset.materialIds[i]==importedMaterialId(previousModel,i))asset.materialIds[i]=importedMaterialId(next,i);
    if(asset.stream) {auto stream=std::make_shared<streaming::ModelStream>(*asset.stream);stream->id=asset.id;asset.stream=std::move(stream);}
}
bool importReserved(const ModelAsset& asset,const std::function<bool(const std::string&)>& reserved) {
    if(reserved(asset.id))return true;
    for(const auto& id:asset.images)if(reserved(id))return true;
    for(const auto& id:asset.materialIds)if(reserved(id))return true;
    return false;
}
}
std::shared_ptr<ModelAsset> publishModelImport(const std::shared_ptr<const ModelImportStage>& stage,const std::function<bool(const std::string&)>& reservedId) {
    check(stage&&stage->asset,"model import stage is unavailable");
    static std::mutex publication;const std::lock_guard lock(publication);
    struct Published {std::string digest,id;fs::path root,directory;std::vector<ModelImportFileStamp> files;};
    static std::unordered_map<std::string,Published> published;
    static std::deque<std::string> recent;
    if(stage->published)return stage->asset;
    const auto root=fs::weakly_canonical(stage->root),models=fs::weakly_canonical(resolveAssetPath(root,"assets/models"));
    const auto staging=fs::weakly_canonical(stage->staging);auto destination=fs::weakly_canonical(stage->destination);
    check(staging.parent_path()==fs::weakly_canonical(modelStagingRoot(root))&&utf8(staging.filename()).starts_with(".staging-model-"),"model import staging path is invalid");
    check(modelImportFilesUnchanged(stage->inputFiles),"model import staging files changed before publication");
    const auto reserved=reservedId?std::unordered_set<std::string>{}:reservedModelImportIds(root);
    const std::function<bool(const std::string&)> unavailable=reservedId?reservedId:[&](const auto& id){return reserved.contains(id);};
    const auto workerDestination=destination;
    const auto unchangedAt=[&](const auto& files,const fs::path& previous,const fs::path& current) {
        auto relocated=files;
        for(auto& file:relocated)file.file=current/file.file.lexically_relative(previous);
        return modelImportFilesUnchanged(relocated);
    };
    const auto reuse=[&](const fs::path& candidate) {
        if(!fs::is_directory(candidate))return false;
        bool verified=false;
        if(!stage->reuseFiles.empty())try {
            const auto current=resolveAssetPath(root,stage->asset->id).parent_path();
            if(candidate==workerDestination||candidate==current)verified=unchangedAt(stage->reuseFiles,workerDestination,candidate);
        }catch(const std::exception&){/* A fresh staged UUID may not be indexed yet. */}
        if(!verified)for(const auto& [key,trusted]:published) {
            (void)key;if(trusted.root!=root||trusted.digest!=stage->contentIdentity)continue;
            try{if(resolveAssetPath(root,trusted.id).parent_path()==candidate&&unchangedAt(trusted.files,trusted.directory,candidate)){verified=true;break;}}
            catch(const std::exception&){/* A failed/deleted publication cannot be reused. */}
        }
        if(!verified)return false;
        retargetImport(*stage->asset,root,candidate,staging/"manifest.json");if(importReserved(*stage->asset,unavailable))return false;
        // Another worker may have prepared this exact package concurrently. Only
        // the publication thread decides identity; no existing file is replaced.
        std::ifstream input(candidate/"manifest.json");const auto metadata=Json::parse(input);stage->asset->name=metadata.at("name").get<std::string>();
        stage->destination=candidate;fs::remove_all(staging);stage->reused=true;stage->published=true;return true;
    };
    // A preceding file in this batch may already have moved the shared package
    // into its captured Content folder. Follow its stable ID before considering
    // the original default publication path or creating another package.
    try{if(reuse(resolveAssetPath(root,stage->asset->id).parent_path()))return stage->asset;}
    catch(const std::exception&){/* Fresh UUID: no published location yet. */}
    for(const auto& [key,trusted]:published) {
        (void)key;if(trusted.root!=root||trusted.digest!=stage->contentIdentity)continue;
        try{if(reuse(resolveAssetPath(root,trusted.id).parent_path()))return stage->asset;}
        catch(const std::exception&){/* Reserved or missing candidate remains unavailable. */}
    }
    if(reuse(destination))return stage->asset;
    const auto base="model-"+stage->contentIdentity;
    for(std::uint32_t suffix=0;;++suffix) {
        destination=models/path(base+(suffix?"-"+std::to_string(suffix):std::string{}));
        if(reuse(destination))return stage->asset;
        if(fs::exists(destination))continue;
        if(!modelIdentityAvailable(root,destination))continue;
        retargetImport(*stage->asset,root,destination,staging/"manifest.json");if(importReserved(*stage->asset,unavailable))continue;
        break;
    }
    stage->asset->name=stage->sourceName;
    auto inputs=stage->inputFiles;for(auto& input:inputs)input.file=destination/input.file.lexically_relative(staging);
    const auto key=lower(utf8(destination));stage->destination=destination;
    std::erase(recent,key);recent.push_back(key);
    try {published[key]={stage->contentIdentity,stage->asset->id,root,destination,std::move(inputs)};fs::rename(staging,destination);}
    catch(...) {published.erase(key);recent.pop_back();throw;}
    // This bridge covers overlapping staged jobs; normal later imports validate
    // managed bytes on a worker. It is not an unbounded residency cache.
    while(recent.size()>256) {published.erase(recent.front());recent.pop_front();}
    if(contentStorageReady(root)) {
        const auto logical="assets/models/"+utf8(destination.filename());
        registerAssetPath(root,logical+"/manifest.json",destination/"manifest.json");
        std::ifstream input(destination/"manifest.json");const auto manifest=Json::parse(input);
        for(const auto& member:manifest.at("dependencies"))if(member.at("kind")=="image") {
            const auto name=member.at("path").get<std::string>();registerAssetPath(root,logical+"/"+name,destination/path(name));
        }
    }
    stage->published=true;return stage->asset;
}
std::shared_ptr<ModelAsset> importModelPackage(const fs::path& source,const fs::path& projectRoot) {
    return publishModelImport(stageModelImport(source,projectRoot));
}
std::shared_ptr<ModelAsset> loadModelPackage(const fs::path& manifest,const fs::path& projectRoot,const ModelResolvers& resolvers) {
    const auto root=fs::weakly_canonical(fs::absolute(projectRoot)); const auto file=fs::weakly_canonical(manifest.is_absolute()?manifest:resolveAssetPath(root,utf8(manifest)));
    check(modelManifestLocation(root,file),"model manifest is outside the managed model library");
    return load(file,root,file.parent_path(),resolvers);
}
std::shared_ptr<ModelAsset> loadModelMaterialMetadata(const fs::path& manifest,const fs::path& projectRoot,const ModelResolvers& resolvers) {
    const auto root=fs::weakly_canonical(fs::absolute(projectRoot));const auto file=fs::weakly_canonical(manifest.is_absolute()?manifest:resolveAssetPath(root,utf8(manifest)));
    check(modelManifestLocation(root,file),"model manifest is outside the managed model library");
    return load(file,root,file.parent_path(),resolvers,true);
}

} // namespace crimson
