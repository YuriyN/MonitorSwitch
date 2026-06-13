#include "monitor_switch/process.hpp"

#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>

extern char** environ;

namespace monitor_switch {

namespace {

// Reads from stdout and stderr concurrently until both reach EOF. Draining one
// pipe to completion before touching the other deadlocks when the child fills
// the unread pipe's buffer while still writing to the one being read, so poll
// both descriptors and consume whichever is ready.
void drain_both(int out_fd, int err_fd, std::string& out, std::string& err) {
    std::array<char, 4096> buffer{};
    struct Stream {
        int fd;
        std::string* sink;
    };
    std::array<Stream, 2> streams = {Stream{out_fd, &out}, Stream{err_fd, &err}};

    while (streams[0].fd >= 0 || streams[1].fd >= 0) {
        struct pollfd fds[2];
        nfds_t nfds = 0;
        for (const Stream& stream : streams) {
            if (stream.fd >= 0) {
                fds[nfds].fd = stream.fd;
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                ++nfds;
            }
        }

        if (::poll(fds, nfds, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (nfds_t i = 0; i < nfds; ++i) {
            Stream* stream = (fds[i].fd == streams[0].fd) ? &streams[0] : &streams[1];
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                ssize_t count = ::read(stream->fd, buffer.data(), buffer.size());
                if (count > 0) {
                    stream->sink->append(buffer.data(),
                                         static_cast<std::size_t>(count));
                } else if (count == 0) {
                    stream->fd = -1;  // EOF
                } else if (errno != EINTR) {
                    stream->fd = -1;  // unrecoverable read error
                }
            }
        }
    }
}

}  // namespace

ProcessResult run_process(const std::vector<std::string>& argv) {
    ProcessResult result;
    if (argv.empty()) {
        result.started = false;
        return result;
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (::pipe(out_pipe) != 0) {
        result.started = false;
        return result;
    }
    if (::pipe(err_pipe) != 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        result.started = false;
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    // Wire the child's stdout/stderr to the write ends of our pipes.
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
    // Close inherited descriptors in the child.
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

    // Build a NULL-terminated argv. posix_spawnp does not modify these, so the
    // const_cast is safe for the lifetime of the call.
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

    pid_t pid = 0;
    int spawn_status =
        ::posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);

    // Close the write ends in the parent so reads see EOF when the child exits.
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    if (spawn_status != 0) {
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        result.started = false;
        return result;
    }

    drain_both(out_pipe[0], err_pipe[0], result.out, result.err);
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    int wait_status = 0;
    while (::waitpid(pid, &wait_status, 0) < 0 && errno == EINTR) {
        // Retry if interrupted by a signal.
    }

    if (WIFEXITED(wait_status)) {
        result.exit_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        result.exit_code = 128 + WTERMSIG(wait_status);
    } else {
        result.exit_code = -1;
    }
    result.started = true;
    return result;
}

}  // namespace monitor_switch
