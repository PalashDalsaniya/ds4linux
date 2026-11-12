// ds4linux::daemon — IPC server implementation (Unix domain socket)

#include "ds4linux/ipc_server.h"
#include "ds4linux/ipc_protocol.h"
#include "ds4linux/constants.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace ds4linux::daemon {

static constexpr std::size_t kMaxClients = 16;

struct IpcServer::Impl {
    int listen_fd = -1;
    std::string socket_path;

    // Connected clients and their receive buffers (for framing partial reads)
    std::unordered_map<int, std::vector<std::uint8_t>> clients;

    ~Impl() {
        for (const auto& [cfd, buf] : clients) {
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
    struct sockaddr_un addr{};
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        throw std::system_error(ENAMETOOLONG, std::system_category(),
                                "socket path too long: " + socket_path);
    }
    impl_->socket_path = socket_path;

    // Remove stale socket
    ::unlink(socket_path.c_str());

    impl_->listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (impl_->listen_fd < 0) {
        throw std::system_error(errno, std::system_category(), "socket() failed");
    }

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

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
    if (cfd >= 0 && impl_->clients.size() >= kMaxClients) {
        ::close(cfd);
        return -1;
    }
    if (cfd >= 0) impl_->clients[cfd] = {};
    return cfd;
}

bool IpcServer::read_client(int client_fd, const IpcMessageHandler& handler) {
    std::uint8_t tmp[4096];
    for (;;) {
        ssize_t n = ::recv(client_fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            auto& buf = impl_->clients[client_fd];
            buf.insert(buf.end(), tmp, tmp + n);

            // Try to decode complete messages.  The socket is world-writable,
            // so a malformed frame drops that client instead of the daemon.
            try {
                while (auto msg = ipc::decode_message(buf)) {
                    handler(client_fd, *msg);
                    if (!is_client(client_fd)) return false;
                }
            } catch (const std::exception& ex) {
                std::cerr << "[ds4linux] Dropping IPC client: " << ex.what() << "\n";
                disconnect(client_fd);
                return false;
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

bool IpcServer::send(int client_fd, const std::string& json_payload) {
    std::vector<std::uint8_t> frame;
    try {
        frame = ipc::encode_message(json_payload);
    } catch (const std::exception&) {
        frame = ipc::encode_message(R"({"type":"Error","message":"response too large"})");
    }

    std::size_t sent = 0;
    while (sent < frame.size()) {
        ssize_t n = ::send(client_fd, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        disconnect(client_fd);
        return false;
    }
    return true;
}

bool IpcServer::is_client(int fd) const {
    return impl_->clients.contains(fd);
}

std::vector<int> IpcServer::clients() const {
    std::vector<int> fds;
    fds.reserve(impl_->clients.size());
    for (const auto& [cfd, buf] : impl_->clients) fds.push_back(cfd);
    return fds;
}

void IpcServer::disconnect(int client_fd) {
    if (impl_->clients.erase(client_fd)) ::close(client_fd);
}

} // namespace ds4linux::daemon
