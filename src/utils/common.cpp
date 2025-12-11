#include <magpie/utils/common.hpp>

#include <chrono>
#include <random>

namespace magpie {

double getUtcTimestamp() {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto sinceEpoch = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::duration<double>>(sinceEpoch);
    return seconds.count();
}

std::string getUniqueId() {
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    static const std::size_t alphabetSize = sizeof(alphabet) - 1;

    static std::mt19937_64 rng{std::random_device{}()};
    static std::uniform_int_distribution<std::size_t> dist(0, alphabetSize - 1);

    std::string result;
    result.reserve(26);
    for (int i = 0; i < 26; ++i) {
        result.push_back(alphabet[dist(rng)]);
    }
    return result;
}

} // namespace magpie

