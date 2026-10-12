#include <cstdlib>
#include <iostream>
#include <memory>

#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/material.hpp"
#include "kiln/renderer.hpp"
#include "kiln/sphere.hpp"

using namespace kiln;

namespace {

int failures = 0;

void check(bool ok, const char* name) {
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

Image render_with(const Camera& camera, const Hittable& world, int threads, int tile_size) {
    Image image(camera.image_width(), camera.image_height());
    RenderSettings settings;
    settings.threads = threads;
    settings.tile_size = tile_size;
    settings.show_progress = false;
    render(camera, world, Color{0.7, 0.8, 1.0}, image, settings);
    return image;
}

}  // namespace

int main() {
    // Small scene with all three scattering materials, so every RNG code path is exercised.
    HittableList world;
    world.add(std::make_shared<Sphere>(Point3{0, -100.5, -1}, 100.0,
                                       std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5})));
    world.add(std::make_shared<Sphere>(Point3{-1, 0, -1}, 0.5, std::make_shared<Dielectric>(1.5)));
    world.add(std::make_shared<Sphere>(Point3{1, 0, -1}, 0.5,
                                       std::make_shared<Metal>(Color{0.8, 0.6, 0.2}, 0.3)));

    // Odd size (61 x 37) so the right and bottom tiles are partial.
    CameraConfig config;
    config.image_width = 61;
    config.aspect_ratio = 61.0 / 37.0;
    config.samples_per_pixel = 8;
    config.max_depth = 10;
    const Camera camera(config);

    const Image one = render_with(camera, world, 1, 8);
    const Image four = render_with(camera, world, 4, 8);
    const Image many = render_with(camera, world, 16, 8);
    const Image again = render_with(camera, world, 4, 8);

    check(one.height() == 37, "image is 61 x 37");
    check(same_pixels(one, four), "1 thread == 4 threads (pixel-identical)");
    check(same_pixels(one, many), "1 thread == 16 threads (more threads than cores)");
    check(same_pixels(four, again), "same settings twice == same image");

    // Every pixel must be written, including the partial edge tiles. The sky is never black.
    bool all_written = true;
    for (int y = 0; y < one.height(); ++y) {
        for (int x = 0; x < one.width(); ++x) {
            const Color& c = one.get(x, y);
            if (c.x == 0.0 && c.y == 0.0 && c.z == 0.0) all_written = false;
        }
    }
    check(all_written, "every pixel rendered, edge tiles included");

    // A different tile size changes the seeding, but the image must still be complete.
    const Image big_tiles = render_with(camera, world, 4, 64);  // one tile covers everything
    check(big_tiles.width() == 61 && big_tiles.height() == 37, "tile larger than image works");

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}