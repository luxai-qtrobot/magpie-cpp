#include <magpie/frames/image_frame.hpp>

#include <sstream>

namespace magpie {

// ---------------------------------------------------------------------------
// ImageFrameRaw
// ---------------------------------------------------------------------------

ImageFrameRaw::ImageFrameRaw() {
    name_ = "ImageFrameRaw";
}

ImageFrameRaw::ImageFrameRaw(std::vector<std::uint8_t> data,
                             std::string format,
                             int width,
                             int height,
                             int channels,
                             std::string pixelFormat)
    : data_(std::move(data))
    , format_(std::move(format))
    , width_(width)
    , height_(height)
    , channels_(channels)
    , pixelFormat_(std::move(pixelFormat))
{
    name_ = "ImageFrameRaw";
}

void ImageFrameRaw::toDict(Dict& out) const {
    Frame::toDict(out);

    out["data"]         = Value::fromBinary(data_);
    out["format"]       = Value::fromString(format_);
    out["width"]        = Value::fromInt(width_);
    out["height"]       = Value::fromInt(height_);
    out["channels"]     = Value::fromInt(channels_);
    out["pixel_format"] = Value::fromString(pixelFormat_);
}

void ImageFrameRaw::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);

    auto it = dict.find("data");
    if (it != dict.end() && it->second.type() == Value::Type::Binary) {
        data_ = it->second.asBinary();
    }

    it = dict.find("format");
    if (it != dict.end() && it->second.type() == Value::Type::String) {
        format_ = it->second.asString();
    }

    it = dict.find("width");
    if (it != dict.end() && it->second.type() == Value::Type::Int) {
        width_ = static_cast<int>(it->second.asInt());
    }

    it = dict.find("height");
    if (it != dict.end() && it->second.type() == Value::Type::Int) {
        height_ = static_cast<int>(it->second.asInt());
    }

    it = dict.find("channels");
    if (it != dict.end() && it->second.type() == Value::Type::Int) {
        channels_ = static_cast<int>(it->second.asInt());
    }

    it = dict.find("pixel_format");
    if (it != dict.end() && it->second.type() == Value::Type::String) {
        pixelFormat_ = it->second.asString();
    }
}

std::string ImageFrameRaw::toString() const {
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_
        << "(size=" << data_.size();

    if (width_ > 0 && height_ > 0) {
        oss << ", dims=" << width_ << "x" << height_ << "x" << channels_;
    } else {
        oss << ", dims=unknown";
    }

    oss << ", format=" << format_ << ")";
    return oss.str();
}

ImageFrameRaw::Registrar::Registrar() {
    Frame::registerType("ImageFrameRaw", []() {
        return std::unique_ptr<Frame>(new ImageFrameRaw());
    });
}

std::unique_ptr<Frame> ImageFrameRaw::clone() const {
    return std::unique_ptr<Frame>(new ImageFrameRaw(*this));
}

ImageFrameRaw::Registrar ImageFrameRaw::registrar_;

} // namespace magpie
