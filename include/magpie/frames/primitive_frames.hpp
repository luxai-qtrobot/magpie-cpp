#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <magpie/frames/frame.hpp>
#include <magpie/serializer/value.hpp>

namespace magpie {

// ---------------------------------------------------------------------------
// BoolFrame
// ---------------------------------------------------------------------------

class BoolFrame : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    BoolFrame();
    explicit BoolFrame(bool value);

    bool value() const { return value_; }
    void setValue(bool v) { value_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

    std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    bool value_{false};

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

// ---------------------------------------------------------------------------
// IntFrame
// ---------------------------------------------------------------------------

class IntFrame : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    IntFrame();
    explicit IntFrame(std::int64_t value);

    std::int64_t value() const { return value_; }
    void setValue(std::int64_t v) { value_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

    std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    std::int64_t value_{0};

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

// ---------------------------------------------------------------------------
// FloatFrame
// ---------------------------------------------------------------------------

class FloatFrame : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    FloatFrame();
    explicit FloatFrame(double value);

    double value() const { return value_; }
    void setValue(double v) { value_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

        std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    double value_{0.0};

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

// ---------------------------------------------------------------------------
// StringFrame
// ---------------------------------------------------------------------------

class StringFrame : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    StringFrame();
    explicit StringFrame(std::string value);

    const std::string& value() const { return value_; }
    void setValue(const std::string& v) { value_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

        std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    std::string value_;

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

// ---------------------------------------------------------------------------
// BytesFrame
// ---------------------------------------------------------------------------

class BytesFrame : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    BytesFrame();
    explicit BytesFrame(const std::vector<std::uint8_t>& value);

    const std::vector<std::uint8_t>& value() const { return value_; }
    void setValue(const std::vector<std::uint8_t>& v) { value_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

        std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    std::vector<std::uint8_t> value_;

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

// ---------------------------------------------------------------------------
// DictFrame
// ---------------------------------------------------------------------------

class DictFrame : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;

    DictFrame();
    explicit DictFrame(const Dict& value);

    const Dict& value() const { return value_; }
    Dict&       value()       { return value_; }
    void        setValue(const Dict& v) { value_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

        std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    Dict value_;

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

// ---------------------------------------------------------------------------
// ListFrame
// ---------------------------------------------------------------------------

class ListFrame : public Frame {
public:
    using Dict  = Frame::Dict;
    using Value = magpie::Value;
    using List  = Value::List;

    ListFrame();
    explicit ListFrame(const List& value);

    const List& value() const { return value_; }
    List&       value()       { return value_; }
    void        setValue(const List& v) { value_ = v; }

    void toDict(Dict& out) const override;
    void loadFromDict(const Dict& dict) override;

        std::string toString() const;
    std::unique_ptr<Frame> clone() const override;

private:
    List value_;

    struct Registrar {
        Registrar();
    };
    static Registrar registrar_;
};

} // namespace magpie
