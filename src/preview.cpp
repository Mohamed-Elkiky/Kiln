#include "kiln/preview.hpp"

#include <iostream>

#ifdef KILN_HAS_SDL

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#define SDL_MAIN_HANDLED  // we have our own main(); don't let SDL replace it
#include <SDL.h>

#include "kiln/image.hpp"
#include "kiln/progressive.hpp"

namespace kiln {

namespace {

// Camera position as angles around the look-at point, so dragging orbits and scrolling zooms.
struct Orbit {
    double yaw = 0.0;    // around the up axis
    double pitch = 0.0;  // above or below the look-at point
    double radius = 1.0;

    static Orbit from(const CameraConfig& c) {
        const Vec3 d = c.look_from - c.look_at;
        Orbit o;
        o.radius = d.length();
        o.yaw = std::atan2(d.x, d.z);
        o.pitch = std::asin(std::clamp(d.y / o.radius, -1.0, 1.0));
        return o;
    }

    Point3 eye(const Point3& target) const {
        return target + radius * Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                      std::cos(pitch) * std::cos(yaw)};
    }
};

// RAII so every early return still tears SDL down in the right order.
struct Sdl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    ~Sdl() {
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

// Prints the current view as a "camera" block that can be pasted straight into a scene file.
void print_camera(const CameraConfig& c) {
    std::printf(
        "\"camera\": { \"look_from\": [%.3f, %.3f, %.3f], \"look_at\": [%.3f, %.3f, %.3f], "
        "\"up\": [%.3f, %.3f, %.3f], \"fov\": %.2f },\n",
        c.look_from.x, c.look_from.y, c.look_from.z, c.look_at.x, c.look_at.y, c.look_at.z,
        c.vup.x, c.vup.y, c.vup.z, c.vfov);
    std::fflush(stdout);
}

void set_title(SDL_Window* window, const ProgressiveRenderer::Status& s) {
    char title[160];
    const double mrays = s.seconds > 0.0 ? static_cast<double>(s.rays) / s.seconds / 1e6 : 0.0;
    std::snprintf(title, sizeof(title), "Kiln  |  pass %d/%d  |  %.1f s  |  %.1f Mrays/s%s",
                  s.passes, s.target, s.seconds, mrays, s.done ? "  |  done" : "");
    SDL_SetWindowTitle(window, title);
}

}  // namespace

bool preview_available() {
    return true;
}

int run_preview(const Hittable& world, const CameraConfig& config, const Color& background,
                const RenderSettings& settings, const std::string& out_path) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "error: could not start SDL: " << SDL_GetError() << '\n';
        return 1;
    }
    Sdl sdl;

    const Camera probe(config);
    const int w = probe.image_width();
    const int h = probe.image_height();

    sdl.window = SDL_CreateWindow("Kiln", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!sdl.window) {
        std::cerr << "error: could not open a window: " << SDL_GetError() << '\n';
        return 1;
    }
    sdl.renderer = SDL_CreateRenderer(sdl.window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl.renderer) sdl.renderer = SDL_CreateRenderer(sdl.window, -1, SDL_RENDERER_SOFTWARE);
    if (!sdl.renderer) {
        std::cerr << "error: could not create a renderer: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_RenderSetLogicalSize(sdl.renderer, w, h);  // resizing keeps the aspect ratio
    sdl.texture =
        SDL_CreateTexture(sdl.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!sdl.texture) {
        std::cerr << "error: could not create a texture: " << SDL_GetError() << '\n';
        return 1;
    }

    // All rendering happens on the progressive renderer's threads. This thread only handles
    // input and shows the latest finished pass, so the window never stalls.
    ProgressiveRenderer progressive(world, background, settings);
    const int target = config.samples_per_pixel;
    CameraConfig view = config;
    Orbit orbit = Orbit::from(view);
    progressive.start(view, target);

    std::cout << "Preview: drag to orbit, scroll to zoom, R resets the view, S saves to "
              << out_path << ", C prints the camera, Esc quits\n";

    std::vector<std::uint8_t> rgba;
    std::uint64_t seen = 0;
    bool dragging = false;
    bool running = true;
    auto last_title = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    constexpr auto kFrame = std::chrono::milliseconds(33);  // ~30 updates a second

    while (running) {
        const auto frame_start = std::chrono::steady_clock::now();
        bool moved = false;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    } else if (e.key.keysym.sym == SDLK_s) {
                        const ProgressiveRenderer::Status s = progressive.status();
                        if (progressive.image().write_png(out_path)) {
                            std::cout << "Saved " << out_path << " (" << s.passes << " spp)\n";
                        } else {
                            std::cerr << "error: failed to write " << out_path << '\n';
                        }
                    } else if (e.key.keysym.sym == SDLK_r) {
                        view = config;
                        orbit = Orbit::from(view);
                        moved = true;
                    } else if (e.key.keysym.sym == SDLK_c) {
                        CameraConfig current = view;
                        current.look_from = orbit.eye(view.look_at);
                        print_camera(current);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) dragging = true;
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT) dragging = false;
                    break;
                case SDL_MOUSEMOTION:
                    if (dragging) {
                        orbit.yaw -= 0.005 * e.motion.xrel;
                        // Stop short of straight up/down, where "up" stops being defined.
                        orbit.pitch = std::clamp(orbit.pitch + 0.005 * e.motion.yrel, -1.5, 1.5);
                        moved = true;
                    }
                    break;
                case SDL_MOUSEWHEEL:
                    orbit.radius *= std::pow(0.9, e.wheel.y);
                    moved = true;
                    break;
                default:
                    break;
            }
        }
        if (!running) break;

        // A new view restarts the accumulation; the old pass is cancelled within one tile.
        if (moved) {
            view.look_from = orbit.eye(view.look_at);
            progressive.start(view, target);
        }

        if (progressive.frame_rgba(rgba, seen)) {
            SDL_UpdateTexture(sdl.texture, nullptr, rgba.data(), w * 4);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_title > std::chrono::milliseconds(250)) {
            set_title(sdl.window, progressive.status());
            last_title = now;
        }

        SDL_RenderClear(sdl.renderer);
        SDL_RenderCopy(sdl.renderer, sdl.texture, nullptr, nullptr);
        SDL_RenderPresent(sdl.renderer);

        const auto spent = std::chrono::steady_clock::now() - frame_start;
        if (spent < kFrame) std::this_thread::sleep_for(kFrame - spent);
    }

    progressive.stop();  // cancel mid-pass and join before the window goes away
    return 0;
}

}  // namespace kiln

#else  // built without SDL2

namespace kiln {

bool preview_available() {
    return false;
}

int run_preview(const Hittable& /*world*/, const CameraConfig& /*config*/,
                const Color& /*background*/, const RenderSettings& /*settings*/,
                const std::string& /*out_path*/) {
    std::cerr << "error: --preview needs a build with -DKILN_PREVIEW=ON (see README)\n";
    return 1;
}

}  // namespace kiln

#endif
