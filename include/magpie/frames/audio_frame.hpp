#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/value.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// AudioFrameRaw
// ---------------------------------------------------------------------------

class AudioFrameRaw : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    AudioFrameRaw();

    AudioFrameRaw(std::vector<std::uint8_t> data,
                  int sampleRate = 16000,
                  int channels   = 1,
                  int bitDepth   = 16,
                  std::string format = "PCM");

    const std::vector<std::uint8_t>& data() const { return data_; }
    std::vector<std::uint8_t>& data() { return data_; }
    void setData(const std::vector<std::uint8_t>& d) { data_ = d; }
    void setData(std::vector<std::uint8_t>&& d) { data_ = std::move(d); }

    int channels() const { return channels_; }
    int sampleRate() const { return sampleRate_; }
    int bitDepth() const { return bitDepth_; }
    const std::string& format() const { return format_; }

    void setChannels(int v) { channels_ = v; }
    void setSampleRate(int v) { sampleRate_ = v; }
    void setBitDepth(int v) { bitDepth_ = v; }
    void setFormat(const std::string& v) { format_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

    std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    std::vector<std::uint8_t> data_;
    int channels_    = 1;
    int sampleRate_  = 16000;
    int bitDepth_    = 16;
    std::string format_ = "PCM";

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

} // namespace magpie
