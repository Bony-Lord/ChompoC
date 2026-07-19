#pragma once

#include "network_manager.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

class SecureChannelManager {
public:
    explicit SecureChannelManager(NetworkManager &network_manager);
    ~SecureChannelManager();

    SecureChannelManager(const SecureChannelManager &) = delete;
    SecureChannelManager &operator=(const SecureChannelManager &) = delete;

    void client_handshake(NetworkManager::Handle socket, std::string_view password, int timeout_ms = 5000);
    void server_handshake(NetworkManager::Handle socket, std::string_view password, int timeout_ms = 5000);

    std::size_t send_line(NetworkManager::Handle socket, std::string_view plaintext, int timeout_ms = 5000);
    NetworkManager::ReceiveResult receive_line(NetworkManager::Handle socket);

    bool active(NetworkManager::Handle socket) const;
    void forget(NetworkManager::Handle socket) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
