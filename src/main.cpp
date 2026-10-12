#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "kiln/bvh.hpp"
#include "kiln/camera.hpp"
#include "kiln/hittable.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/interval.hpp"
#include "kiln/material.hpp"
#include "kiln/obj_loader.hpp"
#include "kiln/quad.hpp"
#include "kiln/random.hpp"
#include "kiln/ray.hpp"
#include "kiln/sphere.hpp"
#include "kiln/triangle.hpp"
#include "kiln/vec3.hpp"
#include "kiln/version.hpp"

using namespace kiln;

Color ray_color(const Ray& r, int depth, const Hittable& world, const Color& background) {
    // Bounce limit reached: no more light gathered.
    if (depth <= 0) return Color{0.0, 0.0, 0.0};

    HitRecord rec;
    // t_min = 0.001 stops a bounced ray re-hitting the surface it just left ("shadow acne").
    if (!world.hit(r, Interval(0.001, kInfinity), rec)) return background;

    // Light the surface gives off itself (only lights are non-black).
    const Color emission = rec.mat->emitted();

    Ray scattered;
    Color attenuation;
    // Lights don't scatter: just return their glow.
    if (!rec.mat->scatter(r, rec, attenuation, scattered)) return emission;

    return emission + attenuation * ray_color(scattered, depth - 1, world, background);
}

Color ray_color(const Ray& r, int depth, const Hittable& world) {
    // Bounce limit reached: no more light gathered.
    if (depth <= 0) return Color{0.0, 0.0, 0.0};

    HitRecord rec;
    // t_min = 0.001 stops a bounced ray re-hitting the surface it just left ("shadow acne").
    if (world.hit(r, Interval(0.001, kInfinity), rec)) {
        Ray scattered;
        Color attenuation;
        // The material decides where the light goes next and how much survives.
        if (rec.mat->scatter(r, rec, attenuation, scattered)) {
            return attenuation * ray_color(scattered, depth - 1, world);
        }
        return Color{0.0, 0.0, 0.0};  // absorbed
    }

    // Sky: blend white to blue based on how far up the ray points.
    const Vec3 dir = unit_vector(r.direction());
    const double a = 0.5 * (dir.y + 1.0);
    return (1.0 - a) * Color{1.0, 1.0, 1.0} + a * Color{0.5, 0.7, 1.0};
}

// ---------- Scenes ----------

// Three spheres (glass, matte, gold) lit by a ceiling panel. Sprint 2 materials test.
void spheres_under_light(HittableList& world, CameraConfig& config, Color& background) {
    auto ground = std::make_shared<Lambertian>(Color{0.8, 0.8, 0.0});
    auto center = std::make_shared<Lambertian>(Color{0.1, 0.2, 0.5});
    auto glass = std::make_shared<Dielectric>(1.5);
    auto bubble = std::make_shared<Dielectric>(1.0 / 1.5);
    auto gold = std::make_shared<Metal>(Color{0.8, 0.6, 0.2}, 0.3);
    auto light = std::make_shared<DiffuseLight>(Color{4.0, 4.0, 4.0});

    world.add(std::make_shared<Sphere>(Point3{0.0, -100.5, -1.0}, 100.0, ground));
    world.add(std::make_shared<Sphere>(Point3{0.0, 0.0, -1.2}, 0.5, center));
    world.add(std::make_shared<Sphere>(Point3{-1.0, 0.0, -1.0}, 0.5, glass));
    world.add(std::make_shared<Sphere>(Point3{-1.0, 0.0, -1.0}, 0.4, bubble));
    world.add(std::make_shared<Sphere>(Point3{1.0, 0.0, -1.0}, 0.5, gold));
    world.add(std::make_shared<Quad>(Point3{-1.5, 1.5, -2.0}, Vec3{3.0, 0.0, 0.0},
                                     Vec3{0.0, 0.0, 2.0}, light));

    config.aspect_ratio = 16.0 / 9.0;
    config.image_width = 800;
    config.vfov = 45.0;
    config.samples_per_pixel = 200;
    config.max_depth = 50;
    config.look_from = {0, 0.5, 2};
    config.look_at = {0, 0, -1};

    background = Color{0.0, 0.0, 0.0};
}

