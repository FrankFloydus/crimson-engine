#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace crimson::profiling::detail {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    Handle()=default;
    explicit Handle(HANDLE handle):value(handle){}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
    Handle& operator=(Handle&& other) noexcept {if(this!=&other){reset();value=std::exchange(other.value,INVALID_HANDLE_VALUE);}return *this;}
    ~Handle(){reset();}
    bool valid() const noexcept{return value&&value!=INVALID_HANDLE_VALUE;}
    void reset() noexcept{if(valid())CloseHandle(value);value=INVALID_HANDLE_VALUE;}
};
inline std::runtime_error windowsError(const char* operation) {
    return std::runtime_error(std::string(operation)+" (Windows error "+std::to_string(GetLastError())+").");
}
inline std::wstring quoted(const std::wstring& value) {
    std::wstring result=L"\"";std::size_t slashes{};
    for(const auto c:value) {
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;
    }
    result.append(slashes*2,L'\\');result+=L'"';return result;
}
inline Handle launch(const std::filesystem::path& executable,const std::vector<std::wstring>& arguments,
    std::span<const HANDLE> inherited={},DWORD flags=0,HANDLE standardInput=nullptr,HANDLE standardOutput=nullptr,HANDLE job=nullptr) {
    std::wstring command=quoted(executable.wstring());
    for(const auto& argument:arguments){command+=L' ';command+=quoted(argument);}
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=inherited.empty()?sizeof(STARTUPINFOW):sizeof(startup);
    startup.StartupInfo.dwFlags=STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;
    if(standardInput&&standardOutput) {
        startup.StartupInfo.dwFlags|=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput=standardInput;startup.StartupInfo.hStdOutput=standardOutput;startup.StartupInfo.hStdError=standardOutput;
    }
    std::vector<std::byte> storage;
    struct Attributes {LPPROC_THREAD_ATTRIBUTE_LIST value{};~Attributes(){if(value)DeleteProcThreadAttributeList(value);}} attributes;
    if(!inherited.empty()) {
        SIZE_T bytes{};InitializeProcThreadAttributeList(nullptr,1,0,&bytes);storage.resize(bytes);
        const auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(list,1,0,&bytes))throw windowsError("Cannot initialize capture process inheritance");
        attributes.value=list;
        if(!UpdateProcThreadAttribute(attributes.value,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(inherited.data()),inherited.size_bytes(),nullptr,nullptr))throw windowsError("Cannot restrict capture process inheritance");
        startup.lpAttributeList=attributes.value;flags|=EXTENDED_STARTUPINFO_PRESENT;
    }
    PROCESS_INFORMATION process{};
    if(job)flags|=CREATE_SUSPENDED;
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,!inherited.empty(),flags,nullptr,nullptr,
        &startup.StartupInfo,&process))throw windowsError("Cannot launch capture process");
    Handle thread(process.hThread),child(process.hProcess);
    if(job) {
        if(!AssignProcessToJobObject(job,child.value)) {const auto error=windowsError("Cannot contain capture process");TerminateProcess(child.value,ERROR_CANCELLED);throw error;}
        if(ResumeThread(thread.value)==static_cast<DWORD>(-1)) {const auto error=windowsError("Cannot start contained capture process");TerminateProcess(child.value,ERROR_CANCELLED);throw error;}
    }
    return child;
}
inline std::wstring handleArgument(HANDLE value){return std::to_wstring(reinterpret_cast<std::uintptr_t>(value));}
inline HANDLE parseHandle(const wchar_t* value){return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::stoull(value)));}
inline std::string utf8(const std::wstring& value) {
    if(value.empty())return {};
    const auto count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string output(static_cast<std::size_t>(count),'\0');
    WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),output.data(),count,nullptr,nullptr);return output;
}
} // namespace crimson::profiling::detail
