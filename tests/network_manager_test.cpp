#include "interpreter/network_manager.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

NetworkManager::ReceiveResult wait_line(NetworkManager &manager, NetworkManager::Handle handle) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        const auto ready = manager.poll({handle}, 250);
        if (std::find(ready.begin(), ready.end(), handle) == ready.end())
            continue;

        auto result = manager.receive_line(handle);
        if (result.status != NetworkManager::ReceiveStatus::Wait)
            return result;
    }
    throw std::runtime_error("timed out waiting for line");
}
}

int main() {
    try {
        NetworkManager server;
        NetworkManager client;
        const auto listener = server.listen("127.0.0.1", 0, 4);
        const auto port = server.local_port(listener);

        std::exception_ptr client_error;
        std::thread client_thread([&]() {
            try {
                const auto socket = client.connect("127.0.0.1", port);
                require(client.send(socket, "hello\n") == 6, "client send size mismatch");
                const auto response = wait_line(client, socket);
                require(response.status == NetworkManager::ReceiveStatus::Data, "client did not receive data");
                require(response.data == "world", "client response mismatch");
                client.close(socket);
            } catch (...) {
                client_error = std::current_exception();
            }
        });

        const auto listener_ready = server.poll({listener}, 5000);
        require(std::find(listener_ready.begin(), listener_ready.end(), listener) != listener_ready.end(),
                "listener was not ready");

        const auto accepted = server.accept(listener);
        require(accepted.has_value(), "accept returned no client");
        const auto request = wait_line(server, *accepted);
        require(request.status == NetworkManager::ReceiveStatus::Data, "server did not receive data");
        require(request.data == "hello", "server request mismatch");
        require(server.send(*accepted, "world\n") == 6, "server send size mismatch");

        client_thread.join();
        if (client_error)
            std::rethrow_exception(client_error);

        server.close(*accepted);
        server.close(listener);
        std::cout << "network manager loopback test passed\n";
        return 0;
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
