#pragma once

#include <bgfx/bgfx.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>

namespace crimson {

class Diagnostics final : public bgfx::CallbackI {
public:
    Diagnostics();
    void log(const std::string& message);
    bool captureSucceeded() const { return captureDone_.load(); }
    bool captureFailed() const { return captureFailed_.load(); }
    bool hadRenderError() const { return renderError_.load(); }

    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char*) override;
    void traceVargs(const char*, uint16_t, const char*, va_list) override;
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char*, uint32_t, uint32_t, uint32_t, const void*, uint32_t, bool) override;
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}

private:
    std::mutex mutex_;
    std::ofstream logFile_;
    std::atomic<bool> captureDone_{false};
    std::atomic<bool> captureFailed_{false};
    std::atomic<bool> renderError_{false};
};

} // namespace crimson
