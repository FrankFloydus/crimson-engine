// Native console broker only. This target must not link a Tracy client.
#include "CaptureProcess.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <thread>

namespace {
using namespace crimson::profiling::detail;
std::string consoleText(HANDLE console) {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(!GetConsoleScreenBufferInfo(console,&info))throw windowsError("Cannot inspect private capture console");
    const auto count=static_cast<DWORD>(info.dwSize.X)*(static_cast<DWORD>(info.dwCursorPosition.Y)+1);
    std::wstring text(count,L' ');DWORD read{};
    if(!ReadConsoleOutputCharacterW(console,text.data(),count,{0,0},&read))throw windowsError("Cannot read private capture console");
    text.resize(read);
    for(std::size_t at=static_cast<std::size_t>(info.dwSize.X);at<text.size();at+=static_cast<std::size_t>(info.dwSize.X))text[at-1]=L'\n';
    return utf8(text);
}
}

int wmain(int argc,wchar_t** argv) {
    Handle child;
    std::ofstream log;
    try {
        if(argc!=9)throw std::runtime_error("Capture broker requires capture tool, output, log, duration, port and three inherited events.");
        const std::filesystem::path executable=argv[1],output=argv[2];
        log.open(std::filesystem::path(argv[3]),std::ios::binary|std::ios::trunc);
        if(!log)throw std::runtime_error("Cannot open capture broker log.");
        const auto duration=std::stoul(argv[4]),port=std::stoul(argv[5]);
        if(duration>3600||port<1||port>65535)throw std::runtime_error("Capture broker duration or port is invalid.");
        const auto stop=parseHandle(argv[6]),ready=parseHandle(argv[7]),abort=parseHandle(argv[8]);
        DWORD ignored{};
        if(!GetHandleInformation(stop,&ignored)||!GetHandleInformation(ready,&ignored)||!GetHandleInformation(abort,&ignored))
            throw windowsError("Capture broker inherited events are unavailable");
        const auto console=GetStdHandle(STD_OUTPUT_HANDLE);
        if(console==INVALID_HANDLE_VALUE||!console)throw std::runtime_error("Capture broker requires its own hidden console.");
        SetConsoleScreenBufferSize(console,{240,1000});
        SetConsoleCtrlHandler(nullptr,FALSE);
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        Handle consoleInput(CreateFileW(L"CONIN$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
        Handle consoleOutput(CreateFileW(L"CONOUT$",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
        if(!consoleInput.valid()||!consoleOutput.valid())throw windowsError("Cannot inherit private collector console");
        const std::array inherited{consoleInput.value,consoleOutput.value};
        std::vector<std::wstring> arguments{L"-a",L"127.0.0.1",L"-p",argv[5],L"-o",output.wstring()};
        // Without -s the official collector records until our explicit stop
        // event. Never pass -s 0: zero denotes manual mode in Crimson only.
        if(duration){arguments.emplace_back(L"-s");arguments.emplace_back(argv[4]);}
        child=launch(executable,arguments,inherited,0,consoleInput.value,consoleOutput.value);
        // Change only this process after the child inherits normal Ctrl-C
        // behavior. The official CLI installs its SIGINT handler on connection.
        if(!SetConsoleCtrlHandler(nullptr,TRUE))throw windowsError("Cannot protect private capture broker from Ctrl-C");
        bool registered{},stopSent{};std::string previous;auto lastLogged=std::chrono::steady_clock::time_point{};
        for(;;) {
            const auto state=WaitForSingleObject(child.value,50);
            const auto text=consoleText(console);
            const auto now=std::chrono::steady_clock::now();
            if(text!=previous&&(!registered||state==WAIT_OBJECT_0||now-lastLogged>=std::chrono::seconds{1})) {
                log<<text<<"\n";log.flush();previous=text;lastLogged=now;
            }
            if(!registered&&text.find("query backlog")!=std::string::npos) {
                // This progress marker is after official SIGINT registration.
                if(!SetEvent(ready))throw windowsError("Cannot publish collector readiness");
                registered=true;
            }
            if(state==WAIT_OBJECT_0) {
                DWORD code{};if(!GetExitCodeProcess(child.value,&code))throw windowsError("Cannot read collector exit status");
                return static_cast<int>(code);
            }
            if(state==WAIT_FAILED)throw windowsError("Cannot wait for official collector");
            if(WaitForSingleObject(abort,0)==WAIT_OBJECT_0) {
                TerminateProcess(child.value,ERROR_CANCELLED);WaitForSingleObject(child.value,5000);
                log<<"Capture broker aborted before a complete official save.\n";return 20;
            }
            if(registered&&!stopSent&&WaitForSingleObject(stop,0)==WAIT_OBJECT_0) {
                if(!GenerateConsoleCtrlEvent(CTRL_C_EVENT,0))throw windowsError("Cannot gracefully stop official collector");
                stopSent=true;
            }
        }
    } catch(const std::exception& error) {
        if(log){log<<"Capture broker error: "<<error.what()<<'\n';log.flush();}
        if(child.valid()&&WaitForSingleObject(child.value,0)==WAIT_TIMEOUT){TerminateProcess(child.value,ERROR_CANCELLED);WaitForSingleObject(child.value,5000);}
        return 21;
    }
}
