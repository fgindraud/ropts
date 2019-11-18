#include "ropts.h"

#include <cstdio>  // FILE* based IO
#include <ostream> // std::ostream IO
#include <string>

namespace ropts {
/******************************************************************************
 * Formatting tools.
 */

struct Output {
    virtual ~Output() = default;
    virtual void write(string_view sv) = 0;
};

struct FileOutput : Output {
    std::FILE * out_;
    FileOutput(std::FILE * out) : out_(out) {}
    void write(string_view sv) final { std::fwrite(sv.data(), 1, sv.size(), out_); }
};
struct StreamOutput : Output {
    std::ostream & out_;
    StreamOutput(std::ostream & out) : out_(out) {}
    void write(string_view sv) final { out_.write(sv.data(), sv.size()); }
};

struct FormatBuffer {
    std::string buffer;

    std::size_t push(char c) {
        buffer.push_back(c);
        return 1;
    }
    std::size_t push(string_view sv) {
        buffer.append(sv.data(), sv.size());
        return sv.size();
    }

    void write_to(Output & output) {
        output.write(string_view(buffer));
        buffer.clear(); // Should not destroy allocation.
    }
};

struct DummyFormatBuffer {
    std::size_t push(char) { return 1; }
    std::size_t push(string_view sv) { return sv.size(); }
};

/******************************************************************************
 * Implementations.
 */

void Application::parse(int argc, char const * const * argv) {
    CommandLineParsingState command_line{argc, argv};
    // Loop until no more elements.
    while(command_line.read_next_argument()) {
        string_view argument = command_line.current_argument();
        if(argument.size() > 0 && argument[0] == '-') {
            if(argument.size() > 1 && argument[1] == '-') {
                // Long option name
                // Maybe '--'.
            } else {
                // Short option name
                // Could be '-' for stdin/stdout FIXME
            }
        } else {
            // Subcommand
            // Positional
            // Or error
        }
    }
}

static void write_usage_impl(
    Output & output, string_view application_name, std::vector<OptionBase *> const & options) {
    FormatBuffer buffer;
    // Header
    {
        buffer.push(application_name);
        buffer.push(" [options]\n\n");
        buffer.write_to(output);
    }
    // Option printing
    {
        auto write_option_pattern = [](auto && buffer, OptionBase const & option) -> std::size_t {
            std::size_t size = 0;
            size += buffer.push("  ");
            if(option.has_short_name()) {
                size += buffer.push('-');
                size += buffer.push(option.short_name);
            }
            if(option.has_short_name() && !option.long_name.empty()) {
                size += buffer.push(',');
            }
            if(!option.long_name.empty()) {
                size += buffer.push("--");
                size += buffer.push(option.long_name);
            }
            for(const CowStr & value_name : option.usage_value_names()) {
                size += buffer.push(' ');
                size += buffer.push(value_name);
            }
            return size;
        };
        // Compute max size of an option text up to help_text for alignement.
        std::size_t help_text_offset = 0;
        for(const OptionBase * option : options) {
            std::size_t option_pattern_len = write_option_pattern(DummyFormatBuffer{}, *option);
            help_text_offset = std::max(help_text_offset, option_pattern_len + 3);
        }
        // Printing
        buffer.push("Options:\n");
        buffer.write_to(output);
        for(const OptionBase * option : options) {
            std::size_t size = write_option_pattern(buffer, *option);
            while(size < help_text_offset) {
                size += buffer.push(' ');
            }
            buffer.push(option->help_text); // TODO line wrapping. Use terminal size ?
            buffer.push('\n');
            buffer.write_to(output);
        }
    }
}

void Application::write_usage(std::FILE * out) const {
    FileOutput file_output{out};
    write_usage_impl(file_output, string_view(name_), options_);
}
void Application::write_usage(std::ostream & out) const {
    StreamOutput stream_output{out};
    write_usage_impl(stream_output, string_view(name_), options_);
}

} // namespace ropts