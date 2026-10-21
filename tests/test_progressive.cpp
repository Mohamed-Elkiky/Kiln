#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "doctest/doctest.h"
#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/material.hpp"
#include "kiln/progressive.hpp"
#include "kiln/renderer.hpp"
#include "kiln/sphere.hpp"

using namespace kiln;

namespace {

const Color kSky{0.7, 0.8, 1.0};

bool same_pixels(const Image& a, const Image& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Color& p = a.get(x, y);
            const Color& q = b.get(x, y);
            if (p.x != q.x || p.y != q.y || p.z != q.z) return false;
        }
    }
    return true;
}

// Polls until the renderer reaches its target (or gives up after 30 seconds).
ProgressiveRenderer::Status wait_done(const ProgressiveRenderer& r) {
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!r.status().done && std::chrono::steady_clock::now() < give_up) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return r.status();
}

double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

HittableList three_spheres() {
    HittableList world;
    world.add(std::make_shared<Sphere>(Point3{0, -100.5, -1}, 100.0,
                                       std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5})));
    world.add(std::make_shared<Sphere>(Point3{-1, 0, -1}, 0.5, std::make_shared<Dielectric>(1.5)));
    world.add(std::make_shared<Sphere>(Point3{1, 0, -1}, 0.5,
                                       std::make_shared<Metal>(Color{0.8, 0.6, 0.2}, 0.3)));
    return world;
}

CameraConfig small_config() {
    CameraConfig config;
    config.image_width = 61;
    config.aspect_ratio = 61.0 / 37.0;
    config.max_depth = 10;
    return config;
}

// Big enough that one pass takes a while, so cancelling mid-pass is actually tested.
CameraConfig big_config() {
    CameraConfig config;
    config.image_width = 400;
    config.aspect_ratio = 1.0;
    config.max_depth = 50;
    return config;
}

RenderSettings four_threads() {
    RenderSettings settings;
    settings.threads = 4;
    settings.tile_size = 8;
    return settings;
}

}  // namespace

TEST_CASE("reaches its target, stops, and matches a single-threaded run") {
    const HittableList world = three_spheres();

    ProgressiveRenderer a(world, kSky, four_threads());
    a.start(small_config(), 12);
    const ProgressiveRenderer::Status s = wait_done(a);
    CHECK(s.done);
    CHECK(s.passes == 12);
    CHECK(s.target == 12);
    CHECK(s.rays >= 61ull * 37ull * 12ull);  // at least one camera ray per pixel per pass
    CHECK(a.width() == 61);
    CHECK(a.height() == 37);

    RenderSettings one_thread = four_threads();
    one_thread.threads = 1;
    ProgressiveRenderer b(world, kSky, one_thread);
    b.start(small_config(), 12);
    wait_done(b);
    CHECK(same_pixels(a.image(), b.image()));
}

TEST_CASE("frames") {
    const HittableList world = three_spheres();
    ProgressiveRenderer r(world, kSky, four_threads());
    r.start(small_config(), 4);
    wait_done(r);

    std::vector<std::uint8_t> rgba;
    std::uint64_t seen = 0;
    REQUIRE(r.frame_rgba(rgba, seen));
    CHECK(rgba.size() == 61u * 37u * 4u);
    CHECK(rgba[3] == 255);                  // opaque
    CHECK_FALSE(r.frame_rgba(rgba, seen));  // nothing new once finished

    // The frame is the running average, encoded exactly like write_png().
    const Image avg = r.image();
    CHECK(rgba[0] == to_display_byte(avg.get(0, 0).x));

    bool finite = true;
    for (int y = 0; y < avg.height(); ++y) {
        for (int x = 0; x < avg.width(); ++x) {
            const Color& c = avg.get(x, y);
            finite = finite && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
        }
    }
    CHECK(finite);
}

TEST_CASE("restart and stop cancel the pass in flight quickly") {
    const HittableList world = three_spheres();
    ProgressiveRenderer r(world, kSky, four_threads());

    r.start(big_config(), 0);  // unlimited: only stop() ends it
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto t0 = std::chrono::steady_clock::now();
    r.start(small_config(), 3);  // new camera, mid-pass
    CHECK(ms_since(t0) < 500.0);
    CHECK(r.width() == 61);
    CHECK(r.status().passes <= 3);
    const ProgressiveRenderer::Status s = wait_done(r);
    CHECK(s.done);
    CHECK(s.passes == 3);

    r.start(big_config(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto t1 = std::chrono::steady_clock::now();
    r.stop();
    r.stop();  // twice is fine
    CHECK(ms_since(t1) < 500.0);
    CHECK_FALSE(r.status().done);  // a stopped unlimited render isn't "done"
}

TEST_CASE("a pre-cancelled render() traces nothing and leaves pixels untouched") {
    const HittableList world = three_spheres();
    std::atomic<bool> cancel{true};
    RenderSettings settings = four_threads();
    settings.cancel = &cancel;
    settings.show_progress = true;  // the progress loop must not hang either

    const Camera camera(small_config());
    Image untouched(camera.image_width(), camera.image_height());
    const RenderStats stats = render(camera, world, kSky, untouched, settings);
    CHECK(stats.rays == 0);
    CHECK(untouched.get(30, 18).length() == 0.0);
}

TEST_CASE("the destructor stops a running render") {
    const HittableList world = three_spheres();
    {
        ProgressiveRenderer r(world, kSky, four_threads());
        r.start(big_config(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }  // must join its thread here: no crash, no hang
    CHECK(true);
}