// Classic Cornell box: red/green walls, white room, ceiling light, glass + mirror spheres.
void cornell_box(HittableList& world, CameraConfig& config, Color& background) {
    auto red = std::make_shared<Lambertian>(Color{0.65, 0.05, 0.05});
    auto white = std::make_shared<Lambertian>(Color{0.73, 0.73, 0.73});
    auto green = std::make_shared<Lambertian>(Color{0.12, 0.45, 0.15});
    auto light = std::make_shared<DiffuseLight>(Color{15.0, 15.0, 15.0});
    auto glass = std::make_shared<Dielectric>(1.5);
    auto mirror = std::make_shared<Metal>(Color{0.8, 0.85, 0.88}, 0.0);

    // Room: 555 units on each side. Camera looks down +z, so x = 555 is on the left.
    world.add(std::make_shared<Quad>(Point3{555, 0, 0}, Vec3{0, 555, 0}, Vec3{0, 0, 555}, red));    // left
    world.add(std::make_shared<Quad>(Point3{0, 0, 0}, Vec3{0, 555, 0}, Vec3{0, 0, 555}, green));    // right
    world.add(std::make_shared<Quad>(Point3{0, 0, 0}, Vec3{555, 0, 0}, Vec3{0, 0, 555}, white));    // floor
    world.add(std::make_shared<Quad>(Point3{555, 555, 555}, Vec3{-555, 0, 0}, Vec3{0, 0, -555}, white));  // ceiling
    world.add(std::make_shared<Quad>(Point3{0, 0, 555}, Vec3{555, 0, 0}, Vec3{0, 555, 0}, white));  // back

    // Light panel, just below the ceiling
    world.add(std::make_shared<Quad>(Point3{343, 554, 332}, Vec3{-130, 0, 0}, Vec3{0, 0, -105}, light));

    // Contents
    world.add(std::make_shared<Sphere>(Point3{190, 90, 190}, 90, glass));    // front right
    world.add(std::make_shared<Sphere>(Point3{370, 100, 370}, 100, mirror)); // back left

    config.aspect_ratio = 1.0;
    config.image_width = 600;
    config.vfov = 40.0;
    config.samples_per_pixel = 500;
    config.max_depth = 50;
    config.look_from = {278, 278, -800};
    config.look_at = {278, 278, 0};

    background = Color{0.0, 0.0, 0.0};
}
// A single triangle and a tetrahedron built from 4 triangles. Sprint 3 geometry test.
void triangles_scene(HittableList& world, CameraConfig& config, Color& background) {
    auto ground = std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5});
    auto red = std::make_shared<Lambertian>(Color{0.8, 0.15, 0.1});
    auto blue = std::make_shared<Lambertian>(Color{0.2, 0.35, 0.75});

    world.add(std::make_shared<Sphere>(Point3{0, -1000, 0}, 1000, ground));

    // Single triangle (left)
    world.add(std::make_shared<Triangle>(Point3{-2.2, 0, 0}, Point3{-0.4, 0, 0},
                                         Point3{-1.3, 1.5, 0}, red));

    // Tetrahedron (right): 3 base corners + apex, 4 faces
    const Point3 b0{0.5, 0, 0.6};
    const Point3 b1{1.9, 0, 0.6};
    const Point3 b2{1.2, 0, -0.7};
    const Point3 apex{1.2, 1.4, 0.1};
    world.add(std::make_shared<Triangle>(b0, b1, apex, blue));
    world.add(std::make_shared<Triangle>(b1, b2, apex, blue));
    world.add(std::make_shared<Triangle>(b2, b0, apex, blue));
    world.add(std::make_shared<Triangle>(b0, b2, b1, blue));  // base

    config.aspect_ratio = 16.0 / 9.0;
    config.image_width = 800;
    config.vfov = 40.0;
    config.samples_per_pixel = 100;
    config.max_depth = 50;
    config.look_from = {4.5, 2.5, 4};
    config.look_at = {0.2, 0.5, 0};

    background = Color{0.7, 0.8, 1.0};  // bright sky acts as the light source
}

// ---------- Mesh scenes (Sprint 3) ----------
// Models live in assets/models/ (not committed, see README). Run kiln from the repo root.

// One model on a grey floor under a bright sky. Shared by the teapot, bunny and dragon scenes
// so their render times are directly comparable.
void mesh_scene(HittableList& world, CameraConfig& config, Color& background,
                const std::string& path, const Transform& transform,
                std::shared_ptr<Material> material) {
    auto ground = std::make_shared<Lambertian>(Color{0.5, 0.5, 0.5});
    world.add(std::make_shared<Sphere>(Point3{0, -1000, 0}, 1000, ground));

    ObjLoadOptions options;
    options.generate_normals_if_missing = true;  // bunny/teapot/dragon ship without normals

    const auto load_start = std::chrono::steady_clock::now();
    const auto triangles = load_obj(path, std::move(material), transform, options);
    const auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - load_start)
                             .count();
    for (const auto& tri : triangles) world.add(tri);
    std::cout << "Loaded " << path << ": " << triangles.size() << " triangles in " << load_ms
              << " ms\n";

    config.aspect_ratio = 1.0;
    config.image_width = 400;
    config.vfov = 30.0;
    config.samples_per_pixel = 16;
    config.max_depth = 10;
    config.look_from = {0, 1.6, 5};
    config.look_at = {0, 0.7, 0};

    background = Color{0.7, 0.8, 1.0};
}

