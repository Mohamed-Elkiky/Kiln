#include "kiln/renderer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "kiln/interval.hpp"
#include "kiln/material.hpp"
#include "kiln/random.hpp"
#include "kiln/ray.hpp"

namespace kiln {

namespace {

Color ray_color(const Ray& r, int depth, const Hittable& world, const Color& background,
                std::uint64_t& rays) {
    // Bounce limit reached: no more light gathered.
    if (depth <= 0) return Color{0.0, 0.0, 0.0};
    ++rays;

    HitRecord rec;
    // t_min = 0.001 stops a bounced ray re-hitting the surface it just left ("shadow acne").
    if (!world.hit(r, Interval(0.001, kInfinity), rec)) return background;

    // Light the surface gives off itself (only lights are non-black).
    const Color emission = rec.mat->emitted();

    Ray scattered;
    Color attenuation;
    // Lights don't scatter: just return their glow.
    if (!rec.mat->scatter(r, rec, attenuation, scattered)) return emission;

    return emission + attenuation * ray_color(scattered, depth - 1, world, background, rays);
}

// Spreads nearby tile indices across the whole 32-bit range, so neighbouring tiles
// don't get near-identical seeds (and so near-identical noise patterns).
std::uint32_t mix_seed(std::uint32_t seed, std::uint32_t tile) {
    std::uint32_t x = seed ^ (tile * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

}  // namespace

int resolve_thread_count(const RenderSettings& settings) {
    if (settings.threads > 0) return settings.threads;
    // hardware_concurrency() may return 0 if it can't tell.
    return std::max(1u, std::thread::hardware_concurrency());
}

RenderStats render(const Camera& camera, const Hittable& world, const Color& background,
                   Image& image, const RenderSettings& settings) {
    const int width = image.width();
    const int height = image.height();
    const int tile = std::max(1, settings.tile_size);
    const int tiles_x = (width + tile - 1) / tile;
    const int tiles_y = (height + tile - 1) / tile;
    const int tile_count = tiles_x * tiles_y;

    std::atomic<int> next_tile{0};
    std::atomic<int> tiles_done{0};
    std::atomic<std::uint64_t> total_rays{0};

    // Lets the last worker wake the progress loop immediately instead of after its next sleep.
    std::mutex done_mutex;
    std::condition_variable done_cv;

    auto worker = [&]() {
        std::uint64_t rays = 0;  // counted locally, added to the total once at the end
        while (true) {
            const int t = next_tile.fetch_add(1, std::memory_order_relaxed);
            if (t >= tile_count) {
                total_rays.fetch_add(rays, std::memory_order_relaxed);
                return;
            }

            // Reseed this thread's RNG for this tile: the pixels depend only on the tile index,
            // never on which thread rendered it or in what order.
            seed_rng(mix_seed(settings.seed, static_cast<std::uint32_t>(t)));

            const int x0 = (t % tiles_x) * tile;
            const int y0 = (t / tiles_x) * tile;
            const int x1 = std::min(x0 + tile, width);
            const int y1 = std::min(y0 + tile, height);

            for (int j = y0; j < y1; ++j) {
                for (int i = x0; i < x1; ++i) {
                    Color pixel_color{0.0, 0.0, 0.0};
                    for (int s = 0; s < camera.samples_per_pixel(); ++s) {
                        pixel_color += ray_color(camera.get_ray(i, j), camera.max_depth(), world,
                                                 background, rays);
                    }
                    // Each pixel belongs to exactly one tile, so threads never write the same
                    // element: no lock needed on the framebuffer.
                    image.set(i, j, pixel_color / camera.samples_per_pixel());
                }
            }
            if (tiles_done.fetch_add(1, std::memory_order_relaxed) + 1 == tile_count) {
                const std::lock_guard<std::mutex> lock(done_mutex);
                done_cv.notify_one();
            }
        }
    };

    const int thread_count = std::min(resolve_thread_count(settings), tile_count);
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(thread_count));
    for (int n = 0; n < thread_count; ++n) pool.emplace_back(worker);

    // Main thread only reports progress; the workers do all the rendering.
    // wait_for returns early the moment the last tile finishes, so timing isn't padded by a sleep.
    if (settings.show_progress) {
        std::unique_lock<std::mutex> lock(done_mutex);
        while (true) {
            const int done = tiles_done.load(std::memory_order_relaxed);
            std::clog << "\rTiles: " << done << '/' << tile_count << " ("
                      << (100 * done / tile_count) << "%)   " << std::flush;
            if (done >= tile_count) break;
            done_cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return tiles_done.load(std::memory_order_relaxed) >= tile_count;
            });
        }
        std::clog << "\rDone.                         \n";
    }

    // join() also makes every worker's pixel writes visible to this thread before we return.
    for (auto& t : pool) t.join();
    return {total_rays.load()};
}

}  // namespace kiln
