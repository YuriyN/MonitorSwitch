#pragma once

#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "monitor_switch/ddc.hpp"

namespace monitor_switch {

// Parsed command-line options after validation.
struct CliOptions {
    std::optional<std::string> input;
    bool interactive = false;
    bool current = false;
    bool list_inputs = false;
    bool detect = false;
    bool doctor = false;
    std::optional<int> display;
    std::optional<int> bus;
    std::string monitor;
    std::optional<std::string> serial;
    bool dry_run = false;
    std::string ddcutil = "ddcutil";
};

// Outcome category of argument parsing.
enum class ParseStatus { Ok, Error, Help, Version };

struct ParseResult {
    ParseStatus status = ParseStatus::Ok;
    CliOptions options;
    std::string message;  // help/version text, or an error description
};

// Parses argv (excluding the program name). `ddcutil_default` is the resolved
// default for --ddcutil (from MONITOR_SWITCH_DDCUTIL or "ddcutil").
ParseResult parse_args(const std::vector<std::string>& args,
                       const std::string& ddcutil_default);

// The help text shown for --help.
std::string help_text();

// Launches the interactive menu for the given options. Returns a process exit
// code. Implemented in tui.cpp (links ncurses).
using MenuLauncher = std::function<int(Ddcutil&, const CliOptions&, std::ostream&)>;

int launch_menu(Ddcutil& client, const CliOptions& options, std::ostream& out);

// Executes the chosen action. `menu` is injectable so dispatch can be tested
// without ncurses. Returns a process exit code.
int run(const CliOptions& options, Ddcutil& client, std::ostream& out,
        std::ostream& err, const MenuLauncher& menu);

// Full program entry: parses argv, resolves defaults, and dispatches.
int main_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace monitor_switch
