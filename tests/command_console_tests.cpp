#include "editor/CommandConsole.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void parsing(){
    for(const auto* text:{"ShowDebug Attributes","showdebug attributes"," SHOWDEBUG\t\tAttributes \n","\rShowDebug\nAttributes\v"})
        require(CommandConsole::parse(text).command==ConsoleCommand::ShowDebugAttributes,"The registered command did not accept case or whitespace variations.");
    for(const auto* text:{"ShowDebug","ShowDebug Attribute","ShowDebug Attributes extra","Other Attributes","ShowDebugAttributes","ShowDebug Attributes; ShowDebug Attributes"}){
        const auto result=CommandConsole::parse(text);require(!result.command&&result.status.find("ShowDebug Attributes")!=std::string::npos,"Unknown command text lacked an actionable rejection.");
    }
    const auto empty=CommandConsole::parse(" \t\r\n");require(!empty.command&&empty.status.empty(),"Empty input produced an error or command.");
}
void historyAndDraft(){
    CommandConsole console;require(!console.previousHistory()&&!console.nextHistory(),"An empty console invented history.");
    console.setText(" first  command ");require(!console.submit()&&console.text().empty()&&console.history()==std::vector<std::string>{"first command"}&&!console.status().empty(),
        "Submitting unknown input did not retain a normalized history entry and clear the input.");
    console.setText(" showdebug\tattributes ");require(console.submit()==ConsoleCommand::ShowDebugAttributes&&console.status().empty(),"Registered submission failed or retained an old error.");
    console.setText("SHOWDEBUG ATTRIBUTES");console.submit();require(console.history().size()==2&&console.history().back()=="ShowDebug Attributes","Equivalent consecutive commands duplicated history.");
    console.setText("unfinished draft");require(console.previousHistory()&&console.text()=="ShowDebug Attributes","Up did not select the newest command.");
    require(console.previousHistory()&&console.text()=="first command"&&!console.previousHistory(),"History did not stop at its oldest entry.");
    require(console.nextHistory()&&console.text()=="ShowDebug Attributes"&&console.nextHistory()&&console.text()=="unfinished draft"&&!console.nextHistory(),
        "Down did not restore the draft after visiting history.");
    console.previousHistory();console.setText("edited history draft");console.previousHistory();console.nextHistory();require(console.text()=="edited history draft","Editing a recalled command lost the replacement draft.");
    console.setText(" ");console.submit();require(console.history().size()==2,"Blank submission created a history entry.");
    for(std::size_t index=0;index<CommandConsole::MaximumHistory+5;++index){console.setText("unknown "+std::to_string(index));console.submit();}
    require(console.history().size()==CommandConsole::MaximumHistory&&console.history().front()=="unknown 5","Session history exceeded its bounded retention or removed the wrong entries.");
    CommandConsole separate;require(separate.history().empty()&&separate.text().empty(),"A new session inherited another console's history.");
}
void completion(){
    CommandConsole console;require(console.completions().empty()&&!console.acceptCompletion(),"An idle input opened completion choices.");
    for(const auto* prefix:{"s","SHOwD","showdebug a"," ShowDebug\t Attr  ","ShowDebug Attributes"}){
        console.setText(prefix);require(console.completions()==std::vector<std::string>{"ShowDebug Attributes"},"Completion did not match a normalized registered prefix.");
        require(!console.acceptCompletion(1)&&console.acceptCompletion()&&console.text()=="ShowDebug Attributes"&&console.history().empty(),"Completion executed a command or chose an invalid entry.");
    }
    console.setText("ShowDebug nonsense");require(console.completions().empty()&&!console.acceptCompletion(),"Completion accepted a mismatched argument.");
}
void effects(){
    CommandConsole console;console.setEffects({{"a","Starting Player"},{"b","Poison"},{"c","Same"},{"d","Same"}});
    console.setText("ApplyEffect Sta");require(console.completions()==std::vector<std::string>{"ApplyEffect \"Starting Player\""},"Effect completion did not filter asset names.");
    require(console.acceptCompletion()&&console.submit()==ConsoleCommand::ApplyEffect&&console.argument()=="Starting Player","Effect completion lost its quoted argument.");
    console.setText("RemoveEffect b");require(console.submit()==ConsoleCommand::RemoveEffect&&console.argument()=="b","Effect UUID was not parsed.");
    for(const auto* input:{"ApplyEffect","RemoveEffect \"unfinished","ApplyEffect two words","ApplyEffect \"ok\" extra"})require(!CommandConsole::parse(input).command,"Malformed effect arguments were accepted.");
    std::string diagnostic;const std::vector<ConsoleEffect> catalog{{"a","Poison"},{"b","Poison"}};
    require(resolveConsoleEffect("a",catalog,diagnostic)=="a","Explicit UUID did not resolve.");
    require(!resolveConsoleEffect("poison",catalog,diagnostic)&&diagnostic.find("ambiguous")!=std::string::npos,"An ambiguous asset name chose an arbitrary effect.");
    require(!resolveConsoleEffect("missing",catalog,diagnostic)&&!diagnostic.empty(),"Missing asset lacked a diagnostic.");
}
}
int main(){try{parsing();historyAndDraft();completion();effects();std::cout<<"Command console checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
