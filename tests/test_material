#include <memory>

#include "doctest/doctest.h"
#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/material.hpp"
#include "kiln/random.hpp"
#include "kiln/renderer.hpp"
#include "kiln/sphere.hpp"

using namespace kiln;

namespace {

// A ray inside the glass that has travelled `distance` and is now leaving through the surface.
Color leaving_glass(const Dielectric& glass, double distance) {
    const Ray r_in(Point3{0, 0, 0}, Vec3{0, 0, -2});  // not unit length, on purpose
    HitRecord rec;
    rec.t = distance / 2.0;
    rec.p = r_in.at(rec.t);
    rec.set_face_normal(r_in, Vec3{0, 0, -1});  // outward normal along the ray: hit from inside
    REQUIRE_FALSE(rec.front_face);

    seed_rng(1);
    Color attenuation;
    Ray scattered;
    REQUIRE(glass.scatter(r_in, rec, attenuation, scattered));
    return attenuation;
}

Image render_glass_sphere(const std::shared_ptr<Material>& glass) {
    HittableList world;
    world.add(std::make_shared<Sphere>(Point3{0, -100.5, -1}, 100.0,
                                       std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5})));
    world.add(std::make_shared<Sphere>(Point3{0, 0, -1}, 0.5, glass));

    CameraConfig config;
    config.image_width = 48;
    config.aspect_ratio = 1.5;
    config.samples_per_pixel = 4;
    config.max_depth = 8;
    const Camera camera(config);

    RenderSettings settings;
    settings.threads = 2;
    settings.show_progress = false;
    Image image(camera.image_width(), camera.image_height());
    render(camera, world, Color{0.7, 0.8, 1.0}, image, settings);
    return image;
}

bool same_pixels(const Image& a, const Image& b) {
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Color& p = a.get(x, y);
            const Color& q = b.get(x, y);
            if (p.x != q.x || p.y != q.y || p.z != q.z) return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("glass with no absorption absorbs nothing") {
    const Dielectric clear(1.5);
    const Color a = leaving_glass(clear, 10.0);
    CHECK(a.x == 1.0);
    CHECK(a.y == 1.0);
    CHECK(a.z == 1.0);

    // A white absorption colour is the same as none, render for render.
    CHECK(same_pixels(
        render_glass_sphere(std::make_shared<Dielectric>(1.5)),
        render_glass_sphere(std::make_shared<Dielectric>(1.5, Color{1.0, 1.0, 1.0}, 3.0))));
}

TEST_CASE("a longer path through coloured glass is darker") {
    const Dielectric amber(1.5, Color{0.9, 0.5, 0.2}, 1.0);
    const Color thin = leaving_glass(amber, 0.2);
    const Color thick = leaving_glass(amber, 2.0);
    CHECK(thick.x < thin.x);
    CHECK(thick.y < thin.y);
    CHECK(thick.z < thin.z);
    CHECK(thick.z < thick.y);  // blue is absorbed fastest, so the tint deepens towards amber
    CHECK(thick.y < thick.x);

    // Beer's law: after 1 / density units, exactly the absorption colour is left.
    const Dielectric dense(1.5, Color{0.9, 0.5, 0.2}, 4.0);
    const Color quarter = leaving_glass(dense, 0.25);
    CHECK(quarter.x == doctest::Approx(0.9));
    CHECK(quarter.y == doctest::Approx(0.5));
    CHECK(quarter.z == doctest::Approx(0.2));
}

TEST_CASE("light entering coloured glass is not tinted yet") {
    const Dielectric amber(1.5, Color{0.9, 0.5, 0.2}, 1.0);
    const Ray r_in(Point3{0, 0, 0}, Vec3{0, 0, -1});
    HitRecord rec;
    rec.t = 5.0;
    rec.p = r_in.at(rec.t);
    rec.set_face_normal(r_in, Vec3{0, 0, 1});  // hit from outside
    REQUIRE(rec.front_face);

    Color attenuation;
    Ray scattered;
    REQUIRE(amber.scatter(r_in, rec, attenuation, scattered));
    CHECK(attenuation.x == 1.0);
    CHECK(attenuation.z == 1.0);
}