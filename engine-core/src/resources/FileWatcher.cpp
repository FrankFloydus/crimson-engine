#include "FileWatcher.h"
#include "ResourceManager.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <mutex>
#include <thread>

namespace crimson {
namespace {
struct NativeHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~NativeHandle() { if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Stamp {
    std::uintmax_t size{}; std::filesystem::file_time_type write{}; bool exists{};
    bool operator==(const Stamp&) const=default;
};
Stamp stamp(const std::filesystem::path& path) {
    std::error_code error; Stamp result;
    result.exists=std::filesystem::is_regular_file(path,error);
    if(result.exists) { result.size=std::filesystem::file_size(path,error); result.write=std::filesystem::last_write_time(path,error); }
    return result;
}
bool ignored(const std::filesystem::path& path,const std::filesystem::path& watchedRoot) {
    auto owner=watchedRoot.filename().generic_string();
    std::transform(owner.begin(),owner.end(),owner.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    bool privateTree=owner==".crimson",legacyTree=owner=="assets";
    const bool gameRoot=owner!="content"&&owner!="engine";bool first=true;
    for(const auto& part:path) {
        auto name=part.generic_string();
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(first&&gameRoot){privateTree=privateTree||name==".crimson";legacyTree=legacyTree||name=="assets";}
        if((privateTree||legacyTree) && (name=="cache" || name==".transactions" || name==".resource-transactions" || name.starts_with(".staging-") || name.ends_with(".tmp"))) return true;
        first=false;
    }
    auto filename=path.filename().generic_string();
    std::transform(filename.begin(),filename.end(),filename.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(filename=="source.glb"||filename=="source.gltf") {
        const auto directory=(watchedRoot/path).parent_path();std::error_code error;
        return std::filesystem::is_regular_file(directory/"manifest.json",error)&&std::filesystem::is_regular_file(directory/"model.gltf",error);
    }
    return false;
}
}
struct FileWatcher::Impl {
    struct Root {
        std::filesystem::path path;
        NativeHandle directory,event;
        OVERLAPPED overlapped{};
        alignas(FILE_NOTIFY_INFORMATION) std::array<std::byte,64*1024> bytes{};
        bool pending{};
        bool begin() {
            ResetEvent(event.value); overlapped={}; overlapped.hEvent=event.value;
            pending=ReadDirectoryChangesW(directory.value,bytes.data(),static_cast<DWORD>(bytes.size()),TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_ATTRIBUTES,
                nullptr,&overlapped,nullptr)!=FALSE;
            return pending;
        }
        ~Root() {
            if(pending) { CancelIoEx(directory.value,&overlapped); DWORD transferred{}; GetOverlappedResult(directory.value,&overlapped,&transferred,TRUE); }
        }
    };
    struct Change { std::filesystem::path path; std::chrono::steady_clock::time_point time; };
    struct OwnWrite { Stamp value; std::chrono::steady_clock::time_point time; };
    std::vector<std::unique_ptr<Root>> roots;
    NativeHandle stopped;
    std::chrono::milliseconds debounce;
    mutable std::mutex mutex;
    std::map<std::string,Change> changes;
    std::map<std::string,OwnWrite> writes;
    std::string error;
    std::jthread thread;
    explicit Impl(std::vector<std::filesystem::path> paths,std::chrono::milliseconds delay):debounce(delay) {
        stopped.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!stopped.value) { error="Could not create resource watcher event."; return; }
        for(auto& path:paths) {
            if(!std::filesystem::is_directory(path)) continue;
            auto root=std::make_unique<Root>(); root->path=std::filesystem::absolute(path).lexically_normal();
            root->directory.value=CreateFileW(root->path.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr);
            root->event.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            if(root->directory.value==INVALID_HANDLE_VALUE || !root->event.value || !root->begin()) { error="Could not watch resource directory."; continue; }
            roots.push_back(std::move(root));
        }
        thread=std::jthread([this](std::stop_token token){run(token);});
    }
    void run(std::stop_token token) {
        std::vector<HANDLE> events{stopped.value};
        for(const auto& root:roots) events.push_back(root->event.value);
        while(!token.stop_requested()) {
            const auto signaled=WaitForMultipleObjects(static_cast<DWORD>(events.size()),events.data(),FALSE,100);
            if(signaled==WAIT_OBJECT_0) return;
            if(signaled==WAIT_TIMEOUT) continue;
            if(signaled<WAIT_OBJECT_0+1 || signaled>=WAIT_OBJECT_0+events.size()) { std::lock_guard lock(mutex); error="Resource watcher wait failed."; return; }
            auto& root=*roots[signaled-WAIT_OBJECT_0-1]; DWORD transferred{};
            const bool complete=GetOverlappedResult(root.directory.value,&root.overlapped,&transferred,FALSE)!=FALSE;
            root.pending=false;
            if(complete && transferred) {
                std::size_t offset=0;
                while(offset+offsetof(FILE_NOTIFY_INFORMATION,FileName)<=transferred) {
                    const auto* info=reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(root.bytes.data()+offset);
                    if(info->FileNameLength%sizeof(wchar_t) || offset+offsetof(FILE_NOTIFY_INFORMATION,FileName)+info->FileNameLength>transferred) break;
                    const auto relative=std::filesystem::path(std::wstring(info->FileName,info->FileNameLength/sizeof(wchar_t)));
                    if(!ignored(relative,root.path)) {
                        const auto file=(root.path/relative).lexically_normal();
                        std::lock_guard lock(mutex); changes[resourcePathKey(file)]={file,std::chrono::steady_clock::now()};
                    }
                    if(!info->NextEntryOffset || info->NextEntryOffset<offsetof(FILE_NOTIFY_INFORMATION,FileName) || offset+info->NextEntryOffset>=transferred) break;
                    offset+=info->NextEntryOffset;
                }
            }
            if(!root.begin()) { std::lock_guard lock(mutex); error="Resource watcher could not continue."; return; }
        }
    }
};
FileWatcher::FileWatcher(std::vector<std::filesystem::path> roots,std::chrono::milliseconds delay):impl_(std::make_unique<Impl>(std::move(roots),delay)) {}
FileWatcher::~FileWatcher() { stop(); }
std::vector<std::filesystem::path> FileWatcher::poll() {
    std::vector<std::filesystem::path> result;
    std::lock_guard lock(impl_->mutex); const auto now=std::chrono::steady_clock::now();
    for(auto iterator=impl_->changes.begin();iterator!=impl_->changes.end();) {
        if(now-iterator->second.time<impl_->debounce) { ++iterator; continue; }
        const auto own=impl_->writes.find(iterator->first);
        if(own==impl_->writes.end() || own->second.value!=stamp(iterator->second.path)) result.push_back(iterator->second.path);
        iterator=impl_->changes.erase(iterator);
    }
    std::erase_if(impl_->writes,[&](const auto& entry){return now-entry.second.time>std::chrono::seconds{5};});
    return result;
}
void FileWatcher::acknowledge(const std::filesystem::path& path) { std::lock_guard lock(impl_->mutex); impl_->writes[resourcePathKey(path)]={stamp(path),std::chrono::steady_clock::now()}; }
std::string FileWatcher::diagnostic() const { std::lock_guard lock(impl_->mutex); return impl_->error; }
void FileWatcher::stop() { if(!impl_ || !impl_->thread.joinable()) return; impl_->thread.request_stop(); SetEvent(impl_->stopped.value); impl_->thread.join(); }
}
