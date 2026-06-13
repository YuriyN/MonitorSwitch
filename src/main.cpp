#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "monitor_switch/cli.hpp"
#include "monitor_switch/ddc.hpp"

namespace monitor_switch {

// Full program entry: resolves defaults, parses argv, and dispatches. Lives in
// the executable (not the core library) because it wires up the ncurses menu.
int main_cli(const std::vector<std::string>& args, std::ostream& out,
             std::ostream& err) {
    std::string ddcutil_default = "ddcutil";
    if (const char* env = std::getenv("MONITOR_SWITCH_DDCUTIL")) {
        if (env[0] != '\0') {
            ddcutil_default = env;
        }
    }

    ParseResult parsed = parse_args(args, ddcutil_default);
    switch (parsed.status) {
        case ParseStatus::Help:
        case ParseStatus::Version:
            out << parsed.message;
            return 0;
        case ParseStatus::Error:
            err << "error: " << parsed.message << "\n";
            return 2;
        case ParseStatus::Ok:
            break;
    }

    Ddcutil client(parsed.options.ddcutil);
    return run(parsed.options, client, out, err, &launch_menu);
}

}  // namespace monitor_switch

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return monitor_switch::main_cli(args, std::cout, std::cerr);
}
