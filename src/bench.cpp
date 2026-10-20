// kiln_bench: renders fixed scenes at fixed settings and prints a Markdown report.
//
//   kiln_bench                     full run, 3 runs per config, report to stdout
//   kiln_bench --out docs/benchmarks.md
//   kiln_bench --runs 1 --skip-naive --scene bunny
//
// Run from the repo root so the mesh scenes can find assets/models/.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#define KILN_HAS_CPUID 1
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#define KILN_HAS_CPUID 1
#endif

#include "kiln/bvh.hpp"
#include "kiln/camera.hpp"
#include "kiln/hittable_list.hpp"
#include "kiln/image.hpp"
#include "kiln/renderer.hpp"
#include "kiln/scene_loader.hpp"
#include "kiln/version.hpp"

#ifndef KILN_BUILD_CONFIG
#define KILN_BUILD_CONFIG "unknown"
#endif
#ifndef KILN_CXX_FLAGS
#define KILN_CXX_FLAGS ""
#endif

using namespace kiln;

namespace {

// ---------- Fixed benchmark settings ----------

struct SceneSpec {
    const char* name;
    const char* path;  // scene file, relative to the repo root
    int width;         // image width; height comes from the scene's aspect ratio
    int spp;
    // Brute force on a mesh takes hours, so naive mesh runs render a smaller, 1 spp image and
    // the time is scaled up by the camera sample count. Mrays/s is measured, not scaled.
    int naive_width;
    int naive_spp;
};

const std::vector<SceneSpec> kScenes = {
    {"Spheres", "scenes/spheres.json", 400, 64, 400, 64},
    {"Cornell box", "scenes/cornell.json", 400, 64, 400, 64},
    {"Bunny", "scenes/bunny.json", 400, 16, 200, 1},
    {"Dragon", "scenes/dragon.json", 400, 16, 100, 1},
};

enum class Accel { kNone, kMidpoint, kSAH };

struct Config {
    const char* label;
    Accel accel;
    bool all_threads;
};

const std::vector<Config> kConfigs = {
    {"Naive, 1T", Accel::kNone, false},
    {"BVH midpoint, 1T", Accel::kMidpoint, false},
    {"BVH SAH, 1T", Accel::kSAH, false},
    {"BVH SAH, NT", Accel::kSAH, true},
};

// ---------- Results ----------

struct Result {
    double seconds = 0.0;  // median, scaled to the full image for shortened naive runs
    double mrays = 0.0;    // median millions of rays per second
    bool estimated = false;
    bool skipped = false;
};

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// One render, timed. Returns {seconds, rays}.
std::pair<double, std::uint64_t> time_render(const Hittable& scene, const CameraConfig& config,
                                             const Color& background, const Lights& lights,
                                             int threads) {
    const Camera camera(config);
    Image image(camera.image_width(), camera.image_height());
    RenderSettings settings;
    settings.threads = threads;
    settings.show_progress = false;
    settings.lights = &lights;

    const auto start = std::chrono::steady_clock::now();
    const RenderStats stats = render(camera, scene, background, image, settings);
    const auto end = std::chrono::steady_clock::now();
    return {std::chrono::duration<double>(end - start).count(), stats.rays};
}

std::uint64_t pixels(const CameraConfig& config) {
    const Camera camera(config);
    return static_cast<std::uint64_t>(camera.image_width()) *
           static_cast<std::uint64_t>(camera.image_height()) *
           static_cast<std::uint64_t>(camera.samples_per_pixel());
}

// ---------- Machine info ----------

std::string cpu_name() {
#ifdef KILN_HAS_CPUID
    unsigned int regs[12] = {};
#if defined(_MSC_VER)
    int r[4];
    __cpuid(r, static_cast<int>(0x80000000u));
    if (static_cast<unsigned int>(r[0]) < 0x80000004) return "unknown";
    for (int i = 0; i < 3; ++i) {
        __cpuid(r, static_cast<int>(0x80000002u + static_cast<unsigned int>(i)));
        for (int k = 0; k < 4; ++k) regs[i * 4 + k] = static_cast<unsigned int>(r[k]);
    }
#else
    if (__get_cpuid_max(0x80000000, nullptr) < 0x80000004) return "unknown";
    for (unsigned int i = 0; i < 3; ++i) {
        __get_cpuid(0x80000002 + i, &regs[i * 4], &regs[i * 4 + 1], &regs[i * 4 + 2],
                    &regs[i * 4 + 3]);
    }
#endif
    std::string name(reinterpret_cast<const char*>(regs), sizeof(regs));
    name = name.c_str();  // drop trailing NULs
    const auto first = name.find_first_not_of(' ');
    const auto last = name.find_last_not_of(' ');
    return first == std::string::npos ? "unknown" : name.substr(first, last - first + 1);
#else
    return "unknown";
#endif
}

std::string compiler_name() {
    std::ostringstream s;
#if defined(__clang__)
    s << "Clang " << __clang_major__ << '.' << __clang_minor__ << '.' << __clang_patchlevel__;
#elif defined(_MSC_VER)
    s << "MSVC " << _MSC_VER / 100 << '.' << _MSC_VER % 100;
#elif defined(__GNUC__)
    s << "GCC " << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__;
#else
    s << "unknown";
#endif
    return s.str();
}

// ---------- Formatting ----------

std::string format_time(double s, bool estimated) {
    std::ostringstream out;
    if (estimated) out << '~';
    if (s < 10.0) {
        out << std::fixed << std::setprecision(2) << s << " s";
    } else if (s < 60.0) {
        out << std::fixed << std::setprecision(1) << s << " s";
    } else {
        const int total = static_cast<int>(s + 0.5);
        if (total >= 3600) out << total / 3600 << "h ";
        out << (total % 3600) / 60 << "m " << total % 60 << 's';
    }
    return out.str();
}

// Mrays/s to 3 significant figures, so brute-force rates like 0.000123 don't round to 0.0001.
std::string format_rate(double v) {
    int decimals = 1;
    if (v > 0.0 && v < 100.0) {
        decimals = std::max(1, 2 - static_cast<int>(std::floor(std::log10(v))));
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << v;
    return out.str();
}

std::string format_speedup(double x) {
    std::ostringstream out;
    if (x >= 100.0) {
        out << static_cast<long long>(x + 0.5);
    } else {
        out << std::fixed << std::setprecision(1) << x;
    }
    out << 'x';
    return out.str();
}

}  // namespace

int main(int argc, char* argv[]) {
    int runs = 3;
    bool skip_naive = false;
    std::string only_scene;
    std::string out_path;

    for (int a = 1; a < argc; ++a) {
        const std::string arg = argv[a];
        if (arg == "--runs" && a + 1 < argc) {
            runs = std::max(1, std::stoi(argv[++a]));
        } else if (arg == "--skip-naive") {
            skip_naive = true;
        } else if (arg == "--scene" && a + 1 < argc) {
            only_scene = argv[++a];
        } else if (arg == "--out" && a + 1 < argc) {
            out_path = argv[++a];
        } else {
            std::cerr << "Unknown option: " << arg
                      << "\nUsage: kiln_bench [--runs N] [--skip-naive] [--scene NAME] "
                         "[--out FILE]\n";
            return 1;
        }
    }

    const int all_threads = resolve_thread_count({});
    auto column_label = [&](const Config& c) {
        return c.all_threads ? "BVH SAH, " + std::to_string(all_threads) + "T"
                             : std::string(c.label);
    };
    std::vector<std::vector<Result>> results;
    std::vector<const SceneSpec*> ran;
    std::vector<std::size_t> object_counts;
    std::vector<std::string> resolutions;

    for (const SceneSpec& spec : kScenes) {
        if (!only_scene.empty()) {
            std::string lower = spec.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string wanted = only_scene;
            std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find(wanted) == std::string::npos) continue;
        }

        Scene loaded;
        try {
            loaded = load_scene(spec.path);
        } catch (const SceneError& e) {
            std::cerr << e.what() << " (skipped)\n";
            continue;
        }
        const HittableList& world = loaded.world;
        const Color& background = loaded.background;
        CameraConfig config = loaded.camera;

        config.image_width = spec.width;
        config.samples_per_pixel = spec.spp;

        const BVH bvh_mid(world.objects(), BVHSplit::kMidpoint);
        const BVH bvh_sah(world.objects(), BVHSplit::kSAH);

        std::vector<Result> row;
        for (const Config& c : kConfigs) {
            Result r;
            const bool naive = c.accel == Accel::kNone;
            if (naive && skip_naive) {
                r.skipped = true;
                row.push_back(r);
                continue;
            }

            const Hittable* scene = naive ? static_cast<const Hittable*>(&world)
                                    : c.accel == Accel::kMidpoint ? &bvh_mid
                                                                  : &bvh_sah;
            CameraConfig run_config = config;
            if (naive) {
                run_config.image_width = spec.naive_width;
                run_config.samples_per_pixel = spec.naive_spp;
            }
            const double scale =
                static_cast<double>(pixels(config)) / static_cast<double>(pixels(run_config));
            r.estimated = scale > 1.0;

            std::vector<double> times;
            std::vector<double> rates;
            for (int n = 0; n < runs; ++n) {
                std::cerr << spec.name << " | " << c.label << " | run " << (n + 1) << '/' << runs
                          << "... " << std::flush;
                const auto [seconds, rays] = time_render(
                    *scene, run_config, background, loaded.lights, c.all_threads ? all_threads : 1);
                times.push_back(seconds * scale);
                rates.push_back(static_cast<double>(rays) / seconds / 1e6);
                std::cerr << format_time(seconds, false) << '\n';
            }
            r.seconds = median(times);
            r.mrays = median(rates);
            row.push_back(r);
        }
        results.push_back(row);
        ran.push_back(&spec);
        object_counts.push_back(world.objects().size());
        const Camera camera(config);
        resolutions.push_back(std::to_string(camera.image_width()) + "x" +
                              std::to_string(camera.image_height()));
    }

