#include "io_manager.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

IOManager::RedirectBuffer::RedirectBuffer(std::ostream &target) : target_(&target) {}

void IOManager::RedirectBuffer::set_target(std::ostream &target) { target_ = &target; }

IOManager::RedirectBuffer::int_type IOManager::RedirectBuffer::overflow(int_type character) {
    if (traits_type::eq_int_type(character, traits_type::eof()))
        return traits_type::not_eof(character);

    target_->put(traits_type::to_char_type(character));
    return target_->good() ? character : traits_type::eof();
}

std::streamsize IOManager::RedirectBuffer::xsputn(const char *data, std::streamsize size) {
    target_->write(data, size);
    return target_->good() ? size : 0;
}

int IOManager::RedirectBuffer::sync() {
    target_->flush();
    return target_->good() ? 0 : -1;
}

IOManager::IOManager(std::istream &standard_input, std::ostream &standard_output)
    : standard_input_(standard_input), standard_output_(standard_output), input_(&standard_input_),
      standard_console_(&standard_input == &std::cin), output_buffer_(standard_output_),
      redirected_output_(&output_buffer_) {}

IOManager::~IOManager() { redirected_output_.flush(); }

std::ostream &IOManager::output_stream() { return redirected_output_; }

std::optional<std::string> IOManager::read_line() {
    std::string line;

    if (std::getline(*input_, line))
        return line;

    if (input_->eof())
        return std::nullopt;

    throw std::runtime_error("failed to read from input stream");
}

bool IOManager::standard_line_ready(int timeout_ms) const {
    if (!standard_console_)
        return true;

#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("failed to access standard input handle");

    const auto started = std::chrono::steady_clock::now();
    const auto remaining_timeout = [&]() -> int {
        if (timeout_ms < 0)
            return -1;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        const auto remaining = static_cast<long long>(timeout_ms) - elapsed.count();
        return remaining > 0 ? static_cast<int>(remaining) : 0;
    };

    DWORD console_mode = 0;
    if (GetConsoleMode(handle, &console_mode) != 0) {
        while (true) {
            const int remaining = remaining_timeout();
            const DWORD wait_ms = remaining < 0 ? INFINITE : static_cast<DWORD>(remaining);
            const DWORD wait_result = WaitForSingleObject(handle, wait_ms);

            if (wait_result == WAIT_TIMEOUT)
                return false;
            if (wait_result != WAIT_OBJECT_0)
                throw std::runtime_error("failed while waiting for console input");

            DWORD event_count = 0;
            if (GetNumberOfConsoleInputEvents(handle, &event_count) == 0)
                throw std::runtime_error("failed to inspect console input");

            const DWORD inspect_count = std::min<DWORD>(event_count, 4096);
            std::vector<INPUT_RECORD> events(inspect_count);
            DWORD read_count = 0;
            if (inspect_count > 0 && PeekConsoleInputW(handle, events.data(), inspect_count, &read_count) == 0)
                throw std::runtime_error("failed to inspect console input events");

            for (DWORD index = 0; index < read_count; ++index) {
                const INPUT_RECORD &event = events[index];
                if (event.EventType == KEY_EVENT && event.Event.KeyEvent.bKeyDown &&
                    event.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
                    return true;
                }
            }

            if (timeout_ms == 0 || remaining_timeout() == 0)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    if (GetFileType(handle) == FILE_TYPE_PIPE) {
        while (true) {
            DWORD available = 0;
            if (PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) == 0) {
                if (GetLastError() == ERROR_BROKEN_PIPE)
                    return true;
                throw std::runtime_error("failed to inspect redirected standard input");
            }

            if (available > 0)
                return true;
            if (timeout_ms == 0 || remaining_timeout() == 0)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    return true;
#else
    pollfd descriptor{};
    descriptor.fd = STDIN_FILENO;
    descriptor.events = POLLIN;

    int result = 0;
    do {
        result = ::poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);

    if (result < 0)
        throw std::runtime_error("failed while polling standard input");
    if (result == 0)
        return false;
    if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
        throw std::runtime_error("standard input polling failed");

    return (descriptor.revents & (POLLIN | POLLHUP)) != 0;
#endif
}

IOManager::InputResult IOManager::poll_line(int timeout_ms) {
    if (timeout_ms < -1)
        throw std::runtime_error("inputPoll timeout must be -1 or non-negative");

    if (input_ == &standard_input_ && !standard_line_ready(timeout_ms))
        return {InputStatus::Wait, {}};

    std::optional<std::string> line = read_line();
    if (!line)
        return {InputStatus::Closed, {}};
    return {InputStatus::Data, std::move(*line)};
}

void IOManager::flush() {
    redirected_output_.flush();
    if (!redirected_output_)
        throw std::runtime_error("failed to flush output stream");
}

void IOManager::set_input(std::string_view path) {
    if (is_standard(path)) {
        standard_input_.clear();
        input_ = &standard_input_;
        input_file_.reset();
        return;
    }

    auto new_input = open_input_file(path);
    input_ = new_input.get();
    input_file_ = std::move(new_input);
}

void IOManager::set_output(std::string_view path, std::string_view mode) {
    redirected_output_.flush();
    output_buffer_.set_target(standard_output_);
    output_file_.reset();
    redirected_output_.clear();

    if (is_standard(path))
        return;

    auto new_output = open_output_file(path, mode);
    output_buffer_.set_target(*new_output);
    output_file_ = std::move(new_output);
}

void IOManager::set_streams(std::string_view input_path, std::string_view output_path, std::string_view output_mode) {
    std::unique_ptr<std::ifstream> new_input;
    if (!is_standard(input_path))
        new_input = open_input_file(input_path);

    std::unique_ptr<std::ofstream> new_output;
    if (!is_standard(output_path))
        new_output = open_output_file(output_path, output_mode);

    redirected_output_.flush();
    output_buffer_.set_target(standard_output_);
    output_file_.reset();
    redirected_output_.clear();

    if (new_input) {
        input_ = new_input.get();
        input_file_ = std::move(new_input);
    } else {
        standard_input_.clear();
        input_ = &standard_input_;
        input_file_.reset();
    }

    if (new_output) {
        output_buffer_.set_target(*new_output);
        output_file_ = std::move(new_output);
    }
}

bool IOManager::is_standard(std::string_view path) { return path == StandardPath; }

std::unique_ptr<std::ifstream> IOManager::open_input_file(std::string_view path) {
    auto file = std::make_unique<std::ifstream>(std::string(path));
    if (!*file)
        throw std::runtime_error("failed to open input file '" + std::string(path) + "'");
    return file;
}

std::unique_ptr<std::ofstream> IOManager::open_output_file(std::string_view path, std::string_view mode) {
    std::ios::openmode open_mode = std::ios::out;

    if (mode == RewriteMode) {
        open_mode |= std::ios::trunc;
    } else if (mode == AppendMode) {
        open_mode |= std::ios::app;
    } else if (mode == CreateMode) {
        if (std::filesystem::exists(std::filesystem::path(path)))
            throw std::runtime_error("output file '" + std::string(path) + "' already exists");
        open_mode |= std::ios::trunc;
    } else {
        throw std::runtime_error("unknown output mode '" + std::string(mode) +
                                 "'; expected 'rewrite', 'append' or 'create'");
    }

    auto file = std::make_unique<std::ofstream>(std::string(path), open_mode);
    if (!*file)
        throw std::runtime_error("failed to open output file '" + std::string(path) + "'");
    return file;
}
