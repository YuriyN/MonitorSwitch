#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace monitor_switch {

// A selectable monitor input, mapping a canonical name and aliases to a VCP
// feature 0x60 value. Support for individual values depends on the monitor.
struct InputSource {
    std::string name;
    int code;
    std::vector<std::string> aliases;
    std::string description;
};

// Raised when an input name or raw value cannot be resolved. Treated as a
// validation failure (exit code 1), not a usage error.
class InputError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

// The catalog of known inputs, in display order.
const std::vector<InputSource>& inputs();

// Builds a catalog for monitor-advertised VCP values, preserving their order.
// Known values receive friendly names; unknown values remain selectable by code.
std::vector<InputSource> inputs_for_codes(const std::vector<int>& codes);

// Lower-cases, trims, and maps underscores to hyphens.
std::string normalize(const std::string& value);

// The result of resolving an input token. `source` is nullptr when the value
// is a raw code that does not match a known input.
struct ResolvedInput {
    int code;
    const InputSource* source;
};

// Resolves a canonical name, alias, or raw decimal/hex value to a VCP code.
// Throws InputError for unknown names or out-of-range values.
ResolvedInput resolve_input(const std::string& value);

// Returns the input with the given VCP code, or nullptr if none matches.
const InputSource* source_for_code(int code);

}  // namespace monitor_switch
