////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/qdr/qdr_binary_container.hpp"
#include "scene/document/scene_content_document.hpp"

#include <string>

namespace quader {

/**
 * Produces a deterministic, human-readable semantic dump of a validated QDRB
 * scene. This JSON is developer output and is not a supported persistence
 * format.
 */
[[nodiscard]] std::string qdr_scene_debug_json(
    const SceneContentDocument &document,
    const quader_io::QdrBinaryContainerReadResult &container);

} // namespace quader
