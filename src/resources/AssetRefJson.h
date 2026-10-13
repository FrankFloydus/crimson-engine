#pragma once
#include "AssetRef.h"
#include <nlohmann/json.hpp>
namespace crimson {
template<class T> void to_json(nlohmann::json& json, const AssetRef<T>& value) { json=value.id; }
template<class T> void from_json(const nlohmann::json& json, AssetRef<T>& value) { value.id=json.get<std::string>(); }
}
