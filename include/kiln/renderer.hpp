#pragma once

#include <atomic>
#include <cstdint>

#include "kiln/camera.hpp"
#include "kiln/hittable.hpp"
#include "kiln/image.hpp"
#include "kiln/lights.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

struct RenderSettings {
    int threads = 0;     // 0 = one per hardware thread
    int tile_size = 32;  // tiles are tile_size x tile_size pixels
    std::uint32_t seed = 1337;
    bool show_progress = true;
    // Set the flag to stop early: render() returns once in-flight tiles finish, and tiles it
    // never started are left untouched. Used by the live preview to restart instantly.
    const std::atomic<bool>* cancel = nullptr;
    // Next event estimation: at every diffuse bounce, also sample one of these lights directly
    // with a shadow ray. Same image on average, far less noise. nullptr = off (pure path tracing).
    const Lights* lights = nullptr;
};

// What a render did, for benchmarking.
struct RenderStats {
    std::uint64_t rays = 0;  // every ray traced: camera rays, bounces and shadow rays
};

// Optional guide images for the denoiser, filled during the same render. Per pixel, each is the
// average over its samples of the first non-specular surface the camera rays reach (glass and
// perfect mirrors are seen through). Sky gives the background colour and a zero normal.
struct RenderAovs {
    Image* albedo = nullptr;
    Image* normal = nullptr;
};

// Number of worker threads render() will actually use for these settings.
int resolve_thread_count(const RenderSettings& settings);

// Path traces the whole image across a pool of worker threads.
// Every tile reseeds the RNG from (seed, tile index), so the output is pixel-identical
// whatever the thread count and whichever thread happens to render each tile. Filling the AOVs
// uses no extra random numbers, so the beauty image is identical with or without them.
// Throws std::invalid_argument if an AOV image isn't the same size as `image`.
RenderStats render(const Camera& camera, const Hittable& world, const Color& background,
                   Image& image, const RenderSettings& settings = {}, const RenderAovs& aovs = {});

}  // namespace kiln
