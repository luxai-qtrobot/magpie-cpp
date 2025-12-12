#include <magpie/frames/image_frame_jpeg.hpp>
#include <magpie/utils/logger.hpp>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Write binary PPM (P6). Input is RGB bytes (W*H*3).
static void writePpm(const std::string& path,
                     int width,
                     int height,
                     const std::vector<std::uint8_t>& rgb)
{
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << width << " " << height << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb.data()),
            static_cast<std::streamsize>(rgb.size()));
}

// Convert BGR -> RGB (in-place to output buffer).
static std::vector<std::uint8_t> bgrToRgb(const std::vector<std::uint8_t>& bgr)
{
    std::vector<std::uint8_t> rgb = bgr;
    for (size_t i = 0; i + 2 < rgb.size(); i += 3) {
        std::swap(rgb[i + 0], rgb[i + 2]);
    }
    return rgb;
}

int main() {
#ifndef MAGPIE_WITH_VIDEO
    magpie::Logger::error("MAGPIE_WITH_VIDEO is OFF. Reconfigure with -DMAGPIE_WITH_VIDEO=ON");
    return 1;
#else
    using namespace magpie;

    Logger::setLevel("INFO");

    const int width = 320;
    const int height = 240;
    const int channels = 3;

    // Create a synthetic BGR gradient image
    std::vector<std::uint8_t> bgr(static_cast<size_t>(width) * height * channels);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = (static_cast<size_t>(y) * width + x) * 3;
            const std::uint8_t b = static_cast<std::uint8_t>((x * 255) / (width - 1));
            const std::uint8_t g = static_cast<std::uint8_t>((y * 255) / (height - 1));
            const std::uint8_t r = static_cast<std::uint8_t>(((x + y) * 255) / (width + height - 2));
            bgr[idx + 0] = b;
            bgr[idx + 1] = g;
            bgr[idx + 2] = r;
        }
    }

    // Save original as PPM for reference (convert to RGB)
    writePpm("out_input.ppm", width, height, bgrToRgb(bgr));
    Logger::info("Wrote out_input.ppm");

    // Encode to JPEG
    const int quality = 80;
    ImageFrameJpeg jpeg = ImageFrameJpeg::fromPixels(bgr, width, height, channels, "BGR", quality);

    Logger::info("JPEG frame: bytes=" + std::to_string(jpeg.data().size()) +
                 ", dims=" + std::to_string(jpeg.width()) + "x" + std::to_string(jpeg.height()) +
                 "x" + std::to_string(jpeg.channels()) +
                 ", pixel_format=" + jpeg.pixelFormat() +
                 ", format=" + jpeg.format());

    // Decode back to pixels
    // Ask for BGR back (same as input)
    std::vector<std::uint8_t> bgr2 = jpeg.toPixels("BGR");

    // Write decoded as PPM for visual inspection (convert to RGB)
    writePpm("out_decoded.ppm", width, height, bgrToRgb(bgr2));
    Logger::info("Wrote out_decoded.ppm");

    // Quick sanity: compare a subset of bytes
    const size_t n = std::min(bgr.size(), bgr2.size());
    size_t diff = 0;
    for (size_t i = 0; i < n; ++i) {
        if (bgr[i] != bgr2[i]) diff++;
    }
    Logger::info("Byte differences (first " + std::to_string(n) + " bytes): " + std::to_string(diff) +
                 " (JPEG is lossy, so diffs are expected)");

    return 0;
#endif
}
