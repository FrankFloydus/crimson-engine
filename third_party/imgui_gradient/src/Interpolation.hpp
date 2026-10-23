#pragma once

#include <cstddef> // Includes size_t

namespace ImGG {

/// Controls how the colors are interpolated between two marks.
enum class Interpolation : size_t { // We use size_t so that we can use the WrapMode to index into an array
    /// Linear interpolation between two marks.
    Linear,
    /// Crimson: hold the left mark until the next mark is reached.
    Constant,
    /// Crimson: smoothstep interpolation between adjacent marks.
    Smooth,
};

} // namespace ImGG
