#pragma once
// ds4linux::daemon — IPC server (Unix domain socket, epoll-driven)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ds4linux::daemon {

/// Callback invoked for every complete JSON message received from a client.
///   client_fd — the peer socket fd (for sending responses)
///   payload   — the decoded JSON string
using IpcMessageHandler = std::function<void(int client_fd,
                                              const std::string& payload)>;

/// Non-blocking IPC server over a Unix domain socket.
class IpcServer {
public:
    /// Bind and listen on `socket_path`.  Throws std::system_error.
    explicit IpcServer(const std::string& socket_path);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    /// The listening socket fd (for epoll registration).
    [[nodiscard]] int fd() const noexcept;

    /// Accept a new client connection.  Returns the client fd, or -1.
    [[nodiscard]] int accept_client();

    /// Read and frame incoming data on `client_fd`, calling `handler` for each
    /// complete message.  Returns false if the client was disconnected
    /// (closed by the peer, or dropped for sending a malformed frame).
    bool read_client(int client_fd, const IpcMessageHandler& handler);

    /// Send a JSON response to a connected client.  Never blocks: a client
    /// whose socket buffer is full (it stopped reading) is disconnected, as
    /// a partial frame would corrupt its stream.  Returns false then.
    bool send(int client_fd, const std::string& json_payload);

    /// Disconnect a client.
    void disconnect(int client_fd);

    /// Whether `fd` is a connected client.
    [[nodiscard]] bool is_client(int fd) const;

    /// Connected client fds (for epoll registration).
    [[nodiscard]] const std::vector<int>& clients() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon
