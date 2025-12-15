// test_frame.cpp
//
// RTF plugin tests for magpie::Frame.
//
// Covers:
//  - default constructor fields
//  - toDict() base keys + types
//  - loadFromDict() loads only when types match, ignores wrong types/missing keys
//  - registerType()/fromDict() success and failure paths
//  - factory returning nullptr
//  - registry override behavior
//
// Assumes you have:
//   - external/rtf/macros.hpp (RTF_TEST_CASE + RTF_CHECK_* macros)
//   - magpie headers available via linking to magpie::core in CMake

#include "external/rtf/macros.hpp"

#include <robottestingframework/TestAssert.h>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/value.hpp>

#include <memory>
#include <string>

using magpie::Frame;
using magpie::Value;

namespace {

// Concrete test frame so we can register and instantiate from Frame::fromDict()
class TestFrameA final : public Frame {
public:
    TestFrameA() { setName("TestFrameA"); }

    std::unique_ptr<Frame> clone() const override {
        return std::make_unique<TestFrameA>(*this);
    }

    // Add one custom field to ensure subclass load/to works in presence of base handling
    int extra_ = 0;

    void toDict(Dict& out) const override {
        Frame::toDict(out);
        out["extra"] = Value::fromInt(extra_);
    }

    void loadFromDict(const Dict& dict) override {
        Frame::loadFromDict(dict);
        auto it = dict.find("extra");
        if (it != dict.end() && it->second.type() == Value::Type::Int) {
            extra_ = static_cast<int>(it->second.asInt());
        }
    }
};

class TestFrameB final : public Frame {
public:
    TestFrameB() { setName("TestFrameB"); }
    std::unique_ptr<Frame> clone() const override {
        return std::make_unique<TestFrameB>(*this);
    }
};

} // namespace

