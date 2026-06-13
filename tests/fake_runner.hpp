#pragma once

#include <string>
#include <vector>

#include "monitor_switch/process.hpp"

namespace mstest {

// Records invocations and replays a queue of canned results.
struct FakeRunner {
    std::vector<std::vector<std::string>> calls;
    std::vector<monitor_switch::ProcessResult> results;
    std::size_t next = 0;

    monitor_switch::ProcessResult operator()(
        const std::vector<std::string>& argv) {
        calls.push_back(argv);
        if (next < results.size()) {
            return results[next++];
        }
        return monitor_switch::ProcessResult{0, "", "", true};
    }
};

inline monitor_switch::ProcessResult ok_result(std::string out) {
    return monitor_switch::ProcessResult{0, std::move(out), "", true};
}

inline monitor_switch::ProcessResult fail_result(std::string err) {
    return monitor_switch::ProcessResult{1, "", std::move(err), true};
}

inline monitor_switch::ProcessResult not_found_result() {
    return monitor_switch::ProcessResult{0, "", "", false};
}

}  // namespace mstest
