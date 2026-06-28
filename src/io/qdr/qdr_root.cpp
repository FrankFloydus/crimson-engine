////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_root.hpp"

#include <limits>
#include <stdexcept>

namespace quader_io {

std::string qdr_string_from_view(std::string_view value) {
    return std::string(value.data(), value.size());
}

void require_qdr_root_object(const QdrJson& root) {
    if (!root.is_object()) {
        throw std::runtime_error("QDR document root must be an object.");
    }
}

QdrRootHeader read_qdr_root_header(const QdrJson& root) {
    const auto format = root.find("format");
    if (format == root.end()) {
        throw std::runtime_error("QDR document is missing its format header.");
    }
    if (!format->is_string()) {
        throw std::runtime_error("QDR document format header must be a string.");
    }

    const auto version = root.find("version");
    if (version == root.end()) {
        throw std::runtime_error("QDR document is missing its version header.");
    }
    if (!version->is_number_integer()) {
        throw std::runtime_error("QDR document version header must be an integer.");
    }

    int parsedVersion = 0;
    if (version->is_number_unsigned()) {
        const auto value = version->get<unsigned long long>();
        if (value > static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("QDR document version header is outside the supported integer range.");
        }
        parsedVersion = static_cast<int>(value);
    } else {
        const auto value = version->get<long long>();
        if (value < static_cast<long long>(std::numeric_limits<int>::min()) ||
                value > static_cast<long long>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("QDR document version header is outside the supported integer range.");
        }
        parsedVersion = static_cast<int>(value);
    }

    return {
            .format = format->get<std::string>(),
            .version = parsedVersion,
    };
}

const QdrJson& qdr_payload_from_root(
        const QdrJson& root,
        std::string_view documentPayloadKey,
        std::string_view legacyPayloadKey) {
    if (root.contains(qdr_string_from_view(documentPayloadKey))) {
        return root.at(qdr_string_from_view(documentPayloadKey));
    }
    if (root.contains(qdr_string_from_view(legacyPayloadKey))) {
        return root.at(qdr_string_from_view(legacyPayloadKey));
    }
    throw std::runtime_error("QDR document is missing its document payload.");
}

} // namespace quader_io
