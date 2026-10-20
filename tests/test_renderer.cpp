#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/lights.hpp"
#include "kiln/material.hpp"
#include "kiln/quad.hpp"
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

// Average of every pixel's red channel, and root-mean-square difference from a reference.
double mean_red(const Image& img) {
    double sum = 0.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) sum += img.get(x, y).x;
    }
    return sum / (img.width() * img.height());
}

// Values are clamped to 1 (display white) first: a few fireflies far brighter than white
// would otherwise dominate the score, and they vary with the standard library's RNG.
double rms_error(const Image& img, const Image& reference) {
    double sum = 0.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const double d =
                std::fmin(img.get(x, y).x, 1.0) - std::fmin(reference.get(x, y).x, 1.0);
            sum += d * d;
        }
    }
    return std::sqrt(sum / (img.width() * img.height()));
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

    // Ray counting: at least one camera ray per sample, identical whatever the thread count.
    {
        Image a(camera.image_width(), camera.image_height());
        Image b(camera.image_width(), camera.image_height());
        RenderSettings s1;
        s1.threads = 1;
        s1.show_progress = false;
        RenderSettings s4 = s1;
        s4.threads = 4;
        const RenderStats one_stats = render(camera, world, Color{0.7, 0.8, 1.0}, a, s1);
        const RenderStats four_stats = render(camera, world, Color{0.7, 0.8, 1.0}, b, s4);
        const std::uint64_t camera_rays = 61ull * 37ull * 8ull;
        check(one_stats.rays >= camera_rays, "ray count includes every camera ray");
        check(one_stats.rays == four_stats.rays, "ray count is the same on 1 and 4 threads");
    }

    // ---------- Denoiser guides (AOVs) ----------
    {
        Image beauty(camera.image_width(), camera.image_height());
        Image albedo(camera.image_width(), camera.image_height());
        Image normal(camera.image_width(), camera.image_height());
        RenderSettings s;
        s.threads = 4;
        s.tile_size = 8;
        s.show_progress = false;
        render(camera, world, Color{0.7, 0.8, 1.0}, beauty, s, {&albedo, &normal});
        check(same_pixels(beauty, four), "filling AOVs leaves the beauty pass unchanged");

        // Top-left corner looks at open sky: albedo is the background, normal is zero.
        const Color& sky = albedo.get(0, 0);
        const Color& sky_n = normal.get(0, 0);
        check(std::fabs(sky.x - 0.7) < 1e-12 && std::fabs(sky.y - 0.8) < 1e-12 &&
                  std::fabs(sky.z - 1.0) < 1e-12,
              "sky albedo is the background");
        check(sky_n.x == 0.0 && sky_n.y == 0.0 && sky_n.z == 0.0, "sky normal is zero");
    }
    {
        // Camera inside a big matte sphere: every pixel sees the same albedo.
        HittableList room;
        room.add(std::make_shared<Sphere>(Point3{0, 0, 0}, 10.0,
                                          std::make_shared<Lambertian>(Color{0.5, 0.25, 0.75})));
        Image beauty(camera.image_width(), camera.image_height());
        Image albedo(camera.image_width(), camera.image_height());
        Image normal(camera.image_width(), camera.image_height());
        RenderSettings s;
        s.show_progress = false;
        render(camera, room, Color{0, 0, 0}, beauty, s, {&albedo, &normal});
        const Color& a = albedo.get(30, 18);
        const Vec3& n = normal.get(30, 18);
        check(std::fabs(a.x - 0.5) < 1e-12 && std::fabs(a.y - 0.25) < 1e-12 &&
                  std::fabs(a.z - 0.75) < 1e-12,
              "matte albedo is the material colour");
        // Looking down -z from the centre: the wall ahead faces the camera, +z.
        check(n.z > 0.99, "normal faces the camera");

        // Glass in front of the sky: the guide sees through it to the sky, not white glass.
        HittableList lens;
        lens.add(
            std::make_shared<Sphere>(Point3{0, 0, -3}, 2.0, std::make_shared<Dielectric>(1.5)));
        render(camera, lens, Color{0.2, 0.4, 0.6}, beauty, s, {&albedo, nullptr});
        const Color& g = albedo.get(30, 18);
        check(std::fabs(g.x - 0.2) < 1e-9 && std::fabs(g.y - 0.4) < 1e-9 &&
                  std::fabs(g.z - 0.6) < 1e-9,
              "glass albedo shows what's behind it");

        Image wrong(5, 5);
        bool threw = false;
        try {
            render(camera, room, Color{0, 0, 0}, beauty, s, {&wrong, nullptr});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "AOV of the wrong size throws");
    }

    // ---------- Next event estimation ----------
    {
        // A matte room lit only by a small ceiling panel, plus a glass ball so light also
        // arrives through specular bounces (the path NEE must not double count or drop).
        auto white = std::make_shared<Lambertian>(Color{0.7, 0.7, 0.7});
        auto lamp_mat = std::make_shared<DiffuseLight>(Color{12, 12, 12});
        HittableList room;
        room.add(std::make_shared<Quad>(Point3{-2, 0, -4}, Vec3{4, 0, 0}, Vec3{0, 0, 4}, white));
        room.add(std::make_shared<Quad>(Point3{-2, 0, -4}, Vec3{0, 3, 0}, Vec3{4, 0, 0}, white));
        room.add(std::make_shared<Quad>(Point3{-2, 3, -4}, Vec3{4, 0, 0}, Vec3{0, 0, 4}, white));
        room.add(
            std::make_shared<Sphere>(Point3{0.6, 0.5, -2}, 0.5, std::make_shared<Dielectric>(1.5)));
        auto lamp = std::make_shared<Quad>(Point3{-0.5, 2.99, -2.5}, Vec3{1, 0, 0}, Vec3{0, 0, 1},
                                           lamp_mat);
        room.add(lamp);
        Lights lights;
        lights.add(lamp);

        CameraConfig cfg;
        cfg.image_width = 24;
        cfg.aspect_ratio = 1.0;
        cfg.look_from = {0, 1.5, 1};
        cfg.look_at = {0, 1.2, -2};
        cfg.vfov = 70;
        cfg.max_depth = 8;

        auto render_room = [&](int spp, bool nee, std::uint32_t seed) {
            CameraConfig c = cfg;
            c.samples_per_pixel = spp;
            const Camera cam(c);
            Image img(cam.image_width(), cam.image_height());
            RenderSettings s;
            s.show_progress = false;
            s.seed = seed;
            s.lights = nee ? &lights : nullptr;
            render(cam, room, Color{0, 0, 0}, img, s);
            return img;
        };

        // Same expected image either way: the averages of two long renders must agree.
        const Image ref_nee = render_room(2048, true, 1);
        const Image ref_plain = render_room(2048, false, 2);
        const double m_nee = mean_red(ref_nee);
        const double m_plain = mean_red(ref_plain);
        std::cout << "       mean brightness: NEE " << m_nee << ", plain " << m_plain << '\n';
        check(std::fabs(m_nee - m_plain) < 0.02 * m_plain,
              "NEE is unbiased: same average brightness as plain path tracing (within 2%)");

        // At a low sample count, NEE is much closer to the converged image.
        const double err_nee = rms_error(render_room(16, true, 3), ref_nee);
        const double err_plain = rms_error(render_room(16, false, 4), ref_nee);
        std::cout << "       16 spp RMS error: NEE " << err_nee << ", plain " << err_plain << '\n';
        check(err_nee < 0.7 * err_plain, "NEE at 16 spp has at least 30% less noise than plain");

        // Still deterministic: shadow rays draw from the same per-tile RNG.
        RenderSettings s1;
        s1.show_progress = false;
        s1.lights = &lights;
        s1.threads = 1;
        RenderSettings s4 = s1;
        s4.threads = 4;
        CameraConfig c = cfg;
        c.samples_per_pixel = 8;
        const Camera cam(c);
        Image a(cam.image_width(), cam.image_height());
        Image b(cam.image_width(), cam.image_height());
        render(cam, room, Color{0, 0, 0}, a, s1);
        render(cam, room, Color{0, 0, 0}, b, s4);
        check(same_pixels(a, b), "NEE: 1 thread == 4 threads (pixel-identical)");

        // The denoiser guides still leave the beauty pass untouched with NEE on.
        Image beauty(cam.image_width(), cam.image_height());
        Image albedo(cam.image_width(), cam.image_height());
        render(cam, room, Color{0, 0, 0}, beauty, s4, {&albedo, nullptr});
        check(same_pixels(beauty, b), "NEE: filling AOVs leaves the beauty pass unchanged");
    }

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
