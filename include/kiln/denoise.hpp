#pragma once

#include "kiln/image.hpp"

namespace kiln {

// True when Kiln was built with Intel Open Image Denoise (CMake option KILN_DENOISE, OIDN found).
bool denoise_available();

// Removes Monte Carlo noise from `beauty` in place with OIDN's ray tracing filter, guided by the
// albedo and normal images render() fills in. All three must be the same size.
// Throws std::runtime_error if OIDN is unavailable or fails.
void denoise(Image& beauty, const Image& albedo, const Image& normal);

}  // namespace kiln
