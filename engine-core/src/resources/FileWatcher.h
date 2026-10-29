#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace crimson {
// Native notifications only produce paths. Resource/document publication stays on the main thread.
class FileWatcher {
public:
    explicit FileWatcher(std::vector<std::filesystem::path> roots,std::chrono::milliseconds debounce=std::chrono::milliseconds{200});
    ~FileWatcher();
    FileWatcher(const FileWatcher&)=delete;
    FileWatcher& operator=(const FileWatcher&)=delete;
    std::vector<std::filesystem::path> poll();
    void acknowledge(const std::filesystem::path& path);
    std::string diagnostic() const;
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
