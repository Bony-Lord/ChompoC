#include "interpreter.h"
#include "callable.h"
#include "network_manager.h"
#include "runtime_error.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
    constexpr std::size_t MaxReceiveSize = 1024 * 1024;

    const std::string &require_string(const Token &token, const Value &value, std::string_view description) {
        if (!value.is_string())
            throw RuntimeError(token, std::string(description) + " must be string, got " + value.type_name());
        return std::get<std::string>(value.data);
    }

    std::int64_t require_integer(const Token &token, const Value &value, std::string_view description) {
        if (!value.is_integer_number())
            throw RuntimeError(token, std::string(description) + " must be integer, got " + value.type_name());
        return value.number_as_integer();
    }

    std::uint16_t require_port(const Token &token, const Value &value) {
        const std::int64_t port = require_integer(token, value, "network port");
        if (port < 0 || port > 65535)
            throw RuntimeError(token, "network port must be in range 0..65535");
        return static_cast<std::uint16_t>(port);
    }

    int require_int_range(const Token &token, const Value &value, std::string_view description,
                          std::int64_t minimum, std::int64_t maximum) {
        const std::int64_t number = require_integer(token, value, description);
        if (number < minimum || number > maximum) {
            throw RuntimeError(token, std::string(description) + " must be in range " +
                                          std::to_string(minimum) + ".." + std::to_string(maximum));
        }
        return static_cast<int>(number);
    }

    NetworkManager::Handle require_handle(const Token &token, const Value &value) {
        return require_integer(token, value, "network handle");
    }

    std::vector<NetworkManager::Handle> require_handle_array(const Token &token, const Value &value) {
        if (!value.is_array())
            throw RuntimeError(token, "netPoll handles must be array, got " + value.type_name());

        const ArrayPtr &array = std::get<ArrayPtr>(value.data);
        std::vector<NetworkManager::Handle> handles;
        if (!array)
            return handles;

        handles.reserve(array->size());
        for (std::size_t index = 0; index < array->size(); ++index) {
            const Value &element = (*array)[index];
            if (!element.is_integer_number()) {
                throw RuntimeError(token, "netPoll handle at index " + std::to_string(index) +
                                              " must be integer, got " + element.type_name());
            }
            handles.push_back(element.number_as_integer());
        }
        return handles;
    }

    Value handles_to_array(const std::vector<NetworkManager::Handle> &handles) {
        auto array = std::make_shared<ArrayValue>();
        array->reserve(handles.size());
        for (const NetworkManager::Handle handle : handles)
            array->emplace_back(handle);
        return Value(std::move(array));
    }

    Value receive_result_to_value(NetworkManager::ReceiveResult result) {
        auto array = std::make_shared<ArrayValue>();
        switch (result.status) {
        case NetworkManager::ReceiveStatus::Data:
            array->reserve(2);
            array->emplace_back("data");
            array->emplace_back(std::move(result.data));
            break;
        case NetworkManager::ReceiveStatus::Wait:
            array->emplace_back("wait");
            break;
        case NetworkManager::ReceiveStatus::Closed:
            array->emplace_back("closed");
            break;
        }
        return Value(std::move(array));
    }

    Value network_error_value(std::string message) {
        auto array = std::make_shared<ArrayValue>();
        array->reserve(2);
        array->emplace_back("error");
        array->emplace_back(std::move(message));
        return Value(std::move(array));
    }

    Value send_result_value(std::string status, std::size_t sent, std::string detail = {}) {
        auto array = std::make_shared<ArrayValue>();
        array->reserve(detail.empty() ? 2 : 3);
        array->emplace_back(std::move(status));
        array->emplace_back(static_cast<std::int64_t>(sent));
        if (!detail.empty())
            array->emplace_back(std::move(detail));
        return Value(std::move(array));
    }

    Value send_all(NetworkManager &manager, NetworkManager::Handle socket, std::string_view data, int timeout_ms) {
        const auto started = std::chrono::steady_clock::now();
        std::size_t total = 0;

        while (total < data.size()) {
            try {
                total += manager.send(socket, data.substr(total));
            } catch (const std::exception &exception) {
                return send_result_value("error", total, exception.what());
            }

            if (total == data.size())
                return send_result_value("sent", total);

            if (timeout_ms >= 0) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started);
                if (elapsed.count() >= timeout_ms)
                    return send_result_value("timeout", total);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return send_result_value("sent", total);
    }

    template <class Operation> Value perform_network(const Token &token, Operation operation) {
        try {
            return operation();
        } catch (const RuntimeError &) {
            throw;
        } catch (const std::exception &exception) {
            throw RuntimeError(token, exception.what());
        }
    }
}

