#include "QuaderSceneBundle.h"
#include "AssetTransaction.h"
#include "resources/AssetId.h"
#include "resources/Paths.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace crimson {
namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
constexpr std::string_view Magic{"CRQDSCN\0",8};
constexpr std::size_t MaximumMetadata=64*1024*1024;
constexpr std::size_t MaximumBytes=std::size_t{2}*1024*1024*1024;
constexpr std::size_t MaximumModels=100000;
constexpr std::array<const char*,4> Members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string utf8(const fs::path& path){const auto value=path.generic_u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
bool below(const fs::path& path,const fs::path& base){const auto relative=path.lexically_relative(base);return !relative.empty()&&!relative.has_root_path()&&*relative.begin()!="..";}
std::uint64_t fingerprint(std::string_view bytes){std::uint64_t hash=14695981039346656037ULL;for(const unsigned char byte:bytes){hash^=byte;hash*=1099511628211ULL;}return hash;}
void integer(std::string& out,std::uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)out.push_back(static_cast<char>((value>>(8*i))&255));}
std::uint64_t integer(std::string_view bytes,std::size_t offset,unsigned count){require(offset<=bytes.size()&&count<=bytes.size()-offset,"Truncated Quader scene bundle header.");std::uint64_t result{};for(unsigned i=0;i<count;++i)result|=std::uint64_t(static_cast<unsigned char>(bytes[offset+i]))<<(8*i);return result;}
std::string read(const fs::path& file){const auto size=fs::file_size(file);require(size<=MaximumBytes,"Quader scene bundle exceeds its supported size.");std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read Quader scene bundle: "+utf8(file));return {std::istreambuf_iterator<char>(input),{}};}
void validIdentity(const std::string& id){require(isAssetUuid(id)&&canonicalAssetId(id)==id&&!isEngineAssetId(id),"Invalid Quader bundle asset identity.");}
void validate(const QuaderSceneBundle& bundle){
    validIdentity(bundle.id);require(!bundle.projectId.empty()&&bundle.projectId.size()<=1024&&!bundle.sceneId.empty()&&bundle.sceneId.size()<=1024,"Invalid Quader scene source identity.");
    require(bundle.id==quaderSceneBundleId(bundle.projectId,bundle.sceneId),"Quader scene bundle identity does not match its source.");
    require(bundle.name.size()<=4096&&bundle.models.size()<=MaximumModels,"Quader scene bundle metadata exceeds its supported bounds.");
    std::set<std::string> identities;
    for(const auto& model:bundle.models){
        validIdentity(model.id);require(model.id==model.memberIds[0],"Quader model and manifest identities disagree.");
        for(const auto& id:model.memberIds){validIdentity(id);require(identities.insert(id).second,"Duplicate Quader scene bundle member identity.");}
        require(model.manifest.size()<=MaximumMetadata&&model.gltf.size()<=MaximumMetadata&&model.buffer.size()<=MaximumBytes,"Quader model exceeds its supported bounds.");
        if(!model.legacyManifest.empty()){
            const auto legacy=pathFromUtf8(model.legacyManifest);require(!legacy.has_root_path()&&model.legacyManifest.starts_with("assets/")&&legacy.filename()=="manifest.json"&&model.legacyManifest.find('\\')==std::string::npos&&model.legacyManifest.find(':')==std::string::npos,"Invalid Quader model legacy identity.");
            for(const auto& part:legacy)require(part!=".."&&part!=".","Invalid Quader model legacy traversal.");
        }
        const auto manifest=Json::parse(model.manifest),gltf=Json::parse(model.gltf);
        require(manifest.is_object()&&manifest.at("version")==1&&manifest.value("format",std::string{})=="Quader"&&manifest.value("document",std::string{})=="model.gltf"&&manifest.value("original",std::string{})=="source.gltf","Unsupported Quader bundled model manifest.");
        const auto& source=manifest.at("quader");require(source.at("project")==bundle.projectId&&source.at("scene")==bundle.sceneId&&!source.value("collision",false),"Quader model belongs to a different source scene.");
        require(gltf.is_object()&&gltf.contains("buffers")&&gltf.at("buffers").is_array()&&gltf.at("buffers").size()==1,"Unsupported Quader bundle geometry buffers.");
        const auto& buffer=gltf.at("buffers").at(0);require(buffer.at("uri")=="buffers/0.bin"&&buffer.at("byteLength").is_number_unsigned()&&buffer.at("byteLength").get<std::uint64_t>()==model.buffer.size(),"Quader bundle geometry buffer length or location is invalid.");
        require(!gltf.contains("images")||gltf.at("images").empty(),"Quader bundle has unsupported retained image dependencies.");
        if(const auto dependencies=manifest.find("dependencies");dependencies!=manifest.end()) {
            require(dependencies->is_array()&&dependencies->size()==1,"Unsupported Quader bundle dependency list.");
            const auto& dependency=dependencies->at(0);require(dependency.at("kind")=="buffer"&&dependency.at("index")==0&&dependency.at("path")=="buffers/0.bin","Unsupported Quader bundle dependency location.");
        }
        if(const auto mappings=manifest.find("materialAssets");mappings!=manifest.end()){
            require(mappings->is_array(),"Invalid Quader bundle material mappings.");for(const auto& material:*mappings)require(material.is_string()&&isAssetUuid(material.get<std::string>()),"Invalid Quader bundle material reference.");
        }
    }
}
fs::path checkedCache(const fs::path& root,const fs::path& relative){
    const auto base=fs::weakly_canonical(fs::absolute(root)),result=fs::weakly_canonical(base/relative);
    require(below(result,base)&&below(result,base/".crimson")&&below(result,base/".crimson/cache/quader-imports"),"Quader derived cache escapes its project owner.");return result;
}
}

