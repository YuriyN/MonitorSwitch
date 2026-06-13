#include <functional>

#include "fake_runner.hpp"
#include "monitor_switch/ddc.hpp"
#include "test_framework.hpp"

using namespace monitor_switch;

namespace {

const char* kDetectOutput =
    "Display 1\n"
    "   I2C bus:          /dev/i2c-4\n"
    "   DRM connector:    card1-DP-1\n"
    "   drm_connector_id: 0\n"
    "   Monitor:          ACM:Office Display:ABC123\n"
    "\n"
    "Display 2\n"
    "   I2C bus:          /dev/i2c-7\n"
    "   Monitor:          XYZ:Studio Display:XYZ789\n";

}  // namespace

TEST_CASE("parses detect --brief output") {
    std::vector<Display> displays = parse_detect_output(kDetectOutput);
    CHECK_EQ(displays.size(), static_cast<std::size_t>(2));
    CHECK(displays[0].index.has_value());
    CHECK_EQ(*displays[0].index, 1);
    CHECK(displays[0].ddc_accessible);
    CHECK(displays[0].bus.has_value());
    CHECK_EQ(*displays[0].bus, 4);
    CHECK_EQ(*displays[0].connector, std::string("card1-DP-1"));
    CHECK_EQ(*displays[0].manufacturer, std::string("ACM"));
    CHECK_EQ(*displays[0].model, std::string("Office Display"));
    CHECK_EQ(*displays[0].serial, std::string("ABC123"));
    CHECK(!displays[1].connector.has_value());
}

TEST_CASE("parses terse input value") {
    CHECK_EQ(parse_input_value("VCP 60 SNC x13\n"), 0x13);
}

TEST_CASE("parses human-readable input value") {
    CHECK_EQ(parse_input_value("VCP code 0x60 (Input Source): Invalid value (sl=0x1b)"),
             0x1B);
}

TEST_CASE("rejects unparseable input output") {
    CHECK_THROWS_AS(parse_input_value("nothing useful"), DdcError);
}

TEST_CASE("parses input values from a raw capabilities string") {
    const char* output =
        "Unparsed capabilities string: "
        "(prot(monitor)vcp(02 10 60( 1B 0F 13 11 12) 62 D6(01 04)))\n";
    const std::vector<int> expected = {0x1B, 0x0F, 0x13, 0x11, 0x12};
    CHECK(parse_input_capabilities(output) == expected);
}

TEST_CASE("returns no input values when capabilities omit feature 60") {
    CHECK(parse_input_capabilities(
              "Unparsed capabilities string: (prot(monitor)vcp(10 12 D6(01 04)))")
              .empty());
    CHECK(parse_input_capabilities("malformed").empty());
}

TEST_CASE("selects a unique monitor") {
    Display display =
        choose_display(parse_detect_output(kDetectOutput), "Office Display");
    CHECK_EQ(*display.index, 1);
}

TEST_CASE("selects by serial") {
    std::vector<Display> displays = parse_detect_output(kDetectOutput);
    Display display = choose_display(displays, "", std::string("xyz789"));
    CHECK_EQ(*display.index, 2);
}

TEST_CASE("reports missing monitor") {
    CHECK_THROWS_AS(choose_display({}), DdcError);
}

TEST_CASE("reports multiple matching monitors") {
    std::vector<Display> displays = parse_detect_output(kDetectOutput);
    CHECK_THROWS_AS(choose_display(displays, ""), DdcError);
}

TEST_CASE("returns all matching monitors for interactive selection") {
    std::vector<Display> displays = parse_detect_output(kDetectOutput);
    std::vector<Display> matches = matching_displays(displays, "");
    CHECK_EQ(matches.size(), static_cast<std::size_t>(2));
    CHECK_EQ(*matches[0].index, 1);
    CHECK_EQ(*matches[1].index, 2);
}

TEST_CASE("set_input uses --noverify and lowercase hex") {
    mstest::FakeRunner fake;
    Ddcutil client("ddcutil", std::ref(fake));
    std::vector<std::string> command = client.set_input(0x1B, {"--display", "1"});

    const std::vector<std::string> expected = {
        "ddcutil", "setvcp", "60", "0x1b", "--display", "1", "--noverify"};
    CHECK(command == expected);
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(1));
    CHECK(fake.calls[0] == expected);
}

TEST_CASE("set_input_command builds without executing") {
    mstest::FakeRunner fake;
    Ddcutil client("ddcutil", std::ref(fake));
    std::vector<std::string> command =
        client.set_input_command(0x0F, {"--display", "1"});
    const std::vector<std::string> expected = {
        "ddcutil", "setvcp", "60", "0x0f", "--display", "1", "--noverify"};
    CHECK(command == expected);
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(0));
}

