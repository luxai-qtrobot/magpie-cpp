#include "external/rtf/macros.hpp"

#include <magpie/serializer/msgpack_serializer.hpp>
#include <magpie/serializer/value.hpp>

#include <msgpack.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using magpie::MsgpackSerializer;
using magpie::Value;




#define CHECK_ROUNDTRIP(ser, input, label)                                        \
    do {                                                                              \
        auto _rtf_bytes = (ser).serialize((input));                                   \
        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(                                     \
            !_rtf_bytes.empty(),                                                      \
            ::robottestingframework::Asserter::format("%s: empty buffer", (label))    \
        );                                                                            \
        auto _rtf_out = (ser).deserialize(_rtf_bytes.data(), _rtf_bytes.size());      \
        const bool _rtf_ok = valueEquals((input), _rtf_out);                          \
        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(_rtf_ok, (label));                   \
    } while (0)


namespace {

// Deep-ish equality for Value (enough for these tests)
bool valueEquals(const Value& a, const Value& b) {
    if (a.type() != b.type()) return false;

    switch (a.type()) {
    case Value::Type::Null:
        return true;
    case Value::Type::Bool:
        return a.asBool() == b.asBool();
    case Value::Type::Int:
        return a.asInt() == b.asInt();
    case Value::Type::Double:
        // serializer packs double; msgpack unpacks float/double as f64.
        // Exact compare is OK for our chosen test numbers.
        return a.asDouble() == b.asDouble();
    case Value::Type::String:
        return a.asString() == b.asString();
    case Value::Type::Binary:
        return a.asBinary() == b.asBinary();
    case Value::Type::List: {
        const auto& la = a.asList();
        const auto& lb = b.asList();
        if (la.size() != lb.size()) return false;
        for (size_t i = 0; i < la.size(); ++i) {
            if (!valueEquals(la[i], lb[i])) return false;
        }
        return true;
    }
    case Value::Type::Dict: {
        const auto& da = a.asDict();
        const auto& db = b.asDict();
        if (da.size() != db.size()) return false;
        auto ita = da.begin();
        auto itb = db.begin();
        for (; ita != da.end(); ++ita, ++itb) {
            if (ita->first != itb->first) return false;
            if (!valueEquals(ita->second, itb->second)) return false;
        }
        return true;
    }
    }
    return false;
}


// Build an EXT msgpack object: should deserialize to Null per implementation.
std::vector<std::uint8_t> makeExtPayload() {
    msgpack::sbuffer sbuf;
    msgpack::packer<msgpack::sbuffer> pk(&sbuf);

    const int8_t type_tag = 1;
    const char data[3] = {'a','b','c'};
    pk.pack_ext(sizeof(data), type_tag);
    pk.pack_ext_body(data, sizeof(data));

    return std::vector<std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(sbuf.data()),
        reinterpret_cast<const std::uint8_t*>(sbuf.data()) + sbuf.size());
}

}

