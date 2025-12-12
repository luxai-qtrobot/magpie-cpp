#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <magpie/serializer/value.hpp>

namespace magpie {


/**
 * Generic value serializer interface.
 * Value -> bytes, bytes -> Value.
 */
class Serializer {
public:
    virtual ~Serializer() = default;

    // Serialize a Value to a fresh byte buffer.
    virtual std::vector<std::uint8_t> serialize(const Value& value) = 0;

    // Deserialize bytes into a Value instance.
    virtual Value deserialize(const std::uint8_t* data, std::size_t size) = 0;
};

} // namespace magpie
