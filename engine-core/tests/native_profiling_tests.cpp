#include "integration/profiling/LevelProfiling.h"
#include "profiling/CaptureController.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QToolButton>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Temporary {std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-profile-widget-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));~Temporary(){std::error_code error;std::filesystem::remove_all(root,error);}};
void nativeControls(){
    Temporary temporary;QMainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);
    crimson::profiling::CaptureOptions options;options.toolsDirectory=temporary.root/"missing tools";options.outputDirectory=temporary.root/"first";
    crimson::profiling::CaptureController captures(options);
    {
        crimson::integration::LevelProfiling controls(window,captures);controls.update(false);
        auto* editor=window.findChild<QAction*>("CrimsonRecordEditor");auto* game=window.findChild<QAction*>("CrimsonRecordGameplay");auto* stop=window.findChild<QAction*>("CrimsonStopCapture");require(editor&&game&&stop,"Native profiling actions were not composed in the existing window.");
        require(editor->isEnabled()==bool(CRIMSON_ENABLE_PROFILING)&&!game->isEnabled()&&!stop->isEnabled(),"Idle profiling action gates did not preserve build and Play ownership.");
        require(!captures.active()&&!std::filesystem::exists(temporary.root),"Opening native profiling controls launched a capture or wrote files.");
        controls.update(true);require(game->isEnabled()==bool(CRIMSON_ENABLE_PROFILING),"Gameplay capture did not follow actual Play state.");
        captures.setOutputDirectory(temporary.root/"selected project");
        if(CRIMSON_ENABLE_PROFILING){
            editor->trigger();require(captures.status().kind==crimson::profiling::CaptureKind::Editor,"Native editor capture triggered the wrong recording owner.");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{3};while(captures.active()){QCoreApplication::processEvents();if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Missing-tool status exceeded the bounded native fixture deadline.");std::this_thread::sleep_for(std::chrono::milliseconds{1});}
            controls.update(false);const auto failed=captures.status();require(failed.state==crimson::profiling::CaptureState::Failed&&!failed.detail.empty()&&failed.manifest.parent_path().parent_path()==temporary.root/"selected project","Missing paired tools did not publish a useful error under the accepted Engine root.");
            require(!stop->isEnabled()&&editor->isEnabled(),"A failed recording prevented a normal retry.");
        }
    }
    require(window.menuBar()->cornerWidget(Qt::TopRightCorner)==nullptr&&!window.findChild<QAction*>("CrimsonRecordEditor"),"Profiling controls retained callbacks after their native window binding was released.");
}
}
int main(int argc,char** argv){try{QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);nativeControls();std::cout<<"Native profiling action ownership, idle cleanliness, failure feedback and widget lifetime passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