RTF_TEST_CASE("Frame") {

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("default constructor sets base fields");

    {
        // Frame is abstract, but we can still test base ctor behavior via derived
        TestFrameA f;

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(!f.gid().empty(), "gid is non-empty");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.id() == 0, "id defaults to 0");
        // Base Frame ctor sets name_="Frame", but TestFrameA ctor overrides to "TestFrameA"
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.name() == "TestFrameA", "derived ctor can override name");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(!f.timestamp().empty(), "timestamp is non-empty string");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("toDict writes base keys with correct types");

    {
        TestFrameA f;
        f.setGid("gid-123");
        f.setId(42);
        f.setTimestamp("ts-999");
        f.extra_ = 7;

        Frame::Dict d;
        f.toDict(d);

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.find("gid") != d.end(), "dict has 'gid'");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.find("id") != d.end(), "dict has 'id'");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.find("name") != d.end(), "dict has 'name'");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.find("timestamp") != d.end(), "dict has 'timestamp'");

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("gid").type() == Value::Type::String, "'gid' is String");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("id").type() == Value::Type::Int, "'id' is Int");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("name").type() == Value::Type::String, "'name' is String");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("timestamp").type() == Value::Type::String, "'timestamp' is String");

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("gid").asString() == "gid-123", "gid value matches");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("id").asInt() == 42, "id value matches");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("name").asString() == "TestFrameA", "name value matches");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("timestamp").asString() == "ts-999", "timestamp value matches");

        // subclass key
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.find("extra") != d.end(), "dict has subclass key 'extra'");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("extra").type() == Value::Type::Int, "'extra' is Int");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(d.at("extra").asInt() == 7, "extra matches");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("loadFromDict loads only when types match; ignores wrong types");

    {
        TestFrameA f;
        f.setGid("orig");
        f.setId(1);
        f.setTimestamp("orig-ts");
        f.extra_ = 5;

        Frame::Dict d;
        d["gid"] = Value::fromString("new-gid");
        d["id"] = Value::fromInt(99);
        d["name"] = Value::fromString("TestFrameA");
        d["timestamp"] = Value::fromString("new-ts");
        d["extra"] = Value::fromInt(123);

        f.loadFromDict(d);

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.gid() == "new-gid", "gid loaded");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.id() == 99, "id loaded");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.name() == "TestFrameA", "name loaded");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.timestamp() == "new-ts", "timestamp loaded");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.extra_ == 123, "extra loaded (subclass)");
    }

    {
        TestFrameA f;
        f.setGid("keep-gid");
        f.setId(7);
        f.setName("TestFrameA");
        f.setTimestamp("keep-ts");
        f.extra_ = 77;

        // wrong types should be ignored by base implementation
        Frame::Dict d;
        d["gid"] = Value::fromInt(123);            // wrong type
        d["id"] = Value::fromString("nope");       // wrong type
        d["name"] = Value::fromInt(9);             // wrong type
        d["timestamp"] = Value::fromBool(true);    // wrong type
        d["extra"] = Value::fromString("nope");    // wrong type for subclass

        f.loadFromDict(d);

        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.gid() == "keep-gid", "gid unchanged on wrong type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.id() == 7, "id unchanged on wrong type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.name() == "TestFrameA", "name unchanged on wrong type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.timestamp() == "keep-ts", "timestamp unchanged on wrong type");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(f.extra_ == 77, "extra unchanged on wrong type");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("Frame::fromDict failure cases");

    {
        // missing name
        Frame::Dict d;
        d["id"] = Value::fromInt(1);
        auto p = Frame::fromDict(d);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p == nullptr, "fromDict returns nullptr when 'name' missing");
    }

    {
        // name not a string
        Frame::Dict d;
        d["name"] = Value::fromInt(123);
        auto p = Frame::fromDict(d);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p == nullptr, "fromDict returns nullptr when 'name' not string");
    }

    {
        // name string but type not registered
        Frame::Dict d;
        d["name"] = Value::fromString("DefinitelyNotRegistered");
        auto p = Frame::fromDict(d);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p == nullptr, "fromDict returns nullptr when type not registered");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("Frame::registerType + fromDict success path (base fields load)");

    {
        // Register TestFrameA
        Frame::registerType("TestFrameA", []() {
            return std::make_unique<TestFrameA>();
        });

        Frame::Dict d;
        d["gid"] = Value::fromString("gid-x");
        d["id"] = Value::fromInt(555);
        d["name"] = Value::fromString("TestFrameA");
        d["timestamp"] = Value::fromString("ts-x");
        d["extra"] = Value::fromInt(9);

        auto p = Frame::fromDict(d);

        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(p != nullptr, "fromDict returned non-null for registered type");

        // Verify base fields loaded into created frame
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p->gid() == "gid-x", "gid loaded into created frame");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p->id() == 555, "id loaded into created frame");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p->name() == "TestFrameA", "name loaded into created frame");
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p->timestamp() == "ts-x", "timestamp loaded into created frame");

        // Verify dynamic type + subclass field loaded
        auto* a = dynamic_cast<TestFrameA*>(p.get());
        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(a != nullptr, "dynamic_cast<TestFrameA*> succeeded");
        if (a) {
            ROBOTTESTINGFRAMEWORK_TEST_CHECK(a->extra_ == 9, "subclass field 'extra' loaded");
        }
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("fromDict: factory returns nullptr => nullptr");

    {
        Frame::registerType("NullFactoryFrame", []() -> std::unique_ptr<Frame> {
            return nullptr;
        });

        Frame::Dict d;
        d["name"] = Value::fromString("NullFactoryFrame");
        auto p = Frame::fromDict(d);
        ROBOTTESTINGFRAMEWORK_TEST_CHECK(p == nullptr, "fromDict returns nullptr when factory returns nullptr");
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("registerType override: later registration wins");

    {
        // Register name "SwapFrame" as A, then override as B
        Frame::registerType("SwapFrame", []() { return std::make_unique<TestFrameA>(); });
        Frame::registerType("SwapFrame", []() { return std::make_unique<TestFrameB>(); });

        Frame::Dict d;
        d["name"] = Value::fromString("SwapFrame");

        auto p = Frame::fromDict(d);

        ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(p != nullptr, "fromDict returned non-null for overridden registration");
        if (p) {
            // Since we overrode with TestFrameB, the dynamic cast to B should work
            auto* b = dynamic_cast<TestFrameB*>(p.get());
            ROBOTTESTINGFRAMEWORK_TEST_FAIL_IF_FALSE(b != nullptr, "override produces TestFrameB");
        }
    }

    ROBOTTESTINGFRAMEWORK_TEST_REPORT("done");
}
