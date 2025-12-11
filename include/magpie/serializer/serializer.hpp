#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace magpie {

class Frame;

/**
 * Generic frame serializer interface.
 * Frame -> bytes, bytes -> Frame.
 */
class Serializer {
public:
    virtual ~Serializer() = default;

    // Serialize a Frame to a fresh byte buffer.
    virtual std::vector<std::uint8_t> serialize(const Frame& frame) = 0;

    // Deserialize bytes into a new Frame instance.
    virtual std::unique_ptr<Frame> deserialize(const std::uint8_t* data,
                                               std::size_t size) = 0;
};

} // namespace magpie
