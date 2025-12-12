#include <magpie/frames/audio_frame.hpp>

#include <sstream>

namespace magpie {

// ---------------------------------------------------------------------------
// AudioFrameRaw
// ---------------------------------------------------------------------------

AudioFrameRaw::AudioFrameRaw() {
    name_ = "AudioFrameRaw";
}

AudioFrameRaw::AudioFrameRaw(std::vector<std::uint8_t> data,
                             int sampleRate,
                             int channels,
                             int bitDepth,
                             std::string format)
    : data_(std::move(data))
    , channels_(channels)
    , sampleRate_(sampleRate)
    , bitDepth_(bitDepth)
    , format_(std::move(format))
{
    name_ = "AudioFrameRaw";
}

void AudioFrameRaw::toDict(Dict& out) const {
    Frame::toDict(out);

    // Match Python keys for interoperability
    out["channels"]    = Value::fromInt(channels_);
    out["sample_rate"] = Value::fromInt(sampleRate_);
    out["bit_depth"]   = Value::fromInt(bitDepth_);
    out["format"]      = Value::fromString(format_);
    out["data"]        = Value::fromBinary(data_);
}

void AudioFrameRaw::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);

    auto it = dict.find("channels");
    if (it != dict.end() && it->second.type() == Value::Type::Int) {
        channels_ = static_cast<int>(it->second.asInt());
    }

    it = dict.find("sample_rate");
    if (it != dict.end() && it->second.type() == Value::Type::Int) {
        sampleRate_ = static_cast<int>(it->second.asInt());
    }

    it = dict.find("bit_depth");
    if (it != dict.end() && it->second.type() == Value::Type::Int) {
        bitDepth_ = static_cast<int>(it->second.asInt());
    }

    it = dict.find("format");
    if (it != dict.end() && it->second.type() == Value::Type::String) {
        format_ = it->second.asString();
    }

    it = dict.find("data");
    if (it != dict.end() && it->second.type() == Value::Type::Binary) {
        data_ = it->second.asBinary();
    }
}

std::string AudioFrameRaw::toString() const {
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_
        << "(sr=" << sampleRate_
        << ", ch=" << channels_
        << ", bd=" << bitDepth_
        << ", fmt=" << format_
        << ", bytes=" << data_.size()
        << ")";
    return oss.str();
}

AudioFrameRaw::Registrar::Registrar() {
    Frame::registerType("AudioFrameRaw", []() {
        return std::unique_ptr<Frame>(new AudioFrameRaw());
    });
}

std::unique_ptr<Frame> AudioFrameRaw::clone() const {
    return std::unique_ptr<Frame>(new AudioFrameRaw(*this));
}

AudioFrameRaw::Registrar AudioFrameRaw::registrar_;

} // namespace magpie
