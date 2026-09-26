#include "forgekv/benchmark/output.hpp"

namespace forgekv::benchmark {
namespace {

bool continuation(unsigned char byte) { return byte >= 0x80U && byte <= 0xbfU; }

std::size_t utf8_sequence_length(std::string_view value, std::size_t offset) {
    const auto byte = [&](std::size_t index) {
        return static_cast<unsigned char>(value[offset + index]);
    };
    const std::size_t remaining = value.size() - offset;
    const unsigned char lead = byte(0);
    if (lead <= 0x7fU) return 1;
    if (lead >= 0xc2U && lead <= 0xdfU) {
        return remaining >= 2 && continuation(byte(1)) ? 2 : 0;
    }
    if (remaining >= 3 &&
        ((lead == 0xe0U && byte(1) >= 0xa0U && byte(1) <= 0xbfU) ||
         ((lead >= 0xe1U && lead <= 0xecU) && continuation(byte(1))) ||
         (lead == 0xedU && byte(1) >= 0x80U && byte(1) <= 0x9fU) ||
         ((lead >= 0xeeU && lead <= 0xefU) && continuation(byte(1)))) &&
        continuation(byte(2))) {
        return 3;
    }
    if (remaining >= 4 &&
        ((lead == 0xf0U && byte(1) >= 0x90U && byte(1) <= 0xbfU) ||
         ((lead >= 0xf1U && lead <= 0xf3U) && continuation(byte(1))) ||
         (lead == 0xf4U && byte(1) >= 0x80U && byte(1) <= 0x8fU)) &&
        continuation(byte(2)) && continuation(byte(3))) {
        return 4;
    }
    return 0;
}

void append_byte_escape(std::string& output, unsigned char byte) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    output += "\\u00";
    output.push_back(kHexDigits[byte >> 4U]);
    output.push_back(kHexDigits[byte & 0x0fU]);
}

}  // namespace

std::string json_escape(std::string_view value) {
    std::string escaped;
    for (std::size_t index = 0; index < value.size();) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (byte >= 0x80U) {
            const std::size_t length = utf8_sequence_length(value, index);
            if (length == 0) {
                append_byte_escape(escaped, byte);
                ++index;
            } else {
                escaped.append(value.substr(index, length));
                index += length;
            }
            continue;
        }
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
                    append_byte_escape(escaped, byte);
                } else {
                    escaped.push_back(static_cast<char>(byte));
                }
                break;
        }
        ++index;
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
