#include "monitor_switch/ddc.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <regex>
#include <sstream>
#include <utility>

namespace monitor_switch {

namespace {

std::string trim(const std::string& value) {
    std::size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string casefold(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string format_hex_byte(int code) {
    std::array<char, 8> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "0x%02x", code & 0xFF);
    return std::string(buffer.data());
}

// Actionable next steps when a monitor does not respond to DDC/CI.
const char* kDdcHint =
    "Enable DDC/CI in the monitor's on-screen menu, load the i2c-dev kernel "
    "module ('sudo modprobe i2c-dev'), and re-run 'ddcutil detect'.";

bool contains_ci(const std::string& haystack, const std::string& needle) {
    return casefold(haystack).find(casefold(needle)) != std::string::npos;
}

}  // namespace

std::string friendly_ddc_error(const std::string& message) {
    // Appends a hint only when it is not already present, so wrapping an
    // already-friendly message is a no-op.
    auto with_hint = [&](const std::string& hint) {
        if (message.find(hint) != std::string::npos) {
            return message;
        }
        return message + "\n" + hint;
    };

    if (contains_ci(message, "communication failed") ||
        contains_ci(message, "ddc null response") ||
        contains_ci(message, "did not respond to ddc")) {
        return with_hint(kDdcHint);
    }
    if (contains_ci(message, "no /dev/i2c") || contains_ci(message, "i2c-dev")) {
        return with_hint(
            "Load the kernel module with 'sudo modprobe i2c-dev', then check "
            "'ddcutil environment'.");
    }
    if (contains_ci(message, "permission denied") ||
        contains_ci(message, "permissions")) {
        return with_hint(
            "Grant your user access to /dev/i2c-* (usually through the i2c group).");
    }
    return message;
}

std::string Display::label() const {
    std::string identity;
    if (manufacturer && !manufacturer->empty()) {
        identity = *manufacturer;
    }
    if (model && !model->empty()) {
        if (!identity.empty()) {
            identity += " ";
        }
        identity += *model;
    }
    if (identity.empty()) {
        identity = "monitor";
    }

    std::vector<std::string> parts;
    if (index) {
        parts.push_back("display " + std::to_string(*index));
    }
    if (serial && !serial->empty()) {
        parts.push_back("serial " + *serial);
    }
    if (parts.empty()) {
        return identity;
    }

    std::string suffix = " (";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            suffix += ", ";
        }
        suffix += parts[i];
    }
    suffix += ")";
    return identity + suffix;
}

std::vector<Display> parse_detect_output(const std::string& output) {
    std::vector<Display> displays;

    // A block starts with either "Display N" (DDC/CI reachable) or "Invalid
    // display" (detected via EDID but not reachable over DDC/CI).
    const std::regex header_re(R"(^(?:Display\s+(\d+)|Invalid display)\s*$)",
                               std::regex::ECMAScript | std::regex::multiline);
    const std::regex bus_re(R"(^\s*I2C bus:\s*/dev/i2c-(\d+)\s*$)",
                            std::regex::ECMAScript | std::regex::multiline);
    const std::regex connector_re(R"(^\s*DRM connector:\s*(\S+)\s*$)",
                                  std::regex::ECMAScript | std::regex::multiline);
    const std::regex monitor_re(R"(^\s*Monitor:\s*([^:\n]*):([^:\n]*):(.*)\s*$)",
                                std::regex::ECMAScript | std::regex::multiline);
    const std::regex comm_failed_re(R"(DDC communication failed|DDC null response)",
                                    std::regex::ECMAScript | std::regex::icase);

    auto begin = std::sregex_iterator(output.begin(), output.end(), header_re);
    auto end = std::sregex_iterator();

    struct Start {
        std::size_t position;
        std::optional<int> index;
    };
    std::vector<Start> starts;
    for (auto it = begin; it != end; ++it) {
        std::optional<int> index;
        if ((*it)[1].matched) {
            index = std::stoi((*it)[1].str());
        }
        starts.push_back({static_cast<std::size_t>(it->position()), index});
    }

    for (std::size_t i = 0; i < starts.size(); ++i) {
        std::size_t block_begin = starts[i].position;
        std::size_t block_end =
            (i + 1 < starts.size()) ? starts[i + 1].position : output.size();
        const std::string block = output.substr(block_begin, block_end - block_begin);

        Display display;
        display.index = starts[i].index;
        // A monitor is DDC/CI-capable when it has a display number and does not
        // report a communication failure.
        display.ddc_accessible =
            starts[i].index.has_value() && !std::regex_search(block, comm_failed_re);

        std::smatch match;
        if (std::regex_search(block, match, bus_re)) {
            display.bus = std::stoi(match[1].str());
        }
        if (std::regex_search(block, match, connector_re)) {
            display.connector = match[1].str();
        }
        if (std::regex_search(block, match, monitor_re)) {
            display.manufacturer = trim(match[1].str());
            display.model = trim(match[2].str());
            display.serial = trim(match[3].str());
        }
        displays.push_back(std::move(display));
    }

    return displays;
}

