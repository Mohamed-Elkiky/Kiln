#include "kiln/renderer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "kiln/interval.hpp"
#include "kiln/material.hpp"
#include "kiln/random.hpp"
#include "kiln/ray.hpp"

namespace kiln {

namespace {

// The denoiser's guide data for one camera ray: the first non-specular surface it reaches.
struct FirstHit {
    Color albedo{0.0, 0.0, 0.0};
    Vec3 normal{0.0, 0.0, 0.0};
    Color tint{1.0, 1.0, 1.0};  // product of the mirrors and glass passed through on the way

    void record(const Color& surface_albedo, const Vec3& surface_normal) {
        albedo = tint * surface_albedo;
        normal = surface_normal;
    }
};

Color clamp01(const Color& c) {
    return {std::clamp(c.x, 0.0, 1.0), std::clamp(c.y, 0.0, 1.0), std::clamp(c.z, 0.0, 1.0)};
}

constexpr double kPi = 3.1415926535897932385;

// t_min = 0.001 stops a bounced ray re-hitting the surface it just left ("shadow acne").
constexpr double kRayEpsilon = 0.001;

// Traces paths through one scene. Holds what every bounce needs, so the recursion only passes
// what changes: the ray, the bounces left, and whether hitting a light should count.
//
// Next event estimation (NEE): at a diffuse surface, light arrives two ways, straight from a
// light or bounced off something else. Shadow rays fire straight at a light to get the first
// part; the random bounce gets the second. If that bounce then lands on a sampled light, its glow
// was already counted by the shadow ray, so it is skipped (count_emission = false). Mirrors and
// glass can't use shadow rays (only one exact direction reaches the light), so a bounce off them
// counts emission again. That is also how caustics still appear.
class PathTracer {
public:
    PathTracer(const Hittable& world, const Color& background, const Lights* lights,
               std::uint64_t& rays)
        : world_(world),
          background_(background),
          lights_(lights && !lights->empty() ? lights : nullptr),
          rays_(rays) {}

    Color trace(const Ray& r, int depth, bool count_emission) const {
        // Bounce limit reached: no more light gathered.
        if (depth <= 0) return Color{0.0, 0.0, 0.0};
        ++rays_;

        HitRecord rec;
        if (!world_.hit(r, Interval(kRayEpsilon, kInfinity), rec)) return background_;
        return shade(r, rec, depth, count_emission);
    }

    // Same maths as trace(), but also records the denoiser guides. Glass and perfect mirrors show
    // what's behind them, so it follows the ray through them to the first rough surface, then
    // hands the rest of the path to plain trace(). It draws exactly the same random numbers, so
    // the beauty result is bit-identical either way.
    Color trace_guided(const Ray& r, int depth, bool count_emission, FirstHit& first) const {
        if (depth <= 0) return Color{0.0, 0.0, 0.0};
        ++rays_;

        HitRecord rec;
        if (!world_.hit(r, Interval(kRayEpsilon, kInfinity), rec)) {
            first.record(clamp01(background_), Vec3{0.0, 0.0, 0.0});
            return background_;
        }

        if (!rec.mat->is_specular()) {
            first.record(rec.mat->albedo(), rec.normal);
            return shade(r, rec, depth, count_emission);
        }

        // Mirrors and glass never use NEE, so this is shade() with the guide bookkeeping added.
        const Color emission = rec.mat->emitted();
        Ray scattered;
        Color attenuation;
        if (!rec.mat->scatter(r, rec, attenuation, scattered)) {
            first.record(rec.mat->albedo(), rec.normal);  // a mirror absorbed the ray
            return emission;
        }
        first.tint = first.tint * attenuation;
        return emission + attenuation * trace_guided(scattered, depth - 1, true, first);
    }

private:
    // Light leaving the hit point back along r.
    Color shade(const Ray& r, const HitRecord& rec, int depth, bool count_emission) const {
        // Light the surface gives off itself (only lights are non-black). Skipped when a shadow
        // ray from the previous bounce already sampled this light.
        Color emission = rec.mat->emitted();
        if (!count_emission && lights_ && lights_->contains(rec.object)) {
            emission = Color{0.0, 0.0, 0.0};
        }

        Ray scattered;
        Color attenuation;
        // Lights don't scatter: just return their glow.
        if (!rec.mat->scatter(r, rec, attenuation, scattered)) return emission;

        if (lights_ && rec.mat->is_diffuse()) {
            // Two statements on purpose: both calls draw random numbers, and C++ doesn't fix the
            // order of operands within one expression. Same order everywhere = same image.
            const Color direct = direct_light(rec);
            return emission + direct + attenuation * trace(scattered, depth - 1, false);
        }
        return emission + attenuation * trace(scattered, depth - 1, true);
    }

