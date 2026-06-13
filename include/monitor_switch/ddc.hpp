#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "monitor_switch/process.hpp"

namespace monitor_switch {

// Raised when ddcutil cannot complete an operation.
class DdcError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

// A monitor detected by `ddcutil detect --brief`.
struct Display {
    // ddcutil only assigns a display number to monitors it can reach over
    // DDC/CI, so an empty index means DDC/CI is unavailable for this monitor.
    std::optional<int> index;
    std::optional<int> bus;
    std::optional<std::string> connector;
    std::optional<std::string> manufacturer;
    std::optional<std::string> model;
    std::optional<std::string> serial;

    // False when the monitor was detected (EDID readable) but did not respond
    // to DDC/CI ("Invalid display" or "DDC communication failed").
    bool ddc_accessible = true;

    // A human-readable identity such as "DEL Display Model (display 1, serial X)".
    std::string label() const;
};

// A ddcutil target selector, e.g. {"--display", "1"} or {"--bus", "4"}.
using Selector = std::pair<std::string, std::string>;

// Parses `ddcutil detect --brief` output into display records.
std::vector<Display> parse_detect_output(const std::string& output);

// Extracts the VCP 0x60 input value from getvcp output. Throws DdcError when
// no recognized value is present.
int parse_input_value(const std::string& output);

// Extracts the values advertised for VCP feature 0x60 from a raw monitor
// capabilities string. Returns an empty list when the feature is not declared.
std::vector<int> parse_input_capabilities(const std::string& output);

// Returns displays matching the model substring (case-insensitive) and, when
// supplied, an exact case-insensitive serial. Includes DDC/CI-incapable
// monitors so callers can report them.
std::vector<Display> matching_displays(const std::vector<Display>& displays,
                                       const std::string& model = "",
                                       const std::optional<std::string>& serial = std::nullopt);

// Returns the matching monitors that respond to DDC/CI. Throws DdcError with an
// actionable message when a monitor matches but DDC/CI is unavailable, or when
// nothing matches at all.
std::vector<Display> accessible_matches(
    const std::vector<Display>& displays, const std::string& model = "",
    const std::optional<std::string>& serial = std::nullopt);

// Selects exactly one matching, DDC/CI-capable display. Throws DdcError when
// none or several remain, or when matches exist but DDC/CI is unavailable.
Display choose_display(const std::vector<Display>& displays,
                       const std::string& model = "",
                       const std::optional<std::string>& serial = std::nullopt);

// Augments a ddcutil error message with actionable guidance for common
// DDC/CI, i2c-dev, and permission problems.
std::string friendly_ddc_error(const std::string& message);

// Wraps invocations of the ddcutil executable.
class Ddcutil {
   public:
    explicit Ddcutil(std::string binary = "ddcutil", ProcessRunner runner = run_process);

    // Builds an argument vector beginning with the configured binary.
    std::vector<std::string> command(const std::vector<std::string>& arguments) const;

    // Returns the first line of `ddcutil --version`.
    std::string version();

    // Runs `ddcutil detect --brief` and parses the result.
    std::vector<Display> detect();

    // Reads the current input via `ddcutil getvcp 60 ... --terse`.
    int get_input(const Selector& selector);

    // Returns the input values advertised by `ddcutil capabilities --brief`.
    std::vector<int> input_values(const Selector& selector);

    // Confirms the target responds to DDC/CI by reading feature 0x60, without
    // requiring the value to be parseable. Throws DdcError when communication
    // fails.
    void verify_ddc(const Selector& selector);

    // Writes `code` via `ddcutil setvcp 60 ... --noverify` and returns the
    // executed command vector.
    std::vector<std::string> set_input(int code, const Selector& selector);

    // Builds (without executing) the setvcp command vector.
    std::vector<std::string> set_input_command(int code, const Selector& selector) const;

   private:
    std::string run(const std::vector<std::string>& arguments);

    std::string binary_;
    ProcessRunner runner_;
};

}  // namespace monitor_switch
