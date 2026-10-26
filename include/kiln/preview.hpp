#pragma once

#include <string>

#include "kiln/camera.hpp"
#include "kiln/hittable.hpp"
#include "kiln/renderer.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// True when Kiln was built with the SDL2 preview window (CMake option KILN_PREVIEW).
bool preview_available();

// Opens a window that refines the scene live, one sample per pixel per pass, up to the camera's
// samples_per_pixel. Drag to orbit, scroll to zoom, R resets the view, S saves a PNG to
// out_path, C prints the camera as JSON for a scene file, Esc or closing the window quits.
// Returns a process exit code.
int run_preview(const Hittable& world, const CameraConfig& config, const Color& background,
                const RenderSettings& settings, const std::string& out_path);

}  // namespace kiln
