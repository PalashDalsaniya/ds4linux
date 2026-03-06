#pragma once
// ds4linux::daemon — IPC server (Unix domain socket, epoll-driven)

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

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

    /// Accept a new client connection.  Returns the client fd.
    [[nodiscard]] int accept_client();

    /// Read and frame incoming data on `client_fd`, calling `handler` for each
    /// complete message.  Returns false if the client disconnected.
    bool read_client(int client_fd, const IpcMessageHandler& handler);

    /// Send a JSON response/event to a connected client.
    void send(int client_fd, const std::string& json_payload);

    /// Broadcast a JSON event to all connected clients.
    void broadcast(const std::string& json_payload);

    /// Disconnect a client.
    void disconnect(int client_fd);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ds4linux::daemon
