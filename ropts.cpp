#include "ropts.h"

#include <charconv> // from_chars / to_chars (C++17)
#include <cstdio>   // FILE* based IO
#include <limits>   // digits10 for write_numeric
#include <ostream>  // std::ostream IO
#include <string>

namespace ropts {
/******************************************************************************
 * Command line decomposition.
 */
std::optional<string_view> CommandLine::next() {
    if(current_element_) {
        string_view current = *current_element_;
        current_element_.reset();
        return current;
    } else if(next_argument_ < argc_) {
        auto current = string_view{argv_[next_argument_]};
        next_argument_ += 1;
        return current;
    } else {
        return {};
    }
}

void CommandLine::push_front(string_view element) {
    assert(!current_element_);
    current_element_ = element;
}

/******************************************************************************
 * ValueTrait
 */

static string_view next_or_fail(CommandLine & state, string_view name) {
    std::optional<string_view> next = state.next();
    if(next) {
        return *next;
    } else {
        std::string buf;
        write_text(buf, "missing value '");
        write_text(buf, name);
        write_text(buf, '\'');
        throw Exception(std::move(buf));
    }
}

// Parse a numeric value using from_chars
template <typename T>
static T parse_numeric(CommandLine & state, string_view name, string_view type_name) {
    string_view text = next_or_fail(state, name);
    T value;
    std::from_chars_result result = std::from_chars(text.begin(), text.end(), value);
    if(result.ptr == text.end()) {
        return value;
    } else {
        std::string buf;
        write_text(buf, "value '");
        write_text(buf, name);
        write_text(buf, "' is not a valid ");
        write_text(buf, type_name);
        write_text(buf, ": '");
        write_text(buf, text);
        write_text(buf, '\'');
        throw Exception(std::move(buf));
    }
}

// Write a numeric value using to_chars
// estimated_size is an heuristic estimation of written size ; better to overapproximate
template <typename T>
static std::size_t write_numeric(std::string & buffer, T value, std::size_t estimated_size) {
    std::size_t init_buf_size = buffer.size();
    while(true) {
        // Try writing to the buffer + estimated_size bytes
        std::size_t temporary_size = init_buf_size + estimated_size;
        buffer.resize(temporary_size);
        std::to_chars_result result =
            std::to_chars(&buffer[init_buf_size], &buffer[temporary_size], value);
        if(result.ec == std::errc{}) {
            assert(&buffer[init_buf_size] <= result.ptr);
            assert(result.ptr <= &buffer[temporary_size]);
            // Trim the buffer to what was written (remove unwritten space)
            std::size_t new_buf_size = result.ptr - buffer.data();
            buffer.resize(new_buf_size);
            return new_buf_size - init_buf_size;
        } else {
            // Try again with bigger space
            estimated_size *= 2;
        }
    }
}

string_view ValueTrait<string_view>::parse(CommandLine & state, string_view name) {
    return next_or_fail(state, name);
}

int ValueTrait<int>::parse(CommandLine & state, string_view name) {
    return parse_numeric<int>(state, name, "integer (int)");
}
std::size_t ValueTrait<int>::write(std::string & buffer, int value) {
    return write_numeric<int>(buffer, value, std::numeric_limits<int>::digits10 + 2);
}

/******************************************************************************
 * OptionBase
 */
string_view OptionBase::name() const noexcept {
    if(has_long_name()) {
        return long_name();
    } else {
        assert(has_short_name());
        return string_view{&short_name_, 1};
    }
}

void OptionBase::fail_option_repeated() const {
    std::string buf;
    write_text(buf, "option '");
    write_text(buf, name());
    write_text(buf, "' cannot be used more than once");
    throw Exception(std::move(buf));
}
void OptionBase::fail_parsing_error(string_view msg) const {
    std::string buf;
    write_text(buf, "option '");
    write_text(buf, name());
    write_text(buf, "': ");
    write_text(buf, msg);
    throw Exception(std::move(buf));
}

/******************************************************************************
 * Application
 */
template <typename Predicate>
static OptionBase * find(std::vector<OptionBase *> & options, Predicate predicate) {
    for(OptionBase * option : options) {
        if(predicate(option)) {
            return option;
        }
    }
    return nullptr;
}

void Application::parse(CommandLine command_line) {
    bool enable_option_parsing = true; // Set to false if '--' is encountered.

    while(std::optional<string_view> maybe_element = command_line.next()) {
        string_view element = *maybe_element;
        if(enable_option_parsing && element.size() >= 2 && element[0] == '-' && element[1] == '-') {
            // Long option
            string_view option_name = string_view{&element[2], element.size() - 2};
            if(option_name.empty()) {
                // '--'
                enable_option_parsing = false;
            } else {
                // '--name'
                // TODO support '--name=blah' mode
                OptionBase * option = find(options_, [option_name](OptionBase * o) {
                    return o->long_name() == option_name;
                });
                if(option != nullptr) {
                    option->parse(command_line);
                } else {
                    std::string buf;
                    write_text(buf, "unknown option name: '");
                    write_text(buf, element);
                    write_text(buf, '\'');
                    throw Exception(std::move(buf));
                }
            }
        } else if(enable_option_parsing && element.size() >= 2 && element[0] == '-') {
            // Short option
            if(element.size() > 2) {
                std::string buf;
                write_text(buf, "packed short options are not supported: '");
                write_text(buf, element);
                write_text(buf, '\'');
                throw Exception(std::move(buf));
            }
            // '-c'
            char option_name = element[1];
            OptionBase * option = find(options_, [option_name](OptionBase * o) {
                return o->short_name() == option_name; //
            });
            if(option != nullptr) {
                option->parse(command_line);
            } else {
                std::string buf;
                write_text(buf, "unknown option name: '");
                write_text(buf, element);
                write_text(buf, '\'');
                throw Exception(std::move(buf));
            }
        } else {
            // TODO
            // Subcommand
            // Positional
            // Or error
        }
    }
}

// Formatting tools
struct Output {
    virtual ~Output() = default;
    virtual void write(string_view sv) = 0;
    void write_buffer(std::string & buffer) {
        write(string_view(buffer));
        buffer.clear(); // Should not destroy allocation.
    }
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

// Dummy buffer to count size
struct DummyBuffer {};
static std::size_t write_text(DummyBuffer, string_view sv) {
    return sv.size();
}
static std::size_t write_text(DummyBuffer, char) {
    return 1;
}

static void write_usage_impl(
    Output & output, string_view application_name, std::vector<OptionBase *> const & options) {
    std::string buffer;
    // Header
    {
        write_text(buffer, application_name);
        write_text(buffer, " [options]\n\n");
        output.write_buffer(buffer);
    }
    // Option printing
    {
        auto write_option_pattern = [](auto && buffer, OptionBase const & option) -> std::size_t {
            std::size_t size = 0;
            size += write_text(buffer, "  ");
            if(option.has_short_name()) {
                size += write_text(buffer, '-');
                size += write_text(buffer, option.short_name());
            }
            if(option.has_short_name() && option.has_long_name()) {
                size += write_text(buffer, ',');
            }
            if(option.has_long_name()) {
                size += write_text(buffer, "--");
                size += write_text(buffer, option.long_name());
            }
            for(const CowStr & value_name : option.value_names()) {
                size += write_text(buffer, ' ');
                size += write_text(buffer, value_name);
            }
            return size;
        };
        // Compute max size of an option text up to help_text for alignement.
        std::size_t help_text_offset = 0;
        for(const OptionBase * option : options) {
            std::size_t option_pattern_len = write_option_pattern(DummyBuffer{}, *option);
            help_text_offset = std::max(help_text_offset, option_pattern_len + 3);
        }
        // Printing
        output.write("Options:\n");
        for(const OptionBase * option : options) {
            std::size_t size = write_option_pattern(buffer, *option);
            while(size < help_text_offset) {
                size += write_text(buffer, ' ');
            }
            write_text(buffer, option->help_text); // TODO line wrapping. Use terminal size ?
            write_text(buffer, '\n');
            output.write_buffer(buffer);
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