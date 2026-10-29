#pragma once

#include "editor/EditorPreferences.h"
#include <functional>
#include <memory>

class QDialog;
class QMainWindow;

namespace crimson::integration {
struct LevelViewRequest;
class LevelPreferences final {
public:
    LevelPreferences(QMainWindow&,std::function<void()> requestFrame,std::filesystem::path file={});
    ~LevelPreferences();
    const EditorPreferenceValues& values()const;
    void open();
    bool ownsInput()const;
    void apply(LevelViewRequest&)const;
    QDialog* dialog()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
