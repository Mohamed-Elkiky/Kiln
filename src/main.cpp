// kiln: render a JSON scene file to PNG.
//
//   kiln scenes/cornell.json -o cornell.png
//   kiln scenes/bunny.json --samples 64 --width 800
//   kiln scenes/dragon.json --benchmark --runs 5
//   kiln scenes/cornell.json --samples 16 --denoise
//   kiln --help

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "CLI11.hpp"
#include "kiln/bvh.hpp"
#include "kiln/camera.hpp"
#include "kiln/denoise.hpp"
#include "kiln/hittable.hpp"
#include "kiln/image.hpp"
#include "kiln/renderer.hpp"
#include "kiln/scene_loader.hpp"
#include "kiln/version.hpp"

using namespace kiln;

int main(int argc, char* argv[]) {
    CLI::App app{"Kiln: a physically based path tracer. Renders a JSON scene file to PNG.", "kiln"};
    app.set_version_flag("--version", std::string("Kiln v") + kVersion);
    app.footer("Example: kiln scenes/cornell.json --samples 100 -o cornell.png");

    std::string scene_path;
    std::string out = "output.png";
    int samples = 0;
    int depth = 0;
    int width = 0;
    int runs = 3;
    bool no_bvh = false;
    bool midpoint = false;
    bool benchmark = false;
    bool denoise_flag = false;
    RenderSettings settings;

    app.add_option("scene", scene_path, "Scene file to render, e.g. scenes/cornell.json")
        ->required()
        ->check(CLI::ExistingFile);
    CLI::Option* out_opt =
        app.add_option("-o,--out", out, "Where to write the PNG")->capture_default_str();
    CLI::Option* samples_opt =
        app.add_option("-s,--samples", samples, "Samples per pixel (overrides the scene)")
            ->check(CLI::Range(1, 1000000));
    CLI::Option* depth_opt =
        app.add_option("-d,--depth", depth, "Max bounces per ray (overrides the scene)")
            ->check(CLI::Range(1, 10000));
    CLI::Option* width_opt =
        app.add_option("-w,--width", width,
                       "Image width in pixels (overrides the scene, keeps its aspect ratio)")
            ->check(CLI::Range(1, 16384));
    app.add_option("-t,--threads", settings.threads, "Worker threads, 0 = one per hardware thread")
        ->check(CLI::Range(0, 1024))
        ->capture_default_str();
    app.add_option("--seed", settings.seed, "Random seed: same seed and settings, same image")
        ->capture_default_str();
    CLI::Option* no_bvh_flag =
        app.add_flag("--no-bvh", no_bvh, "Test every object per ray (brute force, for comparison)");
    app.add_flag("--midpoint", midpoint, "Build the BVH with median splits instead of SAH")
        ->excludes(no_bvh_flag);
    CLI::Option* bench_flag = app.add_flag(
        "--benchmark", benchmark, "Time the render instead of saving it: median time and Mrays/s");
    app.add_option("--runs", runs, "Renders to time with --benchmark")
        ->check(CLI::Range(1, 100))
        ->needs(bench_flag)
        ->capture_default_str();
    bench_flag->excludes(out_opt);
    app.add_flag("--denoise", denoise_flag,
                 "Remove noise with Intel Open Image Denoise, so low sample counts look clean")
        ->excludes(bench_flag);

    CLI11_PARSE(app, argc, argv);

    std::cout << "Kiln v" << kVersion << '\n';
    if (denoise_flag && !denoise_available()) {
        std::cerr << "error: --denoise needs a build with Intel Open Image Denoise "
                     "(see README, Denoising)\n";
        return 1;
    }

    Scene scene;
    try {
        scene = load_scene(scene_path);
    } catch (const SceneError& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    // Command-line flags win over the scene file.
    if (*samples_opt) scene.camera.samples_per_pixel = samples;
    if (*depth_opt) scene.camera.max_depth = depth;
    if (*width_opt) scene.camera.image_width = width;

    // Acceleration structure
    const Hittable* world = &scene.world;
    std::unique_ptr<BVHNode> bvh;
    if (no_bvh) {
        std::cout << "BVH disabled: testing every object per ray\n";
    } else {
        const BVHSplit split = midpoint ? BVHSplit::kMidpoint : BVHSplit::kSAH;
        const auto build_start = std::chrono::steady_clock::now();
        bvh = std::make_unique<BVHNode>(scene.world.objects(), split);
        const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - build_start)
                                  .count();
        std::cout << "Built BVH (" << (midpoint ? "midpoint" : "SAH") << ") over "
                  << scene.world.objects().size() << " objects in " << build_ms << " ms\n";
        world = bvh.get();
    }

    const Camera camera(scene.camera);
    Image image(camera.image_width(), camera.image_height());
    const int threads = resolve_thread_count(settings);
    std::cout << "Rendering " << camera.image_width() << "x" << camera.image_height() << " at "
              << camera.samples_per_pixel() << " spp, depth " << camera.max_depth() << ", with "
              << threads << (threads == 1 ? " thread\n" : " threads\n");

    if (benchmark) {
        settings.show_progress = false;
        std::vector<double> times;
        std::uint64_t rays = 0;
        std::cout << std::fixed << std::setprecision(3);
        for (int n = 0; n < runs; ++n) {
            const auto start = std::chrono::steady_clock::now();
            rays = render(camera, *world, scene.background, image, settings).rays;
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            times.push_back(seconds);
            std::cout << "Run " << (n + 1) << '/' << runs << ": " << seconds << " s\n";
        }
        std::sort(times.begin(), times.end());
        const double median = times[times.size() / 2];
        std::cout << "Median: " << median << " s, " << std::setprecision(1)
                  << static_cast<double>(rays) / median / 1e6 << " Mrays/s (" << rays
                  << " rays per render)\n";
        return 0;
    }

        // The denoiser's albedo and normal guides are filled during the same render.
    std::unique_ptr<Image> albedo;
    std::unique_ptr<Image> normal;
    RenderAovs aovs;
    if (denoise_flag) {
        albedo = std::make_unique<Image>(image.width(), image.height());
        normal = std::make_unique<Image>(image.width(), image.height());
        aovs = {albedo.get(), normal.get()};
    }

    const auto start = std::chrono::steady_clock::now();
    render(camera, *world, scene.background, image, settings, aovs);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();

    if (denoise_flag) {
        const auto denoise_start = std::chrono::steady_clock::now();
        try {
            denoise(image, *albedo, *normal);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << '\n';
            return 1;
        }
        std::cout << "Denoised in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - denoise_start)
                         .count()
                  << " ms\n";
    }

    if (!image.write_png(out)) {
        std::cerr << "error: failed to write " << out << '\n';
        return 1;
    }
    std::cout << "Rendered " << image.width() << "x" << image.height() << " in " << ms << " ms -> "
              << out << '\n';
    return 0;
}