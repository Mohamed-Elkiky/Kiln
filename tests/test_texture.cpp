#include <cmath>
#include <memory>
#include <vector>

#include "doctest/doctest.h"
#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/material.hpp"
#include "kiln/renderer.hpp"
#include "kiln/sphere.hpp"
#include "kiln/texture.hpp"
#include "kiln/triangle.hpp"

using namespace kiln;

namespace {

HitRecord hit_at(const Point3& p, const Vec3& outward_normal = {0, 1, 0}) {
    HitRecord rec;
    rec.p = p;
    rec.normal = outward_normal;
    rec.front_face = true;
    return rec;
}

bool in_unit_range(const Color& c) {
    return c.x >= 0.0 && c.x <= 1.0 && c.y >= 0.0 && c.y <= 1.0 && c.z >= 0.0 && c.z <= 1.0;
}

// A spread of points, both sides of zero and far from the origin.
std::vector<Point3> sample_points() {
    std::vector<Point3> points;
    for (int i = -20; i <= 20; ++i) {
        for (int j = -20; j <= 20; ++j) {
            points.push_back({i * 0.37, j * 0.53, (i - j) * 0.29});
            points.push_back({i * 61.3, j * -47.1, i * j * 0.7});
        }
    }
    return points;
}

std::vector<std::shared_ptr<Texture>> every_texture() {
    const Color black{0, 0, 0};
    const Color white{1, 1, 1};
    return {
        std::make_shared<CheckerTexture>(black, white, 0.5),
        std::make_shared<NoiseTexture>(black, white, 4.0, 7),
        std::make_shared<WoodTexture>(Color{0.7, 0.5, 0.3}, Color{0.3, 0.2, 0.1}, Vec3{1, 0, 0},
                                      Point3{0, 0, 0}, 10.0, 2.0, 1),
        std::make_shared<NormalTexture>(),
        std::make_shared<GradientTexture>(std::vector<Color>{black, Color{1, 0, 0}, white},
                                          Vec3{0, 1, 0}, -1.0, 1.0),
        std::make_shared<WireframeTexture>(black, white, 0.05),
    };
}

Image render_sphere(const std::shared_ptr<Material>& mat) {
    HittableList world;
    world.add(std::make_shared<Sphere>(Point3{0, -100.5, -1}, 100.0,
                                       std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5})));
    world.add(std::make_shared<Sphere>(Point3{0, 0, -1}, 0.5, mat));

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

// Returns one colour everywhere, but through the texture code path.
class Solid final : public Texture {
public:
    explicit Solid(const Color& c) : c_(c) {}
    Color value(const HitRecord& /*rec*/) const override { return c_; }

private:
    Color c_;
};

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

TEST_CASE("textures are deterministic") {
    const auto first = every_texture();
    const auto second = every_texture();  // built again from scratch
    for (std::size_t t = 0; t < first.size(); ++t) {
        CAPTURE(t);
        for (const Point3& p : sample_points()) {
            const Color a = first[t]->value(hit_at(p));
            const Color b = second[t]->value(hit_at(p));
            const Color again = first[t]->value(hit_at(p));
            CHECK((a.x == b.x && a.y == b.y && a.z == b.z));
            CHECK((a.x == again.x && a.y == again.y && a.z == again.z));
        }
    }
}

TEST_CASE("textures stay in the colour range of their inputs") {
    for (const auto& texture : every_texture()) {
        for (const Point3& p : sample_points()) {
            CHECK(in_unit_range(texture->value(hit_at(p, unit_vector(Vec3{p.x, p.y, 1.0})))));
        }
    }
}