TEST_CASE("nonzero exit becomes DdcError") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::fail_result("permission denied"));
    Ddcutil client("ddcutil", std::ref(fake));
    CHECK_THROWS_AS(client.detect(), DdcError);
}

TEST_CASE("missing executable is reported clearly") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::not_found_result());
    Ddcutil client("ddcutil", std::ref(fake));
    bool threw = false;
    try {
        client.detect();
    } catch (const DdcError& exc) {
        threw = true;
        CHECK(std::string(exc.what()).find("was not found") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("parses an Invalid display block as DDC/CI-incapable") {
    const char* output =
        "Display 1\n"
        "   I2C bus:          /dev/i2c-4\n"
        "   Monitor:          ACM:Office Display:GOOD001\n"
        "\n"
        "Invalid display\n"
        "   I2C bus:          /dev/i2c-9\n"
        "   Monitor:          ACM:Office Display:OFFOFF1\n"
        "   DDC communication failed\n";
    std::vector<Display> displays = parse_detect_output(output);
    CHECK_EQ(displays.size(), static_cast<std::size_t>(2));
    CHECK(displays[0].ddc_accessible);
    CHECK(!displays[1].index.has_value());
    CHECK(!displays[1].ddc_accessible);
    CHECK_EQ(*displays[1].serial, std::string("OFFOFF1"));
}

TEST_CASE("treats a Display with communication failure as incapable") {
    const char* output =
        "Display 1\n"
        "   I2C bus:          /dev/i2c-4\n"
        "   Monitor:          ACM:Office Display:ABC123\n"
        "   DDC communication failed\n";
    std::vector<Display> displays = parse_detect_output(output);
    CHECK(!displays[0].ddc_accessible);
}

TEST_CASE("selection skips DDC/CI-incapable monitors") {
    const char* output =
        "Invalid display\n"
        "   I2C bus:          /dev/i2c-9\n"
        "   Monitor:          ACM:Office Display:OFFOFF1\n"
        "   DDC communication failed\n";
    std::vector<Display> displays = parse_detect_output(output);
    // A matching monitor exists but DDC/CI is unavailable: this must fail with
    // an actionable message rather than be selected.
    bool threw = false;
    try {
        choose_display(displays);
    } catch (const DdcError& exc) {
        threw = true;
        const std::string message = exc.what();
        CHECK(message.find("did not respond to DDC/CI") != std::string::npos);
        CHECK(message.find("on-screen menu") != std::string::npos);
    }
    CHECK(threw);
}

TEST_CASE("accessible_matches returns only DDC/CI-capable monitors") {
    const char* output =
        "Display 1\n"
        "   Monitor:          ACM:Office Display:GOOD001\n"
        "\n"
        "Invalid display\n"
        "   Monitor:          ACM:Office Display:OFFOFF1\n"
        "   DDC communication failed\n";
    std::vector<Display> displays = parse_detect_output(output);
    std::vector<Display> ok = accessible_matches(displays, "Office Display");
    CHECK_EQ(ok.size(), static_cast<std::size_t>(1));
    CHECK_EQ(*ok.front().serial, std::string("GOOD001"));
}

TEST_CASE("friendly_ddc_error adds DDC/CI guidance") {
    std::string out = friendly_ddc_error("ddcutil failed: DDC communication failed");
    CHECK(out.find("on-screen menu") != std::string::npos);
    CHECK(out.find("i2c-dev") != std::string::npos);
}

TEST_CASE("verify_ddc issues a getvcp probe and propagates failure") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::fail_result("DDC communication failed"));
    Ddcutil client("ddcutil", std::ref(fake));
    CHECK_THROWS_AS(client.verify_ddc({"--display", "1"}), DdcError);
    const std::vector<std::string> expected = {
        "ddcutil", "getvcp", "60", "--display", "1", "--terse"};
    CHECK(fake.calls[0] == expected);
}

TEST_CASE("get_input issues a terse getvcp 60") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x0f\n"));
    Ddcutil client("ddcutil", std::ref(fake));
    int code = client.get_input({"--display", "2"});
    CHECK_EQ(code, 0x0F);
    const std::vector<std::string> expected = {
        "ddcutil", "getvcp", "60", "--display", "2", "--terse"};
    CHECK(fake.calls[0] == expected);
}

TEST_CASE("input_values queries brief monitor capabilities") {
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result(
        "Unparsed capabilities string: (prot(monitor)vcp(60(0F 10 11 12)))\n"));
    Ddcutil client("ddcutil", std::ref(fake));

    const std::vector<int> expected_values = {0x0F, 0x10, 0x11, 0x12};
    CHECK(client.input_values({"--display", "2"}) == expected_values);
    const std::vector<std::string> expected_command = {
        "ddcutil", "capabilities", "--display", "2", "--brief"};
    CHECK(fake.calls[0] == expected_command);
}
