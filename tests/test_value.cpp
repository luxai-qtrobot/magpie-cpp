// test_value.cpp
//
// RTF plugin test for magpie::Value using RTF_TEST_CASE + RTF_CHECK_* exception macros.
//
// Assumes you have:
//   - external/rtf/macros.hpp  (RTF_TEST_CASE + RTF_CHECK_THROWS*, RTF_CHECK_NOTHROW)
//   - robottestingframework/TestAssert.h available
//   - Value.hpp in include path

#include "external/rtf/macros.hpp"

#include <magpie/serializer/value.hpp>

#include <cstdint>
#include <string>
#include <vector>

using magpie::Value;


RTF_TEST_CASE("Value") {

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("constructors / type() + correct getters");

    {
        Value v;
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Null, "default ctor is Null");
    }

    {
        Value v = Value::null();
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Null, "Value::null() is Null");
    }

    {
        Value v = Value::fromBool(true);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Bool, "fromBool sets Bool type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asBool() == true, "asBool returns stored bool");
    }

    {
        Value v = Value::fromBool(false);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asBool() == false, "asBool returns false when stored");
    }

    {
        Value v = Value::fromInt(-42);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Int, "fromInt sets Int type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asInt() == -42, "asInt returns stored int");
    }

    {
        Value v = Value::fromInt(0);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asInt() == 0, "asInt returns 0 when stored");
    }

    {
        Value v = Value::fromDouble(3.25);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Double, "fromDouble sets Double type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asDouble() == 3.25, "asDouble returns stored double");
    }

    {
        Value v = Value::fromDouble(-1.5);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asDouble() == -1.5, "asDouble returns negative stored double");
    }

    {
        Value v = Value::fromString("hello");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::String, "fromString sets String type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asString() == "hello", "asString returns stored string");
    }

    {
        std::string s;
        Value v = Value::fromString(s);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asString().empty(), "asString supports empty string");
    }

    {
        std::vector<std::uint8_t> bytes{0x00, 0x01, 0xFF};
        Value v = Value::fromBinary(bytes);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Binary, "fromBinary sets Binary type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asBinary().size() == bytes.size(), "asBinary size matches");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asBinary()[0] == 0x00, "binary[0] matches");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asBinary()[1] == 0x01, "binary[1] matches");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asBinary()[2] == 0xFF, "binary[2] matches");
    }

    {
        std::vector<std::uint8_t> empty;
        Value v = Value::fromBinary(empty);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asBinary().empty(), "asBinary supports empty vector");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("asDouble() conversion behavior");

    {
        Value vi = Value::fromInt(7);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(vi.type() == Value::Type::Int, "vi is Int");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(vi.asDouble() == 7.0, "asDouble() converts Int -> Double");
    }

    {
        Value vi = Value::fromInt(-8);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(vi.asDouble() == -8.0, "asDouble() converts negative Int -> Double");
    }

    {
        Value vd = Value::fromDouble(2.5);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(vd.asDouble() == 2.5, "asDouble() on Double returns same value");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("list / dict roundtrip + nested values");

    {
        Value::List lst;
        lst.push_back(Value::fromInt(1));
        lst.push_back(Value::fromString("two"));
        lst.push_back(Value::fromBool(false));

        Value v = Value::fromList(lst);

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::List, "fromList sets List type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asList().size() == 3, "asList size matches");

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asList()[0].asInt() == 1, "list[0] int");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asList()[1].asString() == "two", "list[1] string");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asList()[2].asBool() == false, "list[2] bool");
    }

    {
        // empty list
        Value v = Value::fromList(Value::List{});
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::List, "empty fromList sets List type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asList().empty(), "empty list roundtrip");
    }

    {
        Value::Dict d;
        d["a"] = Value::fromInt(10);
        d["b"] = Value::fromString("bee");

        // nested list in dict
        Value::List inner;
        inner.push_back(Value::fromDouble(1.25));
        inner.push_back(Value::null());
        d["inner"] = Value::fromList(inner);

        Value v = Value::fromDict(d);

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Dict, "fromDict sets Dict type");

        const auto& dv = v.asDict();
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(dv.at("a").asInt() == 10, "dict['a'] int");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(dv.at("b").asString() == "bee", "dict['b'] string");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(dv.at("inner").type() == Value::Type::List, "dict['inner'] is List");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(dv.at("inner").asList().size() == 2, "inner list size");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(dv.at("inner").asList()[0].asDouble() == 1.25, "inner[0] double");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(dv.at("inner").asList()[1].type() == Value::Type::Null, "inner[1] null");
    }

    {
        // empty dict
        Value v = Value::fromDict(Value::Dict{});
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.type() == Value::Type::Dict, "empty fromDict sets Dict type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asDict().empty(), "empty dict roundtrip");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("mutable access (List& / Dict&) modifies underlying value");

    {
        Value v = Value::fromList(Value::List{Value::fromInt(1)});
        v.asList().push_back(Value::fromInt(2));
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asList().size() == 2, "push_back via asList() mutates");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asList()[1].asInt() == 2, "mutated element correct");
    }

    {
        Value v = Value::fromDict(Value::Dict{{"x", Value::fromBool(true)}});
        v.asDict()["y"] = Value::fromString("yes");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asDict().size() == 2, "insert via asDict() mutates");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v.asDict().at("y").asString() == "yes", "mutated entry correct");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("copy semantics preserve content (including nested)");

    {
        Value::Dict d;
        d["n"] = Value::fromInt(123);
        d["s"] = Value::fromString("abc");
        d["l"] = Value::fromList(Value::List{Value::fromBool(true), Value::fromDouble(2.0)});

        Value v1 = Value::fromDict(d);
        Value v2 = v1; // copy

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v2.type() == Value::Type::Dict, "copy keeps type Dict");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v2.asDict().at("n").asInt() == 123, "copy keeps dict int");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v2.asDict().at("s").asString() == "abc", "copy keeps dict string");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v2.asDict().at("l").asList().size() == 2, "copy keeps nested list");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v2.asDict().at("l").asList()[0].asBool() == true, "copy keeps nested bool");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(v2.asDict().at("l").asList()[1].asDouble() == 2.0, "copy keeps nested double");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("wrong-type access throws (and message matches)");

    {
        Value v = Value::null();

        RTF_CHECK_THROWS_MSG_CONTAINS(v.asInt(),    std::runtime_error, "not an int");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asBool(),   std::runtime_error, "not a bool");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asString(), std::runtime_error, "not a string");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asBinary(), std::runtime_error, "not binary");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asList(),   std::runtime_error, "not a list");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asDict(),   std::runtime_error, "not a dict");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asDouble(), std::runtime_error, "not a double");
    }

    {
        Value v = Value::fromString("x");

        RTF_CHECK_THROWS_MSG_CONTAINS(v.asInt(),    std::runtime_error, "not an int");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asBool(),   std::runtime_error, "not a bool");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asBinary(), std::runtime_error, "not binary");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asList(),   std::runtime_error, "not a list");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asDict(),   std::runtime_error, "not a dict");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asDouble(), std::runtime_error, "not a double");

        // correct access should not throw
        RTF_CHECK_NOTHROW(v.asString());
    }

    {
        Value v = Value::fromList(Value::List{});

        RTF_CHECK_THROWS_MSG_CONTAINS(v.asDict(),   std::runtime_error, "not a dict");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asString(), std::runtime_error, "not a string");
        RTF_CHECK_NOTHROW(v.asList());
    }

    {
        Value v = Value::fromDict(Value::Dict{});
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asList(),   std::runtime_error, "not a list");
        RTF_CHECK_THROWS_MSG_CONTAINS(v.asBinary(), std::runtime_error, "not binary");
        RTF_CHECK_NOTHROW(v.asDict());
    }

    {
        // asDouble() accepts Int and Double, but not others
        Value vi = Value::fromInt(99);
        Value vd = Value::fromDouble(99.5);
        Value vb = Value::fromBool(true);

        RTF_CHECK_NOTHROW(vi.asDouble());
        RTF_CHECK_NOTHROW(vd.asDouble());
        RTF_CHECK_THROWS_MSG_CONTAINS(vb.asDouble(), std::runtime_error, "not a double");
    }
    
}
