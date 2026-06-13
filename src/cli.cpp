#include "monitor_switch/cli.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "monitor_switch/inputs.hpp"
#include "monitor_switch/version.hpp"

namespace monitor_switch {

namespace {

// Formats a VCP byte as "0x1b".
std::string format_hex(int code) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02x", code & 0xFF);
    return std::string(buffer);
}

// Quotes an argument for display the way shlex.join would, so dry-run output
// is copy-pasteable into a shell.
std::string shell_quote(const std::string& arg) {
    if (arg.empty()) {
        return "''";
    }
    bool safe = true;
    for (char c : arg) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
              c == '/' || c == '.' || c == ':' || c == '=' || c == ',' || c == '+')) {
            safe = false;
            break;
        }
    }
    if (safe) {
        return arg;
    }
    std::string quoted = "'";
    for (char c : arg) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

std::string join_command(const std::vector<std::string>& command) {
    std::string result;
    for (const std::string& part : command) {
        if (!result.empty()) {
            result += " ";
        }
        result += shell_quote(part);
    }
    return result;
}

// Parses a base-10 integer, requiring the whole string to be consumed.
bool parse_decimal(const std::string& text, int& out) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        int value = std::stoi(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

struct UsageError {
    std::string message;
};

[[noreturn]] void usage_error(const std::string& message) {
    throw UsageError{message};
}

int count_actions(const CliOptions& o) {
    return static_cast<int>(o.interactive) + static_cast<int>(o.current) +
           static_cast<int>(o.list_inputs) + static_cast<int>(o.detect) +
           static_cast<int>(o.doctor);
}

}  // namespace

std::string help_text() {
    std::ostringstream os;
    os << "usage: monitor-switch [OPTIONS] [INPUT]\n\n"
       << "Switch inputs on a DDC/CI-supported monitor.\n\n"
       << "Positional:\n"
       << "  INPUT                 input name, alias, or raw VCP value "
          "(e.g. usb-c, dp-1, 0x1b)\n\n"
       << "Actions (mutually exclusive):\n"
       << "  -i, --interactive, --menu  open the interactive ncurses menu "
          "(default with no arguments)\n"
       << "      --current         show the currently selected input\n"
       << "      --list, --list-inputs  list known input names and values\n"
       << "      --detect          list monitors visible to ddcutil\n"
       << "      --doctor          check ddcutil, detection, and DDC input access\n\n"
       << "Target selection:\n"
       << "  -d, --display NUMBER  ddcutil display number\n"
       << "  -b, --bus NUMBER      I2C bus number, without /dev/i2c-\n"
       << "      --monitor MODEL   model substring for automatic selection\n"
       << "      --serial SERIAL   monitor serial number for automatic selection\n\n"
       << "Other options:\n"
       << "      --dry-run         print the ddcutil command without changing the input\n"
       << "      --ddcutil PATH    ddcutil executable (default: ddcutil; "
          "env MONITOR_SWITCH_DDCUTIL)\n"
       << "      --version         print the application version\n"
       << "  -h, --help            show this help and exit\n\n"
       << "Examples: monitor-switch usb-c; monitor-switch dp-1; "
          "monitor-switch hdmi-2; monitor-switch --current\n";
    return os.str();
}

