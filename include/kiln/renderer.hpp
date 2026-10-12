#pragma once

#include <cstdint>

#include "kiln/camera.hpp"
#include "kiln/hittable.hpp"
#include "kiln/image.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

struct RenderSettings {
    int threads = 0;          // 0 = one per hardware thread
    int tile_size = 32;       // tiles are tile_size x tile_size pixels
    std::uint32_t seed = 1337;
    bool show_progress = true;
};

// Number of worker threads render() will actually use for these settings.
int resolve_thread_count(const RenderSettings& settings);

// Path traces the whole image across a pool of worker threads.
// Every tile reseeds the RNG from (seed, tile index), so the output is pixel-identical
// whatever the thread count and whichever thread happens to render each tile.
void render(const Camera& camera, const Hittable& world, const Color& background, Image& image,
            const RenderSettings& settings = {});

}  // namespace kiln