#include <functional>
#include <sstream>

#include "fake_runner.hpp"
#include "monitor_switch/cli.hpp"
#include "test_framework.hpp"

using namespace monitor_switch;

namespace {

const char* kSingleDisplay =
    "Display 1\n"
    "   I2C bus:          /dev/i2c-4\n"
    "   DRM connector:    card1-DP-1\n"
    "   Monitor:          ACM:Office Display:ABC123\n";

// A menu launcher that records whether it ran and with which options.
struct MenuSpy {
    bool called = false;
    CliOptions options;
    int operator()(Ddcutil&, const CliOptions& opts, std::ostream&) {
        called = true;
        options = opts;
        return 0;
    }
};

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("no arguments defaults to interactive") {
    ParseResult parsed = parse_args({}, "ddcutil");
    CHECK(parsed.status == ParseStatus::Ok);
    CHECK(parsed.options.interactive);
    CHECK(!parsed.options.input.has_value());
    CHECK(parsed.options.monitor.empty());
}

TEST_CASE("explicit menu keeps target options") {
    ParseResult parsed = parse_args({"--menu", "--display", "2"}, "ddcutil");
    CHECK(parsed.status == ParseStatus::Ok);
    CHECK(parsed.options.interactive);
    CHECK(parsed.options.display.has_value());
    CHECK_EQ(*parsed.options.display, 2);
}

TEST_CASE("run dispatches to the interactive menu") {
    ParseResult parsed = parse_args({}, "ddcutil");
    mstest::FakeRunner fake;
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 0);
    CHECK(spy.called);
}

TEST_CASE("ddcutil default comes from the environment value") {
    ParseResult parsed = parse_args({"--current", "-d", "1"}, "/opt/ddcutil");
    CHECK_EQ(parsed.options.ddcutil, std::string("/opt/ddcutil"));
}

TEST_CASE("explicit --ddcutil overrides the default") {
    ParseResult parsed = parse_args({"--ddcutil", "/bin/dc", "--detect"}, "/opt/ddcutil");
    CHECK_EQ(parsed.options.ddcutil, std::string("/bin/dc"));
}

TEST_CASE("switches a unique monitor after a DDC/CI preflight") {
    ParseResult parsed = parse_args({"usb-c"}, "ddcutil");
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result(kSingleDisplay));      // detect
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x0f\n"));  // verify_ddc
    fake.results.push_back(mstest::ok_result(""));                  // setvcp
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 0);
    CHECK(!spy.called);
    CHECK(contains(out.str(), "USB Type-C"));
    CHECK(contains(out.str(), "0x1b"));

    // detect, then a getvcp preflight, then the setvcp write.
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(3));
    CHECK_EQ(fake.calls[1][1], std::string("getvcp"));
    const std::vector<std::string> write = {
        "ddcutil", "setvcp", "60", "0x1b", "--display", "1", "--noverify"};
    CHECK(fake.calls.back() == write);
}

TEST_CASE("switch aborts when the monitor does not respond to DDC/CI") {
    ParseResult parsed = parse_args({"usb-c", "--display", "1"}, "ddcutil");
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::fail_result("DDC communication failed"));  // verify
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 1);
    CHECK(contains(err.str(), "on-screen menu"));
    // Only the preflight getvcp ran; no setvcp was issued.
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(1));
    CHECK_EQ(fake.calls[0][1], std::string("getvcp"));
}

TEST_CASE("dry run prints the command without touching hardware") {
    ParseResult parsed = parse_args({"dp", "--dry-run"}, "ddcutil");
    mstest::FakeRunner fake;
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 0);
    CHECK(contains(out.str(), "setvcp 60 0x0f"));
    CHECK(contains(out.str(), "--noverify"));
    CHECK(!contains(out.str(), "--model"));
    // No hardware access at all: not even detection runs.
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(0));
}

TEST_CASE("dry run with an explicit display does not detect") {
    ParseResult parsed = parse_args({"dp", "--dry-run", "--display", "2"}, "ddcutil");
    mstest::FakeRunner fake;
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 0);
    CHECK(contains(out.str(), "setvcp 60 0x0f"));
    CHECK(contains(out.str(), "--display 2"));
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(0));
}

TEST_CASE("reads the current input without detecting") {
    ParseResult parsed = parse_args({"--current", "--display", "1"}, "ddcutil");
    mstest::FakeRunner fake;
    fake.results.push_back(mstest::ok_result("VCP 60 SNC x13\n"));
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 0);
    CHECK(contains(out.str(), "Auto Select"));
    // No detect call: the only invocation is getvcp.
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(1));
    CHECK_EQ(fake.calls[0][1], std::string("getvcp"));
}

TEST_CASE("invalid input does not probe hardware") {
    ParseResult parsed = parse_args({"vga"}, "ddcutil");
    mstest::FakeRunner fake;
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 1);
    CHECK(contains(err.str(), "unknown input"));
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(0));
}

TEST_CASE("list does not probe hardware") {
    ParseResult parsed = parse_args({"--list"}, "ddcutil");
    mstest::FakeRunner fake;
    Ddcutil client("ddcutil", std::ref(fake));
    MenuSpy spy;
    std::ostringstream out, err;

    int code = run(parsed.options, client, out, err, std::ref(spy));
    CHECK_EQ(code, 0);
    CHECK(contains(out.str(), "usb-c"));
    CHECK_EQ(fake.calls.size(), static_cast<std::size_t>(0));
}

TEST_CASE("dry-run requires a direct input") {
    ParseResult parsed = parse_args({"--menu", "--dry-run"}, "ddcutil");
    CHECK(parsed.status == ParseStatus::Error);
}

TEST_CASE("input cannot be combined with an action flag") {
    ParseResult parsed = parse_args({"usb-c", "--current"}, "ddcutil");
    CHECK(parsed.status == ParseStatus::Error);
}

TEST_CASE("display and bus are mutually exclusive") {
    ParseResult parsed = parse_args({"--display", "1", "--bus", "4"}, "ddcutil");
    CHECK(parsed.status == ParseStatus::Error);
}

TEST_CASE("display must be positive") {
    ParseResult parsed = parse_args({"--current", "--display", "0"}, "ddcutil");
    CHECK(parsed.status == ParseStatus::Error);
}

TEST_CASE("valueless options reject an attached value") {
    for (const char* arg :
         {"--current=oops", "--dry-run=oops", "--interactive=oops", "-hfoo"}) {
        ParseResult parsed = parse_args({arg}, "ddcutil");
        CHECK(parsed.status == ParseStatus::Error);
    }
}

TEST_CASE("help and version short-circuit") {
    CHECK(parse_args({"--help"}, "ddcutil").status == ParseStatus::Help);
    CHECK(parse_args({"--version"}, "ddcutil").status == ParseStatus::Version);
}
