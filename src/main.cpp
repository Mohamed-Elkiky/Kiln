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
#include "kiln/renderer.hpp"
#include "kiln/scenes.hpp"
#include "kiln/vec3.hpp"
#include "kiln/version.hpp"

using namespace kiln;

int main(int argc, char* argv[]) {
    std::cout << "Kiln v" << kVersion << '\n';

    // Flags (stopgap until the Sprint 4 CLI):
    //   --no-bvh     brute force, test every object (the "before" benchmark)
    //   --midpoint   median split instead of SAH
    //   --out FILE   output path (default output.png)
    //   --threads N  worker threads (default: one per hardware thread)
    bool use_bvh = true;
    BVHSplit split = BVHSplit::kSAH;
    std::string out = "output.png";
    RenderSettings settings;
    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--no-bvh") {
            use_bvh = false;
        } else if (arg == "--midpoint") {
            split = BVHSplit::kMidpoint;
        } else if (arg == "--out" && a + 1 < argc) {
            out = argv[++a];
        } else if (arg == "--threads" && a + 1 < argc) {
            settings.threads = std::stoi(argv[++a]);
        } else {
            std::cerr << "Unknown option: " << arg
                      << "\nUsage: kiln [--no-bvh] [--midpoint] [--out FILE] [--threads N]\n";
            return 1;
        }
    }

    HittableList world;
    CameraConfig config;
    Color background;

    // Pick which scene to render
    try {
        cornell_box(world, config, background);
        // spheres_under_light(world, config, background);
        // triangles_scene(world, config, background);
        // teapot_scene(world, config, background);
        // bunny_scene(world, config, background);
        // dragon_scene(world, config, background);
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
    const int threads = resolve_thread_count(settings);
    std::cout << "Rendering with " << threads << (threads == 1 ? " thread\n" : " threads\n");

    const auto start = std::chrono::steady_clock::now();
    render(camera, *scene, background, image, settings);
    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (!image.write_png(out)) {
        std::cerr << "Failed to write " << out << '\n';
        return 1;
    }
    std::cout << "Rendered " << image.width() << "x" << image.height() << " in " << ms << " ms -> "
              << out << '\n';
    return 0;
}
