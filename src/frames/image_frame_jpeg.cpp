#include <magpie/frames/image_frame_jpeg.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace magpie {

// ---------------------------------------------------------------------------
// ImageFrameJpeg
// ---------------------------------------------------------------------------

ImageFrameJpeg::ImageFrameJpeg() : ImageFrameRaw() {
    name_ = "ImageFrameJpeg";
    setFormat("jpeg");
}

ImageFrameJpeg::ImageFrameJpeg(std::vector<std::uint8_t> jpegBytes,
                               int width,
                               int height,
                               int channels,
                               std::string pixelFormat)
    : ImageFrameRaw(std::move(jpegBytes),
                    "jpeg",
                    width,
                    height,
                    channels,
                    std::move(pixelFormat))
{
    name_ = "ImageFrameJpeg";
    setFormat("jpeg");
}

ImageFrameJpeg ImageFrameJpeg::fromPixels(const std::vector<std::uint8_t>& pixels,
                                          int width,
                                          int height,
                                          int channels,
                                          const std::string& pixelFormat,
                                          int quality)
{
#ifndef MAGPIE_WITH_VIDEO
    (void)pixels; (void)width; (void)height; (void)channels; (void)pixelFormat; (void)quality;
    throw std::runtime_error("ImageFrameJpeg::fromPixels: built without MAGPIE_WITH_VIDEO");
#else
    auto jpeg = encodeJpeg_(pixels, width, height, channels, pixelFormat, quality);
    // Store pixel_format as metadata (like Python) and format="jpeg"
    return ImageFrameJpeg(std::move(jpeg), width, height, channels, pixelFormat);
#endif
}

std::vector<std::uint8_t> ImageFrameJpeg::toPixels(const std::string& outPixelFormat) const {
#ifndef MAGPIE_WITH_VIDEO
    (void)outPixelFormat;
    throw std::runtime_error("ImageFrameJpeg::toPixels: built without MAGPIE_WITH_VIDEO");
#else
    // Default colorspace logic: requested > stored > "BGR"
    std::string fmt = outPixelFormat;
    if (fmt.empty()) {
        fmt = pixelFormat().empty() ? "BGR" : pixelFormat();
    }

    int w = width();
    int h = height();
    int c = channels();

    auto pixels = decodeJpeg_(data(), w, h, c, fmt);

    // We do NOT mutate the frame here. Python's to_np_image returns ndarray;
    // metadata in the frame stays as it was (unless you want to update it).
    return pixels;
#endif
}

void ImageFrameJpeg::toDict(Dict& out) const {
    // Use ImageFrameRaw serialization layout but force "jpeg"
    ImageFrameRaw::toDict(out);
    out["format"] = Value::fromString("jpeg");
}

void ImageFrameJpeg::loadFromDict(const Dict& dict) {
    ImageFrameRaw::loadFromDict(dict);
    name_ = "ImageFrameJpeg";
    setFormat("jpeg");
}

std::string ImageFrameJpeg::toString() const {
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_
        << "(size=" << data().size()
        << ", dims=";

    if (width() > 0 && height() > 0) {
        oss << width() << "x" << height() << "x" << channels();
    } else {
        oss << "unknown";
    }

    oss << ", format=jpeg";
    if (!pixelFormat().empty()) {
        oss << ", pixel_format=" << pixelFormat();
    }
    oss << ")";
    return oss.str();
}

std::unique_ptr<Frame> ImageFrameJpeg::clone() const {
    return std::unique_ptr<Frame>(new ImageFrameJpeg(*this));
}

ImageFrameJpeg::Registrar::Registrar() {
    Frame::registerType("ImageFrameJpeg", []() {
        return std::unique_ptr<Frame>(new ImageFrameJpeg());
    });
}
ImageFrameJpeg::Registrar ImageFrameJpeg::registrar_;

} // namespace magpie

// ---------------------------------------------------------------------------
// libjpeg-turbo (turbojpeg) implementation
// ---------------------------------------------------------------------------
#ifdef MAGPIE_WITH_VIDEO

#include <turbojpeg.h>
#include <cstring>