    // ---------- Report ----------
    std::ostringstream md;
    md << "# Benchmarks\n\n"
       << "Generated by `kiln_bench` (Kiln v" << kVersion << "). Median of " << runs
       << (runs == 1 ? " run" : " runs") << " per cell. Times cover rendering only, not model "
       << "loading or BVH build.\n\n"
       << "## Machine\n\n"
       << "| | |\n|---|---|\n"
       << "| CPU | " << cpu_name() << " |\n"
       << "| Hardware threads | " << all_threads << " |\n"
       << "| Compiler | " << compiler_name() << " |\n"
       << "| Build | " << KILN_BUILD_CONFIG << " (`" << KILN_CXX_FLAGS << "`) |\n\n";

    md << "## Scenes\n\n| Scene | Objects | Resolution | SPP |\n|---|---|---|---|\n";
    for (std::size_t s = 0; s < ran.size(); ++s) {
        md << "| " << ran[s]->name << " | " << object_counts[s] << " | " << resolutions[s] << " | "
           << ran[s]->spp << " |\n";
    }

    md << "\n## Render time\n\n| Scene |";
    for (const Config& c : kConfigs) md << ' ' << column_label(c) << " |";
    md << " Speedup |\n|---|";
    for (std::size_t i = 0; i < kConfigs.size(); ++i) md << "---|";
    md << "---|\n";
    for (std::size_t s = 0; s < ran.size(); ++s) {
        md << "| " << ran[s]->name << " |";
        for (const Result& r : results[s]) {
            md << ' ' << (r.skipped ? std::string("-") : format_time(r.seconds, r.estimated))
               << " |";
        }
        const Result& naive = results[s].front();
        const Result& best = results[s].back();
        md << ' '
           << (naive.skipped ? std::string("-") : format_speedup(naive.seconds / best.seconds))
           << " |\n";
    }