int parse_input_value(const std::string& output) {
    static const std::array<std::regex, 3> patterns = {
        std::regex(R"(\bsl=(?:0x|x)([0-9a-fA-F]{1,4})\b)", std::regex::icase),
        std::regex(R"(\bcurrent value\s*=\s*(?:0x|x)([0-9a-fA-F]{1,4})\b)",
                   std::regex::icase),
        std::regex(R"(\bVCP\s+(?:0x)?60\s+\S+\s+(?:0x|x)([0-9a-fA-F]{1,4})\b)",
                   std::regex::icase),
    };

    std::smatch match;
    for (const std::regex& pattern : patterns) {
        if (std::regex_search(output, match, pattern)) {
            return static_cast<int>(std::stoul(match[1].str(), nullptr, 16));
        }
    }

    throw DdcError("could not parse input source from ddcutil output: " + trim(output));
}

std::vector<int> parse_input_capabilities(const std::string& output) {
    const std::size_t vcp_start = output.find("vcp(");
    if (vcp_start == std::string::npos) {
        return {};
    }

    std::size_t vcp_end = std::string::npos;
    int depth = 1;
    for (std::size_t i = vcp_start + 4; i < output.size(); ++i) {
        if (output[i] == '(') {
            ++depth;
        } else if (output[i] == ')' && --depth == 0) {
            vcp_end = i;
            break;
        }
    }
    if (vcp_end == std::string::npos) {
        return {};
    }

    const std::string vcp = output.substr(vcp_start + 4, vcp_end - vcp_start - 4);
    const std::regex input_re(R"((?:^|\s)60\s*\(([^()]*)\))",
                              std::regex::ECMAScript | std::regex::icase);
    std::smatch match;
    if (!std::regex_search(vcp, match, input_re)) {
        return {};
    }

    std::vector<int> values;
    std::istringstream stream(match[1].str());
    std::string token;
    while (stream >> token) {
        if (token.size() > 1 && (token[0] == 'x' || token[0] == 'X')) {
            token.erase(0, 1);
        } else if (token.size() > 2 && token[0] == '0' &&
                   (token[1] == 'x' || token[1] == 'X')) {
            token.erase(0, 2);
        }
        try {
            std::size_t consumed = 0;
            unsigned long value = std::stoul(token, &consumed, 16);
            if (consumed == token.size() && value <= 0xFF) {
                values.push_back(static_cast<int>(value));
            }
        } catch (const std::exception&) {
            // Ignore malformed values in an otherwise usable capabilities list.
        }
    }
    return values;
}

std::vector<Display> matching_displays(const std::vector<Display>& displays,
                                       const std::string& model,
                                       const std::optional<std::string>& serial) {
    std::vector<Display> candidates;
    const std::string wanted_model = casefold(model);
    const std::optional<std::string> wanted_serial =
        serial ? std::optional<std::string>(casefold(*serial)) : std::nullopt;

    for (const Display& display : displays) {
        if (!model.empty()) {
            if (!display.model || casefold(*display.model).find(wanted_model) ==
                                      std::string::npos) {
                continue;
            }
        }
        if (wanted_serial) {
            if (!display.serial || casefold(*display.serial) != *wanted_serial) {
                continue;
            }
        }
        candidates.push_back(display);
    }
    return candidates;
}

