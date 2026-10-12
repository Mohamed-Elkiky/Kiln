#pragma once

#include "kiln/ray.hpp"
#include "kiln/vec3.hpp"

namespace kiln {

struct CameraConfig {
    double aspect_ratio = 16.0 / 9.0;
    int image_width = 400;
    double vfov = 90.0;  // vertical field of view, degrees

    int samples_per_pixel = 100;  // rays per pixel, averaged (antialiasing + noise)
    int max_depth = 50;           // max bounces per ray

    Point3 look_from{0, 0, 0};  // where the camera sits
    Point3 look_at{0, 0, -1};   // what it points at
    Vec3 vup{0, 1, 0};          // which way is "up"
};

class Camera {
public:
    explicit Camera(const CameraConfig& config);

    int image_width() const { return image_width_; }
    int image_height() const { return image_height_; }
    int samples_per_pixel() const { return samples_per_pixel_; }
    int max_depth() const { return max_depth_; }

    // Ray from the camera through a random point inside pixel (i, j). (0, 0) is top-left.
    Ray get_ray(int i, int j) const;

private:
    int image_width_;
    int image_height_;
    int samples_per_pixel_;
    int max_depth_;

    Point3 center_;
    Point3 pixel00_;      // centre of the top-left pixel
    Vec3 pixel_delta_u_;  // step one pixel right
    Vec3 pixel_delta_v_;  // step one pixel down

    Vec3 u_, v_, w_;  // camera basis: right, up, backwards
};

}  // namespace kiln