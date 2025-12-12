#pragma once


#include <vector>
#include <memory>
#include <msgpack.hpp>

#include <magpie/serializer/serializer.hpp>
#include <magpie/serializer/value.hpp>

namespace magpie {

/**
 * MsgpackSerializer
 *
 * Implements Frame <-> msgpack bytes using the same structure as Python:
 *     frame.to_dict() / Frame.from_dict(...)
 *
 * The top-level object is expected to be a dict with:
 *   "gid", "id", "name", "timestamp", and subclass-specific fields.
 */
class MsgpackSerializer : public Serializer {
public:
    MsgpackSerializer() = default;
    ~MsgpackSerializer() override = default;

    // Serialize a Value to a fresh byte buffer.
    virtual std::vector<std::uint8_t> serialize(const Value& value) override;

    // Deserialize bytes into a Value instance.
    virtual Value deserialize(const std::uint8_t* data, std::size_t size) override;


private:
    static void packValue(const Value& v, msgpack::packer<msgpack::sbuffer>& pk);
    static Value valueFromObject(const msgpack::object& obj);
};

} // namespace magpie
