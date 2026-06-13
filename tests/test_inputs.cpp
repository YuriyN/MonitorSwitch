#include "monitor_switch/inputs.hpp"
#include "test_framework.hpp"

using namespace monitor_switch;

TEST_CASE("resolves named aliases case-insensitively") {
    ResolvedInput dp = resolve_input("DP");
    CHECK_EQ(dp.code, 0x0F);
    CHECK(dp.source != nullptr);
    CHECK_EQ(dp.source->name, std::string("dp-1"));

    ResolvedInput usb = resolve_input("type_c");
    CHECK_EQ(usb.code, 0x1B);
    CHECK(usb.source != nullptr);
    CHECK_EQ(usb.source->name, std::string("usb-c"));

    ResolvedInput dp2 = resolve_input("DP-2");
    CHECK_EQ(dp2.code, 0x10);
    CHECK_EQ(dp2.source->name, std::string("dp-2"));

    CHECK_EQ(resolve_input("displayport").source->name, std::string("dp-1"));
    CHECK_EQ(resolve_input("displayport-2").source->name, std::string("dp-2"));
}

TEST_CASE("resolves raw hex and decimal codes") {
    CHECK_EQ(resolve_input("0x2a").code, 0x2A);
    CHECK_EQ(resolve_input("x1b").code, 0x1B);
    CHECK_EQ(resolve_input("27").code, 0x1B);
}

TEST_CASE("raw code matches known source when present") {
    ResolvedInput auto_select = resolve_input("0x13");
    CHECK(auto_select.source != nullptr);
    CHECK_EQ(auto_select.source->name, std::string("auto"));

    ResolvedInput unknown = resolve_input("0xfe");
    CHECK(unknown.source == nullptr);
}

TEST_CASE("rejects unknown input names") {
    CHECK_THROWS_AS(resolve_input("vga"), InputError);
}

TEST_CASE("rejects out-of-range raw values") {
    CHECK_THROWS_AS(resolve_input("0x1ff"), InputError);
}

TEST_CASE("finds source by code") {
    const InputSource* hdmi2 = source_for_code(0x12);
    CHECK(hdmi2 != nullptr);
    CHECK_EQ(hdmi2->name, std::string("hdmi-2"));
    CHECK(source_for_code(0xFE) == nullptr);
}

TEST_CASE("builds a catalog from advertised codes in monitor order") {
    std::vector<InputSource> catalog = inputs_for_codes({0x1B, 0x0F, 0xFE, 0x11});
    CHECK_EQ(catalog.size(), static_cast<std::size_t>(4));
    CHECK_EQ(catalog[0].name, std::string("usb-c"));
    CHECK_EQ(catalog[1].name, std::string("dp-1"));
    CHECK_EQ(catalog[2].name, std::string("input-0xfe"));
    CHECK_EQ(catalog[2].description, std::string("Input 0xfe"));
    CHECK_EQ(catalog[3].name, std::string("hdmi-1"));
}

TEST_CASE("catalog generation removes invalid and duplicate codes") {
    std::vector<InputSource> catalog = inputs_for_codes({0x10, 0x10, -1, 0x100});
    CHECK_EQ(catalog.size(), static_cast<std::size_t>(1));
    CHECK_EQ(catalog.front().name, std::string("dp-2"));
}
