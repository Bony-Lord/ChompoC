#pragma once

#include "network_manager.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

class SecureChannelManager {
public:
    enum class StepStatus {
        Pending,
        Complete,
        Failed
    };

    struct StepResult {
        StepStatus status = StepStatus::Pending;
        std::string detail;
    };

    explicit SecureChannelManager(NetworkManager &network_manager);
    ~SecureChannelManager();

    SecureChannelManager(const SecureChannelManager &) = delete;
    SecureChannelManager &operator=(const SecureChannelManager &) = delete;

    // Blocking convenience wrappers (used by simple clients / tests).
    void client_handshake(NetworkManager::Handle socket, std::string_view password, int timeout_ms = 5000);
    void server_handshake(NetworkManager::Handle socket, std::string_view password, int timeout_ms = 5000);

    // Non-blocking handshake API. begin_* prepares local state and may send the first
    // protocol message; step_* advances when socket data is available without blocking
    // the caller for more than a non-blocking receive/send attempt.
    void begin_client_handshake(NetworkManager::Handle socket, std::string_view password);
    void begin_server_handshake(NetworkManager::Handle socket, std::string_view password);
    StepResult step_client_handshake(NetworkManager::Handle socket);
    StepResult step_server_handshake(NetworkManager::Handle socket);

    std::size_t send_line(NetworkManager::Handle socket, std::string_view plaintext, int timeout_ms = 5000);
    NetworkManager::ReceiveResult receive_line(NetworkManager::Handle socket);

    bool active(NetworkManager::Handle socket) const;
    bool handshake_pending(NetworkManager::Handle socket) const;
    void forget(NetworkManager::Handle socket) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
