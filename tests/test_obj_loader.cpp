#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "kiln/interval.hpp"
#include "kiln/obj_loader.hpp"
#include "kiln/ray.hpp"

using namespace kiln;

namespace {

int failures = 0;

void check(bool ok, const char* name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!ok) ++failures;
}

bool near(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps;
}

const std::string kData = KILN_TEST_DATA_DIR;

}  // namespace

int main() {
    const Interval all(0.001, kInfinity);
    HitRecord rec;

    // ---------- File with vertex normals ----------
    const auto quad = load_obj(kData + "/quad_with_normals.obj", nullptr);
    check(quad.size() == 2, "quad face is triangulated into 2 triangles");

    // Face normal is (0,0,1); the file says (0,0.6,0.8). Smooth shading must use the file's.
    const Ray down(Point3{0.3, 0.3, 1.0}, Vec3{0, 0, -1});
    bool hit = false;
    for (const auto& tri : quad) hit = hit || tri->hit(down, all, rec);
    check(hit, "ray hits the quad");
    // tinyobj stores floats, so compare at float precision.
    check(hit && near(rec.normal, Vec3{0, 0.6, 0.8}, 1e-6),
          "hit uses vertex normal (smooth shading)");

    // ---------- Flat file, degenerate face, transform ----------
    const auto flat = load_obj(kData + "/flat_with_degenerate.obj", nullptr);
    check(flat.size() == 1, "zero-area face is skipped");

    // scale 2, rotate 90 about Y, move +10 in x: the z = 0 triangle now lies in the x = 10 plane.
    Transform t;
    t.scale = 2.0;
    t.rotate_y_degrees = 90.0;
    t.translate = Vec3{10.0, 0.0, 0.0};
    check(near(t.apply_point(Point3{1, 0, 0}), Point3{10, 0, -2}), "transform order: scale, rotate, move");

    const auto moved = load_obj(kData + "/flat_with_degenerate.obj", nullptr, t);
    const Ray from_side(Point3{20.0, 0.5, -0.5}, Vec3{-1, 0, 0});
    hit = !moved.empty() && moved[0]->hit(from_side, all, rec);
    check(hit, "transformed triangle is hit where expected");
    check(hit && std::fabs(rec.t - 10.0) < 1e-9, "transformed hit distance t = 10");
    check(hit && rec.front_face && near(rec.normal, Vec3{1, 0, 0}), "face normal rotated with mesh");

    // ---------- Generated normals ----------
    ObjLoadOptions gen;
    gen.generate_normals_if_missing = true;
    const auto smooth = load_obj(kData + "/flat_with_degenerate.obj", nullptr, {}, gen);
    hit = !smooth.empty() && smooth[0]->hit(down, all, rec);
    check(hit && near(rec.normal, Vec3{0, 0, 1}), "generated normals are unit and outward");

    // ---------- Errors ----------
    bool threw = false;
    try {
        load_obj(kData + "/does_not_exist.obj", nullptr);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "missing file throws");

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests FAILED\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}