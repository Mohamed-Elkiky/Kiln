#include "kiln/denoise.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef KILN_HAS_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

namespace kiln {

#ifdef KILN_HAS_OIDN

namespace {

// OIDN wants tightly packed float RGB; Kiln's framebuffer is double.
std::vector<float> to_floats(const Image& image) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(image.width()) * image.height() * 3);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const Color& c = image.get(x, y);
            out.push_back(static_cast<float>(c.x));
            out.push_back(static_cast<float>(c.y));
            out.push_back(static_cast<float>(c.z));
        }
    }
    return out;
}

void throw_on_error(oidn::DeviceRef& device) {
    const char* message = nullptr;
    if (device.getError(message) != oidn::Error::None) {
        throw std::runtime_error(std::string("OIDN: ") + (message ? message : "unknown error"));
    }
}

}  // namespace

bool denoise_available() {
    return true;
}

void denoise(Image& beauty, const Image& albedo, const Image& normal) {
    const int w = beauty.width();
    const int h = beauty.height();
    if (albedo.width() != w || albedo.height() != h || normal.width() != w ||
        normal.height() != h) {
        throw std::runtime_error("denoise: beauty, albedo and normal must be the same size");
    }

    // CPU device: same result on every machine, and fast enough for a still image.
    oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
    device.commit();
    throw_on_error(device);

    const std::size_t bytes = static_cast<std::size_t>(w) * h * 3 * sizeof(float);
    auto upload = [&](const Image& image) {
        oidn::BufferRef buffer = device.newBuffer(bytes);
        const std::vector<float> data = to_floats(image);
        buffer.write(0, bytes, data.data());
        return buffer;
    };
    oidn::BufferRef color = upload(beauty);
    oidn::BufferRef albedo_raw = upload(albedo);
    oidn::BufferRef normal_raw = upload(normal);
    oidn::BufferRef albedo_clean = device.newBuffer(bytes);
    oidn::BufferRef normal_clean = device.newBuffer(bytes);
    oidn::BufferRef output = device.newBuffer(bytes);

    // The guides are averaged from the same samples, so they're noisy too (rays through glass
    // land in different places). Denoise each one first, then tell the main pass they're clean:
    // OIDN's highest quality mode.
    auto prefilter = [&](const char* name, oidn::BufferRef& in, oidn::BufferRef& out) {
        oidn::FilterRef filter = device.newFilter("RT");
        filter.setImage(name, in, oidn::Format::Float3, w, h);
        filter.setImage("output", out, oidn::Format::Float3, w, h);
        filter.commit();
        filter.execute();
    };
    prefilter("albedo", albedo_raw, albedo_clean);
    prefilter("normal", normal_raw, normal_clean);

    oidn::FilterRef filter = device.newFilter("RT");
    filter.setImage("color", color, oidn::Format::Float3, w, h);
    filter.setImage("albedo", albedo_clean, oidn::Format::Float3, w, h);
    filter.setImage("normal", normal_clean, oidn::Format::Float3, w, h);
    filter.setImage("output", output, oidn::Format::Float3, w, h);
    filter.set("hdr", true);  // linear radiance: lights are far brighter than 1
    filter.set("cleanAux", true);
    filter.commit();
    filter.execute();
    throw_on_error(device);

    std::vector<float> result(static_cast<std::size_t>(w) * h * 3);
    output.read(0, bytes, result.data());
    std::size_t k = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x, k += 3) {
            beauty.set(x, y, Color{result[k], result[k + 1], result[k + 2]});
        }
    }
}

#else  // built without OIDN

bool denoise_available() {
    return false;
}

void denoise(Image& /*beauty*/, const Image& /*albedo*/, const Image& /*normal*/) {
    throw std::runtime_error(
        "denoise: Kiln was built without Intel Open Image Denoise (see README, Denoising)");
}

#endif

}  // namespace kiln