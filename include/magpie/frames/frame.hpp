#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include <magpie/serializer/value.hpp>

namespace magpie {

/**
 * Base class for all frames.
 *
 * Mirrors Python Frame:
 *  - metadata fields: gid, id, name, timestamp
 *  - toDict() / fromDict() converting from/to a plain dict structure
 *  - registry keyed by "name" string to reconstruct subclass instances.
 */
class Frame {
public:
    using Dict    = Value::Dict;
    using Factory = std::function<std::unique_ptr<Frame>()>;

    Frame();
    virtual ~Frame() = default;

    const std::string& gid() const        { return gid_; }
    std::int64_t       id() const         { return id_; }
    const std::string& name() const       { return name_; }
    const std::string& timestamp() const  { return timestamp_; }

    void setGid(const std::string& g)        { gid_ = g; }
    void setId(std::int64_t id)             { id_ = id; }
    void setName(const std::string& n)      { name_ = n; }
    void setTimestamp(const std::string& t) { timestamp_ = t; }

    /**
     * Convert this frame into a Python-like dict.
     *
     * Base implementation fills common keys:
     *  - "gid"
     *  - "id"
     *  - "name"
     *  - "timestamp"
     *
     * Subclasses should override and:
     *   - call Frame::toDict(out) to fill base fields
     *   - then add their own keys (e.g. "value")
     */
    virtual void toDict(Dict& out) const;

    /**
     * Load fields from a dict. Base implementation reads:
     *  - "gid", "id", "name", "timestamp"
     *
     * Subclasses should override and:
     *   - call Frame::loadFromDict(dict) to load base fields
     *   - then load their own keys.
     */
    virtual void loadFromDict(const Dict& dict);

    /**
     *  clone the current Frame
     */
    virtual std::unique_ptr<Frame> clone() const = 0;

    /**
     * Polymorphic construction from a dict.
     * Looks up dict["name"] and uses the registry to create a subclass.
     * If no matching type is registered, returns nullptr.
     */
    static std::unique_ptr<Frame> fromDict(const Dict& dict);

    /**
     * Register a frame type under a given name (must match Python class name
     * used in "name" field).
     *
     * The factory is expected to create an "empty" instance; loadFromDict()
     * will then be called on it.
     */
    static void registerType(const std::string& typeName, Factory factory);
    static void unregisterType(const std::string& typeName);

protected:
    std::string gid_;
    std::int64_t id_;
    std::string name_;
    std::string timestamp_;

private:
    static std::unordered_map<std::string, Factory>& registry();
};

} // namespace magpie