void Interpreter::install_network_builtins(NetworkManager &network_manager) {
    NetworkManager *const manager = &network_manager;

    auto define_native = [this](std::string name, std::size_t min_arity, std::size_t max_arity,
                                NativeFunction::Function function) {
        CallablePtr callable = std::make_shared<NativeFunction>(name, min_arity, max_arity, std::move(function));
        globals_->define(std::move(name), Value(std::move(callable)));
    };

    define_native("netListen", 2, 3,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const std::string &host = require_string(token, arguments[0], "netListen host");
                      const std::uint16_t port = require_port(token, arguments[1]);
                      int backlog = 16;
                      if (arguments.size() >= 3) {
                          backlog = require_int_range(token, arguments[2], "netListen backlog", 1,
                                                      std::numeric_limits<int>::max());
                      }
                      return perform_network(token, [manager, host, port, backlog]() {
                          return Value(manager->listen(host, port, backlog));
                      });
                  });

    define_native("netConnect", 2, 2,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const std::string &host = require_string(token, arguments[0], "netConnect host");
                      const std::uint16_t port = require_port(token, arguments[1]);
                      return perform_network(token,
                                             [manager, host, port]() { return Value(manager->connect(host, port)); });
                  });

    define_native("netAccept", 1, 1,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle listener = require_handle(token, arguments[0]);
                      return perform_network(token, [manager, listener]() {
                          const std::optional<NetworkManager::Handle> accepted = manager->accept(listener);
                          return accepted ? Value(*accepted) : Value(nullptr);
                      });
                  });

    define_native("netPoll", 1, 2,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      std::vector<NetworkManager::Handle> handles = require_handle_array(token, arguments[0]);
                      int timeout_ms = 0;
                      if (arguments.size() >= 2) {
                          timeout_ms = require_int_range(token, arguments[1], "netPoll timeout", -1,
                                                         std::numeric_limits<int>::max());
                      }
                      return perform_network(token, [manager, handles = std::move(handles), timeout_ms]() {
                          return handles_to_array(manager->poll(handles, timeout_ms));
                      });
                  });

    define_native("netSend", 2, 2,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_handle(token, arguments[0]);
                      const std::string &data = require_string(token, arguments[1], "netSend data");
                      return perform_network(token, [manager, socket, data]() {
                          const std::size_t sent = manager->send(socket, data);
                          if (sent > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
                              throw std::runtime_error("sent byte count is too large");
                          return Value(static_cast<std::int64_t>(sent));
                      });
                  });

    define_native("netSendAll", 2, 3,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_handle(token, arguments[0]);
                      const std::string &data = require_string(token, arguments[1], "netSendAll data");
                      int timeout_ms = 5000;
                      if (arguments.size() >= 3) {
                          timeout_ms = require_int_range(token, arguments[2], "netSendAll timeout", -1,
                                                         std::numeric_limits<int>::max());
                      }
                      return send_all(*manager, socket, data, timeout_ms);
                  });

    define_native("netReceive", 1, 2,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_handle(token, arguments[0]);
                      std::size_t max_bytes = 4096;
                      if (arguments.size() >= 2) {
                          const std::int64_t requested = require_integer(token, arguments[1], "netReceive size");
                          if (requested <= 0 || requested > static_cast<std::int64_t>(MaxReceiveSize))
                              throw RuntimeError(token, "netReceive size must be in range 1..1048576");
                          max_bytes = static_cast<std::size_t>(requested);
                      }
                      return perform_network(token, [manager, socket, max_bytes]() {
                          return receive_result_to_value(manager->receive(socket, max_bytes));
                      });
                  });

    define_native("netReceiveLine", 1, 1,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_handle(token, arguments[0]);
                      try {
                          return receive_result_to_value(manager->receive_line(socket));
                      } catch (const std::exception &exception) {
                          return network_error_value(exception.what());
                      }
                  });

    define_native("netPort", 1, 1,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle handle = require_handle(token, arguments[0]);
                      return perform_network(token, [manager, handle]() {
                          return Value(static_cast<std::int64_t>(manager->local_port(handle)));
                      });
                  });

    define_native("netClose", 1, 1,
                  [manager](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle handle = require_handle(token, arguments[0]);
                      return perform_network(token, [manager, handle]() {
                          manager->close(handle);
                          return Value(nullptr);
                      });
                  });
}