TEST_CASE("noise varies over the surface, and the seed changes the pattern") {
    const NoiseTexture noise(Color{0, 0, 0}, Color{1, 1, 1}, 4.0, 0);
    const NoiseTexture other(Color{0, 0, 0}, Color{1, 1, 1}, 4.0, 1);
    double lo = 1.0, hi = 0.0;
    bool differs = false;
    for (const Point3& p : sample_points()) {
        const double v = noise.value(hit_at(p)).x;
        lo = std::fmin(lo, v);
        hi = std::fmax(hi, v);
        differs = differs || v != other.value(hit_at(p)).x;
    }
    CHECK(hi - lo > 0.3);
    CHECK(differs);
}

TEST_CASE("checker, normal and gradient values") {
    const CheckerTexture checker(Color{0, 0, 0}, Color{1, 1, 1}, 1.0);
    CHECK(checker.value(hit_at({0.5, 0.5, 0.5})).x == 0.0);
    CHECK(checker.value(hit_at({1.5, 0.5, 0.5})).x == 1.0);
    CHECK(checker.value(hit_at({-0.5, 0.5, 0.5})).x == 1.0);  // no seam at zero

    // Outward normal, whichever side was hit.
    const NormalTexture normal;
    HitRecord up = hit_at({0, 0, 0}, {0, 1, 0});
    CHECK(normal.value(up).y == doctest::Approx(1.0));
    up.front_face = false;
    up.normal = {0, -1, 0};
    CHECK(normal.value(up).y == doctest::Approx(1.0));

    const GradientTexture gradient({Color{0, 0, 0}, Color{1, 0, 0}, Color{1, 1, 1}}, {0, 1, 0}, 0.0,
                                   2.0);
    CHECK(gradient.value(hit_at({0, -5, 0})).x == 0.0);  // clamped below
    CHECK(gradient.value(hit_at({0, 1, 0})).x == doctest::Approx(1.0));
    CHECK(gradient.value(hit_at({0, 1, 0})).y == doctest::Approx(0.0));
    CHECK(gradient.value(hit_at({0, 1.5, 0})).y == doctest::Approx(0.5));
    CHECK(gradient.value(hit_at({0, 9, 0})).z == 1.0);  // clamped above
}

TEST_CASE("a flat-colour texture renders exactly like a flat colour") {
    const Color c{0.2, 0.6, 0.3};
    const auto solid = std::make_shared<Solid>(c);
    CHECK(same_pixels(render_sphere(std::make_shared<Lambertian>(c)),
                      render_sphere(std::make_shared<Lambertian>(solid))));
    CHECK(same_pixels(render_sphere(std::make_shared<Metal>(c, 0.2)),
                      render_sphere(std::make_shared<Metal>(solid, 0.2))));
}

TEST_CASE("wireframe draws triangle edges") {
    const Color base{0.9, 0.9, 0.9};
    const Color line{0.1, 0.1, 0.1};
    const WireframeTexture wire(base, line, 0.05);
    const Triangle tri(Point3{0, 0, 0}, Point3{1, 0, 0}, Point3{0, 1, 0}, nullptr);

    // Fire a ray straight down the z axis at (x, y) and shade the real hit record.
    const auto shade = [&](double x, double y) {
        HitRecord rec;
        REQUIRE(tri.hit(Ray(Point3{x, y, 1}, Vec3{0, 0, -1}), Interval(0.001, 10.0), rec));
        CHECK(rec.on_triangle);
        CHECK(rec.barycentric.x + rec.barycentric.y + rec.barycentric.z == doctest::Approx(1.0));
        return wire.value(rec);
    };

    CHECK(shade(1.0 / 3.0, 1.0 / 3.0).x == base.x);  // centre
    CHECK(shade(0.5, 0.01).x == line.x);             // next to the bottom edge
    CHECK(shade(0.01, 0.5).x == line.x);             // next to the left edge
    CHECK(shade(0.49, 0.49).x == line.x);            // next to the long edge
    CHECK(shade(0.2, 0.2).x == base.x);              // inside, clear of every edge

    // Not a triangle: just the base colour.
    HitRecord sphere_hit;
    sphere_hit.p = Point3{0, 0, 0};
    CHECK(wire.value(sphere_hit).x == base.x);
}