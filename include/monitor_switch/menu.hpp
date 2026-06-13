#pragma once

#include <optional>
#include <string>
#include <vector>

#include "monitor_switch/ddc.hpp"
#include "monitor_switch/inputs.hpp"

namespace monitor_switch {

// Abstract navigation intent, decoupled from ncurses key codes so the menu
// logic can be tested without a terminal.
enum class NavKey { Up, Down, Home, End, Confirm, Refresh, Cancel, Other };

// Computes the next selected index for a navigation key, wrapping around.
// `count` must be positive; other keys leave the selection unchanged.
int move_selection(NavKey key, int selected, int count);

// Holds the state of the input-selection menu. Reading and writing go through
// a Ddcutil client, but no presentation concern lives here.
class InputMenuModel {
   public:
    InputMenuModel(Selector selector, std::string target_label);

    // Performs the initial read of the current input, positioning the
    // selection on it when recognized.
    void load(Ddcutil& client);

    // Applies a navigation key to the selection.
    void move(NavKey key);

    // Re-reads the current input and updates state and status text.
    void refresh(Ddcutil& client);

    // The input the user is currently highlighting.
    const InputSource& selected_source() const;

    // Writes the highlighted input and returns it.
    const InputSource& confirm(Ddcutil& client);

    const Selector& selector() const { return selector_; }
    const std::string& target_label() const { return target_label_; }
    const std::vector<InputSource>& catalog() const { return catalog_; }
    const InputSource* source_for_code(int code) const;
    std::optional<int> current_code() const { return current_code_; }
    int selected_index() const { return selected_; }
    const std::string& status() const { return status_; }

   private:
    void reposition_to_current();

    Selector selector_;
    std::string target_label_;
    std::vector<InputSource> catalog_;
    std::optional<int> current_code_;
    int selected_ = 0;
    std::string status_;
};

}  // namespace monitor_switch
