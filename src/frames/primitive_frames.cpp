#include <magpie/frames/primitive_frames.hpp>

#include <magpie/utils/logger.hpp>

#include <sstream>

namespace magpie{

// ---------------------------------------------------------------------------
// BoolFrame
// ---------------------------------------------------------------------------

BoolFrame::BoolFrame() {
    name_ = "BoolFrame";
}

BoolFrame::BoolFrame(bool value)
    : value_{value} {
    name_ = "BoolFrame";
}

void BoolFrame::toDict(Dict& out) const {
    Frame::toDict(out);
    out["value"] = Value::fromBool(value_);
}

void BoolFrame::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);
    auto it = dict.find("value");
    if (it != dict.end()) {
        if (it->second.type() == Value::Type::Bool) {
            value_ = it->second.asBool();
        } else if (it->second.type() == Value::Type::Int) {
            value_ = (it->second.asInt() != 0);
        }
    }
}

std::string BoolFrame::toString() const {
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_ << "("
        << (value_ ? "True" : "False") << ")";
    return oss.str();
}

BoolFrame::Registrar::Registrar() {
    Frame::registerType("BoolFrame", []() {
        return std::unique_ptr<Frame>(new BoolFrame());
    });
}

std::unique_ptr<Frame> BoolFrame::clone() const {
    return std::unique_ptr<Frame>(new BoolFrame(*this));
}

BoolFrame::Registrar BoolFrame::registrar_;

// ---------------------------------------------------------------------------
// IntFrame
// ---------------------------------------------------------------------------

IntFrame::IntFrame() {
    name_ = "IntFrame";
}

IntFrame::IntFrame(std::int64_t value)
    : value_{value} {
    name_ = "IntFrame";
}

void IntFrame::toDict(Dict& out) const {
    Frame::toDict(out);
    out["value"] = Value::fromInt(value_);
}

void IntFrame::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);
    auto it = dict.find("value");
    if (it != dict.end()) {
        if (it->second.type() == Value::Type::Int) {
            value_ = it->second.asInt();
        } else if (it->second.type() == Value::Type::Double) {
            value_ = static_cast<std::int64_t>(it->second.asDouble());
        }
    }
}

std::string IntFrame::toString() const {
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_ << "(" << value_ << ")";
    return oss.str();
}

IntFrame::Registrar::Registrar() {
    Frame::registerType("IntFrame", []() {
        return std::unique_ptr<Frame>(new IntFrame());
    });
}

std::unique_ptr<Frame> IntFrame::clone() const {
    return std::unique_ptr<Frame>(new IntFrame(*this));
}

IntFrame::Registrar IntFrame::registrar_;

// ---------------------------------------------------------------------------
// FloatFrame
// ---------------------------------------------------------------------------

FloatFrame::FloatFrame() {
    name_ = "FloatFrame";
}

FloatFrame::FloatFrame(double value)
    : value_{value} {
    name_ = "FloatFrame";
}

void FloatFrame::toDict(Dict& out) const {
    Frame::toDict(out);
    out["value"] = Value::fromDouble(value_);
}

void FloatFrame::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);
    auto it = dict.find("value");
    if (it != dict.end()) {
        if (it->second.type() == Value::Type::Double) {
            value_ = it->second.asDouble();
        } else if (it->second.type() == Value::Type::Int) {
            value_ = static_cast<double>(it->second.asInt());
        }
    }
}

std::string FloatFrame::toString() const {
    std::ostringstream oss;
    oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
    oss.precision(6);
    oss << name_ << "#" << gid_ << ":" << id_ << "(" << value_ << ")";
    return oss.str();
}

FloatFrame::Registrar::Registrar() {
    Frame::registerType("FloatFrame", []() {
        return std::unique_ptr<Frame>(new FloatFrame());
    });
}


std::unique_ptr<Frame> FloatFrame::clone() const {
    return std::unique_ptr<Frame>(new FloatFrame(*this));
}

FloatFrame::Registrar FloatFrame::registrar_;

// ---------------------------------------------------------------------------
// StringFrame
// ---------------------------------------------------------------------------

StringFrame::StringFrame() {
    name_ = "StringFrame";
}

StringFrame::StringFrame(std::string value)
    : value_{std::move(value)} {
    name_ = "StringFrame";
}

void StringFrame::toDict(Dict& out) const {
    Frame::toDict(out);
    out["value"] = Value::fromString(value_);
}

