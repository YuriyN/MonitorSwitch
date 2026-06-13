#include "test_framework.hpp"

int main() {
    int failures = 0;
    for (const auto& test : mstest::registry()) {
        try {
            test.body();
            std::cout << "[pass] " << test.name << "\n";
        } catch (const mstest::Failure& failure) {
            std::cout << "[FAIL] " << test.name << "\n        " << failure.message
                      << "\n";
            ++failures;
        } catch (const std::exception& exc) {
            std::cout << "[FAIL] " << test.name << "\n        unexpected exception: "
                      << exc.what() << "\n";
            ++failures;
        } catch (...) {
            std::cout << "[FAIL] " << test.name << "\n        unknown exception\n";
            ++failures;
        }
    }

    const int total = static_cast<int>(mstest::registry().size());
    std::cout << "\n"
              << (total - failures) << "/" << total << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