    // One shadow ray to a random point on a random light: the light arriving at a diffuse
    // surface straight from the lights, estimated with one sample.
    //
    //   L = (albedo / pi) * Le * cos(surface) * cos(light) / distance^2 / pdf
    //
    // where pdf = 1 / (light count * light area) for picking that point.
    Color direct_light(const HitRecord& rec) const {
        const std::size_t count = lights_->size();
        const auto pick = std::min(
            count - 1, static_cast<std::size_t>(random_double() * static_cast<double>(count)));
        const Quad& light = (*lights_)[pick];

        const Vec3 to_light = light.random_point() - rec.p;
        const double dist_sq = to_light.length_squared();
        const double dist = std::sqrt(dist_sq);
        const Vec3 wi = to_light / dist;

        const double cos_surface = dot(rec.normal, wi);
        if (cos_surface <= 0.0) return Color{0.0, 0.0, 0.0};  // light is behind the surface
        // Lights glow from both faces (DiffuseLight ignores which side it was hit on).
        const double cos_light = std::fabs(dot(light.normal(), wi));
        if (cos_light < 1e-8) return Color{0.0, 0.0, 0.0};  // seeing the light edge-on

        // Anything between the surface and the light blocks it, glass included: light through
        // glass arrives via the specular bounce instead.
        ++rays_;
        HitRecord blocker;
        if (world_.hit(Ray(rec.p, wi), Interval(kRayEpsilon, dist - kRayEpsilon), blocker)) {
            return Color{0.0, 0.0, 0.0};
        }

        const double weight =
            cos_surface * cos_light * light.area() * static_cast<double>(count) / (kPi * dist_sq);
        return rec.mat->albedo() * light.material().emitted() * weight;
    }

    const Hittable& world_;
    const Color background_;
    const Lights* lights_;
    std::uint64_t& rays_;
};

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
                   Image& image, const RenderSettings& settings, const RenderAovs& aovs) {
    const int width = image.width();
    const int height = image.height();
    for (const Image* aov : {aovs.albedo, aovs.normal}) {
        if (aov && (aov->width() != width || aov->height() != height)) {
            throw std::invalid_argument("render: AOV images must be the same size as the image");
        }
    }
    const bool want_aovs = aovs.albedo || aovs.normal;
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

    auto cancelled = [&] {
        return settings.cancel && settings.cancel->load(std::memory_order_relaxed);
    };

    auto worker = [&]() {
        std::uint64_t rays = 0;  // counted locally, added to the total once at the end
        const PathTracer tracer(world, background, settings.lights, rays);
        while (true) {
            const int t = next_tile.fetch_add(1, std::memory_order_relaxed);
            if (t >= tile_count || cancelled()) {
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
                    const int spp = camera.samples_per_pixel();
                    Color pixel_color{0.0, 0.0, 0.0};
                    if (!want_aovs) {
                        // Hot path: no guide bookkeeping at all.
                        for (int s = 0; s < spp; ++s) {
                            pixel_color +=
                                tracer.trace(camera.get_ray(i, j), camera.max_depth(), true);
                        }
                    } else {
                        Color albedo_sum{0.0, 0.0, 0.0};
                        Vec3 normal_sum{0.0, 0.0, 0.0};
                        for (int s = 0; s < spp; ++s) {
                            FirstHit first;
                            pixel_color += tracer.trace_guided(camera.get_ray(i, j),
                                                               camera.max_depth(), true, first);
                            albedo_sum += first.albedo;
                            normal_sum += first.normal;
                        }
                        if (aovs.albedo) aovs.albedo->set(i, j, albedo_sum / spp);
                        if (aovs.normal) aovs.normal->set(i, j, normal_sum / spp);
                    }
                    // Each pixel belongs to exactly one tile, so threads never write the same
                    // element: no lock needed on the framebuffer.
                    image.set(i, j, pixel_color / spp);
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
            if (done >= tile_count || cancelled()) break;
            done_cv.wait_for(lock, std::chrono::milliseconds(100), [&] {
                return tiles_done.load(std::memory_order_relaxed) >= tile_count || cancelled();
            });
        }
        std::clog << "\rDone.                         \n";
    }

    // join() also makes every worker's pixel writes visible to this thread before we return.
    for (auto& t : pool) t.join();
    return {total_rays.load()};
}

}  // namespace kiln
