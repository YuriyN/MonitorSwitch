#include "monitor_switch/inputs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace monitor_switch {

const std::vector<InputSource>& inputs() {
    // Common VCP 0x60 values. Monitor firmware may support only a subset.
    static const std::vector<InputSource> catalog = {
        {"usb-c", 0x1B, {"usbc", "usb", "type-c", "typec"}, "USB Type-C"},
        {"dp-1", 0x0F, {"dp", "dp1", "displayport", "displayport-1", "display-port", "display-port-1"},"DisplayPort 1"},
        {"dp-2", 0x10, {"dp2", "displayport-2", "display-port-2"}, "DisplayPort 2"},
        {"hdmi-1", 0x11, {"hdmi", "hdmi1"}, "HDMI 1"},
        {"hdmi-2", 0x12, {"hdmi2"}, "HDMI 2"},
        {"auto", 0x13, {"auto-select", "automatic"}, "Auto Select"},
    };
    return catalog;
}

std::vector<InputSource> inputs_for_codes(const std::vector<int>& codes) {
    std::vector<InputSource> catalog;
    std::unordered_set<int> seen;
    for (int code : codes) {
        if (code < 0 || code > 0xFF || !seen.insert(code).second) {
            continue;
        }
        if (const InputSource* known = source_for_code(code)) {
            catalog.push_back(*known);
            continue;
        }

        char hex[8];
        std::snprintf(hex, sizeof(hex), "0x%02x", code);
        catalog.push_back(
            {std::string("input-") + hex, code, {}, std::string("Input ") + hex});
    }
    return catalog;
}

std::string normalize(const std::string& value) {
    // Trim leading/trailing whitespace.
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    std::string result;
    result.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        char c = value[i];
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '_') {
            c = '-';
        }
        result.push_back(c);
    }
    return result;
}

namespace {

const std::unordered_map<std::string, const InputSource*>& alias_map() {
    static const std::unordered_map<std::string, const InputSource*> map = [] {
        std::unordered_map<std::string, const InputSource*> result;
        for (const InputSource& source : inputs()) {
            result.emplace(normalize(source.name), &source);
            for (const std::string& alias : source.aliases) {
                result.emplace(normalize(alias), &source);
            }
        }
        return result;
    }();
    return map;
}

// Parses a decimal or hexadecimal integer, requiring the entire string to be
// consumed. Mirrors Python's int(value, 0) for the forms this tool accepts.
bool parse_int_auto(const std::string& text, long& out) {
    if (text.empty()) {
        return false;
    }
    int base = 10;
    std::size_t offset = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        offset = 0;  // strtol handles the 0x prefix itself.
    }
    errno = 0;
    char* parse_end = nullptr;
    const char* start = text.c_str() + offset;
    long value = std::strtol(start, &parse_end, base);
    if (errno != 0 || parse_end == start || *parse_end != '\0') {
        return false;
    }
    out = value;
    return true;
}

}  // namespace

const InputSource* source_for_code(int code) {
    for (const InputSource& source : inputs()) {
        if (source.code == code) {
            return &source;
        }
    }
    return nullptr;
}

ResolvedInput resolve_input(const std::string& value) {
    const std::string normalized = normalize(value);

    const auto& map = alias_map();
    auto it = map.find(normalized);
    if (it != map.end()) {
        return {it->second->code, it->second};
    }

    // Accept a bare "x1b" the same way "0x1b" is accepted.
    std::string raw = normalized;
    if (raw.size() >= 1 && raw[0] == 'x' && !(raw.size() >= 2 && raw[1] == 'x')) {
        raw = "0" + raw;
    }

    long code = 0;
    if (!parse_int_auto(raw, code)) {
        std::string choices;
        for (const InputSource& source : inputs()) {
            if (!choices.empty()) {
                choices += ", ";
            }
            choices += source.name;
        }
        throw InputError("unknown input '" + value + "'; choose " + choices +
                         " or provide a raw code such as 0x1b");
    }

    if (code < 0 || code > 0xFF) {
        throw InputError("input code must be between 0x00 and 0xff");
    }

    return {static_cast<int>(code), source_for_code(static_cast<int>(code))};
}

}  // namespace monitor_switch
