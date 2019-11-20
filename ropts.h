#ifndef ROPTS_INCLUDE_GUARD
#define ROPTS_INCLUDE_GUARD

#include <array>
#include <cassert>
#include <cstdint>   // std::uint32_t in CowStr
#include <cstdio>    // std::FILE
#include <exception> // std::exception
#include <iosfwd>    // std::ostream
#include <string>    // std::char_traits in CowStr
#include <tuple>
#include <vector>

// TODO compat C++14 ?
#include <optional>
#include <string_view>
namespace ropts {
using std::string_view;
}

/* Command line parser.
 *
 * Avoids allocations whevener possible.
 * Allocates when:
 * - non literal strings are used.
 * - errors are returned.
 */
namespace ropts {

/// Exception type used to report parsing errors.
struct Exception final : std::exception {
    std::string error;

    Exception() = default;
    Exception(std::string && message) : error(std::move(message)) {}

    char const * what() const noexcept override { return error.data(); }
};

/// Slice<T> : reference to const T[n]
template <typename T> struct Slice {
    const T * base{nullptr};
    std::size_t size{0};

    Slice() noexcept = default;
    template <std::size_t N>
    explicit Slice(std::array<T, N> const & a) noexcept : base(a.data()), size(N) {}
    explicit Slice(T const & t) noexcept : base(&t), size(1) {}

    const T & operator[](std::size_t n) const noexcept { return base[n]; }
    const T * begin() const noexcept { return base; }
    const T * end() const noexcept { return base + size; }
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

static_assert(sizeof(CowStr) == sizeof(string_view));

/******************************************************************************
 * Cuts a command line into string_view elements.
 */
class CommandLine {
  public:
    CommandLine(int argc, char const * const * argv) : argc_(argc), argv_(argv) {
        assert(argc_ > 0);
    }

    bool read_next_element() {
        if(next_argument_ < argc_) {
            current_element_ = string_view(argv_[next_argument_]);
            next_argument_ += 1;
            return true;
        } else {
            return false;
        }
    }

    string_view current_element() const noexcept { return current_element_; }

    // Used by parser only
    void set_current_element(string_view value) noexcept { current_element_ = value; }

  private:
    // Reference to command line array
    int argc_;
    char const * const * argv_;
    // Iterating state
    string_view current_element_;
    int next_argument_ = 1;
};

/******************************************************************************
 * Parsable value types.
 */

template <typename Specification> struct ValueTrait;

template <> struct ValueTrait<int> {
    using NameType = CowStr;
    using ValueType = int;
    static ValueType parse(CommandLine & command_line, NameType const & name);
};

template <typename... Types> struct ValueTrait<std::tuple<Types...>> {
    using NameType = std::array<CowStr, sizeof...(Types)>;
    using ValueType = std::tuple<typename ValueTrait<Types>::ValueType...>;
    static ValueType parse(CommandLine & command_line, NameType const & names);
    // TODO use parse N times with Types. Needs integer sequence
};

/******************************************************************************
 * Option types.
 */

// Base type for options, required by Application.
class OptionBase {
  public:
    // Constructors : require at least one name
    explicit OptionBase(char short_name) noexcept : short_name_(short_name) {
        assert(has_short_name());
    }
    explicit OptionBase(CowStr long_name) noexcept : long_name_(std::move(long_name)) {
        assert(has_long_name());
    }
    OptionBase(char short_name, CowStr long_name) noexcept
        : long_name_(std::move(long_name)), short_name_(short_name) {
        assert(has_short_name());
        assert(has_long_name());
    }

    // Cannot be relocated (would invalidate registration)
    virtual ~OptionBase() = default;
    OptionBase(const OptionBase &) = delete;
    OptionBase(OptionBase &&) = delete;
    OptionBase & operator=(const OptionBase &) = delete;
    OptionBase & operator=(OptionBase &&) = delete;

    // Access option names (not mutable)
    char short_name() const noexcept { return short_name_; }
    bool has_short_name() const noexcept { return short_name_ != '\0'; }
    string_view long_name() const noexcept { return long_name_; }
    bool has_long_name() const noexcept { return !long_name_.empty(); }
    string_view name() const noexcept;

    std::size_t nb_occurrences() const noexcept { return nb_occurrences_; }

    void parse(CommandLine & state) {
        parse_impl(state);
        nb_occurrences_ += 1;
    }

    virtual Slice<CowStr> value_names() const = 0;

  protected:
    virtual void parse_impl(CommandLine & state) = 0;

    [[noreturn]] void fail_option_repeated() const;
    [[noreturn]] void fail_parsing_error(string_view msg) const;

  public:
    // Public properties
    CowStr help_text;
    CowStr doc_text;

  private:
    CowStr long_name_;
    std::uint16_t nb_occurrences_ = 0;
    char short_name_ = '\0'; // '\0' represent invalid
};

/// Flag that can be set once or more
struct Flag final : OptionBase {
    bool value() const noexcept { return nb_occurrences() > 0; }

    Slice<CowStr> value_names() const override { return Slice<CowStr>{}; }
    void parse_impl(CommandLine &) override {}
};

/// Option with value of type T that can be set only once
template <typename T> struct OptionSingle final : OptionBase {
    // TODO required flag ?
    using OptionBase::OptionBase;

    // Can be set to have a default value
    std::optional<typename ValueTrait<T>::ValueType> value;
    typename ValueTrait<T>::NameType value_name;

    Slice<CowStr> value_names() const override { return Slice<CowStr>{value_name}; }

    void parse_impl(CommandLine & state) override {
        if(nb_occurrences() > 0) {
            fail_option_repeated();
        }
        try {
            value = ValueTrait<T>::parse(state, value_name);
        } catch(std::exception const & e) {
            fail_parsing_error(e.what());
        }
    }
};

/// Option that can be called multiple times, storing the results in a vector
template <typename T> struct OptionMultiple final : OptionBase {
    // TODO min/max conditions ?
    using OptionBase::OptionBase;

    std::vector<typename ValueTrait<T>::ValueType> values;
    typename ValueTrait<T>::NameType value_name;

    Slice<CowStr> value_names() const override { return Slice<CowStr>{value_name}; }

    void parse_impl(CommandLine & state) override {
        try {
            values.push_back(ValueTrait<T>::parse(state, value_name));
        } catch(std::exception const & e) {
            fail_parsing_error(e.what());
        }
    }
};

/******************************************************************************
 * Application.
 *
 * TODO intrusive lists ?
 */

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

    // Parse command_line and fills registered options.
    void parse(CommandLine command_line);

    void write_usage(std::FILE * out) const;
    void write_usage(std::ostream & out) const;

  private:
    CowStr name_;
    std::vector<OptionBase *> options_;
    std::vector<OptionGroup *> groups_;

    // TODO positionals = Options without name ?
    // TODO subcommands
};

} // namespace ropts

#endif