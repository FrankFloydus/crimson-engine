#pragma once
#include <cstdint>
#include <optional>

namespace crimson {
// Inspector controls affect the whole Edit preview. Runtime playback and all
// authored light settings remain independent from these transient controls.
struct LightPreviewContext {
    bool* enabled{};
    bool* paused{};
    bool* restart{};
};

class SceneLights {
public:
    void synchronize(std::uint64_t documentGeneration);
    void advance(std::uint64_t documentGeneration,double seconds);
    void clear();
    LightPreviewContext previewContext() {return {&enabled_,&paused_,&restart_};}
    std::optional<double> sampleTime() const {return enabled_?std::optional{seconds_}:std::nullopt;}
private:
    std::uint64_t generation_{};
    double seconds_{};
    bool enabled_{true},paused_{},restart_{};
};
}
