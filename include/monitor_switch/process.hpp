#pragma once

#include <functional>
#include <string>
#include <vector>

namespace monitor_switch {

// The outcome of running a child process. `started` is false when the
// executable could not be launched (for example, not found on PATH).
struct ProcessResult {
    int exit_code = 0;
    std::string out;
    std::string err;
    bool started = true;
};

// A process runner abstraction so the DDC client can be tested without
// spawning real processes. The first element of argv is the executable.
using ProcessRunner =
    std::function<ProcessResult(const std::vector<std::string>& argv)>;

// Runs a child process without a shell, capturing stdout and stderr and
// preserving the exit status. Never throws; a launch failure is reported via
// ProcessResult::started == false.
ProcessResult run_process(const std::vector<std::string>& argv);

}  // namespace monitor_switch
