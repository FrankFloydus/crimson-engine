#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace crimson::profiling {

enum class CaptureKind { Startup, Gameplay, Editor };
enum class CaptureState { Idle, Connecting, Capturing, Saving, Saved, Failed };

struct CaptureOptions {
    std::filesystem::path toolsDirectory; // Paired bin/profiling tools.
    std::filesystem::path outputDirectory; // Caller supplies the project capture root.
    std::uint16_t port{8086};
    std::chrono::milliseconds connectionTimeout{5000};
    std::chrono::milliseconds stopTimeout{15000};
};

// JSON fields contain caller-authored snapshots, never file-system discovery
// in the frame loop. Empty fields remain explicitly unavailable in the manifest.
struct CaptureContext {
    std::string engineCommit,rendererCommit,backend,gpuName;
    bool engineDirty{},rendererDirty{};
    std::string projectRoot;
    std::string settingsJson,cacheJson,sourcesJson;
};

struct CaptureFrame {
    std::uint64_t appFrame{},bgfxFrame{};
    std::optional<std::uint64_t> sourceFrame;
    std::uint32_t sceneWidth{},sceneHeight{},gameWidth{},gameHeight{};
    bool materialsReady{},playing{};
};

struct CaptureStatus {
    CaptureKind kind{CaptureKind::Gameplay};
    CaptureState state{CaptureState::Idle};
    std::uint32_t durationSeconds{};
    std::filesystem::path trace,manifest,log;
    std::string detail;
    // Connected recording time only; excludes connection and save delays.
    double elapsedSeconds{};
    bool active() const noexcept {
        return state==CaptureState::Connecting || state==CaptureState::Capturing || state==CaptureState::Saving;
    }
};

class CaptureController {
public:
    CaptureController(CaptureOptions,CaptureContext={});
    ~CaptureController();
    CaptureController(const CaptureController&)=delete;
    CaptureController& operator=(const CaptureController&)=delete;

    // Nonblocking, including explicit probes. Gameplay never starts Play or
    // supplies input. A zero duration requests manual recording until Stop or
    // shutdown. Gameplay also stops when Play ends; Editor spans Play changes.
    // Positive durations are capped at 3600 s.
    // Startup observes eight normal submissions after material readiness before
    // stopping and requires a positive duration cap.
    bool captureNow(CaptureKind,std::uint32_t durationSeconds);
    // Only an explicitly requested startup capture should call this bounded
    // handshake before renderer initialization; ordinary startup never waits.
    bool waitForConnection(std::chrono::milliseconds timeout=std::chrono::milliseconds{5000});
    void updateContext(CaptureContext);
    void setOutputDirectory(const std::filesystem::path&); // Future requests only.
    void frame(const CaptureFrame&) noexcept;
    void requestStop() noexcept;
    bool active() const noexcept;
    CaptureStatus status() const;
    // Ends an active capture and joins its bounded collector drain. Call after
    // App teardown while the process Tracy client still exists; destructor too.
    void finish() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* captureStateName(CaptureState) noexcept;
const char* captureKindName(CaptureKind) noexcept;

} // namespace crimson::profiling
