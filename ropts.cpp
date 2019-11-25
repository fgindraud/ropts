#include "ropts.h"

/* Number to str conversions :
 * C++17 has to_chars/from_chars.
 * However they are not supported for floats on Linux (yet).
 *
 * Parsing: use C strto<T> ; they require a null terminated string (copy) and use errno.
 * Writing: use snprintf with format specifier.
 */

#include <cerrno>    // strto<T> error detection
#include <cinttypes> // strtoimax
#include <cstdio>    // FILE* based IO
#include <cstdlib>   // strtoT for floating point
#include <limits>
#include <ostream> // std::ostream IO
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

string_view CommandLine::next_value_or_fail(string_view value_name) {
    std::optional<string_view> value = next();
    if(value) {
        return *value;
    } else {
        std::string buf;
        write_text(buf, "missing value '");
        write_text(buf, value_name);
        write_text(buf, '\'');
        throw Exception(std::move(buf));
    }
}

void CommandLine::push_front(string_view element) {
    assert(!current_element_);
    current_element_ = element;
}

/******************************************************************************
 * ValueTrait
 */

[[noreturn]] static void
fail_invalid_type_parsing(string_view text, string_view value_name, string_view type_name) {
    std::string buf;
    write_text(buf, "value '");
    write_text(buf, value_name);
    write_text(buf, "' is not a valid ");
    write_text(buf, type_name);
    write_text(buf, ": '");
    write_text(buf, text);
    write_text(buf, '\'');
    throw Exception(std::move(buf));
}

static std::intmax_t parse_intmax(string_view text, string_view value_name, string_view type_name) {
    if(!text.empty()) {
        std::string null_terminated{text.data(), text.size()};
        char * end_ptr = nullptr;
        errno = 0;
        std::intmax_t value = std::strtoimax(null_terminated.data(), &end_ptr, 0);
        if(end_ptr == null_terminated.data() + null_terminated.size() && errno == 0) {
            return value;
        }
    }
    fail_invalid_type_parsing(text, value_name, type_name);
}

template <typename T>
static T parse_signed_integer(string_view text, string_view value_name, string_view type_name) {
    using Limits = std::numeric_limits<T>;
    static_assert(Limits::is_integer, "implementation bug");
    static_assert(Limits::is_signed, "implementation bug");
    std::intmax_t value = parse_intmax(text, value_name, type_name);
    if(std::intmax_t(Limits::min()) <= value && value <= std::intmax_t(Limits::max())) {
        return static_cast<T>(value);
    } else {
        fail_invalid_type_parsing(text, value_name, type_name);
    }
}

// Write a numeric value using snprintf
template <typename T>
static std::size_t write_numeric(std::string & buffer, T value, char const * format) {
    std::size_t estimated_size =
        std::numeric_limits<T>::is_integer
            ? std::numeric_limits<T>::digits10 + 2  // Integers : max digits + space for '-'
            : std::numeric_limits<T>::digits10 + 9; // Floats : max digits + '-0.' + 'e-300'
    std::size_t init_buf_size = buffer.size();
    while(true) {
        // Try writing to the buffer + estimated_size bytes
        buffer.resize(init_buf_size + estimated_size);
        int written = std::snprintf(&buffer[init_buf_size], estimated_size, format, value);
        if(written >= 0 && std::size_t(written) < estimated_size) {
            // Trim the buffer to what was written (remove unwritten space)
            auto written_size = static_cast<std::size_t>(written);
            buffer.resize(init_buf_size + written_size);
            return written_size;
        } else {
            // Try again with bigger space
            estimated_size *= 2;
        }
    }
}

int ValueTrait<int>::parse(string_view text, string_view name) {
    return parse_signed_integer<int>(text, name, "integer (int)");
}
int ValueTrait<int>::parse(CommandLine & state, string_view name) {
    return parse(state.next_value_or_fail(name), name);
}
std::size_t ValueTrait<int>::write(std::string & buffer, int value) {
    return write_numeric(buffer, value, "%i");
}

