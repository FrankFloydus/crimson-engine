////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader {

inline constexpr std::string_view kQdrFormatName = "quader.qdr";
inline constexpr std::string_view kQdrDocumentPayloadKey = "document";
inline constexpr std::string_view kQdrLegacySessionPayloadKey = "session";
inline constexpr std::string_view kQdrProjectDocumentKind = "quader.project-document";
inline constexpr int kQdrProjectFormatVersion = 4;
inline constexpr int kQdrLegacySessionFormatVersion = 3;
inline constexpr int kQdrMinSupportedFormatVersion = 1;

} // namespace quader
