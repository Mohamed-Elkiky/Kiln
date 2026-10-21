#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include "doctest/doctest.h"
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

// Average of every pixel's red channel.
double mean_red(const Image& img) {
    double sum = 0.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) sum += img.get(x, y).x;
    }
    return sum / (img.width() * img.height());
}

// Root-mean-square difference from a reference. Values are clamped to 1 (display white) first:
// a few fireflies far brighter than white would otherwise dominate the score.
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

// Small scene with all three scattering materials, so every RNG code path is exercised.
HittableList three_spheres() {
    HittableList world;
    world.add(std::make_shared<Sphere>(Point3{0, -100.5, -1}, 100.0,
                                       std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5})));
    world.add(std::make_shared<Sphere>(Point3{-1, 0, -1}, 0.5, std::make_shared<Dielectric>(1.5)));
    world.add(std::make_shared<Sphere>(Point3{1, 0, -1}, 0.5,
                                       std::make_shared<Metal>(Color{0.8, 0.6, 0.2}, 0.3)));
    return world;
}

// Odd size (61 x 37) so the right and bottom tiles are partial.
Camera small_camera() {
    CameraConfig config;
    config.image_width = 61;
    config.aspect_ratio = 61.0 / 37.0;
    config.samples_per_pixel = 8;
    config.max_depth = 10;
    return Camera(config);
}

RenderSettings quiet(int threads, int tile_size = 8) {
    RenderSettings settings;
    settings.threads = threads;
    settings.tile_size = tile_size;
    settings.show_progress = false;
    return settings;
}

Image render_with(const Camera& camera, const Hittable& world, const RenderSettings& settings) {
    Image image(camera.image_width(), camera.image_height());
    render(camera, world, kSky, image, settings);
    return image;
}

}  // namespace

TEST_CASE("output is pixel-identical whatever the thread count") {
    const HittableList world = three_spheres();
    const Camera camera = small_camera();
    const Image one = render_with(camera, world, quiet(1));

    CHECK(one.width() == 61);
    CHECK(one.height() == 37);
    CHECK(same_pixels(one, render_with(camera, world, quiet(4))));
    CHECK(same_pixels(one, render_with(camera, world, quiet(16))));  // more threads than cores
    CHECK(same_pixels(one, render_with(camera, world, quiet(1))));   // and repeatable
}

TEST_CASE("every pixel is rendered, edge tiles included") {
    const HittableList world = three_spheres();
    const Camera camera = small_camera();

    for (const int tile : {8, 64}) {  // 64: one tile bigger than the whole image
        CAPTURE(tile);
        const Image img = render_with(camera, world, quiet(4, tile));
        bool all_written = true;  // nothing in this scene renders pure black
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const Color& c = img.get(x, y);
                if (c.x == 0.0 && c.y == 0.0 && c.z == 0.0) all_written = false;
            }
        }
        CHECK(all_written);
    }
}

TEST_CASE("ray count includes every camera ray and doesn't depend on threads") {
    const HittableList world = three_spheres();
    const Camera camera = small_camera();
    Image a(camera.image_width(), camera.image_height());
    Image b(camera.image_width(), camera.image_height());
    const RenderStats one = render(camera, world, kSky, a, quiet(1));
    const RenderStats four = render(camera, world, kSky, b, quiet(4));
    CHECK(one.rays >= 61ull * 37ull * 8ull);
    CHECK(one.rays == four.rays);
}

