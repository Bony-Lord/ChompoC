#pragma once

#include <fstream>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>

class IOManager {
public:
    static constexpr std::string_view StandardPath = "standart";
    static constexpr std::string_view RewriteMode = "rewrite";
    static constexpr std::string_view AppendMode = "append";
    static constexpr std::string_view CreateMode = "create";

    IOManager(std::istream &standard_input, std::ostream &standard_output);
    ~IOManager();

    std::ostream &output_stream();
    std::optional<std::string> read_line();

    void set_input(std::string_view path = StandardPath);
    void set_output(std::string_view path = StandardPath, std::string_view mode = RewriteMode);
    void set_streams(std::string_view input_path = StandardPath,
                     std::string_view output_path = StandardPath,
                     std::string_view output_mode = RewriteMode);

private:
    class RedirectBuffer final : public std::streambuf {
    public:
        explicit RedirectBuffer(std::ostream &target);
        void set_target(std::ostream &target);

    protected:
        int_type overflow(int_type character) override;
        std::streamsize xsputn(const char *data, std::streamsize size) override;
        int sync() override;

    private:
        std::ostream *target_;
    };

    static bool is_standard(std::string_view path);
    static std::unique_ptr<std::ifstream> open_input_file(std::string_view path);
    static std::unique_ptr<std::ofstream> open_output_file(std::string_view path, std::string_view mode);

    std::istream &standard_input_;
    std::ostream &standard_output_;
    std::istream *input_;

    std::unique_ptr<std::ifstream> input_file_;
    std::unique_ptr<std::ofstream> output_file_;

    RedirectBuffer output_buffer_;
    std::ostream redirected_output_;
};
