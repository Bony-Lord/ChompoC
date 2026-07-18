#include "network_manager.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
    using NativeSocket = SOCKET;
    constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
#else
    using NativeSocket = int;
    constexpr NativeSocket InvalidSocket = -1;
#endif

    [[noreturn]] void network_error(std::string_view operation, int error_code) {
        throw std::runtime_error(std::string(operation) + " failed with socket error " + std::to_string(error_code));
    }

    int last_socket_error() {
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    bool is_would_block(int error_code) {
#ifdef _WIN32
        return error_code == WSAEWOULDBLOCK;
#else
        return error_code == EAGAIN || error_code == EWOULDBLOCK;
#endif
    }

    void close_native_socket(NativeSocket socket) {
        if (socket == InvalidSocket)
            return;
#ifdef _WIN32
        closesocket(socket);
#else
        ::close(socket);
#endif
    }

    void set_non_blocking(NativeSocket socket) {
#ifdef _WIN32
        u_long enabled = 1;
        if (ioctlsocket(socket, FIONBIO, &enabled) != 0)
            network_error("ioctlsocket", last_socket_error());
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0)
            network_error("fcntl", last_socket_error());
#endif
    }

    void enable_reuse_address(NativeSocket socket) {
        int enabled = 1;
#ifdef _WIN32
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&enabled), sizeof(enabled));
#else
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
    }

    std::string port_string(std::uint16_t port) { return std::to_string(static_cast<unsigned int>(port)); }

    struct AddressList {
        addrinfo *value = nullptr;

        ~AddressList() {
            if (value)
                freeaddrinfo(value);
        }
    };

    AddressList resolve_address(std::string_view host, std::uint16_t port, bool passive) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = passive ? AI_PASSIVE : 0;

        const std::string host_string(host);
        const std::string service = port_string(port);
        AddressList result;
        const char *host_pointer = host.empty() ? nullptr : host_string.c_str();
        const int status = getaddrinfo(host_pointer, service.c_str(), &hints, &result.value);

        if (status != 0) {
#ifdef _WIN32
            throw std::runtime_error("getaddrinfo failed with error " + std::to_string(status));
#else
            throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(status));
#endif
        }

        return result;
    }

    void wait_until_writable(NativeSocket socket) {
#ifdef _WIN32
        WSAPOLLFD descriptor{};
        descriptor.fd = socket;
        descriptor.events = POLLWRNORM;
        const int result = WSAPoll(&descriptor, 1, -1);
#else
        pollfd descriptor{};
        descriptor.fd = socket;
        descriptor.events = POLLOUT;
        const int result = ::poll(&descriptor, 1, -1);
#endif
        if (result <= 0)
            network_error("poll", last_socket_error());
    }
}

struct NetworkManager::Impl {
    enum class Kind {
        Listener,
        Stream
    };

    struct Entry {
        NativeSocket socket = InvalidSocket;
        Kind kind = Kind::Stream;
        std::string line_buffer;
        bool peer_closed = false;
    };

    std::unordered_map<Handle, Entry> entries;
    Handle next_handle = 1;

#ifdef _WIN32
    bool winsock_started = false;
#endif

    Impl() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            network_error("WSAStartup", last_socket_error());
        winsock_started = true;
#endif
    }

    ~Impl() {
        for (auto &[handle, entry] : entries) {
            (void) handle;
            close_native_socket(entry.socket);
        }
#ifdef _WIN32
        if (winsock_started)
            WSACleanup();
#endif
    }

    Handle add(NativeSocket socket, Kind kind) {
        if (next_handle == std::numeric_limits<Handle>::max())
            throw std::runtime_error("network handle limit reached");

        const Handle handle = next_handle++;
        entries.emplace(handle, Entry{socket, kind, {}, false});
        return handle;
    }

    Entry &require(Handle handle) {
        const auto iterator = entries.find(handle);
        if (iterator == entries.end())
            throw std::runtime_error("invalid network handle " + std::to_string(handle));
        return iterator->second;
    }

    const Entry &require(Handle handle) const {
        const auto iterator = entries.find(handle);
        if (iterator == entries.end())
            throw std::runtime_error("invalid network handle " + std::to_string(handle));
        return iterator->second;
    }

    Entry &require_kind(Handle handle, Kind kind, std::string_view description) {
        Entry &entry = require(handle);
        if (entry.kind != kind)
            throw std::runtime_error(std::string(description) + " requires a compatible network handle");
        return entry;
    }
};

NetworkManager::NetworkManager() : impl_(std::make_unique<Impl>()) {}
NetworkManager::~NetworkManager() = default;

