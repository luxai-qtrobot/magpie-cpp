#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/value.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// ImageFrameRaw
// ---------------------------------------------------------------------------

class ImageFrameRaw : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    ImageFrameRaw();

    ImageFrameRaw(std::vector<std::uint8_t> data,
                  std::string format = "raw",
                  int width = 0,
                  int height = 0,
                  int channels = 0,
                  std::string pixelFormat = "");

    const std::vector<std::uint8_t>& data() const { return data_; }
    std::vector<std::uint8_t>& data() { return data_; }
    void setData(std::vector<std::uint8_t> v) { data_ = std::move(v); }

    const std::string& format() const { return format_; }
    void setFormat(const std::string& v) { format_ = v; }

    int width() const { return width_; }
    int height() const { return height_; }
    int channels() const { return channels_; }

    void setWidth(int v) { width_ = v; }
    void setHeight(int v) { height_ = v; }
    void setChannels(int v) { channels_ = v; }

    const std::string& pixelFormat() const { return pixelFormat_; }
    void setPixelFormat(const std::string& v) { pixelFormat_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

    std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    std::vector<std::uint8_t> data_;
    std::string format_ = "raw";

    int width_    = 0;
    int height_   = 0;
    int channels_ = 0;

    std::string pixelFormat_;

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

} // namespace magpie