// Utah teapot, ~6.3k triangles. Source is ~6.4 units wide, so shrink it to fit the frame.
void teapot_scene(HittableList& world, CameraConfig& config, Color& background) {
    Transform t;
    t.scale = 0.35;
    t.rotate_y_degrees = -30.0;
    t.translate = {-0.08, 0.0, 0.0};
    mesh_scene(world, config, background, "assets/models/teapot.obj", t,
               std::make_shared<Metal>(Color{0.85, 0.85, 0.88}, 0.15));
}

// Sir Hops-a-Lot, the Stanford bunny, ~69k triangles. Source is in metres (15 cm tall),
// so scale up 10x.
void bunny_scene(HittableList& world, CameraConfig& config, Color& background) {
    Transform t;
    t.scale = 10.0;
    t.rotate_y_degrees = 20.0;
    t.translate = {0.17, -0.33, 0.0};  // centre in x, feet on the floor
    mesh_scene(world, config, background, "assets/models/stanford-bunny.obj", t,
               std::make_shared<Lambertian>(Color{0.75, 0.6, 0.45}));
}

// XYZ RGB dragon, ~250k triangles. The stress test: hopeless without a BVH.
void dragon_scene(HittableList& world, CameraConfig& config, Color& background) {
    Transform t;
    t.scale = 0.011;
    t.rotate_y_degrees = 30.0;
    t.translate = {0.0, 0.46, 0.0};  // feet (y ~ -41) on the floor
    mesh_scene(world, config, background, "assets/models/xyzrgb_dragon.obj", t,
               std::make_shared<Metal>(Color{0.8, 0.6, 0.2}, 0.2));
}

int main(int argc, char* argv[]) {
    std::cout << "Kiln v" << kVersion << '\n';

    // Flags (stopgap until the Sprint 4 CLI):
    //   --no-bvh     brute force, test every object (the "before" benchmark)
    //   --midpoint   median split instead of SAH
    //   --out FILE   output path (default output.png)
    bool use_bvh = true;
    BVHSplit split = BVHSplit::kSAH;
    std::string out = "output.png";
    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--no-bvh") {
            use_bvh = false;
        } else if (arg == "--midpoint") {
            split = BVHSplit::kMidpoint;
        } else if (arg == "--out" && a + 1 < argc) {
            out = argv[++a];
        } else {
            std::cerr << "Unknown option: " << arg
                      << "\nUsage: kiln [--no-bvh] [--midpoint] [--out FILE]\n";
            return 1;
        }
    }

    // Fixed seed: the same scene always renders the same pixels, so renders can be diffed.
    seed_rng(1337);

    HittableList world;
    CameraConfig config;
    Color background;

    // Pick which scene to render
    try {
        //cornell_box(world, config, background);
        //spheres_under_light(world, config, background);
        //triangles_scene(world, config, background);
        //teapot_scene(world, config, background);
        //bunny_scene(world, config, background);
        dragon_scene(world, config, background);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    // Acceleration structure
    const Hittable* scene = &world;
    std::unique_ptr<BVHNode> bvh;
    if (use_bvh) {
        const auto build_start = std::chrono::steady_clock::now();
        bvh = std::make_unique<BVHNode>(world.objects(), split);
        const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - build_start)
                                  .count();
        std::cout << "Built BVH (" << (split == BVHSplit::kSAH ? "SAH" : "midpoint") << ") over "
                  << world.objects().size() << " objects in " << build_ms << " ms\n";
        scene = bvh.get();
    } else {
        std::cout << "BVH disabled: testing every object per ray\n";
    }

    const Camera camera(config);
    Image image(camera.image_width(), camera.image_height());

    // Render
    const auto start = std::chrono::steady_clock::now();

    for (int j = 0; j < image.height(); ++j) {
        std::clog << "\rScanlines remaining: " << (image.height() - j) << ' ' << std::flush;
        for (int i = 0; i < image.width(); ++i) {
            Color pixel_color{0.0, 0.0, 0.0};
            for (int s = 0; s < camera.samples_per_pixel(); ++s) {
                pixel_color += ray_color(camera.get_ray(i, j), camera.max_depth(), *scene, background);
            }           
            image.set(i, j, pixel_color / camera.samples_per_pixel());
        }
    }
    std::clog << "\rDone.                    \n";

    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (!image.write_png(out)) {
        std::cerr << "Failed to write " << out << '\n';
        return 1;
    }
    std::cout << "Rendered " << image.width() << "x" << image.height() << " in " << ms
              << " ms -> " << out << '\n';
    return 0;
}