NetworkManager::Handle NetworkManager::listen(std::string_view host, std::uint16_t port, int backlog) {
    if (backlog <= 0)
        throw std::runtime_error("listen backlog must be positive");

    AddressList addresses = resolve_address(host, port, true);
    int last_error = 0;

    for (const addrinfo *address = addresses.value; address != nullptr; address = address->ai_next) {
        NativeSocket socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == InvalidSocket) {
            last_error = last_socket_error();
            continue;
        }

        enable_reuse_address(socket);

#ifdef _WIN32
        const int bind_result = ::bind(socket, address->ai_addr, static_cast<int>(address->ai_addrlen));
#else
        const int bind_result = ::bind(socket, address->ai_addr, address->ai_addrlen);
#endif
        if (bind_result != 0 || ::listen(socket, backlog) != 0) {
            last_error = last_socket_error();
            close_native_socket(socket);
            continue;
        }

        try {
            set_non_blocking(socket);
        } catch (...) {
            close_native_socket(socket);
            throw;
        }

        return impl_->add(socket, Impl::Kind::Listener);
    }

    network_error("listen", last_error);
}

NetworkManager::Handle NetworkManager::connect(std::string_view host, std::uint16_t port) {
    if (host.empty())
        throw std::runtime_error("connect host cannot be empty");

    AddressList addresses = resolve_address(host, port, false);
    int last_error = 0;

    for (const addrinfo *address = addresses.value; address != nullptr; address = address->ai_next) {
        NativeSocket socket = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == InvalidSocket) {
            last_error = last_socket_error();
            continue;
        }

#ifdef _WIN32
        const int result = ::connect(socket, address->ai_addr, static_cast<int>(address->ai_addrlen));
#else
        const int result = ::connect(socket, address->ai_addr, address->ai_addrlen);
#endif
        if (result != 0) {
            last_error = last_socket_error();
            close_native_socket(socket);
            continue;
        }

        try {
            set_non_blocking(socket);
        } catch (...) {
            close_native_socket(socket);
            throw;
        }

        return impl_->add(socket, Impl::Kind::Stream);
    }

    network_error("connect", last_error);
}

std::optional<NetworkManager::Handle> NetworkManager::accept(Handle listener) {
    Impl::Entry &entry = impl_->require_kind(listener, Impl::Kind::Listener, "accept");
    NativeSocket socket = ::accept(entry.socket, nullptr, nullptr);

    if (socket == InvalidSocket) {
        const int error = last_socket_error();
        if (is_would_block(error))
            return std::nullopt;
        network_error("accept", error);
    }

    try {
        set_non_blocking(socket);
    } catch (...) {
        close_native_socket(socket);
        throw;
    }

    return impl_->add(socket, Impl::Kind::Stream);
}

std::vector<NetworkManager::Handle> NetworkManager::poll(const std::vector<Handle> &handles, int timeout_ms) {
    if (timeout_ms < -1)
        throw std::runtime_error("poll timeout must be -1 or non-negative");

    if (handles.empty()) {
        if (timeout_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return {};
    }

#ifdef _WIN32
    std::vector<WSAPOLLFD> descriptors;
#else
    std::vector<pollfd> descriptors;
#endif
    descriptors.reserve(handles.size());

    for (const Handle handle : handles) {
        const Impl::Entry &entry = impl_->require(handle);
#ifdef _WIN32
        WSAPOLLFD descriptor{};
        descriptor.fd = entry.socket;
        descriptor.events = POLLRDNORM;
#else
        pollfd descriptor{};
        descriptor.fd = entry.socket;
        descriptor.events = POLLIN;
#endif
        descriptors.push_back(descriptor);
    }

#ifdef _WIN32
    const int result = WSAPoll(descriptors.data(), static_cast<ULONG>(descriptors.size()), timeout_ms);
#else
    const int result = ::poll(descriptors.data(), descriptors.size(), timeout_ms);
#endif
    if (result < 0)
        network_error("poll", last_socket_error());

    std::vector<Handle> ready;
    ready.reserve(static_cast<std::size_t>(result));

    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const short events = descriptors[index].revents;
        if ((events & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0)
            ready.push_back(handles[index]);
    }

    return ready;
}

std::size_t NetworkManager::send(Handle handle, std::string_view data) {
    Impl::Entry &entry = impl_->require_kind(handle, Impl::Kind::Stream, "send");
    std::size_t sent = 0;

    while (sent < data.size()) {
        const std::size_t remaining = data.size() - sent;
        const int chunk_size = remaining > static_cast<std::size_t>(std::numeric_limits<int>::max())
                                   ? std::numeric_limits<int>::max()
                                   : static_cast<int>(remaining);
#ifdef _WIN32
        const int result = ::send(entry.socket, data.data() + sent, chunk_size, 0);
#else
#ifdef MSG_NOSIGNAL
        const int result = static_cast<int>(::send(entry.socket, data.data() + sent, chunk_size, MSG_NOSIGNAL));
#else
        const int result = static_cast<int>(::send(entry.socket, data.data() + sent, chunk_size, 0));
#endif
#endif
        if (result > 0) {
            sent += static_cast<std::size_t>(result);
            continue;
        }

        if (result == 0)
            throw std::runtime_error("socket closed while sending");

        const int error = last_socket_error();
        if (is_would_block(error)) {
            wait_until_writable(entry.socket);
            continue;
        }

        network_error("send", error);
    }

    return sent;
}

NetworkManager::ReceiveResult NetworkManager::receive(Handle handle, std::size_t max_bytes) {
    if (max_bytes == 0)
        throw std::runtime_error("receive size must be positive");

    Impl::Entry &entry = impl_->require_kind(handle, Impl::Kind::Stream, "receive");

    if (!entry.line_buffer.empty()) {
        const std::size_t count = std::min(max_bytes, entry.line_buffer.size());
        std::string data = entry.line_buffer.substr(0, count);
        entry.line_buffer.erase(0, count);
        return {ReceiveStatus::Data, std::move(data)};
    }

    if (entry.peer_closed)
        return {ReceiveStatus::Closed, {}};

    std::vector<char> buffer(max_bytes);
    const int request_size = max_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())
                                 ? std::numeric_limits<int>::max()
                                 : static_cast<int>(max_bytes);
    const int result = ::recv(entry.socket, buffer.data(), request_size, 0);

    if (result > 0)
        return {ReceiveStatus::Data, std::string(buffer.data(), static_cast<std::size_t>(result))};

    if (result == 0) {
        entry.peer_closed = true;
        return {ReceiveStatus::Closed, {}};
    }

    const int error = last_socket_error();
    if (is_would_block(error))
        return {ReceiveStatus::Wait, {}};

    network_error("receive", error);
}

