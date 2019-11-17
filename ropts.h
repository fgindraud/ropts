#ifndef ROPTS_INCLUDE_GUARD
#define ROPTS_INCLUDE_GUARD

#include <array>
#include <cassert>
#include <cstdint>    // std::uint32_t in CowStr
#include <cstdio>     // std::FILE
#include <functional> // std::function in OptionBase
#include <string>     // write(string&), std::char_traits in CowStr
#include <vector>

#include <type_traits> // MaybeUninit

#if __cplusplus >= 201703L
#include <optional>
#include <string_view>
namespace ropts {
using std::string_view;
}
#elif __cplusplus >= 201402L
namespace ropts {
// TODO compat c++14

// Tool to implement an optional<T>
template <typename T> class MaybeUninit {
  public:
    // Created as empty, should be empty when destroyed.
    MaybeUninit() = default;
    ~MaybeUninit() = default;

    MaybeUninit(const MaybeUninit &) = delete;
    MaybeUninit & operator=(const MaybeUninit &) = delete;
    MaybeUninit(MaybeUninit &&) = delete;
    MaybeUninit & operator=(MaybeUninit &&) = delete;

    template <typename... Args> void construct(Args &&... args) {
        new(&storage_) T(std::forward<Args>(args)...);
    }
    T & assume_init() { return *reinterpret_cast<T *>(&storage_); }
    void destroy() { assume_init().~T(); }

  private:
    std::aligned_storage_t<sizeof(T), alignof(T)> storage_;
};
} // namespace ropts
#else
#error "Requires at least C++14"
#endif

namespace ropts {
// Slice<T> : reference to const T[n]
template <typename T> struct Slice {
    const T * base{nullptr};
    std::size_t size{0};

    Slice() = default;
    template <std::size_t N> explicit Slice(const std::array<T, N> & a) : base(a.data()), size(N) {}
    explicit Slice(const T & t) : base(&t), size(1) {}

    const T & operator[](std::size_t n) const { return base[n]; }
    const T * begin() const { return base; }
    const T * end() const { return base + size; }
};

/******************************************************************************
 * CowStr:
 * Contains a string which is either a reference, or owned.
 * The string content is not mutable in place.
 * The CowStr can be reassigned a new value.
 * The content can be accessed as a string_view.
 */
class CowStr {
  public:
    enum class Type : bool { Borrowed, Owned };

    constexpr CowStr() noexcept = default;
    ~CowStr() {
        delete_owned();
        state_ = default_state_;
    }

    CowStr(CowStr const &) = delete;
    CowStr & operator=(CowStr const &) = delete;

    CowStr(CowStr && cow) noexcept : state_{cow.state_} { cow.state_ = default_state_; }
    CowStr & operator=(CowStr && cow) noexcept {
        delete_owned();
        state_ = cow.state_;
        cow.state_ = default_state_;
        return *this;
    }

    /// Raw constructor
    CowStr(char const * start, std::size_t size, Type type) noexcept
        : state_{start, static_cast<std::uint32_t>(size), type} {
        assert(size <= UINT32_MAX);
        assert(start != nullptr);
    }

    /// Anything string_view compatible : own a copy by default, safer
    explicit CowStr(string_view s) : CowStr() {
        if(!s.empty()) {
            char * buf = reinterpret_cast<char *>(operator new(s.size() * sizeof(char)));
            std::char_traits<char>::copy(buf, s.data(), s.size());
            state_ = {buf, static_cast<std::uint32_t>(s.size()), Type::Owned};
        }
    }
    CowStr & operator=(string_view s) noexcept { return *this = CowStr(s); }

    /// Borrow from string_view compatible : must be explicit
    static CowStr borrowed(string_view s) { return {s.data(), s.size(), Type::Borrowed}; }

    /// String literal : borrow by default
    template <std::size_t N>
    CowStr(char const (&s)[N]) noexcept : CowStr{&s[0], N - 1, Type::Borrowed} {
        static_assert(N > 0);
        assert(s[N - 1] == '\0'); // Ensure this is a string like object
    }
    template <std::size_t N> CowStr & operator=(char const (&s)[N]) noexcept {
        return *this = CowStr(s);
    }

