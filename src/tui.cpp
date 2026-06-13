#include <array>
#include <csignal>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <curses.h>
#include <unistd.h>

#include "monitor_switch/cli.hpp"
#include "monitor_switch/ddc.hpp"
#include "monitor_switch/menu.hpp"
#include "monitor_switch/inputs.hpp"

namespace monitor_switch {

namespace {

// Set by the signal handler and consumed from normal control flow once curses
// has been shut down. sig_atomic_t makes the handler's write well-defined.
volatile std::sig_atomic_t g_pending_signal = 0;

bool signal_requested() { return g_pending_signal != 0; }

// Ensures the terminal is restored on normal exit, exceptions, and signals.
class CursesSession {
   public:
    CursesSession() {
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            throw DdcError(
                "could not start the ncurses interface; run it from an interactive "
                "terminal");
        }
        if (initscr() == nullptr) {
            throw DdcError(
                "could not start the ncurses interface; run it from an interactive "
                "terminal");
        }
        active_ = true;
        install_handlers();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        safe_curs_set(0);
    }

    ~CursesSession() { shutdown(); }

    CursesSession(const CursesSession&) = delete;
    CursesSession& operator=(const CursesSession&) = delete;

    WINDOW* window() { return stdscr; }

   private:
    void shutdown() {
        if (active_) {
            endwin();
            active_ = false;
        }
        restore_handlers();
    }

    static void safe_curs_set(int visibility) {
        // Some terminals reject cursor visibility changes; ignore failures.
        curs_set(visibility);
    }

    void install_handlers() {
        struct sigaction sa{};
        sa.sa_handler = &CursesSession::handle_signal;
        sigemptyset(&sa.sa_mask);
        // No SA_RESTART: a signal during wgetch makes it return so the menu
        // loop can observe the pending signal and unwind.
        sa.sa_flags = 0;
        for (std::size_t i = 0; i < kSignals.size(); ++i) {
            sigaction(kSignals[i], &sa, &saved_handlers_[i]);
        }
        handlers_installed_ = true;
    }

    void restore_handlers() {
        if (!handlers_installed_) {
            return;
        }
        for (std::size_t i = 0; i < kSignals.size(); ++i) {
            sigaction(kSignals[i], &saved_handlers_[i], nullptr);
        }
        handlers_installed_ = false;
    }

    // Async-signal-safe: only record the signal. ncurses is not
    // async-signal-safe, so the terminal is restored later from the destructor
    // and the signal is re-raised by launch_menu once curses has closed.
    static void handle_signal(int sig) { g_pending_signal = sig; }

    static constexpr std::array<int, 3> kSignals = {SIGINT, SIGTERM, SIGHUP};