ParseResult parse_args(const std::vector<std::string>& args,
                       const std::string& ddcutil_default) {
    ParseResult result;
    CliOptions& options = result.options;
    options.ddcutil = ddcutil_default;
    bool input_set = false;

    try {
        // Fetches the value for an option, from "--opt=value", an attached
        // short form, or the next argument.
        auto take_value = [&](std::size_t& i, const std::string& arg,
                              const std::string& inline_value,
                              bool has_inline) -> std::string {
            if (has_inline) {
                return inline_value;
            }
            if (i + 1 >= args.size()) {
                usage_error("option " + arg + " requires a value");
            }
            return args[++i];
        };

        auto assign_int = [&](std::optional<int>& target, const std::string& arg,
                              const std::string& value, bool nonnegative) {
            int parsed = 0;
            if (!parse_decimal(value, parsed)) {
                usage_error("option " + arg + " requires an integer");
            }
            if (nonnegative ? parsed < 0 : parsed < 1) {
                usage_error("option " + arg + (nonnegative ? " must be zero or greater"
                                                           : " must be at least 1"));
            }
            target = parsed;
        };

        for (std::size_t i = 0; i < args.size(); ++i) {
            std::string arg = args[i];

            // Split "--name=value" forms.
            std::string inline_value;
            bool has_inline = false;
            std::string name = arg;
            if (arg.rfind("--", 0) == 0) {
                std::size_t eq = arg.find('=');
                if (eq != std::string::npos) {
                    name = arg.substr(0, eq);
                    inline_value = arg.substr(eq + 1);
                    has_inline = true;
                }
            }

            // Split attached short option values, e.g. "-d2".
            if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-') {
                name = arg.substr(0, 2);
                inline_value = arg.substr(2);
                has_inline = true;
            }

            // Rejects an attached value (from "--flag=value" or "-hfoo") on an
            // option that does not take one, instead of silently ignoring it.
            auto reject_inline_value = [&]() {
                if (has_inline) {
                    usage_error("option " + name + " does not take a value");
                }
            };

            if (name == "-h" || name == "--help") {
                reject_inline_value();
                result.status = ParseStatus::Help;
                result.message = help_text();
                return result;
            }
            if (name == "--version") {
                reject_inline_value();
                result.status = ParseStatus::Version;
                result.message = std::string("monitor-switch ") + kVersion + "\n";
                return result;
            }
            if (name == "-i" || name == "--interactive" || name == "--menu") {
                reject_inline_value();
                options.interactive = true;
            } else if (name == "--current") {
                reject_inline_value();
                options.current = true;
            } else if (name == "--list" || name == "--list-inputs") {
                reject_inline_value();
                options.list_inputs = true;
            } else if (name == "--detect") {
                reject_inline_value();
                options.detect = true;
            } else if (name == "--doctor") {
                reject_inline_value();
                options.doctor = true;
            } else if (name == "-d" || name == "--display") {
                assign_int(options.display, name,
                           take_value(i, name, inline_value, has_inline), false);
            } else if (name == "-b" || name == "--bus") {
                assign_int(options.bus, name,
                           take_value(i, name, inline_value, has_inline), true);
            } else if (name == "--monitor") {
                options.monitor = take_value(i, name, inline_value, has_inline);
            } else if (name == "--serial") {
                options.serial = take_value(i, name, inline_value, has_inline);
            } else if (name == "--ddcutil") {
                options.ddcutil = take_value(i, name, inline_value, has_inline);
            } else if (name == "--dry-run") {
                reject_inline_value();
                options.dry_run = true;
            } else if (arg == "--") {
                // Remaining args are positional.
                for (std::size_t j = i + 1; j < args.size(); ++j) {
                    if (input_set) {
                        usage_error("unexpected extra argument '" + args[j] + "'");
                    }
                    options.input = args[j];
                    input_set = true;
                }
                break;
            } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
                usage_error("unrecognized option '" + arg + "'");
            } else {
                if (input_set) {
                    usage_error("unexpected extra argument '" + arg + "'");
                }
                options.input = arg;
                input_set = true;
            }
        }

        if (options.display && options.bus) {
            usage_error("--display and --bus are mutually exclusive");
        }
        if (count_actions(options) > 1) {
            usage_error("only one action may be selected");
        }
        if (input_set && count_actions(options) > 0) {
            usage_error("INPUT cannot be combined with an action flag");
        }
        if (options.dry_run && !input_set) {
            usage_error("--dry-run requires INPUT");
        }

        // Default to the interactive interface when nothing else is requested.
        if (!input_set && count_actions(options) == 0) {
            options.interactive = true;
        }
    } catch (const UsageError& err) {
        result.status = ParseStatus::Error;
        result.message = err.message;
        return result;
    }

    result.status = ParseStatus::Ok;
    return result;
}