long ValueTrait<long>::parse(string_view text, string_view name) {
    return parse_signed_integer<long>(text, name, "integer (long)");
}
long ValueTrait<long>::parse(CommandLine & state, string_view name) {
    return parse(state.next_value_or_fail(name), name);
}
std::size_t ValueTrait<long>::write(std::string & buffer, long value) {
    return write_numeric(buffer, value, "%li");
}

float ValueTrait<float>::parse(string_view text, string_view name) {
    if(!text.empty()) {
        std::string null_terminated{text.data(), text.size()};
        char * end_ptr = nullptr;
        errno = 0;
        float value = std::strtof(null_terminated.data(), &end_ptr);
        if(end_ptr == null_terminated.data() + null_terminated.size() && errno == 0) {
            return value;
        }
    }
    fail_invalid_type_parsing(text, name, "float");
}
float ValueTrait<float>::parse(CommandLine & state, string_view name) {
    return parse(state.next_value_or_fail(name), name);
}
std::size_t ValueTrait<float>::write(std::string & buffer, float value) {
    return write_numeric(buffer, double(value), "%g");
}

double ValueTrait<double>::parse(string_view text, string_view name) {
    if(!text.empty()) {
        std::string null_terminated{text.data(), text.size()};
        char * end_ptr = nullptr;
        errno = 0;
        double value = std::strtod(null_terminated.data(), &end_ptr);
        if(end_ptr == null_terminated.data() + null_terminated.size() && errno == 0) {
            return value;
        }
    }
    fail_invalid_type_parsing(text, name, "double");
}
double ValueTrait<double>::parse(CommandLine & state, string_view name) {
    return parse(state.next_value_or_fail(name), name);
}
std::size_t ValueTrait<double>::write(std::string & buffer, double value) {
    return write_numeric(buffer, value, "%g");
}

long double ValueTrait<long double>::parse(string_view text, string_view name) {
    if(!text.empty()) {
        std::string null_terminated{text.data(), text.size()};
        char * end_ptr = nullptr;
        errno = 0;
        long double value = std::strtold(null_terminated.data(), &end_ptr);
        if(end_ptr == null_terminated.data() + null_terminated.size() && errno == 0) {
            return value;
        }
    }
    fail_invalid_type_parsing(text, name, "long double");
}
long double ValueTrait<long double>::parse(CommandLine & state, string_view name) {
    return parse(state.next_value_or_fail(name), name);
}
std::size_t ValueTrait<long double>::write(std::string & buffer, long double value) {
    return write_numeric(buffer, value, "%Lg");
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

// Output "independence" wrappers : write buffer to output and clear.
// string::clear() should not reallocate in most cases.
static void flush_buffer(std::FILE * out, std::string & buffer) {
    std::fwrite(buffer.data(), 1, buffer.size(), out);
    buffer.clear();
}
static void flush_buffer(std::ostream & out, std::string & buffer) {
    out.write(buffer.data(), buffer.size());
    buffer.clear();
}

// Dummy buffer to count size
struct DummyBuffer {};
static std::size_t write_text(DummyBuffer, string_view sv) {
    return sv.size();
}
static std::size_t write_text(DummyBuffer, char) {
    return 1;
}

template <typename Output>
static void write_usage_impl(
    Output && output, string_view application_name, std::vector<OptionBase *> const & options) {
    std::string buffer;
    // Header
    {
        write_text(buffer, application_name);
        write_text(buffer, " [options]\n\n");
        flush_buffer(output, buffer);
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
        write_text(buffer, "Options:\n");
        flush_buffer(output, buffer);
        for(const OptionBase * option : options) {
            std::size_t size = write_option_pattern(buffer, *option);
            while(size < help_text_offset) {
                size += write_text(buffer, ' ');
            }
            write_text(buffer, option->help_text); // TODO line wrapping. Use terminal size ?
            write_text(buffer, '\n');
            flush_buffer(output, buffer);
        }
    }
}

void Application::write_usage(std::FILE * out) const {
    write_usage_impl(out, string_view(name_), options_);
}
void Application::write_usage(std::ostream & out) const {
    write_usage_impl(out, string_view(name_), options_);
}

} // namespace ropts