namespace magpie {

static std::string upper_(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

int ImageFrameJpeg::tjPixelFormat_(const std::string& fmt, int channels) {
    const std::string f = upper_(fmt);

    if (f == "GRAY" || channels == 1) return TJPF_GRAY;
    if (f == "RGB") return TJPF_RGB;
    if (f == "BGR") return TJPF_BGR;

    throw std::runtime_error("ImageFrameJpeg: unsupported pixel_format: " + fmt);
}

std::vector<std::uint8_t> ImageFrameJpeg::encodeJpeg_(const std::vector<std::uint8_t>& pixels,
                                                      int width,
                                                      int height,
                                                      int channels,
                                                      const std::string& pixelFormat,
                                                      int quality)
{
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("ImageFrameJpeg::fromPixels: width/height must be > 0");
    }
    if (!(channels == 1 || channels == 3)) {
        throw std::runtime_error("ImageFrameJpeg::fromPixels: channels must be 1 or 3");
    }
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    const size_t expected = static_cast<size_t>(width) * height * channels;
    if (pixels.size() < expected) {
        throw std::runtime_error("ImageFrameJpeg::fromPixels: pixels buffer too small");
    }

    tjhandle h = tjInitCompress();
    if (!h) throw std::runtime_error("ImageFrameJpeg::fromPixels: tjInitCompress failed");

    unsigned char* jpegBuf = nullptr;
    unsigned long jpegSize = 0;

    const int pf = tjPixelFormat_(pixelFormat, channels);

    const int rc = tjCompress2(
        h,
        reinterpret_cast<const unsigned char*>(pixels.data()),
        width,
        0,      // pitch = auto (width * pixelSize)
        height,
        pf,
        &jpegBuf,
        &jpegSize,
        TJSAMP_444,
        quality,
        TJFLAG_FASTDCT
    );

    tjDestroy(h);

    if (rc != 0) {
        if (jpegBuf) tjFree(jpegBuf);
        throw std::runtime_error(std::string("ImageFrameJpeg::fromPixels: tjCompress2 failed: ")
                                 + tjGetErrorStr());
    }

    std::vector<std::uint8_t> out(jpegBuf, jpegBuf + jpegSize);
    tjFree(jpegBuf);
    return out;
}

std::vector<std::uint8_t> ImageFrameJpeg::decodeJpeg_(const std::vector<std::uint8_t>& jpegBytes,
                                                      int& outWidth,
                                                      int& outHeight,
                                                      int& outChannels,
                                                      const std::string& outPixelFormat)
{
    if (jpegBytes.empty()) {
        outWidth = 0; outHeight = 0; outChannels = 0;
        return {};
    }

    tjhandle h = tjInitDecompress();
    if (!h) throw std::runtime_error("ImageFrameJpeg::toPixels: tjInitDecompress failed");

    int w = 0, hgt = 0, subsamp = 0, cs = 0;
    int rc = tjDecompressHeader3(h,
                                reinterpret_cast<const unsigned char*>(jpegBytes.data()),
                                static_cast<unsigned long>(jpegBytes.size()),
                                &w, &hgt, &subsamp, &cs);
    if (rc != 0) {
        tjDestroy(h);
        throw std::runtime_error(std::string("ImageFrameJpeg::toPixels: tjDecompressHeader3 failed: ")
                                 + tjGetErrorStr());
    }

    // Decide requested output channels based on format
    const std::string fmt = upper_(outPixelFormat.empty() ? "BGR" : outPixelFormat);
    int reqChannels = (fmt == "GRAY") ? 1 : 3;

    const int pf = tjPixelFormat_(fmt, reqChannels);
    outChannels = (pf == TJPF_GRAY) ? 1 : 3;

    std::vector<std::uint8_t> out(static_cast<size_t>(w) * hgt * outChannels);

    rc = tjDecompress2(h,
                       reinterpret_cast<const unsigned char*>(jpegBytes.data()),
                       static_cast<unsigned long>(jpegBytes.size()),
                       reinterpret_cast<unsigned char*>(out.data()),
                       w,
                       0,
                       hgt,
                       pf,
                       TJFLAG_FASTDCT);

    tjDestroy(h);

    if (rc != 0) {
        throw std::runtime_error(std::string("ImageFrameJpeg::toPixels: tjDecompress2 failed: ")
                                 + tjGetErrorStr());
    }

    outWidth  = w;
    outHeight = hgt;
    return out;
}

} // namespace magpie
#endif // MAGPIE_WITH_VIDEO
