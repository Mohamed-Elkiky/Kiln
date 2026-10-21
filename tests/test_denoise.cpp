#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "doctest/doctest.h"
#include "kiln/bvh.hpp"
#include "kiln/camera.hpp"
#include "kiln/denoise.hpp"
#include "kiln/image.hpp"
#include "kiln/renderer.hpp"
#include "kiln/scene_loader.hpp"

using namespace kiln;

namespace {

// Error in display space (clamped, gamma 2), like the PNG, so the bright light doesn't dominate.
double mse(const Image& a, const Image& b) {
    auto shown = [](double v) { return std::sqrt(std::clamp(v, 0.0, 1.0)); };
    double sum = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Color& p = a.get(x, y);
            const Color& q = b.get(x, y);
            for (int c = 0; c < 3; ++c) {
                const double d = shown(p[c]) - shown(q[c]);
                sum += d * d;
            }
        }
    }
    return sum / (3.0 * a.width() * a.height());
}

}  // namespace

TEST_CASE("without OIDN, denoise() throws a clear error") {
    if (denoise_available()) return;  // built with OIDN: the next test covers it
    Image img(4, 4);
    CHECK_THROWS_AS(denoise(img, img, img), std::runtime_error);
}

TEST_CASE("with OIDN, denoising 8 spp cuts the error vs 256 spp by over 4x") {
    if (!denoise_available()) {
        MESSAGE("built without OIDN: skipped");
        return;
    }

    // Small Cornell box: noisy at 8 spp, reference at 256 spp.
    Scene scene = load_scene(std::string(KILN_SCENES_DIR) + "/cornell.json");
    const BVH bvh(scene.world.objects());
    scene.camera.image_width = 96;
    RenderSettings settings;
    settings.show_progress = false;

    scene.camera.samples_per_pixel = 256;
    const Camera ref_camera(scene.camera);
    Image reference(ref_camera.image_width(), ref_camera.image_height());
    render(ref_camera, bvh, scene.background, reference, settings);

    scene.camera.samples_per_pixel = 8;
    const Camera camera(scene.camera);
    Image noisy(camera.image_width(), camera.image_height());
    Image albedo(camera.image_width(), camera.image_height());
    Image normal(camera.image_width(), camera.image_height());
    render(camera, bvh, scene.background, noisy, settings, {&albedo, &normal});

    Image denoised = noisy;
    denoise(denoised, albedo, normal);

    const double before = mse(noisy, reference);
    const double after = mse(denoised, reference);
    MESSAGE("MSE vs 256 spp: noisy " << before << ", denoised " << after);
    CHECK(after < before / 4.0);

    bool finite = true;
    for (int y = 0; y < denoised.height(); ++y) {
        for (int x = 0; x < denoised.width(); ++x) {
            const Color& c = denoised.get(x, y);
            finite = finite && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
        }
    }
    CHECK(finite);  // no NaN or inf

    Image small(4, 4);
    CHECK_THROWS_AS(denoise(small, albedo, normal), std::runtime_error);  // mismatched sizes
}
