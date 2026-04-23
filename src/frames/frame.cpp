#include <magpie/frames/frame.hpp>
#include <magpie/utils/common.hpp>
#include <magpie/utils/logger.hpp>

namespace magpie {

Frame::Frame()
    : gid_{getUniqueId()}
    , id_{0}
    , name_{"Frame"}
    , timestamp_{std::to_string(getUtcTimestamp())}
{
}

void Frame::toDict(Dict& out) const {
    out["gid"]       = Value::fromString(gid_);
    out["id"]        = Value::fromInt(id_);
    out["name"]      = Value::fromString(name_);
    out["timestamp"] = Value::fromString(timestamp_);
}

void Frame::loadFromDict(const Dict& dict) {
    auto it = dict.find("gid");
    if (it != dict.end() && it->second.type() == Value::Type::String) {
        gid_ = it->second.asString();
    }

    it = dict.find("id");
    if (it != dict.end() && it->second.type() == Value::Type::Int) {
        id_ = it->second.asInt();
    }

    it = dict.find("name");
    if (it != dict.end() && it->second.type() == Value::Type::String) {
        name_ = it->second.asString();
    }

    it = dict.find("timestamp");
    if (it != dict.end() && it->second.type() == Value::Type::String) {
        timestamp_ = it->second.asString();
    }
}

std::unordered_map<std::string, Frame::Factory>& Frame::registry() {
    static std::unordered_map<std::string, Factory> r;
    return r;
}

void Frame::registerType(const std::string& typeName, Factory factory) {
    registry()[typeName] = std::move(factory);
}

void Frame::unregisterType(const std::string& typeName) {
    registry().erase(typeName);
}

std::unique_ptr<Frame> Frame::fromDict(const Dict& dict) {
    auto it = dict.find("name");
    if (it == dict.end() || it->second.type() != Value::Type::String) {
        Logger::warning("Frame::fromDict: 'name' field missing or not a string");
        return nullptr;
    }

    const std::string typeName = it->second.asString();
    auto& r = registry();
    auto ftIt = r.find(typeName);
    if (ftIt == r.end()) {
        Logger::warning("Frame::fromDict: no registered type for '" + typeName + "'");
        return nullptr;
    }

    auto frame = ftIt->second();  // create empty frame
    if (!frame) {
        Logger::warning("Frame::fromDict: factory returned null for '" + typeName + "'");
        return nullptr;
    }

    frame->loadFromDict(dict);
    return frame;
}

} // namespace magpie
