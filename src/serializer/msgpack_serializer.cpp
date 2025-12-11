#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/frames/frame.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

std::vector<std::uint8_t> MsgpackSerializer::serialize(const Frame& frame) {
    // 1) Convert frame -> dict
    Frame::Dict dict;
    frame.toDict(dict);

    // 2) Pack dict as msgpack
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);

    // dict must be the top-level object, matching Python
    pk.pack_map(dict.size());
    for (const auto& kv : dict) {
        pk.pack(kv.first);
        packValue(kv.second, pk);
    }

    return std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(sbuf.data()),
        reinterpret_cast<const std::uint8_t*>(sbuf.data()) + sbuf.size()
    );
}

std::unique_ptr<Frame> MsgpackSerializer::deserialize(const std::uint8_t* data,
                                                      std::size_t size) {
    if (!data || size == 0) {
        Logger::warning("MsgpackSerializer::deserialize called with empty buffer");
        return nullptr;
    }

    try {
        msgpack::object_handle oh = msgpack::unpack(
            reinterpret_cast<const char*>(data), size);
        msgpack::object obj = oh.get();

        if (obj.type != msgpack::type::MAP) {
            Logger::warning("MsgpackSerializer: top-level object is not a map");
            return nullptr;
        }

        // Convert msgpack MAP -> Value::Dict
        Value::Dict dict;
        auto* p = obj.via.map.ptr;
        auto* e = obj.via.map.ptr + obj.via.map.size;

        for (; p != e; ++p) {
            std::string key;
            p->key.convert(key);
            dict[key] = valueFromObject(p->val);
        }

        // Delegate to Frame::fromDict to get the right subclass
        return Frame::fromDict(dict);
    } catch (const std::exception& e) {
        Logger::warning(std::string("MsgpackSerializer::deserialize error: ") + e.what());
        return nullptr;
    } catch (...) {
        Logger::warning("MsgpackSerializer::deserialize unknown error");
        return nullptr;
    }
}

// ---------- value <-> msgpack mapping ----------

void MsgpackSerializer::packValue(const Value& v,
                                  msgpack::packer<msgpack::sbuffer>& pk) {
    switch (v.type()) {
        case Value::Type::Null:
            pk.pack_nil();
            break;
        case Value::Type::Bool:
            pk.pack(v.asBool());
            break;
        case Value::Type::Int:
            pk.pack(v.asInt());
            break;
        case Value::Type::Double:
            pk.pack(v.asDouble());
            break;
        case Value::Type::String:
            pk.pack(v.asString());
            break;
        case Value::Type::Binary: {
            const auto& bin = v.asBinary();
            pk.pack_bin(bin.size());
            pk.pack_bin_body(reinterpret_cast<const char*>(bin.data()), bin.size());
            break;
        }
        case Value::Type::List: {
            const auto& list = v.asList();
            pk.pack_array(list.size());
            for (const auto& elem : list) {
                packValue(elem, pk);
            }
            break;
        }
        case Value::Type::Dict: {
            const auto& dict = v.asDict();
            pk.pack_map(dict.size());
            for (const auto& kv : dict) {
                pk.pack(kv.first);
                packValue(kv.second, pk);
            }
            break;
        }
    }
}

Value MsgpackSerializer::valueFromObject(const msgpack::object& obj) {
    using T = msgpack::type::object_type;

    switch (obj.type) {
        case T::NIL:
            return Value::null();
        case T::BOOLEAN:
            return Value::fromBool(obj.via.boolean);
        case T::POSITIVE_INTEGER:
            return Value::fromInt(static_cast<std::int64_t>(obj.via.u64));
        case T::NEGATIVE_INTEGER:
            return Value::fromInt(static_cast<std::int64_t>(obj.via.i64));
        case T::FLOAT32:
        case T::FLOAT64:
            return Value::fromDouble(obj.via.f64);
        case T::STR: {
            std::string s(obj.via.str.ptr, obj.via.str.size);
            return Value::fromString(s);
        }
        case T::BIN: {
            std::vector<std::uint8_t> bin(
                reinterpret_cast<const std::uint8_t*>(obj.via.bin.ptr),
                reinterpret_cast<const std::uint8_t*>(obj.via.bin.ptr) + obj.via.bin.size);
            return Value::fromBinary(bin);
        }
        case T::ARRAY: {
            Value::List list;
            list.reserve(obj.via.array.size);
            for (uint32_t i = 0; i < obj.via.array.size; ++i) {
                list.push_back(valueFromObject(obj.via.array.ptr[i]));
            }
            return Value::fromList(list);
        }
        case T::MAP: {
            Value::Dict dict;
            auto* p = obj.via.map.ptr;
            auto* e = obj.via.map.ptr + obj.via.map.size;
            for (; p != e; ++p) {
                std::string key;
                p->key.convert(key);
                dict[key] = valueFromObject(p->val);
            }
            return Value::fromDict(dict);
        }
        case T::EXT:
        default:
            // For now, map EXT or unknown types to null.
            return Value::null();
    }
}

} // namespace magpie
