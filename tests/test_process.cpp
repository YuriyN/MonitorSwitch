#include "monitor_switch/process.hpp"
#include "test_framework.hpp"

using namespace monitor_switch;

TEST_CASE("runs a child process and captures stdout") {
    ProcessResult result = run_process({"/bin/echo", "hello world"});
    CHECK(result.started);
    CHECK_EQ(result.exit_code, 0);
    CHECK_EQ(result.out, std::string("hello world\n"));
}

TEST_CASE("preserves a nonzero exit status") {
    ProcessResult result = run_process({"/bin/sh", "-c", "exit 3"});
    CHECK(result.started);
    CHECK_EQ(result.exit_code, 3);
}

TEST_CASE("captures stderr separately") {
    ProcessResult result = run_process({"/bin/sh", "-c", "echo oops 1>&2"});
    CHECK(result.started);
    CHECK_EQ(result.err, std::string("oops\n"));
    CHECK_EQ(result.out, std::string(""));
}

TEST_CASE("reports a missing executable") {
    ProcessResult result = run_process({"/nonexistent/definitely-not-a-binary"});
    CHECK(!result.started);
}

TEST_CASE("does not interpret arguments through a shell") {
    // The "; rm" is a literal argument, never interpreted by a shell.
    ProcessResult result = run_process({"/bin/echo", "a; echo b"});
    CHECK_EQ(result.out, std::string("a; echo b\n"));
}

TEST_CASE("drains stdout and stderr without deadlocking") {
    // Each stream produces far more than a pipe buffer (~64 KiB), with stderr
    // written first. Draining stdout fully before stderr would block the child
    // and hang forever; concurrent draining must read both.
    const std::size_t lines = 20000;  // ~140 KiB per stream
    ProcessResult result = run_process(
        {"/bin/sh", "-c",
         "i=0; while [ $i -lt 20000 ]; do echo errerrerr 1>&2; echo outoutout; "
         "i=$((i+1)); done"});
    CHECK(result.started);
    CHECK_EQ(result.exit_code, 0);
    CHECK_EQ(result.out.size(), lines * std::string("outoutout\n").size());
    CHECK_EQ(result.err.size(), lines * std::string("errerrerr\n").size());
}