namespace {

std::string display_input(int code) {
    const InputSource* source = source_for_code(code);
    std::ostringstream os;
    if (source) {
        os << source->description << " (" << source->name << ", " << format_hex(code)
           << ")";
    } else {
        os << "unknown input (" << format_hex(code) << ")";
    }
    return os.str();
}

// Resolves the ddcutil target selector and a human label for it.
struct Target {
    Selector selector;
    std::optional<Display> display;
    std::string label;
};

Target resolve_target(const CliOptions& options, Ddcutil& client,
                      const std::vector<Display>* detected = nullptr) {
    if (options.display) {
        return {{"--display", std::to_string(*options.display)}, std::nullopt,
                "display " + std::to_string(*options.display)};
    }
    if (options.bus) {
        return {{"--bus", std::to_string(*options.bus)}, std::nullopt,
                "/dev/i2c-" + std::to_string(*options.bus)};
    }
    std::vector<Display> displays = detected ? *detected : client.detect();
    Display chosen = choose_display(displays, options.monitor, options.serial);
    // choose_display only returns DDC/CI-capable monitors, which always carry a
    // display number.
    return {{"--display", std::to_string(*chosen.index)}, chosen, chosen.label()};
}

// Builds a setvcp command for --dry-run without touching hardware. If no target
// filter was supplied, leave selection to ddcutil when the command is executed.
std::vector<std::string> dry_run_command(const CliOptions& options, Ddcutil& client,
                                         int code) {
    std::vector<std::string> arguments = {"setvcp", "60", format_hex(code)};
    if (options.display) {
        arguments.insert(arguments.end(), {"--display", std::to_string(*options.display)});
    } else if (options.bus) {
        arguments.insert(arguments.end(), {"--bus", std::to_string(*options.bus)});
    } else if (options.serial) {
        arguments.insert(arguments.end(), {"--sn", *options.serial});
    } else if (!options.monitor.empty()) {
        arguments.insert(arguments.end(), {"--model", options.monitor});
    }
    arguments.push_back("--noverify");
    return client.command(arguments);
}

void print_inputs(std::ostream& out) {
    out << "Known input values:\n";
    for (const InputSource& source : inputs()) {
        std::string aliases;
        for (const std::string& alias : source.aliases) {
            if (!aliases.empty()) {
                aliases += ", ";
            }
            aliases += alias;
        }
        std::ostringstream line;
        line << "  ";
        line.width(14);
        line << std::left << source.name << "  " << format_hex(source.code) << "  ";
        line.width(14);
        line << std::left << source.description << " aliases: " << aliases;
        out << line.str() << "\n";
    }
    out << "\nA raw one-byte VCP value such as 0x1b is also accepted.\n";
}

void print_detected(const std::vector<Display>& displays, std::ostream& out) {
    if (displays.empty()) {
        out << "No DDC/CI monitors detected.\n";
        return;
    }
    for (const Display& display : displays) {
        std::vector<std::string> fields;
        if (display.index) {
            fields.push_back("Display " + std::to_string(*display.index));
        } else {
            fields.push_back("Invalid display");
        }
        if (display.model && !display.model->empty()) {
            std::string identity;
            if (display.manufacturer) {
                identity = *display.manufacturer;
            }
            if (!identity.empty()) {
                identity += " ";
            }
            identity += *display.model;
            fields.push_back(identity);
        }
        if (display.serial && !display.serial->empty()) {
            fields.push_back("serial " + *display.serial);
        }
        if (display.bus) {
            fields.push_back("/dev/i2c-" + std::to_string(*display.bus));
        }
        if (display.connector && !display.connector->empty()) {
            fields.push_back(*display.connector);
        }
        if (!display.ddc_accessible) {
            fields.push_back("DDC/CI unavailable");
        }
        std::string line;
        for (const std::string& field : fields) {
            if (!line.empty()) {
                line += " | ";
            }
            line += field;
        }
        out << line << "\n";
    }
}

int run_doctor(const CliOptions& options, Ddcutil& client, std::ostream& out) {
    try {
        out << "[ok] " << client.version() << "\n";
    } catch (const DdcError& exc) {
        out << "[fail] " << exc.what() << "\n";
        return 1;
    }

    std::vector<Display> displays;
    try {
        displays = client.detect();
    } catch (const DdcError& exc) {
        out << "[fail] Monitor detection: " << friendly_ddc_error(exc.what()) << "\n";
        return 1;
    }

    if (displays.empty()) {
        out << "[fail] No monitors detected\n";
        return 1;
    }

    std::size_t accessible = 0;
    for (const Display& display : displays) {
        if (display.ddc_accessible) {
            ++accessible;
        }
    }
    out << "[ok] Detected " << displays.size() << " monitor(s); " << accessible
        << " with DDC/CI\n";
    for (const Display& display : displays) {
        out << "     " << display.label()
            << (display.ddc_accessible ? "" : " [DDC/CI unavailable]") << "\n";
    }
    if (accessible == 0) {
        out << "[fail] No monitor responded to DDC/CI\n";
        return 1;
    }

    try {
        Target target = resolve_target(options, client, &displays);
        out << "[ok] Selected " << target.label << "\n";
        client.verify_ddc(target.selector);
        out << "[ok] DDC/CI responded on the selected monitor\n";
        int code = client.get_input(target.selector);
        out << "[ok] Input source is " << display_input(code) << "\n";
    } catch (const DdcError& exc) {
        out << "[fail] " << friendly_ddc_error(exc.what()) << "\n";
        return 1;
    }

    return 0;
}

}  // namespace

