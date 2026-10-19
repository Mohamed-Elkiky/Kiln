#include "kiln/image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stb_image_write.h"

namespace kiln {

std::uint8_t to_display_byte(double linear) {
    // Linear light -> gamma 2. Monitors expect gamma-encoded values; without this images look
    // too dark.
    const double gamma = linear > 0.0 ? std::sqrt(linear) : 0.0;
    return static_cast<std::uint8_t>(256.0 * std::clamp(gamma, 0.0, 0.999));
}

Image::Image(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height) {}

bool Image::write_png(const std::string& path) const {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(pixels_.size() * 3);
    for (const Color& c : pixels_) {
        bytes.push_back(to_display_byte(c.x));
        bytes.push_back(to_display_byte(c.y));
        bytes.push_back(to_display_byte(c.z));
    }
    return stbi_write_png(path.c_str(), width_, height_, 3, bytes.data(), width_ * 3) != 0;
}

}  // namespace kiln
