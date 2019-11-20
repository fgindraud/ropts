#include "ropts.h"

#include <charconv> // from_chars (C++17)
#include <cstdio>   // FILE* based IO
#include <ostream>  // std::ostream IO
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

// TODO replace with something else
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
        FormatBuffer error;
        error.push("missing value '");
        error.push(name);
        error.push('\'');
        throw Exception(std::move(error.buffer));
    }
}

string_view ValueTrait<string_view>::parse(CommandLine & state, string_view name) {
    return next_or_fail(state, name);
}

template <typename T>
static T parse_with_from_chars(CommandLine & state, string_view name, string_view type_name) {
    string_view text = next_or_fail(state, name);
    T value;
    std::from_chars_result result = std::from_chars(text.begin(), text.end(), value);
    if(result.ptr == text.end()) {
        return value;
    } else {
        FormatBuffer error;
        error.push("value '");
        error.push(name);
        error.push("' is not a valid ");
        error.push(type_name);
        error.push(": '");
        error.push(text);
        error.push('\'');
        throw Exception(std::move(error.buffer));
    }
}

int ValueTrait<int>::parse(CommandLine & state, string_view name) {
    return parse_with_from_chars<int>(state, name, "integer");
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
    FormatBuffer error;
    error.push("option '");
    error.push(name());
    error.push("' cannot be used more than once");
    throw Exception(std::move(error.buffer));
}
void OptionBase::fail_parsing_error(string_view msg) const {
    FormatBuffer error;
    error.push("option '");
    error.push(name());
    error.push("': ");
    error.push(msg);
    throw Exception(std::move(error.buffer));
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
                    FormatBuffer error;
                    error.push("unknown option name: '");
                    error.push(element);
                    error.push('\'');
                    throw Exception(std::move(error.buffer));
                }
            }
        } else if(enable_option_parsing && element.size() >= 2 && element[0] == '-') {
            // Short option
            if(element.size() > 2) {
                FormatBuffer error;
                error.push("packed short options are not supported: '");
                error.push(element);
                error.push('\'');
                throw Exception(std::move(error.buffer));
            }
            // '-c'
            char option_name = element[1];
            OptionBase * option = find(options_, [option_name](OptionBase * o) {
                return o->short_name() == option_name; //
            });
            if(option != nullptr) {
                option->parse(command_line);
            } else {
                FormatBuffer error;
                error.push("unknown option name: '");
                error.push(element);
                error.push('\'');
                throw Exception(std::move(error.buffer));
            }
        } else {
            // TODO
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
                size += buffer.push(option.short_name());
            }
            if(option.has_short_name() && option.has_long_name()) {
                size += buffer.push(',');
            }
            if(option.has_long_name()) {
                size += buffer.push("--");
                size += buffer.push(option.long_name());
            }
            for(const CowStr & value_name : option.value_names()) {
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