    // Access
    string_view view() const noexcept { return {state_.start, state_.size}; }
    operator string_view() const noexcept { return view(); }
    char const * data() const noexcept { return state_.start; }
    std::size_t size() const noexcept { return state_.size; }
    Type type() const noexcept { return state_.type; }
    bool empty() const noexcept { return size() == 0; }

  private:
    struct RawState {
        char const * start;
        std::uint32_t size; // 32 bits are sufficient ; struct 30% smaller due to padding
        Type type;
    };
    static constexpr RawState default_state_{"", 0, Type::Borrowed};
    RawState state_{default_state_};

    void delete_owned() noexcept {
        if(state_.type == Type::Owned) {
            operator delete(const_cast<char *>(state_.start));
        }
    }
};

/******************************************************************************
 * Cuts a command line into string_view elements.
 */
class CommandLineParsingState {
  public:
    CommandLineParsingState(int argc, char const * const * argv) : argc_(argc), argv_(argv) {
        assert(argc_ > 0);
    }

    bool read_next_argument() {
        if(next_argument_ < argc_) {
            current_argument_ = string_view(argv_[next_argument_]);
            next_argument_ += 1;
            return true;
        } else {
            return false;
        }
    }

    string_view current_argument() const noexcept { return current_argument_; }

    // Used by parser only
    void set_current_argument(string_view value) noexcept { current_argument_ = value; }

  private:
    // Reference to command line array
    int argc_;
    char const * const * argv_;
    // Iterating state
    string_view current_argument_;
    int next_argument_ = 1;
};

/******************************************************************************
 * Option types.
 */

// Occurrence requirement, must fall between [min,max].
struct Occurrence {
    std::uint16_t min;
    std::uint16_t max;
    std::uint16_t seen = 0;

    constexpr Occurrence(std::uint16_t min_, std::uint16_t max_) noexcept : min(min_), max(max_) {
        assert(min <= max);
    }
};
constexpr Occurrence exactly_once = Occurrence{1, 1};
constexpr Occurrence maybe_once = Occurrence{0, 1};
constexpr Occurrence at_least_once = Occurrence{1, UINT16_MAX};
constexpr Occurrence any_number = Occurrence{0, UINT16_MAX};

struct OptionBase {
    Occurrence occurrence = maybe_once;
    char short_name = '\0';
    CowStr long_name;
    CowStr help_text;
    CowStr doc_text;

    OptionBase() = default;
    explicit OptionBase(char short_name_) : short_name(short_name_) {}
    explicit OptionBase(CowStr long_name_) : long_name(std::move(long_name_)) {}
    OptionBase(char short_name_, CowStr long_name_)
        : short_name(short_name_), long_name(std::move(long_name_)) {}

    virtual ~OptionBase() = default;
    OptionBase(const OptionBase &) = delete;
    OptionBase(OptionBase &&) = delete;
    OptionBase & operator=(const OptionBase &) = delete;
    OptionBase & operator=(OptionBase &&) = delete;

    bool has_short_name() const noexcept { return short_name != '\0'; }

    virtual void parse(CommandLineParsingState & state) = 0;
    virtual Slice<CowStr> usage_value_names() const = 0;

    // FIXME interface for multi args ?
    // Check (required, ...)
};

// Arity
struct Dynamic {
    using ValueNameType = CowStr;
    using CallbackInputType = CommandLineParsingState &;
};
template <std::size_t N> struct Fixed {
    static_assert(N > 0, "Use Flag instead of O-arity option");
    using ValueNameType = std::array<CowStr, N>;
    using CallbackInputType = Slice<string_view>;
};
template <> struct Fixed<1> {
    using ValueNameType = CowStr;
    using CallbackInputType = string_view;
};

template <typename T, typename Arity = Fixed<1>> struct Option final : OptionBase {
    std::optional<T> value; // default value if the optional is filled
    typename Arity::ValueNameType value_name;
    std::function<void(std::optional<T> &, typename Arity::CallbackInputType)> action;

    using OptionBase::OptionBase;

    void parse(CommandLineParsingState & state) override {
        // TODO
    }
    Slice<CowStr> usage_value_names() const override { return Slice<CowStr>{value_name}; }
};

//
struct Flag : OptionBase {
    bool value = false;