TEST_CASE("denoiser guides (AOVs)") {
    const Camera camera = small_camera();
    const int w = camera.image_width();
    const int h = camera.image_height();
    Image beauty(w, h);
    Image albedo(w, h);
    Image normal(w, h);

    SUBCASE("filling them leaves the beauty pass unchanged") {
        const HittableList world = three_spheres();
        render(camera, world, kSky, beauty, quiet(4), {&albedo, &normal});
        CHECK(same_pixels(beauty, render_with(camera, world, quiet(4))));

        // Top-left corner looks at open sky: albedo is the background, normal is zero.
        CHECK(albedo.get(0, 0).x == doctest::Approx(0.7));
        CHECK(albedo.get(0, 0).y == doctest::Approx(0.8));
        CHECK(albedo.get(0, 0).z == doctest::Approx(1.0));
        CHECK(normal.get(0, 0).length() == 0.0);
    }
    SUBCASE("matte surfaces give their colour and a normal facing the camera") {
        // Camera inside a big matte sphere: every pixel sees the same albedo.
        HittableList room;
        room.add(std::make_shared<Sphere>(Point3{0, 0, 0}, 10.0,
                                          std::make_shared<Lambertian>(Color{0.5, 0.25, 0.75})));
        render(camera, room, Color{0, 0, 0}, beauty, quiet(0), {&albedo, &normal});
        CHECK(albedo.get(30, 18).x == doctest::Approx(0.5));
        CHECK(albedo.get(30, 18).y == doctest::Approx(0.25));
        CHECK(albedo.get(30, 18).z == doctest::Approx(0.75));
        CHECK(normal.get(30, 18).z > 0.99);  // the wall ahead faces +z, towards the camera
    }
    SUBCASE("glass shows what's behind it") {
        HittableList lens;
        lens.add(
            std::make_shared<Sphere>(Point3{0, 0, -3}, 2.0, std::make_shared<Dielectric>(1.5)));
        render(camera, lens, Color{0.2, 0.4, 0.6}, beauty, quiet(0), {&albedo, nullptr});
        CHECK(albedo.get(30, 18).x == doctest::Approx(0.2));
        CHECK(albedo.get(30, 18).y == doctest::Approx(0.4));
        CHECK(albedo.get(30, 18).z == doctest::Approx(0.6));
    }
    SUBCASE("an AOV of the wrong size throws") {
        Image wrong(5, 5);
        CHECK_THROWS_AS(render(camera, three_spheres(), kSky, beauty, quiet(0), {&wrong, nullptr}),
                        std::invalid_argument);
    }
}

TEST_CASE("next event estimation") {
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
    auto lamp =
        std::make_shared<Quad>(Point3{-0.5, 2.99, -2.5}, Vec3{1, 0, 0}, Vec3{0, 0, 1}, lamp_mat);
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

    auto render_room = [&](int spp, bool nee, std::uint32_t seed, int threads = 0) {
        CameraConfig c = cfg;
        c.samples_per_pixel = spp;
        const Camera cam(c);
        Image img(cam.image_width(), cam.image_height());
        RenderSettings s = quiet(threads);
        s.seed = seed;
        s.lights = nee ? &lights : nullptr;
        render(cam, room, Color{0, 0, 0}, img, s);
        return img;
    };

    // doctest reruns the test case once per SUBCASE, so the slow reference is only rendered in
    // the two that need it.
    SUBCASE("unbiased: same average brightness as plain path tracing (within 2%)") {
        const double m_nee = mean_red(render_room(2048, true, 1));
        const double m_plain = mean_red(render_room(2048, false, 2));
        MESSAGE("mean brightness: NEE " << m_nee << ", plain " << m_plain);
        CHECK(m_nee == doctest::Approx(m_plain).epsilon(0.02));
    }
    SUBCASE("at 16 spp, at least 30% less noise than plain path tracing") {
        const Image ref_nee = render_room(2048, true, 1);
        const double err_nee = rms_error(render_room(16, true, 3), ref_nee);
        const double err_plain = rms_error(render_room(16, false, 4), ref_nee);
        MESSAGE("16 spp RMS error: NEE " << err_nee << ", plain " << err_plain);
        CHECK(err_nee < 0.7 * err_plain);
    }
    SUBCASE("still deterministic: 1 thread == 4 threads") {
        CHECK(same_pixels(render_room(8, true, 5, 1), render_room(8, true, 5, 4)));
    }
    SUBCASE("filling AOVs leaves the beauty pass unchanged") {
        CameraConfig c = cfg;
        c.samples_per_pixel = 8;
        const Camera cam(c);
        Image beauty(cam.image_width(), cam.image_height());
        Image albedo(cam.image_width(), cam.image_height());
        RenderSettings s = quiet(4);
        s.seed = 5;
        s.lights = &lights;
        render(cam, room, Color{0, 0, 0}, beauty, s, {&albedo, nullptr});
        CHECK(same_pixels(beauty, render_room(8, true, 5, 4)));
    }
}