std::vector<Display> accessible_matches(const std::vector<Display>& displays,
                                        const std::string& model,
                                        const std::optional<std::string>& serial) {
    std::vector<Display> matched = matching_displays(displays, model, serial);

    std::vector<Display> accessible;
    std::vector<Display> inaccessible;
    for (const Display& display : matched) {
        (display.ddc_accessible ? accessible : inaccessible).push_back(display);
    }

    if (!accessible.empty()) {
        return accessible;
    }

    if (!inaccessible.empty()) {
        // A matching monitor exists but DDC/CI is not responding.
        throw DdcError("monitor " + inaccessible.front().label() +
                       " was detected but did not respond to DDC/CI\n" + kDdcHint);
    }

    std::string details;
    if (!model.empty()) {
        details = "model '" + model + "'";
    }
    if (serial) {
        if (!details.empty()) {
            details += " and ";
        }
        details += "serial '" + *serial + "'";
    }
    if (details.empty()) {
        throw DdcError(
            "no DDC/CI-capable monitor was detected; run monitor-switch --detect");
    }
    throw DdcError("no monitor matching " + details +
                   " was detected; run monitor-switch --detect");
}

Display choose_display(const std::vector<Display>& displays, const std::string& model,
                       const std::optional<std::string>& serial) {
    std::vector<Display> candidates = accessible_matches(displays, model, serial);

    if (candidates.size() > 1) {
        std::string numbers;
        for (const Display& display : candidates) {
            if (!numbers.empty()) {
                numbers += ", ";
            }
            numbers += std::to_string(display.index.value_or(0));
        }
        throw DdcError("multiple matching monitors were detected (displays " + numbers +
                       "); select one with --display or --serial");
    }
    return candidates.front();
}

Ddcutil::Ddcutil(std::string binary, ProcessRunner runner)
    : binary_(std::move(binary)), runner_(std::move(runner)) {}

std::vector<std::string> Ddcutil::command(
    const std::vector<std::string>& arguments) const {
    std::vector<std::string> result;
    result.reserve(arguments.size() + 1);
    result.push_back(binary_);
    result.insert(result.end(), arguments.begin(), arguments.end());
    return result;
}

std::string Ddcutil::run(const std::vector<std::string>& arguments) {
    const std::vector<std::string> argv = command(arguments);
    ProcessResult result = runner_(argv);
    if (!result.started) {
        throw DdcError(binary_ + " was not found; install ddcutil and try again");
    }
    if (result.exit_code != 0) {
        std::string details = trim(result.err);
        if (details.empty()) {
            details = trim(result.out);
        }
        if (details.empty()) {
            details = "unknown error";
        }
        throw DdcError("ddcutil failed: " + details);
    }
    return result.out;
}

std::string Ddcutil::version() {
    const std::string output = run({"--version"});
    std::istringstream stream(output);
    std::string first_line;
    std::getline(stream, first_line);
    first_line = trim(first_line);
    if (first_line.empty()) {
        return "ddcutil";
    }
    return first_line;
}

std::vector<Display> Ddcutil::detect() {
    return parse_detect_output(run({"detect", "--brief"}));
}

int Ddcutil::get_input(const Selector& selector) {
    return parse_input_value(
        run({"getvcp", "60", selector.first, selector.second, "--terse"}));
}

std::vector<int> Ddcutil::input_values(const Selector& selector) {
    return parse_input_capabilities(
        run({"capabilities", selector.first, selector.second, "--brief"}));
}

void Ddcutil::verify_ddc(const Selector& selector) {
    // Reaching feature 0x60 confirms DDC/CI is working; the value itself is not
    // needed here, so parsing is skipped.
    run({"getvcp", "60", selector.first, selector.second, "--terse"});
}

std::vector<std::string> Ddcutil::set_input_command(int code,
                                                    const Selector& selector) const {
    return command(
        {"setvcp", "60", format_hex_byte(code), selector.first, selector.second,
         "--noverify"});
}

std::vector<std::string> Ddcutil::set_input(int code, const Selector& selector) {
    const std::vector<std::string> arguments = {
        "setvcp", "60", format_hex_byte(code), selector.first, selector.second,
        "--noverify"};
    run(arguments);
    return command(arguments);
}

}  // namespace monitor_switch
