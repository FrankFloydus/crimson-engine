#include "ModelAsset.h"
#include "ModelAnimation.h"
#include "MaterialAsset.h"
#include "ContentStorage.h"
#include "QuaderSceneBundle.h"
#include "resources/AssetId.h"
#include "resources/StreamingModel.h"

// cgltf is header-only. Keep its implementation in the CPU asset boundary.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
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

uint32_t unsignedValue(const uint8_t* p, cgltf_component_type type) {
    if (type==cgltf_component_type_r_8u) return *p;
    if (type==cgltf_component_type_r_16u) return uint32_t(p[0]) | (uint32_t(p[1])<<8);
    if (type==cgltf_component_type_r_32u) return u32(p);
    fail("indices must be unsigned 8, 16, or 32 bit values");
}
void validateAccessors(cgltf_data& data) {
    for (size_t i=0; i<data.buffer_views_count; ++i) {
        const auto& view=data.buffer_views[i]; check(view.buffer!=nullptr,"buffer view has no buffer");
        range(view.offset,view.size,view.buffer->size,"buffer view");
        check(!view.stride || (view.stride>=4 && view.stride<=252 && view.stride%4==0),"invalid buffer view stride");
    }
    for (size_t i=0; i<data.accessors_count; ++i) {
        const auto& a=data.accessors[i]; const auto component=cgltf_component_size(a.component_type), element=cgltf_calc_size(a.type,a.component_type);
        check(component>0 && element>0 && a.count>0 && a.count<=MaxVertices*3,"invalid accessor type or count");
        check(a.offset%component==0 && a.stride>=element && a.stride%component==0,"invalid accessor stride or alignment");
        check(!a.normalized || a.component_type!=cgltf_component_type_r_32f,"float attributes cannot be normalized");
        if (a.buffer_view) {
            check((a.buffer_view->offset+a.offset)%component==0,"unaligned accessor");
            check(a.offset<=a.buffer_view->size && element<=a.buffer_view->size-a.offset &&
                  a.count-1<=(a.buffer_view->size-a.offset-element)/a.stride,"accessor exceeds its buffer view");
        } else check(a.offset==0,"accessor without a buffer view has a byte offset");
        if (!a.is_sparse) continue;
        const auto& s=a.sparse; const auto indexSize=cgltf_component_size(s.indices_component_type);
        check(s.count>0 && s.count<=a.count && s.indices_buffer_view && s.values_buffer_view,"invalid sparse accessor");
        check(s.indices_component_type==cgltf_component_type_r_8u || s.indices_component_type==cgltf_component_type_r_16u ||
              s.indices_component_type==cgltf_component_type_r_32u,"invalid sparse index type");
        check(!s.indices_buffer_view->stride && !s.values_buffer_view->stride,"sparse values and indices must be tightly packed");
        range(s.indices_byte_offset,s.count*indexSize,s.indices_buffer_view->size,"sparse indices");
        range(s.values_byte_offset,s.count*element,s.values_buffer_view->size,"sparse values");
        check((s.indices_buffer_view->offset+s.indices_byte_offset)%indexSize==0 &&
              (s.values_buffer_view->offset+s.values_byte_offset)%component==0,"unaligned sparse accessor");
        const auto* values=cgltf_buffer_view_data(s.indices_buffer_view)+s.indices_byte_offset; uint32_t previous=0;
        for (size_t j=0; j<s.count; ++j) {
            const auto value=unsignedValue(values+j*indexSize,s.indices_component_type);
            check(value<a.count && (j==0 || value>previous),"sparse indices must be increasing and within accessor bounds"); previous=value;
        }
    }
}
std::vector<float> floats(const cgltf_accessor& accessor) {
    const size_t components=cgltf_num_components(accessor.type); std::vector<float> result(accessor.count*components);
    auto base=accessor; base.is_sparse=false;
    check(cgltf_accessor_unpack_floats(&base,result.data(),result.size())==result.size(),"could not unpack vertex attribute");
    if (accessor.is_sparse) {
        // cgltf 1.15 uses the base stride for sparse values; glTF sparse values
        // are tightly packed even when the base attribute is interleaved.
        const auto& s=accessor.sparse; auto values=base; values.buffer_view=s.values_buffer_view;
        values.offset=s.values_byte_offset; values.stride=cgltf_calc_size(base.type,base.component_type); values.count=s.count;
        const auto* indices=cgltf_buffer_view_data(s.indices_buffer_view)+s.indices_byte_offset;
        for (size_t j=0; j<s.count; ++j) {
            const auto index=unsignedValue(indices+j*cgltf_component_size(s.indices_component_type),s.indices_component_type);
            check(cgltf_accessor_read_float(&values,j,result.data()+index*components,components)!=0,"invalid sparse vertex value");
        }
    }
    check(std::all_of(result.begin(),result.end(),[](float f){return std::isfinite(f);}),"nonfinite vertex attribute"); return result;
}
std::vector<uint32_t> indices(const cgltf_accessor& accessor) {
    check(accessor.type==cgltf_type_scalar && !accessor.normalized,"invalid triangle index accessor");
    const size_t size=cgltf_component_size(accessor.component_type); std::vector<uint32_t> result(accessor.count);
    if (accessor.buffer_view) {
        const auto* source=cgltf_buffer_view_data(accessor.buffer_view)+accessor.offset;
        for (size_t i=0;i<accessor.count;++i) result[i]=unsignedValue(source+i*accessor.stride,accessor.component_type);
    } else { uint8_t zero[4]{}; unsignedValue(zero,accessor.component_type); }
    if (accessor.is_sparse) {
        const auto& s=accessor.sparse; const auto* source=cgltf_buffer_view_data(s.values_buffer_view)+s.values_byte_offset;
        const auto* mapping=cgltf_buffer_view_data(s.indices_buffer_view)+s.indices_byte_offset;
        for (size_t i=0;i<s.count;++i) result[unsignedValue(mapping+i*cgltf_component_size(s.indices_component_type),s.indices_component_type)]=unsignedValue(source+i*size,accessor.component_type);
    }
    return result;
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

ModelWrap wrap(cgltf_wrap_mode value) {
    if (value==cgltf_wrap_mode_repeat) return ModelWrap::Repeat;
    if (value==cgltf_wrap_mode_clamp_to_edge) return ModelWrap::Clamp;
    if (value==cgltf_wrap_mode_mirrored_repeat) return ModelWrap::Mirror;
    fail("unsupported sampler wrapping mode");
}
ModelFilter filter(cgltf_filter_type value,bool minification,ModelAsset& asset) {
    (void)asset;
    if (value==cgltf_filter_type_undefined || value==cgltf_filter_type_linear) return ModelFilter::Linear;
    if (value==cgltf_filter_type_nearest) return ModelFilter::Nearest;
    check(minification && value>=cgltf_filter_type_nearest_mipmap_nearest && value<=cgltf_filter_type_linear_mipmap_linear,"invalid texture filter");
    switch(value) {
    case cgltf_filter_type_nearest_mipmap_nearest:return ModelFilter::NearestMipmapNearest;
    case cgltf_filter_type_linear_mipmap_nearest:return ModelFilter::LinearMipmapNearest;
    case cgltf_filter_type_nearest_mipmap_linear:return ModelFilter::NearestMipmapLinear;
    case cgltf_filter_type_linear_mipmap_linear:return ModelFilter::LinearMipmapLinear;
    default:fail("invalid texture filter");
    }
}
ModelTexture texture(const cgltf_texture_view& view,const cgltf_data& data,const std::vector<std::string>& images,ModelAsset& asset) {
    ModelTexture result; if (!view.texture) return result;
    check(view.texture->image!=nullptr,"texture has no supported core image fallback");
    check(view.texcoord>=0 && view.texcoord<=1,"material references a UV set beyond UV1");
    result.image=AssetRef<DecodedImage>{images.at(static_cast<size_t>(view.texture->image-data.images))}; result.texCoord=static_cast<uint32_t>(view.texcoord);
    check(!result.image.empty(),"core material texture has no supported PNG/JPEG image");
    if (view.has_transform) warn(asset,"Optional KHR_texture_transform ignored; core texture coordinates are used.");
    if (const auto* sampler=view.texture->sampler) {
        result.wrapU=wrap(sampler->wrap_s); result.wrapV=wrap(sampler->wrap_t);
        result.minFilter=filter(sampler->min_filter,true,asset); result.magFilter=filter(sampler->mag_filter,false,asset);
    }
    return result;
}
void materials(const cgltf_data& data,const std::vector<std::string>& images,ModelAsset& asset) {
    bool needsFallback=false;
    for(size_t mesh=0;mesh<data.meshes_count;++mesh)
        for(size_t primitive=0;primitive<data.meshes[mesh].primitives_count;++primitive)
            needsFallback|=data.meshes[mesh].primitives[primitive].material==nullptr;
    asset.materials.reserve(data.materials_count+(needsFallback?1:0));
    for (size_t i=0;i<data.materials_count;++i) {
        const auto& source=data.materials[i]; ModelMaterial m; m.name=source.name&&source.name[0]?source.name:"Material "+std::to_string(i+1);
        if (source.has_pbr_metallic_roughness) {
            const auto& p=source.pbr_metallic_roughness; std::copy_n(p.base_color_factor,4,m.baseColor.begin());
            m.metallic=p.metallic_factor; m.roughness=p.roughness_factor;
            m.baseColorTexture=texture(p.base_color_texture,data,images,asset);
            m.metallicRoughnessTexture=texture(p.metallic_roughness_texture,data,images,asset);
        }
        m.normalTexture=texture(source.normal_texture,data,images,asset); if (source.normal_texture.texture) m.normalScale=source.normal_texture.scale;
        m.occlusionTexture=texture(source.occlusion_texture,data,images,asset); if (source.occlusion_texture.texture) m.occlusionStrength=source.occlusion_texture.scale;
        m.emissiveTexture=texture(source.emissive_texture,data,images,asset); std::copy_n(source.emissive_factor,3,m.emissive.begin());
        m.alphaMask=source.alpha_mode==cgltf_alpha_mode_mask; m.alphaCutoff=source.alpha_cutoff; m.doubleSided=source.double_sided!=0;
        m.alphaBlend=source.alpha_mode==cgltf_alpha_mode_blend;
        auto unit=[](float value){return std::isfinite(value) && value>=0 && value<=1;};
        check(std::all_of(m.baseColor.begin(),m.baseColor.end(),unit) && unit(m.metallic) && unit(m.roughness) && unit(m.occlusionStrength),"material factors must be in [0, 1]: "+m.name);
        check(std::isfinite(m.normalScale) && std::isfinite(m.alphaCutoff) && m.alphaCutoff>=0 &&
              std::all_of(m.emissive.begin(),m.emissive.end(),unit),"invalid normal, alpha, or emissive factor: "+m.name);
        asset.materials.push_back(std::move(m));
    }
    if(needsFallback) {
        ModelMaterial fallback; fallback.name="Default material"; asset.materials.push_back(std::move(fallback));
    }
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
const cgltf_accessor* attribute(const cgltf_primitive& primitive,cgltf_attribute_type type,int index=0) {
    const cgltf_accessor* found=nullptr;
    for (size_t i=0;i<primitive.attributes_count;++i) if (primitive.attributes[i].type==type && primitive.attributes[i].index==index) {
        check(!found,"duplicate vertex attribute"); found=primitive.attributes[i].data;
    }
    return found;
}
void deformation(const cgltf_primitive& source,ModelPrimitive& primitive,bool hasNormals,bool hasTangents) {
    for(std::size_t index=0;index<source.attributes_count;++index) {
        const auto& attr=source.attributes[index];
        if(attr.type==cgltf_attribute_type_joints||attr.type==cgltf_attribute_type_weights)
            check(attr.index>=0&&attr.index<2,"more than eight joint influences are unsupported (JOINTS/WEIGHTS sets beyond 1)");
    }
    const bool skinned=attribute(source,cgltf_attribute_type_joints)||attribute(source,cgltf_attribute_type_weights);
    if(skinned){primitive.skinWeights.resize(primitive.vertices.size());primitive.hasSkinning=true;}
    for(int set=0;set<2;++set) {
        const auto* joints=attribute(source,cgltf_attribute_type_joints,set);const auto* weights=attribute(source,cgltf_attribute_type_weights,set);
        check(bool(joints)==bool(weights),"JOINTS and WEIGHTS attributes must be paired");if(!joints)continue;
        check(skinned,"JOINTS_1 requires JOINTS_0");
        check(joints->type==cgltf_type_vec4&&joints->count==primitive.vertices.size()&&!joints->normalized&&
            (joints->component_type==cgltf_component_type_r_8u||joints->component_type==cgltf_component_type_r_16u),"JOINTS must be unnormalized unsigned byte/short VEC4 attributes");
        check(weights->type==cgltf_type_vec4&&weights->count==primitive.vertices.size()&&
            (weights->component_type==cgltf_component_type_r_32f||
             (weights->normalized&&(weights->component_type==cgltf_component_type_r_8u||weights->component_type==cgltf_component_type_r_16u))),
            "WEIGHTS must be float or normalized unsigned byte/short VEC4 attributes");
        const auto jointValues=floats(*joints),weightValues=floats(*weights);
        for(std::size_t vertex=0;vertex<primitive.vertices.size();++vertex)for(std::size_t component=0;component<4;++component) {
            const auto offset=vertex*4+component;auto& target=primitive.skinWeights[vertex];
            check(weightValues[offset]>=0,"skin weights must be nonnegative");
            target.joints[std::size_t(set)*4+component]=std::uint16_t(jointValues[offset]);target.weights[std::size_t(set)*4+component]=weightValues[offset];
        }
    }
    for(auto& vertex:primitive.skinWeights) {
        double sum{};for(float value:vertex.weights)sum+=value;check(sum>1e-12&&std::isfinite(sum),"skin vertex has zero total weight");
        for(float& weight:vertex.weights)weight=float(weight/sum);
    }
    check(source.targets_count<=256,"more than 256 morph targets per primitive are unsupported");
    primitive.morphTargets.resize(source.targets_count);
    for(std::size_t targetIndex=0;targetIndex<source.targets_count;++targetIndex) {
        const auto& sourceTarget=source.targets[targetIndex];auto& target=primitive.morphTargets[targetIndex];
        for(std::size_t index=0;index<sourceTarget.attributes_count;++index) {
            const auto& attr=sourceTarget.attributes[index];const auto* accessor=attr.data;
            check(attr.index==0&&accessor&&accessor->type==cgltf_type_vec3&&accessor->count==primitive.vertices.size()&&accessor->component_type==cgltf_component_type_r_32f,
                "morph targets require matching FLOAT VEC3 POSITION/NORMAL/TANGENT deltas");
            std::vector<Vec3>* output=nullptr;
            if(attr.type==cgltf_attribute_type_position)output=&target.positions;
            else if(attr.type==cgltf_attribute_type_normal){check(hasNormals,"morph NORMAL target requires a base NORMAL attribute");output=&target.normals;}
            else if(attr.type==cgltf_attribute_type_tangent){if(!hasNormals)continue;check(hasTangents,"morph TANGENT target requires a base TANGENT attribute");output=&target.tangents;}
            else fail("unsupported morph target attribute");
            check(output->empty(),"duplicate morph target attribute");const auto values=floats(*accessor);output->resize(primitive.vertices.size());
            for(std::size_t vertex=0;vertex<output->size();++vertex) {
                std::copy_n(values.data()+vertex*3,3,(*output)[vertex].begin());
                if(output==&target.positions)math::include(target.positionBounds,(*output)[vertex]);
            }
        }
        check(sourceTarget.attributes_count>0,"morph targets cannot be empty");
    }
}
void geometry(const cgltf_data& data,ModelAsset& asset) {
    for (size_t meshIndex=0;meshIndex<data.meshes_count;++meshIndex) {
        const auto& mesh=data.meshes[meshIndex]; ModelMesh target; target.name=mesh.name?mesh.name:"Mesh "+std::to_string(meshIndex+1);
        if(mesh.weights_count)target.weights.assign(mesh.weights,mesh.weights+mesh.weights_count);
        check(std::all_of(target.weights.begin(),target.weights.end(),[](float value){return std::isfinite(value);}),"nonfinite mesh morph weight");
        for (size_t primitiveIndex=0;primitiveIndex<mesh.primitives_count;++primitiveIndex) {
            const auto& source=mesh.primitives[primitiveIndex];
            check(!primitiveIndex||source.targets_count==mesh.primitives[0].targets_count,"mesh primitives must have matching morph target counts");
            check(!mesh.weights_count||mesh.weights_count==source.targets_count,"mesh morph weights must match its targets");
            check(source.type==cgltf_primitive_type_triangles || source.type==cgltf_primitive_type_triangle_strip || source.type==cgltf_primitive_type_triangle_fan,"point and line primitives are unsupported");
            const auto* positions=attribute(source,cgltf_attribute_type_position);
            check(positions && positions->type==cgltf_type_vec3 && positions->component_type==cgltf_component_type_r_32f && positions->count<=MaxVertices,"primitive needs FLOAT VEC3 positions");
            const auto* normals=attribute(source,cgltf_attribute_type_normal); const auto* tangents=attribute(source,cgltf_attribute_type_tangent);
            const bool authoredTangents=tangents!=nullptr;
            // glTF requires ignoring authored tangents when normals are absent.
            if (!normals) tangents=nullptr;
            const auto* uv0=attribute(source,cgltf_attribute_type_texcoord,0); const auto* uv1=attribute(source,cgltf_attribute_type_texcoord,1);
            const auto* colors=attribute(source,cgltf_attribute_type_color);
            ModelPrimitive p; p.material=source.material?static_cast<uint32_t>(source.material-data.materials):static_cast<uint32_t>(data.materials_count);
            const auto& material=asset.materials[p.material];
            p.uvMask=static_cast<uint8_t>((uv0?1:0)|(uv1?2:0));
            const auto authoredTangentUv=source.material&&source.material->normal_texture.texture?static_cast<uint32_t>(source.material->normal_texture.texcoord):0u;
            if(!material.triplanar&&!material.normalTexture.image.empty()&&material.normalTexture.texCoord!=authoredTangentUv){
                if(tangents)warn(asset,"Normal-map UV selection changed; authored tangents were regenerated.");tangents=nullptr;
            }
            p.tangentUv=tangents?static_cast<uint8_t>(authoredTangentUv):material.normalTexture.image.empty()||material.triplanar?0xff:static_cast<uint8_t>(material.normalTexture.texCoord);
            for (const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
                check(slot->texCoord<=1,"material references a UV set beyond UV1");
                check(material.triplanar || slot->image.empty() || (slot->texCoord?uv1:uv0),"material references a missing UV set");
            }
            const auto positionValues=floats(*positions); p.vertices.resize(positions->count);
            for (size_t v=0;v<positions->count;++v) std::copy_n(positionValues.data()+v*3,3,p.vertices[v].position.begin());
            auto apply=[&](const cgltf_accessor* a,size_t components,auto assignment) {
                if (!a) return; check(a->count==positions->count && cgltf_num_components(a->type)==components,"vertex attribute count or shape mismatch");
                const auto values=floats(*a); for (size_t v=0;v<p.vertices.size();++v) assignment(p.vertices[v],values.data()+v*components);
            };
            if (normals) check(normals->component_type==cgltf_component_type_r_32f,"NORMAL must use floating point components");
            if (tangents) check(tangents->component_type==cgltf_component_type_r_32f,"TANGENT must use floating point components");
            for (const auto* uv:{uv0,uv1,colors}) if (uv) check(uv->component_type==cgltf_component_type_r_32f ||
                (uv->normalized && (uv->component_type==cgltf_component_type_r_8u || uv->component_type==cgltf_component_type_r_16u)),"UV/color attributes must be floats or normalized unsigned bytes/shorts");
            apply(normals,3,[&](ModelVertex& v,const float* f){v.normal=normalized({f[0],f[1],f[2]});});
            apply(tangents,4,[&](ModelVertex& v,const float* f){
                check(f[3]==1 || f[3]==-1,"tangent handedness must be +1 or -1"); const auto t=normalized({f[0],f[1],f[2]},{1,0,0}); v.tangent={t[0],t[1],t[2],f[3]};
            });
            apply(uv0,2,[](ModelVertex& v,const float* f){std::copy_n(f,2,v.uv0.begin());});
            apply(uv1,2,[](ModelVertex& v,const float* f){std::copy_n(f,2,v.uv1.begin());});
            if (colors) {
                const auto count=cgltf_num_components(colors->type); check(count==3 || count==4,"COLOR_0 must have three or four components");
                apply(colors,count,[count](ModelVertex& v,const float* f){std::copy_n(f,count,v.color.begin());
                    check(std::all_of(v.color.begin(),v.color.end(),[](float x){return x>=0 && x<=1;}),"vertex color must be in [0, 1]");});
            }
            deformation(source,p,normals!=nullptr,authoredTangents);
            std::vector<uint32_t> input;
            if (source.indices) input=indices(*source.indices); else { input.resize(p.vertices.size()); std::iota(input.begin(),input.end(),0u); }
            check(input.size()>=3 && std::all_of(input.begin(),input.end(),[&](uint32_t v){return v<p.vertices.size();}),"triangle indices are out of bounds or incomplete");
            if (source.type==cgltf_primitive_type_triangles) { check(input.size()%3==0,"triangle index count must be divisible by three"); p.indices=std::move(input); }
            else {
                check(input.size()<=MaxVertices,"triangle strip/fan is too large"); p.indices.reserve((input.size()-2)*3);
                for (size_t i=2;i<input.size();++i) {
                    uint32_t a=source.type==cgltf_primitive_type_triangle_fan?input[0]:input[i-2], b=input[i-1], c=input[i];
                    if (source.type==cgltf_primitive_type_triangle_strip && (i&1)) std::swap(a,b);
                    if (a!=b && b!=c && a!=c) p.indices.insert(p.indices.end(),{a,b,c});
                }
                check(!p.indices.empty(),"primitive has no nondegenerate triangles");
            }
            const bool needTangents=!material.triplanar && !material.normalTexture.image.empty() && !tangents;
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
                static_assert(sizeof(ModelVertex)==18*sizeof(float));
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
            target.primitives.push_back(static_cast<uint32_t>(asset.primitives.size())); asset.primitives.push_back(std::move(p));
        }
        asset.meshes.push_back(std::move(target));
    }
}

void hierarchy(const cgltf_data& data,ModelAsset& asset) {
    check(data.nodes_count<=MaxNodes,"too many hierarchy nodes"); asset.nodes.resize(data.nodes_count);
    std::vector<uint32_t> parents(data.nodes_count);
    for (size_t i=0;i<data.nodes_count;++i) {
        const auto& source=data.nodes[i]; auto& node=asset.nodes[i]; node.name=source.name?source.name:"Node "+std::to_string(i+1);
        node.matrixAuthored=source.has_matrix!=0;
        if(source.skin)node.skin=static_cast<std::int32_t>(source.skin-data.skins);
        if(source.has_translation)std::copy_n(source.translation,3,node.translation.begin());
        if(source.has_scale)std::copy_n(source.scale,3,node.scale.begin());
        if(source.has_rotation)std::copy_n(source.rotation,4,node.rotation.begin());
        check(!source.has_matrix || (!source.has_translation && !source.has_rotation && !source.has_scale),"node cannot combine matrix and TRS transforms");
        cgltf_node_transform_local(&source,node.local.data());
        check(std::all_of(node.local.begin(),node.local.end(),[](float x){return std::isfinite(x);}),"nonfinite node transform");
        check(std::abs(node.local[3])<1e-6f && std::abs(node.local[7])<1e-6f && std::abs(node.local[11])<1e-6f && std::abs(node.local[15]-1)<1e-6f,"node transform must be affine");
        if (source.mesh) {
            node.mesh=static_cast<int32_t>(source.mesh-data.meshes);const auto& mesh=asset.meshes[std::size_t(node.mesh)];
            const auto morphCount=mesh.primitives.empty()?0:asset.primitives[mesh.primitives.front()].morphTargets.size();
            check(!source.weights_count||source.weights_count==morphCount,"node morph weights must match its targets");
            if(source.weights_count)node.weights.assign(source.weights,source.weights+source.weights_count);
            else if(!mesh.weights.empty())node.weights=mesh.weights;else node.weights.resize(morphCount);
        } else check(!source.weights_count&&!source.skin,"skin and morph weights require a mesh node");
        for (size_t j=0;j<source.children_count;++j) {
            const auto child=static_cast<uint32_t>(source.children[j]-data.nodes);
            check(child<data.nodes_count && ++parents[child]==1,"node hierarchy has multiple parents or duplicate children"); node.children.push_back(child);asset.nodes[child].parent=static_cast<std::int32_t>(i);
        }
    }
    // Kahn traversal validates all nodes, including nodes outside the active scene,
    // without recursive calls on adversarially deep source hierarchies.
    std::vector<uint32_t> pending; auto remaining=parents;
    for (uint32_t i=0;i<remaining.size();++i) if (!remaining[i]) pending.push_back(i);
    for (size_t i=0;i<pending.size();++i) for (auto child:asset.nodes[pending[i]].children) if (--remaining[child]==0) pending.push_back(child);
    check(pending.size()==asset.nodes.size(),"node hierarchy contains a cycle");
    const auto* scene=data.scene?data.scene:data.scenes_count?&data.scenes[0]:nullptr;
    if (scene) {
        std::set<uint32_t> roots;
        for (size_t i=0;i<scene->nodes_count;++i) {
            const auto index=static_cast<uint32_t>(scene->nodes[i]-data.nodes);
            check(!parents[index] && roots.insert(index).second,"scene root has a parent or is duplicated"); asset.roots.push_back(index);
        }
    } else for (uint32_t i=0;i<parents.size();++i) if (!parents[i]) asset.roots.push_back(i);
    struct Entry { uint32_t node; ModelMatrix parent; }; std::vector<Entry> stack;
    for (auto it=asset.roots.rbegin();it!=asset.roots.rend();++it) stack.push_back({*it,ModelIdentity});
    while (!stack.empty()) {
        auto entry=stack.back(); stack.pop_back(); const auto& node=asset.nodes[entry.node]; const auto world=multiply(entry.parent,node.local);
        if (node.mesh>=0) {
            const bool singular=math::handedness(world)==0;
            if(singular)warn(asset,(node.skin>=0?"Skin joint transforms replace singular mesh placement on node: ":
                data.animations_count?"Retained animated draw with singular bind transform on node: ":"Skipped singular transform on node: ")+node.name);
            if(!singular||data.animations_count||node.skin>=0)for (auto primitive:asset.meshes[static_cast<size_t>(node.mesh)].primitives) {
                asset.draws.push_back({primitive,world,static_cast<std::int32_t>(entry.node)}); const auto& mesh=asset.primitives[primitive];
                for (auto index:mesh.indices) include(asset.bounds,mesh.vertices[index].position,world);
            }
        }
        for (auto it=node.children.rbegin();it!=node.children.rend();++it) stack.push_back({*it,world});
    }
    check(!asset.primitives.empty(),"asset contains no supported triangle geometry");
    if (asset.draws.empty()) warn(asset,"The selected scene has no drawable geometry.");
}

void skins(const cgltf_data& data,ModelAsset& asset) {
    check(data.skins_count<=MaxNodes,"too many skins");asset.skins.reserve(data.skins_count);
    for(std::size_t skinIndex=0;skinIndex<data.skins_count;++skinIndex) {
        const auto& source=data.skins[skinIndex];ModelSkin skin;skin.name=source.name?source.name:"Skin "+std::to_string(skinIndex+1);
        check(source.joints_count>0&&source.joints_count<=65536,"skin must contain between 1 and 65536 joints");
        for(std::size_t joint=0;joint<source.joints_count;++joint) {
            check(source.joints[joint]!=nullptr,"skin has a missing joint");skin.joints.push_back(std::uint32_t(source.joints[joint]-data.nodes));
        }
        if(source.skeleton)skin.skeleton=std::int32_t(source.skeleton-data.nodes);
        skin.inverseBindMatrices.resize(source.joints_count);
        if(source.inverse_bind_matrices) {
            const auto& accessor=*source.inverse_bind_matrices;
            check(accessor.type==cgltf_type_mat4&&accessor.component_type==cgltf_component_type_r_32f&&accessor.count>=source.joints_count,
                "inverse bind matrices must be FLOAT MAT4 and cover every joint");
            const auto values=floats(accessor);for(std::size_t joint=0;joint<source.joints_count;++joint)
                std::copy_n(values.data()+joint*16,16,skin.inverseBindMatrices[joint].begin());
        }
        asset.skins.push_back(std::move(skin));
    }
    for(const auto& node:asset.nodes)if(node.skin>=0) {
        check(std::size_t(node.skin)<asset.skins.size()&&node.mesh>=0,"invalid mesh skin reference");const auto& skin=asset.skins[std::size_t(node.skin)];
        for(auto primitive:asset.meshes[std::size_t(node.mesh)].primitives) {
            const auto& geometry=asset.primitives[primitive];check(!geometry.skinWeights.empty(),"skinned mesh requires JOINTS_0 and WEIGHTS_0 attributes");
            for(const auto& vertex:geometry.skinWeights)for(std::size_t influence=0;influence<MaximumModelJointInfluences;++influence)
                check(vertex.joints[influence]<skin.joints.size(),"vertex joint index exceeds its skin joint count");
        }
    }
}
void animations(const cgltf_data& data,ModelAsset& asset) {
    check(data.animations_count<=MaxNodes,"too many animation clips");asset.animations.reserve(data.animations_count);
    for(std::size_t clipIndex=0;clipIndex<data.animations_count;++clipIndex) {
        const auto& source=data.animations[clipIndex];ModelAnimationClip clip;clip.name=source.name?source.name:"Animation "+std::to_string(clipIndex+1);
        check(source.channels_count>0&&source.channels_count<=MaxNodes,"animation must contain channels");
        clip.start=std::numeric_limits<float>::max();clip.end=0;std::set<std::pair<std::uint32_t,ModelAnimationPath>> targets;
        for(std::size_t channelIndex=0;channelIndex<source.channels_count;++channelIndex) {
            const auto& sourceChannel=source.channels[channelIndex];ModelAnimationChannel channel;
            check(sourceChannel.target_node&&sourceChannel.sampler,"animation channel requires a target node and sampler");
            channel.node=std::uint32_t(sourceChannel.target_node-data.nodes);const auto& node=asset.nodes[channel.node];
            switch(sourceChannel.target_path) {
            case cgltf_animation_path_type_translation:channel.path=ModelAnimationPath::Translation;channel.components=3;break;
            case cgltf_animation_path_type_rotation:channel.path=ModelAnimationPath::Rotation;channel.components=4;break;
            case cgltf_animation_path_type_scale:channel.path=ModelAnimationPath::Scale;channel.components=3;break;
            case cgltf_animation_path_type_weights:
                channel.path=ModelAnimationPath::Weights;channel.components=std::uint32_t(node.weights.size());check(channel.components>0,"animated morph weights require mesh targets");break;
            default:fail("unsupported animation channel path");
            }
            check(targets.emplace(channel.node,channel.path).second,"animation contains duplicate channels for one node property");
            check(channel.path==ModelAnimationPath::Weights||!node.matrixAuthored,"TRS animation cannot target a matrix-authored node");
            const auto& sampler=*sourceChannel.sampler;check(sampler.input&&sampler.output,"animation sampler requires input and output accessors");
            const auto& input=*sampler.input;const auto& output=*sampler.output;
            check(input.type==cgltf_type_scalar&&input.component_type==cgltf_component_type_r_32f&&!input.normalized,"animation times must be FLOAT SCALAR");
            channel.times=floats(input);
            for(std::size_t key=0;key<channel.times.size();++key)check(channel.times[key]>=0&&(!key||channel.times[key]>channel.times[key-1]),"animation times must be nonnegative and strictly increasing");
            switch(sampler.interpolation) {
            case cgltf_interpolation_type_step:channel.interpolation=ModelAnimationInterpolation::Step;break;
            case cgltf_interpolation_type_linear:channel.interpolation=ModelAnimationInterpolation::Linear;break;
            case cgltf_interpolation_type_cubic_spline:channel.interpolation=ModelAnimationInterpolation::CubicSpline;check(channel.times.size()>=2,"CUBICSPLINE animation requires at least two keys");break;
            default:fail("unsupported animation interpolation");
            }
            const bool weights=channel.path==ModelAnimationPath::Weights;
            const bool normalizedOutput=(weights||channel.path==ModelAnimationPath::Rotation)&&output.normalized&&
                (output.component_type==cgltf_component_type_r_8||output.component_type==cgltf_component_type_r_16||
                 output.component_type==cgltf_component_type_r_8u||output.component_type==cgltf_component_type_r_16u);
            check(((output.component_type==cgltf_component_type_r_32f&&!output.normalized)||normalizedOutput)&&
                (weights?output.type==cgltf_type_scalar:output.type==(channel.components==4?cgltf_type_vec4:cgltf_type_vec3)),"animation output accessor shape or component type is invalid");
            channel.values=floats(output);const auto multiplier=channel.interpolation==ModelAnimationInterpolation::CubicSpline?3u:1u;
            check(channel.values.size()==channel.times.size()*channel.components*multiplier,"animation sampler output count differs from its input keys");
            clip.start=std::min(clip.start,channel.times.front());clip.end=std::max(clip.end,channel.times.back());clip.channels.push_back(std::move(channel));
        }
        asset.animations.push_back(std::move(clip));
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
    const auto encoded=read(documentPath); const std::string text(encoded.begin(),encoded.end()); cgltf_options options{}; cgltf_data* raw=nullptr;
    const auto parsed=cgltf_parse(&options,text.data(),text.size(),&raw);
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    check(parsed==cgltf_result_success && data,"invalid glTF document (parser code "+std::to_string(parsed)+")");
    check(data->asset.version && std::string(data->asset.version)=="2.0","unsupported glTF version");
    check(data->extensions_required_count==0,"unsupported required extension");
    check(data->nodes_count<=MaxNodes && data->meshes_count<=MaxNodes && data->materials_count<=MaxNodes,"asset table exceeds supported size");
    std::set<std::string> dependencies;
    for (const auto& dependency:manifest.at("dependencies")) {
        const auto file=dependency.at("path").get<std::string>(); managedPath(directory,file); dependencies.insert(file);
    }
    std::vector<Bytes> buffers(data->buffers_count);
    for (size_t i=0;i<data->buffers_count;++i) {
        auto& buffer=data->buffers[i]; check(buffer.uri && dependencies.contains(buffer.uri),"buffer is not a declared managed dependency");
        const auto source=managedPath(directory,buffer.uri);check(fs::is_regular_file(source)&&buffer.size>0&&fs::file_size(source)==buffer.size,"managed buffer size mismatch");
        if(metadataOnly)continue;
        buffers[i]=read(source); check(buffers[i].size()==buffer.size,"managed buffer size mismatch");
        buffer.data=buffers[i].data(); buffer.data_free_method=cgltf_data_free_method_none;
    }
    if(!metadataOnly)validateAccessors(*data);
    auto asset=std::make_shared<ModelAsset>(); asset->name=manifest.at("name").get<std::string>(); asset->format=manifest.at("format").get<std::string>();
    asset->id=modelIdentity(root,identityDirectory);
    for (size_t i=0;i<data->extensions_used_count;++i) warn(*asset,"Optional extension ignored: "+std::string(data->extensions_used[i]));
    if (data->cameras_count || data->lights_count) warn(*asset,"Imported cameras and lights are ignored; preview lighting and camera remain active.");
    std::vector<std::string> images; images.reserve(data->images_count);
    for (size_t i=0;i<data->images_count;++i) {
        const auto& image=data->images[i]; check(image.uri && dependencies.contains(image.uri),"image is not a declared managed dependency");
        const auto id=derivedAssetId(asset->id,"image:"+std::string(image.uri));
        const auto replacement=resolvers.image?resolvers.image(id):id;
        if(replacement.empty() || isEngineAssetId(replacement)) { images.push_back(id);asset->images.push_back(id);continue; }
        auto source=managedPath(directory,image.uri);
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
    materials(*data,images,*asset);
    const auto explicitMaterials=manifest.find("materialAssets");
    const bool needsFallback=asset->materials.size()>data->materials_count;
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
        geometry(*data,*asset);hierarchy(*data,*asset);skins(*data,*asset);animations(*data,*asset);validateModelAnimationData(*asset);
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
