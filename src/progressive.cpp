#include "kiln/progressive.hpp"

#include <cstddef>

namespace kiln {

namespace {

// A different, well-mixed seed for every pass, so passes don't repeat each other's noise.
std::uint32_t pass_seed(std::uint32_t seed, int pass) {
    std::uint32_t x = seed + 0x9E3779B9u * static_cast<std::uint32_t>(pass + 1);
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

}  // namespace

ProgressiveRenderer::ProgressiveRenderer(const Hittable& world, const Color& background,
                                         const RenderSettings& settings)
    : world_(world), background_(background), settings_(settings) {}

ProgressiveRenderer::~ProgressiveRenderer() {
    stop();
}

void ProgressiveRenderer::start(const CameraConfig& config, int target_passes) {
    stop();

    CameraConfig one_sample = config;
    one_sample.samples_per_pixel = 1;
    camera_ = std::make_unique<Camera>(one_sample);

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        width_ = camera_->image_width();
        height_ = camera_->image_height();
        sum_.assign(static_cast<std::size_t>(width_) * height_, Color{0.0, 0.0, 0.0});
        passes_ = 0;
        target_ = target_passes > 0 ? target_passes : 0;
        rays_ = 0;
        ++version_;
        done_ = false;
        start_time_ = std::chrono::steady_clock::now();
        done_seconds_ = 0.0;
    }

    cancel_.store(false);
    thread_ = std::thread(&ProgressiveRenderer::run, this);
}

void ProgressiveRenderer::stop() {
    cancel_.store(true);
    if (thread_.joinable()) thread_.join();
}

void ProgressiveRenderer::run() {
    const int w = camera_->image_width();
    const int h = camera_->image_height();
    Image pass(w, h);

    RenderSettings settings = settings_;
    settings.show_progress = false;
    settings.cancel = &cancel_;

    for (int p = 0; target_ == 0 || p < target_; ++p) {
        settings.seed = pass_seed(settings_.seed, p);
        const RenderStats stats = render(*camera_, world_, background_, pass, settings);
        if (cancel_.load()) return;  // unfinished pass: throw it away

        // Only finished passes are added, under the lock, so readers never see a partial one.
        const std::lock_guard<std::mutex> lock(mutex_);
        std::size_t k = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) sum_[k++] += pass.get(x, y);
        }
        ++passes_;
        rays_ += stats.rays;
        ++version_;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    done_ = true;
    done_seconds_ =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    ++version_;
}

ProgressiveRenderer::Status ProgressiveRenderer::status() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    Status s;
    s.passes = passes_;
    s.target = target_;
    s.rays = rays_;
    s.done = done_;
    s.seconds =
        done_
            ? done_seconds_
            : std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
    return s;
}

int ProgressiveRenderer::width() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return width_;
}

int ProgressiveRenderer::height() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return height_;
}

bool ProgressiveRenderer::frame_rgba(std::vector<std::uint8_t>& rgba, std::uint64_t& seen) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (version_ == seen) return false;
    seen = version_;

    rgba.resize(sum_.size() * 4);
    const double scale = passes_ > 0 ? 1.0 / passes_ : 0.0;
    std::size_t k = 0;
    for (const Color& c : sum_) {
        rgba[k++] = to_display_byte(c.x * scale);
        rgba[k++] = to_display_byte(c.y * scale);
        rgba[k++] = to_display_byte(c.z * scale);
        rgba[k++] = 255;
    }
    return true;
}

Image ProgressiveRenderer::image() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    Image out(width_, height_);
    const double scale = passes_ > 0 ? 1.0 / passes_ : 0.0;
    std::size_t k = 0;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) out.set(x, y, sum_[k++] * scale);
    }
    return out;
}

}  // namespace kiln