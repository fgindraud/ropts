#ifndef ROPTS_INCLUDE_GUARD
#define ROPTS_INCLUDE_GUARD

#include <cassert>
#include <new>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace ropts {

/******************************************************************************
 * Contains a string which is either a reference, or owned.
 * The string content is not mutable in place.
 * The CowStr can be reassigned a new value.
 * The content can be accessed as a std::string_view.
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
    CowStr(char const * start, std::size_t size, Type type) noexcept : state_{start, size, type} {
        assert(start != nullptr);
    }

    /// Always borrow from string literal
    template <std::size_t N>
    explicit CowStr(char const (&s)[N]) noexcept : CowStr{&s[0], N - 1, Type::Borrowed} {
        static_assert(N > 0);
        assert(s[N - 1] == '\0');
    }
    template <std::size_t N> CowStr & operator=(char const (&s)[N]) noexcept {
        return *this = CowStr(s);
    }

    /// Anything string_view compatible : own a copy by default, safer
    explicit CowStr(std::string_view s) : CowStr() {
        if(!s.empty()) {
            state_ = copy_view_content(s);
        }
    }
    CowStr & operator=(std::string_view s) noexcept { return *this = CowStr(s); }

    /// Borrow from string_view compatible : must be explicit
    static CowStr borrowed(std::string_view s) { return {s.data(), s.size(), Type::Borrowed}; }

    // Access
    std::string_view view() const noexcept { return {state_.start, state_.size}; }
    operator std::string_view() const noexcept { return view(); }
    char const * data() const noexcept { return state_.start; }
    std::size_t size() const noexcept { return state_.size; }
    Type type() const noexcept { return state_.type; }

  private:
    struct RawState {
        char const * start;
        std::size_t size;
        Type type;
    };
    static constexpr RawState default_state_{"", 0, Type::Borrowed};
    RawState state_{default_state_};

    void delete_owned() noexcept {
        if(state_.type == Type::Owned) {
            operator delete(const_cast<char *>(state_.start));
        }
    }
    static RawState copy_view_content(std::string_view s) {
        char * buf = reinterpret_cast<char *>(operator new(s.size() * sizeof(char)));
        std::char_traits<char>::copy(buf, s.data(), s.size());
        return {buf, s.size(), Type::Owned};
    }
};

struct OptionBase {
    CowStr short_name;
    CowStr long_name;
    CowStr help_text;

    virtual ~OptionBase() = default;
    virtual void parse(std::string_view sv) = 0;

    // FIXME interface for multi args ?
    // Check (required, ...)
    // Value names
    // Doc
};

struct Parser {
    std::vector<OptionBase *> options_;

    std::vector<OptionBase *> positionals_;
    // OR subcommands
};

// Template versions of Optionbase interface will register in a parser.
// They must outlive the parser itself.
// The parser will fill them with values from the parsing step

} // namespace ropts

#endif