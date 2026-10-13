#pragma once

#include <string>
#include <vector>

#include "kiln/vec3.hpp"

namespace kiln {

// Framebuffer of linear colours, (0, 0) is top-left.
class Image {
public:
    Image(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    void set(int x, int y, const Color& c) { pixels_[static_cast<size_t>(y) * width_ + x] = c; }
    const Color& get(int x, int y) const { return pixels_[static_cast<size_t>(y) * width_ + x]; }

    // Returns false if the file could not be written.
    bool write_png(const std::string& path) const;

private:
    int width_;
    int height_;
    std::vector<Color> pixels_;
};

}  // namespace kiln
