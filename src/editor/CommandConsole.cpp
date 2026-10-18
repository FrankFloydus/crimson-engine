#include "CommandConsole.h"
#include <algorithm>

namespace crimson {
namespace {
constexpr std::string_view AttributeCommand="ShowDebug Attributes";
constexpr std::string_view ApplyCommand="ApplyEffect",RemoveCommand="RemoveEffect";
bool whitespace(char value){return value==' '||value=='\t'||value=='\r'||value=='\n'||value=='\f'||value=='\v';}
std::string normalized(std::string_view text,bool lowercase) {
    std::string result;bool pendingSpace=false;
    for(const char value:text) {
        if(whitespace(value)){pendingSpace=!result.empty();continue;}
        if(pendingSpace)result+=' ';pendingSpace=false;
        result+=lowercase&&value>='A'&&value<='Z'?static_cast<char>(value-'A'+'a'):value;
    }
    return result;
}
std::string trim(std::string_view value) {
    while(!value.empty()&&whitespace(value.front()))value.remove_prefix(1);
    while(!value.empty()&&whitespace(value.back()))value.remove_suffix(1);
    return std::string(value);
}
std::optional<std::string> effectArgument(std::string_view value) {
    auto argument=trim(value);if(argument.empty())return {};
    if(argument.front()!='"') {
        if(std::any_of(argument.begin(),argument.end(),whitespace))return {};
        return argument;
    }
    std::string decoded;bool escape=false;
    for(std::size_t i=1;i<argument.size();++i) {
        const auto c=argument[i];
        if(escape){if(c!='"'&&c!='\\')return {};decoded+=c;escape=false;}
        else if(c=='\\')escape=true;
        else if(c=='"'){if(i+1!=argument.size()||decoded.empty())return {};return decoded;}
        else decoded+=c;
    }
    return {};
}
std::string quote(std::string_view value) {
    std::string result="\"";
    for(const auto c:value){if(c=='"'||c=='\\')result+='\\';result+=c;}
    return result+'"';
}
}
ConsoleCommandResult CommandConsole::parse(std::string_view value) {
    const auto command=normalized(value,true);
    if(command.empty())return {};
    if(command==normalized(AttributeCommand,true))return {ConsoleCommand::ShowDebugAttributes,{}};
    const auto input=trim(value);const auto split=input.find_first_of(" \t\r\n");
    const auto verb=normalized(input.substr(0,split),true);
    if(verb=="applyeffect"||verb=="removeeffect") {
        const auto argument=split==std::string::npos?std::nullopt:effectArgument(std::string_view(input).substr(split));
        if(!argument)return {{},"Use "+std::string(verb=="applyeffect"?ApplyCommand:RemoveCommand)+" <UUID or \"effect name\">."};
        return {verb=="applyeffect"?ConsoleCommand::ApplyEffect:ConsoleCommand::RemoveEffect,{},*argument};
    }
    return {{},"Unknown command. Try ShowDebug Attributes, ApplyEffect or RemoveEffect."};
}
std::optional<std::string> resolveConsoleEffect(std::string_view argument,std::span<const ConsoleEffect> effects,std::string& diagnostic) {
    diagnostic.clear();const auto key=normalized(argument,true);
    for(const auto& effect:effects)if(normalized(effect.id,true)==key)return effect.id;
    std::optional<std::string> result;
    for(const auto& effect:effects)if(normalized(effect.name,true)==key) {
        if(result&&*result!=effect.id){diagnostic="Effect name is ambiguous; use its UUID.";return {};}
        result=effect.id;
    }
    if(!result)diagnostic="Effect not found: "+std::string(argument);
    return result;
}
void CommandConsole::setText(std::string value) {
    text_=std::move(value);historyPosition_.reset();draft_.clear();status_.clear();
}
std::vector<std::string> CommandConsole::completions() const {
    const auto prefix=normalized(text_,true);
    if(prefix.empty())return {};
    std::vector<std::string> result;
    for(const auto verb:{AttributeCommand,ApplyCommand,RemoveCommand})
        if(normalized(verb,true).starts_with(prefix))result.emplace_back(verb);
    const auto raw=trim(text_);const auto split=raw.find_first_of(" \t");
    const auto verb=normalized(raw.substr(0,split),true);
    if((verb=="applyeffect"||verb=="removeeffect") && (split!=std::string::npos || (!text_.empty()&&whitespace(text_.back())))) {
        result.clear();auto filter=split==std::string::npos?std::string{}:trim(std::string_view(raw).substr(split));
        if(!filter.empty()&&filter.front()=='"')filter.erase(filter.begin());
        if(!filter.empty()&&filter.back()=='"')filter.pop_back();
        filter=normalized(filter,true);
        for(const auto& effect:effects_)if(normalized(effect.name,true).find(filter)!=std::string::npos||normalized(effect.id,true).starts_with(filter)) {
            const auto duplicates=std::count_if(effects_.begin(),effects_.end(),[&](const auto& other){return normalized(other.name,true)==normalized(effect.name,true);});
            result.push_back(std::string(verb=="applyeffect"?ApplyCommand:RemoveCommand)+' '+(duplicates>1?effect.id:quote(effect.name)));
        }
        std::sort(result.begin(),result.end());
    }
    return result;
}
bool CommandConsole::acceptCompletion(std::size_t index) {
    const auto choices=completions();if(index>=choices.size())return false;
    setText(choices[index]);return true;
}
bool CommandConsole::previousHistory() {
    if(history_.empty())return false;
    if(!historyPosition_){draft_=text_;historyPosition_=history_.size()-1;}
    else if(*historyPosition_>0)--*historyPosition_;
    else return false;
    text_=history_[*historyPosition_];status_.clear();return true;
}
bool CommandConsole::nextHistory() {
    if(!historyPosition_)return false;
    if(*historyPosition_+1<history_.size())text_=history_[++*historyPosition_];
    else {text_=std::move(draft_);draft_.clear();historyPosition_.reset();}
    status_.clear();return true;
}
std::optional<ConsoleCommand> CommandConsole::submit() {
    auto result=parse(text_);auto entered=normalized(text_,false);
    if(result.command==ConsoleCommand::ShowDebugAttributes)entered=AttributeCommand;
    else if(result.command)entered=std::string(*result.command==ConsoleCommand::ApplyEffect?ApplyCommand:RemoveCommand)+' '+quote(result.argument);
    if(!entered.empty()&&(history_.empty()||history_.back()!=entered)) {
        history_.push_back(std::move(entered));if(history_.size()>MaximumHistory)history_.erase(history_.begin());
    }
    text_.clear();draft_.clear();historyPosition_.reset();status_=std::move(result.status);argument_=std::move(result.argument);return result.command;
}
}
