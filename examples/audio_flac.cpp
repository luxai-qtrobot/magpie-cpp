#include <magpie/frames/audio_frame_flac.hpp>
#include <magpie/utils/logger.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>
#include <cstring>


static void writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

int main() {
#ifndef MAGPIE_WITH_AUDIO
    magpie::Logger::error("MAGPIE_WITH_AUDIO is OFF. Reconfigure with -DMAGPIE_WITH_AUDIO=ON");
    return 1;
#else
    using namespace magpie;

    Logger::setLevel("INFO");

    const int sampleRate = 16000;
    const int channels   = 1;
    const int bitDepth   = 16;
    const double freqHz  = 440.0;
    const double seconds = 1.0;

    const int numSamples = static_cast<int>(sampleRate * seconds);

    // Generate mono PCM16 sine wave
    std::vector<std::uint8_t> pcm;
    pcm.resize(static_cast<size_t>(numSamples) * channels * 2);

    for (int i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double s = std::sin(2.0 * M_PI * freqHz * t);

        // scale to int16
        const std::int16_t v = static_cast<std::int16_t>(s * 0.6 * 32767.0);
        std::memcpy(&pcm[static_cast<size_t>(i) * 2], &v, 2);
    }

    Logger::info("Generated PCM bytes: " + std::to_string(pcm.size()));

    // Encode PCM -> FLAC frame
    AudioFrameFlac flac = AudioFrameFlac::fromPcm(pcm, channels, sampleRate, bitDepth);

    Logger::info("FLAC frame: bytes=" + std::to_string(flac.data().size()) +
                 ", sr=" + std::to_string(flac.sampleRate()) +
                 ", ch=" + std::to_string(flac.channels()) +
                 ", bd=" + std::to_string(flac.bitDepth()) +
                 ", fmt=" + flac.format());

    // Decode FLAC -> PCM16
    std::vector<std::uint8_t> pcm2 = flac.toPcm();

    Logger::info("Decoded PCM bytes: " + std::to_string(pcm2.size()) +
                 ", sr=" + std::to_string(flac.sampleRate()) +
                 ", ch=" + std::to_string(flac.channels()) +
                 ", bd=" + std::to_string(flac.bitDepth()));

    // Quick sanity check: same byte count (for 1ch, 16-bit, 1 sec)
    if (pcm2.size() != pcm.size()) {
        Logger::warning("Decoded size differs from original (ok sometimes, but check): " +
                        std::to_string(pcm.size()) + " -> " + std::to_string(pcm2.size()));
    }

    // Optionally dump raw PCM so you can inspect/play with external tools
    // (PCM S16LE, 16kHz, mono)
    writeFile("out_input.pcm", pcm);
    writeFile("out_decoded.pcm", pcm2);

    Logger::info("Wrote out_input.pcm and out_decoded.pcm (S16LE, 16kHz, mono)");

    // If you want a simple similarity check, compare first N samples
    const size_t n = std::min<size_t>(pcm.size(), pcm2.size());
    size_t diff = 0;
    for (size_t i = 0; i < n; ++i) {
        if (pcm[i] != pcm2[i]) diff++;
    }
    Logger::info("Byte differences (first " + std::to_string(n) + " bytes): " + std::to_string(diff));

    return 0;
#endif
}
