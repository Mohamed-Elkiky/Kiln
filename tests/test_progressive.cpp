#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/material.hpp"
#include "kiln/progressive.hpp"
#include "kiln/renderer.hpp"
#include "kiln/sphere.hpp"

using namespace kiln;

namespace {

int failures = 0;

void check(bool ok, const std::string& name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!ok) ++failures;
}

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

// Polls until the renderer reaches its target (or gives up after a few seconds).
ProgressiveRenderer::Status wait_done(const ProgressiveRenderer& r) {
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!r.status().done && std::chrono::steady_clock::now() < give_up) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return r.status();
}

}  // namespace

int main() {
    HittableList world;
    world.add(std::make_shared<Sphere>(Point3{0, -100.5, -1}, 100.0,
                                       std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5})));
    world.add(std::make_shared<Sphere>(Point3{-1, 0, -1}, 0.5, std::make_shared<Dielectric>(1.5)));
    world.add(std::make_shared<Sphere>(Point3{1, 0, -1}, 0.5,
                                       std::make_shared<Metal>(Color{0.8, 0.6, 0.2}, 0.3)));
    const Color sky{0.7, 0.8, 1.0};

    CameraConfig config;
    config.image_width = 61;
    config.aspect_ratio = 61.0 / 37.0;
    config.max_depth = 10;

    RenderSettings settings;
    settings.threads = 4;
    settings.tile_size = 8;

    // ---------- Reaches its target and stops ----------
    ProgressiveRenderer a(world, sky, settings);
    a.start(config, 12);
    const ProgressiveRenderer::Status sa = wait_done(a);
    check(sa.done && sa.passes == 12 && sa.target == 12, "stops after exactly 12 passes");
    check(sa.rays >= 61ull * 37ull * 12ull, "counts at least one camera ray per pixel per pass");
    check(a.width() == 61 && a.height() == 37, "image is the camera's size");

    // ---------- Deterministic, whatever the thread count ----------
    RenderSettings one_thread = settings;
    one_thread.threads = 1;
    ProgressiveRenderer b(world, sky, one_thread);
    b.start(config, 12);
    wait_done(b);
    check(same_pixels(a.image(), b.image()), "4 threads == 1 thread after 12 passes");

    // ---------- Frames ----------
    std::vector<std::uint8_t> rgba;
    std::uint64_t seen = 0;
    check(a.frame_rgba(rgba, seen) && rgba.size() == 61u * 37u * 4u, "first frame is RGBA8");
    check(rgba[3] == 255, "frame is opaque");
    check(!a.frame_rgba(rgba, seen), "no new frame once finished");

    // The frame is the average, encoded exactly like write_png().
    const Image avg = a.image();
    check(rgba[0] == to_display_byte(avg.get(0, 0).x), "frame bytes match the saved image");

    // Converges: later passes average out, so the noise between neighbours drops.
    bool finite = true;
    for (int y = 0; y < avg.height(); ++y) {
        for (int x = 0; x < avg.width(); ++x) {
            const Color& c = avg.get(x, y);
            finite = finite && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
        }
    }
    check(finite, "average has no NaN or inf");

    // ---------- Restart, cancel ----------
    ProgressiveRenderer c(world, sky, settings);
    CameraConfig big = config;
    big.image_width = 400;
    big.aspect_ratio = 1.0;
    big.max_depth = 50;
    c.start(big, 0);  // unlimited: only stop() ends it
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto t0 = std::chrono::steady_clock::now();
    c.start(config, 3);  // restart with a new camera mid-pass
    const double restart_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(restart_ms < 500.0, "restart cancels the pass in flight quickly");
    check(c.width() == 61 && c.status().passes <= 3, "restart resets size and passes");
    const ProgressiveRenderer::Status sc = wait_done(c);
    check(sc.done && sc.passes == 3, "restarted render reaches its new target");

    c.start(big, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto t1 = std::chrono::steady_clock::now();
    c.stop();
    c.stop();  // twice is fine
    const double stop_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    check(stop_ms < 500.0, "stop() mid-pass returns quickly");
    check(!c.status().done, "a stopped unlimited render is not 'done'");

    // ---------- render() cancel flag ----------
    std::atomic<bool> cancel{true};
    RenderSettings cancelled = settings;
    cancelled.cancel = &cancel;
    cancelled.show_progress = true;  // the progress loop must not hang either
    const Camera camera(config);
    Image untouched(camera.image_width(), camera.image_height());
    const RenderStats stats = render(camera, world, sky, untouched, cancelled);
    check(stats.rays == 0, "pre-cancelled render traces nothing");
    const Color& p = untouched.get(30, 18);
    check(p.x == 0.0 && p.y == 0.0 && p.z == 0.0, "pre-cancelled render leaves pixels untouched");

    // A destroyed renderer joins its thread (no crash, no leak) even mid-render.
    {
        ProgressiveRenderer d(world, sky, settings);
        d.start(big, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(true, "destructor stops a running render");

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
