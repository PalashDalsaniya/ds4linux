// ds4linux::daemon — IPC server implementation (Unix domain socket)

#include "ds4linux/ipc_server.h"
#include "ds4linux/ipc_protocol.h"
#include "ds4linux/constants.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace ds4linux::daemon {

struct IpcServer::Impl {
    int listen_fd = -1;
    std::string socket_path;

    // Per-client receive buffers (for framing partial reads)
    std::unordered_map<int, std::vector<std::uint8_t>> client_buffers;
    std::vector<int> client_fds;

    ~Impl() {
        for (int cfd : client_fds) {
            ::close(cfd);
        }
        if (listen_fd >= 0) {
            ::close(listen_fd);
        }
        if (!socket_path.empty()) {
            ::unlink(socket_path.c_str());
        }
    }
};

// ── Construction ─────────────────────────────────────────────────────────────

IpcServer::IpcServer(const std::string& socket_path)
    : impl_(std::make_unique<Impl>())
{
    impl_->socket_path = socket_path;

    // Remove stale socket
    ::unlink(socket_path.c_str());

    impl_->listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (impl_->listen_fd < 0) {
        throw std::system_error(errno, std::system_category(), "socket() failed");
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(impl_->listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::system_error(errno, std::system_category(),
                                "bind() failed on " + socket_path);
    }

    // Allow non-root clients to connect
    ::chmod(socket_path.c_str(), 0666);

    if (::listen(impl_->listen_fd, 8) < 0) {
        throw std::system_error(errno, std::system_category(), "listen() failed");
    }
}

IpcServer::~IpcServer() = default;

int IpcServer::fd() const noexcept { return impl_->listen_fd; }

// ── Client management ────────────────────────────────────────────────────────

int IpcServer::accept_client() {
    int cfd = ::accept4(impl_->listen_fd, nullptr, nullptr,
                        SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (cfd >= 0) {
        impl_->client_fds.push_back(cfd);
        impl_->client_buffers[cfd] = {};
    }
    return cfd;
}

bool IpcServer::read_client(int client_fd, const IpcMessageHandler& handler) {
    std::uint8_t tmp[4096];
    for (;;) {
        ssize_t n = ::recv(client_fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            auto& buf = impl_->client_buffers[client_fd];
            buf.insert(buf.end(), tmp, tmp + n);

            // Try to decode complete messages
            while (auto msg = ipc::decode_message(buf)) {
                handler(client_fd, *msg);
            }
        } else if (n == 0) {
            // Peer closed connection
            disconnect(client_fd);
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            disconnect(client_fd);
            return false;
        }
    }
    return true;
}

void IpcServer::send(int client_fd, const std::string& json_payload) {
    auto frame = ipc::encode_message(json_payload);
    // Best-effort send (non-blocking socket may partial-write;
    // production code should buffer outgoing data.)
    ::send(client_fd, frame.data(), frame.size(), MSG_NOSIGNAL);
}

void IpcServer::broadcast(const std::string& json_payload) {
    for (int cfd : impl_->client_fds) {
        send(cfd, json_payload);
    }
}

void IpcServer::disconnect(int client_fd) {
    ::close(client_fd);
    impl_->client_buffers.erase(client_fd);
    auto& v = impl_->client_fds;
    v.erase(std::remove(v.begin(), v.end(), client_fd), v.end());
}

} // namespace ds4linux::daemon
