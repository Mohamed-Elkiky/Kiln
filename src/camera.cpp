#include "kiln/camera.hpp"

#include <algorithm>
#include <cmath>

#include "kiln/random.hpp"

namespace kiln {

namespace {
constexpr double kPi = 3.1415926535897932385;
constexpr double degrees_to_radians(double degrees) {
    return degrees * kPi / 180.0;
}
}  // namespace

Camera::Camera(const CameraConfig& config)
    : image_width_(config.image_width),
      image_height_(std::max(1, static_cast<int>(config.image_width / config.aspect_ratio))),
      samples_per_pixel_(std::max(1, config.samples_per_pixel)),
      max_depth_(std::max(1, config.max_depth)),
      center_(config.look_from) {
    // Viewport size from the field of view
    const double focal_length = (config.look_from - config.look_at).length();
    const double h = std::tan(degrees_to_radians(config.vfov) / 2.0);
    const double viewport_height = 2.0 * h * focal_length;
    const double viewport_width =
        viewport_height * (static_cast<double>(image_width_) / image_height_);

    // Orthonormal basis: w points backwards, u right, v up
    w_ = unit_vector(config.look_from - config.look_at);
    u_ = unit_vector(cross(config.vup, w_));
    v_ = cross(w_, u_);

    // Vectors across the viewport (v goes down because pixel rows go down)
    const Vec3 viewport_u = viewport_width * u_;
    const Vec3 viewport_v = viewport_height * -v_;

    pixel_delta_u_ = viewport_u / image_width_;
    pixel_delta_v_ = viewport_v / image_height_;

    const Point3 viewport_upper_left =
        center_ - focal_length * w_ - viewport_u / 2.0 - viewport_v / 2.0;
    pixel00_ = viewport_upper_left + 0.5 * (pixel_delta_u_ + pixel_delta_v_);
}

Ray Camera::get_ray(int i, int j) const {
    // Random offset inside the pixel square [-0.5, 0.5) so edges get averaged (antialiasing).
    const double offset_x = random_double() - 0.5;
    const double offset_y = random_double() - 0.5;

    const Point3 pixel_sample =
        pixel00_ + (i + offset_x) * pixel_delta_u_ + (j + offset_y) * pixel_delta_v_;
    return {center_, pixel_sample - center_};
}

}  // namespace kiln
