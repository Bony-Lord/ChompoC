#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class NetworkManager {
public:
    using Handle = std::int64_t;

    enum class ReceiveStatus {
        Data,
        Wait,
        Closed
    };

    struct ReceiveResult {
        ReceiveStatus status;
        std::string data;
    };

    NetworkManager();
    ~NetworkManager();

    NetworkManager(const NetworkManager &) = delete;
    NetworkManager &operator=(const NetworkManager &) = delete;

    Handle listen(std::string_view host, std::uint16_t port, int backlog = 16);
    Handle connect(std::string_view host, std::uint16_t port);
    std::optional<Handle> accept(Handle listener);

    std::vector<Handle> poll(const std::vector<Handle> &handles, int timeout_ms);
    std::size_t send(Handle socket, std::string_view data);
    ReceiveResult receive(Handle socket, std::size_t max_bytes = 4096);
    ReceiveResult receive_line(Handle socket);

    std::uint16_t local_port(Handle handle) const;
    void close(Handle handle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
