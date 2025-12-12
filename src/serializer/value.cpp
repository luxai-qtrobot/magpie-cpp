#include <magpie/serializer/value.hpp>

#include <sstream>

namespace magpie {

static std::string indentStr(int n) {
    return std::string(n, ' ');
}

std::string Value::toDebugString(int indent) const {
    std::ostringstream oss;
    const std::string pad = indentStr(indent);

    switch (type()) {
        case Type::Null:
            oss << "null";
            break;

        case Type::Bool:
            oss << (asBool() ? "true" : "false");
            break;

        case Type::Int:
            oss << asInt();
            break;

        case Type::Double:
            oss << asDouble();
            break;

        case Type::String:
            oss << "\"" << asString() << "\"";
            break;

        case Type::Binary:
            oss << "<binary:" << asBinary().size() << " bytes>";
            break;

        case Type::List: {
            oss << "[\n";
            const auto& list = asList();
            for (std::size_t i = 0; i < list.size(); ++i) {
                oss << pad << "  "
                    << list[i].toDebugString(indent + 2);
                if (i + 1 < list.size()) oss << ",";
                oss << "\n";
            }
            oss << pad << "]";
            break;
        }

        case Type::Dict: {
            oss << "{\n";
            const auto& dict = asDict();
            std::size_t i = 0;
            for (const auto& kv : dict) {
                oss << pad << "  "
                    << "\"" << kv.first << "\": "
                    << kv.second.toDebugString(indent + 2);
                if (++i < dict.size()) oss << ",";
                oss << "\n";
            }
            oss << pad << "}";
            break;
        }
    }

    return oss.str();
}

} // namespace magpie