void StringFrame::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);
    auto it = dict.find("value");
    if (it != dict.end() && it->second.type() == Value::Type::String) {
        value_ = it->second.asString();
    }
}

std::string StringFrame::toString() const {
    std::string v = value_;
    if (v.size() > 40) {
        v = v.substr(0, 37) + "...";
    }
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_ << "(\"" << v << "\")";
    return oss.str();
}

StringFrame::Registrar::Registrar() {
    Frame::registerType("StringFrame", []() {
        return std::unique_ptr<Frame>(new StringFrame());
    });
}

std::unique_ptr<Frame> StringFrame::clone() const {
    return std::unique_ptr<Frame>(new StringFrame(*this));
}

StringFrame::Registrar StringFrame::registrar_;

// ---------------------------------------------------------------------------
// BytesFrame
// ---------------------------------------------------------------------------

BytesFrame::BytesFrame() {
    name_ = "BytesFrame";
}

BytesFrame::BytesFrame(const std::vector<std::uint8_t>& value)
    : value_{value} {
    name_ = "BytesFrame";
}

void BytesFrame::toDict(Dict& out) const {
    Frame::toDict(out);
    out["value"] = Value::fromBinary(value_);
}

void BytesFrame::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);
    auto it = dict.find("value");
    if (it != dict.end() && it->second.type() == Value::Type::Binary) {
        value_ = it->second.asBinary();
    }
}

std::string BytesFrame::toString() const {
    std::ostringstream oss;
    oss << name_ << "#" << gid_ << ":" << id_
        << "(len=" << value_.size() << ")";
    return oss.str();
}

BytesFrame::Registrar::Registrar() {
    Frame::registerType("BytesFrame", []() {
        return std::unique_ptr<Frame>(new BytesFrame());
    });
}

std::unique_ptr<Frame> BytesFrame::clone() const {
    return std::unique_ptr<Frame>(new BytesFrame(*this));
}

BytesFrame::Registrar BytesFrame::registrar_;

// ---------------------------------------------------------------------------
// DictFrame
// ---------------------------------------------------------------------------

DictFrame::DictFrame() {
    name_ = "DictFrame";
}

DictFrame::DictFrame(const Dict& value)
    : value_{value} {
    name_ = "DictFrame";
}

void DictFrame::toDict(Dict& out) const {
    Frame::toDict(out);
    out["value"] = Value::fromDict(value_);
}

void DictFrame::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);
    auto it = dict.find("value");
    if (it != dict.end() && it->second.type() == Value::Type::Dict) {
        value_ = it->second.asDict();
    }
}

std::string DictFrame::toString() const {
    std::ostringstream oss;
    std::string preview = "{...}";
    // You can improve this by formatting a few entries from value_
    oss << name_ << "#" << gid_ << ":" << id_ << "(" << preview << ")";
    return oss.str();
}

DictFrame::Registrar::Registrar() {
    Frame::registerType("DictFrame", []() {
        return std::unique_ptr<Frame>(new DictFrame());
    });
}


std::unique_ptr<Frame> DictFrame::clone() const {
    return std::unique_ptr<Frame>(new DictFrame(*this));
}

DictFrame::Registrar DictFrame::registrar_;

// ---------------------------------------------------------------------------
// ListFrame
// ---------------------------------------------------------------------------

ListFrame::ListFrame() {
    name_ = "ListFrame";
}

ListFrame::ListFrame(const List& value)
    : value_{value} {
    name_ = "ListFrame";
}

void ListFrame::toDict(Dict& out) const {
    Frame::toDict(out);
    out["value"] = Value::fromList(value_);
}

void ListFrame::loadFromDict(const Dict& dict) {
    Frame::loadFromDict(dict);
    auto it = dict.find("value");
    if (it != dict.end() && it->second.type() == Value::Type::List) {
        value_ = it->second.asList();
    }
}

std::string ListFrame::toString() const {
    std::ostringstream oss;
    std::string preview = "[...]";
    // You can improve by showing some elements
    oss << name_ << "#" << gid_ << ":" << id_ << "(" << preview << ")";
    return oss.str();
}

ListFrame::Registrar::Registrar() {
    Frame::registerType("ListFrame", []() {
        return std::unique_ptr<Frame>(new ListFrame());
    });
}

std::unique_ptr<Frame> ListFrame::clone() const {
    return std::unique_ptr<Frame>(new ListFrame(*this));
}

ListFrame::Registrar ListFrame::registrar_;

} // namespace magpie
