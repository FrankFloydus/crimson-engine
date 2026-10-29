#include "SceneNames.h"
#include <algorithm>

namespace crimson {
namespace {
struct Family {std::string_view stem,number;};
Family familyOf(std::string_view name) {
    const auto split=name.rfind('_');
    if(split==std::string_view::npos||split==0||split+1==name.size())return {name,{}};
    auto number=name.substr(split+1);
    if(!std::all_of(number.begin(),number.end(),[](char digit){return digit>='0'&&digit<='9';}))return {name,{}};
    const auto nonzero=number.find_first_not_of('0');
    number.remove_prefix(nonzero==std::string_view::npos?number.size()-1:nonzero);
    return {name.substr(0,split),number};
}
void incrementDecimal(std::string& value) {
    auto digit=value.size();
    while(digit&&value[digit-1]=='9')value[--digit]='0';
    if(digit)++value[digit-1];else value.insert(value.begin(),'1');
}
}
SceneDuplicateNameAllocator::SceneDuplicateNameAllocator(std::span<const EntityRecord> records) {
    maxima_.reserve(records.size());for(const auto& record:records)reserve(record.name);
}
void SceneDuplicateNameAllocator::reserve(std::string_view name) {
    const auto family=familyOf(name);if(family.number.empty())return;
    auto& maximum=maxima_[std::string(family.stem)];
    if(family.number.size()>maximum.size()||(family.number.size()==maximum.size()&&family.number>maximum))maximum=family.number;
}
std::string SceneDuplicateNameAllocator::next(std::string_view source) {
    if(source.empty())source="Entity";
    reserve(source);const auto family=familyOf(source);
    auto& maximum=maxima_[std::string(family.stem)];if(maximum.empty())maximum="0";
    incrementDecimal(maximum);
    std::string result(family.stem);result.push_back('_');
    if(maximum.size()<3)result.append(3-maximum.size(),'0');
    result+=maximum;return result;
}
}
