#include <functional>

#include "fake_runner.hpp"
#include "monitor_switch/menu.hpp"
#include "test_framework.hpp"

using namespace monitor_switch;

namespace {

const char* kCapabilities =
    "Unparsed capabilities string: "
    "(prot(monitor)vcp(60(1B 0F 10 11 12 13)))\n";

}  // namespace

TEST_CASE("move_selection wraps around") {
    CHECK_EQ(move_selection(NavKey::Down, 4, 5), 0);
    CHECK_EQ(move_selection(NavKey::Up, 0, 5), 4);
    CHECK_EQ(move_selection(NavKey::Down, 2, 5), 3);
    CHECK_EQ(move_selection(NavKey::Up, 2, 5), 1);
}

TEST_CASE("move_selection honors home and end") {
    CHECK_EQ(move_selection(NavKey::Home, 3, 5), 0);
    CHECK_EQ(move_selection(NavKey::End, 1, 5), 4);
    CHECK_EQ(move_selection(NavKey::Other, 2, 5), 2);
}

TEST_CASE("input menu loads current input and positions selection") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x13\n"));  // auto
    fake.results.push_back(mstest::ok_result(kCapabilities));
    Ddcutil client("ddcutil", std::ref(fake));

    InputMenuModel model({"--display", "1"}, "display 1");
    model.load(client);

    CHECK(model.current_code().has_value());
    CHECK_EQ(*model.current_code(), 0x13);
    // "auto" is the last entry in the catalog.
    CHECK_EQ(model.selected_source().name, std::string("auto"));
}

TEST_CASE("input menu reports unreadable current input but stays usable") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::fail_result("permission denied"));
    fake.results.push_back(mstest::ok_result(kCapabilities));
    Ddcutil client("ddcutil", std::ref(fake));

    InputMenuModel model({"--display", "1"}, "display 1");
    model.load(client);

    CHECK(!model.current_code().has_value());
    CHECK(model.status().find("Could not read current input") != std::string::npos);

    // Navigation still works after a failed read.
    model.move(NavKey::Down);
    CHECK_EQ(model.selected_index(), 1);
}

TEST_CASE("refresh re-reads the current input") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::fail_result("temporary"));      // load fails
    fake.results.push_back(mstest::ok_result(kCapabilities));       // capabilities
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x0f\n"));  // refresh ok
    Ddcutil client("ddcutil", std::ref(fake));

    InputMenuModel model({"--display", "1"}, "display 1");
    model.load(client);
    CHECK(!model.current_code().has_value());

    model.refresh(client);
    CHECK(model.current_code().has_value());
    CHECK_EQ(*model.current_code(), 0x0F);
    CHECK_EQ(model.selected_source().name, std::string("dp-1"));
    CHECK_EQ(model.status(), std::string("Current input refreshed."));
}

TEST_CASE("confirm writes the selected input with --noverify") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x13\n"));  // load
    fake.results.push_back(mstest::ok_result(kCapabilities));
    Ddcutil client("ddcutil", std::ref(fake));

    InputMenuModel model({"--display", "1"}, "display 1");
    model.load(client);
    model.move(NavKey::Home);  // first entry: usb-c

    const InputSource& chosen = model.confirm(client);
    CHECK_EQ(chosen.name, std::string("usb-c"));

    const std::vector<std::string>& write_call = fake.calls.back();
    const std::vector<std::string> expected = {
        "ddcutil", "setvcp", "60", "0x1b", "--display", "1", "--noverify"};
    CHECK(write_call == expected);
}

TEST_CASE("input menu uses monitor-advertised inputs") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x0f\n"));
    fake.results.push_back(mstest::ok_result(
        "Unparsed capabilities string: (prot(monitor)vcp(60(0F FE 12)))\n"));
    Ddcutil client("ddcutil", std::ref(fake));

    InputMenuModel model({"--display", "1"}, "display 1");
    model.load(client);

    CHECK_EQ(model.catalog().size(), static_cast<std::size_t>(3));
    CHECK_EQ(model.catalog()[0].name, std::string("dp-1"));
    CHECK_EQ(model.catalog()[1].name, std::string("input-0xfe"));
    CHECK_EQ(model.catalog()[2].name, std::string("hdmi-2"));
    CHECK_EQ(model.selected_index(), 0);
}

TEST_CASE("input menu falls back when capabilities fail") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x10\n"));
    fake.results.push_back(mstest::fail_result("capabilities unavailable"));
    Ddcutil client("ddcutil", std::ref(fake));

    InputMenuModel model({"--display", "1"}, "display 1");
    model.load(client);

    CHECK_EQ(model.catalog().size(), inputs().size());
    CHECK_EQ(model.selected_source().name, std::string("dp-2"));
}
