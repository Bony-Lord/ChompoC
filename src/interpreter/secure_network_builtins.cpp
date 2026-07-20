#include "interpreter.h"
#include "callable.h"
#include "network_manager.h"
#include "runtime_error.h"
#include "secure_channel.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
const std::string &require_secure_string(const Token &token, const Value &value, std::string_view description) {
    if (!value.is_string())
        throw RuntimeError(token, std::string(description) + " must be string, got " + value.type_name());
    return std::get<std::string>(value.data);
}

NetworkManager::Handle require_secure_handle(const Token &token, const Value &value) {
    if (!value.is_integer_number())
        throw RuntimeError(token, "secure network handle must be integer, got " + value.type_name());
    return value.number_as_integer();
}

int require_secure_timeout(const Token &token, const Value &value) {
    if (!value.is_integer_number())
        throw RuntimeError(token, "secure network timeout must be integer, got " + value.type_name());

    const std::int64_t timeout = value.number_as_integer();
    if (timeout < -1 || timeout > std::numeric_limits<int>::max())
        throw RuntimeError(token, "secure network timeout must be -1 or a non-negative 32-bit integer");
    return static_cast<int>(timeout);
}

Value operation_result(bool success, std::string detail = {}) {
    auto result = std::make_shared<ArrayValue>();
    result->emplace_back(success ? "ok" : "error");
    if (!detail.empty())
        result->emplace_back(std::move(detail));
    return Value(std::move(result));
}

Value secure_send_result(std::string status, std::size_t sent, std::string detail = {}) {
    auto result = std::make_shared<ArrayValue>();
    result->emplace_back(std::move(status));
    result->emplace_back(static_cast<std::int64_t>(sent));
    if (!detail.empty())
        result->emplace_back(std::move(detail));
    return Value(std::move(result));
}

Value secure_receive_result(NetworkManager::ReceiveResult result) {
    auto value = std::make_shared<ArrayValue>();
    switch (result.status) {
    case NetworkManager::ReceiveStatus::Data:
        value->emplace_back("data");
        value->emplace_back(std::move(result.data));
        break;
    case NetworkManager::ReceiveStatus::Wait:
        value->emplace_back("wait");
        break;
    case NetworkManager::ReceiveStatus::Closed:
        value->emplace_back("closed");
        break;
    }
    return Value(std::move(value));
}

Value secure_error(std::string message) {
    auto value = std::make_shared<ArrayValue>();
    value->emplace_back("error");
    value->emplace_back(std::move(message));
    return Value(std::move(value));
}

Value step_result_value(const SecureChannelManager::StepResult &result) {
    auto value = std::make_shared<ArrayValue>();
    switch (result.status) {
    case SecureChannelManager::StepStatus::Pending:
        value->emplace_back("pending");
        break;
    case SecureChannelManager::StepStatus::Complete:
        value->emplace_back("ok");
        break;
    case SecureChannelManager::StepStatus::Failed:
        value->emplace_back("error");
        value->emplace_back(result.detail.empty() ? "secure handshake failed" : result.detail);
        break;
    }
    return Value(std::move(value));
}
}

void Interpreter::install_secure_network_builtins(NetworkManager &network_manager) {
    auto secure = std::make_shared<SecureChannelManager>(network_manager);

    auto define_native = [this](std::string name, std::size_t min_arity, std::size_t max_arity,
                                NativeFunction::Function function) {
        CallablePtr callable = std::make_shared<NativeFunction>(name, min_arity, max_arity, std::move(function));
        globals_->define(std::move(name), Value(std::move(callable)));
    };

    // Blocking convenience wrappers (loop step until complete / timeout).
    define_native("netSecureClient", 2, 3,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      const std::string &password =
                          require_secure_string(token, arguments[1], "netSecureClient password");
                      const int timeout = arguments.size() >= 3 ? require_secure_timeout(token, arguments[2]) : 5000;
                      try {
                          secure->client_handshake(socket, password, timeout);
                          return operation_result(true);
                      } catch (const std::exception &exception) {
                          secure->forget(socket);
                          return operation_result(false, exception.what());
                      }
                  });

    define_native("netSecureServer", 2, 3,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      const std::string &password =
                          require_secure_string(token, arguments[1], "netSecureServer password");
                      const int timeout = arguments.size() >= 3 ? require_secure_timeout(token, arguments[2]) : 5000;
                      try {
                          secure->server_handshake(socket, password, timeout);
                          return operation_result(true);
                      } catch (const std::exception &exception) {
                          secure->forget(socket);
                          return operation_result(false, exception.what());
                      }
                  });

    // Non-blocking handshake: begin prepares state (server also sends HELLO), step advances.
    define_native("netSecureClientBegin", 2, 2,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      const std::string &password =
                          require_secure_string(token, arguments[1], "netSecureClientBegin password");
                      try {
                          secure->begin_client_handshake(socket, password);
                          return operation_result(true);
                      } catch (const std::exception &exception) {
                          secure->forget(socket);
                          return operation_result(false, exception.what());
                      }
                  });

    define_native("netSecureServerBegin", 2, 2,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      const std::string &password =
                          require_secure_string(token, arguments[1], "netSecureServerBegin password");
                      try {
                          secure->begin_server_handshake(socket, password);
                          return operation_result(true);
                      } catch (const std::exception &exception) {
                          secure->forget(socket);
                          return operation_result(false, exception.what());
                      }
                  });

    define_native("netSecureClientStep", 1, 1,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      return step_result_value(secure->step_client_handshake(socket));
                  });

    define_native("netSecureServerStep", 1, 1,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      return step_result_value(secure->step_server_handshake(socket));
                  });

    define_native("netSecureSendLine", 2, 3,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      const std::string &text = require_secure_string(token, arguments[1], "secure message");
                      const int timeout = arguments.size() >= 3 ? require_secure_timeout(token, arguments[2]) : 5000;
                      try {
                          const std::size_t sent = secure->send_line(socket, text, timeout);
                          return secure_send_result("sent", sent);
                      } catch (const std::exception &exception) {
                          secure->forget(socket);
                          return secure_send_result("error", 0, exception.what());
                      }
                  });

    define_native("netSecureReceiveLine", 1, 1,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      try {
                          return secure_receive_result(secure->receive_line(socket));
                      } catch (const std::exception &exception) {
                          return secure_error(exception.what());
                      }
                  });

    define_native("netSecureActive", 1, 1,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      return Value(secure->active(socket));
                  });

    define_native("netSecurePending", 1, 1,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      return Value(secure->handshake_pending(socket));
                  });

    define_native("netSecureForget", 1, 1,
                  [secure](Interpreter &, const Token &token, const std::vector<Value> &arguments) {
                      const NetworkManager::Handle socket = require_secure_handle(token, arguments[0]);
                      secure->forget(socket);
                      return Value(nullptr);
                  });
}
