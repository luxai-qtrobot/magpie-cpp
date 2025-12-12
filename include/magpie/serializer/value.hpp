#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <stdexcept>

namespace magpie {

/**
 * MagpieValue is a generic, serializer-agnostic "any" type that can represent
 * the same structures Python msgpack sees: None, bool, int, float, str, bytes,
 * list, dict.
 *
 * It's intentionally simple (no unions): we store all fields and interpret
 * based on `type_`.
 */
class Value {
public:
    enum class Type {
        Null,
        Bool,
        Int,
        Double,
        String,
        Binary,
        List,
        Dict
    };

    using List = std::vector<Value>;
    using Dict = std::map<std::string, Value>;

    // Constructors
    Value() : type_(Type::Null), boolValue_(false), intValue_(0), doubleValue_(0.0) {}
    static Value null() { return Value(); }

    static Value fromBool(bool v) {
        Value val;
        val.type_ = Type::Bool;
        val.boolValue_ = v;
        return val;
    }

    static Value fromInt(std::int64_t v) {
        Value val;
        val.type_ = Type::Int;
        val.intValue_ = v;
        return val;
    }

    static Value fromDouble(double v) {
        Value val;
        val.type_ = Type::Double;
        val.doubleValue_ = v;
        return val;
    }

    static Value fromString(const std::string& v) {
        Value val;
        val.type_ = Type::String;
        val.stringValue_ = v;
        return val;
    }

    static Value fromBinary(const std::vector<std::uint8_t>& v) {
        Value val;
        val.type_ = Type::Binary;
        val.binaryValue_ = v;
        return val;
    }

    static Value fromList(const List& v) {
        Value val;
        val.type_ = Type::List;
        val.listValue_ = v;
        return val;
    }

    static Value fromDict(const Dict& v) {
        Value val;
        val.type_ = Type::Dict;
        val.dictValue_ = v;
        return val;
    }

    Type type() const { return type_; }

    bool asBool() const {
        if (type_ != Type::Bool) throw std::runtime_error("Value: not a bool");
        return boolValue_;
    }

    std::int64_t asInt() const {
        if (type_ != Type::Int) throw std::runtime_error("Value: not an int");
        return intValue_;
    }

    double asDouble() const {
        if (type_ == Type::Double) return doubleValue_;
        if (type_ == Type::Int) return static_cast<double>(intValue_);
        throw std::runtime_error("Value: not a double");
    }

    const std::string& asString() const {
        if (type_ != Type::String) throw std::runtime_error("Value: not a string");
        return stringValue_;
    }

    const std::vector<std::uint8_t>& asBinary() const {
        if (type_ != Type::Binary) throw std::runtime_error("Value: not binary");
        return binaryValue_;
    }

    const List& asList() const {
        if (type_ != Type::List) throw std::runtime_error("Value: not a list");
        return listValue_;
    }

    const Dict& asDict() const {
        if (type_ != Type::Dict) throw std::runtime_error("Value: not a dict");
        return dictValue_;
    }

    List& asList() {
        if (type_ != Type::List) throw std::runtime_error("Value: not a list");
        return listValue_;
    }

    Dict& asDict() {
        if (type_ != Type::Dict) throw std::runtime_error("Value: not a dict");
        return dictValue_;
    }

    
    std::string toDebugString(int indent = 0) const;

private:
    Type type_;

    bool                      boolValue_;
    std::int64_t              intValue_;
    double                    doubleValue_;
    std::string               stringValue_;
    std::vector<std::uint8_t> binaryValue_;
    List                      listValue_;
    Dict                      dictValue_;
};

} // namespace magpie
