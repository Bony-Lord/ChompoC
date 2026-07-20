#include "terminal_manager.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
constexpr std::size_t MaxInputBytes = 4096;

std::string_view style_code(std::string_view style) {
    if (style == "muted")
        return "\x1b[90m";
    if (style == "info")
        return "\x1b[36m";
    if (style == "success")
        return "\x1b[32;1m";
    if (style == "warning")
        return "\x1b[33;1m";
    if (style == "error")
        return "\x1b[31;1m";
    if (style == "accent")
        return "\x1b[35;1m";
    if (style == "system")
        return "\x1b[34;1m";
    if (style == "self")
        return "\x1b[32;1m";
    if (style == "message")
        return "\x1b[37m";
    return "\x1b[0m";
}

std::string sanitize_terminal_text(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char byte : text) {
        if (byte == 0x1b || byte == 0x07)
            continue;
        if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r')
            continue;
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

std::size_t utf8_length(unsigned char first) {
    if (first < 0x80)
        return 1;
    if ((first & 0xe0) == 0xc0)
        return 2;
    if ((first & 0xf0) == 0xe0)
        return 3;
    if ((first & 0xf8) == 0xf0)
        return 4;
    return 1;
}

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xc0) == 0x80; }

std::uint32_t decode_utf8_unit(std::string_view unit) {
    if (unit.empty())
        return 0;

    const auto first = static_cast<unsigned char>(unit[0]);
    if (unit.size() == 1)
        return first;
    if (unit.size() == 2) {
        return ((first & 0x1fU) << 6U) | (static_cast<unsigned char>(unit[1]) & 0x3fU);
    }
    if (unit.size() == 3) {
        return ((first & 0x0fU) << 12U) | ((static_cast<unsigned char>(unit[1]) & 0x3fU) << 6U) |
               (static_cast<unsigned char>(unit[2]) & 0x3fU);
    }
    if (unit.size() == 4) {
        return ((first & 0x07U) << 18U) | ((static_cast<unsigned char>(unit[1]) & 0x3fU) << 12U) |
               ((static_cast<unsigned char>(unit[2]) & 0x3fU) << 6U) |
               (static_cast<unsigned char>(unit[3]) & 0x3fU);
    }
    return 0xfffd;
}

std::string encode_utf8(std::uint32_t codepoint) {
    std::string result;
    if (codepoint <= 0x7f) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        result.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffff) {
        result.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        result.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
    return result;
}

std::vector<std::string> split_utf8(std::string_view text) {
    std::vector<std::string> units;
    for (std::size_t index = 0; index < text.size();) {
        std::size_t length = utf8_length(static_cast<unsigned char>(text[index]));
        if (index + length > text.size())
            length = 1;
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            if (!is_utf8_continuation(static_cast<unsigned char>(text[index + offset]))) {
                valid = false;
                break;
            }
        }
        if (!valid)
            length = 1;
        units.emplace_back(text.substr(index, length));
        index += length;
    }
    return units;
}