RTF_TEST_CASE("MsgpackSerializer") {

    MsgpackSerializer ser;

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("deserialize() edge cases: null/empty/garbage");

    {
        // nullptr + size 0 => returns null()
        Value v = ser.deserialize(nullptr, 0);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Null,
                                         "nullptr,0 => Value::Null");
    }

    {
        // non-null pointer but size 0 => returns null()
        const std::uint8_t dummy = 0xAA;
        Value v = ser.deserialize(&dummy, 0);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Null,
                                         "ptr,0 => Value::Null");
    }

    {
        // Definitely invalid/truncated msgpack: str32 length=1 but no payload byte
        std::vector<std::uint8_t> truncated{
            0xDB,             // str32
            0x00, 0x00, 0x00, 0x01  // length = 1
            // missing 1 byte of string payload -> unpack should throw -> deserialize returns Null
        };

        Value v = ser.deserialize(truncated.data(), truncated.size());
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Null,
                                        "truncated msgpack => Value::Null");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("round-trip: primitive types");

    {
        CHECK_ROUNDTRIP(ser, Value::null(), "Null round-trip");
    }

    {
        CHECK_ROUNDTRIP(ser, Value::fromBool(true), "Bool(true) round-trip");
        CHECK_ROUNDTRIP(ser, Value::fromBool(false), "Bool(false) round-trip");
    }

    {
        CHECK_ROUNDTRIP(ser, Value::fromInt(0), "Int(0) round-trip");
        CHECK_ROUNDTRIP(ser, Value::fromInt(123), "Int(+123) round-trip");
        CHECK_ROUNDTRIP(ser, Value::fromInt(-123), "Int(-123) round-trip");

        // “edge-ish” values (still safe)
        CHECK_ROUNDTRIP(ser,
                       Value::fromInt(std::numeric_limits<std::int32_t>::max()),
                       "Int(int32 max) round-trip");
        CHECK_ROUNDTRIP(ser,
                       Value::fromInt(std::numeric_limits<std::int32_t>::min()),
                       "Int(int32 min) round-trip");
    }

    {
        CHECK_ROUNDTRIP(ser, Value::fromDouble(0.0), "Double(0.0) round-trip");
        CHECK_ROUNDTRIP(ser, Value::fromDouble(1.25), "Double(1.25) round-trip");
        CHECK_ROUNDTRIP(ser, Value::fromDouble(-3.5), "Double(-3.5) round-trip");
    }

    {
        CHECK_ROUNDTRIP(ser, Value::fromString(""), "String(empty) round-trip");
        CHECK_ROUNDTRIP(ser, Value::fromString("hello"), "String('hello') round-trip");

        // include a string with embedded null to ensure size-based handling
        std::string s("a\0b", 3);
        CHECK_ROUNDTRIP(ser, Value::fromString(s), "String(with NUL) round-trip");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("round-trip: binary and binary-vs-string distinction");

    {
        std::vector<std::uint8_t> empty;
        CHECK_ROUNDTRIP(ser, Value::fromBinary(empty), "Binary(empty) round-trip");
    }

    {
        std::vector<std::uint8_t> bytes{0x00, 0x01, 0xFF, 0x10};
        Value in = Value::fromBinary(bytes);
        auto buf = ser.serialize(in);
        Value out = ser.deserialize(buf.data(), buf.size());

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(out.type() == Value::Type::Binary,
                                         "Binary stays Binary after round-trip");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(out.asBinary() == bytes,
                                         "Binary payload preserved");
    }

    {
        // Ensure String encodes as STR and comes back as String, not Binary
        Value in = Value::fromString("abc");
        auto buf = ser.serialize(in);
        Value out = ser.deserialize(buf.data(), buf.size());

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(out.type() == Value::Type::String,
                                         "String stays String after round-trip");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(out.asString() == "abc",
                                         "String payload preserved");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("round-trip: list and dict (including nested)");

    {
        Value::List lst{
            Value::null(),
            Value::fromBool(true),
            Value::fromInt(7),
            Value::fromDouble(2.5),
            Value::fromString("x"),
            Value::fromBinary(std::vector<std::uint8_t>{1,2,3})
        };

        CHECK_ROUNDTRIP(ser, Value::fromList(lst), "List(mixed) round-trip");
    }

    {
        CHECK_ROUNDTRIP(ser, Value::fromList(Value::List{}), "List(empty) round-trip");
    }

    {
        Value::Dict d;
        d["a"] = Value::fromInt(1);
        d["b"] = Value::fromString("bee");
        d["c"] = Value::null();

        CHECK_ROUNDTRIP(ser, Value::fromDict(d), "Dict(simple) round-trip");
    }

    {
        CHECK_ROUNDTRIP(ser, Value::fromDict(Value::Dict{}), "Dict(empty) round-trip");
    }

    {
        // nested dict -> list -> dict
        Value::Dict root;

        Value::List innerList;
        innerList.push_back(Value::fromString("s"));
        innerList.push_back(Value::fromInt(-9));

        Value::Dict innerDict;
        innerDict["k"] = Value::fromDouble(9.75);
        innerDict["bin"] = Value::fromBinary(std::vector<std::uint8_t>{0xAA, 0xBB});

        innerList.push_back(Value::fromDict(innerDict));

        root["inner"] = Value::fromList(innerList);
        root["flag"] = Value::fromBool(false);

        CHECK_ROUNDTRIP(ser, Value::fromDict(root), "Nested dict/list round-trip");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("deserialize: EXT objects map to Null");

    {
        auto extBuf = makeExtPayload();
        Value v = ser.deserialize(extBuf.data(), extBuf.size());
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Null,
                                         "EXT => Value::Null");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("deserialize: map with non-string key should yield Null (conversion fails)");

    {
        // Build msgpack map with integer key: { 1 : "x" }
        msgpack::sbuffer sbuf;
        msgpack::packer<msgpack::sbuffer> pk(&sbuf);
        pk.pack_map(1);
        pk.pack_int(1);
        pk.pack(std::string("x"));

        std::vector<std::uint8_t> buf(
            reinterpret_cast<const std::uint8_t*>(sbuf.data()),
            reinterpret_cast<const std::uint8_t*>(sbuf.data()) + sbuf.size());

        // In valueFromObject: p->key.convert(key) will throw; deserialize catches and returns null
        Value v = ser.deserialize(buf.data(), buf.size());
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Null,
                                         "non-string map key => Value::Null");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("serialize: does not throw for supported Value types");

    {
        // serialize should be safe for any well-formed Value (it uses switch + asX guarded by type)
        RTF_CHECK_NOTHROW(ser.serialize(Value::null()));
        RTF_CHECK_NOTHROW(ser.serialize(Value::fromBool(true)));
        RTF_CHECK_NOTHROW(ser.serialize(Value::fromInt(1)));
        RTF_CHECK_NOTHROW(ser.serialize(Value::fromDouble(1.0)));
        RTF_CHECK_NOTHROW(ser.serialize(Value::fromString("ok")));
        RTF_CHECK_NOTHROW(ser.serialize(Value::fromBinary(std::vector<std::uint8_t>{1})));
        RTF_CHECK_NOTHROW(ser.serialize(Value::fromList(Value::List{})));
        RTF_CHECK_NOTHROW(ser.serialize(Value::fromDict(Value::Dict{})));
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("done");
}