    Slice<CowStr> usage_value_names() const override { return Slice<CowStr>{}; }
};

struct OptionGroup {
    enum class Constraint {
        None, // Just a group for usage.
        MutuallyExclusive,
        RequiredAndMutuallyExclusive,
    };

    CowStr name;
    std::vector<OptionBase *> options;
    Constraint constraint = Constraint::None;
};

// Template versions of Optionbase interface will register in a parser.
// They must outlive the parser itself.
// The parser will fill them with values from the parsing step
class Application {
  public:
    Application(CowStr name) : name_(std::move(name)) {}

    void add(OptionBase & option) { options_.emplace_back(&option); }
    void parse(int argc, char const * const * argv);

    void write_usage(FILE * out) const;

  private:
    CowStr name_;
    std::vector<OptionBase *> options_;
    std::vector<OptionGroup *> groups_;

    // TODO positionals = Options without name ?
    // TODO subcommands
};

// TODO use intrusive lists

/******************************************************************************
 * Implementations.
 * TODO system to make them non inline for separate compilation.
 */

inline void Application::parse(int argc, char const * const * argv) {
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

// Dummy set of write primitives, used to measure len only
struct None {};
inline std::size_t write(None, char) {
    return 1;
}
inline std::size_t write(None, std::string_view sv) {
    return sv.size();
}

inline std::size_t write(std::string & out, char c) {
    out.push_back(c);
    return 1;
}
inline std::size_t write(std::string & out, string_view sv) {
    out.append(sv.data(), sv.size());
    return sv.size();
}

inline std::size_t write(FILE * out, char c) {
    std::fputc(c, out);
    return 1;
}
inline std::size_t write(FILE * out, std::string_view sv) {
    return std::fwrite(sv.data(), 1, sv.size(), out);
}

constexpr char spaces_buffer[] = "                                                                ";
constexpr std::size_t spaces_buffer_size = std::size(spaces_buffer) - 1;

template <typename Out> std::size_t write_spaces(Out && out, std::size_t n) {
    while(n >= spaces_buffer_size) {
        n -= write(out, string_view{spaces_buffer, spaces_buffer_size});
    }
    write(out, string_view{spaces_buffer, n});
    return n;
}

inline void Application::write_usage(FILE * out) const {
    // Header
    {
        write(out, name_);
        write(out, " [options]\n\n");
    }
    // Option printing
    {
        auto write_option_pattern = [](auto && out, const OptionBase & option) -> std::size_t {
            std::size_t size = 0;
            size += write(out, "  ");
            if(option.has_short_name()) {
                size += write(out, '-');
                size += write(out, option.short_name);
            }
            if(option.has_short_name() && !option.long_name.empty()) {
                size += write(out, ',');
            }
            if(!option.long_name.empty()) {
                size += write(out, "--");
                size += write(out, option.long_name);
            }
            for(const CowStr & value_name : option.usage_value_names()) {
                size += write(out, ' ');
                size += write(out, value_name);
            }
            return size;
        };
        // Compute max size of an option text up to help_text for alignement.
        std::size_t help_text_offset = 0;
        for(const OptionBase * option : options_) {
            std::size_t option_pattern_len = write_option_pattern(None{}, *option);
            help_text_offset = std::max(help_text_offset, option_pattern_len + 3);
        }
        // Printing
        write(out, "Options:\n");
        for(const OptionBase * option : options_) {
            std::size_t size = write_option_pattern(out, *option);
            write_spaces(out, help_text_offset - size);
            write(out, option->help_text); // TODO line wrapping. Use terminal size ?
            write(out, '\n');
        }
    }
}

} // namespace ropts

#endif