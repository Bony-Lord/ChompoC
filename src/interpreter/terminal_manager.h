#pragma once

#include "io_manager.h"

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

class TerminalManager {
public:
    enum class InputStatus {
        Data,
        Wait,
        Closed,
    };

    struct InputResult {
        InputStatus status;
        std::string data;
    };

    TerminalManager(IOManager &io_manager, std::ostream &output);
    ~TerminalManager();

    TerminalManager(const TerminalManager &) = delete;
    TerminalManager &operator=(const TerminalManager &) = delete;

    bool open();
    void close() noexcept;
    bool interactive() const noexcept;

    void set_prompt(std::string text, std::string style = "accent");
    void set_title(std::string_view title);
    void clear();
    void print_line(std::string_view text, std::string_view style = "default");

    InputResult poll_line(int timeout_ms = 0);
    std::optional<std::string> read_line(std::string prompt = {}, bool masked = false,
                                         std::string prompt_style = "accent");

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