bool is_combining(std::uint32_t codepoint) {
    return (codepoint >= 0x0300 && codepoint <= 0x036f) ||
           (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
           (codepoint >= 0x1dc0 && codepoint <= 0x1dff) ||
           (codepoint >= 0x20d0 && codepoint <= 0x20ff) ||
           (codepoint >= 0xfe00 && codepoint <= 0xfe0f) ||
           (codepoint >= 0xfe20 && codepoint <= 0xfe2f) || codepoint == 0x200d;
}

bool is_wide(std::uint32_t codepoint) {
    return (codepoint >= 0x1100 && codepoint <= 0x115f) || codepoint == 0x2329 || codepoint == 0x232a ||
           (codepoint >= 0x2e80 && codepoint <= 0xa4cf) ||
           (codepoint >= 0xac00 && codepoint <= 0xd7a3) ||
           (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
           (codepoint >= 0xfe10 && codepoint <= 0xfe19) ||
           (codepoint >= 0xfe30 && codepoint <= 0xfe6f) ||
           (codepoint >= 0xff00 && codepoint <= 0xff60) ||
           (codepoint >= 0xffe0 && codepoint <= 0xffe6) ||
           (codepoint >= 0x1f000 && codepoint <= 0x1faff) ||
           (codepoint >= 0x20000 && codepoint <= 0x3fffd);
}

std::size_t unit_width(std::string_view unit) {
    const std::uint32_t codepoint = decode_utf8_unit(unit);
    if (codepoint == 0 || codepoint < 0x20 || is_combining(codepoint))
        return 0;
    return is_wide(codepoint) ? 2 : 1;
}

std::size_t text_width(std::string_view text) {
    std::size_t width = 0;
    for (const std::string &unit : split_utf8(text))
        width += unit_width(unit);
    return width;
}
}

struct TerminalManager::Impl {
    IOManager &io_manager;
    std::ostream &output;
    bool opened = false;
    bool is_interactive = false;
    bool masked = false;
    std::string prompt;
    std::string prompt_style = "accent";
    std::vector<std::string> buffer;
    std::size_t cursor = 0;
    std::vector<std::string> history;
    std::size_t history_position = 0;

#ifdef _WIN32
    HANDLE input_handle = INVALID_HANDLE_VALUE;
    HANDLE output_handle = INVALID_HANDLE_VALUE;
    DWORD original_input_mode = 0;
    DWORD original_output_mode = 0;
    UINT original_input_code_page = 0;
    UINT original_output_code_page = 0;
    wchar_t pending_high_surrogate = 0;
#else
    termios original_termios{};
    bool termios_saved = false;
    std::string utf8_pending;
    std::size_t utf8_expected = 0;
    std::string escape_sequence;
#endif

    Impl(IOManager &io, std::ostream &stream) : io_manager(io), output(stream) {}

    std::size_t input_bytes() const {
        std::size_t total = 0;
        for (const std::string &unit : buffer)
            total += unit.size();
        return total;
    }

    std::string joined_buffer() const {
        std::string result;
        result.reserve(input_bytes());
        for (const std::string &unit : buffer)
            result += unit;
        return result;
    }

    std::size_t columns() const {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (output_handle != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output_handle, &info) != 0)
            return static_cast<std::size_t>(info.srWindow.Right - info.srWindow.Left + 1);
#else
        winsize size{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
            return size.ws_col;
#endif
        return 120;
    }

    void clear_input_line() {
        if (!is_interactive)
            return;
        output << "\r\x1b[2K";
    }

    std::size_t range_width(std::size_t begin, std::size_t end) const {
        if (masked)
            return end - begin;
        std::size_t width = 0;
        for (std::size_t index = begin; index < end; ++index)
            width += unit_width(buffer[index]);
        return width;
    }

    void render() {
        if (!is_interactive)
            return;

        clear_input_line();
        output << style_code(prompt_style) << prompt << "\x1b[0m";

        const std::size_t prompt_width = text_width(prompt);
        const std::size_t available = columns() > prompt_width + 2 ? columns() - prompt_width - 2 : 8;

        std::size_t start = 0;
        while (start < cursor && range_width(start, cursor) > available)
            ++start;

        std::size_t end = start;
        std::size_t used = 0;
        while (end < buffer.size()) {
            const std::size_t width = masked ? 1 : unit_width(buffer[end]);
            if (used + width > available)
                break;
            used += width;
            ++end;
        }

        for (std::size_t index = start; index < end; ++index)
            output << (masked ? "•" : buffer[index]);

        const std::size_t tail_width = cursor < end ? range_width(cursor, end) : 0;
        if (tail_width > 0)
            output << "\x1b[" << tail_width << 'D';
        output.flush();
    }

    void replace_buffer(std::string_view text) {
        buffer = split_utf8(text);
        cursor = buffer.size();
        render();
    }

    void insert(std::string unit) {
        if (unit.empty() || input_bytes() + unit.size() > MaxInputBytes)
            return;
        buffer.insert(buffer.begin() + static_cast<std::ptrdiff_t>(cursor), std::move(unit));
        ++cursor;
        render();
    }

    void backspace() {
        if (cursor == 0)
            return;
        buffer.erase(buffer.begin() + static_cast<std::ptrdiff_t>(cursor - 1));
        --cursor;
        render();
    }

    void erase_at_cursor() {
        if (cursor >= buffer.size())
            return;
        buffer.erase(buffer.begin() + static_cast<std::ptrdiff_t>(cursor));
        render();
    }

    void move_left() {
        if (cursor > 0) {
            --cursor;
            render();
        }
    }

    void move_right() {
        if (cursor < buffer.size()) {
            ++cursor;
            render();
        }
    }

    void history_up() {
        if (history.empty())
            return;
        if (history_position > 0)
            --history_position;
        replace_buffer(history[history_position]);
    }

    void history_down() {
        if (history_position < history.size())
            ++history_position;
        if (history_position == history.size())
            replace_buffer("");
        else
            replace_buffer(history[history_position]);
    }

    TerminalManager::InputResult finish_line() {
        std::string line = joined_buffer();
        clear_input_line();
        output.flush();

        if (!line.empty() && (history.empty() || history.back() != line)) {
            history.push_back(line);
            if (history.size() > 100)
                history.erase(history.begin());
        }
        history_position = history.size();
        buffer.clear();
        cursor = 0;
        return {TerminalManager::InputStatus::Data, std::move(line)};
    }

#ifdef _WIN32
    std::optional<TerminalManager::InputResult> process_windows_key(const KEY_EVENT_RECORD &key) {
        if (!key.bKeyDown)
            return std::nullopt;

        const bool control = (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        const WORD repeat_count = std::max<WORD>(1, key.wRepeatCount);

        for (WORD repeat = 0; repeat < repeat_count; ++repeat) {
            switch (key.wVirtualKeyCode) {
            case VK_RETURN:
                return finish_line();
            case VK_BACK:
                backspace();
                continue;
            case VK_DELETE:
                erase_at_cursor();
                continue;
            case VK_LEFT:
                move_left();
                continue;
            case VK_RIGHT:
                move_right();
                continue;
            case VK_HOME:
                cursor = 0;
                render();
                continue;
            case VK_END:
                cursor = buffer.size();
                render();
                continue;
            case VK_UP:
                history_up();
                continue;
            case VK_DOWN:
                history_down();
                continue;
            default:
                break;
            }

            const wchar_t value = key.uChar.UnicodeChar;
            if (control && (value == L'l' || value == L'L')) {
                output << "\x1b[2J\x1b[H";
                render();
                continue;
            }
            if (control && (value == L'u' || value == L'U')) {
                buffer.clear();
                cursor = 0;
                render();
                continue;
            }
            if (value == L'\t') {
                insert("    ");
                continue;
            }
            if (value < 0x20)
                continue;

            const std::uint32_t unit = static_cast<std::uint32_t>(value);
            if (unit >= 0xd800 && unit <= 0xdbff) {
                pending_high_surrogate = value;
                continue;
            }
            if (unit >= 0xdc00 && unit <= 0xdfff && pending_high_surrogate != 0) {
                const std::uint32_t high = static_cast<std::uint32_t>(pending_high_surrogate) - 0xd800U;
                const std::uint32_t low = unit - 0xdc00U;
                pending_high_surrogate = 0;
                insert(encode_utf8(0x10000U + ((high << 10U) | low)));
                continue;
            }
            pending_high_surrogate = 0;
            insert(encode_utf8(unit));
        }
        return std::nullopt;
    }
#else
    bool escape_prefix_known() const {
        static constexpr std::string_view sequences[] = {
            "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D", "\x1b[H", "\x1b[F", "\x1b[3~"};
        for (const std::string_view sequence : sequences) {
            if (sequence.substr(0, escape_sequence.size()) == escape_sequence)
                return true;
        }
        return false;
    }

    void finish_escape_if_complete() {
        if (escape_sequence == "\x1b[A")
            history_up();
        else if (escape_sequence == "\x1b[B")
            history_down();
        else if (escape_sequence == "\x1b[C")
            move_right();
        else if (escape_sequence == "\x1b[D")
            move_left();
        else if (escape_sequence == "\x1b[H") {
            cursor = 0;
            render();
        } else if (escape_sequence == "\x1b[F") {
            cursor = buffer.size();
            render();
        } else if (escape_sequence == "\x1b[3~") {
            erase_at_cursor();
        } else {
            return;
        }
        escape_sequence.clear();
    }

    std::optional<TerminalManager::InputResult> process_unix_byte(unsigned char byte) {
        if (!escape_sequence.empty()) {
            escape_sequence.push_back(static_cast<char>(byte));
            finish_escape_if_complete();
            if (!escape_sequence.empty() && (!escape_prefix_known() || escape_sequence.size() > 5))
                escape_sequence.clear();
            return std::nullopt;
        }

        if (!utf8_pending.empty()) {
            if (!is_utf8_continuation(byte)) {
                utf8_pending.clear();
                utf8_expected = 0;
                return process_unix_byte(byte);
            }
            utf8_pending.push_back(static_cast<char>(byte));
            if (utf8_pending.size() == utf8_expected) {
                insert(std::move(utf8_pending));
                utf8_pending.clear();
                utf8_expected = 0;
            }
            return std::nullopt;
        }

        if (byte == '\r' || byte == '\n')
            return finish_line();
        if (byte == 0x7f || byte == 0x08) {
            backspace();
            return std::nullopt;
        }
        if (byte == 0x04 && buffer.empty())
            return TerminalManager::InputResult{TerminalManager::InputStatus::Closed, {}};
        if (byte == 0x0c) {
            output << "\x1b[2J\x1b[H";
            render();
            return std::nullopt;
        }
        if (byte == 0x15) {
            buffer.clear();
            cursor = 0;
            render();
            return std::nullopt;
        }
        if (byte == 0x1b) {
            escape_sequence.assign(1, static_cast<char>(byte));
            return std::nullopt;
        }
        if (byte == '\t') {
            insert("    ");
            return std::nullopt;
        }
        if (byte < 0x20)
            return std::nullopt;
        if (byte < 0x80) {
            insert(std::string(1, static_cast<char>(byte)));
            return std::nullopt;
        }

        utf8_expected = utf8_length(byte);
        if (utf8_expected <= 1)
            return std::nullopt;
        utf8_pending.assign(1, static_cast<char>(byte));
        return std::nullopt;
    }
#endif
};

TerminalManager::TerminalManager(IOManager &io_manager, std::ostream &output)
    : impl_(std::make_unique<Impl>(io_manager, output)) {}

TerminalManager::~TerminalManager() { close(); }

bool TerminalManager::open() {
    if (impl_->opened)
        return impl_->is_interactive;
    impl_->opened = true;

#ifdef _WIN32
    impl_->input_handle = GetStdHandle(STD_INPUT_HANDLE);
    impl_->output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (impl_->input_handle == nullptr || impl_->input_handle == INVALID_HANDLE_VALUE ||
        impl_->output_handle == nullptr || impl_->output_handle == INVALID_HANDLE_VALUE)
        return false;

    if (GetConsoleMode(impl_->input_handle, &impl_->original_input_mode) == 0 ||
        GetConsoleMode(impl_->output_handle, &impl_->original_output_mode) == 0)
        return false;

    impl_->original_input_code_page = GetConsoleCP();
    impl_->original_output_code_page = GetConsoleOutputCP();

    DWORD input_mode = impl_->original_input_mode;
    input_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE);
    input_mode |= ENABLE_EXTENDED_FLAGS | ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT;
    DWORD output_mode = impl_->original_output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    if (SetConsoleMode(impl_->input_handle, input_mode) == 0 ||
        SetConsoleMode(impl_->output_handle, output_mode) == 0) {
        SetConsoleMode(impl_->input_handle, impl_->original_input_mode);
        SetConsoleMode(impl_->output_handle, impl_->original_output_mode);
        return false;
    }

    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    impl_->is_interactive = true;
#else
    if (isatty(STDIN_FILENO) == 0 || isatty(STDOUT_FILENO) == 0)
        return false;
    if (tcgetattr(STDIN_FILENO, &impl_->original_termios) != 0)
        return false;

    termios raw = impl_->original_termios;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return false;

    impl_->termios_saved = true;
    impl_->is_interactive = true;
#endif

    impl_->render();
    return true;
}

void TerminalManager::close() noexcept {
    if (!impl_ || !impl_->opened)
        return;

    if (impl_->is_interactive) {
        impl_->clear_input_line();
        impl_->output << "\x1b[0m";
        impl_->output.flush();
#ifdef _WIN32
        SetConsoleMode(impl_->input_handle, impl_->original_input_mode);
        SetConsoleMode(impl_->output_handle, impl_->original_output_mode);
        if (impl_->original_input_code_page != 0)
            SetConsoleCP(impl_->original_input_code_page);
        if (impl_->original_output_code_page != 0)
            SetConsoleOutputCP(impl_->original_output_code_page);
#else
        if (impl_->termios_saved)
            tcsetattr(STDIN_FILENO, TCSANOW, &impl_->original_termios);
#endif
    }

    impl_->opened = false;
    impl_->is_interactive = false;
}

bool TerminalManager::interactive() const noexcept { return impl_->is_interactive; }

void TerminalManager::set_prompt(std::string text, std::string style) {
    impl_->prompt = sanitize_terminal_text(text);
    impl_->prompt_style = std::move(style);
    impl_->render();
}

void TerminalManager::set_title(std::string_view title) {
    if (!impl_->is_interactive)
        return;
    impl_->output << "\x1b]0;" << sanitize_terminal_text(title) << '\a';
    impl_->output.flush();
}

void TerminalManager::clear() {
    if (!impl_->is_interactive)
        return;
    impl_->output << "\x1b[2J\x1b[H";
    impl_->output.flush();
    impl_->render();
}

void TerminalManager::print_line(std::string_view text, std::string_view style) {
    const std::string safe = sanitize_terminal_text(text);
    if (impl_->is_interactive)
        impl_->clear_input_line();

    if (impl_->is_interactive)
        impl_->output << style_code(style);
    impl_->output << safe;
    if (safe.empty() || safe.back() != '\n')
        impl_->output << '\n';
    if (impl_->is_interactive)
        impl_->output << "\x1b[0m";
    impl_->output.flush();

    if (impl_->is_interactive)
        impl_->render();
}

TerminalManager::InputResult TerminalManager::poll_line(int timeout_ms) {
    if (timeout_ms < -1)
        throw std::runtime_error("terminal poll timeout must be -1 or non-negative");
    if (!impl_->opened)
        open();

    if (!impl_->is_interactive) {
        const IOManager::InputResult result = impl_->io_manager.poll_line(timeout_ms);
        switch (result.status) {
        case IOManager::InputStatus::Data:
            return {InputStatus::Data, result.data};
        case IOManager::InputStatus::Wait:
            return {InputStatus::Wait, {}};
        case IOManager::InputStatus::Closed:
            return {InputStatus::Closed, {}};
        }
    }

    const auto started = std::chrono::steady_clock::now();
    const auto remaining_timeout = [&]() -> int {
        if (timeout_ms < 0)
            return -1;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        const long long remaining = static_cast<long long>(timeout_ms) - elapsed.count();
        return remaining > 0 ? static_cast<int>(remaining) : 0;
    };

#ifdef _WIN32
    while (true) {
        const int remaining = remaining_timeout();
        const DWORD wait_time = remaining < 0 ? INFINITE : static_cast<DWORD>(remaining);
        const DWORD wait_result = WaitForSingleObject(impl_->input_handle, wait_time);
        if (wait_result == WAIT_TIMEOUT)
            return {InputStatus::Wait, {}};
        if (wait_result != WAIT_OBJECT_0)
            throw std::runtime_error("failed while waiting for terminal input");

        INPUT_RECORD record{};
        DWORD read_count = 0;
        if (ReadConsoleInputW(impl_->input_handle, &record, 1, &read_count) == 0)
            throw std::runtime_error("failed to read terminal input");
        if (read_count == 0)
            continue;
        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            impl_->render();
            continue;
        }
        if (record.EventType != KEY_EVENT)
            continue;
        if (std::optional<InputResult> result = impl_->process_windows_key(record.Event.KeyEvent))
            return std::move(*result);
        if (timeout_ms == 0 && remaining_timeout() == 0)
            return {InputStatus::Wait, {}};
    }
#else
    while (true) {
        pollfd descriptor{};
        descriptor.fd = STDIN_FILENO;
        descriptor.events = POLLIN;
        const int remaining = remaining_timeout();
        int poll_result = 0;
        do {
            poll_result = ::poll(&descriptor, 1, remaining);
        } while (poll_result < 0 && errno == EINTR);

        if (poll_result < 0)
            throw std::runtime_error("failed while waiting for terminal input");
        if (poll_result == 0)
            return {InputStatus::Wait, {}};
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0)
            throw std::runtime_error("terminal input polling failed");

        unsigned char byte = 0;
        const ssize_t count = ::read(STDIN_FILENO, &byte, 1);
        if (count == 0)
            return {InputStatus::Closed, {}};
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            throw std::runtime_error("failed to read terminal input");
        }

        if (std::optional<InputResult> result = impl_->process_unix_byte(byte))
            return std::move(*result);
        if (timeout_ms == 0 && remaining_timeout() == 0)
            return {InputStatus::Wait, {}};
    }
#endif

    return {InputStatus::Wait, {}};
}

std::optional<std::string> TerminalManager::read_line(std::string prompt, bool masked, std::string prompt_style) {
    if (!impl_->opened)
        open();

    if (!impl_->is_interactive) {
        if (!prompt.empty()) {
            impl_->output << sanitize_terminal_text(prompt);
            impl_->output.flush();
        }
        return impl_->io_manager.read_line();
    }

    impl_->prompt = sanitize_terminal_text(prompt);
    impl_->prompt_style = std::move(prompt_style);
    impl_->masked = masked;
    impl_->render();

    while (true) {
        InputResult result = poll_line(-1);
        if (result.status == InputStatus::Data) {
            impl_->masked = false;
            impl_->prompt.clear();
            impl_->render();
            return std::move(result.data);
        }
        if (result.status == InputStatus::Closed) {
            impl_->masked = false;
            impl_->prompt.clear();
            impl_->render();
            return std::nullopt;
        }
    }
}
