#include "resources/ShaderCompiler.h"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs=std::filesystem;
using namespace crimson;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE v=nullptr):value(v){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
fs::path executable(){std::wstring path(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));check(size&&size<path.size(),"Cannot resolve test executable.");path.resize(size);return path;}
std::wstring quoted(const fs::path& path){return L"\""+path.wstring()+L"\"";}
std::string read(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
void write(const fs::path& path,const std::string& text){std::ofstream output(path,std::ios::binary);output<<text;check(bool(output),"Cannot write process coordination fixture.");}
struct Child {
    Handle process;
    explicit Child(const std::wstring& arguments):process() {
        auto command=quoted(executable())+L" "+arguments;STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION info{};
        check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=FALSE,"Cannot start isolated shader-cache requester.");
        process.value=info.hProcess;CloseHandle(info.hThread);
    }
    ~Child(){if(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){TerminateProcess(process.value,ERROR_CANCELLED);WaitForSingleObject(process.value,5000);}}
    DWORD finish(){check(WaitForSingleObject(process.value,10000)==WAIT_OBJECT_0,"Shader-cache requester timed out.");DWORD result{};check(GetExitCodeProcess(process.value,&result)!=FALSE,"Cannot read requester exit code.");return result;}
};
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-shader-singleflight-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root/"cache/runs");fs::create_directories(root/"sources");fs::create_directories(root/"includes");write(root/"varying.def.sc","");}
    ~Project(){std::error_code error;const auto target=fs::weakly_canonical(root,error),owner=fs::weakly_canonical(fs::temp_directory_path(),error);if(!error&&target.parent_path()==owner&&target.filename().string().starts_with("crimson-shader-singleflight-"))fs::remove_all(target,error);}
};
struct Gate {
    std::wstring name=L"Local\\CrimsonShaderFixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
    Handle handle{CreateEventW(nullptr,TRUE,FALSE,name.c_str())};
    explicit Gate(const fs::path& root){check(handle.value!=nullptr,"Cannot create compiler gate.");write(root/"cache/gate",std::string(name.begin(),name.end()));}
    ~Gate(){SetEvent(handle.value);}
    void release(){check(SetEvent(handle.value)!=FALSE,"Cannot release compiler gate.");}
};
unsigned runs(const fs::path& root){unsigned count{};for(const auto& entry:fs::directory_iterator(root/"cache/runs"))if(entry.is_regular_file())++count;return count;}
template<class Predicate> void until(Predicate predicate,const char* message){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){check(std::chrono::steady_clock::now()<deadline,message);std::this_thread::sleep_for(std::chrono::milliseconds(5));}}

// Acts as shaderc for this process-coordination test only. Real shader syntax,
// reflection and GPU publication are covered by the separate compiler suites.
int compiler(int argc,char** argv){
    fs::path source,output;for(int i=1;i+1<argc;++i){if(std::string_view(argv[i])=="-f")source=argv[++i];else if(std::string_view(argv[i])=="-o")output=argv[++i];}
    check(!source.empty()&&!output.empty(),"Compiler fixture arguments are incomplete.");
    const auto cache=source.parent_path();write(cache/"runs"/(std::to_string(GetCurrentProcessId())+".txt"),"invoked");
    const auto gateName=read(cache/"gate");const std::wstring wide(gateName.begin(),gateName.end());Handle gate{OpenEventW(SYNCHRONIZE,FALSE,wide.c_str())};
    check(gate.value&&WaitForSingleObject(gate.value,10000)==WAIT_OBJECT_0,"Compiler fixture gate timed out.");
    write(output,std::string{"FSH"}+char(11)+std::string(20,'\0'));return 0;
}
int request(const fs::path& root,const std::string& key,const std::string& id,bool cancel){
    write(root/(id+".ready"),"ready");until([&]{return fs::exists(root/"start");},"Requester start timed out.");
    ShaderCompileConfig config{executable(),root/"sources",root/"includes",root/"cache"};
    ShaderCompileRecipe recipe;recipe.name=key;recipe.source="float4 main() : SV_Target { return 1; }";recipe.varyingDefinition=root/"varying.def.sc";recipe.raw=true;
    std::stop_source stop;std::jthread cancelThread;
    if(cancel)cancelThread=std::jthread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));stop.request_stop();});
    try{const auto binary=compileShader(config,recipe,stop.get_token());check(!cancel&&!binary.bytes.empty(),"A waiting canceled requester published a shader.");write(root/(id+".result"),std::to_string(shaderCompilerMetrics().compilerInvocations));}
    catch(const std::exception& error){if(!cancel||std::string(error.what()).find("canceled")==std::string::npos)throw;check(shaderCompilerMetrics().compilerInvocations==0,"A canceled waiter started another compiler.");write(root/(id+".result"),"canceled");}
    return 0;
}
std::unique_ptr<Child> launch(const fs::path& root,const std::string& key,const std::string& id,bool cancel=false){return std::make_unique<Child>(L"--request "+quoted(root)+L" "+std::wstring(key.begin(),key.end())+L" "+std::wstring(id.begin(),id.end())+(cancel?L" --cancel":L""));}
void sameInput(){
    Project project;Gate gate(project.root);auto first=launch(project.root,"shared","first"),second=launch(project.root,"shared","second");
    until([&]{return fs::exists(project.root/"first.ready")&&fs::exists(project.root/"second.ready");},"Both shader requesters did not start.");write(project.root/"start","go");
    until([&]{return runs(project.root)>0;},"No cold compiler started.");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));gate.release();
    check(first->finish()==0&&second->finish()==0,"A concurrent shader requester failed.");
    const auto count=std::stoul(read(project.root/"first.result"))+std::stoul(read(project.root/"second.result"));
    check(count==1&&runs(project.root)==1,"Two processes compiled the same cold shader instead of sharing its published artifact.");
}
void independentInputs(){
    Project project;Gate gate(project.root);auto first=launch(project.root,"independent-a","first"),second=launch(project.root,"independent-b","second");
    until([&]{return fs::exists(project.root/"first.ready")&&fs::exists(project.root/"second.ready");},"Independent requesters did not start.");write(project.root/"start","go");
    until([&]{return runs(project.root)==2;},"Unrelated shader inputs were serialized behind one compiler lease.");gate.release();
    check(first->finish()==0&&second->finish()==0,"Independent shader compilation failed.");
}
void canceledWaiter(){
    Project project;Gate gate(project.root);auto first=launch(project.root,"cancel-shared","owner");write(project.root/"start","go");until([&]{return runs(project.root)==1;},"The owner compiler did not reach its gate.");
    auto second=launch(project.root,"cancel-shared","waiter",true);check(second->finish()==0&&read(project.root/"waiter.result")=="canceled","A canceled shader waiter did not release promptly.");
    check(runs(project.root)==1,"A canceled waiter launched a duplicate compiler.");gate.release();check(first->finish()==0,"Canceling a waiter damaged the owner compilation.");
    auto warm=launch(project.root,"cancel-shared","warm");check(warm->finish()==0&&read(project.root/"warm.result")=="0","The owner's published artifact was not reusable after waiter cancellation.");
}
}
int main(int argc,char** argv){try{
    if(argc>1&&std::string_view(argv[1])=="-f")return compiler(argc,argv);
    if(argc>=5&&std::string_view(argv[1])=="--request")return request(argv[2],argv[3],argv[4],argc==6);
    sameInput();independentInputs();canceledWaiter();std::cout<<"Cross-process shader deduplication, independent inputs and canceled/warm waiters passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
