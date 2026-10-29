#include "AtomicFile.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <algorithm>
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

namespace crimson {
namespace {
#ifdef _WIN32
void publish(const std::filesystem::path& temporary,const std::filesystem::path& destination,const char* operation) {
    // A short-lived reader or scanner may deny replacement after our own
    // output handle has closed. Retain the complete temporary and old file
    // while retrying the same atomic rename; never delete/truncate the target.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds{500};
    auto delay=std::chrono::milliseconds{1};
    for(;;) {
        if(MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return;
        const auto error=GetLastError();
        const bool retryable=error==ERROR_ACCESS_DENIED||error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION;
        const auto attributes=GetFileAttributesW(destination.c_str());
        const bool protectedTarget=attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_DIRECTORY));
        const auto now=std::chrono::steady_clock::now();
        if(!retryable||protectedTarget||now>=deadline) {
            const auto encoded=destination.generic_u8string();
            throw std::runtime_error(std::string("Could not ")+operation+" the file '"+std::string(encoded.begin(),encoded.end())+
                "' (Windows error "+std::to_string(error)+").");
        }
        std::this_thread::sleep_until(std::min(deadline,now+delay));
        delay=std::min(delay*2,std::chrono::milliseconds{25});
    }
}
#endif
}

void copyFileAtomically(const std::filesystem::path& source,const std::filesystem::path& destination) {
    if(!destination.parent_path().empty())std::filesystem::create_directories(destination.parent_path());
    static std::atomic<uint64_t> sequence{};auto temporary=destination;
    do{temporary=destination;temporary+=".restore-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1))+".tmp";}while(std::filesystem::exists(temporary));
    try{
        std::filesystem::copy_file(source,temporary);
#ifdef _WIN32
        publish(temporary,destination,"restore");
#else
        std::filesystem::rename(temporary,destination);
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}

void writeFileAtomically(const std::filesystem::path& path, std::string_view contents) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    static std::atomic<uint64_t> sequence{0};
    std::filesystem::path temporary;
    do {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary = path;
        temporary += "." + std::to_string(ticks) + "." + std::to_string(sequence.fetch_add(1)) + ".tmp";
    } while (std::filesystem::exists(temporary));
    try {
        {
            std::ofstream output(temporary,std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot create the temporary file.");
            output.write(contents.data(),static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) throw std::runtime_error("Could not write the complete file.");
            output.close();
            if (!output) throw std::runtime_error("Could not close the temporary file.");
        }
#ifdef _WIN32
        publish(temporary,path,"publish");
#else
        std::filesystem::rename(temporary,path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary,ignored);
        throw;
    }
}

} // namespace crimson
