////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace quader_io {

using QdrJson = nlohmann::ordered_json;

/**
 * Stores the shared QDR root format/version header parsed before payload dispatch.
 */
struct QdrRootHeader {
    std::string format;
    int version = 0;
};

[[nodiscard]] std::string qdr_string_from_view(std::string_view value);
void require_qdr_root_object(const QdrJson& root);
[[nodiscard]] QdrRootHeader read_qdr_root_header(const QdrJson& root);
[[nodiscard]] const QdrJson &
qdr_payload_from_root(const QdrJson &root,
                      std::string_view document_payload_key,
                      std::string_view legacy_payload_key);

} // namespace quader_io