    md << "\n## Throughput (Mrays/s)\n\n| Scene |";
    for (const Config& c : kConfigs) md << ' ' << column_label(c) << " |";
    md << "\n|---|";
    for (std::size_t i = 0; i < kConfigs.size(); ++i) md << "---|";
    md << '\n';
    for (std::size_t s = 0; s < ran.size(); ++s) {
        md << "| " << ran[s]->name << " |";
        for (const Result& r : results[s]) {
            md << ' ' << (r.skipped ? std::string("-") : format_rate(r.mrays)) << " |";
        }
        md << '\n';
    }

    md << "\n## Notes\n\n"
       << "- Speedup is Naive 1T over BVH SAH on all threads.\n"
       << "- A ray is every ray traced: camera rays, every bounce and every shadow ray. Scenes "
          "with light quads (Spheres, Cornell box) use next event estimation.\n"
       << "- `~` marks an estimate. Brute force on a mesh takes hours at full size, so naive mesh "
          "runs render a smaller 1 spp image of the same view and the time is scaled up by the "
          "camera sample count. Mrays/s is measured directly.\n"
       << "- Scenes with only a handful of objects run faster without the BVH: the box tests "
          "cost more than they save. It pays off from meshes upwards.\n"
       << "- Output is byte-identical across thread counts (checked by `test_renderer`). "
          "`test_bvh` checks both BVH builds return the same closest hit as brute force on "
          "20k random rays.\n\n"
       << "Reproduce: `kiln_bench --out docs/benchmarks.md` from the repo root.\n";

    std::cout << md.str();
    if (!out_path.empty()) {
        std::ofstream file(out_path);
        if (!file) {
            std::cerr << "Failed to write " << out_path << '\n';
            return 1;
        }
        file << md.str();
        std::cerr << "Wrote " << out_path << '\n';
    }
    return 0;
}
