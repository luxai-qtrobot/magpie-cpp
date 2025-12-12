#include <magpie/frames/audio_frame_flac.hpp>

#include <sstream>
#include <stdexcept>

namespace magpie {

// ---------------------------------------------------------------------------
// AudioFrameFlac
// ---------------------------------------------------------------------------

AudioFrameFlac::AudioFrameFlac() : AudioFrameRaw() {
    name_ = "AudioFrameFlac";
    setFormat("FLAC");
}

AudioFrameFlac::AudioFrameFlac(std::vector<std::uint8_t> flacBytes,
                               int sampleRate,
                               int channels,
                               int bitDepth)
    : AudioFrameRaw(std::move(flacBytes), sampleRate, channels, bitDepth, "FLAC")
{
    name_ = "AudioFrameFlac";
    setFormat("FLAC");
}

AudioFrameFlac AudioFrameFlac::fromPcm(const std::vector<std::uint8_t>& pcmBytes,
                                       int channels,
                                       int sampleRate,
                                       int bitDepth)
{
#ifndef MAGPIE_WITH_AUDIO
    (void)pcmBytes; (void)channels; (void)sampleRate; (void)bitDepth;
    throw std::runtime_error("AudioFrameFlac: built without MAGPIE_WITH_AUDIO (libFLAC disabled)");
#else
    std::vector<std::uint8_t> flac = encodeFlac_(pcmBytes, channels, sampleRate, bitDepth);
    return AudioFrameFlac(std::move(flac), sampleRate, channels, bitDepth);
#endif
}

std::vector<std::uint8_t> AudioFrameFlac::toPcm() {
#ifndef MAGPIE_WITH_AUDIO
    throw std::runtime_error("AudioFrameFlac: built without MAGPIE_WITH_AUDIO (libFLAC disabled)");
#else
    int outCh = channels();
    int outSr = sampleRate();
    int outBd = bitDepth();

    std::vector<std::uint8_t> pcm16 = decodeFlacToPcm16_(data(), outCh, outSr, outBd);

    // Match Python behavior: update metadata to decoded values and force PCM16.
    setChannels(outCh);
    setSampleRate(outSr);
    setBitDepth(outBd);
    setFormat("FLAC"); // frame remains FLAC; returned bytes are PCM

    return pcm16;
#endif
}

void AudioFrameFlac::toDict(Dict& out) const {
    // Same wire fields as AudioFrameRaw, but always format="FLAC"
    AudioFrameRaw::toDict(out);
    out["format"] = Value::fromString("FLAC");
}

void AudioFrameFlac::loadFromDict(const Dict& dict) {
    AudioFrameRaw::loadFromDict(dict);
    name_ = "AudioFrameFlac";
    setFormat("FLAC");
}

std::string AudioFrameFlac::toString() const {
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_
        << "(sr=" << sampleRate()
        << ", ch=" << channels()
        << ", bd=" << bitDepth()
        << ", fmt=FLAC"
        << ", bytes=" << data().size()
        << ")";
    return oss.str();
}

std::unique_ptr<Frame> AudioFrameFlac::clone() const {
    return std::unique_ptr<Frame>(new AudioFrameFlac(*this));
}

AudioFrameFlac::Registrar::Registrar() {
    Frame::registerType("AudioFrameFlac", []() {
        return std::unique_ptr<Frame>(new AudioFrameFlac());
    });
}
AudioFrameFlac::Registrar AudioFrameFlac::registrar_;

} // namespace magpie

// ---------------------------------------------------------------------------
// libFLAC implementation (only when enabled)
// ---------------------------------------------------------------------------
#ifdef MAGPIE_WITH_AUDIO

#include <FLAC/stream_decoder.h>
#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cstring>