std::string quaderSceneBundleId(std::string_view project,std::string_view scene){
    require(!project.empty()&&!scene.empty(),"Quader scene source identities cannot be empty.");
    return canonicalAssetId("quader-scene-bundle:"+std::to_string(project.size())+":"+std::string(project)+":"+std::string(scene));
}
std::string encodeQuaderSceneBundle(const QuaderSceneBundle& bundle){
    validate(bundle);Json models=Json::array();std::size_t offset{};
    for(const auto& model:bundle.models){require(model.buffer.size()<=MaximumBytes-offset,"Quader scene bundle exceeds its supported size.");
        models.push_back({{"id",model.id},{"legacyManifest",model.legacyManifest},{"memberIds",model.memberIds},{"manifest",model.manifest},{"gltf",model.gltf},{"current",model.current},{"bufferOffset",offset},{"bufferLength",model.buffer.size()},{"bufferFingerprint",fingerprint(model.buffer)}});offset+=model.buffer.size();}
    const auto metadata=Json{{"version",1},{"id",bundle.id},{"project",bundle.projectId},{"scene",bundle.sceneId},{"name",bundle.name},{"models",std::move(models)}}.dump();
    require(metadata.size()<=MaximumMetadata&&metadata.size()+20<=MaximumBytes-offset,"Quader scene bundle metadata exceeds its supported bounds.");
    std::string result;result.reserve(20+metadata.size()+offset);result.append(Magic);integer(result,1,4);integer(result,metadata.size(),8);result+=metadata;for(const auto& model:bundle.models)result+=model.buffer;return result;
}
QuaderSceneBundle decodeQuaderSceneBundle(std::string_view bytes){
    require(bytes.size()>=20&&bytes.size()<=MaximumBytes&&bytes.substr(0,8)==Magic&&integer(bytes,8,4)==1,"Unsupported Quader scene bundle header.");
    const auto length=integer(bytes,12,8);require(length<=MaximumMetadata&&length<=bytes.size()-20,"Invalid Quader scene bundle metadata length.");
    const auto metadata=Json::parse(bytes.substr(20,static_cast<std::size_t>(length)));require(metadata.at("version")==1&&metadata.at("models").is_array()&&metadata.at("models").size()<=MaximumModels,"Unsupported Quader scene bundle metadata.");
    QuaderSceneBundle bundle{metadata.at("id").get<std::string>(),metadata.at("project").get<std::string>(),metadata.at("scene").get<std::string>(),metadata.at("name").get<std::string>(),{}};
    const auto payload=bytes.substr(20+static_cast<std::size_t>(length));std::size_t cursor{};bundle.models.reserve(metadata.at("models").size());
    for(const auto& item:metadata.at("models")){
        require(item.at("bufferOffset").is_number_unsigned()&&item.at("bufferLength").is_number_unsigned()&&item.at("bufferFingerprint").is_number_unsigned(),"Invalid Quader scene binary metadata types.");
        require(item.at("memberIds").is_array()&&item.at("memberIds").size()==4,"Invalid Quader scene member identity count.");
        const auto offset=item.at("bufferOffset").get<std::uint64_t>(),size=item.at("bufferLength").get<std::uint64_t>();require(offset==cursor&&size<=payload.size()-cursor,"Invalid Quader scene binary member range.");
        QuaderBundleModel model;model.id=item.at("id").get<std::string>();model.legacyManifest=item.at("legacyManifest").get<std::string>();model.memberIds=item.at("memberIds").get<std::array<std::string,4>>();model.manifest=item.at("manifest").get<std::string>();model.gltf=item.at("gltf").get<std::string>();model.current=item.value("current",true);model.buffer=std::string(payload.substr(cursor,static_cast<std::size_t>(size)));require(fingerprint(model.buffer)==item.at("bufferFingerprint").get<std::uint64_t>(),"Quader scene binary member checksum mismatch.");cursor+=static_cast<std::size_t>(size);bundle.models.push_back(std::move(model));
    }
    require(cursor==payload.size(),"Quader scene bundle contains unowned binary data.");validate(bundle);return bundle;
}
fs::path quaderBundleCacheDirectory(const fs::path& root,std::string_view bundleId,std::string_view modelId){validIdentity(std::string(bundleId));validIdentity(std::string(modelId));return checkedCache(root,fs::path(".crimson/cache/quader-imports")/pathFromUtf8(bundleId)/pathFromUtf8(modelId));}
std::vector<StoredAssetPath> quaderBundleCacheLocations(const fs::path& root,const QuaderSceneBundle& bundle){
    std::vector<StoredAssetPath> result;result.reserve(bundle.models.size()*4);for(const auto& model:bundle.models){const auto directory=quaderBundleCacheDirectory(root,bundle.id,model.id);for(std::size_t i=0;i<Members.size();++i)result.push_back({model.memberIds[i],directory/Members[i]});}return result;
}
std::optional<fs::path> quaderBundleSource(const fs::path& root,std::string_view modelId){
    try{const auto base=fs::weakly_canonical(fs::absolute(root)),file=resolveAssetPath(root,modelId),relative=file.lexically_relative(base/".crimson/cache/quader-imports");
        if(relative.empty()||relative.has_root_path()||*relative.begin()=="..")return {};auto part=relative.begin();const auto bundleId=utf8(*part++);if(part==relative.end()||!isAssetUuid(bundleId))return {};const auto memberId=utf8(*part++);if(part==relative.end()||!isAssetUuid(memberId)||*part++!="manifest.json"||part!=relative.end())return {};
        const auto source=resolveAssetPath(root,bundleId);if(source.extension()!=".crimson-quader"||!below(source,base/"Content"))return {};return source;
    }catch(const std::exception&){return {};}
}
std::optional<std::pair<fs::path,QuaderSceneBundle>> findQuaderSceneBundle(const fs::path& root,std::string_view project,std::string_view scene){
    const auto id=quaderSceneBundleId(project,scene);std::optional<fs::path> indexed;
    try{indexed=resolveAssetPath(root,id);}catch(const std::exception&){/* A missing private index is rebuilt from tracked bundles. */}
    if(indexed&&indexed->extension()==".crimson-quader"&&fs::is_regular_file(*indexed)){auto bundle=decodeQuaderSceneBundle(read(*indexed));require(bundle.id==id,"Indexed Quader scene bundle identity mismatch.");return std::pair{*indexed,std::move(bundle)};}
    const auto content=fs::weakly_canonical(fs::absolute(root))/"Content";if(!fs::is_directory(content))return {};
    for(const auto& entry:fs::recursive_directory_iterator(content))if(entry.is_regular_file()&&entry.path().extension()==".crimson-quader"){
        const auto file=checkedContentPath(root,entry.path());auto bundle=decodeQuaderSceneBundle(read(file));if(bundle.id==id)return std::pair{file,std::move(bundle)};
    }return {};
}
fs::path quaderSceneContentDirectory(const fs::path& root,const QuaderSceneBundle& bundle){
    std::string name;const auto& source=bundle.name;
    for(std::size_t i=0;i<source.size();++i){const auto c=static_cast<unsigned char>(source[i]);
        const bool upper=c>='A'&&c<='Z',lower=c>='a'&&c<='z',digit=c>='0'&&c<='9';
        if(upper&&i&&((source[i-1]>='a'&&source[i-1]<='z')||(source[i-1]>='0'&&source[i-1]<='9'))&&!name.empty()&&name.back()!='_')name+='_';
        if(upper||lower||digit||c>=128)name+=static_cast<char>(upper?c+('a'-'A'):c);
        else if(!name.empty()&&name.back()!='_')name+='_';
    }
    while(!name.empty()&&name.back()=='_')name.pop_back();
    if(name.size()>80){std::size_t end=80;while(end>0&&(static_cast<unsigned char>(name[end])&0xc0)==0x80)--end;name.resize(end);}
    while(!name.empty()&&name.back()=='_')name.pop_back();
    if(name.empty())name="quader_scene";
    try{name=contentFilename(name);}catch(...){name="_"+name;}
    return checkedContentPath(root,fs::path("Content")/pathFromUtf8(name));
}
fs::path defaultQuaderSceneBundlePath(const fs::path& root,const QuaderSceneBundle& bundle){
    const auto directory=quaderSceneContentDirectory(root,bundle);
    return checkedContentPath(root,directory/"models"/pathFromUtf8(utf8(directory.filename())+"_"+bundle.id.substr(0,8)+".crimson-quader"));
}
PreparedQuaderBundleModelRemoval prepareQuaderBundleModelRemoval(const fs::path& root,std::span<const std::string> modelIds){
    const auto base=fs::weakly_canonical(fs::absolute(root));PreparedQuaderBundleModelRemoval prepared;
    prepared.storageRevision=contentStorageRevision(root);
    std::map<fs::path,std::set<std::string>> selected;
    for(const auto& id:modelIds)if(const auto source=quaderBundleSource(root,id))selected[checkedContentPath(root,*source)].insert(id);
    for(const auto& [source,ids]:selected){
        const auto prior=read(source);auto bundle=decodeQuaderSceneBundle(prior);
        require(resolveAssetPath(root,bundle.id)==source,"Quader bundle location changed before deletion.");
        std::size_t count{};
        std::erase_if(bundle.models,[&](const auto& model){
            if(!ids.contains(model.id))return false;
            const auto directory=quaderBundleCacheDirectory(root,bundle.id,model.id);
            for(std::size_t index=0;index<Members.size();++index){
                const auto path=directory/Members[index];
                require(resolveAssetPath(root,model.memberIds[index])==path,"Quader member location changed before deletion.");
                prepared.files.push_back({utf8(path.lexically_relative(base)),std::nullopt});
                prepared.memberIds.push_back(model.memberIds[index]);prepared.memberLocations.push_back({model.memberIds[index],path});
            }
            prepared.modelIds.push_back(model.id);prepared.emptyDirectories.push_back(directory/"buffers");prepared.emptyDirectories.push_back(directory);++count;return true;
        });
        require(count==ids.size(),"A selected Quader mesh is missing from its scene bundle.");
        if(bundle.models.empty()){
            prepared.files.push_back({utf8(source.lexically_relative(base)),std::nullopt});
            prepared.removedBundleIds.push_back(bundle.id);prepared.emptyDirectories.push_back(source.parent_path());
        }else prepared.files.push_back({utf8(source.lexically_relative(base)),encodeQuaderSceneBundle(bundle)});
        prepared.baselines.emplace_back(source,prior);
    }
    return prepared;
}
void validateQuaderBundleModelRemoval(const fs::path& root,const PreparedQuaderBundleModelRemoval& prepared){
    require(contentStorageRevision(root)==prepared.storageRevision,"Asset locations changed while Quader mesh deletion was prepared.");
    for(const auto& [source,prior]:prepared.baselines)require(read(source)==prior,"Quader bundle changed while deletion was prepared.");
    for(const auto& member:prepared.memberLocations)require(resolveAssetPath(root,member.id)==member.file,"Quader member location changed while deletion was prepared.");
}
std::vector<std::string> pruneUnusedQuaderBundleModels(const fs::path& root,std::span<const std::string> referencedIds){
    const auto base=fs::weakly_canonical(fs::absolute(root));std::set<std::string> referenced;
    for(const auto& id:referencedIds)if(!id.empty())referenced.insert(canonicalAssetId(id));
    std::vector<AssetFileChange> files;std::vector<std::string> removed,removedMembers;
    std::vector<std::pair<fs::path,std::string>> baselines;std::vector<fs::path> directories;
    for(const auto& stored:storedAssetPaths(root)){
        if(stored.file.extension()!=".crimson-quader"||!fs::is_regular_file(stored.file))continue;
        const auto source=checkedContentPath(root,stored.file);const auto prior=read(source);auto bundle=decodeQuaderSceneBundle(prior);
        require(bundle.id==stored.id,"Quader bundle location identity mismatch during cleanup.");
        bool changed=false;
        std::erase_if(bundle.models,[&](const auto& model){
            if(model.current||std::any_of(model.memberIds.begin(),model.memberIds.end(),[&](const auto& id){return referenced.contains(id);}))return false;
            const auto directory=quaderBundleCacheDirectory(root,bundle.id,model.id);
            for(std::size_t index=0;index<Members.size();++index){
                const auto path=directory/Members[index];
                // Delete only the cache file owned by this exact member. A
                // location reassigned outside its bundle is never cleanup data.
                require(resolveAssetPath(root,model.memberIds[index])==path,"Quader member location changed before cleanup.");
                files.push_back({utf8(path.lexically_relative(base)),std::nullopt});removedMembers.push_back(model.memberIds[index]);
            }
            removed.push_back(model.id);directories.push_back(directory/"buffers");directories.push_back(directory);changed=true;return true;
        });
        if(changed){
            if(bundle.models.empty()){files.push_back({utf8(source.lexically_relative(base)),std::nullopt});removedMembers.push_back(bundle.id);directories.push_back(source.parent_path());}
            else files.push_back({utf8(source.lexically_relative(base)),encodeQuaderSceneBundle(bundle)});
            baselines.emplace_back(source,prior);
        }
    }
    if(removed.empty())return removed;
    files.push_back({".crimson/asset-locations.json",prepareAssetPathRegistration(root,{},removedMembers)});
    for(const auto& [source,prior]:baselines)require(read(source)==prior,"Quader bundle changed while cleanup was prepared.");
    commitAssetTransaction(root,files);refreshContentStorage(root);
    // These exact directories were ownership-checked above. Remove empty
    // containers only; an unrelated file never becomes recursive cleanup data.
    std::sort(directories.begin(),directories.end(),[](const auto& a,const auto& b){return std::distance(a.begin(),a.end())>std::distance(b.begin(),b.end());});
    for(const auto& directory:directories){std::error_code ignored;if(fs::is_directory(directory,ignored)&&fs::is_empty(directory,ignored))fs::remove(directory,ignored);}
    return removed;
}
} // namespace crimson
