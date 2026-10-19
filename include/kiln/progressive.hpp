#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "kiln/camera.hpp"
#include "kiln/hittable.hpp"
#include "kiln/image.hpp"
#include "kiln/renderer.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

// Renders on a background thread, one sample per pixel per pass, and keeps the running average
// of every finished pass. Any thread can read the latest average at any time: a pass is only
// added once it is complete, so readers never see a half-rendered frame.
class ProgressiveRenderer {
public:
    // world must outlive the renderer. settings.threads / tile_size / seed are used per pass.
    ProgressiveRenderer(const Hittable& world, const Color& background,
                        const RenderSettings& settings);
    ~ProgressiveRenderer();  // stops and joins the render thread

    ProgressiveRenderer(const ProgressiveRenderer&) = delete;
    ProgressiveRenderer& operator=(const ProgressiveRenderer&) = delete;

    // (Re)starts from zero with this camera, cancelling any pass in flight. Stops by itself after
    // target_passes passes; 0 means keep refining until stop().
    void start(const CameraConfig& config, int target_passes);

    // Cancels the pass in flight (it is thrown away) and waits for the thread. Safe to call twice.
    void stop();

    struct Status {
        int passes = 0;          // finished passes = samples per pixel so far
        int target = 0;          // 0 = unlimited
        std::uint64_t rays = 0;  // rays traced by the finished passes
        double seconds = 0.0;    // since start(), frozen once the target is reached
        bool done = false;       // target reached
    };
    Status status() const;

    int width() const;
    int height() const;

    // If the average changed since `seen`, writes it as RGBA8 (gamma encoded, ready for a
    // texture), updates `seen` and returns true. Otherwise returns false and leaves rgba alone.
    bool frame_rgba(std::vector<std::uint8_t>& rgba, std::uint64_t& seen) const;

    // The current average in linear colour, e.g. for write_png(). Black before the first pass.
    Image image() const;

private:
    void run();

    const Hittable& world_;
    const Color background_;
    const RenderSettings settings_;

    std::unique_ptr<Camera> camera_;  // 1 sample per pixel; replaced by start()
    std::thread thread_;
    std::atomic<bool> cancel_{false};

    // Everything below is shared with readers and guarded by mutex_.
    mutable std::mutex mutex_;
    int width_ = 0;
    int height_ = 0;
    std::vector<Color> sum_;  // sum of all finished passes
    int passes_ = 0;
    int target_ = 0;
    std::uint64_t rays_ = 0;
    std::uint64_t version_ = 0;  // bumped on every change, so readers can skip stale frames
    bool done_ = false;
    std::chrono::steady_clock::time_point start_time_;
    double done_seconds_ = 0.0;
};

}  // namespace kiln