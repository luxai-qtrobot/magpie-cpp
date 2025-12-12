#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <magpie/frames/frame.hpp>
#include <magpie/frames/audio_frame.hpp> 
#include <magpie/serializer/value.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// AudioFrameFlac (optional: MAGPIE_WITH_AUDIO)
// ---------------------------------------------------------------------------

class AudioFrameFlac : public AudioFrameRaw {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    AudioFrameFlac();

    AudioFrameFlac(std::vector<std::uint8_t> flacBytes,
                   int sampleRate = 16000,
                   int channels   = 1,
                   int bitDepth   = 16);

    /**
     * @brief Create a FLAC-compressed frame from raw PCM bytes.
     *
     * This matches Python AudioFrameFlac.from_pcm():
     *  - input is raw interleaved PCM bytes (int16 or int32)
     *  - result stores FLAC container bytes in data()
     */
    static AudioFrameFlac fromPcm(const std::vector<std::uint8_t>& pcmBytes,
                                  int channels,
                                  int sampleRate,
                                  int bitDepth = 16);

    /**
     * @brief Decode the FLAC payload back to raw PCM bytes.
     *
     * Matches Python AudioFrameFlac.to_pcm():
     *  - returns PCM16 bytes
     *  - updates sampleRate/channels/bitDepth accordingly
     */
    std::vector<std::uint8_t> toPcm();

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

    std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;

private:
#ifdef MAGPIE_WITH_AUDIO
    static std::vector<std::uint8_t> encodeFlac_(const std::vector<std::uint8_t>& pcmBytes,
                                                 int channels,
                                                 int sampleRate,
                                                 int bitDepth);

    static std::vector<std::uint8_t> decodeFlacToPcm16_(const std::vector<std::uint8_t>& flacBytes,
                                                        int& outChannels,
                                                        int& outSampleRate,
                                                        int& outBitDepth);
#endif
};

} // namespace magpie