namespace magpie {

namespace {

struct FlacEncodeCtx {
    std::vector<std::uint8_t> out;
};

FLAC__StreamEncoderWriteStatus flacWriteCb(const FLAC__StreamEncoder*,
                                           const FLAC__byte buffer[],
                                           size_t bytes,
                                           unsigned /*samples*/,
                                           unsigned /*current_frame*/,
                                           void* client_data)
{
    auto* ctx = static_cast<FlacEncodeCtx*>(client_data);
    ctx->out.insert(ctx->out.end(), buffer, buffer + bytes);
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

struct FlacDecodeCtx {
    const std::vector<std::uint8_t>* in = nullptr;
    size_t pos = 0;

    std::vector<FLAC__int32> decodedInterleaved; // store decoded samples as int32 interleaved
    int channels = 0;
    int sampleRate = 0;
    int bitDepth = 16;
};

FLAC__StreamDecoderReadStatus flacReadCb(const FLAC__StreamDecoder*,
                                        FLAC__byte buffer[],
                                        size_t* bytes,
                                        void* client_data)
{
    auto* ctx = static_cast<FlacDecodeCtx*>(client_data);
    if (*bytes == 0) return FLAC__STREAM_DECODER_READ_STATUS_ABORT;

    const size_t remaining = ctx->in->size() - ctx->pos;
    const size_t toCopy = std::min(*bytes, remaining);

    if (toCopy == 0) {
        *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }

    std::memcpy(buffer, ctx->in->data() + ctx->pos, toCopy);
    ctx->pos += toCopy;
    *bytes = toCopy;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus flacWritePcmCb(const FLAC__StreamDecoder*,
                                              const FLAC__Frame* frame,
                                              const FLAC__int32* const buffer[],
                                              void* client_data)
{
    auto* ctx = static_cast<FlacDecodeCtx*>(client_data);
    const int channels = frame->header.channels;
    const int blocksize = frame->header.blocksize;

    // Ensure interleaved output
    const size_t base = ctx->decodedInterleaved.size();
    ctx->decodedInterleaved.resize(base + static_cast<size_t>(channels) * blocksize);

    for (int i = 0; i < blocksize; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            ctx->decodedInterleaved[base + static_cast<size_t>(i) * channels + ch] = buffer[ch][i];
        }
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void flacMetaCb(const FLAC__StreamDecoder*,
                const FLAC__StreamMetadata* metadata,
                void* client_data)
{
    if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;

    auto* ctx = static_cast<FlacDecodeCtx*>(client_data);
    ctx->channels   = static_cast<int>(metadata->data.stream_info.channels);
    ctx->sampleRate = static_cast<int>(metadata->data.stream_info.sample_rate);
    ctx->bitDepth   = static_cast<int>(metadata->data.stream_info.bits_per_sample);
}

void flacErrorCb(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) {
    // We handle errors via return statuses; keep quiet here.
}

} // namespace

std::vector<std::uint8_t> AudioFrameFlac::encodeFlac_(const std::vector<std::uint8_t>& pcmBytes,
                                                      int channels,
                                                      int sampleRate,
                                                      int bitDepth)
{
    if (channels <= 0) throw std::runtime_error("AudioFrameFlac::fromPcm: channels must be > 0");
    if (sampleRate <= 0) throw std::runtime_error("AudioFrameFlac::fromPcm: sampleRate must be > 0");
    if (!(bitDepth == 16 || bitDepth == 32)) {
        throw std::runtime_error("AudioFrameFlac::fromPcm: only bitDepth 16 or 32 supported");
    }

    const int bytesPerSample = bitDepth / 8;
    const size_t frameBytes = static_cast<size_t>(channels) * bytesPerSample;
    if (frameBytes == 0) throw std::runtime_error("AudioFrameFlac::fromPcm: invalid parameters");

    const size_t numFrames = pcmBytes.size() / frameBytes; // truncate like python does
    if (numFrames == 0) return {};

    // Convert to FLAC__int32 interleaved samples (libFLAC expects int32)
    std::vector<FLAC__int32> samples;
    samples.resize(numFrames * static_cast<size_t>(channels));

    if (bitDepth == 16) {
        for (size_t i = 0; i < numFrames * static_cast<size_t>(channels); ++i) {
            std::int16_t s;
            std::memcpy(&s, &pcmBytes[i * 2], 2);
            samples[i] = static_cast<FLAC__int32>(s);
        }
    } else { // 32
        for (size_t i = 0; i < numFrames * static_cast<size_t>(channels); ++i) {
            std::int32_t s;
            std::memcpy(&s, &pcmBytes[i * 4], 4);
            samples[i] = static_cast<FLAC__int32>(s);
        }
    }

    FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
    if (!enc) throw std::runtime_error("AudioFrameFlac::fromPcm: FLAC encoder alloc failed");

    FlacEncodeCtx ctx;

    FLAC__stream_encoder_set_channels(enc, static_cast<unsigned>(channels));
    FLAC__stream_encoder_set_sample_rate(enc, static_cast<unsigned>(sampleRate));
    FLAC__stream_encoder_set_bits_per_sample(enc, static_cast<unsigned>(bitDepth));
    FLAC__stream_encoder_set_compression_level(enc, 5); // reasonable default

    auto initStatus = FLAC__stream_encoder_init_stream(enc,
                                                       flacWriteCb,
                                                       /*seek_cb=*/nullptr,
                                                       /*tell_cb=*/nullptr,
                                                       /*metadata_cb=*/nullptr,
                                                       &ctx);
    if (initStatus != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        FLAC__stream_encoder_delete(enc);
        throw std::runtime_error("AudioFrameFlac::fromPcm: FLAC encoder init failed");
    }

    const FLAC__bool ok = FLAC__stream_encoder_process_interleaved(
        enc,
        samples.data(),
        static_cast<unsigned>(numFrames)
    );

    FLAC__stream_encoder_finish(enc);
    FLAC__stream_encoder_delete(enc);

    if (!ok) {
        throw std::runtime_error("AudioFrameFlac::fromPcm: FLAC encode failed");
    }

    return std::move(ctx.out);
}

std::vector<std::uint8_t> AudioFrameFlac::decodeFlacToPcm16_(const std::vector<std::uint8_t>& flacBytes,
                                                             int& outChannels,
                                                             int& outSampleRate,
                                                             int& outBitDepth)
{
    if (flacBytes.empty()) {
        outBitDepth = 16;
        return {};
    }

    FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
    if (!dec) throw std::runtime_error("AudioFrameFlac::toPcm: FLAC decoder alloc failed");

    FlacDecodeCtx ctx;
    ctx.in = &flacBytes;

    auto initStatus = FLAC__stream_decoder_init_stream(dec,
                                                       flacReadCb,
                                                       /*seek_cb=*/nullptr,
                                                       /*tell_cb=*/nullptr,
                                                       /*length_cb=*/nullptr,
                                                       /*eof_cb=*/nullptr,
                                                       flacWritePcmCb,
                                                       flacMetaCb,
                                                       flacErrorCb,
                                                       &ctx);
    if (initStatus != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        FLAC__stream_decoder_delete(dec);
        throw std::runtime_error("AudioFrameFlac::toPcm: FLAC decoder init failed");
    }

    const FLAC__bool ok = FLAC__stream_decoder_process_until_end_of_stream(dec);

    FLAC__stream_decoder_finish(dec);
    FLAC__stream_decoder_delete(dec);

    if (!ok) {
        throw std::runtime_error("AudioFrameFlac::toPcm: FLAC decode failed");
    }

    // Update metadata from STREAMINFO
    outChannels   = (ctx.channels > 0) ? ctx.channels : outChannels;
    outSampleRate = (ctx.sampleRate > 0) ? ctx.sampleRate : outSampleRate;

    // Python always returns PCM_16 (int16) in to_pcm()
    outBitDepth = 16;

    // Convert decoded int32 samples to int16 bytes (clamp)
    std::vector<std::uint8_t> pcm16;
    pcm16.resize(ctx.decodedInterleaved.size() * 2);

    for (size_t i = 0; i < ctx.decodedInterleaved.size(); ++i) {
        const FLAC__int32 s32 = ctx.decodedInterleaved[i];
        const FLAC__int32 clamped = std::max<FLAC__int32>(-32768, std::min<FLAC__int32>(32767, s32));
        const std::int16_t s16 = static_cast<std::int16_t>(clamped);
        std::memcpy(&pcm16[i * 2], &s16, 2);
    }

    return pcm16;
}

} // namespace magpie

#endif // MAGPIE_WITH_AUDIO
