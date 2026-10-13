#pragma once
#include "Math.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace crimson {
namespace math_json_detail {
template<class T> void read(const nlohmann::json& json, T& value) {
    if (!json.is_array() || json.size()!=value.size()) throw std::invalid_argument("Invalid math value array length.");
    T result;
    for (std::size_t i=0;i<value.size();++i) {
        if (!json[i].is_number()) throw std::invalid_argument("Math values must contain numbers.");
        result[i]=json[i].get<float>();
        if (!std::isfinite(result[i])) throw std::invalid_argument("Math values must be finite.");
    }
    value=result;
}
}
template<std::size_t N> void to_json(nlohmann::json& json, const Vector<N>& value) { json=value.values; }
template<std::size_t N> void from_json(const nlohmann::json& json, Vector<N>& value) { math_json_detail::read(json,value); }
template<std::size_t N> void to_json(nlohmann::json& json, const Matrix<N>& value) { json=value.values; }
template<std::size_t N> void from_json(const nlohmann::json& json, Matrix<N>& value) { math_json_detail::read(json,value); }
inline void to_json(nlohmann::json& json, const Quat& value) { json=value.values; }
inline void from_json(const nlohmann::json& json, Quat& value) { math_json_detail::read(json,value); }
} // namespace crimson
