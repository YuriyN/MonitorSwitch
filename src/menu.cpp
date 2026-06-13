#include "monitor_switch/menu.hpp"

#include <utility>

namespace monitor_switch {

int move_selection(NavKey key, int selected, int count) {
    if (count <= 0) {
        return 0;
    }
    switch (key) {
        case NavKey::Up:
            return (selected - 1 + count) % count;
        case NavKey::Down:
            return (selected + 1) % count;
        case NavKey::Home:
            return 0;
        case NavKey::End:
            return count - 1;
        default:
            return selected;
    }
}

InputMenuModel::InputMenuModel(Selector selector, std::string target_label)
    : selector_(std::move(selector)),
      target_label_(std::move(target_label)),
      catalog_(inputs()) {}

void InputMenuModel::reposition_to_current() {
    if (!current_code_) {
        return;
    }
    for (std::size_t i = 0; i < catalog_.size(); ++i) {
        if (catalog_[i].code == *current_code_) {
            selected_ = static_cast<int>(i);
            return;
        }
    }
}

void InputMenuModel::load(Ddcutil& client) {
    try {
        current_code_ = client.get_input(selector_);
    } catch (const DdcError& exc) {
        current_code_ = std::nullopt;
        status_ = std::string("Could not read current input: ") +
                  friendly_ddc_error(exc.what());
    }

    try {
        std::vector<InputSource> advertised =
            inputs_for_codes(client.input_values(selector_));
        if (!advertised.empty()) {
            catalog_ = std::move(advertised);
        }
    } catch (const DdcError&) {
        // Capabilities strings are optional and often unreliable. Keep fallback.
    }
    reposition_to_current();
}

void InputMenuModel::move(NavKey key) {
    selected_ = move_selection(key, selected_, static_cast<int>(catalog_.size()));
}

void InputMenuModel::refresh(Ddcutil& client) {
    try {
        current_code_ = client.get_input(selector_);
        reposition_to_current();
        status_ = "Current input refreshed.";
    } catch (const DdcError& exc) {
        status_ = std::string("Refresh failed: ") + exc.what();
    }
}

const InputSource& InputMenuModel::selected_source() const {
    return catalog_[static_cast<std::size_t>(selected_)];
}

const InputSource* InputMenuModel::source_for_code(int code) const {
    for (const InputSource& source : catalog_) {
        if (source.code == code) {
            return &source;
        }
    }
    return nullptr;
}

const InputSource& InputMenuModel::confirm(Ddcutil& client) {
    const InputSource& source = selected_source();
    client.set_input(source.code, selector_);
    return source;
}

}  // namespace monitor_switch
