////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"

#include <vector>

namespace quader {

class QuaderEditorApi;
struct ViewportInputFrame;
struct ViewportToolState;
struct QuaderViewportKeybindingRepeat;

/**
 * Resolves canonical host input into semantic viewport actions and commands.
 */
class ViewportKeybindingResolver final {
public:
  static void resolve(QuaderEditorApi &editor, const ViewportToolState &state,
                      ViewportInputFrame &input,
                      std::vector<QuaderInputChord> &suppressed_chords,
                      double &linear_array_wheel_remainder,
                      double &radial_array_wheel_remainder,
                      ViewportRuntimeBindingToken runtime_binding = {},
                      std::vector<QuaderViewportKeybindingRepeat> *repeat_bindings = nullptr);
};

} // namespace quader
