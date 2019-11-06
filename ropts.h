#ifndef ROPTS_INCLUDE_GUARD
#define ROPTS_INCLUDE_GUARD

#include <array>
#include <cassert>
#include <cstdint>    // std::uint32_t in CowStr
#include <functional> // std::function in OptionBase
#include <optional>
#include <string> // std::char_traits in CowStr
#include <vector>

// TODO compat c++14
#include <string_view>
namespace ropts {
using std::string_view;
}

namespace ropts {

/******************************************************************************
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

    /// Always borrow from string literal
    template <std::size_t N>
    explicit CowStr(char const (&s)[N]) noexcept : CowStr{&s[0], N - 1, Type::Borrowed} {
        static_assert(N > 0);
        assert(s[N - 1] == '\0'); // Ensure this is a string like object
    }
    template <std::size_t N> CowStr & operator=(char const (&s)[N]) noexcept {
        return *this = CowStr(s);
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

    // Access
    string_view view() const noexcept { return {state_.start, state_.size}; }
    operator string_view() const noexcept { return view(); }
    char const * data() const noexcept { return state_.start; }
    std::size_t size() const noexcept { return state_.size; }
    Type type() const noexcept { return state_.type; }

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
 *
 */
class CommandLineView {
    // TODO
};

/******************************************************************************
 * Base for option types.
 */
struct OptionBase {
    CowStr short_name;
    CowStr long_name;
    CowStr help_text;
    CowStr doc_text;

    OptionBase() = default;
    virtual ~OptionBase() = default;

    OptionBase(const OptionBase &) = delete;
    OptionBase(OptionBase &&) = delete;
    OptionBase & operator=(const OptionBase &) = delete;
    OptionBase & operator=(OptionBase &&) = delete;

    virtual void parse(CommandLineView & view) = 0;

    // FIXME interface for multi args ?
    // Check (required, ...)
};

// Arity
struct Dynamic {
    using ValueNameType = CowStr;
    using CallbackInputType = CommandLineView;
};
template <std::size_t N> struct Fixed {
    static_assert(N > 0);
    using ValueNameType = std::array<CowStr, N>;
    using CallbackInputType = std::array<string_view, N>;
};
template <> struct Fixed<1> {
    using ValueNameType = CowStr;
    using CallbackInputType = string_view;
};

// TODO occurrence check : optional | required | 0+ | 1+

template <typename T, typename ArityTag = Fixed<1>> struct Option final : OptionBase {
    std::optional<T> value; // default value if the optional is filled
    typename ArityTag::ValueNameType value_name;
    std::function<void(std::optional<T> &, typename ArityTag::CallbackInputType)> from_text;

    void parse(CommandLineView & view) override {
        // TODO
    }
};

//
struct Flag : OptionBase {
    bool value;
};

struct OptionGroup {
    CowStr name;
    std::vector<OptionBase *> options;
};

class Parser {
  public:
    Parser() = default;

    void add(OptionBase * option) { options_.emplace_back(option); }
    void add(OptionBase & option) { add(&option); }

  private:
    std::vector<OptionBase *> options_;
    std::vector<OptionGroup *> groups_;

    std::vector<OptionBase *> positionals_;
    // OR subcommands
};

// Template versions of Optionbase interface will register in a parser.
// They must outlive the parser itself.
// The parser will fill them with values from the parsing step

// TODO use intrusive lists

} // namespace ropts

#endif