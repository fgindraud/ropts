#ifndef ROPTS_INCLUDE_GUARD
#define ROPTS_INCLUDE_GUARD

#include <array>
#include <cassert>
#include <cstdint>    // std::uint32_t in CowStr
#include <cstdio>     // std::FILE
#include <iosfwd>     // std::ostream
#include <string>     // std::char_traits in CowStr
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
// Slice<T> : reference to const T[n]
template <typename T> struct Slice {
    const T * base{nullptr};
    std::size_t size{0};

    Slice() = default;
    template <std::size_t N> explicit Slice(std::array<T, N> const & a) : base(a.data()), size(N) {}
    explicit Slice(T const & t) : base(&t), size(1) {}

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

    virtual void parse(CommandLine & state) = 0;
    virtual Slice<CowStr> usage_value_names() const = 0;

    // FIXME interface for multi args ?
    // Check (required, ...)
};

template <typename ValueSpec> struct ValueTraits;

template <> struct ValueTraits<int> {
    using Type = int;
    using ValueName = CowStr;
    static void parse(int & value, CommandLine & state, ValueName const & name) {
        // Start from global parse function
    }
};

// Arity
struct Dynamic {
    using ValueNameType = CowStr;
    using CallbackInputType = CommandLine &;
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

    using OptionBase::OptionBase;

    void parse(CommandLine & state) override {
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

    // Parse command_line and fills registered options.
    // Return : empty optional on success, or an error message.
    std::optional<std::string> parse(CommandLine command_line);

    void write_usage(std::FILE * out) const;
    void write_usage(std::ostream & out) const;

  private:
    CowStr name_;
    std::vector<OptionBase *> options_;
    std::vector<OptionGroup *> groups_;

    // TODO positionals = Options without name ?
    // TODO subcommands
};

// TODO use intrusive lists

} // namespace ropts

#endif