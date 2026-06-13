#pragma once

#include <ostream>

#include "monitor_switch/cli.hpp"
#include "monitor_switch/ddc.hpp"

namespace monitor_switch {

// Runs the interactive ncurses interface. Returns a process exit code.
// Throws DdcError for detection failures or when a terminal is unavailable.
// Declared in cli.hpp as well; defined in tui.cpp which links ncurses.

}  // namespace monitor_switch
