#include "io_manager.h"

#include <filesystem>
#include <ios>
#include <stdexcept>
#include <utility>

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
      output_buffer_(standard_output_), redirected_output_(&output_buffer_) {}

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
