#include "forgekv/benchmark/output.hpp"

namespace forgekv::benchmark {

std::string json_escape(std::string_view value) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string escaped;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (byte < 0x20U) {
                    escaped += "\\u00";
                    escaped.push_back(kHexDigits[byte >> 4U]);
                    escaped.push_back(kHexDigits[byte & 0x0fU]);
                } else {
                    escaped.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    return escaped;
}

std::string csv_escape(std::string_view value) {
    if (value.find_first_of(",\"\r\n") == std::string_view::npos) return std::string(value);
    std::string escaped{"\""};
    for (const char character : value) {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

}  // namespace forgekv::benchmark
