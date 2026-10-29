#pragma once
#include <memory>
class QMainWindow;
namespace crimson::profiling {class CaptureController;}
namespace crimson::integration {
class LevelProfiling final {
public:
    LevelProfiling(QMainWindow&,profiling::CaptureController&);
    ~LevelProfiling();
    void update(bool playing);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
