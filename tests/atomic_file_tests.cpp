#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
namespace fs=std::filesystem;
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);require(bool(input),"Cannot read atomic-file fixture.");return {std::istreambuf_iterator<char>(input),{}};}
struct Temporary {
    fs::path root=fs::temp_directory_path()/("crimson-atomic-file-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary(){fs::create_directories(root);}
    ~Temporary(){std::error_code error;const auto target=fs::weakly_canonical(root,error),parent=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-atomic-file-"))fs::remove_all(target,error);}
};
void noTemporaries(const fs::path& root){for(const auto& entry:fs::directory_iterator(root))require(entry.path().extension()!=".tmp","Atomic publication leaked its temporary file.");}
void ordinaryPublication(const fs::path& root){
    const auto file=root/"ordinary.json",source=root/"source.json";
    writeFileAtomically(file,"before");writeFileAtomically(file,"complete replacement");
    require(read(file)=="complete replacement","Atomic write did not publish complete replacement bytes.");
    writeFileAtomically(source,"restored bytes");copyFileAtomically(source,file);
    require(read(file)=="restored bytes"&&read(source)=="restored bytes","Atomic restore lost source or destination bytes.");noTemporaries(root);
}
#ifdef _WIN32
struct Reader {
    HANDLE handle{INVALID_HANDLE_VALUE};
    explicit Reader(const fs::path& file){handle=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        require(handle!=INVALID_HANDLE_VALUE,"Cannot acquire deterministic non-delete-sharing reader.");}
    Reader(const Reader&)=delete;Reader& operator=(const Reader&)=delete;
    void close(){if(handle!=INVALID_HANDLE_VALUE){CloseHandle(handle);handle=INVALID_HANDLE_VALUE;}}
    ~Reader(){close();}
};
void transientReader(const fs::path& root,bool restore){
    const auto destination=root/(restore?"restore.json":"asset-locations.json"),source=root/"replacement.json",probe=root/"probe.json";
    writeFileAtomically(destination,"original index");writeFileAtomically(source,"new complete index");writeFileAtomically(probe,"probe");
    Reader reader(destination);
    require(!MoveFileExW(probe.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH),"The reader did not block a raw atomic replacement.");
    const auto blocked=GetLastError();require(blocked==ERROR_ACCESS_DENIED||blocked==ERROR_SHARING_VIOLATION||blocked==ERROR_LOCK_VIOLATION,"The deterministic blocker produced an unrelated Windows error.");
    fs::remove(probe);
    std::jthread release([&reader]{std::this_thread::sleep_for(std::chrono::milliseconds{100});reader.close();});
    if(restore)copyFileAtomically(source,destination);else writeFileAtomically(destination,"new complete index");
    release.join();
    require(read(destination)=="new complete index","A transient reader prevented atomic publication after releasing its handle.");noTemporaries(root);
}
void persistentReader(const fs::path& root,bool restore){
    const auto destination=root/(restore?"blocked-restore.json":"blocked-index.json"),source=root/"replacement.json";
    writeFileAtomically(destination,"retained index");writeFileAtomically(source,"new complete index");Reader reader(destination);
    const auto started=std::chrono::steady_clock::now();std::string diagnostic;
    try{if(restore)copyFileAtomically(source,destination);else writeFileAtomically(destination,"new complete index");}
    catch(const std::exception& error){diagnostic=error.what();}
    require(!diagnostic.empty()&&diagnostic.find(destination.filename().string())!=std::string::npos&&diagnostic.find("Windows error")!=std::string::npos,
        "A persistent reader did not produce a path-specific publication failure.");
    require(std::chrono::steady_clock::now()-started<std::chrono::seconds{3},"A persistent file lock caused an unbounded retry.");
    require(read(destination)=="retained index","A failed publication deleted or modified the working index.");reader.close();noTemporaries(root);
}
void protectedTargets(const fs::path& root){
    const auto file=root/"readonly.json";writeFileAtomically(file,"protected bytes");
    struct RestoreAttributes {fs::path file;DWORD attributes;~RestoreAttributes(){SetFileAttributesW(file.c_str(),attributes);}} attributes{file,GetFileAttributesW(file.c_str())};
    require(attributes.attributes!=INVALID_FILE_ATTRIBUTES&&SetFileAttributesW(file.c_str(),attributes.attributes|FILE_ATTRIBUTE_READONLY),"Cannot protect the disposable target.");
    bool rejected{};try{writeFileAtomically(file,"replacement");}catch(const std::exception&){rejected=true;}
    require(rejected&&read(file)=="protected bytes"&&(GetFileAttributesW(file.c_str())&FILE_ATTRIBUTE_READONLY),"Atomic publication bypassed protection or changed the protected target.");
    const auto directory=root/"directory.json";fs::create_directory(directory);rejected=false;
    try{writeFileAtomically(directory,"replacement");}catch(const std::exception&){rejected=true;}
    require(rejected&&fs::is_directory(directory),"Atomic publication replaced a directory target.");noTemporaries(root);
}
void indexRegistration(const fs::path& root){
    const auto project=root/"project";fs::create_directory(project);initializeContentStorage(project);
    const auto index=project/".crimson/asset-locations.json";const auto revision=contentStorageRevision(project);
    const std::array paths{StoredAssetPath{"assets/imported.png",project/"Content/imported.png"}};
    {
        Reader reader(index);std::jthread release([&reader]{std::this_thread::sleep_for(std::chrono::milliseconds{100});reader.close();});
        registerAssetPaths(project,paths);release.join();
    }
    require(contentStorageRevision(project)==revision+1&&resolveAssetPath(project,"assets/imported.png")==paths[0].file,
        "Index registration did not publish its snapshot after the transient reader released.");
    const auto before=read(index);const auto previousRevision=contentStorageRevision(project);
    const std::array blocked{StoredAssetPath{"assets/blocked.png",project/"Content/blocked.png"}};
    Reader reader(index);bool rejected{};try{registerAssetPaths(project,blocked);}catch(const std::exception&){rejected=true;}
    require(rejected&&read(index)==before&&contentStorageRevision(project)==previousRevision,
        "A persistent index reader left disk and cached storage at different revisions.");
    const auto locations=storedAssetPaths(project);
    require(std::none_of(locations.begin(),locations.end(),[&](const auto& entry){return entry.file==blocked[0].file;}),
        "A failed index write published a cached location.");
    reader.close();noTemporaries(project/".crimson");
}
#endif
}
int main(){try{Temporary temporary;ordinaryPublication(temporary.root);
#ifdef _WIN32
    transientReader(temporary.root,false);transientReader(temporary.root,true);persistentReader(temporary.root,false);persistentReader(temporary.root,true);protectedTargets(temporary.root);indexRegistration(temporary.root);
#endif
    std::cout<<"Atomic publication, transient reader retry and retained-target checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
