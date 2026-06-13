#pragma once

// A tiny dependency-free test framework so the build does not download
// anything during configure. Tests register themselves at static-init time and
// are run by the shared main in test_main.cpp.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mstest {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

// Thrown by a failed assertion to abort the current test.
struct Failure {
    std::string message;
};

}  // namespace mstest

#define MSTEST_CONCAT_INNER(a, b) a##b
#define MSTEST_CONCAT(a, b) MSTEST_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                            \
    static void MSTEST_CONCAT(mstest_body_, __LINE__)();                           \
    static ::mstest::Registrar MSTEST_CONCAT(mstest_reg_, __LINE__)(               \
        name, &MSTEST_CONCAT(mstest_body_, __LINE__));                             \
    static void MSTEST_CONCAT(mstest_body_, __LINE__)()

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::ostringstream oss;                                                \
            oss << __FILE__ << ":" << __LINE__ << ": CHECK failed: " << #cond;     \
            throw ::mstest::Failure{oss.str()};                                    \
        }                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                             \
    do {                                                                           \
        auto mstest_a = (a);                                                       \
        auto mstest_b = (b);                                                       \
        if (!(mstest_a == mstest_b)) {                                             \
            std::ostringstream oss;                                                \
            oss << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " << #a      \
                << " == " << #b << " (" << mstest_a << " vs " << mstest_b << ")";  \
            throw ::mstest::Failure{oss.str()};                                    \
        }                                                                          \
    } while (0)

#define CHECK_THROWS_AS(expr, exception_type)                                      \
    do {                                                                           \
        bool mstest_threw = false;                                                 \
        try {                                                                      \
            (void)(expr);                                                          \
        } catch (const exception_type&) {                                          \
            mstest_threw = true;                                                   \
        } catch (...) {                                                            \
            std::ostringstream oss;                                                \
            oss << __FILE__ << ":" << __LINE__                                     \
                << ": CHECK_THROWS_AS caught wrong type for " << #expr;            \
            throw ::mstest::Failure{oss.str()};                                    \
        }                                                                          \
        if (!mstest_threw) {                                                       \
            std::ostringstream oss;                                                \
            oss << __FILE__ << ":" << __LINE__ << ": expected " << #expr           \
                << " to throw " << #exception_type;                                \
            throw ::mstest::Failure{oss.str()};                                    \
        }                                                                          \
    } while (0)