NetworkManager::ReceiveResult NetworkManager::receive_line(Handle handle) {
    constexpr std::size_t MaxBufferedLine = 1024 * 1024;
    Impl::Entry &entry = impl_->require_kind(handle, Impl::Kind::Stream, "receive line");

    const auto extract_line = [&entry]() -> std::optional<std::string> {
        const std::size_t newline = entry.line_buffer.find('\n');
        if (newline == std::string::npos)
            return std::nullopt;

        std::string line = entry.line_buffer.substr(0, newline);
        entry.line_buffer.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return line;
    };

    if (std::optional<std::string> line = extract_line())
        return {ReceiveStatus::Data, std::move(*line)};

    if (entry.peer_closed) {
        if (entry.line_buffer.empty())
            return {ReceiveStatus::Closed, {}};

        std::string line = std::move(entry.line_buffer);
        entry.line_buffer.clear();
        return {ReceiveStatus::Data, std::move(line)};
    }

    std::array<char, 4096> buffer{};

    while (true) {
        const int result = ::recv(entry.socket, buffer.data(), static_cast<int>(buffer.size()), 0);

        if (result > 0) {
            entry.line_buffer.append(buffer.data(), static_cast<std::size_t>(result));
            if (entry.line_buffer.size() > MaxBufferedLine)
                throw std::runtime_error("received line exceeds 1 MiB limit");

            if (std::optional<std::string> line = extract_line())
                return {ReceiveStatus::Data, std::move(*line)};
            continue;
        }

        if (result == 0) {
            entry.peer_closed = true;
            if (entry.line_buffer.empty())
                return {ReceiveStatus::Closed, {}};

            std::string line = std::move(entry.line_buffer);
            entry.line_buffer.clear();
            return {ReceiveStatus::Data, std::move(line)};
        }

        const int error = last_socket_error();
        if (is_would_block(error))
            return {ReceiveStatus::Wait, {}};

        network_error("receive line", error);
    }
}

std::uint16_t NetworkManager::local_port(Handle handle) const {
    const Impl::Entry &entry = impl_->require(handle);
    sockaddr_storage address{};
#ifdef _WIN32
    int address_size = sizeof(address);
#else
    socklen_t address_size = sizeof(address);
#endif
    if (getsockname(entry.socket, reinterpret_cast<sockaddr *>(&address), &address_size) != 0)
        network_error("getsockname", last_socket_error());

    char service[NI_MAXSERV]{};
    const int result = getnameinfo(reinterpret_cast<const sockaddr *>(&address), address_size, nullptr, 0, service,
                                   sizeof(service), NI_NUMERICSERV);
    if (result != 0)
        throw std::runtime_error("failed to determine local port");

    const unsigned long port = std::stoul(service);
    if (port > 65535)
        throw std::runtime_error("invalid local port returned by socket API");
    return static_cast<std::uint16_t>(port);
}

void NetworkManager::close(Handle handle) {
    const auto iterator = impl_->entries.find(handle);
    if (iterator == impl_->entries.end())
        throw std::runtime_error("invalid network handle " + std::to_string(handle));

    close_native_socket(iterator->second.socket);
    impl_->entries.erase(iterator);
}
