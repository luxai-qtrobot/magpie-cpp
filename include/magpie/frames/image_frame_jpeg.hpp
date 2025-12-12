#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <magpie/frames/image_frame.hpp>
#include <magpie/serializer/value.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// ImageFrameJpeg
// ---------------------------------------------------------------------------

class ImageFrameJpeg : public ImageFrameRaw {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    ImageFrameJpeg();

    /**
     * @brief Construct from already-encoded JPEG bytes.
     *
     * @param jpegBytes    JPEG bitstream bytes.
     * @param width        Image width (optional but recommended).
     * @param height       Image height (optional but recommended).
     * @param channels     1 (GRAY) or 3 (RGB/BGR). (Optional but recommended.)
     * @param pixelFormat  Metadata about the pixel format you want to decode to
     *                     or the original pixel format ("BGR","RGB","GRAY").
     */
    ImageFrameJpeg(std::vector<std::uint8_t> jpegBytes,
                   int width,
                   int height,
                   int channels,
                   std::string pixelFormat = "BGR");

    /**
     * @brief Encode raw pixels into JPEG and create ImageFrameJpeg.
     *
     * @param pixels       Raw interleaved pixels, row-major (H*W*C bytes).
     * @param width        Image width.
     * @param height       Image height.
     * @param channels     1 (GRAY) or 3 (RGB/BGR).
     * @param pixelFormat  "BGR", "RGB", or "GRAY" (must match input pixels layout).
     * @param quality      JPEG quality (1..100).
     */
    static ImageFrameJpeg fromPixels(const std::vector<std::uint8_t>& pixels,
                                    int width,
                                    int height,
                                    int channels,
                                    const std::string& pixelFormat = "BGR",
                                    int quality = 80);

    /**
     * @brief Decode JPEG bytes to raw pixels.
     *
     * @param outPixelFormat Desired output format ("BGR","RGB","GRAY").
     *                       If empty: uses self.pixel_format when available,
     *                       otherwise defaults to "BGR".
     * @return Raw interleaved pixel buffer.
     */
    std::vector<std::uint8_t> toPixels(const std::string& outPixelFormat = "") const;

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

    std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
#ifdef MAGPIE_WITH_VIDEO
    static int tjPixelFormat_(const std::string& fmt, int channels);

    static std::vector<std::uint8_t> encodeJpeg_(const std::vector<std::uint8_t>& pixels,
                                                 int width,
                                                 int height,
                                                 int channels,
                                                 const std::string& pixelFormat,
                                                 int quality);

    static std::vector<std::uint8_t> decodeJpeg_(const std::vector<std::uint8_t>& jpegBytes,
                                                 int& outWidth,
                                                 int& outHeight,
                                                 int& outChannels,
                                                 const std::string& outPixelFormat);
#endif

private:
    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

} // namespace magpie