int run(const CliOptions& options, Ddcutil& client, std::ostream& out,
        std::ostream& err, const MenuLauncher& menu) {
    // --list never touches hardware.
    if (options.list_inputs) {
        print_inputs(out);
        return 0;
    }

    // Reject invalid inputs before any hardware access.
    std::optional<ResolvedInput> resolved;
    if (options.input) {
        try {
            resolved = resolve_input(*options.input);
        } catch (const InputError& exc) {
            err << "error: " << exc.what() << "\n";
            return 1;
        }
    }

    try {
        if (options.interactive) {
            return menu(client, options, out);
        }
        if (options.detect) {
            print_detected(client.detect(), out);
            return 0;
        }
        if (options.doctor) {
            return run_doctor(options, client, out);
        }
        if (options.current) {
            Target target = resolve_target(options, client);
            int code = client.get_input(target.selector);
            out << "Current input: " << display_input(code) << "\n";
            return 0;
        }

        // Direct input change.
        const int code = resolved->code;
        const InputSource* source = resolved->source;

        // --dry-run must not touch hardware, so build the command from the
        // options alone instead of detecting and resolving a display.
        if (options.dry_run) {
            out << join_command(dry_run_command(options, client, code)) << "\n";
            return 0;
        }

        Target target = resolve_target(options, client);
        const std::string source_label = source ? source->description : "raw input";

        // Confirm the monitor responds to DDC/CI before writing, so an
        // unsupported or disabled monitor fails with a clear message instead of
        // a silently ignored switch.
        client.verify_ddc(target.selector);
        client.set_input(code, target.selector);
        out << "Switched " << target.label << " to " << source_label << " ("
            << format_hex(code) << ").\n";
        return 0;
    } catch (const DdcError& exc) {
        err << "error: " << friendly_ddc_error(exc.what()) << "\n";
        return 1;
    }
}

}  // namespace monitor_switch