    bool active_ = false;
    bool handlers_installed_ = false;
    std::array<struct sigaction, 3> saved_handlers_{};
};

void write_clipped(WINDOW* win, int row, int col, const std::string& text,
                   int attribute = A_NORMAL) {
    int height = 0;
    int width = 0;
    getmaxyx(win, height, width);
    if (row < 0 || row >= height || col >= width) {
        return;
    }
    int available = width - col - 1;
    if (available <= 0) {
        return;
    }
    wattron(win, attribute);
    mvwaddnstr(win, row, col, text.c_str(), available);
    wattroff(win, attribute);
}

NavKey translate_key(int key) {
    if (key == KEY_UP || key == 'k' || key == 'K') {
        return NavKey::Up;
    }
    if (key == KEY_DOWN || key == 'j' || key == 'J') {
        return NavKey::Down;
    }
    if (key == KEY_HOME) {
        return NavKey::Home;
    }
    if (key == KEY_END) {
        return NavKey::End;
    }
    if (key == KEY_ENTER || key == 10 || key == 13 || key == ' ') {
        return NavKey::Confirm;
    }
    if (key == 'r' || key == 'R') {
        return NavKey::Refresh;
    }
    if (key == 27 || key == 'q' || key == 'Q') {
        return NavKey::Cancel;
    }
    return NavKey::Other;
}

std::string display_text(const Display& display) {
    std::string text = display.label();
    if (display.bus) {
        text += " | /dev/i2c-" + std::to_string(*display.bus);
    }
    if (display.connector && !display.connector->empty()) {
        text += " | " + *display.connector;
    }
    return text;
}

// Returns the chosen display, or nullopt if the user cancelled.
std::optional<Display> choose_display_menu(WINDOW* win,
                                           const std::vector<Display>& displays) {
    int selected = 0;
    for (;;) {
        if (signal_requested()) {
            return std::nullopt;
        }
        werase(win);
        write_clipped(win, 0, 0, "Monitor Switch", A_BOLD);
        write_clipped(win, 2, 0, "Select a monitor:");

        for (std::size_t i = 0; i < displays.size(); ++i) {
            const bool active = static_cast<int>(i) == selected;
            const std::string marker = active ? ">" : " ";
            const int attr = active ? A_REVERSE : A_NORMAL;
            write_clipped(win, 4 + static_cast<int>(i), 0,
                          marker + " " + display_text(displays[i]), attr);
        }
        write_clipped(win, 6 + static_cast<int>(displays.size()), 0,
                      "Up/Down or j/k: move   Enter: select   q/Esc: cancel", A_DIM);
        wrefresh(win);

        const NavKey key = translate_key(wgetch(win));
        if (signal_requested() || key == NavKey::Cancel) {
            return std::nullopt;
        }
        if (key == NavKey::Confirm) {
            return displays[static_cast<std::size_t>(selected)];
        }
        selected = move_selection(key, selected, static_cast<int>(displays.size()));
    }
}

std::string current_text(const InputMenuModel& model) {
    if (!model.current_code()) {
        return "unavailable";
    }
    const int code = *model.current_code();
    const InputSource* source = model.source_for_code(code);
    char hex[8];
    std::snprintf(hex, sizeof(hex), "0x%02x", code & 0xFF);
    if (!source) {
        return std::string("unknown (") + hex + ")";
    }
    return source->description + " (" + hex + ")";
}

// Drives the input menu. Returns the chosen source, or nullopt if cancelled.
std::optional<InputSource> choose_input_menu(WINDOW* win, Ddcutil& client,
                                             InputMenuModel& model) {
    model.load(client);
    const std::vector<InputSource>& catalog = model.catalog();

    for (;;) {
        if (signal_requested()) {
            return std::nullopt;
        }
        werase(win);
        write_clipped(win, 0, 0, "Monitor Switch", A_BOLD);
        write_clipped(win, 1, 0, "Monitor: " + model.target_label());
        write_clipped(win, 2, 0, "Current input: " + current_text(model));
        write_clipped(win, 4, 0, "Select an input:");

        for (std::size_t i = 0; i < catalog.size(); ++i) {
            const InputSource& source = catalog[i];
            const bool active = static_cast<int>(i) == model.selected_index();
            const std::string selected_marker = active ? ">" : " ";
            const bool is_current =
                model.current_code() && *model.current_code() == source.code;
            const std::string current_marker = is_current ? "*" : " ";
            char line[128];
            std::snprintf(line, sizeof(line), "%s%s %-12s %-12s 0x%02x",
                          selected_marker.c_str(), current_marker.c_str(),
                          source.name.c_str(), source.description.c_str(), source.code);
            write_clipped(win, 6 + static_cast<int>(i), 0, line,
                          active ? A_REVERSE : A_NORMAL);
        }

        const int footer_row = 8 + static_cast<int>(catalog.size());
        write_clipped(win, footer_row, 0,
                      "Up/Down or j/k: move   Enter: switch   r: refresh   "
                      "q/Esc: cancel",
                      A_DIM);
        if (!model.status().empty()) {
            write_clipped(win, footer_row + 2, 0, model.status());
        }
        wrefresh(win);

        const NavKey key = translate_key(wgetch(win));
        if (signal_requested() || key == NavKey::Cancel) {
            return std::nullopt;
        }
        if (key == NavKey::Confirm) {
            return model.confirm(client);
        }
        if (key == NavKey::Refresh) {
            model.refresh(client);
            continue;
        }
        model.move(key);
    }
}

}  // namespace

int launch_menu(Ddcutil& client, const CliOptions& options, std::ostream& out) {
    std::optional<Selector> selector;
    std::string target_label;
    std::vector<Display> displays;

    if (options.display) {
        selector = Selector{"--display", std::to_string(*options.display)};
        target_label = "display " + std::to_string(*options.display);
    } else if (options.bus) {
        selector = Selector{"--bus", std::to_string(*options.bus)};
        target_label = "/dev/i2c-" + std::to_string(*options.bus);
    } else {
        // Throws an actionable error when nothing matches or matches exist but
        // DDC/CI is unavailable.
        displays = accessible_matches(client.detect(), options.monitor, options.serial);
        if (displays.size() == 1) {
            selector =
                Selector{"--display", std::to_string(*displays.front().index)};
            target_label = displays.front().label();
        }
    }

    std::optional<InputSource> chosen;
    std::string chosen_label;

    {
        CursesSession session;  // restores the terminal on every exit path
        WINDOW* win = session.window();

        if (!selector) {
            std::optional<Display> picked = choose_display_menu(win, displays);
            if (!picked) {
                return 0;  // user cancelled
            }
            selector = Selector{"--display", std::to_string(*picked->index)};
            target_label = picked->label();
        }

        InputMenuModel model(*selector, target_label);
        chosen = choose_input_menu(win, client, model);
        chosen_label = model.target_label();
    }  // ncurses closes here, before any normal output

    // The terminal has now been restored by the session destructor. If a signal
    // interrupted the menu, re-raise it with its default disposition so the exit
    // status reflects the signal instead of a normal exit.
    if (g_pending_signal != 0) {
        const int sig = g_pending_signal;
        std::signal(sig, SIG_DFL);
        std::raise(sig);
    }

    if (!chosen) {
        return 0;
    }

    char hex[8];
    std::snprintf(hex, sizeof(hex), "0x%02x", chosen->code & 0xFF);
    out << "Switched " << chosen_label << " to " << chosen->description << " (" << hex
        << ").\n";
    return 0;
}

}  // namespace monitor_switch
